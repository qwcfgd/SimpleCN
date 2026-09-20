#include "ChannelViewModel.h"
#include <QDir>
#include <QSaveFile>
#include <QCoreApplication>
#include "protocol/FlashJob.h"
#include "domain/CddConfiguration.h"
namespace host {
using namespace communication;
ChannelViewModel::ChannelViewModel(ChannelSettings settings,QObject *parent):
    QObject(parent),m_settings(std::move(settings)),m_model(new ChannelModel(m_settings,this)),m_frames(this),m_backendSimulation(m_settings.simulation) {
    m_signals=new SignalTransmitViewModel(m_settings.bus==Bus::Can?signal::Bus::Can:signal::Bus::Lin,this);
    connect(m_signals,&SignalTransmitViewModel::startRequested,m_model,&ChannelModel::signalsRequested);
    connect(m_signals,&SignalTransmitViewModel::stopRequested,m_model,&ChannelModel::signalStopRequested);
    connect(m_signals,&SignalTransmitViewModel::payloadRequested,m_model,&ChannelModel::signalPayloadRequested);
    connect(m_signals,&SignalTransmitViewModel::scheduleRequested,m_model,&ChannelModel::signalScheduleRequested);
    connect(m_model,&ChannelModel::signalStatus,m_signals,&SignalTransmitViewModel::applyStatus);
    connect(m_model,&ChannelModel::busEvents,m_signals,&SignalTransmitViewModel::receive);
    connect(m_model,&ChannelModel::observedFrames,m_signals,&SignalTransmitViewModel::observeFrames);
    connect(&m_frames,&FrameTableModel::cleared,m_signals,&SignalTransmitViewModel::clearTrace);
    connect(m_signals,&SignalTransmitViewModel::structureChanged,this,[this]{m_frames.setDatabase(m_signals->database());});
    connect(m_model,&ChannelModel::connectionGenerationChanged,this,[this](quint64 generation){m_connectionGeneration=generation;});
    connect(m_signals,&SignalTransmitViewModel::notice,this,&ChannelViewModel::log);
    connect(m_signals,&SignalTransmitViewModel::changed,this,[this]{emit changed();});
    connect(this,&ChannelViewModel::changed,this,[this]{m_signals->setAvailability(connected(),otherTaskBusy(),m_settings.bitrate,m_connectionGeneration);});
    m_diagnosticRepeatTimer.setSingleShot(true);
    m_diagnosticRepeatTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_diagnosticRepeatTimer,&QTimer::timeout,this,[this]{
        if(!m_repeatActive)return;
        if(!connected()||m_pending){cancel();return;}
        --m_repeatRemaining;m_diagnosticBusy=true;
        m_diagnosticResult=QString("自动重发：第 %1/%2 次发送，等待 ECU 响应…").arg(m_repeatCompleted+1).arg(m_repeatTotal);
        m_model->diagnosticRequested(m_diagnosticSettings,m_lastDiagnostic);emit changed();
    });
    connect(m_model,&ChannelModel::bindingChanged,this,[this](QString key,quint32 handle){
        m_settings.hardwareKey=key;m_settings.handle=handle;emit settingsChanged();
    });
    connect(m_model,&ChannelModel::ready,this,[this](){m_ready=true;emit changed();});
    connect(m_model,&ChannelModel::hardwareChanged,this,[this](HardwareChannels list){m_hardware=std::move(list);selectAvailableHardware();emit hardwareListChanged();emit changed();});
    connect(m_model,&ChannelModel::stateChanged,this,[this](ConnectionState state,QString detail){
        if(state==ConnectionState::Connected){m_wasConnected=true;m_lost=false;m_manualDisconnect=false;}
        else if(m_wasConnected && !m_manualDisconnect && (state==ConnectionState::Missing || state==ConnectionState::Fault))m_lost=true;
        m_state=state;m_error=state==ConnectionState::Fault?detail:QString();
        if(state!=ConnectionState::Connected){
            m_health=Health::Removed;m_healthDetail=connectionText(state);
            if(m_repeatActive){m_repeatActive=false;m_diagnosticRepeatTimer.stop();log("自动重发已停止：连接关闭");}
        }
        emit changed();
    });
    connect(m_model,&ChannelModel::healthChanged,this,[this](Health h,QString detail){if(m_health!=h && !detail.isEmpty())log(detail);
        m_health=h;m_healthDetail=std::move(detail);emit changed();});
    connect(m_model,&ChannelModel::framesReceived,&m_frames,&FrameTableModel::append);
    connect(m_model,&ChannelModel::logMessage,this,&ChannelViewModel::log);
    connect(m_model,&ChannelModel::taskChanged,this,[this](TaskState state,int progress,QString text){
        m_task=state;m_progress=progress;m_taskText=std::move(text);emit changed();
    });
    connect(m_model,&ChannelModel::scanChanged,this,[this](bool scanning,QString text){
        m_scanning=scanning;m_scanText=std::move(text);emit changed();
    });
    connect(m_model,&ChannelModel::commandFinished,this,[this](){m_pending=false;m_connecting=false;selectAvailableHardware();emit changed();});
    connect(m_model,&ChannelModel::diagnosticActivity,this,[this](bool active){m_protocolBusy=active;emit changed();});
    connect(m_model,&ChannelModel::diagnosticFinished,this,[this](bool ok,QByteArray response,QString detail){
        m_diagnosticBusy=false;
        if(ok&&!response.isEmpty()&&m_lastDiagnostic.service.response.issue.isEmpty()){
            diag::Values values;QString error;
            if(!diag::Codec::decode(m_lastDiagnostic.service.response,response,values,error)){ok=false;detail="正响应不符合 CDD 定义："+error;}
        }
        m_diagnosticResult=QString("%1\nTX  %2\nRX  %3\n%4\n%5")
            .arg(ok?"请求完成":"请求失败",QString::fromLatin1(m_lastDiagnostic.bytes.toHex(' ')).toUpper(),
                 QString::fromLatin1(response.toHex(' ')).toUpper(),detail,
                 response.isEmpty()?QString():diag::Codec::describeResponse(m_lastDiagnostic.service,response));
        if(m_repeatActive){
            ++m_repeatCompleted;
            m_diagnosticResult.prepend(QString("自动重发 · 第 %1/%2 次发送\n").arg(m_repeatCompleted).arg(m_repeatTotal));
            if(ok&&m_repeatRemaining>0&&connected()&&!m_pending)m_diagnosticRepeatTimer.start(m_diagnosticSettings.udsRepeatDelayMs);
            else {m_repeatActive=false;m_diagnosticRepeatTimer.stop();m_diagnosticResult+=ok?"\n自动重发完成":"\n自动重发已停止：请求失败";}
        }
        log(m_diagnosticResult);emit diagnosticFinished(ok,response,detail);emit changed();
    });
    if(!m_settings.cddPath.isEmpty())QTimer::singleShot(0,this,[this]{QString error;if(!loadCdd(m_settings.cddPath,error,false)){m_diagnosticResult=error;log(error);emit changed();}});
    if(!m_settings.signalConfiguration.isEmpty())QTimer::singleShot(0,this,[this]{m_signals->readConfigurationAsync(m_settings.signalConfiguration,{});});
}
ChannelSettings ChannelViewModel::snapshotSettings()const{auto snapshot=m_settings;snapshot.signalConfiguration=m_signals->configuration();return snapshot;}
bool ChannelViewModel::setSettings(const ChannelSettings &s) {
    if(busy() || s.bus!=m_settings.bus)return false;
    auto old=m_settings.toJson(),next=s.toJson();
    old.remove("signalConfiguration");next.remove("signalConfiguration");
    for(const auto &key:{"flashPath","applicationPath","flashAddress","applicationAddress","flashRequired","repeatDownloadEnabled","repeatDownloadCount","repeatDownloadIntervalMs","downloadProfile","cddPath","cddEcu","cddVariant","udsRepeatCount","udsRepeatDelayMs","udsRepeatEnabled"}){old.remove(key);next.remove(key);}
    const bool configurationChanged=old!=next;
    if(connected())for(const auto &key:{"softwareId","hardwareKey","handle","bitrate","simulation","autoReconnect"})if(old.value(key)!=next.value(key))return false;
    SoftwareChannelConfiguration config;QString error;
    if(!s.toConfiguration(config,error)){m_error=error;emit changed();return false;}
    const bool modeChanged=m_settings.simulation!=s.simulation;
    m_settings=s;
    if(modeChanged && s.hardwareKey==old.value("hardwareKey").toString()){m_settings.hardwareKey.clear();m_settings.handle=0;}
    const bool valid=true;m_error.clear();
    if(configurationChanged)m_model->resetDiagnosticRequested();
    if(valid && (m_backendSimulation!=m_settings.simulation || (m_lost && configurationChanged))){m_backendSimulation=m_settings.simulation;m_pending=true;m_model->settingsRequested(m_settings);}
    if(modeChanged){m_hardware.clear();emit hardwareListChanged();}
    emit settingsChanged();emit changed();return valid;
}
communication::HardwareChannels ChannelViewModel::hardware() const {
    auto result=m_hardware;
    for(auto &h:result)if(m_reserved.contains(h.handle))h.available=false;
    return result;
}
void ChannelViewModel::setReservations(const QSet<quint32> &ports) {
    if(m_reserved==ports)return;
    m_reserved=ports;selectAvailableHardware();emit hardwareListChanged();emit changed();
}
void ChannelViewModel::selectAvailableHardware() {
    if(hardwareLocked() || m_lost)return;
    const auto list=hardware();QString device;QSet<QString> devices;
    for(const auto &h:list){
        devices.insert(hardwareDeviceKey(h));
        if(h.key==m_settings.hardwareKey){if(h.available)return;device=hardwareDeviceKey(h);}
    }
    // Preserve unplugged selections; a missing adapter must never select another one.
    if(!m_settings.hardwareKey.isEmpty() && device.isEmpty())return;
    if(device.isEmpty()){if(devices.size()!=1)return;device=*devices.constBegin();}
    auto next=m_settings;next.hardwareKey.clear();next.handle=0;
    for(const auto &h:list)if(h.available && hardwareDeviceKey(h)==device){next.hardwareKey=h.key;next.handle=h.handle;break;}
    if(next.hardwareKey!=m_settings.hardwareKey || next.handle!=m_settings.handle)setSettings(next);
}
int ChannelViewModel::communicationIndicator()const {
    if(connected())return (m_health==Health::Ready || m_health==Health::Sleeping)?1:2;
    return m_lost?2:0;
}
bool ChannelViewModel::canConnect() const {
    if(!m_ready || busy() || connected() || (m_settings.hardwareKey.isEmpty() && !m_settings.handle))return false;
    SoftwareChannelConfiguration c;QString error;if(!m_settings.toConfiguration(c,error))return false;
    int matches=0;
    for(const auto &h:hardware()) {
        if(!h.available)continue;
        if((m_settings.hardwareKey.isEmpty() && !m_settings.handle) ||
           ((!m_settings.hardwareKey.isEmpty()?m_settings.hardwareKey==h.key:m_settings.handle==h.handle)))++matches;
    }
    return matches==1;
}
bool ChannelViewModel::canStart() const{return startHint().isEmpty();}
QString ChannelViewModel::startHint() const {
    if(busy())return "通道正在执行操作";
    SoftwareChannelConfiguration c;QString error;if(!m_settings.toConfiguration(c,error))return error;
    if(!connected())return "请先连接软件通道";
    if(!m_settings.simulation){
        if(m_settings.bus!=Bus::Lin)return "真实 CAN 下载尚未完成适配；当前支持 PLIN / LIN 真实下载";
        if(m_health!=Health::Ready&&m_health!=Health::Sleeping)return "硬件通道状态异常";
        boot::FlashProfile profile;if(!boot::FlashProfile::fromJson(m_settings.downloadProfile,profile,error))return error;
        if(profile.keyLibrary.trimmed().isEmpty())return "请配置已授权的安全访问 DLL";
        if(!profile.keyLibrary.trimmed().isEmpty()&&profile.keyProvider!="external-generatekeyex")return "下载设置中请选择 External GenerateKeyEx 算法";
        const QDir app(QCoreApplication::applicationDirPath());
        const auto key=QDir::isRelativePath(profile.keyLibrary)?app.absoluteFilePath(profile.keyLibrary):profile.keyLibrary;
        if(!profile.keyLibrary.trimmed().isEmpty()&&(!QFileInfo::exists(key)||!QFileInfo::exists(app.filePath("seedkey/SeedkeyBridge32.exe"))))return "Seedkey DLL 或 32 位调用程序缺失，请使用完整发布目录";
    }
    if(!imageReady(m_settings.applicationPath))return "请选择有效的 Application 镜像";
    if(m_settings.flashRequired && !imageReady(m_settings.flashPath))return "此配置需要有效的 Flash Driver 镜像";
    return {};
}
bool ChannelViewModel::canScan()const {
    return connected() && !busy() && !m_settings.simulation && m_settings.bus==Bus::Lin && (m_health==Health::Ready || m_health==Health::Sleeping);
}
void ChannelViewModel::toggleConnection() {
    if(m_pending)return;
    if(connected()){if(diagnosticBusy())cancel();m_manualDisconnect=true;m_wasConnected=false;m_lost=false;m_pending=true;m_model->disconnectionRequested();}
    else if(canConnect()){m_connecting=true;m_pending=true;m_error.clear();m_model->connectionRequested(m_settings);}
    emit changed();
}
void ChannelViewModel::refresh(){if(!m_pending){m_pending=true;m_model->refreshRequested();emit changed();}}
void ChannelViewModel::start(){if(canStart()){m_pending=true;m_model->previewRequested(m_settings);emit changed();}}
void ChannelViewModel::cancel(){
    if(m_signals->running())m_signals->stop();
    if(m_repeatActive){m_repeatActive=false;m_diagnosticRepeatTimer.stop();m_diagnosticResult+="\n自动重发已停止";log("自动重发已停止");}
    if(m_task==TaskState::Running || m_scanning || m_diagnosticBusy || m_protocolBusy)m_model->cancelRequested();
    emit changed();
}
void ChannelViewModel::scanHeaders(){if(canScan()){m_pending=true;m_model->scanRequested();emit changed();}}
void ChannelViewModel::log(const QString &text){
    const QString line=QDateTime::currentDateTime().toString("HH:mm:ss.zzz")+"  "+text;
    m_logs.append(line);while(m_logs.size()>ChannelPageInitialValues::logCapacity)m_logs.removeFirst();emit logAdded(line);
}
void ChannelViewModel::clearLogs(){m_logs.clear();emit logsCleared();}
bool ChannelViewModel::chooseImage(bool flash,const QString &path) {
    if(path.isEmpty() || busy())return false;
    if(!imageReady(path)){m_error="镜像必须是可读取的非空 BIN / HEX 文件。";log(m_error);emit changed();return false;}
    auto s=m_settings;if(flash)s.flashPath=QFileInfo(path).absoluteFilePath();else s.applicationPath=QFileInfo(path).absoluteFilePath();
    return setSettings(s);
}
QStringList ChannelViewModel::imageCandidates(const QString &path) {
    QFileInfo f(path);if(f.isFile())return imageReady(path)?QStringList{f.absoluteFilePath()}:QStringList{};
    if(!f.isDir())return {};
    QStringList results;QDir dir(f.absoluteFilePath());
    for(const auto &info:dir.entryInfoList(QDir::Files|QDir::Readable,QDir::Name)) {
        if(imageReady(info.absoluteFilePath()))results.append(info.absoluteFilePath());
        if(results.size()>=1000)break;
    }return results;
}
bool ChannelViewModel::exportLogs(const QString &path,QString &error) const {
    error.clear();QSaveFile f(path);const QByteArray data=(m_logs.join('\n')+'\n').toUtf8();
    if(!f.open(QIODevice::WriteOnly) || f.write(data)!=data.size() || !f.commit()){error="日志导出失败。";return false;}return true;
}
bool ChannelViewModel::loadCdd(const QString &path,QString &error,bool applyCommunication){
    if(busy()){error="通道正在执行操作";return false;}
    diag::Database db;if(!diag::Database::load(path,db,error))return false;
    int ecuIndex=0,variantIndex=0;
    for(int i=0;i<db.ecus.size();++i)if(db.ecus[i].id==m_settings.cddEcu||db.ecus[i].qualifier==m_settings.cddEcu)ecuIndex=i;
    const auto &ecu=db.ecus[ecuIndex];
    for(int i=0;i<ecu.variants.size();++i)if(ecu.variants[i].base)variantIndex=i;
    for(int i=0;i<ecu.variants.size();++i)if(ecu.variants[i].id==m_settings.cddVariant||ecu.variants[i].qualifier==m_settings.cddVariant)variantIndex=i;
    if(ecu.variants.isEmpty()){error="ECU 缺少 Variant";return false;}
    auto s=m_settings;s.cddPath=db.path;s.cddEcu=ecu.id;s.cddVariant=ecu.variants[variantIndex].id;
    if(cddProtocolState(ecu.variants[variantIndex],s.bus)==2){error="CDD 协议与当前通道不一致";return false;}
    if(applyCommunication&&!applyCddCommunication(ecu.variants[variantIndex],s,error))return false;
    if(!setSettings(s)){error="无法更新 CDD 配置";return false;}
    m_database=std::move(db);m_services.setServices(m_database.ecus[ecuIndex].variants[variantIndex].services);
    m_model->resetDiagnosticRequested();m_diagnosticResult=QString("已载入 CDD %1 · %2 个服务\nECU：%3 · Variant：%4\n%5")
        .arg(m_database.version).arg(m_services.rowCount()).arg(m_database.ecus[ecuIndex].name,
             m_database.ecus[ecuIndex].variants[variantIndex].name,m_database.warnings.join('\n'));
    emit diagnosticDatabaseChanged();emit changed();return true;
}
bool ChannelViewModel::selectDiagnosticTarget(const QString &ecuText,const QString &variantText,QString &error){
    if(busy()){error="通道正在执行操作";return false;}
    const diag::Ecu *ecu=nullptr;int matches=0;
    for(const auto &e:m_database.ecus)if(e.id==ecuText){ecu=&e;matches=1;break;}
    if(!ecu)for(const auto &e:m_database.ecus)if(e.name==ecuText||e.qualifier==ecuText){ecu=&e;++matches;}
    if(matches!=1){error=matches?"ECU 名称不唯一，请从列表选择或输入 ID":"CDD 中未找到 ECU："+ecuText;return false;}
    const diag::Variant *variant=nullptr;matches=0;
    for(const auto &v:ecu->variants)if(v.id==variantText){variant=&v;matches=1;break;}
    if(!variant)for(const auto &v:ecu->variants)if(v.name==variantText||v.qualifier==variantText){variant=&v;++matches;}
    if(matches!=1){error=matches?"Variant 名称不唯一，请从列表选择或输入 ID":"CDD 中未找到 Variant："+variantText;return false;}
    auto s=m_settings;s.cddEcu=ecu->id;s.cddVariant=variant->id;
    if(!applyCddCommunication(*variant,s,error))return false;
    if(!setSettings(s)){error="无法保存目标选择；若波特率变化，请先断开通道";return false;}
    m_services.setServices(variant->services);m_model->resetDiagnosticRequested();
    m_diagnosticResult=QString("已选择 ECU：%1 · Variant：%2 · %3 个服务").arg(ecu->name,variant->name).arg(m_services.rowCount());
    emit diagnosticDatabaseChanged();emit changed();error.clear();return true;
}
QString ChannelViewModel::diagnosticHint()const{
    if(busy())return "通道正在执行操作";
    if(!connected())return "请先连接通道";
    if(!m_settings.simulation&&m_health!=communication::Health::Ready&&m_health!=communication::Health::Sleeping)return "硬件通道状态异常";
    return {};
}
bool ChannelViewModel::applyDiagnosticConfiguration(const diag::Database &db,const ChannelSettings &settings,QString &error){
    const diag::Variant *target=nullptr;
    for(const auto &e:db.ecus)if(e.id==settings.cddEcu)for(const auto &v:e.variants)if(v.id==settings.cddVariant)target=&v;
    if(!db.ecus.isEmpty()&&!target){error="请选择 CDD 中有效的 ECU / Variant";return false;}
    if(target&&cddProtocolState(*target,settings.bus)==2){error="CDD 协议与当前通道不一致";return false;}
    communication::SoftwareChannelConfiguration config;if(!settings.toConfiguration(config,error))return false;
    if(!setSettings(settings)){error="通道忙，或已连接时更改了波特率；请等待请求结束或先断开通道";return false;}
    const auto services=target?target->services:QVector<diag::Service>{};
    m_database=db;m_services.setServices(services);m_model->resetDiagnosticRequested();
    m_diagnosticResult=QString("UDS 设置已更新 · CDD %1 · %2 个服务").arg(db.version).arg(services.size());
    emit diagnosticDatabaseChanged();emit changed();error.clear();return true;
}
bool ChannelViewModel::sendDiagnostic(int row,const QByteArray &bytes,QString &error){
    error=diagnosticHint();if(!error.isEmpty())return false;
    auto service=m_services.service(row);if(!service){error="请选择 CDD 服务";return false;}
    if(!service->physical){error="此服务仅支持功能寻址；请使用支持物理寻址的服务";return false;}
    if(!diag::Codec::validate(*service,bytes,error))return false;
    if(m_settings.bus==communication::Bus::Can&&bytes.size()>m_settings.canNetwork.value("receiveCapacity").toInt(4095)){error="请求超过配置的最大 PDU 字节数";return false;}
    m_lastDiagnostic={*service,bytes};m_diagnosticSettings=m_settings;m_diagnosticBusy=true;m_diagnosticResult="正在等待 ECU 响应…";
    m_model->diagnosticRequested(m_diagnosticSettings,m_lastDiagnostic);emit changed();return true;
}
bool ChannelViewModel::repeatDiagnostic(int row,const QByteArray &bytes,QString &error){
    if(!sendDiagnostic(row,bytes,error))return false;
    m_repeatActive=true;m_repeatRemaining=m_settings.udsRepeatCount;
    m_repeatCompleted=0;m_repeatTotal=m_settings.udsRepeatCount+1;emit changed();return true;
}
QString ChannelViewModel::diagnosticRepeatStatus()const{
    if(!m_repeatActive)return {};
    return QString("已完成 %1/%2 次发送%3").arg(m_repeatCompleted).arg(m_repeatTotal)
        .arg(m_diagnosticRepeatTimer.isActive()?QString(" · 等待 %1 ms").arg(m_settings.udsRepeatDelayMs):QString(" · 等待响应"));
}
}
