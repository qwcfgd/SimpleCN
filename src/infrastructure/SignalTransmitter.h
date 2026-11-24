#pragma once
#include "domain/SignalTypes.h"
#include "protocol/CanTxScheduler.h"
#include "protocol/LinScheduleRunner.h"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>

namespace host {
class CanHardware;class LinHardware;
class SignalTransmitter : public QObject {
    Q_OBJECT
public:
    explicit SignalTransmitter(QObject*parent=nullptr);
    ~SignalTransmitter()override;
    bool running()const{return signal::active(m_status.state)||m_status.cleanupPending;}
    quint64 run()const{return m_status.run;}
    bool publishesLin(quint32 id)const;
    void observe(const signal::BusFrameEvents&);
    std::function<bool()> drainReceived;
    std::function<bool(const signal::TxItem &,QString &)> guardedCanSend;
    void start(const signal::TxPlan&,int bitrate,bool simulation,CanHardware*,LinHardware*);
    void stop(quint64 run=0);
    void hardwareClosed();
    void update(const signal::PayloadUpdate&);
    void switchSchedule(quint64,const QString&);
    void linkFailed(const QString&);
    bool hasUnconfirmedCanFrame()const{return m_canOutstanding;}
    void setCanPaused(bool paused,bool controllerReset=false);
    void setCanEchoTimeout(int ms){m_canEchoTimeoutMs=qBound(1,ms,60000);}
signals:
    void canRecoveryNeeded(int state,quint64 connection);
    void statusChanged(host::signal::RunStatus);
    void events(host::signal::BusFrameEvents);
    void notice(QString);
private:
    void replayTick(qint64);bool replaySend(signal::ReplayRecord,qint64,QString&);
    bool cleanup(QString&);
    void tick();void flush();bool send(const signal::TxItem&,QString&);void fail(const QString&);
    QTimer m_timer;QElapsedTimer m_clock;qint64 m_lastFlush=0;
    signal::TxPlan m_plan;signal::RunStatus m_status;
    bool m_simulation=false;CanHardware*m_can=nullptr;
    bool m_canPaused=false,m_canOutstanding=false;
    signal::BusFrameEvent m_canPending;
    qint64 m_canPendingAtUs=0,m_replayResumeUs=0;
    int m_canEchoTimeoutMs=1000;
    bool canWriteFailed();
    void rememberCanWrite(quint32,bool,bool,const QByteArray&,int length=-1);
    signal::CanTxScheduler m_scheduler;
    std::unique_ptr<signal::LinDevice> m_device;
    std::unique_ptr<signal::LinScheduleRunner> m_lin;
    signal::BusFrameEvents m_events;int m_once=0;
    QMap<quint32,signal::ReplayRecord>m_replayResponses;
    LinHardware*m_replayLin=nullptr;int m_replayIndex=0,m_replayRound=0;QVector<qint64> m_extraDue;QSet<QString>m_loggedKeys;QHash<QString,int>m_itemIndex;
    QVector<int> m_remaining;QVector<qint64> m_due;
};
}
