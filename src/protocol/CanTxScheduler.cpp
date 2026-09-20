#include "CanTxScheduler.h"
namespace host::signal {
void CanTxScheduler::start(const QVector<TxItem>&items,qint64 now){stop();m_stats={};m_items=items;m_started=true;for(int i=0;i<items.size();++i){m_indices[items[i].key]=i;if(items[i].enabled){m_due.push({now,i});m_round.insert(items[i].key);}}}
void CanTxScheduler::stop(){m_started=false;m_atBoundary=true;m_pending.clear();m_round.clear();m_due={};m_items.clear();m_indices.clear();}
void CanTxScheduler::setEnabled(const QString&key,bool enabled){if(m_indices.contains(key))m_pending[key]=enabled;}
void CanTxScheduler::applyEnabled(qint64 now){
    if(!m_pending.isEmpty()){
        QSet<int> queued;std::priority_queue<Due> next;
        while(!m_due.empty()){auto due=m_due.top();m_due.pop();const auto&i=m_items[due.index];if(m_pending.value(i.key,i.enabled)){next.push(due);queued.insert(due.index);}}
        for(auto it=m_pending.cbegin();it!=m_pending.cend();++it){auto index=m_indices.value(it.key());auto&i=m_items[index];i.enabled=it.value();if(i.enabled&&!queued.contains(index)&&!m_stats.failures.contains(i.key))next.push({now+qint64(i.periodMs)*1000,index});}
        m_due=std::move(next);m_pending.clear();
    }
    m_atBoundary=true;m_round.clear();for(const auto&i:m_items)if(i.enabled&&!m_stats.failures.contains(i.key))m_round.insert(i.key);
}
void CanTxScheduler::update(const QString&key,const AppliedPayload&payload){const auto it=m_indices.find(key);if(it==m_indices.end())return;auto &item=m_items[it.value()];if(payload.revision<=item.revision||payload.bytes.size()!=item.payload.size())return;item.payload=payload.bytes;item.revision=payload.revision;}
int CanTxScheduler::pump(qint64 now,const Sender&send,int budget){
    if((m_atBoundary||m_round.isEmpty())&&!m_pending.isEmpty())applyEnabled(now);
    int submitted=0;while(!m_due.empty()&&m_due.top().time<=now&&submitted<budget){m_atBoundary=false;auto due=m_due.top();m_due.pop();auto&item=m_items[due.index];
        const qint64 period=qint64(item.periodMs)*1000;
        if(period<=0){m_stats.failures[item.key]="周期至少为 1 ms";m_round.remove(item.key);if(m_round.isEmpty())applyEnabled(now);continue;}
        const auto skipped=(now-due.time)/period;m_stats.missed[item.key]+=quint64(skipped);
        QString error;if(send(item,error)){++m_stats.sent[item.key];due.time+=(skipped+1)*period;m_due.push(due);}else m_stats.failures[item.key]=error;
        ++submitted;m_round.remove(item.key);if(m_round.isEmpty())applyEnabled(now);
    }return submitted;
}
}
