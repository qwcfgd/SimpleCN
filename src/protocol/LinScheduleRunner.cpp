#include "LinScheduleRunner.h"
#include "model/SignalCodec.h"
#include <QElapsedTimer>
#include <algorithm>
namespace host::signal {
const Schedule*LinScheduleRunner::schedule(const QString&name)const{for(const auto&s:m_plan.schedules)if(s.name==name)return &s;return nullptr;}
Schedule LinScheduleRunner::effective(const QString&name)const{
    const auto*source=schedule(name);if(!source)return {};auto result=*source;
    for(int n=result.entries.size()-1;n>=0;--n)for(const auto&i:m_plan.items)if(i.key==result.entries[n].frame&&!i.enabled){result.entries.removeAt(n);break;}
    if(result.entries.size()!=source->entries.size())result.issue.clear();return result;
}
void LinScheduleRunner::applyEnabled(){for(auto&i:m_plan.items)if(m_pendingEnabled.contains(i.key))i.enabled=m_pendingEnabled.value(i.key);m_pendingEnabled.clear();m_enableBoundary=false;m_slaveBoundary=false;}
QString LinScheduleRunner::validate(const Schedule&s)const{if(s.entries.isEmpty())return {};QVector<FrameDefinition> frames;for(const auto&i:m_plan.items){FrameDefinition f;f.key=i.key;f.id=i.id;f.length=i.payload.size();frames.append(f);}return SignalCodec::validateSchedule(s,frames,m_bitrate);}
QSet<QString> LinScheduleRunner::responseSet(const Schedule&s)const{QSet<QString> result;if(m_plan.role==LinRole::Monitor)return result;for(const auto&entry:s.entries)for(const auto&item:m_plan.items)if(item.enabled&&item.key==entry.frame&&item.publisher==m_plan.node)result.insert(item.key);return result;}
bool LinScheduleRunner::fault(QString&error){const auto cause=error;QString cleanup;if(m_device&&!m_device->stop(cleanup)&&!cleanup.isEmpty())error=cause+"；清理失败："+cleanup;m_publish.clear();m_status.state=RunState::Faulted;m_status.detail=error;return false;}
bool LinScheduleRunner::activate(const QString&name,qint64 now,QString&error){
    const auto*s=schedule(name);if(!s){error="调度表不存在";return fault(error);}
    m_effective=effective(name);error=m_plan.role==LinRole::Monitor?QString():validate(m_effective);if(!error.isEmpty())return fault(error);
    m_publish=responseSet(m_effective);
    QVector<TxItem> selected;QSet<quint32> ids;for(const auto &slot:m_effective.entries)for(const auto &item:m_plan.items)if(item.key==slot.frame&&!ids.contains(item.id)){selected.append(item);ids.insert(item.id);}
    if(m_device&&(!m_device->install(selected,m_publish,error)||(m_plan.role==LinRole::Master&&!m_effective.entries.isEmpty()&&!m_device->start(m_effective,selected,error))))return fault(error);
    m_status.current=name;m_status.pending.clear();m_status.state=RunState::Running;m_status.detail=m_device?"LIN 硬件运行":"SIM LIN 运行";m_slot=0;m_observedSlot=0;m_due=now;m_frameEnd=now;
    if(m_device&&m_plan.role==LinRole::Master&&m_roundsRemaining>0&&!m_effective.entries.isEmpty()&&!m_device->requestBoundary(error,true))return fault(error);return true;
}
bool LinScheduleRunner::start(const TxPlan&plan,int bitrate,qint64 now,QString&error){
    if(active(m_status.state)){error="LIN 任务已在运行";return false;}m_plan=plan;m_roundsRemaining=plan.periodic?0:qBound(1,plan.repeatCount,1000000);m_bitrate=bitrate;m_status={};m_status.run=plan.run;
    m_publish.clear();m_slot=0;m_due=now;m_frameEnd=now;m_pendingEnabled.clear();m_enableBoundary=false;m_slaveBoundary=false;m_observed=false;m_gapPending=false;
    if(m_device&&(!m_device->configure(plan.role,bitrate,error)||!m_device->stop(error)))return fault(error);
    if(plan.role==LinRole::Monitor){m_effective=effective(plan.schedule);m_status.current=plan.schedule;QVector<TxItem> monitorItems;QSet<quint32> monitorIds;for(const auto &i:plan.items)if(!monitorIds.contains(i.id)){monitorItems.append(i);monitorIds.insert(i.id);}if(m_device&&!m_device->install(monitorItems,{},error))return fault(error);m_status.state=RunState::Running;return true;}
    return activate(plan.schedule,now,error);
}
bool LinScheduleRunner::switchTo(const QString&name,qint64 now,QString&error){
    if(m_status.state!=RunState::Running||!schedule(name)){error="运行状态不允许切表";return false;}
    error=m_plan.role==LinRole::Monitor?QString():validate(effective(name));if(!error.isEmpty())return false;if(name==m_status.current)return true;
    m_status.pending=name;m_switchBegan=now;m_status.state=RunState::SwitchPending;m_status.detail="等待当前帧完成";
    // Suspend only the scheduler, retaining response data until the in-flight frame is complete.
    if(m_device&&m_plan.role==LinRole::Master&&!m_effective.entries.isEmpty()&&!m_device->requestBoundary(error,false))return fault(error);
    if(m_device)m_frameEnd=now+(174000000LL+m_bitrate-1)/m_bitrate; // conservative maximum LIN frame, including tolerance
    return true;
}
bool LinScheduleRunner::setEnabled(const QString&key,bool enabled,qint64,QString&error){
    if(!active(m_status.state))return true;
    auto item=std::find_if(m_plan.items.begin(),m_plan.items.end(),[&](const auto&i){return i.key==key;});
    if(item==m_plan.items.end()){error="使能更新引用未知帧";return false;}
    m_pendingEnabled[key]=enabled;
    if(m_device&&m_plan.role==LinRole::Master&&!m_effective.entries.isEmpty()&&!m_enableBoundary&&m_status.state==RunState::Running){if(!m_device->requestBoundary(error,true))return fault(error);m_enableBoundary=true;}
    return true;
}
bool LinScheduleRunner::tick(qint64 now,QString&error){
    if(!active(m_status.state))return true;
    if(m_roundsRemaining>0&&m_plan.role==LinRole::Master&&m_status.state==RunState::Running){
        bool ended=!m_device&&m_slot>=m_effective.entries.size()&&now>=m_due;
        if(m_device&&!m_device->boundary(ended,error,true))return fault(error);
        if(ended){if(--m_roundsRemaining==0){if(!stop(error))return false;m_status.detail="多次调度已完成";return true;}
            if(m_device&&!activate(m_status.current,now,error))return false;else if(!m_device)m_slot=0;}
    }
    const bool switching=m_status.state==RunState::SwitchPending;
    bool boundary=false;
    if(switching){
        boundary=now>=m_frameEnd;
        if(boundary&&m_device&&m_plan.role==LinRole::Master&&!m_effective.entries.isEmpty()){if(!m_device->boundary(boundary,error,false))return fault(error);}
    }else if(!m_pendingEnabled.isEmpty()){
        boundary=m_effective.entries.isEmpty();
        if(m_device&&m_plan.role==LinRole::Master&&m_enableBoundary){if(!m_device->boundary(boundary,error,true))return fault(error);}
        else if(m_plan.role==LinRole::Slave)boundary=boundary||m_slaveBoundary;
        else if(!m_device)boundary=boundary||(m_slot>=m_effective.entries.size()&&now>=m_due);
    }
    if(boundary){
        if(m_device&&drainBeforeSwitch&&!drainBeforeSwitch())return true;
        if(!active(m_status.state)){error="边界前接收队列排空失败";return fault(error);}
        const auto next=switching?m_status.pending:m_status.current;
        m_gapPending=switching&&m_observed;m_gapAnchor=m_lastObserved;m_gapFrom=m_status.current;m_gapTo=next;
        applyEnabled();const auto upcoming=effective(next);if(upcoming.entries.isEmpty())m_gapPending=false;
        if(m_gapPending)for(const auto&i:m_plan.items)if(i.key==upcoming.entries.first().frame)m_gapFirstId=i.id;
        if(m_device&&m_plan.role!=LinRole::Master&&!m_device->stop(error))return fault(error);
        if(!activate(next,now,error))return false;
    }
    if(m_device||m_plan.role!=LinRole::Master||now<m_due||m_effective.entries.isEmpty())return true;
    if(m_slot>=m_effective.entries.size())m_slot=0;
    const auto&entry=m_effective.entries[m_slot++];auto item=std::find_if(m_plan.items.begin(),m_plan.items.end(),[&](const auto&i){return i.key==entry.frame;});
    if(item==m_plan.items.end()){error="SIM 调度帧不存在";return fault(error);}
    BusFrameEvent e;e.bus=Bus::Lin;e.id=item->id;e.arrivalUs=now;e.source=EventSource::Simulated;e.classicChecksum=item->classicChecksum;
    if(m_publish.contains(item->key)){e.bytes=item->payload;e.detail="SIM 主节点发布";++m_status.sent[item->key];}
    else{e.valid=false;e.source=EventSource::NoResponse;e.detail="SIM 帧头；未模拟外部从节点响应";}
    if(event)event(e);m_lastOldFrameUs=now;m_due=now+qint64(SignalCodec::scheduleDelayMs(entry.delayMs))*1000;
    m_frameEnd=now+((34+10*(item->payload.size()+1))*1400000LL+m_bitrate-1)/m_bitrate;return true;
}
void LinScheduleRunner::observe(const BusFrameEvent&e){
    if(!active(m_status.state)||e.bus!=Bus::Lin)return;
    if(!e.valid&&e.source!=EventSource::NoResponse)return;
    auto key=frameKey(Bus::Lin,e.id);for(const auto &slot:m_effective.entries)for(const auto &item:m_plan.items)if(item.key==slot.frame&&item.id==e.id){key=item.key;break;}
    if(e.valid&&e.source==EventSource::HardwareEcho&&m_publish.contains(key))++m_status.sent[key];
    if(m_gapPending&&e.id==m_gapFirstId){
        QString measured;
        if(e.hardwareUs&&m_gapAnchor.hardwareUs&&e.hardwareUs>=m_gapAnchor.hardwareUs)
            measured=QString("硬件时间戳间隔 %1 ms").arg((e.hardwareUs-m_gapAnchor.hardwareUs)/1000.0,0,'f',3);
        else if(e.arrivalUs>=m_gapAnchor.arrivalUs)
            measured=QString("仅主机到达时间估计 %1 ms（无可用硬件时间戳）").arg((e.arrivalUs-m_gapAnchor.arrivalUs)/1000.0,0,'f',3);
        else measured="时间戳无效，间隔不可测";
        if(notice)notice(QString("LIN 切表 %1 → %2：旧末帧至新首帧%3；该值为事件间隔，非线路空闲时间").arg(m_gapFrom,m_gapTo,measured));
        m_gapPending=false;
    }
    const auto*s=&m_effective;
    if(m_plan.role==LinRole::Slave&&!s->entries.isEmpty()&&s->entries[m_observedSlot].frame==key){
        if(++m_observedSlot==s->entries.size()){m_observedSlot=0;if(!m_pendingEnabled.isEmpty())m_slaveBoundary=true;}
    }
    if(s)for(const auto&entry:s->entries)if(entry.frame==key){m_lastObserved=e;m_observed=true;break;}
}
bool LinScheduleRunner::update(const QString&key,const AppliedPayload&payload,QString&error){
    if(!active(m_status.state)||m_plan.role==LinRole::Monitor)return true;
    for(auto&item:m_plan.items)if(item.key==key){if(payload.bytes.size()!=item.payload.size()){error="热更新帧长度改变";return fault(error);}if(payload.revision<=item.revision)return true;item.payload=payload.bytes;item.revision=payload.revision;
        if(m_publish.contains(key)&&m_device&&!m_device->update(item,error))return fault(error);return true;}
    return true;
}
bool LinScheduleRunner::stop(QString&error){m_publish.clear();m_pendingEnabled.clear();m_enableBoundary=false;m_slaveBoundary=false;m_gapPending=false;m_status.pending.clear();if(m_device&&!m_device->stop(error)){m_status.state=RunState::Faulted;m_status.detail=error;return false;}m_status.state=RunState::Stopped;m_status.detail="已停止；响应已关闭";return true;}
QByteArray LinScheduleRunner::simulatedHeader(quint32 id)const{if(m_device||m_plan.role!=LinRole::Slave||m_status.state!=RunState::Running)return {};for(const auto&i:m_plan.items)if(i.id==id&&m_publish.contains(i.key))return i.payload;return {};}
}
