#pragma once
#include "domain/SignalTypes.h"
#include <functional>
namespace host::signal {
class LinDevice {
public:
    virtual ~LinDevice()=default;
    virtual bool configure(LinRole,int bitrate,QString&)=0;
    virtual bool stop(QString&)=0;
    virtual bool install(const QVector<TxItem>&,const QSet<QString>&publish,QString&)=0;
    virtual bool start(const Schedule&,const QVector<TxItem>&,QString&)=0;
    virtual bool requestBoundary(QString&)=0;
    virtual bool boundary(bool&,QString&)=0;
    virtual bool update(const TxItem&,QString&)=0;
};
class LinScheduleRunner {
public:
    explicit LinScheduleRunner(LinDevice *device=nullptr):m_device(device){}
    bool start(const TxPlan&,int bitrate,qint64 nowUs,QString&);
    bool switchTo(const QString&,qint64 nowUs,QString&);
    bool update(const QString&,const AppliedPayload&,QString&);
    bool tick(qint64 nowUs,QString&);
    void observe(const BusFrameEvent&);
    bool stop(QString&);
    // SIM only: an external master header. No autonomous slave/monitor traffic.
    QByteArray simulatedHeader(quint32 id)const;
    RunStatus status()const{return m_status;}
    const QSet<QString>&publishers()const{return m_publish;}
    std::function<void(const BusFrameEvent&)> event;
    std::function<void(const QString&)> notice;
    // Drain the old hardware queue after its breakpoint and before replacing
    // the table. False postpones switching until the next worker tick.
    std::function<bool()> drainBeforeSwitch;
private:
    const Schedule*schedule(const QString&)const;
    bool activate(const QString&,qint64 nowUs,QString&);
    QSet<QString> responseSet(const Schedule&)const;
    bool fault(QString&);
    QString validate(const Schedule&)const;
    LinDevice*m_device=nullptr;TxPlan m_plan;RunStatus m_status;QSet<QString> m_publish;
    int m_slot=0;int m_bitrate=19200;qint64 m_due=0,m_switchBegan=0,m_lastOldFrameUs=0;
    BusFrameEvent m_lastObserved,m_gapAnchor;
    bool m_observed=false,m_gapPending=false;
    quint32 m_gapFirstId=0;QString m_gapFrom,m_gapTo;
};
}
