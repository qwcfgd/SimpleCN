#include "ReplayViewModel.h"
#include "ChannelViewModel.h"
#include "domain/TraceClock.h"
#include <QTimer>

namespace host {
ReplayViewModel::~ReplayViewModel(){stop();}
void ReplayViewModel::start(SignalTransmitViewModel *owner,bool periodic,const QVector<ChannelViewModel*> &channels){
    using namespace signal;
    auto report=[&](const QString &error){for(auto *vm:channels)if(vm->signalTransmission()==owner)vm->log(error);emit notice(error);emit owner->notice(error);};
    if(m_replayOwner){report("已有日志回放正在运行，请先停止");return;}
    if(owner->replayLog().frames.isEmpty()){report("日志尚未成功载入，请重新导入");return;}
    const auto mapping=owner->working().replaySettings.value("mapping").toObject();QMap<QString,ChannelViewModel*> targets;
    for(auto *vm:channels)targets[vm->settings().softwareId]=vm;
    QMap<ChannelViewModel*,TxPlan> plans;QMap<ChannelViewModel*,QSet<QString>> exclude;QMap<ChannelViewModel*,QHash<QString,FrameDefinition>> definitions;
    for(auto it=mapping.begin();it!=mapping.end();++it){const auto target=it.value().toString();if(target.isEmpty())continue;
        if(!owner->replayLog().channels.contains(it.key()))continue;
        if(!targets.contains(target)){report("映射目标不存在："+target);return;}auto *vm=targets[target];const bool can=vm->settings().bus==communication::Bus::Can;
        if(it.key().startsWith("CAN:")!=can){report("回放映射总线类型不匹配");return;}
        if(plans.contains(vm))continue;
        if(!vm->connected()||vm->busy()){report("回放目标未连接或正在执行任务："+target);return;}
        auto *svm=vm->signalTransmission();TxPlan plan;plan.bus=can?Bus::Can:Bus::Lin;plan.role=svm->working().role;plan.node=svm->working().node;plan.replay=true;plan.periodic=periodic;plan.repeatCount=owner->working().repeatCount;plan.replayDurationUs=owner->replayLog().durationUs;plans[vm]=plan;
        const auto selectedNode=can?svm->working().canNode:svm->working().node;
        for(const auto &f:svm->database()->frames){definitions[vm][f.key]=f;if(!vm->settings().simulation&&!selectedNode.isEmpty()&&(f.publisher==selectedNode||f.transmitters.contains(selectedNode)))exclude[vm].insert(f.key);}
    }
    if(plans.isEmpty()){report("请先配置至少一个日志通道映射");return;}
    for(const auto &r:owner->replayLog().frames){const auto target=mapping.value(r.channel).toString();auto *vm=targets.value(target,nullptr);if(!vm||!plans.contains(vm))continue;auto &plan=plans[vm];const auto key=frameKey(plan.bus,r.id,r.extended);
        if(exclude[vm].contains(key))continue;
        if(!vm->settings().simulation&&r.fd){report("当前经典 CAN 硬件驱动不支持 CAN FD 回放："+target);return;}
        if(plan.bus==Bus::Lin&&(r.length<1||(r.id>=60&&r.length!=8))){report("日志包含无效 LIN 帧长度");return;}
        ReplayRecord record;record.id=r.id;record.extended=r.extended;record.payload=r.payload;record.timeUs=r.timeUs;record.length=r.length;record.rx=r.direction.compare("Rx",Qt::CaseInsensitive)==0;record.fd=r.fd;record.rtr=r.rtr;record.brs=r.brs;record.esi=r.esi;record.classicChecksum=r.classicChecksum;
        const auto definition=definitions[vm].constFind(key);record.publish=definition==definitions[vm].constEnd()||definition->publisher==plan.node;
        plan.replayFrames.append(record);
    }
    bool any=false;for(auto it=plans.begin();it!=plans.end();++it){auto *vm=it.key();auto *svm=vm->signalTransmission();auto &plan=it.value();QSet<QString> queued;
        if(plan.bus==Bus::Can)for(const auto &f:svm->queuedDefinitions())queued.insert(f.key);
        else for(const auto &table:svm->working().schedules)if(table.name==svm->working().schedule)for(const auto &slot:table.entries)queued.insert(slot.frame);
        for(const auto &key:queued){const auto *f=svm->frame(key);if(!f||!f->issue.isEmpty()||exclude[vm].contains(frameKey(plan.bus,f->id,f->extended)))continue;const auto &draft=svm->working().frames[key];
            int period=draft.cycleMs;if(plan.bus==Bus::Lin)for(const auto &s:svm->working().schedules)if(s.name==svm->working().schedule)for(const auto &slot:s.entries)if(slot.frame==key){period=qMax(1,qRound(slot.delayMs.toDouble()));break;}
            // Keep disabled rows too: a later enable can override a matching log ID.
            plan.items.append({frameKey(plan.bus,f->id,f->extended),f->id,f->extended,draft.applied.bytes,period,draft.applied.revision,f->custom?plan.node:f->publisher,f->classicChecksum,draft.sendEnabled});}
        any|=!plan.replayFrames.isEmpty();for(const auto&i:plan.items)any|=i.enabled;
    }
    if(!any){report("筛选后没有可回放或追加发送的报文");return;}
    const auto start=captureTimeUs()+100000;m_replayOwner=owner;
    for(auto it=plans.begin();it!=plans.end();++it){it.value().replayStartUs=start;QString error;auto *target=it.key()->signalTransmission();
        if(!target->startReplayPlan(it.value(),error)){report(error);stop();return;}m_replayParticipants.append(target);}
    owner->setReplayGroupRunning(true);
    if(!m_replayTimer){m_replayTimer=new QTimer(this);m_replayTimer->setInterval(20);connect(m_replayTimer,&QTimer::timeout,this,[this]{if(!m_replayOwner){stop();return;}bool active=false;for(auto target:m_replayParticipants){if(!target||target->status().state==signal::RunState::Faulted){stop();return;}active|=signal::active(target->status().state);}if(!active)stop();});}
    m_replayTimer->start();
}
void ReplayViewModel::stop(){
    if(m_stoppingReplay)return;m_stoppingReplay=true;
    auto owner=m_replayOwner;auto participants=m_replayParticipants;m_replayOwner.clear();m_replayParticipants.clear();if(m_replayTimer)m_replayTimer->stop();if(owner)owner->setReplayGroupRunning(false);
    for(auto target:participants)if(target&&(signal::active(target->status().state)||target->status().cleanupPending))target->stop();m_stoppingReplay=false;
}
}

namespace host { bool ReplayViewModel::contains(SignalTransmitViewModel *vm)const{return m_replayOwner==vm||m_replayParticipants.contains(vm);} }
