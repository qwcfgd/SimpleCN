#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include "protocol/SimulatedLinEcu.h"
#include "protocol/SimulatedCanEcu.h"
#include "domain/HostTypes.h"
#include "model/CddDatabase.h"
#include <QPointer>
#include "communication/SoftwareChannel.h"
#include "SignalTransmitter.h"
#include "BusHardware.h"
#include "protocol/OperationCoordinator.h"
namespace host {
class ChannelWorker : public QObject {
    Q_OBJECT
public:
    explicit ChannelWorker(ChannelSettings settings):m_settings(std::move(settings)){}
public slots:
    void initialize();
    void updateSettings(host::ChannelSettings);
    void connectChannel(host::ChannelSettings);
    void disconnectChannel();
    void refresh();
    void startPreview(host::ChannelSettings);
    void cancelTask();
    void startHeaderScan();
    void sendDiagnostic(host::ChannelSettings,host::diag::Request);
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
    void stateChanged(communication::ConnectionState,QString);
    void healthChanged(communication::Health,QString);
    void framesReceived(host::FrameBatch);
    void observedFrames(host::FrameBatch);
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
    void createSession();
    bool apply(const ChannelSettings &);
    void receive();
    void beginPreviewRound();
    void failPreview(const QString &);
    void beginDownload();
    void completeRound(const QString &result);
    void clearProtocol();
    bool createProtocol(const boot::FlashProfile &,QString &error,bool manual);
    void scanTick();
    void stopScan(const QString &);
    bool diagnosticMaster(QString &error);
    void projectSignalEvents(const host::signal::BusFrameEvents &);
    void publishFrames(host::FrameBatch);
    ChannelSettings m_settings;
    std::unique_ptr<CanHardware> m_can;
    std::unique_ptr<LinHardware> m_lin;
    std::unique_ptr<communication::SoftwareChannel> m_session;
    std::unique_ptr<boot::SimulatedLinEcu> m_ecu;
    std::unique_ptr<boot::LinTransport> m_transport;
    std::unique_ptr<boot::SimulatedCanEcu> m_canEcu;
    std::unique_ptr<boot::CanTransport> m_canTransport;
    std::unique_ptr<boot::UdsSession> m_uds;
    std::unique_ptr<boot::FlashJob> m_job;
    QTimer *m_receive=nullptr,*m_scan=nullptr,*m_repeat=nullptr;
    qint64 m_traceOffsetUs=0,m_lastCaptureUs=0;
    bool m_running=false,m_scanning=false,m_waiting=false;
    int m_round=0,m_totalRounds=1;
    int m_progress=0,m_scanId=0,m_scanSent=0,m_scanEvents=0,m_scanResponses=0,m_scanErrors=0;
    QString m_receiveError;
    bool m_manualMode=false,m_manualBusy=false;
    diag::Request m_manualRequest;
    QMap<int,QByteArray> m_diagnosticDids;
    struct CanEcho {QPointer<boot::CanTransport> owner;quint64 token;boot::CanFrame frame;};
    QQueue<CanEcho> m_canEchoes;
    std::unique_ptr<SignalTransmitter> m_signal;
    OperationCoordinator m_operations;
    quint64 m_lastSignalRun=0;
    bool m_receiveDrained=true;
};
}
