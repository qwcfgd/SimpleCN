#include "BusHardware.h"
#include "TosunHardware.h"
namespace host {
namespace {
class PeakCan final:public CanHardware {
    PCANBasicClass api; tstPeakCan driver; communication::PeakCanBackend backend;quint32 handle=0;
public:
    PeakCan():driver(nullptr,&api),backend(driver){}
    communication::HardwareChannels scan(QString&e)override{return backend.scan(e);}
    bool open(const communication::HardwareChannel&h,const communication::SoftwareChannelConfiguration&c,QString&e)override{handle=h.handle;return backend.open(h,c,e);}
    bool close(QString&e)override{return backend.close(e);}
    communication::Health health(QString&e)override{return backend.health(e);}
    bool sendRaw(TPCANMsg m)override{return driver.sendRaw(m);}
    DWORD recvRaw(DWORD n,TPCANMsg*m,TPCANTimestamp*t)override{return driver.recvRaw(n,m,t);}
    unsigned long lastErrorCode()const override{return driver.lastErrorCode();}
    QString lastErrorText()const override{return driver.lastErrorText();}
    bool enableEcho()override{DWORD enabled=PCAN_PARAMETER_ON;return api.SetValue(TPCANHandle(handle),PCAN_ALLOW_ECHO_FRAMES,&enabled,sizeof(enabled))==PCAN_ERROR_OK;}
};
class PeakLin final:public LinHardware {
    tstPeakLin driver;communication::PeakLinBackend backend;
public:
    PeakLin():backend(driver){}
    communication::HardwareChannels scan(QString&e)override{return backend.scan(e);}
    bool open(const communication::HardwareChannel&h,const communication::SoftwareChannelConfiguration&c,QString&e)override{return backend.open(h,c,e);}
    bool close(QString&e)override{return backend.close(e);}
    communication::Health health(QString&e)override{return backend.health(e);}
    bool sendRaw(TLINMsg m)override{return driver.sendRaw(m);}
    DWORD recvRaw(DWORD n,TLINRcvMsg*m)override{return driver.recvRaw(n,m);}
    unsigned long lastErrorCode()const override{return driver.lastErrorCode();}
    QString lastErrorText()const override{return driver.lastErrorText();}
    bool clearMsg()override{return driver.clearMsg();}
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
    std::unique_ptr<Interface> peak,tosun;Interface *active=nullptr;
    Selection(std::unique_ptr<Interface>a,std::unique_ptr<Interface>b):peak(std::move(a)),tosun(std::move(b)),active(peak.get()){}
    communication::HardwareChannels scan(QString&e){QString a,b;auto result=peak->scan(a);result+=tosun->scan(b);e=result.isEmpty()&&!a.isEmpty()&&!b.isEmpty()?a+"; "+b:QString();return result;}
    bool open(const communication::HardwareChannel&h,const communication::SoftwareChannelConfiguration&c,QString&e){active=h.key.startsWith("tosun:")?tosun.get():peak.get();return active->open(h,c,e);}
};
#define COMMON_IO \
    communication::HardwareChannels scan(QString&e)override{return choice.scan(e);} \
    bool open(const communication::HardwareChannel&h,const communication::SoftwareChannelConfiguration&c,QString&e)override{return choice.open(h,c,e);} \
    bool close(QString&e)override{return choice.active->close(e);} \
    communication::Health health(QString&e)override{return choice.active->health(e);} \
    unsigned long lastErrorCode()const override{return choice.active->lastErrorCode();} \
    QString lastErrorText()const override{return choice.active->lastErrorText();}
class CombinedCan final:public CanHardware {
    Selection<CanHardware> choice{std::make_unique<PeakCan>(),createTosunCan(tosun::sharedRuntime())};
public:
    COMMON_IO
    bool sendRaw(TPCANMsg m)override{return choice.active->sendRaw(m);}
    DWORD recvRaw(DWORD n,TPCANMsg*m,TPCANTimestamp*t)override{return choice.active->recvRaw(n,m,t);}
    bool enableEcho()override{return choice.active->enableEcho();}
};
class CombinedLin final:public LinHardware {
    Selection<LinHardware> choice{std::make_unique<PeakLin>(),createTosunLin(tosun::sharedRuntime())};
public:
    COMMON_IO
    bool sendRaw(TLINMsg m)override{return choice.active->sendRaw(m);}
    DWORD recvRaw(DWORD n,TLINRcvMsg*m)override{return choice.active->recvRaw(n,m);}
    bool clearMsg()override{return choice.active->clearMsg();}
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
