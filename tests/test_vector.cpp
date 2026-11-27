#include <QtTest>
#include <QQueue>
#include <cstring>
#include "infrastructure/VectorHardware.h"
using namespace host;
using namespace host::vectorxl;
class VectorTest:public QObject {
    Q_OBJECT
    struct Fake {
        Config cfg{};bool permission=true,active=false;quint8 dlcs[64]={},checksumModes[60]={};Status bitrateResult=0,activateResult=0,closeResult=0;quint32 accepted=1;
        int opens=0,closes=0,activations=0,deactivations=0,role=0,wakes=0;Status wakeResult=0;Handle wakeHandle=-1;Mask wakeMask=0;Mask lastMask=0;QQueue<Event>rx;QVector<Event>tx;QVector<int>requests;QMap<int,bool>responses;QMap<int,QByteArray>payload;QMap<int,int>checksums;std::shared_ptr<Runtime>runtime;
        Fake(){cfg.count=2;for(int i=0;i<2;++i){auto&c=cfg.channels[i];std::memcpy(c.name,"VN1630A",8);c.hwType=57;c.hwChannel=quint8(i);c.channelIndex=quint8(35+i);c.channelMask=Mask(1)<<(35+i);c.channelBusCapabilities=i==0?0x10001:0x30003;c.serialNumber=12345;}
            Functions f;f.openDriver=[](){return Status(0);};f.closeDriver=[](){return Status(0);};f.config=[this](Config*c){*c=cfg;return Status(0);};
            f.openPort=[this](Handle*h,char*,Mask m,Mask*p,quint32,quint32 v,quint32){++opens;lastMask=m;*h=7;*p=permission?m:0;return Status(v==3?0:1);};
            f.closePort=[this](Handle){++closes;return closeResult;};f.flush=[](Handle){return Status(0);};
            f.activate=[this](Handle,Mask,quint32,quint32){++activations;if(!activateResult)active=true;return activateResult;};f.deactivate=[this](Handle,Mask){++deactivations;active=false;return Status(0);};
            f.receive=[this](Handle,quint32*n,Event*e){if(rx.isEmpty()){*n=0;return QueueEmpty;}*n=1;*e=rx.dequeue();return Status(0);};
            f.canBitrate=[this](Handle,Mask,quint32){return bitrateResult;};f.canMode=[](Handle,Mask,int,int){return Status(0);};
            f.transmit=[this](Handle,Mask,quint32*n,void*e){tx.append(*static_cast<Event*>(e));*n=accepted;return Status(0);};
            f.linParams=[this](Handle,Mask,LinParams p){role=int(p.mode);return Status(0);};f.linDlc=[this](Handle,Mask,quint8*d){if(active)return Status(101);std::memcpy(dlcs,d,64);return Status(0);};f.linChecksum=[this](Handle,Mask,quint8*d){if(active)return Status(101);std::memcpy(checksumModes,d,60);return Status(0);};
            f.linSlave=[this](Handle,Mask,quint8 id,quint8*d,quint8 n,quint16 checksum){payload[id]=QByteArray(reinterpret_cast<char*>(d),n);checksums[id]=checksum;return Status(0);};
            f.linSwitch=[this](Handle,Mask,quint8 id,quint8 on){responses[id]=on!=0;return Status(0);};
            f.linRequest=[this](Handle,Mask,quint8 id,quint32){requests.append(id);return Status(0);};f.linWakeUp=[this](Handle h,Mask m){++wakes;wakeHandle=h;wakeMask=m;return wakeResult;};runtime=std::make_shared<Runtime>(f);
        }
    };
private slots:
    void linReceiveDirectionUsesTxCompletedFlag(){
        Fake f;QString error;auto lin=createVectorLin(f.runtime);communication::SoftwareChannelConfiguration cfg;cfg.bus=communication::Bus::Lin;cfg.bitrate=19200;
        QVERIFY(lin->open(lin->scan(error).first(),cfg,error));
        Event event{};event.tag=20;event.channel=f.cfg.channels[1].channelIndex;event.data.lin.id=0x3c;event.data.lin.dlc=8;event.data.lin.crc=0xff;
        f.rx.enqueue(event);event.data.lin.flags=0x40;f.rx.enqueue(event);event.data.lin.flags=0xc1;f.rx.enqueue(event);
        TLINRcvMsg frames[3]={};QCOMPARE(lin->recvRaw(3,frames),DWORD(3));
        QCOMPARE(frames[0].Direction,BYTE(dirSubscriber));QCOMPARE(frames[1].Direction,BYTE(dirPublisher));QCOMPARE(frames[2].Direction,BYTE(dirPublisher));
        QCOMPARE(frames[0].ErrorFlags,TLINMsgErrors(0));QCOMPARE(frames[1].ErrorFlags,TLINMsgErrors(0));QVERIFY(frames[2].ErrorFlags!=0);
        QVERIFY(lin->close(error));
    }
    void capabilitiesAndStableIdentity(){Fake f;QString e;auto can=f.runtime->scan(communication::Bus::Can,e);auto lin=f.runtime->scan(communication::Bus::Lin,e);QCOMPARE(can.size(),2);QCOMPARE(lin.size(),1);QVERIFY(can[0].persistentIdentity);QVERIFY(can[1].key!=lin[0].key);const auto key=can[0].key;f.cfg.channels[0].channelIndex=50;f.cfg.channels[0].channelMask=Mask(1)<<50;QCOMPARE(f.runtime->scan(communication::Bus::Can,e)[0].key,key);f.cfg.channels[0].channelBusCapabilities=1;QCOMPARE(f.runtime->scan(communication::Bus::Can,e).size(),1);f.cfg.count=65;QVERIFY(f.runtime->scan(communication::Bus::Can,e).isEmpty());QVERIFY(!e.isEmpty());}
    void namespacedHandleForChannelZero(){Fake f;QString e;f.cfg.channels[0].channelIndex=0;f.cfg.channels[0].channelMask=1;auto can=createVectorCan(f.runtime);const auto ports=can->scan(e);QCOMPARE(ports[0].handle,quint32(0x60000001));QVERIFY(ports[0].handle!=0);QVERIFY(ports[0].handle!=1);QVERIFY(ports[0].handle!=2);QVERIFY(ports[0].handle!=0x70000001u);communication::SoftwareChannelConfiguration c;QVERIFY(can->open(ports[0],c,e));QCOMPARE(f.lastMask,Mask(1));}
    void permissionFailureAndCleanup(){Fake f;QString e;auto can=createVectorCan(f.runtime);auto ports=can->scan(e);communication::SoftwareChannelConfiguration c;f.permission=false;QVERIFY(!can->open(ports[0],c,e));QCOMPARE(f.closes,1);QCOMPARE(f.activations,0);f.permission=true;f.bitrateResult=101;QVERIFY(!can->open(ports[0],c,e));QCOMPARE(f.closes,2);f.bitrateResult=0;QVERIFY(can->open(ports[0],c,e));QCOMPARE(f.lastMask,Mask(1)<<35);QVERIFY(can->close(e));QCOMPARE(f.deactivations,1);}
    void exclusivePhysicalLease(){Fake f;QString e;auto a=createVectorCan(f.runtime),b=createVectorCan(f.runtime);auto lin=createVectorLin(f.runtime);auto ports=a->scan(e);communication::SoftwareChannelConfiguration c;QVERIFY(a->open(ports[1],c,e));QVERIFY(!b->open(ports[1],c,e));auto lp=lin->scan(e);c.bus=communication::Bus::Lin;c.bitrate=19200;QVERIFY(!lin->open(lp[0],c,e));QVERIFY(a->close(e));QVERIFY(lin->open(lp[0],c,e));}
    void staleIdentityAndCloseFailure(){Fake f;QString e;auto can=createVectorCan(f.runtime);auto ports=can->scan(e);communication::SoftwareChannelConfiguration c;f.cfg.channels[0].serialNumber=999;QVERIFY(!can->open(ports[0],c,e));QCOMPARE(f.opens,0);f.cfg.channels[0].serialNumber=12345;f.cfg.channels[0].channelIndex=52;f.cfg.channels[0].channelMask=Mask(1)<<52;QVERIFY(can->open(ports[0],c,e));QCOMPARE(f.lastMask,Mask(1)<<52);f.closeResult=101;QVERIFY(!can->close(e));auto other=createVectorCan(f.runtime);QVERIFY(!other->open(ports[0],c,e));f.closeResult=0;QVERIFY(can->close(e));QVERIFY(other->open(ports[0],c,e));}
    void deniedPermissionRetainsFailedClose(){Fake f;QString e;auto can=createVectorCan(f.runtime);const auto port=can->scan(e)[0];communication::SoftwareChannelConfiguration c;f.permission=false;f.closeResult=101;QVERIFY(!can->open(port,c,e));QVERIFY(!can->scan(e)[0].available);f.closeResult=0;QVERIFY(can->close(e));QVERIFY(can->scan(e)[0].available);}
    void canWireAndErrors(){Fake f;QString e;auto can=createVectorCan(f.runtime);auto ports=can->scan(e);communication::SoftwareChannelConfiguration c;QVERIFY(can->open(ports[0],c,e));TPCANMsg msg{};msg.ID=0x123456;msg.LEN=8;msg.MSGTYPE=PCAN_MESSAGE_EXTENDED;msg.DATA[7]=42;QVERIFY(can->sendRaw(msg));QCOMPARE(f.tx[0].data.can.id,quint32(0x80123456));auto event=f.tx[0];event.tag=1;event.channel=35;event.ns=1234567000;event.data.can.flags=0x40;f.rx.enqueue(event);TPCANMsg out{};TPCANTimestamp ts{};QCOMPARE(can->recvRaw(1,&out,&ts),DWORD(1));QCOMPARE(out.ID,msg.ID);QCOMPARE(out.DATA[7],BYTE(42));QVERIFY(out.MSGTYPE&PCAN_MESSAGE_ECHO);QCOMPARE(ts.millis,DWORD(1234));QCOMPARE(ts.micros,WORD(567));msg.LEN=9;QVERIFY(!can->sendRaw(msg));msg.LEN=8;f.accepted=0;QVERIFY(!can->sendRaw(msg));event={};event.tag=4;event.channel=35;event.data.raw[0]=1;f.rx.enqueue(event);can->recvRaw(1,&out,&ts);QCOMPARE(can->busState(),CanBusState::BusOff);}
    void linMonitorRolesAndSchedule(){Fake f;QString e;auto lin=createVectorLin(f.runtime);auto ports=lin->scan(e);communication::SoftwareChannelConfiguration c;c.bus=communication::Bus::Lin;c.bitrate=19200;QVERIFY(lin->open(ports[0],c,e));QCOMPARE(f.role,2);TLINMsg msg{};msg.FrameId=16;msg.Length=2;msg.ChecksumType=cstEnhanced;msg.Direction=dirPublisher;QVERIFY(!lin->sendRaw(msg));QVERIFY(lin->signalConfigureMode(true));QCOMPARE(f.role,1);
        TLINFrameEntry a{};a.FrameId=16;a.Length=2;a.ChecksumType=cstEnhanced;a.Flags=FRAME_FLAG_RESPONSE_ENABLE;a.InitialData[0]=55;auto b=a;b.FrameId=17;b.Flags=0;QVERIFY(lin->signalInstallFrames({a,b}));QVERIFY(f.responses[16]);QVERIFY(!f.responses[17]);QCOMPARE(f.checksums[16],0x200);
        TLINScheduleSlot sa{};sa.Type=sltUnconditional;sa.Delay=10;sa.FrameId[0]=16;auto sb=sa;sb.FrameId[0]=17;QVERIFY(lin->signalStartSchedule({sa,sb}));QVERIFY(lin->signalRequestRoundBoundary());bool reached=false;QTRY_VERIFY_WITH_TIMEOUT(lin->signalRoundBoundaryReached(reached)&&reached,500);QCOMPARE(f.requests,QVector<int>({16,17}));QVERIFY(lin->signalStop());QCOMPARE(lin->getDevMode(),int(modSlave));QCOMPARE(f.role,2);for(bool on:f.responses)QVERIFY(!on);QVERIFY(lin->signalConfigureMode(false));QVERIFY(lin->signalInstallFrames({a}));QVERIFY(lin->signalUpdateFrame(16,QByteArray::fromHex("aabb")));QCOMPARE(f.payload[16],QByteArray::fromHex("aabb"));QVERIFY(!lin->signalStartSchedule({sa}));QVERIFY(lin->close(e));for(bool on:f.responses)QVERIFY(!on);
    }
    void linAutoLengthScanAndIdValidation(){Fake f;QString e;auto lin=createVectorLin(f.runtime);communication::SoftwareChannelConfiguration c;c.bus=communication::Bus::Lin;c.bitrate=19200;QVERIFY(lin->open(lin->scan(e)[0],c,e));QVERIFY(lin->signalConfigureMode(true));TLINMsg msg{};msg.FrameId=16;msg.Length=2;msg.Direction=dirPublisher;msg.ChecksumType=cstEnhanced;QVERIFY(lin->sendRaw(msg));QVERIFY(f.responses[16]);msg.Direction=dirSubscriberAutoLength;msg.ChecksumType=cstAuto;QVERIFY(lin->sendRaw(msg));QVERIFY(!f.responses[16]);QCOMPARE(f.dlcs[16],quint8(0xff));QCOMPARE(f.checksumModes[16],quint8(0xff));QCOMPARE(f.requests.last(),16);const int count=f.requests.size();msg.FrameId=0x50;QVERIFY(!lin->sendRaw(msg));QCOMPARE(f.requests.size(),count);msg.FrameId=16;msg.Direction=dirSubscriber;msg.Length=2;msg.ChecksumType=cstClassic;QVERIFY(lin->sendRaw(msg));QCOMPARE(f.dlcs[16],quint8(2));QCOMPARE(f.checksumModes[16],quint8(0));const int activated=f.activations;QVERIFY(lin->sendRaw(msg));QCOMPARE(f.activations,activated);}
    void linWakeChecksRoleOwnershipErrorsAndOptionalExport(){
        Fake f;QString e;auto lin=createVectorLin(f.runtime);communication::SoftwareChannelConfiguration c;c.bus=communication::Bus::Lin;c.bitrate=19200;
        QCOMPARE(lin->wakeUp(),LinWakeResult::Failed);QVERIFY(lin->open(lin->scan(e)[0],c,e));QCOMPARE(lin->wakeUp(),LinWakeResult::Failed);QCOMPARE(f.wakes,0);
        QVERIFY(lin->signalConfigureMode(true));QCOMPARE(lin->wakeUp(),LinWakeResult::PulseQueued);QCOMPARE(f.wakeHandle,Handle(7));QCOMPARE(f.wakeMask,f.lastMask);QCOMPARE(f.wakes,1);QVERIFY(f.requests.isEmpty());
        Event wake{};wake.tag=24;wake.channel=f.cfg.channels[1].channelIndex;f.rx.enqueue(wake); // external
        wake.data.raw[0]=1;--wake.channel;f.rx.enqueue(wake); // other channel
        TLINRcvMsg event{};QCOMPARE(lin->recvRaw(1,&event),DWORD(0));QCOMPARE(lin->wakeUp(),LinWakeResult::Failed);
        ++wake.channel;f.rx.enqueue(wake);QCOMPARE(lin->recvRaw(1,&event),DWORD(1));QCOMPARE(event.Type,BYTE(mstBusWakeUp));
        f.wakeResult=101;QCOMPARE(lin->wakeUp(),LinWakeResult::Failed);QVERIFY(lin->lastErrorText().contains("xlLinWakeUp"));
        f.runtime->api.linWakeUp={};QCOMPARE(lin->wakeUp(),LinWakeResult::Unsupported);QCOMPARE(f.wakes,2);
        TLINMsg msg{};msg.FrameId=61;msg.Direction=dirSubscriber;msg.Length=8;msg.ChecksumType=cstClassic;QVERIFY(lin->sendRaw(msg));QCOMPARE(f.requests.last(),61);
    }
    void linDiagnosticClassicAndBounds(){Fake f;QString e;auto lin=createVectorLin(f.runtime);communication::SoftwareChannelConfiguration c;c.bus=communication::Bus::Lin;c.bitrate=19200;QVERIFY(lin->open(lin->scan(e)[0],c,e));QVERIFY(lin->signalConfigureMode(true));TLINMsg msg{};msg.FrameId=60;msg.Length=8;msg.ChecksumType=cstEnhanced;msg.Direction=dirPublisher;QVERIFY(lin->sendRaw(msg));QCOMPARE(f.checksums[60],0x100);QCOMPARE(f.requests.size(),1);msg.Direction=dirSubscriber;QVERIFY(lin->sendRaw(msg));QVERIFY(!f.responses[60]);msg.Direction=dirSubscriberAutoLength;QVERIFY(lin->sendRaw(msg));QCOMPARE(f.requests.size(),3);QCOMPARE(f.dlcs[60],quint8(0xff));TLINFrameEntry frame{};frame.FrameId=16;frame.Length=8;frame.ChecksumType=cstClassic;QVERIFY(lin->signalInstallFrames({frame}));TLINScheduleSlot slot{};slot.Type=sltUnconditional;slot.FrameId[0]=16;slot.Delay=4;QVERIFY(!lin->signalStartSchedule({slot}));}
};
QTEST_GUILESS_MAIN(VectorTest)
#include "test_vector.moc"
