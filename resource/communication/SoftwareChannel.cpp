#include "SoftwareChannel.h"
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QScopedValueRollback>
namespace communication {
namespace {
QMutex leasesMutex;
QHash<QString,const SoftwareChannel*> leases;
}
SoftwareChannel::SoftwareChannel(const SoftwareChannelConfiguration &config,std::unique_ptr<HardwareBackend> backend,QObject *parent)
    : QObject(parent),m_config(config),m_backend(std::move(backend))
{
    Q_ASSERT(m_backend);
    m_timer.setParent(this);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer,&QTimer::timeout,this,&SoftwareChannel::poll);
    qRegisterMetaType<HardwareChannels>();
    qRegisterMetaType<ConnectionState>();
    qRegisterMetaType<Health>();
}
SoftwareChannel::~SoftwareChannel()
{
    m_timer.stop();
    // Consumers may already be being destroyed; do not emit during destruction.
    blockSignals(true);
    QString ignored;
    m_backend->close(ignored);
    releaseLease();
}
bool SoftwareChannel::configure(const SoftwareChannelConfiguration &config)
{
    if (m_busy || m_state==ConnectionState::Connected || !config.isValid() || config.bus!=m_backend->bus()) return false;
    m_config=config;
    m_wantConnected=false;
    return true;
}
void SoftwareChannel::startMonitoring(int intervalMs)
{
    m_timer.start(qMax(100,intervalMs));
    QTimer::singleShot(0,this,&SoftwareChannel::poll);
}
void SoftwareChannel::transition(ConnectionState state,const QString &detail)
{
    if (m_state==state && m_error==detail) return;
    m_state=state; m_error=detail;
    emit stateChanged(state,detail);
}
bool SoftwareChannel::refresh()
{
    QString error;
    auto found=m_backend->scan(error);
    if (!error.isEmpty()) {
        m_scanError=error;
        ++m_failures;
        if (!m_scanned || (m_failures>=2 && !m_hardware.isEmpty())) {
            m_scanned=true; m_hardware.clear(); emit hardwareChanged(m_hardware);
        }
        return false;
    }
    m_failures=0;
    m_scanError.clear();
    {
        QMutexLocker lock(&leasesMutex);
        for(auto &h:found) {
            const auto owner=leases.value(QString("%1:%2").arg(static_cast<int>(h.bus)).arg(h.handle),nullptr);
            if(owner && owner!=this)h.available=false;
        }
    }
    if (!m_scanned || found!=m_hardware) {
        m_scanned=true; m_hardware=found;
        emit hardwareChanged(m_hardware);
    }
    return true;
}
bool SoftwareChannel::choose(HardwareChannel &selected) const
{
    int matches=0;
    for (const auto &h:m_hardware) {
        if (h.bus!=m_config.bus || !h.available) continue;
        if (!m_config.hardwareKey.isEmpty()) {
            if (h.key!=m_config.hardwareKey) continue;
        } else if (m_config.preferredHandle && h.handle!=m_config.preferredHandle) continue;
        selected=h; ++matches;
    }
    // Ambiguous identities must never silently attach a different device.
    return matches==1;
}
bool SoftwareChannel::connectChannel()
{
    if (m_busy) return false;
    if (m_state==ConnectionState::Connected) return true;
    QScopedValueRollback<bool> guard(m_busy,true);
    m_wantConnected=true;
    m_retry.restart();
    ++m_generation;
    if (!m_config.isValid() || m_config.bus!=m_backend->bus()) {
        transition(ConnectionState::Fault,"Invalid software-channel configuration"); return false;
    }
    if (!refresh()) { transition(ConnectionState::Fault,m_scanError); return false; }
    HardwareChannel selected;
    if (!choose(selected)) {
        transition(m_hardware.isEmpty()?ConnectionState::Missing:ConnectionState::Fault,
                   "Select one available hardware channel (missing, occupied or ambiguous selection)");
        return false;
    }
    const QString lease=QString("%1:%2").arg(static_cast<int>(selected.bus)).arg(selected.handle);
    bool occupied=false;
    {
        QMutexLocker lock(&leasesMutex);
        occupied=leases.contains(lease);
        if (!occupied) { leases.insert(lease,this); m_lease=lease; }
    }
    if (occupied) {
        transition(ConnectionState::Fault,"Hardware channel is bound to another software channel");
        return false;
    }
    transition(ConnectionState::Connecting);
    QString error;
    if (!m_backend->open(selected,m_config,error)) {
        QString cleanup; m_backend->close(cleanup); releaseLease();
        transition(ConnectionState::Fault,error.isEmpty()?QStringLiteral("Hardware initialization failed"):error);
        return false;
    }
    m_active=selected;
    m_config.hardwareKey=selected.key;
    m_config.preferredHandle=selected.handle;
    m_health=Health::Ready;
    transition(ConnectionState::Connected);
    emit healthChanged(m_health,QStringLiteral("Channel initialized"));
    return true;
}
void SoftwareChannel::releaseLease()
{
    QMutexLocker lock(&leasesMutex);
    if (!m_lease.isEmpty() && leases.value(m_lease)==this) leases.remove(m_lease);
    m_lease.clear();
}
bool SoftwareChannel::disconnectChannel()
{
    if (m_busy) return false;
    QScopedValueRollback<bool> guard(m_busy,true);
    m_wantConnected=false;
    if (m_state!=ConnectionState::Connected && m_lease.isEmpty()) return true;
    ++m_generation;
    transition(ConnectionState::Disconnecting);
    emit connectionClosing();
    QString error;
    const bool ok=m_backend->close(error);
    releaseLease(); m_active=HardwareChannel();
    transition(m_hardware.isEmpty()?ConnectionState::Missing:ConnectionState::Available,error);
    return ok;
}
void SoftwareChannel::lose(ConnectionState next,const QString &reason)
{
    ++m_generation;
    emit connectionClosing();
    QString cleanup;
    m_backend->close(cleanup);
    releaseLease(); m_active=HardwareChannel();
    m_retry.restart();
    transition(next,reason);
}
void SoftwareChannel::poll()
{
    if (m_busy) return;
    QScopedValueRollback<bool> guard(m_busy,true);
    if (!refresh()) {
        if (m_state==ConnectionState::Connected && m_failures>=2) lose(ConnectionState::Fault,m_scanError);
        else if (m_state!=ConnectionState::Connected) transition(ConnectionState::Fault,m_scanError);
        return;
    }
    if (m_state==ConnectionState::Connected) {
        bool present=false;
        for (const auto &h:m_hardware)
            if (h.key==m_active.key && h.handle==m_active.handle) present=true;
        if (!present) { lose(ConnectionState::Missing,"Hardware removed; reconnect will initialize a new handle"); return; }
        QString detail;
        const auto health=m_backend->health(detail);
        if (health!=m_health) { m_health=health; emit healthChanged(health,detail); }
        if (health==Health::Removed || health==Health::Error) lose(ConnectionState::Fault,detail);
        return;
    }
    HardwareChannel selected;
    const bool available=choose(selected);
    if (m_wantConnected && m_config.autoReconnect && available && selected.persistentIdentity && (!m_retry.isValid() || m_retry.elapsed()>=1000)) {
        // connectChannel has its own reentrancy guard.
        m_busy=false;
        connectChannel();
    } else if (m_state!=ConnectionState::Fault || available) {
        const QString detail=(m_wantConnected && m_config.autoReconnect && available && !selected.persistentIdentity)
            ? QStringLiteral("Hardware identity is unavailable; select and reconnect manually") : QString();
        transition(m_hardware.isEmpty()?ConnectionState::Missing:ConnectionState::Available,detail);
    }
}
}
