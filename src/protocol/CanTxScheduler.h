#pragma once
#include "domain/SignalTypes.h"
#include <functional>
#include <queue>
namespace host::signal {
// One monotonic deadline heap per worker; no timer or queued catch-up per frame.
class CanTxScheduler {
public:
    using Sender=std::function<bool(const TxItem&,QString&)>;
    void start(const QVector<TxItem>&,qint64 nowUs);
    void stop();
    void update(const QString&,const AppliedPayload&);
    int pump(qint64 nowUs,const Sender&,int budget=256);
    bool running()const{return !m_due.empty();}
    const RunStatus &statistics()const{return m_stats;}
private:
    struct Due {qint64 time;int index;bool operator<(const Due&o)const{return time==o.time?index>o.index:time>o.time;}};
    QVector<TxItem> m_items;QMap<QString,int> m_indices;
    std::priority_queue<Due> m_due;RunStatus m_stats;
};
}
