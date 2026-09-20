#pragma once
#include "domain/SignalTypes.h"
#include <QObject>
#include <QFutureWatcher>
#include <QJsonObject>
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
    bool running()const{return signal::active(m_status.state);}
    bool canStructure()const{return !running()&&!m_externalBusy&&!m_importing;}
    bool canData()const{return !m_externalBusy&&!m_importing&&!(running()&&m_work.role==signal::LinRole::Monitor&&m_bus==signal::Bus::Lin);}
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
    bool setCanOptions(const QString&,bool enabled,int period,QString&error);
    bool putCustom(const QString &oldKey,const QString &name,quint32 id,int length,int cycle,QString&error);
    bool removeCustom(const QString&,QString&error);
    bool setRole(signal::LinRole,const QString&,QString&error);
    bool selectSchedule(const QString&,QString&error);
    bool replaceSchedules(const QVector<signal::Schedule>&,const QString&,QString&error);
    void back();void restore();
    bool save(const QString&,QString&error)const;
    bool read(const QString&,QString&error);
    void readAsync(const QString&);
    void readConfigurationAsync(const QJsonObject&,const QString &baseDirectory);
    QJsonObject configuration(const QString &relativeTo=QString())const;
    bool readConfiguration(const QJsonObject&,const QString &baseDirectory,QString&error);
    bool start(bool periodic,QString&error);
    void stop();
    void applyStatus(host::signal::RunStatus);
    void receive(host::signal::BusFrameEvents);
signals:
    void changed();
    void structureChanged();
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
    signal::Database m_database;
    signal::WorkingSet m_work,m_baseline;
    QVector<signal::WorkingSet> m_history;
    signal::RunStatus m_status;
    bool m_connected=false,m_externalBusy=false,m_importing=false,m_hasBaseline=false;
    int m_bitrate=19200;quint64 m_connection=0,m_nextRun=0;
    QString m_message;
    QFutureWatcher<signal::ImportResult> m_import;
    QFutureWatcher<signal::ConfigurationResult> m_configurationLoad;
};
}
