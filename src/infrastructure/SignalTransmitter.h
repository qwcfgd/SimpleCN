#pragma once
#include "domain/SignalTypes.h"
#include "protocol/CanTxScheduler.h"
#include "protocol/LinScheduleRunner.h"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
class tstPeakCan;class tstPeakLin;
namespace host {
class SignalTransmitter : public QObject {
    Q_OBJECT
public:
    explicit SignalTransmitter(QObject*parent=nullptr);
    ~SignalTransmitter()override;
    bool running()const{return signal::active(m_status.state);}
    quint64 run()const{return m_status.run;}
    bool publishesLin(quint32 id)const;
    void observe(const signal::BusFrameEvents&);
    std::function<bool()> drainReceived;
    void start(const signal::TxPlan&,int bitrate,bool simulation,tstPeakCan*,tstPeakLin*);
    void stop(quint64 run=0);
    void update(const signal::PayloadUpdate&);
    void switchSchedule(quint64,const QString&);
    void linkFailed(const QString&);
signals:
    void statusChanged(host::signal::RunStatus);
    void events(host::signal::BusFrameEvents);
    void notice(QString);
private:
    void tick();void flush();bool send(const signal::TxItem&,QString&);void fail(const QString&);
    QTimer m_timer;QElapsedTimer m_clock;qint64 m_lastFlush=0;
    signal::TxPlan m_plan;signal::RunStatus m_status;
    bool m_simulation=false;tstPeakCan*m_can=nullptr;
    signal::CanTxScheduler m_scheduler;
    std::unique_ptr<signal::LinDevice> m_device;
    std::unique_ptr<signal::LinScheduleRunner> m_lin;
    signal::BusFrameEvents m_events;int m_once=0;
};
}
