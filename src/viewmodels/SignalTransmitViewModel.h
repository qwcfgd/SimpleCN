#pragma once
#include "domain/SignalTypes.h"
#include "domain/HostTypes.h"
#include <QObject>
#include <functional>
#include <QFutureWatcher>
#include <QJsonObject>
#include "infrastructure/TraceReader.h"
namespace host {
class SignalTransmitViewModel : public QObject {
    Q_OBJECT
public:
    explicit SignalTransmitViewModel(signal::Bus,QObject *parent=nullptr);
    signal::Database database()const{return m_database;}
    const signal::WorkingSet &working()const{return m_work;}
    const signal::RunStatus &status()const{return m_status;}
    QVector<signal::FrameDefinition> definitions()const;
    QVector<signal::FrameDefinition> queuedDefinitions()const;
    bool selectCanNode(const QString&,const QString &direction,QString&error);
    bool removeQueuedFrame(const QString&,QString&error);
    const signal::FrameDefinition *frame(const QString&)const;
    bool running()const{return signal::active(m_status.state)||m_status.cleanupPending||m_replayGroupRunning;}
    bool replayRunning()const{return m_replayGroupRunning||(m_replaying&&signal::active(m_status.state));}
    bool canStructure()const{return !running()&&!m_externalBusy&&!m_importing;}
    bool canData()const{return !m_status.cleanupPending&&!m_externalBusy&&!m_importing&&!(running()&&m_work.role==signal::LinRole::Monitor&&m_bus==signal::Bus::Lin);}
    bool canStart()const{return canStructure()&&m_connected;}
    bool canBack()const{return canStructure()&&!m_history.isEmpty();}
    bool canRestore()const{return canStructure()&&m_hasBaseline;}
    bool importing()const{return m_importing;}
    QString message()const{return m_message;}
    bool hasInvalidDraft()const;
    QString protocolInfo(const QString&)const;
    QString publicationHint(const QString&)const;
    void setAvailability(bool connected,bool otherTaskBusy,int bitrate,quint64 connection);
    bool importFile(const QString&,QString&error);
    void importAsync(const QString&);
    bool editSignal(const QString &key,int field,const QString&,bool physical,QString&error,int range=-1);
    bool editPayload(const QString&,const QString&,QString&error);
    bool setFrameEnabled(const QString&,bool,QString&error);
    bool setCanOptions(const QString&,bool enabled,int period,QString&error);
    bool putCustom(const QString &oldKey,const QString &name,quint32 id,int length,int cycle,QString&error);
    bool removeCustom(const QString&,QString&error);
    bool setRole(signal::LinRole,const QString&,QString&error);
    bool selectSchedule(const QString&,QString&error);
    bool replaceSchedules(const QVector<signal::Schedule>&,const QString&,QString&error);
    void back();void restore();
    void readConfigurationAsync(const QJsonObject&,const QString &baseDirectory);
    QJsonObject configuration(const QString &relativeTo=QString())const;
    bool readConfiguration(const QJsonObject&,const QString &baseDirectory,QString&error);
    bool start(bool periodic,QString&error);
    bool hasEnabled()const;
    void setReplayGroupRunning(bool);
    bool replayConfigured()const{return !m_work.replaySettings.value("path").toString().isEmpty();}
    const TraceLog &replayLog()const{return m_replayLog;}
    void importReplay(const QString&);void resetReplay();void setReplayMapping(const QJsonObject&);
    bool startReplayPlan(signal::TxPlan,QString&);
    std::function<QStringList(signal::Bus)> replayTargets;
    bool changeId(const QString&,quint32,QString&);
    bool createSchedule(QString&);
    bool setLinDelay(int,const QString&,QString&);
    void setRepeatCount(int);
    void setUiSettings(const QJsonObject&);
    void stop();
    void applyStatus(host::signal::RunStatus);
    void receive(host::signal::BusFrameEvents);
    void observeFrames(host::FrameBatch);
    const FrameBatch &traceHistory() const {return m_traceHistory;}
    void clearTrace();
signals:
    void replayRequested(bool);void replayStopRequested();
    void framesObserved(host::FrameBatch);
    void traceCleared();
    void changed();
    void structureChanged();
    void configurationRestored();
    void frameChanged(QString);
    void startRequested(host::signal::TxPlan);
    void stopRequested(quint64);
    void payloadRequested(host::signal::PayloadUpdate);
    void scheduleRequested(quint64,QString);
    void notice(QString);
private:
    bool install(signal::Database,QString&error);
    void remember();void changedFrame(const QString&);
    bool commit(const QString&,QString&error);
    void restoreSnapshot(const signal::WorkingSet&);
    void applyConfiguration(signal::Database,const signal::WorkingSet&);
    signal::Bus m_bus;
    FrameBatch m_traceHistory;
    qint64 m_traceOrigin=-1;
    signal::Database m_database;
    signal::WorkingSet m_work,m_baseline;
    QVector<signal::WorkingSet> m_history;
    signal::RunStatus m_status;
    bool m_replayGroupRunning=false;
    bool m_connected=false,m_externalBusy=false,m_importing=false,m_hasBaseline=false;
    bool m_periodic=false,m_replaying=false;int m_bitrate=19200;quint64 m_connection=0,m_nextRun=0;
    QString m_message;
    TraceLog m_replayLog;QString m_replayPath;
    QFutureWatcher<TraceLog> m_replayLoad;
    QFutureWatcher<signal::ImportResult> m_import;
    QFutureWatcher<signal::ConfigurationResult> m_configurationLoad;
};
}
