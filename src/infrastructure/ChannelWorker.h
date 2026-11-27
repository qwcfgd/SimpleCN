#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include "protocol/SimulatedLinEcu.h"
#include "protocol/SimulatedCanEcu.h"
#include "domain/HostTypes.h"
#include "model/CddDatabase.h"
#include <QPointer>
#include <QSet>
#include <functional>
#include "communication/SoftwareChannel.h"
#include "SignalTransmitter.h"
#include "SafeTraceRecorder.h"
#include "BusHardware.h"
#include "protocol/OperationCoordinator.h"
namespace host {
class ChannelWorker : public QObject {
    Q_OBJECT
public:
    using CanFactory=std::function<std::unique_ptr<CanHardware>()>;
    using LinFactory=std::function<std::unique_ptr<LinHardware>()>;
    explicit ChannelWorker(ChannelSettings settings,CanFactory canFactory={},LinFactory linFactory={}):m_settings(std::move(settings)),m_canFactory(std::move(canFactory)),m_linFactory(std::move(linFactory)){}
    ~ChannelWorker() override { stopReceiveNotifier(); }
public slots:
    void initialize();
    void updateSettings(host::ChannelSettings);
    void connectChannel(host::ChannelSettings);
    void disconnectChannel();
    void refresh();
    void startPreview(host::ChannelSettings);
    void cancelTask();
    void startHeaderScan();
    void sendDiagnostic(host::ChannelSettings,host::diag::Request,bool manualStart=true);
    void resetDiagnostic();
    void startSignals(host::signal::TxPlan);
    void stopSignals(quint64);
    void updateSignalPayload(host::signal::PayloadUpdate);
    void switchSignalSchedule(quint64,QString);
    void shutdown();
signals:
    void bindingChanged(QString,quint32);
    void ready();
    void hardwareChanged(communication::HardwareChannels);
    void stateChanged(communication::ConnectionState,QString,bool quiet=false);
    void healthChanged(communication::Health,QString,bool quiet=false);
    void framesReceived(host::FrameBatch);
    void observedFrames(host::FrameBatch);
    void peakReads(host::PeakReadBatch);
    void logMessage(QString);
    void taskChanged(host::TaskState,int,QString);
    void scanChanged(bool,QString);
    void commandFinished();
    void diagnosticFinished(bool,QByteArray,QString);
    void diagnosticActivity(bool);
    void signalStatus(host::signal::RunStatus);
    void busEvents(host::signal::BusFrameEvents);
    void connectionGenerationChanged(quint64);
private:
    bool traceRecord(const QString &,quint32,int,const QByteArray &,bool valid=true,const TraceTiming &timing=TraceTiming{});
    void traceDeadline(const QString &,int,qint64);
    void traceFailure();
    void createSession();
    void bindCanReceiveNotifier(bool pumpImmediately=true);
    void stopReceiveNotifier();
    bool apply(const ChannelSettings &);
    void receive();
    bool handleCanState(CanBusState,const QString&);
    void withCanStartPermission(bool manualStart,std::function<void()> start,
                                std::function<void(const QString&)> reject);
    void beginSignalRun(const signal::TxPlan &);
    void beginManualDiagnostic(ChannelSettings,diag::Request);
    void beginPreviewRound();
    void failPreview(const QString &);
    void beginDownload();
    void completeRound(const QString &result);
    void clearProtocol();
    bool createProtocol(const boot::FlashProfile &,QString &error,bool manual);
    void scanTick();
    void stopScan(const QString &);
    bool diagnosticMaster(QString &error);
    bool observeDownloadWake(const TLINRcvMsg &message);
    void projectSignalEvents(const host::signal::BusFrameEvents &);
    void publishFrames(host::FrameBatch);
    void stampPeakRead(FrameRecord &record);
    QSharedPointer<PeakRead> peakEvent();
    PeakReadBatch capturePlin(const TLINRcvMsg *,DWORD,TLINError,bool monitor);
    PeakReadBatch capturePcan(const TPCANMsg *,const TPCANTimestamp *,DWORD,bool tail,unsigned long status);
    bool clearLinQueue();
    void noteLinCancel(const QString &origin);
    QUuid m_peakSession;
    quint64 m_peakSequence=0,m_linSubmission=0;
    qint64 m_writeBeginUs=0;
    TLINMsg m_submittedMessage{};
    bool m_linEvidence=false;
    quint64 m_peakClockDomain=0,m_lastPeakStamp=0;
    ChannelSettings m_settings;
    CanFactory m_canFactory;
    LinFactory m_linFactory;
    SafeTraceRecorder m_safeTrace;
    QSet<quint32> m_traceDiagnosticIds;
    bool m_traceEnabled=false,m_traceFailed=false;
    std::unique_ptr<CanHardware> m_can;
    std::unique_ptr<LinHardware> m_lin;
    std::unique_ptr<communication::SoftwareChannel> m_session;
    std::unique_ptr<boot::SimulatedLinEcu> m_ecu;
    std::unique_ptr<boot::LinTransport> m_transport;
    std::unique_ptr<boot::SimulatedCanEcu> m_canEcu;
    std::unique_ptr<boot::CanTransport> m_canTransport;
    std::unique_ptr<boot::UdsSession> m_uds;
    std::unique_ptr<boot::FlashJob> m_job;
    quint64 m_wakeEarliestHeaderUs=0;
    LinWakeResult m_wakeRequest=LinWakeResult::Unsupported;
    std::function<void()> m_afterWake;
    QByteArray m_firstLinWrite;
    QString m_firstLinStep;
    bool m_canDownloadReception=false;
    void restoreCanErrorReception();
    QTimer *m_receive=nullptr,*m_receivePump=nullptr,*m_scan=nullptr,*m_repeat=nullptr;
    QObject *m_canNotifier=nullptr;
    qint64 m_traceOffsetUs=0,m_lastCaptureUs=0;
    bool m_running=false,m_scanning=false,m_waiting=false;
    int m_round=0,m_totalRounds=1;
    int m_progress=0,m_scanId=0,m_scanSent=0,m_scanEvents=0,m_scanResponses=0,m_scanErrors=0;
    QString m_receiveError;
    bool m_manualMode=false,m_manualBusy=false;
    diag::Request m_manualRequest;
    QMap<int,QByteArray> m_diagnosticDids;
    struct CanEcho {QPointer<boot::CanTransport> owner;quint64 token;boot::CanFrame frame;qint64 sendCallUs=-1;quint64 generation=0;};
    QQueue<CanEcho> m_canEchoes;
    std::unique_ptr<SignalTransmitter> m_signal;
    OperationCoordinator m_operations;
    quint64 m_lastSignalRun=0;
    bool m_receiveDrained=true;
    bool m_canBlocked=false,m_canHealthInitialized=false,m_canUnresolved=false;
    bool m_reconnectingCan=false,m_canStartAfterReconnect=false;
    CanBusState m_reportedCanState=CanBusState::Unsupported;
    bool m_udsWriteAttempted=false,m_recoveryCloseSucceeded=false;
};
}
