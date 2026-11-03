#include "domain/TraceClock.h"
#include "ChannelWorker.h"
#include "model/FramePresentation.h"
#include <QThread>
#ifdef Q_OS_WIN
#include <QWinEventNotifier>
#endif
#include <cstring>
#include "protocol/PluginKey.h"
namespace host {
using namespace communication;
static void stampFrame(FrameRecord &record){
    const auto now=QDateTime::currentDateTime();record.epochMs=now.toMSecsSinceEpoch();
    record.timestamp=now.toString("HH:mm:ss.zzz");
    record.captureUs=captureTimeUs();
}
class PreviewBackend final : public HardwareBackend {
public:
    explicit PreviewBackend(Bus bus):m_bus(bus){}
    Bus bus() const override{return m_bus;}
    HardwareChannels scan(QString &error) override {
        error.clear();HardwareChannels list;
        for(int port=1;port<=2;++port) {
            HardwareChannel h;h.bus=m_bus;h.handle=(m_bus==Bus::Can?0xf100:0xf200)+port;
            h.key=QString("preview:%1:%2").arg(m_bus==Bus::Can?"CAN":"LIN").arg(port);
            h.label=m_bus==Bus::Can?"模拟 CAN 双通道适配器":"模拟 LIN 双通道适配器";
            h.deviceId=1;h.controller=port;h.persistentIdentity=true;list.append(h);
        }return list;
    }
    bool open(const HardwareChannel &,const SoftwareChannelConfiguration &,QString &error) override{error.clear();return true;}
    bool close(QString &error) override{error.clear();return true;}
    Health health(QString &detail) override{detail="模拟通道已就绪";return Health::Ready;}
private:Bus m_bus;
};
void ChannelWorker::initialize() {
    Q_ASSERT(QThread::currentThread()==thread());
    m_traceEnabled=qEnvironmentVariableIsSet("QTBOOTLOADER_TRACE_DIR");
    if(m_traceEnabled&&!m_safeTrace.open(qEnvironmentVariable("QTBOOTLOADER_TRACE_DIR")))traceFailure();
    m_signal=std::make_unique<SignalTransmitter>();
    m_signal->drainReceived=[this]{receive();return m_receiveDrained;};
    connect(m_signal.get(),&SignalTransmitter::statusChanged,this,[this](signal::RunStatus status){
        if(!signal::active(status.state)&&!status.cleanupPending)m_operations.release(OperationCoordinator::Task::Signal);
        if(m_receive&&m_settings.bus==Bus::Can&&!m_settings.simulation){
            if(signal::active(status.state))m_receive->setInterval(m_canNotifier?20:1);else if(!m_running)m_receive->setInterval(20);
        }
        emit signalStatus(status);
    });
    connect(m_signal.get(),&SignalTransmitter::events,this,[this](signal::BusFrameEvents events){projectSignalEvents(events);emit busEvents(events);});
    connect(m_signal.get(),&SignalTransmitter::notice,this,&ChannelWorker::logMessage);
    connect(m_signal.get(),&SignalTransmitter::canRecoveryNeeded,this,[this](int state,quint64 connection){
        if(m_can&&m_session&&m_session->state()==ConnectionState::Connected&&connection==m_session->generation())handleCanState(static_cast<CanBusState>(state),m_can->lastErrorText());
    },Qt::QueuedConnection);
    m_receive=new QTimer(this);m_receive->setInterval(20);m_receive->setTimerType(Qt::PreciseTimer);connect(m_receive,&QTimer::timeout,this,&ChannelWorker::receive);
    m_receivePump=new QTimer(this);m_receivePump->setSingleShot(true);m_receivePump->setTimerType(Qt::PreciseTimer);
    connect(m_receivePump,&QTimer::timeout,this,&ChannelWorker::receive);
    m_scan=new QTimer(this);connect(m_scan,&QTimer::timeout,this,&ChannelWorker::scanTick);
    m_repeat=new QTimer(this);m_repeat->setSingleShot(true);m_repeat->setTimerType(Qt::PreciseTimer);
    connect(m_repeat,&QTimer::timeout,this,&ChannelWorker::beginPreviewRound);
    createSession();m_receive->start();emit ready();
}
bool ChannelWorker::traceRecord(const QString &direction,quint32 id,int dlc,const QByteArray &data,bool valid,const TraceTiming &timing){
    if(!m_traceEnabled)return true;
    if(m_traceFailed)return false;
    if(m_safeTrace.append(direction,id,dlc,data,QDateTime::currentMSecsSinceEpoch(),captureTimeUs(),valid && m_traceDiagnosticIds.contains(id),timing))return true;
    traceFailure();return false;
}
void ChannelWorker::traceDeadline(const QString &kind,int limit,qint64 elapsed){
    TraceTiming timing;timing.timeoutKind=kind;timing.timeoutLimitMs=limit;timing.timeoutElapsedMs=elapsed;
    if(kind=="CAN_N_AS"||kind=="CAN_N_AR")for(int i=m_canEchoes.size()-1;i>=0;--i)if(m_canEchoes[i].owner==m_canTransport.get()){timing.sendCallUs=m_canEchoes[i].sendCallUs;break;}
    traceRecord("TIMEOUT",0,0,{},false,timing);
}
void ChannelWorker::traceFailure(){
    if(m_traceFailed)return;
    m_traceFailed=true;
    emit logMessage("Trace write failed: evidence incomplete; transmission disabled.");
    QMetaObject::invokeMethod(this,[this]{
        if(m_canTransport)m_canTransport->linkFailed("Trace write failed: evidence incomplete");
        if(m_signal)m_signal->linkFailed("Trace write failed: evidence incomplete");
        if(m_running)failPreview("Trace write failed: evidence incomplete");
    },Qt::QueuedConnection);
}
void ChannelWorker::createSession() {
    m_canBlocked=m_canHealthInitialized=false;
    stopReceiveNotifier();
    m_session.reset();m_can.reset();m_lin.reset();m_canEchoes.clear();
    SoftwareChannelConfiguration config;QString error;
    if(!m_settings.toConfiguration(config,error)){emit logMessage(error);return;}
    std::unique_ptr<HardwareBackend> backend;
    if(m_settings.simulation){backend.reset(new PreviewBackend(m_settings.bus));}
    else if(m_settings.bus==Bus::Lin){m_lin=createLinHardware();backend=sessionBackend(*m_lin);}
    else {m_can=m_canFactory?m_canFactory():createCanHardware();backend=sessionBackend(*m_can);}
    m_session.reset(new SoftwareChannel(config,std::move(backend)));
    connect(m_session.get(),&SoftwareChannel::enumerationMeasured,this,[this](qint64 elapsedUs){
        if(m_traceEnabled&&m_can){TraceTiming timing;timing.hardwareOperation="CAN_ENUMERATE";timing.hardwareDurationUs=elapsedUs;traceRecord("PROFILE",0,0,{},false,timing);}
    });
    connect(m_session.get(),&SoftwareChannel::hardwareChanged,this,&ChannelWorker::hardwareChanged);
    connect(m_session.get(),&SoftwareChannel::stateChanged,this,[this](ConnectionState s,const QString &detail){
        if(s==ConnectionState::Connected) {
            emit connectionGenerationChanged(m_session->generation());
            const auto c=m_session->configuration();
            m_settings.hardwareKey=c.hardwareKey;m_settings.handle=c.preferredHandle;
            emit bindingChanged(c.hardwareKey,c.preferredHandle);
#ifdef Q_OS_WIN
            if(m_can && m_can->receiveEventHandle()){
                auto *notifier=new QWinEventNotifier(static_cast<HANDLE>(m_can->receiveEventHandle()),this);
                notifier->setEnabled(true);
                if(notifier->isEnabled()){
                    m_canNotifier=notifier;const auto generation=m_session->generation();
                    connect(notifier,&QWinEventNotifier::activated,this,[this,generation]{
                        if(m_session&&m_session->state()==ConnectionState::Connected&&m_session->generation()==generation)receive();
                    });
                    if(m_receivePump&&!m_receivePump->isActive())m_receivePump->start(0);
                }else delete notifier;
            }
            if(m_can&&!m_canNotifier&&c.hardwareKey.startsWith("can:"))emit logMessage("PCAN receive event unavailable; using polling.");
#endif
        }
        emit stateChanged(s,detail);
        emit logMessage(connectionText(s)+(detail.isEmpty()?QString():" · "+detail));
    });
    connect(m_session.get(),&SoftwareChannel::healthChanged,this,[this](Health health,const QString &detail){
        if(m_can){
            const auto observed=m_can->classifyStatus(m_can->lastErrorCode());const auto live=m_can->busState();
            if(live!=CanBusState::Unsupported){
                const auto state=(observed==CanBusState::BusOff||observed==CanBusState::Warning||observed==CanBusState::Fatal)?observed:live;
                handleCanState(state,detail);return;
            }
        }
        emit healthChanged(health,detail);
    });
    connect(m_session.get(),&SoftwareChannel::connectionClosing,this,[this](){
        stopReceiveNotifier();
        if(m_signal){m_signal->stop();m_signal->hardwareClosed();}
        m_canBlocked=m_canHealthInitialized=false;
        cancelTask();clearProtocol();m_diagnosticDids.clear();if(m_scanning)stopScan("扫描已停止：连接正在关闭");
    });
    m_session->startMonitoring();
}
void ChannelWorker::stopReceiveNotifier(){
    if(m_receivePump)m_receivePump->stop();
    delete m_canNotifier;m_canNotifier=nullptr;
}
bool ChannelWorker::handleCanState(CanBusState state,const QString &detail){
    if(state==CanBusState::Unsupported)return false;
    if(state==CanBusState::Ready){
        const bool hadFault=m_canBlocked;m_canBlocked=false;
        if(hadFault||!m_canHealthInitialized){emit healthChanged(Health::Ready,hadFault?"CAN recovered":"CAN ready");m_canHealthInitialized=true;}
        return false;
    }
    const bool firstWarning=!m_canBlocked;
    m_canBlocked=true;
    if(firstWarning){
        if(!m_canEchoes.isEmpty()||(m_uds&&m_uds->hasActiveRequest()&&m_udsWriteAttempted)){m_canUnresolved=true;m_recoveryCloseSucceeded=false;}
        const QString reason=detail.isEmpty()?QStringLiteral("CAN bus or adapter error; transmission stopped"):detail;
        if(m_canTransport)m_canTransport->linkFailed(reason);
        if(m_running)failPreview(reason);
        if(m_signal)m_signal->linkFailed(reason);
        emit healthChanged(Health::BusWarning,reason);m_canHealthInitialized=true;emit logMessage(reason);
    }
    return true;
}
bool ChannelWorker::apply(const ChannelSettings &settings) {
    SoftwareChannelConfiguration c;QString error;
    if(!settings.toConfiguration(c,error)){emit logMessage(error);return false;}
    if(m_session && m_session->state()==ConnectionState::Connected){emit logMessage("请先断开后修改硬件及协议配置。");return false;}
    const bool recreate=!m_session || m_settings.simulation!=settings.simulation;
    if(recreate&&m_canUnresolved){emit logMessage("Earlier request may have executed; reconnect the original channel before changing its mode.");return false;}
    m_settings=settings;
    if(recreate){
        createSession();
        // Publish the new backend's ports before commandFinished unlocks selection.
        // startMonitoring schedules its first poll, so its old list must not win here.
        if(m_session)m_session->poll();
    }
    else {
        if(!m_session->configure(c)){emit logMessage(m_session->error());return false;}
        m_session->poll();
    }
    return bool(m_session);
}
void ChannelWorker::updateSettings(ChannelSettings s){apply(s);emit commandFinished();}
void ChannelWorker::connectChannel(ChannelSettings s) {
    if(m_traceEnabled&&(m_traceFailed||s.bus!=Bus::Can||s.simulation)){emit logMessage("Trace mode requires a healthy log and physical CAN channel.");emit commandFinished();return;}
    if(apply(s) && m_session && m_session->connectChannel() && m_canUnresolved && m_recoveryCloseSucceeded){
        m_canEchoes.clear();m_canUnresolved=false;m_recoveryCloseSucceeded=false;m_udsWriteAttempted=false;
        emit logMessage("Previous local transaction was discarded after manual reconnect; the ECU may already have executed its request.");
    }
    emit commandFinished();
}
void ChannelWorker::disconnectChannel(){
    const bool connected=m_session&&m_session->state()==ConnectionState::Connected;
    const bool closed=m_session&&m_session->disconnectChannel();
    if(connected&&m_canUnresolved){
        m_recoveryCloseSucceeded=closed;
        emit logMessage(closed?"Previous request may already have executed; reconnect manually to discard its local association.":"Previous request may already have executed; adapter close failed and its local association remains unresolved.");
    }
    if(closed&&connected&&m_signal)m_signal->hardwareClosed();
    emit commandFinished();
}
void ChannelWorker::refresh(){if(m_session)m_session->poll();emit commandFinished();}
void ChannelWorker::startPreview(ChannelSettings s) {
    SoftwareChannelConfiguration config;QString error;
    if(s.bus==Bus::Can&&(m_canBlocked||m_canUnresolved||!m_canEchoes.isEmpty()||(m_signal&&m_signal->hasUnconfirmedCanFrame()))){emit logMessage("CAN fault or an earlier transaction is unresolved; inspect the ECU before starting another download");emit commandFinished();return;}
    if(m_traceEnabled&&(m_traceFailed||s.bus!=Bus::Can||s.simulation)){emit logMessage("Trace mode unavailable: evidence incomplete or not physical CAN.");emit commandFinished();return;}
    if(!s.toConfiguration(config,error)){emit logMessage(error);emit commandFinished();return;}
    if(!m_operations.permits(OperationCoordinator::Task::Download) || m_running || m_scanning || m_manualBusy || (m_manualMode && m_uds && m_uds->busy()) || s.simulation!=m_settings.simulation || !m_session ||
       m_session->state()!=ConnectionState::Connected || !imageReady(s.applicationPath) ||
       (s.flashRequired && !imageReady(s.flashPath))) {
        emit logMessage("无法开始下载：请检查连接和镜像。");emit commandFinished();return;
    }
    if(!diagnosticMaster(error)){emit logMessage(error);emit commandFinished();return;}
    m_operations.acquire(OperationCoordinator::Task::Download);
    m_settings=s;m_progress=0;m_round=0;m_totalRounds=s.repeatDownloadEnabled?s.repeatDownloadCount:1;m_running=true;
    if(s.bus==Bus::Can&&!s.simulation)m_receive->setInterval(m_canNotifier?20:1);
    emit logMessage(s.simulation?QString("开始模拟下载，共 %1 次；不访问物理总线。").arg(m_totalRounds):QString("开始真实 %1 下载，共 %2 次。").arg(s.bus==Bus::Can?"CAN":"LIN").arg(m_totalRounds));
    beginPreviewRound();emit commandFinished();
}
void ChannelWorker::beginPreviewRound() {
    if(!m_running)return;
    if(!m_session || m_session->state()!=ConnectionState::Connected){failPreview("连接已关闭");return;}
    if(!imageReady(m_settings.applicationPath) || (m_settings.flashRequired && !imageReady(m_settings.flashPath))){
        failPreview("镜像文件不存在或不可读取");return;
    }
    m_waiting=false;m_progress=0;++m_round;
    emit logMessage(QString("开始第 %1/%2 次下载").arg(m_round).arg(m_totalRounds));
    emit taskChanged(TaskState::Running,0,QString("第 %1/%2 次 · 检查镜像").arg(m_round).arg(m_totalRounds));
    beginDownload();
}
void ChannelWorker::failPreview(const QString &reason) {
    ++m_confirmationId;m_confirmationPending=false;m_stagedApp={};m_stagedDriver={};
    m_operations.release(OperationCoordinator::Task::Download);
    if(m_job)m_job->cancel();
    m_repeat->stop();m_running=m_waiting=false;m_receive->setInterval(20);
    emit taskChanged(TaskState::Failed,m_progress,"下载停止 · "+reason);emit logMessage("重复下载已停止："+reason);
}
void ChannelWorker::completeRound(const QString &result) {
    if(!m_running)return;
    m_progress=100;
    emit logMessage(QString("第 %1/%2 次下载完成").arg(m_round).arg(m_totalRounds));
    if(m_round<m_totalRounds){
        m_waiting=true;
        emit taskChanged(TaskState::Running,100,QString("第 %1/%2 次完成 · 等待 %3 s").arg(m_round).arg(m_totalRounds).arg(m_settings.repeatDownloadIntervalMs/1000.0,0,'f',1));
        m_repeat->start(m_settings.repeatDownloadIntervalMs);
    }else{
        m_running=false;m_receive->setInterval(20);
        m_operations.release(OperationCoordinator::Task::Download);
        emit taskChanged(TaskState::Completed,100,QString("%1 · 共 %2 次").arg(result).arg(m_totalRounds));
        emit logMessage(result);
    }
}
void ChannelWorker::clearProtocol(){
    m_operations.release(OperationCoordinator::Task::Diagnostic);
    if(m_job)m_job->cancel();
    m_job.reset();m_uds.reset();m_transport.reset();m_ecu.reset();m_canTransport.reset();m_canEcu.reset();m_manualMode=false;
    emit diagnosticActivity(false);
}
void ChannelWorker::beginDownload(bool confirmed){
    clearProtocol();
    boot::FirmwareImage application,driver;QString error;
    if(confirmed){application=std::move(m_stagedApp);driver=std::move(m_stagedDriver);}
    else if(!boot::FirmwareImage::load(m_settings.applicationPath,m_settings.applicationAddress.toUInt(nullptr,16),application,error)||
       (m_settings.flashRequired&&!boot::FirmwareImage::load(m_settings.flashPath,m_settings.flashAddress.toUInt(nullptr,16),driver,error))){failPreview(error);return;}
    boot::FlashProfile profile;
    if(!boot::FlashProfile::fromJson(m_settings.downloadProfile,profile,error)){failPreview(error);return;}
    if(!confirmed&&(profile.isT008()||boot::t008::looksLikeImage(application)||boot::t008::looksLikeDriver(driver))&&
       ((profile.enabled("driver36")&&!profile.enabled("driver34"))||(profile.enabled("app36")&&!profile.enabled("app34")))){failPreview("TransferData requires RequestDownload");return;}
    if(!confirmed&&(m_settings.checkDriverAbi||m_settings.checkAppVersion||m_settings.checkMinimumBoot)){
        boot::t008::Plan plan;
        if(!boot::t008::images(application,driver,plan,error)){failPreview(error);return;}
        QMap<QString,QString> expected;
        if(m_settings.checkDriverAbi)expected["driverAbi"]=m_settings.expectedDriverAbi;
        if(m_settings.checkAppVersion)expected["appVersion"]=m_settings.expectedAppVersion;
        if(m_settings.checkMinimumBoot)expected["minimumBoot"]=m_settings.expectedMinimumBoot;
        QStringList mismatches;if(!boot::t008::compareFileVersions(plan,expected,mismatches,error)){failPreview(error);return;}
        if(!mismatches.isEmpty()){
            m_stagedApp=application;m_stagedDriver=driver;m_confirmationPending=true;const auto id=++m_confirmationId;
            emit packageConfirmationRequested(id,QString("版本不一致或无法解析，请确认是否继续：\n")+mismatches.join("\n"));return;
        }
    }
    if(profile.isT008()&&(m_settings.simulation||m_settings.bus!=Bus::Can||m_settings.bitrate!=500000||!m_settings.flashRequired||m_settings.repeatDownloadEnabled||m_settings.autoReconnect)){
        failPreview("T008 requires physical CAN at 500000 bit/s, native driver, one attempt and auto reconnect disabled");return;
    }
    if(profile.isT008()){
        const QDir runtime(QCoreApplication::applicationDirPath());
        const QFileInfo dll(QDir::isRelativePath(profile.keyLibrary)?runtime.absoluteFilePath(profile.keyLibrary):profile.keyLibrary);
        const QFileInfo bridge(runtime.absoluteFilePath("seedkey/SeedkeyBridge32.exe"));
        if(!dll.isFile()||dll.size()==0||!bridge.isFile()||bridge.size()==0){failPreview("T008 requires a non-empty external GenerateKeyEx DLL and SeedkeyBridge32.exe");return;}
    }
    profile.applicationEraseBase=m_settings.applicationAddress.toUInt(nullptr,16);
    profile.applicationEraseLength=m_settings.applicationLength.toUInt(nullptr,16);
    profile.fingerprintData=QByteArray::fromHex(m_settings.fingerprintHex.toLatin1());
    profile.name=m_settings.profileId;profile.session=quint8(m_settings.programmingSession);
    if(profile.flow=="legacy")profile.securityLevel=quint8(m_settings.securityLevel);
    auto key=boot::makeDownloadKeyProvider(profile,m_settings.simulation);
    if(!m_settings.simulation)emit logMessage(profile.keyLibrary.trimmed().isEmpty()
        ?"未配置 27 DLL：收到的 seed 原样作为 key 发送":"安全访问使用已配置的 27 DLL");
    if(!createProtocol(profile,error,false)){failPreview(error);return;}
    if(m_settings.simulation&&(profile.isT008()||boot::t008::looksLikeImage(application)||boot::t008::looksLikeDriver(driver))){
        if(m_canEcu)m_canEcu->uds.maxBlockLength=1026;
        if(m_ecu)m_ecu->maxBlockLength=1026;
    }
    emit logMessage(QString("镜像快照 %1 B · SHA256 %2 · 配置 %3").arg(application.size).arg(QString::fromLatin1(application.sha256.toHex())).arg(profile.name));
    m_job.reset(new boot::FlashJob(*m_uds,profile,std::move(key)));
    connect(m_job.get(),&boot::FlashJob::notice,this,&ChannelWorker::logMessage);
    connect(m_job.get(),&boot::FlashJob::stepEvent,this,&ChannelWorker::logMessage);
    connect(m_job.get(),&boot::FlashJob::progress,this,[this](int value,const QString &step){
        if(!m_running)return;m_progress=value;emit taskChanged(TaskState::Running,value,QString("第 %1/%2 次 · %3").arg(m_round).arg(m_totalRounds).arg(step));
    });
    connect(m_job.get(),&boot::FlashJob::finished,this,[this](bool ok,const QString &why){if(ok)completeRound(why);else failPreview(why);});
    if(!m_job->start(application,driver,error))failPreview(error);
}
void ChannelWorker::confirmPackage(quint64 id,bool accepted){
    if(!m_running||!m_confirmationPending||id!=m_confirmationId)return;
    m_confirmationPending=false;
    if(!accepted){m_stagedApp={};m_stagedDriver={};cancelTask();return;}
    beginDownload(true);
}

bool ChannelWorker::createProtocol(const boot::FlashProfile &profile,QString &error,bool manual){
    if(!diagnosticMaster(error))return false;
    if(!m_settings.simulation&&m_lin)m_lin->clearMsg();
    if(m_settings.bus==Bus::Lin){
        const auto nad=quint8(m_settings.nad.toUInt(nullptr,16));
        if(m_settings.simulation)m_ecu.reset(new boot::SimulatedLinEcu(nad,profile));
        const int slotMs=m_settings.simulation?qMax(1,m_settings.linSlotMs):qMax(m_settings.linSlotMs,qMax(20,(150000+m_settings.bitrate-1)/m_settings.bitrate));
        m_transport.reset(new boot::LinTransport(nad,slotMs,m_settings.linCrMs,[this](quint8 id,const QByteArray &data,QString &error){
            if(m_ecu){
                if(!m_ecu->write(id,data,error))return false;
                if(id==0x3d){const auto frame=m_ecu->takeResponseFrame();if(!frame.isEmpty())m_transport->receiveFrame(id,frame);}
                return true;
            }
            if(!m_lin){error="LIN hardware unavailable";return false;}
            TLINMsg message={};message.FrameId=id;message.Length=8;message.ChecksumType=cstClassic;
            message.Direction=id==0x3c?dirPublisher:dirSubscriber;
            for(int i=0;i<8;++i)message.Data[i]=i<data.size()?BYTE(data[i]):BYTE(0xff);
            if(!m_lin->sendRaw(message)){error=m_lin->lastErrorText();return false;}return true;
        }));
        m_transport->requireTransmitConfirmation(!m_settings.simulation,m_settings.linAsMs);
        connect(m_transport.get(),&boot::DiagnosticTransport::cancelled,this,[this]{if(m_ecu)m_ecu->clearWire();});
        connect(m_transport.get(),&boot::LinTransport::trace,this,[this](bool tx,quint8 id,const QByteArray &bytes){
            // A 0x3D header opens one receive event. Its eventual response or
            // no-response timeout is the only row shown for that event.
            if(tx&&bytes.isEmpty())return;
            if(!m_settings.simulation){
                // Record every requested bus transmission immediately. The later
                // PLIN 0x3C queue event is a separate RxD readback.
                if(!tx)return;
                FrameRecord f;stampFrame(f);f.channel=m_settings.softwareId;
                f.direction="TX";f.identifier=QString("0x%1").arg(id,2,16,QChar('0')).toUpper();
                f.data=QString::fromLatin1(bytes.toHex(' ')).toUpper();f.length=bytes.size();
                f.bus=signal::Bus::Lin;f.request=true;f.classicChecksum=true;
                f.status="发送";publishFrames({f});return;
            }
            FrameRecord f;stampFrame(f);f.channel=m_settings.softwareId;
            f.direction="SIM "+QString(tx?(bytes.isEmpty()?"HEADER":"TX"):"RX");
            f.identifier=QString("0x%1").arg(id,2,16,QChar('0')).toUpper();f.data=QString::fromLatin1(bytes.toHex(' ')).toUpper();f.length=bytes.size();
            f.bus=signal::Bus::Lin;f.simulated=true;f.classicChecksum=true;
            f.status="LIN UDS · Classic checksum";publishFrames({f});
        });
    }else{
        boot::CanOptions options;options.txId=(manual||m_settings.downloadTxId.isEmpty()?m_settings.requestId:m_settings.downloadTxId).toUInt(nullptr,16);
        options.rxId=(manual||m_settings.downloadRxId.isEmpty()?m_settings.responseId:m_settings.downloadRxId).toUInt(nullptr,16);
        if(!m_settings.functionalId.trimmed().isEmpty()){
            bool ok=false;quint32 fid=m_settings.functionalId.trimmed().toUInt(&ok,16);
            if(ok)options.functionalTxId=fid;
        }
        options.extended=m_settings.extendedId;
        if(!boot::CanOptions::fromJson(m_settings.canNetwork,options,error))return false;
        options.bitrate=m_settings.bitrate;
        // Download CFs may use a bounded window only after peer CTS with STmin=0.
        options.boundedFastSend=!manual;
        if(profile.isT008()){
            if(m_settings.simulation||options.extended||options.txId!=0x715||options.rxId!=0x795||options.functionalTxId!=0x7df){error="T008 requires physical 11-bit CAN IDs 715/795/7DF";return false;}
            options.strictClassicCc=true;options.padding=0xcc;options.functionalRx=false;
            if(!options.valid(error))return false;
        }
        m_traceDiagnosticIds={options.txId,options.rxId,options.functionalTxId};
        if(!m_settings.simulation){
            if(!m_can||!m_can->enableEcho()){
                error="当前 CAN 驱动未能开启发送回显，无法确认诊断请求已上总线";return false;
            }
            if(!manual)emit logMessage("真实 CAN 下载：硬件回显与 ISO-TP 传输层已激活");
        }else m_canEcu.reset(new boot::SimulatedCanEcu(options,profile));
        m_canTransport.reset(new boot::CanTransport(options,[this,manual](const boot::CanFrame &frame,quint64 token,QString &error){
            if(m_traceEnabled&&m_traceFailed){error="Trace failed: evidence incomplete";return false;}
            if(m_canBlocked){error="CAN bus fault: diagnostic request was not queued";return false;}
            if(m_canEcu)return m_canEcu->writeFromHost(frame,token,error);
            const bool downloadCf=!manual&&!frame.data.isEmpty()&&(quint8(frame.data[0])>>4)==2;
            const auto generation=m_session?m_session->generation():0;
            bool sameOwner=true;for(const auto &pending:m_canEchoes)if(pending.owner!=m_canTransport.get()||pending.generation!=generation)sameOwner=false;
            if(!m_can||(!m_canEchoes.isEmpty()&&(!downloadCf||!sameOwner||m_canEchoes.size()>=4))){error="CAN transmit confirmation window unavailable";return false;}
            TPCANMsg message={};message.ID=frame.id;message.MSGTYPE=frame.extended?PCAN_MESSAGE_EXTENDED:PCAN_MESSAGE_STANDARD;
            message.LEN=BYTE(frame.data.size());for(int i=0;i<frame.data.size();++i)message.DATA[i]=BYTE(frame.data[i]);
            const qint64 sendStart=m_traceEnabled?captureTimeUs():-1;
            m_udsWriteAttempted=true;
            const bool sent=m_traceEnabled?m_safeTrace.sendIfHealthy([&]{return m_can->sendRaw(message);}):m_can->sendRaw(message);
            const qint64 sendCallUs=m_traceEnabled?captureTimeUs()-sendStart:-1;
            if(!sent){
                m_canUnresolved=true;m_recoveryCloseSucceeded=false;
                error=m_traceFailed?"Trace failed: evidence incomplete":m_can->lastErrorText();
                const auto writeStatus=m_can->lastErrorCode();
                if(m_traceEnabled){TraceTiming timing;timing.hardwareOperation=QString("CAN_WRITE_STATUS_0x%1").arg(writeStatus,0,16);timing.hardwareDurationUs=sendCallUs;if(!traceRecord("PROFILE",0,0,{},false,timing)){error="Trace failed: evidence incomplete";return false;}}
                const auto state=m_can->classifyStatus(writeStatus);const auto generation=m_session?m_session->generation():0;
                if(state!=CanBusState::Ready&&state!=CanBusState::Unsupported)QTimer::singleShot(0,this,[this,state,generation,error]{
                    if(m_session&&m_session->state()==ConnectionState::Connected&&m_session->generation()==generation)handleCanState(state,error);
                });
                return false;
            }
            m_canEchoes.enqueue({m_canTransport.get(),token,frame,sendCallUs,generation});
            if(!m_receivePump->isActive())m_receivePump->start(0);
            return true;
        }));
        connect(m_canTransport.get(),&boot::DiagnosticTransport::failed,this,[this](const QString &){
            if(!m_canEchoes.isEmpty()||(m_uds&&m_uds->hasActiveRequest()&&m_udsWriteAttempted)){m_canUnresolved=true;m_recoveryCloseSucceeded=false;}
        });
        connect(m_canTransport.get(),&boot::CanTransport::deadlineObserved,this,&ChannelWorker::traceDeadline);
        if(m_canEcu){m_canEcu->attach(*m_canTransport);
            connect(m_canEcu.get(),&boot::SimulatedCanEcu::failed,m_canTransport.get(),&boot::CanTransport::linkFailed);
        }
        connect(m_canTransport.get(),&boot::CanTransport::trace,this,[this](bool tx,const boot::CanFrame &frame){
            if(!m_settings.simulation&&!tx)return;
            TraceTiming traceTiming;traceTiming.frameFormat=frame.extended?"EXT":"STD";
            if(tx&&!traceRecord("TX request",frame.id,frame.data.size(),frame.data,true,traceTiming))return;
            FrameRecord f;stampFrame(f);f.channel=m_settings.softwareId;
            f.direction=(m_settings.simulation?QString("SIM "):QString())+(tx?"TX":"RX");f.identifier=QString("0x%1").arg(frame.id,frame.extended?8:3,16,QChar('0')).toUpper();
            f.data=QString::fromLatin1(frame.data.toHex(' ')).toUpper();f.length=frame.data.size();
            f.extended=frame.extended;f.simulated=m_settings.simulation;f.request=tx&&!m_settings.simulation;
            f.status=frame.extended?"CAN ISO-TP · 29 bit":"CAN ISO-TP · 11 bit";publishFrames({f});
        });
    }
    boot::DiagnosticTransport *network=m_settings.bus==Bus::Lin?static_cast<boot::DiagnosticTransport*>(m_transport.get()):m_canTransport.get();
    boot::SessionOptions timing{m_settings.p2Ms,m_settings.p2StarMs,m_settings.maxPendingMs,m_settings.testerPresentEnabled?m_settings.testerPresentMs:0};
    timing.p3Ms=m_settings.p3Ms;timing.testerPresentRequest=QByteArray::fromHex(m_settings.testerPresentRequest.toLatin1());
    timing.p2MarginMs=m_settings.p2ServerMs>0?qMax(0,m_settings.p2Ms-m_settings.p2ServerMs):0;
    timing.p2StarMarginMs=m_settings.p2StarServerMs>0?qMax(0,m_settings.p2StarMs-m_settings.p2StarServerMs):0;
    m_uds.reset(new boot::UdsSession(*network,timing));
    connect(m_uds.get(),&boot::UdsSession::trace,this,[this](bool tx,const QByteArray &){if(tx)m_udsWriteAttempted=false;});
    connect(m_uds.get(),&boot::UdsSession::deadlineObserved,this,&ChannelWorker::traceDeadline);
    if(manual)connect(m_uds.get(),&boot::UdsSession::activityChanged,this,[this](bool active){
        if(active)m_operations.acquire(OperationCoordinator::Task::Diagnostic);else m_operations.release(OperationCoordinator::Task::Diagnostic);
        emit diagnosticActivity(active);
    });
    connect(m_uds.get(),&boot::UdsSession::notice,this,&ChannelWorker::logMessage);
    m_manualMode=manual;
    if(manual){
        auto handler=[this](const QByteArray &bytes){
            if(bytes==QByteArray::fromHex("3e80"))return QList<QByteArray>{};
            if(bytes==QByteArray::fromHex("3e00"))return QList<QByteArray>{QByteArray::fromHex("7e00")};
            if(bytes!=m_manualRequest.bytes)return QList<QByteArray>{QByteArray::fromHex("7f")+bytes.left(1)+char(0x11)};
            auto response=diag::Codec::simulationResponse(m_manualRequest.service,bytes,m_diagnosticDids);
            bool suppressed=false;for(const auto &f:m_manualRequest.service.request.fields)if(f.suppressible&&bytes.size()>1&&(quint8(bytes[1])&0x80))suppressed=true;
            return suppressed&&!response.isEmpty()&&quint8(response[0])!=0x7f?QList<QByteArray>{}:QList<QByteArray>{response};
        };
        if(m_ecu)m_ecu->diagnosticHandler=handler;
        if(m_canEcu)m_canEcu->uds.diagnosticHandler=handler;
        connect(m_uds.get(),&boot::UdsSession::trace,this,[this](bool tx,const QByteArray &pdu){
            emit logMessage(QString(tx?"UDS TX  ":"UDS RX  ")+QString::fromLatin1(pdu.toHex(' ')).toUpper());
        });
    }
    return true;
}
void ChannelWorker::resetDiagnostic(){
    if(!m_running&&!m_manualBusy){clearProtocol();m_diagnosticDids.clear();}
}
bool ChannelWorker::diagnosticMaster(QString &error){
    if(!m_settings.simulation&&m_lin&&m_lin->getDevMode()!=modMaster){
        if(!m_lin->signalConfigureMode(true)){error="恢复诊断 LIN 主模式失败："+m_lin->lastErrorText();return false;}
        emit logMessage("已恢复下载/UDS/扫描所需 LIN 主模式");
    }return true;
}
void ChannelWorker::startSignals(signal::TxPlan plan){
    if(m_signal)m_signal->setCanEchoTimeout(m_settings.canNetwork.value("nAsMs").toInt(1000));
    auto reject=[&](const QString&why){signal::RunStatus s;s.run=plan.run;s.state=signal::RunState::Faulted;s.detail=why;emit signalStatus(s);emit logMessage(why);};
    if(m_traceEnabled&&(m_traceFailed||plan.bus!=signal::Bus::Can||m_settings.simulation||plan.replay)){reject("Trace mode permits only physical CAN signal transmission with a healthy log; replay remains disabled.");return;}
    // Queued commands from a stopped run must never restart it, even if the
    // connection generation has not changed. Do not disturb a newer run's UI.
    if(!plan.run||plan.run<=m_lastSignalRun){emit logMessage("忽略已失效的信号启动命令");return;}
    m_lastSignalRun=plan.run;
    if(!m_session||m_session->state()!=ConnectionState::Connected||plan.connection!=m_session->generation()||
       (plan.bus==signal::Bus::Can)!=(m_settings.bus==Bus::Can)||m_running||m_scanning||m_manualBusy||
       !m_operations.permits(OperationCoordinator::Task::Signal)||(m_signal&&m_signal->running())){
        reject("信号任务被拒绝：通道未连接、代次已变化或任务冲突");return;
    }
    if(m_canBlocked||!m_canEchoes.isEmpty()){reject("CAN fault or an earlier diagnostic frame is unresolved; no frame submitted");return;}
    clearProtocol(); // Also destroys idle TesterPresent timers; outstanding frames must be confirmed first.
    if(!m_operations.acquire(OperationCoordinator::Task::Signal)){reject("通道正在执行其他任务");return;}
    if(plan.replay)plan.replayStampOffsetUs=m_traceOffsetUs;
    m_signal->guardedCanSend={};
    if(m_traceEnabled)m_signal->guardedCanSend=[this](const signal::TxItem &item,QString &error){
        if(m_traceFailed||!m_can){error="Trace failed: evidence incomplete";return false;}
        TPCANMsg message={};message.ID=item.id;message.MSGTYPE=item.extended?PCAN_MESSAGE_EXTENDED:PCAN_MESSAGE_STANDARD;message.LEN=BYTE(item.payload.size());
        std::memcpy(message.DATA,item.payload.constData(),size_t(item.payload.size()));
        const bool sent=m_safeTrace.sendSignal(item.id,item.payload,item.extended,QDateTime::currentMSecsSinceEpoch(),captureTimeUs(),[this,&message]{return m_can->sendRaw(message);});
        if(!sent){if(!m_safeTrace.healthy()){traceFailure();error="Trace failed: evidence incomplete";}else error=m_can->lastErrorText();return false;}
        return true;
    };
    m_signal->start(plan,m_settings.bitrate,m_settings.simulation,m_can.get(),m_lin.get());
}
void ChannelWorker::stopSignals(quint64 run){if(m_signal)m_signal->stop(run);}
void ChannelWorker::updateSignalPayload(signal::PayloadUpdate update){if(m_session&&update.connection==m_session->generation()&&m_signal)m_signal->update(update);}
void ChannelWorker::switchSignalSchedule(quint64 run,QString name){if(m_signal)m_signal->switchSchedule(run,name);}
void ChannelWorker::publishFrames(FrameBatch batch){
    for(auto&r:batch)normalizeFramePresentation(r);
    for(auto &r:batch){if(!r.replay)r.captureUs+=m_traceOffsetUs;r.captureUs=qMax(m_lastCaptureUs,r.captureUs);m_lastCaptureUs=r.captureUs;if(r.replay)m_traceOffsetUs=qMax(m_traceOffsetUs,r.captureUs-captureTimeUs());if(!r.typed){r.id=r.identifier.toUInt(nullptr,16);r.payload=QByteArray::fromHex(r.data.toLatin1());r.typed=true;}}
    emit observedFrames(batch);
    for(int i=batch.size()-1;i>=0;--i)if(batch[i].monitorHidden)batch.removeAt(i);
    if(!batch.isEmpty())emit framesReceived(batch);
}
void ChannelWorker::projectSignalEvents(const signal::BusFrameEvents &events){
    FrameBatch batch;for(const auto&e:events){FrameRecord f;stampFrame(f);f.channel=m_settings.softwareId;
        f.bus=e.bus;f.id=e.id;f.extended=e.extended;f.payload=e.bytes;f.typed=true;f.hardwareUs=e.hardwareUs;f.classicChecksum=e.classicChecksum;
        f.replay=e.replay;f.fd=e.fd;f.brs=e.brs;f.esi=e.esi;f.rtr=e.rtr;
        f.echo=e.source==signal::EventSource::HardwareEcho;f.request=e.source==signal::EventSource::RequestAccepted;
        f.simulated=m_settings.simulation||e.source==signal::EventSource::Simulated;f.noResponse=e.source==signal::EventSource::NoResponse;
        if(e.arrivalUs>0)f.captureUs=e.arrivalUs;
        const bool simulation=m_settings.simulation;f.direction=(simulation?"SIM ":"")+QString(e.source==signal::EventSource::NoResponse?"HEADER":e.source==signal::EventSource::HardwareEcho?"TX ECHO":e.source==signal::EventSource::Received?"RX":"TX");
        f.identifier=QString("0x%1").arg(e.id,e.bus==signal::Bus::Lin?2:(e.extended?8:3),16,QChar('0')).toUpper();
        f.data=QString::fromLatin1(e.bytes.toHex(' ')).toUpper();f.length=e.length<0?e.bytes.size():e.length;f.status=e.detail;f.error=!e.valid&&e.source!=signal::EventSource::NoResponse;f.warning=e.source==signal::EventSource::NoResponse;batch.append(f);
    }if(!batch.isEmpty())publishFrames(batch);
}
void ChannelWorker::sendDiagnostic(ChannelSettings settings,diag::Request request){
    auto reject=[this](const QString &error){emit diagnosticFinished(false,{},error);};
    if(settings.bus==Bus::Can&&(m_canBlocked||m_canUnresolved||!m_canEchoes.isEmpty()||(m_signal&&m_signal->hasUnconfirmedCanFrame()))){reject("CAN fault or an earlier transaction is unresolved; diagnostic not submitted");return;}
    QString error;
    if(m_traceEnabled&&(m_traceFailed||settings.bus!=Bus::Can||settings.simulation)){reject("Trace mode unavailable: evidence incomplete or not physical CAN.");return;}
    if(!m_operations.permits(OperationCoordinator::Task::Diagnostic)||m_running||m_scanning||m_manualBusy||!m_session||m_session->state()!=ConnectionState::Connected){reject("通道未连接或正在执行操作");return;}
    if(settings.bus!=m_settings.bus||settings.simulation!=m_settings.simulation||(!request.raw&&!diag::Codec::validate(request.service,request.bytes,error))){reject(error.isEmpty()?"通道设置已变化":error);return;}
    m_settings=settings;
    if(!m_manualMode){
        clearProtocol();boot::FlashProfile profile;
        if(!boot::FlashProfile::fromJson(settings.downloadProfile,profile,error)||!createProtocol(profile,error,true)){clearProtocol();reject(error);return;}
    }
    if(!m_uds){reject("UDS 会话不可用");return;}
    m_manualRequest=std::move(request);m_manualBusy=true;
    bool suppressed=false;for(const auto &f:m_manualRequest.service.request.fields)if(f.suppressible&&m_manualRequest.bytes.size()>1&&(quint8(m_manualRequest.bytes[1])&0x80))suppressed=true;
    bool functional=!m_manualRequest.service.physical&&m_manualRequest.service.functional;
    QByteArray expected=(m_manualRequest.raw||m_manualRequest.service.sid<0)?(m_manualRequest.bytes.isEmpty()?QByteArray{}:QByteArray(1,char(quint8(m_manualRequest.bytes[0])+0x40))):diag::Codec::expected(m_manualRequest.service,m_manualRequest.bytes);
    if(!m_uds->request(m_manualRequest.bytes,expected,
        [this](bool ok,const QByteArray &pdu,const QString &why){
            m_manualBusy=false;emit diagnosticFinished(ok,pdu,why.isEmpty()?(pdu.isEmpty()?"抑制正响应请求已完成":"收到正响应"):why);
        },suppressed,functional)){
        m_manualBusy=false;reject("UDS 请求队列拒绝请求");
    }
}

void ChannelWorker::cancelTask() {
    if(m_settings.bus==Bus::Can&&(!m_canEchoes.isEmpty()||(m_uds&&m_uds->hasActiveRequest()&&m_udsWriteAttempted))){m_canUnresolved=true;m_recoveryCloseSucceeded=false;}
    if(m_signal)m_signal->stop();
    if(m_manualMode&&!m_manualBusy&&m_uds)m_uds->cancel();
    if(m_manualBusy){if(m_uds)m_uds->cancel();m_manualBusy=false;emit diagnosticFinished(false,{},"诊断请求已取消或连接关闭");}
    if(m_scanning)stopScan("扫描已取消");
    if(!m_running)return;
    ++m_confirmationId;m_confirmationPending=false;m_stagedApp={};m_stagedDriver={};
    if(m_job)m_job->cancel();
    m_repeat->stop();m_running=m_waiting=false;m_receive->setInterval(20);
    m_operations.release(OperationCoordinator::Task::Download);
    emit taskChanged(TaskState::Cancelled,m_progress,"下载已取消");
    emit logMessage("用户取消或连接关闭，下载任务已停止。");
}
void ChannelWorker::receive() {
    m_receiveDrained=true;
    if(!m_session || m_session->state()!=ConnectionState::Connected || m_settings.simulation)return;
    FrameBatch batch;
    signal::BusFrameEvents events;QElapsedTimer budget;budget.start();
    if(m_lin) {
      do {
        TLINRcvMsg messages[64]={};const auto n=m_lin->recvRaw(64,messages);
        const auto error=m_lin->lastErrorCode();
        if(error!=errOK && error!=errRcvQueueEmpty) {
            const auto text=m_lin->lastErrorText();if(m_signal)m_signal->linkFailed(text);if(m_manualBusy){m_uds->cancel();m_manualBusy=false;emit diagnosticFinished(false,{},text);}if(m_running)failPreview(text);if(text!=m_receiveError){emit logMessage(text);m_receiveError=text;}break;
        }
        m_receiveError.clear();
        for(DWORD i=0;i<n;++i) {
            const auto &m=messages[i];if(m.Type!=mstStandard){
                if(m.Type>=mstOverrun){++m_scanErrors;emit logMessage("LIN 接收队列溢出。");if(m_signal)m_signal->linkFailed("LIN 接收队列溢出");if(m_running)failPreview("LIN 接收队列溢出");}continue;
            }
            const auto noResponseFlags=MSG_ERR_SLAVE_NOT_RESPONDING|MSG_ERR_TIMEOUT;
            const bool absent=(m.ErrorFlags&noResponseFlags) && !(m.ErrorFlags&~noResponseFlags);
            const bool bad=(m.ErrorFlags!=0 && !absent) || (m.ErrorFlags==0 && (m.Length<1 || m.Length>8));
            const bool valid=!m.ErrorFlags && m.Length>=1 && m.Length<=8;
            if(m_transport&&(m.FrameId&0x3f)==0x3c)m_transport->confirmTransmitted(QByteArray(reinterpret_cast<const char*>(m.Data),qMin(int(m.Length),8)),valid);
            if(m_transport)m_transport->receiveFrame(m.FrameId&0x3f,QByteArray(reinterpret_cast<const char*>(m.Data),qMin(int(m.Length),8)),absent,bad);
            FrameRecord record;stampFrame(record);record.channel=m_settings.softwareId;
            record.bus=signal::Bus::Lin;record.id=m.FrameId&0x3f;record.typed=true;
            record.payload=QByteArray(reinterpret_cast<const char*>(m.Data),qMin(int(m.Length),8));
            record.hardwareUs=m.TimeStamp;record.checksum=m.Checksum;record.classicChecksum=m.ChecksumType==cstClassic;
            record.errorFlags=m.ErrorFlags;record.noResponse=absent;
            const auto frameId=quint8(m.FrameId&0x3f);const bool transmitReadback=(m_signal&&m_signal->running())?(m.Direction==dirPublisher&&m_signal->publishesLin(frameId)):frameId==0x3c;
            signal::BusFrameEvent event;event.bus=signal::Bus::Lin;event.id=frameId;event.bytes=QByteArray(reinterpret_cast<const char*>(m.Data),qMin(int(m.Length),8));
            event.hardwareUs=m.TimeStamp;event.arrivalUs=captureTimeUs();event.valid=valid;
            event.source=absent?signal::EventSource::NoResponse:transmitReadback?signal::EventSource::HardwareEcho:signal::EventSource::Received;
            event.detail=absent?"无从节点响应":bad?QString("LIN 错误 0x%1").arg(m.ErrorFlags,0,16):"有效总线帧";events.append(event);
            record.direction="RX";record.identifier=QString("0x%1").arg(frameId,2,16,QChar('0')).toUpper();
            record.echo=transmitReadback;
            record.length=m.Length;
            if(valid)record.data=QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(m.Data),qMin(int(m.Length),8)).toHex(' ')).toUpper();
            record.status=transmitReadback&&valid?"发送回读":(absent?"无从节点响应":(bad?QString("LIN 错误 0x%1").arg(m.ErrorFlags,0,16):"有效响应"));
            record.error=bad;record.warning=absent;
            record.monitorHidden=transmitReadback&&!m_settings.rxdEnabled;batch.append(record);
            if(m_scanning){++m_scanEvents;if(valid)++m_scanResponses;if(bad)++m_scanErrors;}
        }
        m_receiveDrained=n<64;if(m_receiveDrained)break;
      }while(budget.nsecsElapsed()<2000000);
    } else if(m_can) {
      {
        TPCANMsg messages[64]={};TPCANTimestamp stamps[64]={};QElapsedTimer readTimer;if(m_traceEnabled)readTimer.start();const auto n=m_can->recvRaw(64,messages,stamps);
        const qint64 readDurationUs=m_traceEnabled?readTimer.nsecsElapsed()/1000:-1;
        const qint64 readEndUs=m_traceEnabled?captureTimeUs():-1;
        // Avoid synchronous trace writes for routine empty polls while retaining
        // slow empty reads that can explain gaps before the next received frame.
        if(m_traceEnabled&&n==0&&readDurationUs>=10000){TraceTiming timing;timing.hardwareOperation="CAN_READ_EMPTY_SLOW";timing.hardwareDurationUs=readDurationUs;if(!traceRecord("PROFILE",0,0,{},false,timing))return;}
        const auto status=m_can->lastErrorCode();const auto statusText=m_can->lastErrorText();
        const auto observed=m_can->classifyStatus(status);
        QElapsedTimer statusTimer;if(m_traceEnabled)statusTimer.start();
        const auto polled=m_can->busState();
        const auto liveStatus=m_can->lastErrorCode();
        const qint64 statusDurationUs=m_traceEnabled?statusTimer.nsecsElapsed()/1000:-1;
        const bool supported=polled!=CanBusState::Unsupported;
        bool batchFault=false;
        if(supported){
            const auto state=(observed==CanBusState::BusOff||observed==CanBusState::Fatal||observed==CanBusState::Warning)?observed:polled;
            if(state!=CanBusState::Ready&&!m_canBlocked&&m_traceEnabled){
                TraceTiming readTiming;readTiming.hardwareOperation=QString("CAN_READ_STATUS_0x%1").arg(status,0,16);readTiming.hardwareDurationUs=readDurationUs;
                TraceTiming liveTiming;liveTiming.hardwareOperation=QString("CAN_GET_STATUS_0x%1").arg(liveStatus,0,16);liveTiming.hardwareDurationUs=statusDurationUs;
                if(!traceRecord("PROFILE",0,0,{},false,readTiming)||!traceRecord("PROFILE",0,0,{},false,liveTiming))return;
            }
            batchFault=handleCanState(state,statusText);
        }else{
            batchFault=status!=PCAN_ERROR_OK&&status!=PCAN_ERROR_QRCVEMPTY;
            if(m_canTransport&&batchFault)m_canTransport->linkFailed(statusText);
            if(m_signal&&batchFault)m_signal->linkFailed(statusText);
        }
        // A status frame can follow data in the same hardware batch. Stop the
        // transaction before delivering any part of a faulted batch.
        for(DWORD i=0;i<n;++i){
            const auto reported=m_can->statusFrame(messages[i]);
            if(reported!=CanBusState::Unsupported&&reported!=CanBusState::Ready){
                if(!m_canBlocked&&m_traceEnabled){
                    const auto &m=messages[i];const quint32 raw=(quint32(m.DATA[0])<<24)|(quint32(m.DATA[1])<<16)|(quint32(m.DATA[2])<<8)|quint32(m.DATA[3]);
                    TraceTiming timing;timing.hardwareOperation=QString("CAN_STATUS_FRAME_0x%1").arg(raw,0,16);timing.hardwareDurationUs=readDurationUs;
                    if(!traceRecord("PROFILE",0,0,{},false,timing))return;
                }
                batchFault=handleCanState(reported,"PCAN status frame")||batchFault;
            }
        }
        for(DWORD i=0;i<n;++i) {
            const auto &m=messages[i];
            boot::CanFrame frame{m.ID,bool(m.MSGTYPE&PCAN_MESSAGE_EXTENDED),bool(m.MSGTYPE&PCAN_MESSAGE_RTR),bool(m.MSGTYPE&(PCAN_MESSAGE_ERRFRAME|PCAN_MESSAGE_STATUS)),bool(m.MSGTYPE&PCAN_MESSAGE_FD),QByteArray(reinterpret_cast<const char*>(m.DATA),qMin(int(m.LEN),8))};
            const bool echo=m.MSGTYPE&PCAN_MESSAGE_ECHO;
            signal::BusFrameEvent event;event.bus=signal::Bus::Can;event.id=m.ID;event.extended=frame.extended;event.bytes=frame.data;
            event.rtr=frame.rtr;event.length=m.LEN;event.valid=!frame.error&&!frame.fd&&m.LEN<=8;event.source=echo?signal::EventSource::HardwareEcho:signal::EventSource::Received;
            event.hardwareUs=(quint64(stamps[i].millis_overflow)<<32)*1000+quint64(stamps[i].millis)*1000+stamps[i].micros;
            event.arrivalUs=captureTimeUs();event.detail=event.valid?"CAN 总线帧":"CAN 状态/错误/非经典数据帧";events.append(event);
            int echoIndex=-1;
            if(echo)for(int k=0;k<m_canEchoes.size();++k){const auto &pending=m_canEchoes[k];
                if(pending.frame.id==frame.id&&pending.frame.extended==frame.extended&&pending.frame.data==frame.data){echoIndex=k;break;}
            }
            if(echoIndex>=0&&(echoIndex>0||!m_session||m_canEchoes[echoIndex].generation!=m_session->generation())){
                m_canUnresolved=true;m_recoveryCloseSucceeded=false;batchFault=true;echoIndex=-1;
                if(m_canTransport)m_canTransport->linkFailed("CAN transmit echoes arrived out of order");
            }
            TraceTiming timing;timing.driverTimestampUs=qint64(event.hardwareUs);timing.hostReadEndUs=readEndUs;timing.hardwareOperation="CAN_READ_BATCH";timing.hardwareDurationUs=readDurationUs;
            timing.frameFormat=frame.extended?"EXT":"STD";if(frame.rtr)timing.frameFormat+="|RTR";if(frame.error)timing.frameFormat+="|ERR";if(frame.fd)timing.frameFormat+="|FD";
            if(echoIndex>=0)timing.sendCallUs=m_canEchoes[echoIndex].sendCallUs;
            if(!traceRecord(echo?"TX echo":"RX",m.ID,m.LEN,frame.data,event.valid,timing))return;
            if(frame.error&&!supported&&m_signal)m_signal->linkFailed(event.detail);
            if(echo){
                if(echoIndex>=0){const auto pending=m_canEchoes[echoIndex];m_canEchoes.removeAt(echoIndex);if(!batchFault&&pending.owner)pending.owner->confirmTransmitted(pending.token);}
            }else if(!batchFault&&m_canTransport){
                if(frame.error)m_canTransport->linkFailed("CAN 总线错误或状态帧");
                else m_canTransport->receiveFrame(frame);
            }
            FrameRecord r;stampFrame(r);
            r.typed=true;r.id=m.ID;r.extended=frame.extended;r.fd=frame.fd;r.rtr=frame.rtr;r.payload=frame.data;
            r.echo=echo;r.hardwareUs=event.hardwareUs;r.error=frame.error||frame.fd||m.LEN>8;r.errorFlags=m.MSGTYPE;
            r.channel=m_settings.softwareId;r.direction=echo?"TX ECHO":"RX";r.identifier=QString("0x%1").arg(m.ID,0,16).toUpper();
            r.length=m.LEN;r.data=QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(m.DATA),qMin(int(m.LEN),8)).toHex(' ')).toUpper();
            r.status=frame.fd?"当前经典 CAN 接口无法完整采集 CAN FD":(m.MSGTYPE&PCAN_MESSAGE_STATUS)?"硬件状态":"CAN";r.hardwareStatus=bool(m.MSGTYPE&PCAN_MESSAGE_STATUS);r.warning=m.MSGTYPE&PCAN_MESSAGE_STATUS;batch.append(r);
        }
        // Yield after at most 64 CAN frames; a full batch schedules an
        // immediate continuation below even if no further event arrives.
        m_receiveDrained=n<64;
      }
    }
    if(!batch.isEmpty())publishFrames(batch);
    if(!events.isEmpty()){if(m_signal&&!m_canBlocked)m_signal->observe(events);emit busEvents(events);}
    if(!m_receiveDrained){if(!m_receivePump->isActive())m_receivePump->start(0);}
    else if(!m_canNotifier&&!m_canEchoes.isEmpty()&&m_canTransport&&m_canTransport->busy()){
        if(!m_receivePump->isActive())m_receivePump->start(1);
    }
}
void ChannelWorker::startHeaderScan() {
    QString detail;const auto health=m_lin?m_lin->hardwareHealth(&detail):Health::Removed;
    if(!m_operations.permits(OperationCoordinator::Task::Scan) || m_scanning || m_running || m_manualBusy || !m_lin || !m_session || m_session->state()!=ConnectionState::Connected ||
       (health!=Health::Ready && health!=Health::Sleeping)) {
        emit logMessage("扫描需要处于活动状态的真实 LIN 主机连接。"+detail);emit commandFinished();return;
    }
    QString error;if(!diagnosticMaster(error)){emit logMessage(error);emit commandFinished();return;}
    clearProtocol();m_operations.acquire(OperationCoordinator::Task::Scan);m_lin->clearMsg();m_scanning=true;m_scanId=0;m_scanSent=m_scanEvents=m_scanResponses=m_scanErrors=0;
    m_scan->setInterval(qMax(100,2000000/m_settings.bitrate));
    emit scanChanged(true,"扫描 00–3B、3D · 仅帧头");emit logMessage("开始帧头扫描；不发送控制数据或 UDS 请求。");
    m_scan->start();scanTick();emit commandFinished();
}
void ChannelWorker::scanTick() {
    if(!m_scanning)return;
    if(m_scanId==0x3c)++m_scanId;
    if(m_scanId>0x3d) {
        receive();
        m_scanErrors+=qAbs(m_scanSent-m_scanEvents);
        stopScan(QString("扫描完成 · %1 个帧头 / %2 个事件 / %3 个有效响应 / %4 个错误")
                 .arg(m_scanSent).arg(m_scanEvents).arg(m_scanResponses).arg(m_scanErrors));
        return;
    }
    TLINMsg message={};message.FrameId=BYTE(m_scanId++);message.Length=8;
    message.Direction=dirSubscriberAutoLength;message.ChecksumType=message.FrameId==0x3d?cstClassic:cstAuto;
    if(!m_lin->sendRaw(message)){stopScan("扫描失败："+m_lin->lastErrorText());return;}
    ++m_scanSent;
}
void ChannelWorker::stopScan(const QString &text){m_scan->stop();m_scanning=false;m_operations.release(OperationCoordinator::Task::Scan);emit scanChanged(false,text);emit logMessage(text);}
void ChannelWorker::shutdown() {
    stopReceiveNotifier();
    if(m_signal){m_signal->stop();m_signal.reset();}
    clearProtocol();
    if(m_receive)m_receive->stop();
    if(m_scan)m_scan->stop();
    if(m_repeat)m_repeat->stop();
    m_running=m_scanning=m_waiting=false;
    m_session.reset();m_can.reset();m_lin.reset();m_canEchoes.clear();
}
}
