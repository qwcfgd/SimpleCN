#include "TosunRuntime.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QRegularExpression>
#include <windows.h>
namespace host::tosun {
namespace {
template<typename Ret,typename... Args>
std::function<Ret(Args...)> symbol(HMODULE library,const char *name){
    auto ptr=reinterpret_cast<Ret(__stdcall*)(Args...)>(GetProcAddress(library,name));
    return ptr?std::function<Ret(Args...)>(ptr):std::function<Ret(Args...)>();
}
}
Runtime::Runtime(){
    QStringList folders;
    const auto configured=qEnvironmentVariable("TOSUN_SDK_DIR");
    if(!configured.isEmpty())folders<<configured;
    folders<<QCoreApplication::applicationDirPath()+"/dll/tosun";
    const auto programFiles=qEnvironmentVariable("ProgramFiles(x86)","C:/Program Files (x86)");
    folders<<programFiles+"/TOSUN/TSMaster/"+(sizeof(void*)==8?"bin64":"bin");
    for(const auto &folder:folders){
        const auto path=QDir::toNativeSeparators(QFileInfo(folder+"/libTSCAN.dll").absoluteFilePath());
        if(!QFileInfo::exists(path))continue;
        m_library=LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(m_library){m_path=path;break;}
        m_error=QString("同星运行库加载失败（Windows %1）：%2").arg(GetLastError()).arg(path);
    }
    if(!m_library){if(m_error.isEmpty())m_error="未找到同星 libTSCAN.dll；请安装 TSMaster 或设置 TOSUN_SDK_DIR";return;}
    const auto lib=static_cast<HMODULE>(m_library);
    auto initialize=symbol<void,bool,bool,bool>(lib,"initialize_lib_tscan");
    m_finalize=symbol<void>(lib,"finalize_lib_tscan");
    api.scan=symbol<quint32,quint32*>(lib,"tscan_scan_devices");
    api.info=symbol<quint32,qint32,char**,char**,char**>(lib,"tscan_get_device_info");
    api.connect=symbol<quint32,const char*,quintptr*>(lib,"tscan_connect");
    api.disconnect=symbol<quint32,quintptr>(lib,"tscan_disconnect_by_handle");
    api.configureCan=symbol<quint32,quintptr,int,double,quint32>(lib,"tscan_config_can_by_baudrate");
    api.configureCanController=symbol<quint32,quintptr,int,double,double,int,int,quint32>(lib,"tscan_config_canfd_by_baudrate");
    api.sendCan=symbol<quint32,quintptr,const Can*>(lib,"tscan_transmit_can_async");
    api.readCan=symbol<quint32,quintptr,Can*,qint32*,quint8,quint8>(lib,"tsfifo_receive_can_msgs");
    api.readCanFd=symbol<quint32,quintptr,CanFd*,qint32*,quint8,quint8>(lib,"tsfifo_receive_canfd_msgs");
    api.configureLin=symbol<quint32,quintptr,int,double,quint8>(lib,"tslin_config_baudrate");
    api.linRole=symbol<quint32,quintptr,int,quint8>(lib,"tslin_set_node_functiontype");
    if(!api.linRole)api.linRole=symbol<quint32,quintptr,int,quint8>(lib,"tslin_set_node_funtiontype");
    api.sendLin=symbol<quint32,quintptr,const Lin*>(lib,"tslin_transmit_lin_async");
    api.readLin=symbol<quint32,quintptr,Lin*,qint32*,quint8,quint8>(lib,"tsfifo_receive_lin_msgs");
    api.clearLin=symbol<quint32,quintptr,int>(lib,"tslin_clear_schedule_tables");
    api.resetLin=symbol<quint32,quintptr,int>(lib,"tslin_apply_download_new_ldf");
    api.describe=symbol<quint32,quint32,char**>(lib,"tscan_get_error_description");
    if(!initialize||!m_finalize||!api.scan||!api.info||!api.connect||!api.disconnect||!api.configureCan||!api.sendCan||!api.readCan||!api.configureLin||!api.linRole||!api.sendLin||!api.readLin||!api.clearLin){
        m_error="同星运行库缺少所需 CAN/LIN 接口，请更新完整的 TSMaster 运行库";m_finalize={};return;
    }
    initialize(true,true,true);m_loaded=true;
}
Runtime::Runtime(Functions functions):api(std::move(functions)),m_loaded(true){}
Runtime::~Runtime(){
    if(m_loaded){for(const auto &c:m_connections)api.disconnect(c.handle);if(m_finalize)m_finalize();}
    if(m_library)FreeLibrary(static_cast<HMODULE>(m_library));
}
bool Runtime::result(quint32 code,const QString &operation,QString &error)const{
    if(!code){error.clear();return true;}
    char *description=nullptr;if(api.describe)api.describe(code,&description);
    error=QString("同星 %1 失败（%2）%3").arg(operation).arg(code).arg(description?QString::fromLocal8Bit(description):QString());return false;
}
bool Runtime::refresh(QString &error){
    if(!m_loaded){error=m_error;return false;}
    if(m_scanClock.isValid()&&m_scanClock.elapsed()<250){error.clear();return true;}
    quint32 count=0;if(!result(api.scan(&count),"枚举",error))return false;
    if(count>256){error="同星 SDK 返回异常设备数量";return false;}
    QVector<Device> devices;
    for(quint32 i=0;i<count;++i){
        char *maker=nullptr,*product=nullptr,*serial=nullptr;
        if(!result(api.info(i,&maker,&product,&serial),"设备信息",error))return false;
        if(!serial||!*serial)continue;
        Device d;d.product=QString::fromLocal8Bit(product?product:"TOSUN");d.serial=QString::fromLatin1(serial);
        // USB product descriptors report physical counts without opening the bus.
        const auto can=QRegularExpression("CAN(?:FD)?(\\d+)",QRegularExpression::CaseInsensitiveOption).match(d.product);
        const auto lin=QRegularExpression("LIN(\\d+)",QRegularExpression::CaseInsensitiveOption).match(d.product);
        d.canChannels=can.hasMatch()?can.captured(1).toInt():0;d.linChannels=lin.hasMatch()?lin.captured(1).toInt():0;
        if(!d.canChannels&&!d.linChannels&&d.product.contains("TC1016",Qt::CaseInsensitive)){d.canChannels=4;d.linChannels=2;}
        d.canChannels=qBound(0,d.canChannels,32);d.linChannels=qBound(0,d.linChannels,32);devices.append(d);
    }
    m_devices=devices;m_scanClock.start();error.clear();return true;
}
communication::HardwareChannels Runtime::scan(communication::Bus bus,QString &error){
    QMutexLocker lock(&mutex);communication::HardwareChannels channels;if(!refresh(error))return channels;
    for(const auto &d:m_devices){const int count=bus==communication::Bus::Can?d.canChannels:d.linChannels;
        for(int port=0;port<count;++port){communication::HardwareChannel h;h.bus=bus;h.controller=port+1;h.persistentIdentity=true;
            h.key=QString("tosun:%1:%2:%3").arg(d.serial,bus==communication::Bus::Can?"CAN":"LIN").arg(port);
            if(!m_handles.contains(h.key))m_handles[h.key]=0x70000000u+quint32(m_handles.size()+1);
            h.handle=m_handles.value(h.key);h.label=QString("%1 [%2] / channel %3").arg(d.product,d.serial).arg(port+1);channels.append(h);}
    }return channels;
}
bool Runtime::acquire(const communication::HardwareChannel &port,quintptr &handle,int &index,QString &error){
    QMutexLocker lock(&mutex);if(!refresh(error))return false;const auto serial=port.key.section(':',1,1);bool found=false;
    index=port.key.section(':',3,3).toInt();
    for(const auto &d:m_devices)if(d.serial==serial&&index>=0&&index<(port.bus==communication::Bus::Can?d.canChannels:d.linChannels))found=true;
    if(!found){error="同星设备或通道已移除";return false;}
    auto it=m_connections.find(serial);
    if(it!=m_connections.end()){++it->users;handle=it->handle;return true;}
    handle=0;if(!result(api.connect(serial.toLatin1().constData(),&handle),"连接",error))return false;
    if(!handle){error="同星 SDK 返回空设备句柄";return false;}m_connections[serial]={handle,1};return true;
}
bool Runtime::release(const QString &serial,QString &error){
    QMutexLocker lock(&mutex);auto it=m_connections.find(serial);if(it==m_connections.end()){error.clear();return true;}
    if(--it->users>0){error.clear();return true;}const auto handle=it->handle;m_connections.erase(it);return result(api.disconnect(handle),"断开",error);
}
bool Runtime::present(const QString &serial,QString &error){QMutexLocker lock(&mutex);if(!refresh(error))return false;for(const auto &d:m_devices)if(d.serial==serial)return true;error="同星设备已移除";return false;}
std::shared_ptr<Runtime> sharedRuntime(){static auto runtime=std::make_shared<Runtime>();return runtime;}
}
