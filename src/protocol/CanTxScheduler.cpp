#include "CanTxScheduler.h"
namespace host::signal {
void CanTxScheduler::start(const QVector<TxItem>&items,qint64 now){stop();m_stats={};m_items=items;for(int i=0;i<items.size();++i){m_indices[items[i].key]=i;m_due.push({now,i});}}
void CanTxScheduler::stop(){m_due={};m_items.clear();m_indices.clear();}
void CanTxScheduler::update(const QString&key,const AppliedPayload&payload){const auto it=m_indices.find(key);if(it==m_indices.end())return;auto &item=m_items[it.value()];if(payload.revision<=item.revision||payload.bytes.size()!=item.payload.size())return;item.payload=payload.bytes;item.revision=payload.revision;}
int CanTxScheduler::pump(qint64 now,const Sender&send,int budget){
    int submitted=0;while(!m_due.empty()&&m_due.top().time<=now&&submitted<budget){auto due=m_due.top();m_due.pop();auto&item=m_items[due.index];
        const qint64 period=qint64(item.periodMs)*1000;
        if(period<=0){m_stats.failures[item.key]="周期至少为 1 ms";continue;}
        const auto skipped=(now-due.time)/period;m_stats.missed[item.key]+=quint64(skipped);
        QString error;if(send(item,error)){++m_stats.sent[item.key];due.time+=(skipped+1)*period;m_due.push(due);}else m_stats.failures[item.key]=error;
        ++submitted;
    }return submitted;
}
}
