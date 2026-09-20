#include "domain/TraceClock.h"
#include "SignalTransmitter.h"
#include "model/SignalCodec.h"
#include "BusHardware.h"

#include <algorithm>
#include <cstring>
namespace host {
using namespace signal;
namespace {
class HardwareLinSignalDevice final:public LinDevice {
    LinHardware &lin;
    bool result(bool ok,QString&error){if(!ok)error=lin.lastErrorText();return ok;}
public:
    explicit HardwareLinSignalDevice(LinHardware &driver):lin(driver){}
    bool configure(LinRole role,int,QString&e)override{return result(role==LinRole::Monitor?lin.signalConfigureMonitor():lin.signalConfigureMode(role==LinRole::Master),e);}
    bool stop(QString&e)override{return result(lin.signalStop(),e);}
    bool install(const QVector<TxItem>&items,const QSet<QString>&publish,QString&e)override{
        QVector<TLINFrameEntry> entries;for(const auto&i:items){TLINFrameEntry entry={};entry.FrameId=BYTE(i.id);entry.Length=BYTE(i.payload.size());
            entry.Direction=publish.contains(i.key)?dirPublisher:dirSubscriber;
            entry.ChecksumType=i.classicChecksum?cstClassic:cstEnhanced;
            entry.Flags=publish.contains(i.key)?FRAME_FLAG_RESPONSE_ENABLE:0;
            std::memcpy(entry.InitialData,i.payload.constData(),size_t(i.payload.size()));entries.append(entry);}
        return result(lin.signalInstallFrames(entries),e);
    }
    bool start(const Schedule&s,const QVector<TxItem>&items,QString&e)override{
        QVector<TLINScheduleSlot> entries;for(const auto&slot:s.entries){auto item=std::find_if(items.begin(),items.end(),[&](const auto&i){return i.key==slot.frame;});if(item==items.end()){e="调度引用未知帧";return false;}
            // Diagnostic slot types suppress unchanged requests (PLIN API 3.7).
            // Use unconditional slots for all IDs to honor every LDF slot on every round.
            TLINScheduleSlot entry={};entry.Type=sltUnconditional;entry.FrameId[0]=BYTE(item->id);entry.Delay=WORD(SignalCodec::scheduleDelayMs(slot.delayMs));entries.append(entry);}
        return result(lin.signalStartSchedule(entries),e);
    }
    bool requestBoundary(QString&e,bool round=true)override{return result(round?lin.signalRequestRoundBoundary():lin.signalRequestFrameBoundary(),e);}
    bool boundary(bool&reached,QString&e,bool round=true)override{return result(round?lin.signalRoundBoundaryReached(reached):lin.signalFrameBoundaryReached(reached),e);}
    bool update(const TxItem&i,QString&e)override{return result(lin.signalUpdateFrame(BYTE(i.id),i.payload),e);}
};
}
SignalTransmitter::SignalTransmitter(QObject*parent):QObject(parent){m_clock.start();m_timer.setTimerType(Qt::PreciseTimer);m_timer.setInterval(1);connect(&m_timer,&QTimer::timeout,this,&SignalTransmitter::tick);}
SignalTransmitter::~SignalTransmitter(){stop();}
void SignalTransmitter::start(const TxPlan&plan,int bitrate,bool simulation,CanHardware*can,LinHardware*lin){
    if(running())return;m_plan=plan;m_status={};m_status.run=plan.run;m_status.state=RunState::Starting;m_can=can;m_simulation=simulation;m_once=0;m_events.clear();m_remaining.fill(qBound(1,plan.repeatCount,1000000),plan.items.size());m_due.fill(captureTimeUs(),plan.items.size());m_lin.reset();m_device.reset();
    m_replayResponses.clear();m_replayLin=lin;m_replayIndex=0;m_replayRound=0;m_loggedKeys.clear();m_itemIndex.clear();m_extraDue.fill(0,plan.items.size());
    for(int n=0;n<plan.items.size();++n)m_itemIndex[plan.items[n].key]=n;
    if(plan.replay){
        if(plan.replayDurationUs<=0||plan.repeatCount<1){fail("回放时间或次数无效");return;}
        for(const auto &r:plan.replayFrames){if(r.timeUs<0||r.timeUs>=plan.replayDurationUs||r.id>(plan.bus==Bus::Can?0x1fffffff:61)||r.length<0||r.length>(r.fd?64:8)||(!r.rtr&&r.payload.size()!=r.length)||(!simulation&&r.fd)){fail("回放记录无效，或当前驱动不支持 CAN FD 硬件发送");return;}m_loggedKeys.insert(frameKey(plan.bus,r.id,r.extended));}
        if(!simulation&&plan.bus==Bus::Lin&&(!lin||!lin->signalStop()||!(plan.role==LinRole::Monitor?lin->signalConfigureMonitor():lin->signalConfigureMode(plan.role==LinRole::Master)))){fail(lin?lin->lastErrorText():"LIN 驱动不可用");return;}
        m_status.state=RunState::Running;m_status.detail=simulation?"SIM 快速回放":"原始 1 倍速回放";m_timer.setInterval(simulation?0:1);emit statusChanged(m_status);m_timer.start();return;
    }
    m_timer.setInterval(1);
    QSet<QString> keys;QSet<quint32> linIds;QVector<FrameDefinition> definitions;
    for(const auto&i:plan.items){if(keys.contains(i.key)||i.payload.size()>8||i.id>(plan.bus==Bus::Can?0x1fffffff:61)||
            (plan.bus==Bus::Can&&!i.extended&&i.id>0x7ff)||(plan.bus==Bus::Lin&&(i.payload.isEmpty()||(i.id>=60&&(i.payload.size()!=8||!i.classicChecksum))))||
            (plan.bus==Bus::Can&&plan.periodic&&i.enabled&&i.periodMs<1)){fail("发送计划未通过 worker 校验");return;}
        keys.insert(i.key);linIds.insert(i.id);FrameDefinition f;f.key=i.key;f.id=i.id;f.length=i.payload.size();definitions.append(f);}
    if(plan.bus==Bus::Can&&plan.items.isEmpty()){fail("发送计划为空");return;}
    if(plan.bus==Bus::Lin){
        if(!simulation&&!lin){fail("LIN 驱动不可用");return;}
        if(!simulation)m_device=std::make_unique<HardwareLinSignalDevice>(*lin);m_lin=std::make_unique<LinScheduleRunner>(m_device.get());
        m_lin->event=[this](const BusFrameEvent&e){m_events.append(e);};m_lin->notice=[this](const QString&s){emit notice(s);};m_lin->drainBeforeSwitch=drainReceived;QString error;
        if(!m_lin->start(plan,bitrate,captureTimeUs(),error)){fail(error);return;}m_status=m_lin->status();
    }else {if(!simulation&&!can){fail("CAN 驱动不可用");return;}if(plan.periodic)m_scheduler.start(plan.items,captureTimeUs());m_status.state=RunState::Running;m_status.detail=simulation?"SIM CAN 运行":"CAN 请求提交中";}
    emit statusChanged(m_status);m_timer.start();tick();
}
bool SignalTransmitter::send(const TxItem&i,QString&error){
    if(!m_simulation){TPCANMsg message={};message.ID=i.id;message.MSGTYPE=i.extended?PCAN_MESSAGE_EXTENDED:PCAN_MESSAGE_STANDARD;message.LEN=BYTE(i.payload.size());std::memcpy(message.DATA,i.payload.constData(),size_t(i.payload.size()));
        if(!m_can||!m_can->sendRaw(message)){error=m_can?m_can->lastErrorText():"CAN 驱动已释放";return false;}}
    BusFrameEvent event;event.bus=Bus::Can;event.id=i.id;event.extended=i.extended;event.bytes=i.payload;event.arrivalUs=captureTimeUs();
    event.source=m_simulation?EventSource::Simulated:EventSource::RequestAccepted;event.detail=m_simulation?"SIM TX（无 ECU 响应）":"驱动已接受发送请求（非 ECU 确认）";m_events.append(event);return true;
}
void SignalTransmitter::tick(){if(!running())return;const auto now=captureTimeUs();QString error;if(m_plan.replay){replayTick(now);return;}
    if(m_lin){if(!m_lin->tick(now,error)){fail(error);return;}const auto previous=m_status.state;m_status=m_lin->status();if(previous!=m_status.state)emit statusChanged(m_status);if(!running()){m_timer.stop();flush();return;}}
    else if(m_plan.periodic){m_scheduler.pump(now,[this](const auto&i,QString&e){return send(i,e);});const auto stats=m_scheduler.statistics();
        m_status.sent=stats.sent;m_status.missed=stats.missed;m_status.failures=stats.failures;
        if(!m_scheduler.running()){fail("全部 CAN 周期发送项已停止：驱动写入失败");return;}
    }else{int budget=256;bool pending=false;
        for(int n=0;n<m_plan.items.size();++n){const auto &i=m_plan.items[n];if(!i.enabled){m_remaining[n]=0;continue;}
            if(m_remaining[n]>0&&now>=m_due[n]&&budget>0){--budget;if(send(i,error)){++m_status.sent[i.key];--m_remaining[n];m_due[n]=now+qMax(1,i.periodMs)*1000LL;}else{m_status.failures[i.key]=error;m_remaining[n]=0;}}
            pending|=m_remaining[n]>0;}
        if(!pending){m_status.state=RunState::Stopped;m_status.detail="多次发送已完成";m_timer.stop();flush();return;}}
    // Deliver timestamps before the worker receives another bus batch. Status
    // updates remain throttled, but holding frames for 20 ms reorders captures.
    if(!m_events.isEmpty()){emit events(m_events);m_events.clear();}
    if(now-m_lastFlush>=20000)flush();
}
void SignalTransmitter::flush(){m_lastFlush=captureTimeUs();if(!m_events.isEmpty()){emit events(m_events);m_events.clear();}emit statusChanged(m_status);}
bool SignalTransmitter::cleanup(QString &error){
    bool ok=true;
    if(m_plan.replay&&!m_simulation&&m_plan.bus==Bus::Lin&&m_replayLin){
        ok=m_replayLin->signalStop();if(!ok)error=m_replayLin->lastErrorText();
    }
    if(m_lin){QString detail;if(!m_lin->stop(detail)){ok=false;if(!error.isEmpty())error+="；";error+=detail;}}
    m_status.cleanupPending=!ok;
    if(!ok&&error.isEmpty())error="硬件未确认停止";
    return ok;
}
void SignalTransmitter::stop(quint64 run){
    if(run&&run!=m_status.run)return;if(!running())return;m_timer.stop();m_scheduler.stop();QString error;
    m_status.pending.clear();
    if(cleanup(error)){m_status.state=RunState::Stopped;m_status.detail="已停止；已进入硬件队列的帧可完成";}
    else{m_status.state=RunState::Faulted;m_status.detail="停止失败，硬件响应可能仍启用，请重试停止或断开通道："+error;emit notice(m_status.detail);}
    flush();
}
void SignalTransmitter::hardwareClosed(){
    if(!m_status.cleanupPending)return;
    m_status.cleanupPending=false;m_status.state=RunState::Stopped;m_status.detail="硬件通道已断开";
    m_lin.reset();m_device.reset();m_can=nullptr;m_replayLin=nullptr;flush();
}
void SignalTransmitter::fail(const QString&why){
    m_timer.stop();m_scheduler.stop();QString error;cleanup(error);m_status.state=RunState::Faulted;m_status.pending.clear();
    m_status.detail=why+(error.isEmpty()?QString():"；停止未确认："+error);emit notice(m_status.detail);flush();
}
void SignalTransmitter::linkFailed(const QString&why){if(signal::active(m_status.state))fail(why);}
void SignalTransmitter::update(const PayloadUpdate&u){if(!running()||u.run!=m_plan.run||u.connection!=m_plan.connection)return;
    QString error;if(m_plan.replay){for(auto &i:m_plan.items)if(i.key==u.key){if(u.enableChanged)i.enabled=u.enabled;else if(u.payload.revision>i.revision){i.payload=u.payload.bytes;i.revision=u.payload.revision;}}return;}if(u.enableChanged){if(m_lin){if(!m_lin->setEnabled(u.key,u.enabled,captureTimeUs(),error))fail(error);}else{m_scheduler.setEnabled(u.key,u.enabled);for(auto &i:m_plan.items)if(i.key==u.key)i.enabled=u.enabled;}return;}if(m_lin){if(!m_lin->update(u.key,u.payload,error))fail(error);}else{m_scheduler.update(u.key,u.payload);for(auto&i:m_plan.items)if(i.key==u.key&&u.payload.revision>i.revision&&u.payload.bytes.size()==i.payload.size()){i.payload=u.payload.bytes;i.revision=u.payload.revision;}}}
void SignalTransmitter::switchSchedule(quint64 run,const QString&name){if(!running()||run!=m_plan.run||!m_lin)return;QString error;
    if(!m_lin->switchTo(name,captureTimeUs(),error)){fail(error);return;}m_status=m_lin->status();flush();}
bool SignalTransmitter::publishesLin(quint32 id)const{if(m_plan.replay&&running()){for(const auto&r:m_plan.replayFrames)if(r.id==id&&r.publish)return true;}return m_lin&&running()&&m_lin->publishes(id);}
void SignalTransmitter::observe(const BusFrameEvents&events){if(m_lin&&running())for(const auto&e:events)m_lin->observe(e);}
}

namespace host {
bool SignalTransmitter::replaySend(signal::ReplayRecord r,qint64 logicalUs,QString &error){
    const auto key=signal::frameKey(m_plan.bus,r.id,r.extended);
    const int overrideIndex=m_itemIndex.value(key,-1);if(overrideIndex>=0&&m_plan.items[overrideIndex].enabled){r.payload=m_plan.items[overrideIndex].payload;r.length=r.payload.size();r.rtr=false;}
    signal::BusFrameEvent event;event.bus=m_plan.bus;event.id=r.id;event.extended=r.extended;event.bytes=r.payload;event.replay=true;event.fd=r.fd;event.brs=r.brs;event.esi=r.esi;event.rtr=r.rtr;event.length=r.length;event.classicChecksum=r.classicChecksum;
    event.arrivalUs=(m_simulation?logicalUs:captureTimeUs())+m_plan.replayStampOffsetUs;event.source=r.rx?signal::EventSource::Received:signal::EventSource::Simulated;event.detail=m_simulation?"日志快速回放":"日志回放";
    if(!m_simulation){
        if(m_plan.bus==signal::Bus::Can){TPCANMsg msg={};msg.ID=r.id;msg.LEN=BYTE(r.length);msg.MSGTYPE=TPCANMessageType((r.extended?PCAN_MESSAGE_EXTENDED:PCAN_MESSAGE_STANDARD)|(r.rtr?PCAN_MESSAGE_RTR:0));std::memcpy(msg.DATA,r.payload.constData(),size_t(qMin(8,int(r.payload.size()))));
            if(!m_can||!m_can->sendRaw(msg)){error=m_can?m_can->lastErrorText():"CAN 驱动不可用";return false;}event.source=signal::EventSource::RequestAccepted;}
        else if(m_plan.role==signal::LinRole::Monitor){event.source=signal::EventSource::Received;event.detail="观测节点：仅软件重现日志";}
        else if(m_plan.role==signal::LinRole::Master){TLINMsg msg={};msg.FrameId=BYTE(r.id);msg.Length=BYTE(r.length);msg.Direction=r.publish?dirPublisher:dirSubscriber;msg.ChecksumType=r.classicChecksum?cstClassic:cstEnhanced;std::memcpy(msg.Data,r.payload.constData(),size_t(qMin(8,int(r.payload.size()))));
            if(!m_replayLin||!m_replayLin->sendRaw(msg)){error=m_replayLin?m_replayLin->lastErrorText():"LIN 驱动不可用";return false;}event.source=signal::EventSource::RequestAccepted;event.detail=r.publish?"回放：主节点帧头与数据请求":"回放：主节点帧头请求，等待外部数据";if(!r.publish){event.bytes.clear();event.length=0;}}
        else{if(!r.publish)return true;const bool existing=m_replayResponses.contains(r.id)&&m_replayResponses[r.id].length==r.length;m_replayResponses[r.id]=r;bool ok=false;
            if(existing&&m_replayLin)ok=m_replayLin->signalUpdateFrame(BYTE(r.id),r.payload);
            else{QVector<TLINFrameEntry> entries;for(const auto &response:m_replayResponses){TLINFrameEntry entry={};entry.FrameId=BYTE(response.id);entry.Length=BYTE(response.length);entry.Direction=dirPublisher;entry.ChecksumType=response.classicChecksum?cstClassic:cstEnhanced;entry.Flags=FRAME_FLAG_RESPONSE_ENABLE;std::memcpy(entry.InitialData,response.payload.constData(),size_t(qMin(8,int(response.payload.size()))));entries.append(entry);}if(m_replayLin)ok=m_replayLin->signalInstallFrames(entries);}
            if(!ok){error=m_replayLin?m_replayLin->lastErrorText():"LIN 驱动不可用";return false;}
            // Updating a slave's response buffer is not a transmitted bus frame.
            return true;}
    }
    m_events.append(event);++m_status.sent[key];return true;
}
void SignalTransmitter::replayTick(qint64 now){
    if(now<m_plan.replayStartUs)return;QElapsedTimer budgetClock;budgetClock.start();int budget=512;QString error;
    while(budget--&&budgetClock.elapsed()<4){
        qint64 due=m_plan.replayDurationUs;int extra=-1;bool logged=false;
        if(m_replayIndex<m_plan.replayFrames.size()){due=m_plan.replayFrames[m_replayIndex].timeUs;logged=true;}
        for(int n=0;n<m_plan.items.size();++n){const auto&i=m_plan.items[n];if(i.enabled&&!m_loggedKeys.contains(i.key)&&m_extraDue[n]<due){due=m_extraDue[n];extra=n;logged=false;}}
        const auto logical=m_plan.replayStartUs+qint64(m_replayRound)*m_plan.replayDurationUs+due;
        if(!m_simulation&&now<logical)break;
        if(due>=m_plan.replayDurationUs){
            ++m_replayRound;if(!m_plan.periodic&&m_replayRound>=m_plan.repeatCount){stop();if(m_status.state==RunState::Stopped)m_status.detail="多次回放已完成";flush();return;}
            m_replayIndex=0;m_extraDue.fill(0,m_plan.items.size());continue;
        }
        signal::ReplayRecord record;
        if(logged)record=m_plan.replayFrames[m_replayIndex++];
        else if(extra>=0){const auto&i=m_plan.items[extra];record.id=i.id;record.extended=i.extended;record.payload=i.payload;record.length=i.payload.size();record.classicChecksum=i.classicChecksum;record.publish=i.publisher==m_plan.node;m_extraDue[extra]+=qMax(1,i.periodMs)*1000LL;}
        else break;
        if(!replaySend(record,logical,error)){fail(error);return;}
    }
    if(!m_events.isEmpty()){emit events(m_events);m_events.clear();}if(now-m_lastFlush>=20000)flush();
}
}
