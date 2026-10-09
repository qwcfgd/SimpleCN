#pragma once
#include "HardwareBackend.h"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
namespace communication {
class SoftwareChannel : public QObject {
    Q_OBJECT
public:
    SoftwareChannel(const SoftwareChannelConfiguration &, std::unique_ptr<HardwareBackend>, QObject *parent=nullptr);
    ~SoftwareChannel() override;
    const SoftwareChannelConfiguration &configuration() const { return m_config; }
    const HardwareChannels &hardware() const { return m_hardware; }
    ConnectionState state() const { return m_state; }
    quint64 generation() const { return m_generation; }
    QString error() const { return m_error; }
    bool configure(const SoftwareChannelConfiguration &);
    void startMonitoring(int intervalMs=500);
public slots:
    void poll();
    bool connectChannel();
    bool disconnectChannel();
signals:
    void hardwareChanged(const communication::HardwareChannels &channels);
    void stateChanged(communication::ConnectionState state, const QString &detail);
    void healthChanged(communication::Health health, const QString &detail);
    void connectionClosing(); // Stop consumers before the SDK resources are released.
private:
    void transition(ConnectionState,const QString &detail=QString());
    bool refresh();
    bool choose(HardwareChannel &selected) const;
    void lose(ConnectionState,const QString &);
    void releaseLease();
    SoftwareChannelConfiguration m_config;
    std::unique_ptr<HardwareBackend> m_backend;
    QTimer m_timer;
    QElapsedTimer m_retry;
    HardwareChannels m_hardware;
    HardwareChannel m_active;
    ConnectionState m_state=ConnectionState::Missing;
    Health m_health=Health::Removed;
    QString m_error;
    QString m_scanError;
    QString m_lease;
    quint64 m_generation=0;
    int m_failures=0;
    bool m_scanned=false;
    bool m_wantConnected=false;
    bool m_busy=false;
};
}
