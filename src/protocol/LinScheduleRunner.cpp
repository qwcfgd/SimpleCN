#include "LinScheduleRunner.h"
#include "model/SignalCodec.h"
#include <QElapsedTimer>
#include <algorithm>
namespace host::signal {
const Schedule*LinScheduleRunner::schedule(const QString&name)const{for(const auto&s:m_plan.schedules)if(s.name==name)return &s;return nullptr;}
QString LinScheduleRunner::validate(const Schedule&s)const{QVector<FrameDefinition> frames;for(const auto&i:m_plan.items){FrameDefinition f;f.key=i.key;f.id=i.id;f.length=i.payload.size();frames.append(f);}return SignalCodec::validateSchedule(s,frames,m_bitrate);}
QSet<QString> LinScheduleRunner::responseSet(const Schedule&s)const{QSet<QString> result;for(const auto&entry:s.entries)for(const auto&item:m_plan.items)if(item.key==entry.frame&&item.publisher==m_plan.node)result.insert(item.key);return result;}
bool LinScheduleRunner::fault(QString&error){const auto cause=error;QString cleanup;if(m_device&&!m_device->stop(cleanup)&&!cleanup.isEmpty())error=cause+"；清理失败："+cleanup;m_publish.clear();m_status.state=RunState::Faulted;m_status.detail=error;return false;}
bool LinScheduleRunner::activate(const QString&name,qint64 now,QString&error){
    const auto*s=schedule(name);if(!s){error="调度表不存在";return fault(error);}
    error=validate(*s);if(!error.isEmpty())return fault(error);
    m_publish=responseSet(*s);
    if(m_device&&(!m_device->install(m_plan.items,m_publish,error)||(m_plan.role==LinRole::Master&&!m_device->start(*s,m_plan.items,error))))return fault(error);
    m_status.current=name;m_status.pending.clear();m_status.state=RunState::Running;m_status.detail=m_device?"LIN 硬件运行":"SIM LIN 运行";m_slot=0;m_due=now;return true;
}
bool LinScheduleRunner::start(const TxPlan&plan,int bitrate,qint64 now,QString&error){
    if(active(m_status.state)){error="LIN 任务已在运行";return false;}m_plan=plan;m_bitrate=bitrate;m_status={};m_status.run=plan.run;
    m_publish.clear();m_slot=0;m_due=now;m_observed=false;m_gapPending=false;
    if(m_device&&(!m_device->configure(plan.role,bitrate,error)||!m_device->stop(error)))return fault(error);
    if(plan.role==LinRole::Monitor){if(m_device&&!m_device->install(plan.items,{},error))return fault(error);m_status.state=RunState::Running;m_status.detail=m_device?"监听中；响应集合为空":"SIM 监听；无自动接收数据";return true;}
    return activate(plan.schedule,now,error);
}
bool LinScheduleRunner::switchTo(const QString&name,qint64 now,QString&error){
    if(m_status.state!=RunState::Running||!schedule(name)){error="运行状态不允许切表";return false;}
    error=validate(*schedule(name));if(!error.isEmpty())return false;
    if(name==m_status.current)return true;
    m_status.pending=name;m_switchBegan=now;
    if(m_plan.role==LinRole::Master){
        if(m_device&&!m_device->requestBoundary(error))return fault(error);
        m_status.state=RunState::SwitchPending;m_status.detail="切换中：等待旧表整轮结束";return true;
    }
    if(m_plan.role==LinRole::Slave){
        QElapsedTimer window;window.start();
        m_status.state=RunState::Switching;m_publish.clear();
        if(m_device&&!m_device->stop(error))return fault(error);
        if(!activate(name,now,error))return false;
        if(notice)notice(QString("从节点切表：响应已暂停、完整替换集合并恢复；主机测得配置窗口 %1 ms（不推测外部主节点轮次）").arg(window.nsecsElapsed()/1000000.0,0,'f',3));return true;
    }return true;
}
bool LinScheduleRunner::tick(qint64 now,QString&error){
    if(!active(m_status.state))return true;
    if(m_device){if(m_status.state==RunState::SwitchPending){bool reached=false;if(!m_device->boundary(reached,error))return fault(error);
            if(reached){
                if(drainBeforeSwitch&&!drainBeforeSwitch())return true;
                if(m_status.state!=RunState::SwitchPending){error="切表前接收队列排空失败";return fault(error);}
                const auto name=m_status.pending;const auto*old=schedule(m_status.current);const auto*next=schedule(name);
                m_gapPending=m_observed&&old&&!old->entries.isEmpty()&&next&&!next->entries.isEmpty()&&frameKey(Bus::Lin,m_lastObserved.id)==old->entries.last().frame;
                m_gapAnchor=m_lastObserved;m_gapFrom=m_status.current;m_gapTo=name;
                if(m_gapPending)for(const auto&i:m_plan.items)if(i.key==next->entries.first().frame)m_gapFirstId=i.id;
                m_status.state=RunState::Switching;if(!activate(name,now,error))return false;
                if(notice)notice(QString("LIN 整轮边界已由硬件断点确认；开始新表首槽。请求至边界主机耗时 %1 ms；%2").arg((now-m_switchBegan)/1000.0).arg(m_gapPending?"等待新首帧以记录切表间隔":"未获取旧末槽事件，本次总线间隔不可测"));}}
        return true;
    }
    if(m_plan.role!=LinRole::Master||now<m_due)return true;
    const auto*s=schedule(m_status.current);if(!s||s->entries.isEmpty()){error="SIM 调度表为空";return fault(error);}
    // Simulation models slots and round boundaries explicitly; it never claims hardware precision.
    if(m_slot>=s->entries.size()){
        if(m_status.state==RunState::SwitchPending){const auto next=m_status.pending;if(!activate(next,now,error))return false;s=schedule(m_status.current);if(notice)notice(QString("SIM 整轮切表完成；主机估计间隙 %1 ms").arg((now-m_lastOldFrameUs)/1000.0));}
        else m_slot=0;
    }
    const auto&entry=s->entries[m_slot++];auto item=std::find_if(m_plan.items.begin(),m_plan.items.end(),[&](const auto&i){return i.key==entry.frame;});
    if(item==m_plan.items.end()){error="SIM 调度帧不存在";return fault(error);}
    BusFrameEvent e;e.bus=Bus::Lin;e.id=item->id;e.arrivalUs=now;e.source=EventSource::Simulated;
    if(m_publish.contains(item->key)){e.bytes=item->payload;e.detail="SIM 主节点发布";++m_status.sent[item->key];}
    else{e.valid=false;e.source=EventSource::NoResponse;e.detail="SIM 帧头；未模拟外部从节点响应";}
    if(event)event(e);m_lastOldFrameUs=now;m_due=now+qint64(SignalCodec::scheduleDelayMs(entry.delayMs))*1000;return true;
}
void LinScheduleRunner::observe(const BusFrameEvent&e){
    if(!m_device||!active(m_status.state)||e.bus!=Bus::Lin)return;
    if(!e.valid&&e.source!=EventSource::NoResponse)return;
    const auto key=frameKey(Bus::Lin,e.id);
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
    const auto*s=schedule(m_status.current);
    if(s)for(const auto&entry:s->entries)if(entry.frame==key){m_lastObserved=e;m_observed=true;break;}
}
bool LinScheduleRunner::update(const QString&key,const AppliedPayload&payload,QString&error){
    if(!active(m_status.state)||m_plan.role==LinRole::Monitor)return true;
    for(auto&item:m_plan.items)if(item.key==key){if(payload.bytes.size()!=item.payload.size()){error="热更新帧长度改变";return fault(error);}if(payload.revision<=item.revision)return true;item.payload=payload.bytes;item.revision=payload.revision;
        if(m_publish.contains(key)&&m_device&&!m_device->update(item,error))return fault(error);return true;}
    return true;
}
bool LinScheduleRunner::stop(QString&error){m_publish.clear();m_gapPending=false;m_status.pending.clear();if(m_device&&!m_device->stop(error)){m_status.state=RunState::Faulted;m_status.detail=error;return false;}m_status.state=RunState::Stopped;m_status.detail="已停止；响应已关闭";return true;}
QByteArray LinScheduleRunner::simulatedHeader(quint32 id)const{if(m_device||m_plan.role!=LinRole::Slave||m_status.state!=RunState::Running)return {};for(const auto&i:m_plan.items)if(i.id==id&&m_publish.contains(i.key))return i.payload;return {};}
}
