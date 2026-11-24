#include "VectorHardware.h"
#include "domain/TraceClock.h"
#include "model/SignalCodec.h"
#include <QMutexLocker>
#include <QTimer>
#include <QMap>
#include <cstring>
namespace host {
namespace {
using namespace vectorxl;
class Port {
public:
    std::shared_ptr<Runtime> runtime;Handle handle=-1;Mask mask=0;quint8 index=0;quint32 type=0;int bitrate=0;bool active=false;QString error;
    explicit Port(std::shared_ptr<Runtime> r):runtime(std::move(r)){}
    bool checked(Status s,const QString&op){return runtime->result(s,op,error);}
    bool open(const communication::HardwareChannel&h,const communication::SoftwareChannelConfiguration&c,communication::Bus bus){
        if(handle>=0){error="Vector channel is already open";return false;}
        if(h.bus!=bus||c.bus!=bus||c.bitrate<=0){error="Invalid Vector bus configuration";return false;}
        if(!runtime->acquire(h,handle,mask,index,error))return false;
        type=bus==communication::Bus::Can?1:2;bitrate=c.bitrate;return true;
    }
    bool activate(){QMutexLocker lock(&runtime->mutex);if(active)return true;if(!checked(runtime->api.activate(handle,mask,type,0),"xlActivateChannel"))return false;active=true;return true;}
    bool deactivate(){QMutexLocker lock(&runtime->mutex);if(!active)return true;if(!checked(runtime->api.deactivate(handle,mask),"xlDeactivateChannel"))return false;active=false;return true;}
    bool close(){if(handle<0){error.clear();return true;}const bool off=deactivate();const QString first=error;
        const bool closed=runtime->release(handle,mask,error);if(closed)active=false;if(!off)error=first;return off&&closed;}
    bool receive(Event&event){quint32 count=1;QMutexLocker lock(&runtime->mutex);const auto status=runtime->api.receive(handle,&count,&event);
        if(status==QueueEmpty){error.clear();return false;}if(!checked(status,"xlReceive"))return false;
        if(count!=1){error="Vector XL returned invalid event count";return false;}return true;}
    communication::Health health(QString&e){if(handle<0){e="Vector channel is closed";return communication::Health::Removed;}e=error;return error.isEmpty()?communication::Health::Ready:communication::Health::Error;}
};
class VectorCan final:public CanHardware {
    Port p;unsigned long code=PCAN_ERROR_OK;CanBusState state=CanBusState::Ready;
public:
    explicit VectorCan(std::shared_ptr<Runtime>r):p(std::move(r)){}
    ~VectorCan()override{QString ignored;close(ignored);}
    communication::HardwareChannels scan(QString&e)override{return p.runtime->scan(bus(),e);}
    bool open(const communication::HardwareChannel&h,const communication::SoftwareChannelConfiguration&c,QString&e)override{
        if(!p.open(h,c,bus())){e=p.error;return false;}bool ok;
        {QMutexLocker lock(&p.runtime->mutex);ok=p.checked(p.runtime->api.canBitrate(p.handle,p.mask,quint32(c.bitrate)),"xlCanSetChannelBitrate")&&p.checked(p.runtime->api.canMode(p.handle,p.mask,1,0),"xlCanSetChannelMode");}
        if(ok)ok=p.activate();if(!ok){const QString first=p.error;p.close();p.error=first;}e=p.error;code=ok?PCAN_ERROR_OK:PCAN_ERROR_INITIALIZE;state=CanBusState::Ready;return ok;
    }
    bool close(QString&e)override{const bool ok=p.close();e=p.error;return ok;}
    communication::Health health(QString&e)override{auto h=p.health(e);if(h==communication::Health::Ready&&state==CanBusState::BusOff){e="Vector CAN bus off";return communication::Health::BusWarning;}return h;}
    unsigned long lastErrorCode()const override{return code;}
    QString lastErrorText()const override{return p.error;}
    CanBusState busState()override{return p.handle<0||!p.error.isEmpty()?CanBusState::Fatal:state;}
    bool enableEcho()override{return p.active;}
    bool sendRaw(TPCANMsg msg)override{
        if(!p.active||msg.LEN>8||(msg.MSGTYPE&~(PCAN_MESSAGE_EXTENDED|PCAN_MESSAGE_RTR))||msg.ID>((msg.MSGTYPE&PCAN_MESSAGE_EXTENDED)?0x1fffffffu:0x7ffu)){code=PCAN_ERROR_ILLPARAMVAL;p.error="Invalid classic CAN frame or closed Vector channel";return false;}
        Event event{};event.tag=10;event.data.can.id=msg.ID|((msg.MSGTYPE&PCAN_MESSAGE_EXTENDED)?0x80000000u:0u);event.data.can.dlc=msg.LEN;event.data.can.flags=(msg.MSGTYPE&PCAN_MESSAGE_RTR)?0x10:0;std::memcpy(event.data.can.data,msg.DATA,8);
        quint32 count=1;QMutexLocker lock(&p.runtime->mutex);const bool ok=p.checked(p.runtime->api.transmit(p.handle,p.mask,&count,&event),"xlCanTransmit");
        if(ok&&count!=1)p.error="Vector transmit queue did not accept the frame";code=ok&&count==1?PCAN_ERROR_OK:PCAN_ERROR_XMTFULL;return code==PCAN_ERROR_OK;
    }
    DWORD recvRaw(DWORD capacity,TPCANMsg*out,TPCANTimestamp*stamps)override{
        if(!p.active||(!out&&capacity)){code=PCAN_ERROR_INITIALIZE;return 0;}DWORD n=0;
        for(unsigned budget=0;n<capacity&&budget<1024;++budget){Event event{};if(!p.receive(event))break;if(event.channel!=p.index)continue;
            if(event.tag==4){const quint8 s=event.data.raw[0];state=(s&1)?CanBusState::BusOff:(s&6)?CanBusState::Warning:CanBusState::Ready;continue;}
            if(event.tag!=1)continue;const auto&m=event.data.can;if(m.flags&0x80)continue;
            auto&f=out[n];f={};f.ID=m.id&0x1fffffffu;f.LEN=quint8(qMin<quint16>(m.dlc,8));
            f.MSGTYPE=TPCANMessageType(((m.id&0x80000000u)?PCAN_MESSAGE_EXTENDED:0)|((m.flags&0x10)?PCAN_MESSAGE_RTR:0)|((m.flags&0x40)?PCAN_MESSAGE_ECHO:0)|((m.flags&3)||m.dlc>8||(event.flags&1)?PCAN_MESSAGE_ERRFRAME:0));std::memcpy(f.DATA,m.data,8);
            if(stamps){const quint64 us=event.ns/1000,ms=us/1000;stamps[n].millis=DWORD(ms);stamps[n].millis_overflow=WORD(ms>>32);stamps[n].micros=WORD(us%1000);}++n;
        }code=p.error.isEmpty()?PCAN_ERROR_OK:PCAN_ERROR_UNKNOWN;return n;
    }
};
class VectorLin final:public LinHardware {
    Port p;QTimer timer;int role=modSlave,selectedRole=modMaster;bool monitor=true;quint8 dlcs[64],checksumModes[60];bool tablesValid=false;
    QMap<int,TLINFrameEntry>frames;QVector<TLINScheduleSlot>schedule;int slot=0;bool frameBreak=false,roundBreak=false,suspended=false;qint64 due=0;QString scheduleError;
    bool fail(const QString&e){p.error=e;return false;}
    bool disableResponses(){if(p.handle<0)return fail("Vector LIN channel is closed");QMutexLocker lock(&p.runtime->mutex);
        bool ok=true;QString first;for(int id=0;id<64;++id)if(!p.checked(p.runtime->api.linSwitch(p.handle,p.mask,quint8(id),0),"xlLinSwitchSlave OFF")){if(first.isEmpty())first=p.error;ok=false;}if(!ok)p.error=first;return ok;}
    bool applyTables(const quint8 *lengths,const quint8 *checksums){
        if(tablesValid&&!std::memcmp(dlcs,lengths,64)&&!std::memcmp(checksumModes,checksums,60))return true;
        const bool reactivate=p.active;if(!p.deactivate())return false;
        quint8 nextDlc[64],nextChecksum[60];std::memcpy(nextDlc,lengths,64);std::memcpy(nextChecksum,checksums,60);
        bool ok;{QMutexLocker lock(&p.runtime->mutex);ok=p.checked(p.runtime->api.linDlc(p.handle,p.mask,nextDlc),"xlLinSetDLC")&&p.checked(p.runtime->api.linChecksum(p.handle,p.mask,nextChecksum),"xlLinSetChecksum");}
        tablesValid=ok;if(!ok)return false;std::memcpy(dlcs,nextDlc,64);std::memcpy(checksumModes,nextChecksum,60);
        return !reactivate||p.activate();
    }
    bool setDefinition(quint8 id,quint8 length,quint8 checksum){quint8 nextDlc[64],nextChecksum[60];std::memcpy(nextDlc,dlcs,64);std::memcpy(nextChecksum,checksumModes,60);nextDlc[id]=length;if(id<60)nextChecksum[id]=checksum;return applyTables(nextDlc,nextChecksum);}
    bool switchResponse(quint8 id,bool enabled){QMutexLocker lock(&p.runtime->mutex);return p.checked(p.runtime->api.linSwitch(p.handle,p.mask,id,enabled?0xff:0),"xlLinSwitchSlave");}
    bool configure(int next){if(p.handle<0)return fail("Vector LIN channel is closed");if(!p.deactivate())return false;
        bool ok;{QMutexLocker lock(&p.runtime->mutex);LinParams params{quint32(next==modMaster?1:2),p.bitrate,2,0};ok=p.checked(p.runtime->api.linParams(p.handle,p.mask,params),"xlLinSetChannelParams");}
        if(!ok)return false;role=next;std::memset(dlcs,0xff,sizeof(dlcs));std::memset(checksumModes,0xff,sizeof(checksumModes));tablesValid=false;if(!disableResponses()||!applyTables(dlcs,checksumModes))return false;return p.activate();}
    bool put(const TLINFrameEntry&f,bool enabled){
        if(!switchResponse(f.FrameId,false)||!setDefinition(f.FrameId,f.Length,f.ChecksumType==cstClassic?0:1))return false;
        QMutexLocker lock(&p.runtime->mutex);quint8 data[8];std::memcpy(data,f.InitialData,8);
        if(!p.checked(p.runtime->api.linSlave(p.handle,p.mask,f.FrameId,data,f.Length,(f.FrameId>=60||f.ChecksumType==cstClassic)?0x100:0x200),"xlLinSetSlave"))return false;
        return p.checked(p.runtime->api.linSwitch(p.handle,p.mask,f.FrameId,enabled?0xff:0),"xlLinSwitchSlave");}
    void tick(){if(!timer.isActive()||captureTimeUs()<due)return;
        if(frameBreak||(roundBreak&&slot>=schedule.size())){suspended=true;timer.stop();return;}
        if(slot>=schedule.size())slot=0;const auto&s=schedule[slot++];const auto f=frames.value(s.FrameId[0]);TLINMsg msg{};msg.FrameId=f.FrameId;msg.Length=f.Length;msg.ChecksumType=f.ChecksumType;msg.Direction=(f.Flags&FRAME_FLAG_RESPONSE_ENABLE)?dirPublisher:dirSubscriber;std::memcpy(msg.Data,f.InitialData,8);
        if(!sendRaw(msg)){scheduleError=p.error;timer.stop();return;}due=captureTimeUs()+qint64(s.Delay)*1000;}
public:
    explicit VectorLin(std::shared_ptr<Runtime>r):p(std::move(r)){std::memset(dlcs,0xff,sizeof(dlcs));std::memset(checksumModes,0xff,sizeof(checksumModes));timer.setTimerType(Qt::PreciseTimer);timer.setInterval(1);QObject::connect(&timer,&QTimer::timeout,&timer,[this]{tick();});}
    ~VectorLin()override{QString ignored;close(ignored);}
    communication::HardwareChannels scan(QString&e)override{return p.runtime->scan(bus(),e);}
    bool open(const communication::HardwareChannel&h,const communication::SoftwareChannelConfiguration&c,QString&e)override{
        if(c.bitrate<1000||c.bitrate>20000||(c.linMode!=modMaster&&c.linMode!=modSlave)){e="Invalid LIN bitrate or role";return false;}
        if(!p.open(h,c,bus())){e=p.error;return false;}selectedRole=c.linMode;monitor=true;const bool ok=configure(modSlave);
        if(!ok){const QString first=p.error;p.close();p.error=first;}e=p.error;return ok;}
    bool close(QString&e)override{timer.stop();bool ok=true;if(p.handle>=0)ok=signalStop();const QString first=p.error;const bool closed=p.close();e=ok?p.error:first;return ok&&closed;}
    communication::Health health(QString&e)override{if(!scheduleError.isEmpty()){e=scheduleError;return communication::Health::Error;}return p.health(e);}
    unsigned long lastErrorCode()const override{return p.error.isEmpty()&&scheduleError.isEmpty()?errOK:errUnknown;}
    QString lastErrorText()const override{return scheduleError.isEmpty()?p.error:scheduleError;}
    int getDevMode()const override{return role;}
    bool clearMsg()override{if(p.handle<0)return fail("Vector LIN channel is closed");QMutexLocker lock(&p.runtime->mutex);return p.checked(p.runtime->api.flush(p.handle),"xlFlushReceiveQueue");}
    bool signalStop()override{timer.stop();schedule.clear();frames.clear();frameBreak=roundBreak=suspended=false;monitor=true;scheduleError.clear();if(!disableResponses())return false;return role==modSlave||configure(modSlave);}
    bool signalConfigureMode(bool master)override{if(!signalStop())return false;selectedRole=master?modMaster:modSlave;if(!configure(selectedRole))return false;monitor=false;return true;}
    bool signalConfigureMonitor()override{return signalStop()&&configure(modSlave);}
    LinWakeResult wakeUp()override{
        if(!p.active||monitor||role!=modMaster){fail("Vector LIN wake-up requires an open master channel");return LinWakeResult::Failed;}
        if(!p.runtime->api.linWakeUp)return LinWakeResult::Unsupported;
        QMutexLocker lock(&p.runtime->mutex);
        return p.checked(p.runtime->api.linWakeUp(p.handle,p.mask),"xlLinWakeUp")?LinWakeResult::Sent:LinWakeResult::Failed;
    }
    bool sendRaw(TLINMsg msg)override{
        const quint8 id=msg.FrameId&0x3f;const bool autoLength=msg.Direction==dirSubscriberAutoLength;
        // The application raw API accepts six-bit IDs; PID generation belongs to the SDK.
        if(msg.FrameId>0x3f||!p.active||monitor||id>61||(!autoLength&&(msg.Length<1||msg.Length>8))||(msg.ChecksumType!=cstClassic&&msg.ChecksumType!=cstEnhanced&&!(autoLength&&msg.ChecksumType==cstAuto))||(msg.Direction!=dirPublisher&&msg.Direction!=dirSubscriber&&!autoLength))return fail("Invalid LIN frame, closed Vector channel, or monitor mode");
        if(role==modSlave&&msg.Direction!=dirPublisher)return fail("LIN slave cannot issue a header");
        if(msg.Direction==dirPublisher){TLINFrameEntry f{};f.FrameId=id;f.Length=msg.Length;f.ChecksumType=msg.ChecksumType;std::memcpy(f.InitialData,msg.Data,8);if(!put(f,true))return false;}
        else {if(!switchResponse(id,false)||!setDefinition(id,autoLength?0xff:msg.Length,msg.ChecksumType==cstAuto?0xff:msg.ChecksumType==cstClassic?0:1))return false;}
        if(role==modSlave)return true;QMutexLocker lock(&p.runtime->mutex);return p.checked(p.runtime->api.linRequest(p.handle,p.mask,id,0),"xlLinSendRequest");
    }
    DWORD recvRaw(DWORD capacity,TLINRcvMsg*out)override{
        if(!p.active||(!out&&capacity)){fail("Vector LIN channel is closed or receive buffer is invalid");return 0;}DWORD n=0;
        for(unsigned budget=0;n<capacity&&budget<1024;++budget){Event event{};if(!p.receive(event))break;if(event.channel!=p.index)continue;
            if(event.tag!=20&&event.tag!=21&&event.tag!=22&&event.tag!=23)continue;
            auto&f=out[n++];f={};f.Type=mstStandard;f.TimeStamp=event.ns/1000;
            if(event.tag!=20){f.FrameId=event.data.raw[0]&0x3f;f.ErrorFlags=0x10000;continue;}
            const auto&m=event.data.lin;f.FrameId=m.id&0x3f;f.Length=quint8(qMin<int>(m.dlc,8));f.Direction=(m.flags&0x40)?dirPublisher:dirSubscriber;f.Checksum=m.crc;std::memcpy(f.Data,m.data,8);
            auto checksum=[&](unsigned sum){for(int k=0;k<f.Length;++k){sum+=m.data[k];if(sum>255)sum-=255;}return quint8(~sum);};
            const bool classic=checksum(0)==m.crc,enhanced=f.FrameId<60&&checksum(signal::SignalCodec::linPid(f.FrameId))==m.crc;f.ChecksumType=classic?cstClassic:cstEnhanced;
            if((!classic&&!enhanced)||(m.flags&0x81)||m.dlc>8||(event.flags&1))f.ErrorFlags=0x10000;
        }return n;
    }
    bool signalInstallFrames(const QVector<TLINFrameEntry>&entries)override{
        QMap<int,TLINFrameEntry> next;for(const auto&f:entries){if(f.FrameId>61||f.Length<1||f.Length>8||next.contains(f.FrameId)||(f.ChecksumType!=cstClassic&&f.ChecksumType!=cstEnhanced))return fail("Invalid or duplicate LIN frame definition");next.insert(f.FrameId,f);}
        if(!signalStop()||!configure(selectedRole))return false;
        quint8 dlc[64],checksum[60];std::memset(dlc,0xff,sizeof(dlc));std::memset(checksum,0xff,sizeof(checksum));for(const auto&f:entries){dlc[f.FrameId]=f.Length;if(f.FrameId<60)checksum[f.FrameId]=f.ChecksumType==cstClassic?0:1;}
        bool ok=applyTables(dlc,checksum);
        if(ok)for(const auto&f:entries)if(!put(f,(f.Flags&FRAME_FLAG_RESPONSE_ENABLE)!=0)){ok=false;break;}
        if(!ok){const QString first=p.error;signalStop();p.error=first;return false;}frames=next;monitor=false;return true;
    }
    bool signalStartSchedule(QVector<TLINScheduleSlot>entries)override{
        if(!p.active||monitor||role!=modMaster||entries.isEmpty())return fail("Vector LIN schedule requires configured master mode");
        for(const auto&s:entries){if(s.Type!=sltUnconditional||!frames.contains(s.FrameId[0]))return fail("Only unconditional LIN schedule slots are supported");
            // 34 header + (data + checksum)*10 bits, with 40% LIN timing margin.
            const int bits=34+(frames.value(s.FrameId[0]).Length+1)*10;const int minMs=(bits*1400+p.bitrate-1)/p.bitrate;
            if(s.Delay<qMax(4,minMs))return fail("LIN schedule slot is shorter than its wire-time budget");}
        schedule=std::move(entries);slot=0;frameBreak=roundBreak=suspended=false;due=captureTimeUs();scheduleError.clear();timer.start();return true;
    }
    bool signalRequestFrameBoundary()override{frameBreak=true;if(!timer.isActive())suspended=true;return lastErrorCode()==errOK;}
    bool signalRequestRoundBoundary()override{roundBreak=true;if(!timer.isActive())suspended=true;return lastErrorCode()==errOK;}
    bool signalFrameBoundaryReached(bool&r)override{tick();r=suspended;return lastErrorCode()==errOK;}
    bool signalRoundBoundaryReached(bool&r)override{tick();r=suspended;return lastErrorCode()==errOK;}
    bool signalUpdateFrame(BYTE id,const QByteArray&data)override{
        if(!frames.contains(id)||frames[id].Length!=data.size())return fail("LIN update ID or length does not match installed frame");auto f=frames[id];std::memcpy(f.InitialData,data.constData(),size_t(data.size()));
        if(!put(f,(f.Flags&FRAME_FLAG_RESPONSE_ENABLE)!=0))return false;frames[id]=f;return true;
    }
};
}
std::unique_ptr<CanHardware> createVectorCan(std::shared_ptr<vectorxl::Runtime>r){return std::make_unique<VectorCan>(std::move(r));}
std::unique_ptr<LinHardware> createVectorLin(std::shared_ptr<vectorxl::Runtime>r){return std::make_unique<VectorLin>(std::move(r));}
}
