#include "SignalTransmitter.h"
#include "model/SignalCodec.h"
#include "driverCan/tstPeakCan.h"
#include "driverLin/tstPeakLin.h"
#include <algorithm>
#include <cstring>
namespace host {
using namespace signal;
namespace {
class PeakLinSignalDevice final:public LinDevice {
    tstPeakLin &lin;
    bool result(bool ok,QString&error){if(!ok)error=lin.lastErrorText();return ok;}
public:
    explicit PeakLinSignalDevice(tstPeakLin &driver):lin(driver){}
    bool configure(LinRole role,int,QString&e)override{return result(lin.signalConfigureMode(role==LinRole::Master),e);}
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
    bool requestBoundary(QString&e)override{return result(lin.signalRequestRoundBoundary(),e);}
    bool boundary(bool&reached,QString&e)override{return result(lin.signalRoundBoundaryReached(reached),e);}
    bool update(const TxItem&i,QString&e)override{return result(lin.signalUpdateFrame(BYTE(i.id),i.payload),e);}
};
}
SignalTransmitter::SignalTransmitter(QObject*parent):QObject(parent){m_clock.start();m_timer.setTimerType(Qt::PreciseTimer);m_timer.setInterval(1);connect(&m_timer,&QTimer::timeout,this,&SignalTransmitter::tick);}
SignalTransmitter::~SignalTransmitter(){stop();}
void SignalTransmitter::start(const TxPlan&plan,int bitrate,bool simulation,tstPeakCan*can,tstPeakLin*lin){
    if(running())return;m_plan=plan;m_status={};m_status.run=plan.run;m_status.state=RunState::Starting;m_can=can;m_simulation=simulation;m_once=0;m_events.clear();m_lin.reset();m_device.reset();
    QSet<QString> keys;QSet<quint32> linIds;QVector<FrameDefinition> definitions;
    for(const auto&i:plan.items){if(keys.contains(i.key)||i.payload.size()>8||i.id>(plan.bus==Bus::Can?0x1fffffff:61)||
            (plan.bus==Bus::Can&&!i.extended&&i.id>0x7ff)||(plan.bus==Bus::Lin&&(i.payload.isEmpty()||linIds.contains(i.id)||(i.id>=60&&(i.payload.size()!=8||!i.classicChecksum))))||
            (plan.bus==Bus::Can&&plan.periodic&&i.periodMs<1)){fail("发送计划未通过 worker 校验");return;}
        keys.insert(i.key);linIds.insert(i.id);FrameDefinition f;f.key=i.key;f.id=i.id;f.length=i.payload.size();definitions.append(f);}
    if(plan.bus==Bus::Can&&plan.items.isEmpty()){fail("发送计划为空");return;}
    if(plan.bus==Bus::Lin){
        if(!simulation&&!lin){fail("LIN 驱动不可用");return;}
        if(plan.role!=LinRole::Monitor){auto s=std::find_if(plan.schedules.begin(),plan.schedules.end(),[&](const auto&s){return s.name==plan.schedule;});if(s==plan.schedules.end()){fail("所选调度表不存在");return;}const auto error=SignalCodec::validateSchedule(*s,definitions,bitrate);if(!error.isEmpty()){fail(error);return;}}
        if(!simulation)m_device=std::make_unique<PeakLinSignalDevice>(*lin);m_lin=std::make_unique<LinScheduleRunner>(m_device.get());
        m_lin->event=[this](const BusFrameEvent&e){m_events.append(e);};m_lin->notice=[this](const QString&s){emit notice(s);};m_lin->drainBeforeSwitch=drainReceived;QString error;
        if(!m_lin->start(plan,bitrate,m_clock.nsecsElapsed()/1000,error)){fail(error);return;}m_status=m_lin->status();
    }else {if(!simulation&&!can){fail("CAN 驱动不可用");return;}if(plan.periodic)m_scheduler.start(plan.items,m_clock.nsecsElapsed()/1000);m_status.state=RunState::Running;m_status.detail=simulation?"SIM CAN 运行":"CAN 请求提交中";}
    emit statusChanged(m_status);m_timer.start();tick();
}
bool SignalTransmitter::send(const TxItem&i,QString&error){
    if(!m_simulation){TPCANMsg message={};message.ID=i.id;message.MSGTYPE=i.extended?PCAN_MESSAGE_EXTENDED:PCAN_MESSAGE_STANDARD;message.LEN=BYTE(i.payload.size());std::memcpy(message.DATA,i.payload.constData(),size_t(i.payload.size()));
        if(!m_can||!m_can->sendRaw(message)){error=m_can?m_can->lastErrorText():"CAN 驱动已释放";return false;}}
    BusFrameEvent event;event.bus=Bus::Can;event.id=i.id;event.extended=i.extended;event.bytes=i.payload;event.arrivalUs=m_clock.nsecsElapsed()/1000;
    event.source=m_simulation?EventSource::Simulated:EventSource::RequestAccepted;event.detail=m_simulation?"SIM TX（无 ECU 响应）":"驱动已接受发送请求（非 ECU 确认）";m_events.append(event);return true;
}
void SignalTransmitter::tick(){if(!running())return;const auto now=m_clock.nsecsElapsed()/1000;QString error;
    if(m_lin){if(!m_lin->tick(now,error)){fail(error);return;}const auto previous=m_status.state;m_status=m_lin->status();if(previous!=m_status.state)emit statusChanged(m_status);}
    else if(m_plan.periodic){m_scheduler.pump(now,[this](const auto&i,QString&e){return send(i,e);});const auto stats=m_scheduler.statistics();
        m_status.sent=stats.sent;m_status.missed=stats.missed;m_status.failures=stats.failures;
        if(!m_scheduler.running()){fail("全部 CAN 周期发送项已停止：驱动写入失败");return;}
    }else{int budget=256;while(m_once<m_plan.items.size()&&budget--){const auto&i=m_plan.items[m_once++];if(send(i,error))++m_status.sent[i.key];else m_status.failures[i.key]=error;}
        if(m_once==m_plan.items.size()){m_status.state=RunState::Stopped;m_status.detail="单次发送请求已提交";m_timer.stop();flush();return;}}
    if(now-m_lastFlush>=20000)flush();
}
void SignalTransmitter::flush(){m_lastFlush=m_clock.nsecsElapsed()/1000;if(!m_events.isEmpty()){emit events(m_events);m_events.clear();}emit statusChanged(m_status);}
void SignalTransmitter::stop(quint64 run){if(run&&run!=m_status.run)return;if(!running())return;m_timer.stop();m_scheduler.stop();QString error;
    if(m_lin&&!m_lin->stop(error)){m_status=m_lin->status();}else{m_status.state=RunState::Stopped;m_status.pending.clear();m_status.detail="已停止；已进入硬件队列的帧可完成";}flush();}
void SignalTransmitter::fail(const QString&why){m_timer.stop();m_scheduler.stop();QString error;if(m_lin)m_lin->stop(error);m_status.state=RunState::Faulted;m_status.pending.clear();m_status.detail=why+(error.isEmpty()?QString():"；清理："+error);emit notice(m_status.detail);flush();}
void SignalTransmitter::linkFailed(const QString&why){if(running())fail(why);}
void SignalTransmitter::update(const PayloadUpdate&u){if(!running()||u.run!=m_plan.run||u.connection!=m_plan.connection)return;
    QString error;if(m_lin){if(!m_lin->update(u.key,u.payload,error))fail(error);}else{m_scheduler.update(u.key,u.payload);for(auto&i:m_plan.items)if(i.key==u.key&&u.payload.revision>i.revision&&u.payload.bytes.size()==i.payload.size()){i.payload=u.payload.bytes;i.revision=u.payload.revision;}}}
void SignalTransmitter::switchSchedule(quint64 run,const QString&name){if(!running()||run!=m_plan.run||!m_lin)return;QString error;
    if(!m_lin->switchTo(name,m_clock.nsecsElapsed()/1000,error)){fail(error);return;}m_status=m_lin->status();flush();}
bool SignalTransmitter::publishesLin(quint32 id)const{return m_lin&&running()&&m_lin->publishers().contains(frameKey(Bus::Lin,id));}
void SignalTransmitter::observe(const BusFrameEvents&events){if(m_lin&&running())for(const auto&e:events)m_lin->observe(e);}
}
