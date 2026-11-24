#include "VectorRuntime.h"
#include "communication/DriverLibrary.h"
#include <QCoreApplication>
#include <QDir>
#include <QMutexLocker>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace host::vectorxl {
Runtime::Runtime(){
#ifdef Q_OS_WIN
    path=communication::driverLibraryPath("vector",sizeof(void*)==8?"vxlapi64.dll":"vxlapi.dll");
    auto lib=LoadLibraryExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    library=lib;
    if(!lib){error=QString("Vector XL library unavailable: %1 (Windows %2). Install Vector hardware driver and package the matching XL DLL.").arg(path).arg(GetLastError());return;}
#define XL_BIND(field,name,...) api.field=reinterpret_cast<Status(__stdcall*)(__VA_ARGS__)>(GetProcAddress(lib,name))
    XL_BIND(openDriver,"xlOpenDriver");XL_BIND(closeDriver,"xlCloseDriver");XL_BIND(config,"xlGetDriverConfig",Config*);
    XL_BIND(openPort,"xlOpenPort",Handle*,char*,Mask,Mask*,quint32,quint32,quint32);
    XL_BIND(closePort,"xlClosePort",Handle);XL_BIND(flush,"xlFlushReceiveQueue",Handle);
    XL_BIND(activate,"xlActivateChannel",Handle,Mask,quint32,quint32);XL_BIND(deactivate,"xlDeactivateChannel",Handle,Mask);
    XL_BIND(receive,"xlReceive",Handle,quint32*,Event*);
    XL_BIND(canBitrate,"xlCanSetChannelBitrate",Handle,Mask,quint32);XL_BIND(canMode,"xlCanSetChannelMode",Handle,Mask,int,int);
    XL_BIND(transmit,"xlCanTransmit",Handle,Mask,quint32*,void*);
    XL_BIND(linParams,"xlLinSetChannelParams",Handle,Mask,LinParams);XL_BIND(linDlc,"xlLinSetDLC",Handle,Mask,quint8*);
    XL_BIND(linChecksum,"xlLinSetChecksum",Handle,Mask,quint8*);
    XL_BIND(linSlave,"xlLinSetSlave",Handle,Mask,quint8,quint8*,quint8,quint16);
    XL_BIND(linSwitch,"xlLinSwitchSlave",Handle,Mask,quint8,quint8);XL_BIND(linRequest,"xlLinSendRequest",Handle,Mask,quint8,quint32);
    XL_BIND(linWakeUp,"xlLinWakeUp",Handle,Mask);
#undef XL_BIND
    initialize();
#else
    error="Vector XL is supported on Windows only";
#endif
}
Runtime::Runtime(Functions functions):api(std::move(functions)){initialize();}
bool Runtime::initialize(){
    if(!api.openDriver||!api.closeDriver||!api.config||!api.openPort||!api.closePort||!api.activate||!api.deactivate||!api.receive||!api.flush){error="Incomplete Vector XL API";return false;}
    loaded=result(api.openDriver(),"xlOpenDriver",error);return loaded;
}
Runtime::~Runtime(){if(loaded)api.closeDriver();
#ifdef Q_OS_WIN
    if(library)FreeLibrary(static_cast<HMODULE>(library));
#endif
}
bool Runtime::result(Status status,const QString&op,QString&e)const {if(!status){e.clear();return true;}e=QString("Vector %1 failed (XL status %2)").arg(op).arg(status);return false;}
communication::HardwareChannels Runtime::enumerate(communication::Bus bus,Config&cfg,QString&e){
    communication::HardwareChannels out;if(!loaded){e=error;return out;}
    if(!result(api.config(&cfg),"xlGetDriverConfig",e))return out;
    if(cfg.count>64){e="Vector XL returned invalid channel count";return out;}
    const quint32 type=bus==communication::Bus::Can?1:2;
    const bool supported=type==1?bool(api.canBitrate&&api.canMode&&api.transmit):bool(api.linParams&&api.linDlc&&api.linChecksum&&api.linSlave&&api.linSwitch&&api.linRequest);
    if(!supported){e="Vector XL library lacks required bus functions";return out;}
    for(quint32 i=0;i<cfg.count;++i){const auto &c=cfg.channels[i];
        // Physical compatible channels with an active transceiver capability only.
        if(!c.hwType||c.hwType==1||c.configError||c.channelIndex>=64||c.channelMask!=(Mask(1)<<c.channelIndex)||!(c.channelBusCapabilities&type)||!(c.channelBusCapabilities&(type<<16)))continue;
        communication::HardwareChannel h;h.bus=bus;h.handle=0x60000000u+quint32(c.channelIndex)+1;h.controller=c.hwChannel+1;h.deviceId=c.serialNumber;
        const QString identity=c.serialNumber?QString::number(c.serialNumber):QString("runtime%1").arg(c.hwIndex);
        h.key=QString("vector:%1:%2:%3:%4").arg(c.hwType).arg(identity).arg(type==1?"can":"lin").arg(c.hwChannel);
        h.persistentIdentity=c.serialNumber!=0;h.available=!leased.contains(c.channelMask);
        h.label=QString("Vector %1 [%2] %3%4").arg(QString::fromLocal8Bit(QByteArray(c.name,32).split(char(0)).first())).arg(identity).arg(type==1?"CAN":"LIN").arg(c.hwChannel+1);
        out.append(h);
    }return out;
}
communication::HardwareChannels Runtime::scan(communication::Bus bus,QString&e){QMutexLocker lock(&mutex);Config cfg{};return enumerate(bus,cfg,e);}
bool Runtime::acquire(const communication::HardwareChannel&h,Handle&port,Mask&mask,quint8&index,QString&e){
    QMutexLocker lock(&mutex);Config cfg{};const auto channels=enumerate(h.bus,cfg,e);if(!e.isEmpty())return false;
    bool found=false;for(const auto &c:channels)if(c.key==h.key){index=quint8(c.handle-0x60000000u-1);found=true;break;}
    if(!found){e="Vector selected channel is no longer present";return false;}
    mask=Mask(1)<<index;if(leased.contains(mask)){e="Vector physical channel is already in use";return false;}
    Mask permission=mask;char name[]="GeneralController";Handle opened=-1;
    if(!result(api.openPort(&opened,name,mask,&permission,4096,3,h.bus==communication::Bus::Can?1:2),"xlOpenPort",e))return false;
    if(opened<0||(permission&mask)!=mask){e="Vector channel initialization permission denied; close other users of this channel";if(opened>=0){const auto status=api.closePort(opened);if(status){port=opened;leased.insert(mask);e+=QString("; xlClosePort failed (%1), close this session before retrying").arg(status);}}return false;}
    port=opened;leased.insert(mask);return true;
}
bool Runtime::release(Handle&port,Mask mask,QString&e){QMutexLocker lock(&mutex);if(port<0){e.clear();return true;}
    if(!result(api.closePort(port),"xlClosePort",e))return false;
    leased.remove(mask);port=-1;return true;
}
std::shared_ptr<Runtime> sharedRuntime(){static auto runtime=std::make_shared<Runtime>();return runtime;}
}
