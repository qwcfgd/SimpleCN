#include "TosunHardware.h"
#include "domain/TraceClock.h"
#include "model/SignalCodec.h"
#include <QMutexLocker>
#include <QTimer>
#include <cstring>
namespace host {
namespace {
class Port {
public:
    std::shared_ptr<tosun::Runtime> runtime;quintptr handle=0;int index=0,bitrate=0;QString serial,error;
    explicit Port(std::shared_ptr<tosun::Runtime> api):runtime(std::move(api)){}
    bool open(const communication::HardwareChannel &h,int rate){
        if(handle){error="同星通道已经打开";return false;}
        if(!runtime->acquire(h,handle,index,error))return false;serial=h.key.section(':',1,1);bitrate=rate;return true;
    }
    bool close(){if(!handle){error.clear();return true;}handle=0;return runtime->release(serial,error);}
    bool result(quint32 code,const QString &operation){return runtime->result(code,operation,error);}
    communication::Health health(QString &text){if(!handle){text="同星通道未打开";return communication::Health::Removed;}
        if(!runtime->present(serial,text))return communication::Health::Removed;
        if(!error.isEmpty()){text=error;return communication::Health::Error;}text.clear();return communication::Health::Ready;}
};
class TosunCan final:public CanHardware {
    Port p;unsigned long code=PCAN_ERROR_OK;
public:
    explicit TosunCan(std::shared_ptr<tosun::Runtime> api):p(std::move(api)){}
    ~TosunCan()override{QString ignored;close(ignored);}
    communication::HardwareChannels scan(QString &e)override{return p.runtime->scan(bus(),e);}
    bool open(const communication::HardwareChannel &h,const communication::SoftwareChannelConfiguration &c,QString &e)override{
        if(!p.open(h,c.bitrate)){e=p.error;return false;}bool ok;
        {QMutexLocker lock(&p.runtime->mutex);const auto &api=p.runtime->api;
            // Explicitly select classic CAN and normal ACK mode on FD controllers.
            const auto status=api.configureCanController?api.configureCanController(p.handle,p.index,c.bitrate/1000.0,c.bitrate/1000.0,0,0,0):api.configureCan(p.handle,p.index,c.bitrate/1000.0,0);
            ok=p.result(status,"CAN 波特率");}
        if(!ok){const auto error=p.error;p.close();p.error=error;}e=p.error;code=ok?PCAN_ERROR_OK:PCAN_ERROR_UNKNOWN;return ok;
    }
    bool close(QString &e)override{const bool ok=p.close();e=p.error;return ok;}
    communication::Health health(QString &e)override{return p.health(e);}
    bool enableEcho()override{return p.handle!=0;}
    bool sendRaw(TPCANMsg message)override{
        if(!p.handle||message.LEN>8||message.ID>((message.MSGTYPE&PCAN_MESSAGE_EXTENDED)?0x1fffffffu:0x7ffu)||(message.MSGTYPE&PCAN_MESSAGE_FD)){p.error="同星经典 CAN 帧无效或通道未打开";code=PCAN_ERROR_ILLPARAMVAL;return false;}
        tosun::Can out;out.channel=p.index;out.id=message.ID;out.length=message.LEN;
        out.properties=1|((message.MSGTYPE&PCAN_MESSAGE_EXTENDED)?4:0)|((message.MSGTYPE&PCAN_MESSAGE_RTR)?2:0);
        std::memcpy(out.data,message.DATA,8);QMutexLocker lock(&p.runtime->mutex);
        const bool ok=p.result(p.runtime->api.sendCan(p.handle,&out),"CAN 发送");code=ok?PCAN_ERROR_OK:PCAN_ERROR_XMTFULL;return ok;
    }
    DWORD recvRaw(DWORD capacity,TPCANMsg *messages,TPCANTimestamp *stamps)override{
        if(!p.handle){code=PCAN_ERROR_INITIALIZE;p.error="同星通道未打开";return 0;}
        QVector<tosun::CanFd> data(int(qMin(capacity,DWORD(256))));qint32 count=data.size();QMutexLocker lock(&p.runtime->mutex);
        quint32 status=0;
        // TC1016P routes classic frames and controller errors through the FD FIFO.
        if(p.runtime->api.readCanFd)status=p.runtime->api.readCanFd(p.handle,data.data(),&count,quint8(p.index),1);
        else {
            QVector<tosun::Can> classic(count);status=p.runtime->api.readCan(p.handle,classic.data(),&count,quint8(p.index),1);
            if(count>=0&&count<=classic.size())for(int n=0;n<count;++n){const auto &in=classic[n];auto &out=data[n];
                out.channel=in.channel;out.properties=in.properties;out.length=in.length;out.id=in.id;out.us=in.us;std::memcpy(out.data,in.data,8);}
        }
        if(!p.result(status,"CAN 接收")){code=PCAN_ERROR_UNKNOWN;return 0;}
        if(count<0||count>data.size()){code=PCAN_ERROR_UNKNOWN;p.error="同星 SDK 返回异常 CAN 帧数";return 0;}
        code=PCAN_ERROR_OK;
        for(int n=0;n<count;++n){const auto &in=data[n];auto &out=messages[n];out={};out.ID=in.id;out.LEN=in.length;
            out.MSGTYPE=TPCANMessageType(((in.properties&4)?PCAN_MESSAGE_EXTENDED:0)|((in.properties&2)?PCAN_MESSAGE_RTR:0)|((in.properties&1)?PCAN_MESSAGE_ECHO:0)|((in.properties&128)||in.length>8?PCAN_MESSAGE_ERRFRAME:0)|((in.fdProperties&1)?PCAN_MESSAGE_FD:0));
            std::memcpy(out.DATA,in.data,8);
            if(stamps){const auto ms=in.us/1000;stamps[n].millis=DWORD(ms);stamps[n].millis_overflow=WORD(ms>>32);stamps[n].micros=WORD(in.us%1000);}
        }return DWORD(count);
    }
    unsigned long lastErrorCode()const override{return code;}
    QString lastErrorText()const override{return p.error;}
};
class TosunLin final:public LinHardware {
    Port p;QTimer timer;unsigned long code=errOK;int role=2,selectedRole=2,protocol=2;QString scheduleError;
    QMap<int,TLINFrameEntry> frames;QVector<TLINScheduleSlot> schedule;
    int slot=0;bool roundBreak=false,frameBreak=false,suspended=false; qint64 due=0;
    bool checked(quint32 result,const QString &operation){const bool ok=p.result(result,operation);code=ok?errOK:errUnknown;return ok;}
    bool setRole(int next){if(!p.handle){p.error="同星 LIN 通道未打开";code=errIllegalHardwareState;return false;}QMutexLocker lock(&p.runtime->mutex);
        if(!checked(p.runtime->api.linRole(p.handle,p.index,quint8(next)),"LIN 角色"))return false;role=next;return true;}
    bool setProtocol(int next){if(protocol==next)return true;QMutexLocker lock(&p.runtime->mutex);
        if(!checked(p.runtime->api.configureLin(p.handle,p.index,p.bitrate/1000.0,quint8(next)),"LIN 校验协议"))return false;protocol=next;return true;}
    void tick(){
        if(!timer.isActive()||captureTimeUs()<due)return;
        if(frameBreak||(slot>=schedule.size()&&roundBreak)){suspended=true;timer.stop();return;}
        if(slot>=schedule.size())slot=0;
        const auto current=schedule[slot++];const auto f=frames.value(current.FrameId[0]);
        TLINMsg msg={};msg.FrameId=f.FrameId;msg.Length=f.Length;msg.ChecksumType=f.ChecksumType;
        msg.Direction=(f.Flags&FRAME_FLAG_RESPONSE_ENABLE)?dirPublisher:dirSubscriber;std::memcpy(msg.Data,f.InitialData,8);
        if(!sendRaw(msg)){scheduleError=p.error;timer.stop();return;}
        due=captureTimeUs()+qint64(current.Delay)*1000;
    }
public:
    explicit TosunLin(std::shared_ptr<tosun::Runtime> api):p(std::move(api)){
        timer.setTimerType(Qt::PreciseTimer);timer.setInterval(1);QObject::connect(&timer,&QTimer::timeout,&timer,[this]{tick();});
    }
    ~TosunLin()override{QString ignored;close(ignored);}
    communication::HardwareChannels scan(QString &e)override{return p.runtime->scan(bus(),e);}
    bool open(const communication::HardwareChannel &h,const communication::SoftwareChannelConfiguration &c,QString &e)override{
        if(!p.open(h,c.bitrate)){e=p.error;return false;}
        bool ok;{QMutexLocker lock(&p.runtime->mutex);ok=checked(p.runtime->api.configureLin(p.handle,p.index,c.bitrate/1000.0,2),"LIN 波特率");}
        // Opening a channel alone must not activate cached slave responses.
        if(ok)ok=setRole(2);
        if(!ok){const auto error=p.error;p.close();p.error=error;}protocol=2;e=p.error;return ok;
    }
    bool close(QString &e)override{
        timer.stop();bool ok=true;if(p.handle)ok=signalStop();const auto first=p.error;
        const bool disconnected=p.close();e=ok?p.error:first;return ok&&disconnected;
    }
    communication::Health health(QString &e)override{if(!scheduleError.isEmpty()){e=scheduleError;return communication::Health::Error;}return p.health(e);}
    unsigned long lastErrorCode()const override{return scheduleError.isEmpty()?code:errUnknown;}
    QString lastErrorText()const override{return scheduleError.isEmpty()?p.error:scheduleError;}
    int getDevMode()const override{return role==0?modMaster:modSlave;}
    bool signalConfigureMode(bool master)override{selectedRole=master?0:1;return signalStop()&&setRole(selectedRole);}
    bool signalConfigureMonitor()override{selectedRole=2;return signalStop();}
    bool signalStop()override{
        timer.stop();schedule.clear();roundBreak=frameBreak=suspended=false;frames.clear();scheduleError.clear();
        if(!p.handle){code=errIllegalHardwareState;p.error="同星 LIN 通道未打开";return false;}
        // Monitor mode disables responses regardless of the previous role.
        if(!setRole(2))return false;
        QMutexLocker lock(&p.runtime->mutex);
        if(!checked(p.runtime->api.clearLin(p.handle,p.index),"清除 LIN 调度"))return false;
        return !p.runtime->api.resetLin||checked(p.runtime->api.resetLin(p.handle,p.index),"清除 LIN 响应配置");
    }
    bool clearMsg()override{
        TLINRcvMsg data[64];for(int n=0;n<64;++n)if(recvRaw(64,data)<64)return code==errOK;return true;
    }
    bool sendRaw(TLINMsg message)override{
        const int id=message.FrameId&0x3f;
        if(!p.handle||role==2||id>61||message.Length<1||message.Length>8){code=errIllegalFrameConfiguration;p.error="同星 LIN 帧无效，或当前为观测节点";return false;}
        const bool classic=id>=60||message.ChecksumType==cstClassic;
        if(!setProtocol(classic?0:2))return false;
        tosun::Lin out;out.channel=p.index;out.id=id;out.length=message.Length;out.properties=message.Direction==dirPublisher?1:0;
        std::memcpy(out.data,message.Data,8);
        unsigned sum=classic?0:signal::SignalCodec::linPid(quint8(id));for(int n=0;n<out.length;++n){sum+=out.data[n];if(sum>255)sum-=255;}out.checksum=quint8(~sum);
        QMutexLocker lock(&p.runtime->mutex);return checked(p.runtime->api.sendLin(p.handle,&out),"LIN 发送");
    }
    DWORD recvRaw(DWORD capacity,TLINRcvMsg *messages)override{
        if(!p.handle){code=errIllegalHardwareState;return 0;}
        QVector<tosun::Lin> data(qMin(int(capacity),256));qint32 count=data.size();QMutexLocker lock(&p.runtime->mutex);
        if(!checked(p.runtime->api.readLin(p.handle,data.data(),&count,quint8(p.index),1),"LIN 接收"))return 0;
        if(count<0||count>data.size()){code=errUnknown;p.error="同星 SDK 返回异常 LIN 帧数";return 0;}
        for(int n=0;n<count;++n){const auto &in=data[n];auto &out=messages[n];out={};out.Type=mstStandard;out.FrameId=in.id&0x3f;out.Length=in.length;out.Direction=(in.properties&1)?dirPublisher:dirSubscriber;
            out.Checksum=in.checksum;out.ChecksumType=cstClassic;out.TimeStamp=in.us;std::memcpy(out.Data,in.data,8);
            // The receive FIFO can contain frames captured before a protocol change.
            // Determine the checksum from this frame, never the last transmit mode.
            if(out.FrameId<60&&in.length<=8){
                auto checksum=[&](unsigned sum){for(int k=0;k<in.length;++k){sum+=in.data[k];if(sum>255)sum-=255;}return quint8(~sum);};
                const bool classic=checksum(0)==in.checksum;
                const bool enhanced=checksum(signal::SignalCodec::linPid(out.FrameId))==in.checksum;
                out.ChecksumType=classic?cstClassic:cstEnhanced;
                if(!classic&&!enhanced)out.ErrorFlags=0x10000;
            }
            if(in.error||(in.properties&128)||in.length>8)out.ErrorFlags=(0x10000|int(in.error)<<17);
        }return DWORD(count);
    }
    bool signalInstallFrames(const QVector<TLINFrameEntry>&entries)override{
        if(!signalStop())return false;
        for(const auto &entry:entries){if(entry.FrameId>61||entry.Length<1||entry.Length>8||frames.contains(entry.FrameId)){p.error="同星 LIN 响应表无效";code=errIllegalFrameConfiguration;return false;}frames[entry.FrameId]=entry;}
        if(!setRole(selectedRole))return false;
        if(role==1)for(const auto &entry:entries)if(entry.Flags&FRAME_FLAG_RESPONSE_ENABLE){TLINMsg msg={};msg.FrameId=entry.FrameId;msg.Length=entry.Length;msg.Direction=dirPublisher;msg.ChecksumType=entry.ChecksumType;std::memcpy(msg.Data,entry.InitialData,8);if(!sendRaw(msg)){const auto error=p.error;signalStop();p.error=error;code=errUnknown;return false;}}
        return true;
    }
    bool signalStartSchedule(QVector<TLINScheduleSlot> entries)override{
        if(role!=0||entries.isEmpty()){code=errIllegalSchedule;p.error="同星 LIN 调度仅允许主节点启动";return false;}
        for(const auto &entry:entries)if(entry.Type!=sltUnconditional||entry.Delay<4||!frames.contains(entry.FrameId[0])){code=errIllegalSchedule;p.error="同星 LIN 调度槽无效";return false;}
        schedule=std::move(entries);slot=0;due=captureTimeUs();roundBreak=frameBreak=suspended=false;timer.start();return true;
    }
    bool signalRequestFrameBoundary()override{frameBreak=true;return true;}
    bool signalRequestRoundBoundary()override{roundBreak=true;return true;}
    bool signalFrameBoundaryReached(bool &out)override{tick();out=suspended;return lastErrorCode()==errOK;}
    bool signalRoundBoundaryReached(bool &out)override{tick();out=suspended;return lastErrorCode()==errOK;}
    bool signalUpdateFrame(BYTE id,const QByteArray &bytes)override{
        if(!frames.contains(id)||frames[id].Length!=bytes.size()){p.error="同星 LIN 热更新长度或 ID 不匹配";code=errIllegalFrameConfiguration;return false;}
        auto &f=frames[id];std::memcpy(f.InitialData,bytes.constData(),size_t(bytes.size()));
        if(role!=1||!(f.Flags&FRAME_FLAG_RESPONSE_ENABLE))return true;TLINMsg msg={};msg.FrameId=id;msg.Length=f.Length;msg.Direction=dirPublisher;msg.ChecksumType=f.ChecksumType;std::memcpy(msg.Data,f.InitialData,8);return sendRaw(msg);
    }
};
}
std::unique_ptr<CanHardware> createTosunCan(std::shared_ptr<tosun::Runtime> api){return std::make_unique<TosunCan>(std::move(api));}
std::unique_ptr<LinHardware> createTosunLin(std::shared_ptr<tosun::Runtime> api){return std::make_unique<TosunLin>(std::move(api));}
}
