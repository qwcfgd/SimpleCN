#include "BusHardware.h"
#include "TosunHardware.h"
#include "VectorHardware.h"
#include <QStringList>
#include <QRegularExpression>
namespace host {
namespace {
class PeakCan final:public CanHardware {
    PCANBasicClass api; tstPeakCan driver; communication::PeakCanBackend backend;
public:
    PeakCan():driver(nullptr,&api),backend(driver){}
    communication::HardwareChannels scan(QString&e)override{return backend.scan(e);}
    bool open(const communication::HardwareChannel&h,const communication::SoftwareChannelConfiguration&c,QString&e)override{return backend.open(h,c,e);}
    bool close(QString&e)override{return backend.close(e);}
    communication::Health health(QString&e)override{return backend.health(e);}
    bool sendRaw(TPCANMsg m)override{return driver.sendRaw(m);}
    DWORD recvRaw(DWORD n,TPCANMsg*m,TPCANTimestamp*t)override{return driver.recvRaw(n,m,t);}
    DWORD recvRawWithTail(DWORD n,TPCANMsg*m,TPCANTimestamp*t,bool &tail)override{return driver.recvRaw(n,m,t,&tail);}
    unsigned long lastErrorCode()const override{return driver.lastErrorCode();}
    QString lastErrorText()const override{return driver.lastErrorText();}
    bool enableEcho()override{return driver.enableEcho();}
    bool setDownloadErrorReception(bool active)override{return driver.setDownloadErrorReception(active);}
    void *receiveEventHandle()const override{return driver.receiveEventHandle();}
    CanBusState classifyStatus(unsigned long status)const override{
        const auto allowed=PCAN_ERROR_ANYBUSERR|PCAN_ERROR_QRCVEMPTY;
        const auto queueErrors=PCAN_ERROR_XMTFULL|PCAN_ERROR_OVERRUN|PCAN_ERROR_QOVERRUN|PCAN_ERROR_QXMTFULL;
        if((status & PCAN_ERROR_BUSOFF) && !(status & ~(allowed|queueErrors)))return CanBusState::BusOff;
        if(status & ~allowed)return CanBusState::Fatal;
        if(status & PCAN_ERROR_ANYBUSERR)return CanBusState::Warning;
        return CanBusState::Ready;
    }
    CanBusState busState()override{
        const auto health=driver.hardwareHealth();
        if(health==communication::Health::Removed||health==communication::Health::Error)return CanBusState::Fatal;
        return classifyStatus(driver.lastErrorCode());
    }
    CanBusState statusFrame(const TPCANMsg &frame)const override{
        if(!(frame.MSGTYPE&PCAN_MESSAGE_STATUS))return CanBusState::Unsupported;
        // PCAN status frames encode the status in DATA[0..3], big endian; ID/DLC are not meaningful.
        const auto status=(quint32(frame.DATA[0])<<24)|(quint32(frame.DATA[1])<<16)|(quint32(frame.DATA[2])<<8)|quint32(frame.DATA[3]);
        return classifyStatus(status);
    }
};
class PeakLin final:public LinHardware {
    tstPeakLin driver;communication::PeakLinBackend backend;
public:
    PeakLin():backend(driver){}
    communication::HardwareChannels scan(QString&e)override{return backend.scan(e);}
    bool open(const communication::HardwareChannel&h,const communication::SoftwareChannelConfiguration&c,QString&e)override{return backend.open(h,c,e);}
    bool close(QString&e)override{return backend.close(e);}
    communication::Health health(QString&e)override{return backend.health(e);}
    LinWakeResult wakeUp(quint64 *since)override{return driver.wakeUp(since)?LinWakeResult::HeaderQueued:LinWakeResult::Failed;}
    bool sendRaw(TLINMsg m)override{return driver.sendRaw(m);}
    DWORD recvRaw(DWORD n,TLINRcvMsg*m)override{return driver.recvRaw(n,m);}
    unsigned long lastErrorCode()const override{return driver.lastErrorCode();}
    QString lastErrorText()const override{return driver.lastErrorText();}
    void setWriteObserver(tstPeakLin::WriteObserver observer)override{driver.writeObserver=std::move(observer);}
    bool clearMsg()override{return driver.clearMsg();}
    bool clearMsgObserved(const tstPeakLin::DrainObserver &observer)override{return driver.clearMsg(observer);}
    int getDevMode()const override{return const_cast<tstPeakLin&>(driver).getDevMode();}
    bool signalConfigureMode(bool master)override{return driver.signalConfigureMode(master);}
    bool signalStop()override{return driver.signalStop();}
    bool signalInstallFrames(const QVector<TLINFrameEntry>&entries)override{return driver.signalInstallFrames(entries);}
    bool signalStartSchedule(QVector<TLINScheduleSlot> entries)override{return driver.signalStartSchedule(std::move(entries));}
    bool signalRequestFrameBoundary()override{return driver.signalRequestFrameBoundary();}
    bool signalFrameBoundaryReached(bool&r)override{return driver.signalFrameBoundaryReached(r);}
    bool signalRequestRoundBoundary()override{return driver.signalRequestRoundBoundary();}
    bool signalRoundBoundaryReached(bool&r)override{return driver.signalRoundBoundaryReached(r);}
    bool signalUpdateFrame(BYTE id,const QByteArray&p)override{return driver.signalUpdateFrame(id,p);}
};
template<class Interface> class Selection {
public:
    std::unique_ptr<Interface> peak,tosun,vector;Interface *active=nullptr;bool opened=false;
    Selection(std::unique_ptr<Interface>a,std::unique_ptr<Interface>b,std::unique_ptr<Interface>c):peak(std::move(a)),tosun(std::move(b)),vector(std::move(c)),active(peak.get()){}
    communication::HardwareChannels scan(QString&e){QString a,b,c;auto result=peak->scan(a);result+=tosun->scan(b);result+=vector->scan(c);e=result.isEmpty()?QStringList{a,b,c}.filter(QRegularExpression(".+")).join("; "):QString();return result;}
    bool open(const communication::HardwareChannel&h,const communication::SoftwareChannelConfiguration&c,QString&e){Interface *next=h.key.startsWith("vector:")?vector.get():h.key.startsWith("tosun:")?tosun.get():peak.get();if(opened&&!close(e))return false;active=next;opened=true;return active->open(h,c,e);}
    bool close(QString&e){const bool ok=active->close(e);if(ok)opened=false;return ok;}
};
#define COMMON_IO \
    communication::HardwareChannels scan(QString&e)override{return choice.scan(e);} \
    bool open(const communication::HardwareChannel&h,const communication::SoftwareChannelConfiguration&c,QString&e)override{return choice.open(h,c,e);} \
    bool close(QString&e)override{return choice.close(e);} \
    communication::Health health(QString&e)override{return choice.active->health(e);} \
    unsigned long lastErrorCode()const override{return choice.active->lastErrorCode();} \
    QString lastErrorText()const override{return choice.active->lastErrorText();}
class CombinedCan final:public CanHardware {
    Selection<CanHardware> choice{std::make_unique<PeakCan>(),createTosunCan(tosun::sharedRuntime()),createVectorCan(vectorxl::sharedRuntime())};
public:
    COMMON_IO
    bool sendRaw(TPCANMsg m)override{return choice.active->sendRaw(m);}
    DWORD recvRaw(DWORD n,TPCANMsg*m,TPCANTimestamp*t)override{return choice.active->recvRaw(n,m,t);}
    DWORD recvRawWithTail(DWORD n,TPCANMsg*m,TPCANTimestamp*t,bool &tail)override{return choice.active->recvRawWithTail(n,m,t,tail);}
    bool enableEcho()override{return choice.active->enableEcho();}
    bool setDownloadErrorReception(bool active)override{return choice.active->setDownloadErrorReception(active);}
    bool isWireError(int index,const TPCANMsg &f)const override{return choice.active->isWireError(index,f);}
    void *receiveEventHandle()const override{return choice.active->receiveEventHandle();}
    CanBusState classifyStatus(unsigned long status)const override{return choice.active->classifyStatus(status);}
    CanBusState busState()override{return choice.active->busState();}
    CanBusState statusFrame(const TPCANMsg &frame)const override{return choice.active->statusFrame(frame);}
};
class CombinedLin final:public LinHardware {
    Selection<LinHardware> choice{std::make_unique<PeakLin>(),createTosunLin(tosun::sharedRuntime()),createVectorLin(vectorxl::sharedRuntime())};
public:
    COMMON_IO
    LinWakeResult wakeUp(quint64 *since)override{return choice.active->wakeUp(since);}
    bool sendRaw(TLINMsg m)override{return choice.active->sendRaw(m);}
    DWORD recvRaw(DWORD n,TLINRcvMsg*m)override{return choice.active->recvRaw(n,m);}
    void setWriteObserver(tstPeakLin::WriteObserver observer)override{choice.active->setWriteObserver(std::move(observer));}
    bool isWireError(int index,const TLINRcvMsg &f)const override{return choice.active->isWireError(index,f);}
    bool isEmptySlot(int index)const override{return choice.active->isEmptySlot(index);}
    bool clearMsg()override{return choice.active->clearMsg();}
    bool clearMsgObserved(const tstPeakLin::DrainObserver &observer)override{return choice.active->clearMsgObserved(observer);}
    int getDevMode()const override{return choice.active->getDevMode();}
    bool signalConfigureMode(bool master)override{return choice.active->signalConfigureMode(master);}
    bool signalConfigureMonitor()override{return choice.active->signalConfigureMonitor();}
    bool signalStop()override{return choice.active->signalStop();}
    bool signalInstallFrames(const QVector<TLINFrameEntry>&f)override{return choice.active->signalInstallFrames(f);}
    bool signalStartSchedule(QVector<TLINScheduleSlot> s)override{return choice.active->signalStartSchedule(std::move(s));}
    bool signalRequestFrameBoundary()override{return choice.active->signalRequestFrameBoundary();}
    bool signalFrameBoundaryReached(bool&r)override{return choice.active->signalFrameBoundaryReached(r);}
    bool signalRequestRoundBoundary()override{return choice.active->signalRequestRoundBoundary();}
    bool signalRoundBoundaryReached(bool&r)override{return choice.active->signalRoundBoundaryReached(r);}
    bool signalUpdateFrame(BYTE id,const QByteArray&p)override{return choice.active->signalUpdateFrame(id,p);}
};
#undef COMMON_IO
class SessionBackend final:public communication::HardwareBackend{
    communication::HardwareBackend &io;
public:
    explicit SessionBackend(communication::HardwareBackend&hardware):io(hardware){}
    communication::Bus bus()const override{return io.bus();}
    communication::HardwareChannels scan(QString&e)override{return io.scan(e);}
    bool open(const communication::HardwareChannel&h,const communication::SoftwareChannelConfiguration&c,QString&e)override{return io.open(h,c,e);}
    bool close(QString&e)override{return io.close(e);}
    communication::Health health(QString&e)override{return io.health(e);}
};
}
std::unique_ptr<CanHardware> createCanHardware(){return std::make_unique<CombinedCan>();}
std::unique_ptr<LinHardware> createLinHardware(){return std::make_unique<CombinedLin>();}
std::unique_ptr<communication::HardwareBackend> sessionBackend(communication::HardwareBackend&hardware){return std::make_unique<SessionBackend>(hardware);}
}
