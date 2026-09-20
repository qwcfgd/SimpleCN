#include "SignalTransmitViewModel.h"
#include "model/DatabaseImporter.h"
#include "model/SignalCodec.h"
#include "infrastructure/SignalConfigurationStore.h"
#include <QtConcurrent/QtConcurrentRun>
#include <limits>
#include <algorithm>
#include <cmath>
namespace host {
using namespace signal;
SignalTransmitViewModel::SignalTransmitViewModel(Bus bus,QObject*parent):QObject(parent),m_bus(bus){
    auto empty=QSharedPointer<DatabaseDefinition>::create();empty->bus=bus;m_database=empty;
    connect(&m_import,&QFutureWatcher<ImportResult>::finished,this,[this]{
        m_importing=false;const auto result=m_import.result();QString error=result.error;
        if(result.database&&!install(result.database,error)){}m_message=error.isEmpty()?"导入完成；源文件只读":error;emit notice(m_message);emit changed();
    });
    connect(&m_configurationLoad,&QFutureWatcher<ConfigurationResult>::finished,this,[this]{
        m_importing=false;const auto result=m_configurationLoad.result();
        if(result.ok&&canStructure()){applyConfiguration(result.snapshot.database,result.snapshot.working);m_message="读取完成；已停止，并建立新的撤销基准";}
        else m_message=result.ok?"通道状态已改变；通信配置未应用":result.error;
        emit notice(m_message);emit changed();
    });
}
QVector<FrameDefinition> SignalTransmitViewModel::definitions()const{auto list=m_database->frames;list+=m_work.customFrames;return list;}
QVector<FrameDefinition> SignalTransmitViewModel::queuedDefinitions()const{
    QVector<FrameDefinition> result;for(const auto&f:definitions())if(m_work.frames.value(f.key).enabled)result.append(f);std::stable_sort(result.begin(),result.end(),[](const auto&a,const auto&b){return a.id!=b.id?a.id<b.id:a.extended<b.extended;});return result;
}
bool SignalTransmitViewModel::selectCanNode(const QString&node,const QString&direction,QString&error){
    if(!canStructure()||m_bus!=Bus::Can||(!node.isEmpty()&&!m_database->nodes.contains(node))||
       (direction!="Tx"&&direction!="Rx"&&direction!="Tx/Rx")){error="当前不能选择 DBC 节点或方向";return false;}
    remember();m_work.canNode=node;m_work.canDirection=direction;
    for(const auto&f:m_database->frames){
        bool receive=false;for(const auto&s:f.fields)if(s.receivers.contains(node)){receive=true;break;}
        m_work.frames[f.key].enabled=!node.isEmpty()&&((direction!="Rx"&&(f.publisher==node||f.transmitters.contains(node)))||(direction!="Tx"&&receive));
    }
    emit structureChanged();emit changed();return true;
}
bool SignalTransmitViewModel::removeQueuedFrame(const QString&key,QString&error){
    if(!canStructure()||m_bus!=Bus::Can||!frame(key)||!m_work.frames.value(key).enabled){error="停止后才能删除待发送报文";return false;}
    if(frame(key)->custom)return removeCustom(key,error);
    remember();m_work.frames[key].enabled=false;emit structureChanged();emit changed();return true;
}
const FrameDefinition* SignalTransmitViewModel::frame(const QString&key)const{for(const auto&f:m_database->frames)if(f.key==key)return &f;for(const auto&f:m_work.customFrames)if(f.key==key)return &f;return nullptr;}
QString SignalTransmitViewModel::protocolInfo(const QString&key)const{const auto*f=frame(key);if(!f)return {};if(m_bus==Bus::Can)return f->extended?"经典 CAN · 扩展 29 bit":"经典 CAN · 标准 11 bit";
    return QString("PID 0x%1 · %2 checksum").arg(SignalCodec::linPid(quint8(f->id)),2,16,QChar('0')).arg(f->classicChecksum?"Classic":"Enhanced");}
QString SignalTransmitViewModel::publicationHint(const QString&key)const{const auto*f=frame(key);if(!f||m_bus!=Bus::Lin)return {};bool referenced=false;
    for(const auto&s:m_work.schedules)if(s.name==m_work.schedule)for(const auto&entry:s.entries)if(entry.frame==key)referenced=true;
    return referenced&&f->publisher==m_work.node&&m_work.role!=LinRole::Monitor?"当前节点发布且在所选表中":"仅保存本地副本";}
void SignalTransmitViewModel::setAvailability(bool connected,bool other,int bitrate,quint64 connection){
    if(m_connected==connected&&m_externalBusy==other&&m_bitrate==bitrate&&m_connection==connection)return;
    m_connected=connected;m_externalBusy=other;m_bitrate=bitrate;m_connection=connection;emit changed();
}
bool SignalTransmitViewModel::install(Database db,QString&error){
    if(!canStructure()){error="停止后才能导入数据库";return false;}
    WorkingSet work;work.node=db->master;work.schedules=db->schedules;if(!work.schedules.isEmpty())work.schedule=work.schedules.first().name;
    for(const auto&f:db->frames){TxDraft draft;if(!SignalCodec::initialize(f,db->bus,draft,error))return false;work.frames[f.key]=draft;}
    m_database=db;m_work=work;m_baseline=work;m_hasBaseline=true;m_history.clear();m_status={};emit structureChanged();emit changed();return true;
}
bool SignalTransmitViewModel::importFile(const QString&path,QString&error){
    if(!canStructure()){error="停止后才能导入数据库";return false;}
    const auto result=DatabaseImporter::load(path,m_bus);if(!result.database){error=result.error;return false;}return install(result.database,error);
}
void SignalTransmitViewModel::importAsync(const QString&path){
    if(!canStructure())return;m_importing=true;m_message="正在解析和校验数据库…";emit changed();const auto bus=m_bus;
    m_import.setFuture(QtConcurrent::run([path,bus]{return DatabaseImporter::load(path,bus);}));
}
void SignalTransmitViewModel::remember(){m_message.clear();m_history.append(m_work);}
void SignalTransmitViewModel::changedFrame(const QString&key){emit frameChanged(key);emit changed();}
bool SignalTransmitViewModel::commit(const QString&key,QString&error){
    auto &draft=m_work.frames[key];const auto*f=frame(key);if(!f){error="未知帧";return false;}
    if(!draft.errors.isEmpty()||!draft.frameError.isEmpty()){error="此帧仍有错误草稿；继续上一有效 payload";return false;}
    auto payload=draft.applied.bytes;if(!SignalCodec::encode(*f,draft.values,payload,error))return false;
    draft.applied.bytes=payload;draft.appliedValues=draft.values;++draft.applied.revision;
    if(running())emit payloadRequested({key,draft.applied,m_status.run,m_connection});return true;
}
bool SignalTransmitViewModel::editSignal(const QString&key,int field,const QString&input,bool physical,QString&error,int range){
    const auto*f=frame(key);if(!canData()||!f||field<0||field>=f->fields.size()||!f->issue.isEmpty()){error="当前不可编辑信号";return false;}
    remember();auto &draft=m_work.frames[key];auto value=physical?SignalCodec::parsePhysical(f->fields[field],input,range):SignalCodec::parseRaw(f->fields[field],input);
    draft.inputs[field]=(physical?"物理 ":"raw ")+input;
    if(!value.ok()){draft.errors[field]=value.error;error=value.error;changedFrame(key);return false;}
    draft.errors.remove(field);draft.warnings[field]=value.warning;draft.values[field]=value.raw;
    const bool ok=commit(key,error);changedFrame(key);return ok;
}
bool SignalTransmitViewModel::editPayload(const QString&key,const QString&input,QString&error){
    const auto*f=frame(key);if(!canData()||!f||!f->issue.isEmpty()){error="当前不可编辑帧数据";return false;}
    remember();auto&draft=m_work.frames[key];draft.frameInput=input;QByteArray bytes;
    if(!SignalCodec::parseBytes(input,f->length,bytes,error)){draft.frameError=error;changedFrame(key);return false;}
    auto values=draft.values;if(!SignalCodec::decode(*f,bytes,values,error)){draft.frameError=error;changedFrame(key);return false;}
    draft.frameError.clear();
    if(!draft.errors.isEmpty()){error="信号仍有错误草稿；整帧 HEX 未应用";draft.frameError=error;changedFrame(key);return false;}
    draft.values=values;draft.appliedValues=values;draft.applied.bytes=bytes;++draft.applied.revision;draft.inputs.clear();draft.warnings.clear();
    for(int i=0;i<f->fields.size();++i)draft.warnings[i]=SignalCodec::rangeWarning(f->fields[i],values[i]);
    if(running())emit payloadRequested({key,draft.applied,m_status.run,m_connection});changedFrame(key);return true;
}
bool SignalTransmitViewModel::setCanOptions(const QString&key,bool enabled,int period,QString&error){
    if(!canStructure()||m_bus!=Bus::Can||!frame(key)||period<0){error="停止后才能修改发送选择/周期（整数 ms）";return false;}
    if(enabled&&!frame(key)->issue.isEmpty()){error=frame(key)->issue;return false;}
    const bool membershipChanged=m_work.frames[key].enabled!=enabled;
    remember();m_work.frames[key].enabled=enabled;m_work.frames[key].cycleMs=period;if(membershipChanged)emit structureChanged();changedFrame(key);return true;
}
bool SignalTransmitViewModel::putCustom(const QString&oldKey,const QString&name,quint32 id,int length,int cycle,QString&error){
    if(!canStructure()||m_bus!=Bus::Can||name.trimmed().isEmpty()||id>0x1fffffff||length<0||length>8||cycle<0){error="自建帧需要有效名称、0–0x1FFFFFFF ID、0–8 字节及非负整数周期";return false;}
    if(!oldKey.isEmpty()&&(!frame(oldKey)||!frame(oldKey)->custom)){error="只能修改自建帧结构";return false;}
    for(const auto&f:definitions())if(f.key!=oldKey&&(f.id==id||f.name==name)){error="与当前通道全部 DBC/自建帧数值 ID 或名称重复；未提交";return false;}
    remember();FrameDefinition f;f.key=frameKey(Bus::Can,id,id>0x7ff);f.id=id;f.extended=id>0x7ff;f.length=length;f.name=name.trimmed();f.custom=true;f.cycleMs=cycle;
    TxDraft draft;if(!oldKey.isEmpty()){draft=m_work.frames.take(oldKey);for(int i=0;i<m_work.customFrames.size();++i)if(m_work.customFrames[i].key==oldKey){m_work.customFrames.removeAt(i);break;}}
    if(draft.applied.bytes.size()!=length){draft.applied.bytes=QByteArray(length,0);draft.frameError.clear();draft.frameInput.clear();}draft.cycleMs=cycle;++draft.applied.revision;
    draft.enabled=true;m_work.customFrames.append(f);m_work.frames[f.key]=draft;emit structureChanged();emit changed();return true;
}
bool SignalTransmitViewModel::removeCustom(const QString&key,QString&error){
    if(!canStructure()||!frame(key)||!frame(key)->custom){error="停止后只能删除自建 CAN 帧";return false;}
    remember();for(int i=0;i<m_work.customFrames.size();++i)if(m_work.customFrames[i].key==key){m_work.customFrames.removeAt(i);break;}m_work.frames.remove(key);emit structureChanged();emit changed();return true;
}
bool SignalTransmitViewModel::setRole(LinRole role,const QString&node,QString&error){
    if(!canStructure()||m_bus!=Bus::Lin){error="运行时不能改变角色/节点";return false;}
    if(role!=LinRole::Master&&role!=LinRole::Slave&&role!=LinRole::Monitor){error="未知 LIN 角色";return false;}
    QString selected=node;
    if(selected.isEmpty()){selected=m_database->master;if(role==LinRole::Slave)for(const auto&n:m_database->nodes)if(n!=m_database->master){selected=n;break;}}
    if((role==LinRole::Master&&selected!=m_database->master)||(role==LinRole::Slave&&(!m_database->nodes.contains(selected)||selected==m_database->master))){error="角色与实际节点不匹配";return false;}
    if(m_work.role==role&&m_work.node==selected)return true;
    remember();m_work.role=role;m_work.node=selected;emit structureChanged();emit changed();return true;
}
bool SignalTransmitViewModel::selectSchedule(const QString&name,QString&error){
    const auto it=std::find_if(m_work.schedules.begin(),m_work.schedules.end(),[&](const auto&s){return s.name==name;});
    if(it==m_work.schedules.end()||m_importing||m_externalBusy||m_status.state==RunState::Starting||m_status.state==RunState::Stopping||m_status.state==RunState::SwitchPending||m_status.state==RunState::Switching){error="调度表不存在或切换正在进行";return false;}
    if(running()&&m_work.role!=LinRole::Monitor){error=SignalCodec::validateSchedule(*it,definitions(),m_bitrate);if(!error.isEmpty())return false;
        m_status.state=RunState::SwitchPending;m_status.pending=name;emit scheduleRequested(m_status.run,name);
    }else {remember();m_work.schedule=name;}emit changed();return true;
}
bool SignalTransmitViewModel::replaceSchedules(const QVector<Schedule>&schedules,const QString&selected,QString&error){
    if(!canStructure()||m_bus!=Bus::Lin){error="运行时调度结构冻结";return false;}QSet<QString> names;
    auto normalized=schedules;
    for(auto&s:normalized){if(s.name.trimmed().isEmpty()||names.contains(s.name)){error="调度表名称为空或重复";return false;}names.insert(s.name);
        for(auto&slot:s.entries){if(!frame(slot.frame)){
                bool known=false;for(const auto&original:m_database->schedules)for(const auto&entry:original.entries)if(entry.frame==slot.frame&&!entry.issue.isEmpty()){known=true;slot.issue=entry.issue;s.issue=entry.issue;}
                if(!known){error="槽只能引用当前 LDF 帧；已识别的不可执行源槽可保留供浏览";return false;}
            }bool ok=false;const auto delay=slot.delayMs.toDouble(&ok);if(!ok||!std::isfinite(delay)||delay<=0){error="delay 必须为正数";return false;}}}
    if(!selected.isEmpty()&&!names.contains(selected)){error="所选调度表不存在";return false;}
    remember();m_work.schedules=normalized;m_work.schedule=selected;emit structureChanged();emit changed();return true;
}
void SignalTransmitViewModel::restoreSnapshot(const WorkingSet&snapshot){
    auto next=snapshot;for(auto it=next.frames.begin();it!=next.frames.end();++it)if(m_work.frames.contains(it.key())){it->rx=m_work.frames[it.key()].rx;it->applied.revision=m_work.frames[it.key()].applied.revision+1;}
    m_work=next;emit structureChanged();emit changed();
}
void SignalTransmitViewModel::back(){if(canBack()){const auto snapshot=m_history.takeLast();restoreSnapshot(snapshot);}}
void SignalTransmitViewModel::restore(){if(canRestore()){restoreSnapshot(m_baseline);m_history.clear();emit changed();}}
bool SignalTransmitViewModel::hasInvalidDraft()const{for(const auto&d:m_work.frames)if(!d.errors.isEmpty()||!d.frameError.isEmpty())return true;return false;}
bool SignalTransmitViewModel::start(bool periodic,QString&error){
    if(!canStart()){error="请连接通道，并停止其他任务";return false;}
    TxPlan plan;plan.bus=m_bus;plan.role=m_work.role;plan.node=m_work.node;plan.schedule=m_work.schedule;plan.schedules=m_work.schedules;
    plan.periodic=periodic;plan.connection=m_connection;plan.databaseRevision=m_database->sha256;
    if(m_bus==Bus::Lin){
        if(m_work.role!=LinRole::Monitor){auto it=std::find_if(plan.schedules.begin(),plan.schedules.end(),[&](const auto&s){return s.name==plan.schedule;});if(it==plan.schedules.end()){error="请选择调度表";return false;}
            error=SignalCodec::validateSchedule(*it,definitions(),m_bitrate);if(!error.isEmpty())return false;}
        if((plan.role==LinRole::Master&&plan.node!=m_database->master)||(plan.role==LinRole::Slave&&(!m_database->nodes.contains(plan.node)||plan.node==m_database->master))){error="请选择有效的实际节点";return false;}
    }
    for(const auto&f:definitions()){const auto &draft=m_work.frames[f.key];if(m_bus==Bus::Can&&!draft.enabled)continue;
        if(m_bus==Bus::Can&&(!f.issue.isEmpty()||(periodic&&draft.cycleMs<1))){error=f.issue.isEmpty()?"周期发送的每一项必须设置至少 1 ms":f.issue;return false;}
        if(m_bus==Bus::Lin&&!f.issue.isEmpty())continue;
        plan.items.append({f.key,f.id,f.extended,draft.applied.bytes,draft.cycleMs,draft.applied.revision,m_bus==Bus::Lin&&f.id==61&&m_work.role==LinRole::Slave?m_work.node:f.publisher,f.classicChecksum});
    }
    if(m_bus==Bus::Can&&plan.items.isEmpty()){error="请先选择节点报文或右键自建报文";return false;}
    plan.run=++m_nextRun;m_status={};m_status.state=RunState::Starting;m_status.run=plan.run;m_status.detail="启动中";emit startRequested(plan);emit changed();return true;
}
void SignalTransmitViewModel::stop(){if(!running())return;m_status.state=RunState::Stopping;emit stopRequested(m_status.run);emit changed();}
void SignalTransmitViewModel::applyStatus(RunStatus status){if(status.run!=m_status.run)return;m_status=std::move(status);if(!m_status.current.isEmpty())m_work.schedule=m_status.current;emit changed();}
void SignalTransmitViewModel::receive(BusFrameEvents events){QSet<QString> updated;for(const auto&e:events){
    if(e.bus!=m_bus||e.source==EventSource::RequestAccepted||e.source==EventSource::HardwareEcho||e.source==EventSource::Simulated)continue;
    const auto key=frameKey(e.bus,e.id,e.extended);if(!m_work.frames.contains(key))continue;
    auto&rx=m_work.frames[key].rx;rx={e.bytes,e.hardwareUs,e.arrivalUs,true,e.valid,e.detail};updated.insert(key);
    }for(const auto&key:updated)emit frameChanged(key);
}
QJsonObject SignalTransmitViewModel::configuration(const QString&directory)const{
    return SignalConfigurationStore::serialize(m_database,m_work,directory);
}
bool SignalTransmitViewModel::save(const QString&path,QString&error)const{
    if(!SignalConfigurationStore::save(path,m_database,m_work,error))return false;
    if(hasInvalidDraft())error="已保存最后有效值；错误草稿未保存";return true;
}
bool SignalTransmitViewModel::read(const QString&path,QString&error){
    if(!canStructure()){error="停止后才能读取通信配置";return false;}
    ConfigurationSnapshot snapshot;if(!SignalConfigurationStore::read(path,m_bus,snapshot,error))return false;
    applyConfiguration(snapshot.database,snapshot.working);return true;
}
void SignalTransmitViewModel::readAsync(const QString&path){
    if(!canStructure())return;m_importing=true;m_message="正在读取配置并校验源数据库…";emit changed();const auto bus=m_bus;
    m_configurationLoad.setFuture(QtConcurrent::run([path,bus]{ConfigurationResult result;result.ok=SignalConfigurationStore::read(path,bus,result.snapshot,result.error);return result;}));
}
void SignalTransmitViewModel::readConfigurationAsync(const QJsonObject&root,const QString&directory){
    if(!canStructure())return;m_importing=true;m_message="正在恢复配置并校验源数据库…";emit changed();const auto bus=m_bus;
    m_configurationLoad.setFuture(QtConcurrent::run([root,directory,bus]{ConfigurationResult result;result.ok=SignalConfigurationStore::parse(root,bus,directory,result.snapshot,result.error);return result;}));
}
bool SignalTransmitViewModel::readConfiguration(const QJsonObject&root,const QString&directory,QString&error){
    if(!canStructure()){error="停止后才能读取通信配置";return false;}
    ConfigurationSnapshot snapshot;if(!SignalConfigurationStore::parse(root,m_bus,directory,snapshot,error))return false;
    applyConfiguration(snapshot.database,snapshot.working);return true;
}
void SignalTransmitViewModel::applyConfiguration(Database database,const WorkingSet&work){
    m_database=database;m_work=work;m_baseline=work;m_hasBaseline=true;m_history.clear();m_status={};
    emit structureChanged();emit changed();
}
}
