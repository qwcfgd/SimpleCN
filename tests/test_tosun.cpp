#include <QtTest>
#include <QQueue>
#include "infrastructure/SignalTransmitter.h"
#include "viewmodels/SignalTransmitViewModel.h"
#include "domain/TraceClock.h"
#include "infrastructure/TosunHardware.h"
#include "model/SignalCodec.h"
using namespace host;
class TosunTest:public QObject {
    Q_OBJECT
    struct Fake {
        int connects=0,disconnects=0,role=2,resets=0;QVector<tosun::Can> canWrites;QVector<tosun::Lin> linWrites;
        QQueue<tosun::Can> canReads;QQueue<tosun::Lin> linReads;
        std::shared_ptr<tosun::Runtime> runtime;
        Fake(){
            tosun::Functions f;
            f.scan=[](quint32*n){*n=1;return 0u;};
            f.info=[](qint32,char**m,char**p,char**s){static char a[]="TOSUN",b[]="TOSUN HS CANFD4.LIN2",c[]="TEST-SERIAL";*m=a;*p=b;*s=c;return 0u;};
            f.connect=[this](const char*,quintptr*h){++connects;*h=0x12345678;return 0u;};
            f.disconnect=[this](quintptr){++disconnects;return 0u;};
            f.configureCan=[](quintptr,int,double,quint32){return 0u;};
            f.configureLin=[](quintptr,int,double,quint8){return 0u;};
            f.linRole=[this](quintptr,int,quint8 r){role=r;return 0u;};
            f.clearLin=[](quintptr,int){return 0u;};
            f.resetLin=[this](quintptr,int){++resets;return 0u;};
            f.sendCan=[this](quintptr,const tosun::Can*m){canWrites.append(*m);return 0u;};
            f.sendLin=[this](quintptr,const tosun::Lin*m){linWrites.append(*m);return 0u;};
            f.readCan=[this](quintptr,tosun::Can*m,qint32*n,quint8,quint8){int count=0;while(count<*n&&!canReads.isEmpty())m[count++]=canReads.dequeue();*n=count;return 0u;};
            f.readLin=[this](quintptr,tosun::Lin*m,qint32*n,quint8,quint8){int count=0;while(count<*n&&!linReads.isEmpty())m[count++]=linReads.dequeue();*n=count;return 0u;};
            runtime=std::make_shared<tosun::Runtime>(f);
        }
    };
private slots:
    void enumerateAndSharedConnection(){
        Fake sdk;QString error;const auto can=sdk.runtime->scan(communication::Bus::Can,error),lin=sdk.runtime->scan(communication::Bus::Lin,error);
        QCOMPARE(can.size(),4);QCOMPARE(lin.size(),2);QVERIFY(can[0].persistentIdentity);QVERIFY(can[0].key!=lin[0].key);
        auto one=createTosunCan(sdk.runtime),three=createTosunCan(sdk.runtime);communication::SoftwareChannelConfiguration config;config.bitrate=500000;
        QVERIFY(one->open(can[0],config,error));QVERIFY(three->open(can[2],config,error));QCOMPARE(sdk.connects,1);
        QVERIFY(one->close(error));QCOMPARE(sdk.disconnects,0);QCOMPARE(three->health(error),communication::Health::Ready);
        QVERIFY(three->close(error));QCOMPARE(sdk.disconnects,1);
    }
    void canWireConversionAndBounds(){
        Fake sdk;QString error;auto can=createTosunCan(sdk.runtime);auto ports=can->scan(error);communication::SoftwareChannelConfiguration c;QVERIFY(can->open(ports[2],c,error));
        TPCANMsg tx={};tx.ID=0x1ABCDE;tx.MSGTYPE=PCAN_MESSAGE_EXTENDED;tx.LEN=8;tx.DATA[0]=0xA5;QVERIFY(can->sendRaw(tx));
        QCOMPARE(sdk.canWrites[0].channel,quint8(2));QCOMPARE(sdk.canWrites[0].properties,quint8(5));
        auto rx=sdk.canWrites[0];rx.properties=4;rx.us=1234567;sdk.canReads.enqueue(rx);
        TPCANMsg out[2]={};TPCANTimestamp stamps[2]={};QCOMPARE(can->recvRaw(2,out,stamps),DWORD(1));QCOMPARE(out[0].ID,tx.ID);QCOMPARE(out[0].DATA[0],BYTE(0xA5));QCOMPARE(stamps[0].millis,DWORD(1234));QCOMPARE(stamps[0].micros,WORD(567));QVERIFY(!(out[0].MSGTYPE&PCAN_MESSAGE_ECHO));
        rx.properties=5;sdk.canReads.enqueue(rx);QCOMPARE(can->recvRaw(2,out,stamps),DWORD(1));QVERIFY(out[0].MSGTYPE&PCAN_MESSAGE_ECHO);
        tx.LEN=9;QVERIFY(!can->sendRaw(tx));QCOMPARE(sdk.canWrites.size(),1);
    }
    void linRolesScheduleAndCleanup(){
        Fake sdk;QString error;auto lin=createTosunLin(sdk.runtime);auto ports=lin->scan(error);communication::SoftwareChannelConfiguration c;c.bus=communication::Bus::Lin;c.bitrate=19200;QVERIFY(lin->open(ports[0],c,error));QCOMPARE(sdk.role,2);
        TLINMsg msg={};msg.FrameId=0x10;msg.Length=2;msg.Direction=dirPublisher;msg.ChecksumType=cstEnhanced;QVERIFY(!lin->sendRaw(msg));
        QVERIFY(lin->signalConfigureMode(true));QVERIFY(lin->signalStop());TLINFrameEntry a={};a.FrameId=0x10;a.Length=2;a.ChecksumType=cstEnhanced;a.Direction=dirPublisher;a.Flags=FRAME_FLAG_RESPONSE_ENABLE;a.InitialData[0]=0x55;
        auto b=a;b.FrameId=0x11;b.Direction=dirSubscriber;b.Flags=0;QVERIFY(lin->signalInstallFrames({a,b}));
        TLINScheduleSlot sa={};sa.Type=sltUnconditional;sa.Delay=10;sa.FrameId[0]=0x10;auto sb=sa;sb.FrameId[0]=0x11;
        QVERIFY(lin->signalStartSchedule({sa,sb}));QVERIFY(lin->signalRequestRoundBoundary());bool ended=false;
        QTRY_VERIFY_WITH_TIMEOUT(lin->signalRoundBoundaryReached(ended)&&ended,500);
        QCOMPARE(sdk.linWrites.size(),2);QCOMPARE(sdk.linWrites[0].properties,quint8(1));QCOMPARE(sdk.linWrites[1].properties,quint8(0));
        QVERIFY(lin->signalStop());QCOMPARE(sdk.role,2);QTest::qWait(25);QCOMPARE(sdk.linWrites.size(),2);
        QVERIFY(lin->signalConfigureMode(false));QVERIFY(lin->signalStop());QVERIFY(lin->signalInstallFrames({a}));QCOMPARE(sdk.role,1);QCOMPARE(sdk.linWrites.size(),3);
        QVERIFY(lin->signalUpdateFrame(0x10,QByteArray::fromHex("6677")));QCOMPARE(sdk.linWrites.last().data[0],quint8(0x66));
        QVERIFY(lin->signalConfigureMonitor());QCOMPARE(sdk.role,2);QVERIFY(sdk.resets>0);
    }
    void classicFramesAndErrorsInFdQueue(){
        Fake sdk;QString error;int reads=0;bool optionsCorrect=false;
        sdk.runtime->api.readCanFd=[&](quintptr,tosun::CanFd *out,qint32 *count,quint8 channel,quint8 direction){
            optionsCorrect=channel==0&&direction==1&&*count>=3;++reads;*count=3;
            out[0].id=0x321;out[0].length=2;out[0].data[0]=0x5a;out[0].us=1234567;
            out[1].id=0xffffffff;out[1].properties=128;out[1].length=1;out[1].data[0]=6;
            out[2].id=0x123;out[2].fdProperties=1;out[2].length=8;
            return 0u;
        };
        bool classicNormal=false;sdk.runtime->api.configureCanController=[&](quintptr,int,double arb,double,int type,int mode,quint32 termination){classicNormal=arb==500&&type==0&&mode==0&&termination==0;return 0u;};
        auto can=createTosunCan(sdk.runtime);auto ports=can->scan(error);communication::SoftwareChannelConfiguration c;c.bitrate=500000;
        QVERIFY(can->open(ports[0],c,error));QVERIFY(classicNormal);
        TPCANMsg out[4]={};TPCANTimestamp stamps[4]={};QCOMPARE(can->recvRaw(4,out,stamps),DWORD(3));
        QVERIFY(optionsCorrect);QCOMPARE(reads,1);QCOMPARE(out[0].ID,DWORD(0x321));QCOMPARE(out[0].DATA[0],BYTE(0x5a));QCOMPARE(stamps[0].micros,WORD(567));
        QVERIFY(!(out[0].MSGTYPE&PCAN_MESSAGE_FD));QVERIFY(out[1].MSGTYPE&PCAN_MESSAGE_ERRFRAME);QCOMPARE(out[1].DATA[0],BYTE(6));
        QVERIFY(out[2].MSGTYPE&PCAN_MESSAGE_FD);
    }
    void linDecodeErrorAndTimestamp(){
        Fake sdk;QString error;auto lin=createTosunLin(sdk.runtime);auto ports=lin->scan(error);communication::SoftwareChannelConfiguration c;c.bitrate=19200;QVERIFY(lin->open(ports[0],c,error));
        tosun::Lin rx;rx.id=0x3d;rx.length=8;rx.us=987654;rx.checksum=0x52;rx.data[0]=0xAB;sdk.linReads.enqueue(rx);
        TLINRcvMsg out[2]={};QCOMPARE(lin->recvRaw(2,out),DWORD(1));QCOMPARE(out[0].TimeStamp,UINT64(987654));QCOMPARE(out[0].ChecksumType,cstClassic);QCOMPARE(out[0].Data[0],BYTE(0xAB));QCOMPARE(out[0].ErrorFlags,0);
        rx.error=7;sdk.linReads.enqueue(rx);QCOMPARE(lin->recvRaw(2,out),DWORD(1));QVERIFY(out[0].ErrorFlags!=0);
    }

    void checksumIndependentOfTransmitMode(){
        Fake sdk;QString error;auto lin=createTosunLin(sdk.runtime);auto ports=lin->scan(error);communication::SoftwareChannelConfiguration c;c.bitrate=19200;
        QVERIFY(lin->open(ports[0],c,error));QVERIFY(lin->signalConfigureMode(true));
        tosun::Lin enhanced;enhanced.id=0x10;enhanced.length=2;enhanced.checksum=0xaf;sdk.linReads.enqueue(enhanced);
        TLINMsg diagnostic={};diagnostic.FrameId=0x3c;diagnostic.Length=8;diagnostic.ChecksumType=cstClassic;diagnostic.Direction=dirPublisher;QVERIFY(lin->sendRaw(diagnostic));
        TLINRcvMsg out={};QCOMPARE(lin->recvRaw(1,&out),DWORD(1));QCOMPARE(out.ChecksumType,cstEnhanced);QCOMPARE(out.ErrorFlags,0);
        auto classic=enhanced;classic.checksum=0xff;sdk.linReads.enqueue(classic);
        diagnostic.FrameId=0x11;diagnostic.ChecksumType=cstEnhanced;QVERIFY(lin->sendRaw(diagnostic));
        QCOMPARE(lin->recvRaw(1,&out),DWORD(1));QCOMPARE(out.ChecksumType,cstClassic);QCOMPARE(out.ErrorFlags,0);
        QVERIFY(lin->signalConfigureMonitor());sdk.linReads.enqueue(enhanced);QCOMPARE(lin->recvRaw(1,&out),DWORD(1));QCOMPARE(out.ChecksumType,cstEnhanced);
        enhanced.checksum=0x01;sdk.linReads.enqueue(enhanced);QCOMPARE(lin->recvRaw(1,&out),DWORD(1));QVERIFY(out.ErrorFlags!=0);
    }
    void replayCleanupFailure_data(){
        QTest::addColumn<int>("mode");QTest::newRow("manual-stop")<<0;QTest::newRow("finite-completion")<<1;QTest::newRow("link-failure")<<2;
    }
    void replayCleanupFailure(){
        QFETCH(int,mode);using namespace host::signal;
        Fake sdk;QString error;auto lin=createTosunLin(sdk.runtime);auto ports=lin->scan(error);communication::SoftwareChannelConfiguration c;c.bitrate=19200;
        QVERIFY(lin->open(ports[0],c,error));
        bool refuseStop=false;sdk.runtime->api.linRole=[&](quintptr,int,quint8 role){if(refuseStop&&role==2)return 7u;sdk.role=role;return 0u;};
        SignalTransmitViewModel vm(Bus::Lin);vm.setAvailability(true,false,19200,1);SignalTransmitter tx;RunStatus status;
        connect(&vm,&SignalTransmitViewModel::startRequested,&tx,[&](TxPlan p){tx.start(p,19200,false,nullptr,lin.get());});
        connect(&tx,&SignalTransmitter::statusChanged,&vm,[&](RunStatus s){status=s;vm.applyStatus(s);});
        connect(&vm,&SignalTransmitViewModel::stopRequested,&tx,&SignalTransmitter::stop);
        TxPlan plan;plan.bus=Bus::Lin;plan.role=LinRole::Slave;plan.replay=true;plan.replayStartUs=captureTimeUs();plan.replayDurationUs=mode==1?100000:10000000;plan.repeatCount=1;
        ReplayRecord r;r.id=0x10;r.length=1;r.payload="A";r.publish=true;plan.replayFrames.append(r);
        QVERIFY(vm.startReplayPlan(plan,error));QTRY_VERIFY_WITH_TIMEOUT(!sdk.linWrites.isEmpty(),500);
        refuseStop=true;
        if(mode==0)vm.stop();else if(mode==2)tx.linkFailed("injected link error");
        QTRY_COMPARE_WITH_TIMEOUT(status.state,RunState::Faulted,1000);QVERIFY(status.cleanupPending);QVERIFY(status.detail.contains("7"));QVERIFY(!status.detail.contains("已完成"));QCOMPARE(sdk.role,1);
        QVERIFY(vm.running());QVERIFY(!vm.canStart());QVERIFY(!vm.canData());
        refuseStop=false;vm.stop();QCOMPARE(status.state,RunState::Stopped);QVERIFY(!status.cleanupPending);QCOMPARE(sdk.role,2);QVERIFY(vm.canStart());
    }

    void linScheduleFailureSurvivesReceivePolling(){
        Fake sdk;QString error;auto lin=createTosunLin(sdk.runtime);auto ports=lin->scan(error);communication::SoftwareChannelConfiguration c;c.bitrate=19200;
        QVERIFY(lin->open(ports[0],c,error));QVERIFY(lin->signalConfigureMode(true));QVERIFY(lin->signalStop());
        TLINFrameEntry frame={};frame.FrameId=0x10;frame.Length=2;frame.ChecksumType=cstEnhanced;
        QVERIFY(lin->signalInstallFrames({frame}));TLINScheduleSlot slot={};slot.Type=sltUnconditional;slot.Delay=10;slot.FrameId[0]=0x10;
        sdk.runtime->api.sendLin=[](quintptr,const tosun::Lin*){return 7u;};
        QVERIFY(lin->signalStartSchedule({slot}));bool boundary=false;QVERIFY(!lin->signalRoundBoundaryReached(boundary));
        TLINRcvMsg received[4]={};QCOMPARE(lin->recvRaw(4,received),DWORD(0));
        QVERIFY(lin->lastErrorCode()!=errOK);QVERIFY(lin->lastErrorText().contains("7"));QVERIFY(!lin->signalRoundBoundaryReached(boundary));
        QVERIFY(lin->signalStop());QCOMPARE(lin->lastErrorCode(),static_cast<unsigned long>(errOK));
    }
};
QTEST_GUILESS_MAIN(TosunTest)
#include "test_tosun.moc"
