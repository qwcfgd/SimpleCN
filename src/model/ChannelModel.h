#pragma once
#include <QThread>
#include "domain/HostTypes.h"
#include "domain/SignalTypes.h"
#include "model/CddDatabase.h"
namespace host {
class ChannelWorker;
class ChannelModel : public QObject {
    Q_OBJECT
public:
    explicit ChannelModel(ChannelSettings,QObject *parent=nullptr);
    ~ChannelModel() override;
signals:
    void settingsRequested(host::ChannelSettings);
    void connectionRequested(host::ChannelSettings);
    void disconnectionRequested();
    void refreshRequested();
    void previewRequested(host::ChannelSettings);
    void cancelRequested();
    void scanRequested();
    void diagnosticRequested(host::ChannelSettings,host::diag::Request);
    void resetDiagnosticRequested();
    void signalsRequested(host::signal::TxPlan);
    void signalStopRequested(quint64);
    void signalPayloadRequested(host::signal::PayloadUpdate);
    void signalScheduleRequested(quint64,QString);
    void signalStatus(host::signal::RunStatus);
    void busEvents(host::signal::BusFrameEvents);
    void connectionGenerationChanged(quint64);
    void diagnosticFinished(bool,QByteArray,QString);
    void diagnosticActivity(bool);
    void bindingChanged(QString,quint32);
    void ready();
    void hardwareChanged(communication::HardwareChannels);
    void stateChanged(communication::ConnectionState,QString);
    void healthChanged(communication::Health,QString);
    void framesReceived(host::FrameBatch);
    void logMessage(QString);
    void taskChanged(host::TaskState,int,QString);
    void scanChanged(bool,QString);
    void commandFinished();
private: QThread m_thread;ChannelWorker *m_worker;
};
}
