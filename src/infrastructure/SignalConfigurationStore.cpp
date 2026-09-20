#include "SignalConfigurationStore.h"
#include "model/DatabaseImporter.h"
#include "model/SignalCodec.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
namespace host::signal {
static bool legacyDiagnosticIssue(const QString&issue){return issue=="首版不执行 LIN 诊断帧/槽"||issue=="首版不执行诊断槽：MasterReq"||issue=="首版不执行诊断槽：SlaveResp";}
static QVector<FrameDefinition> definitions(const Database&db,const WorkingSet&work){auto result=db->frames;result+=work.customFrames;return result;}
QJsonObject SignalConfigurationStore::serialize(const Database&db,const WorkingSet&work,const QString&directory){
    QJsonObject root{{"schema","signal-communication"},{"version",1},{"bus",db->bus==Bus::Can?"CAN":"LIN"}};
    auto path=db->path;if(!directory.isEmpty()&&!path.isEmpty())path=QDir(directory).relativeFilePath(path);root["source"]=path;root["sha256"]=db->sha256;
    root["role"]=int(work.role);root["node"]=work.node;root["schedule"]=work.schedule;
    if(db->bus==Bus::Can){root["canNode"]=work.canNode;root["canDirection"]=work.canDirection;}
    QJsonArray custom;for(const auto&f:work.customFrames)custom.append(QJsonObject{{"name",f.name},{"id",QString::number(f.id)},{"length",f.length}});root["custom"]=custom;
    QJsonArray frames;for(const auto&f:definitions(db,work)){const auto d=work.frames.value(f.key);QJsonArray values;for(int i=0;i<f.fields.size();++i)values.append(SignalCodec::rawText(f.fields[i],d.appliedValues.value(i)));
        frames.append(QJsonObject{{"key",f.key},{"payload",QString::fromLatin1(d.applied.bytes.toHex())},{"values",values},{"enabled",d.enabled},{"cycleMs",d.cycleMs}});}root["frames"]=frames;
    QJsonArray schedules;for(const auto&s:work.schedules){QJsonArray entries;for(const auto&slot:s.entries)entries.append(QJsonObject{{"frame",slot.frame},{"delayMs",slot.delayMs},{"issue",slot.issue}});schedules.append(QJsonObject{{"name",s.name},{"issue",s.issue},{"slots",entries}});}root["schedules"]=schedules;return root;
}
bool SignalConfigurationStore::save(const QString&path,const Database&db,const WorkingSet&work,QString&error){
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){error=file.errorString();return false;}const auto bytes=QJsonDocument(serialize(db,work,QFileInfo(path).absolutePath())).toJson();
    if(file.write(bytes)!=bytes.size()||!file.commit()){error=file.errorString();return false;}return true;
}
bool SignalConfigurationStore::read(const QString&path,Bus bus,ConfigurationSnapshot&snapshot,QString&error){
    QFile file(path);if(!file.open(QIODevice::ReadOnly)){error=file.errorString();return false;}if(file.size()>8*1024*1024){error="通信配置超过 8 MiB";return false;}
    QJsonParseError parseError;const auto json=QJsonDocument::fromJson(file.readAll(),&parseError);
    if(parseError.error!=QJsonParseError::NoError||!json.isObject()){error="通信配置 JSON 无效："+parseError.errorString();return false;}
    return parse(json.object(),bus,QFileInfo(path).absolutePath(),snapshot,error);
}
bool SignalConfigurationStore::parse(const QJsonObject&root,Bus bus,const QString&directory,ConfigurationSnapshot&snapshot,QString&error){
    auto reject=[&](const QString&why){error=why;return false;};
    if(root["schema"]!="signal-communication"||root["version"]!=1||root["bus"]!=(bus==Bus::Can?"CAN":"LIN"))return reject("配置版本、schema 或总线不匹配");
    for(const auto&key:{"frames","custom","schedules"})if(!root[key].isArray())return reject("缺少配置数组："+QString(key));
    if(!root["source"].isString()||!root["sha256"].isString())return reject("缺少数据库源信息");
    ConfigurationSnapshot next;auto empty=QSharedPointer<DatabaseDefinition>::create();empty->bus=bus;next.database=empty;QString source=root["source"].toString();
    if(!source.isEmpty()){
        if(QDir::isRelativePath(source))source=QDir(directory).absoluteFilePath(source);const auto loaded=DatabaseImporter::load(source,bus);
        if(!loaded.database)return reject(loaded.error);next.database=loaded.database;
        if(loaded.database->sha256!=root["sha256"].toString())return reject("源数据库摘要已变化；未应用配置。原摘要 "+root["sha256"].toString()+"；当前 "+loaded.database->sha256+"。请重新导入并核对布局差异。");
    }else if(bus==Bus::Lin||!root["sha256"].toString().isEmpty())return reject("配置缺少关联数据库路径");
    auto&work=next.working;QSet<QString> keys,names;QSet<quint32> ids;
    for(const auto&f:next.database->frames){TxDraft d;if(!SignalCodec::initialize(f,bus,d,error))return false;work.frames[f.key]=d;keys.insert(f.key);names.insert(f.name);ids.insert(f.id);}
    for(const auto&value:root["custom"].toArray()){
        if(bus!=Bus::Can)return reject("LIN 配置不允许自建 CAN 帧");const auto o=value.toObject();bool ok=false;const auto id=o["id"].toString().toULongLong(&ok);const auto name=o["name"].toString();
        const auto length=o["length"];
        if(!ok||id>0x1fffffff||!length.isDouble()||length.toDouble()!=length.toInt(-1)||length.toInt(-1)<0||length.toInt()>8||name.trimmed().isEmpty()||ids.contains(quint32(id))||names.contains(name))return reject("自建帧 ID/名称重复或结构无效");
        FrameDefinition f;f.custom=true;f.id=quint32(id);f.extended=id>0x7ff;f.key=frameKey(Bus::Can,f.id,f.extended);f.name=name;f.length=length.toInt();
        TxDraft d;if(!SignalCodec::initialize(f,bus,d,error))return false;work.customFrames.append(f);work.frames[f.key]=d;keys.insert(f.key);ids.insert(f.id);names.insert(name);
    }
    const auto all=definitions(next.database,work);QMap<QString,FrameDefinition> byKey;for(const auto&f:all)byKey[f.key]=f;
    QSet<QString> seen;for(const auto&value:root["frames"].toArray()){
        const auto o=value.toObject();const auto key=o["key"].toString();if(!byKey.contains(key)||seen.contains(key))return reject("配置帧引用未知或重复："+key);seen.insert(key);const auto&f=byKey[key];auto&d=work.frames[key];QByteArray bytes;
        if(!o["payload"].isString()||!SignalCodec::parseBytes(o["payload"].toString(),f.length,bytes,error))return reject(error.isEmpty()?"缺少帧 payload":error);
        if(!o["values"].isArray())return reject("缺少信号使用值数组");const auto values=o["values"].toArray();if(values.size()!=f.fields.size())return reject("配置信号数量不一致");
        QVector<RawValue> raw;for(int i=0;i<values.size();++i){if(!values[i].isString())return reject("64 位/raw 值必须使用字符串");const auto r=SignalCodec::parseRaw(f.fields[i],values[i].toString());if(!r.ok())return reject(r.error);raw.append(r.raw);d.warnings[i]=r.warning;}
        if(f.issue.isEmpty()){auto check=bytes;if(!SignalCodec::encode(f,raw,check,error))return false;if(check!=bytes)return reject("配置 payload 与信号使用值不一致");}
        const auto cycle=o["cycleMs"];if(!cycle.isDouble()||cycle.toDouble()!=cycle.toInt(-1)||cycle.toInt(-1)<0||!o["enabled"].isBool())return reject("无效周期/启用字段");
        d.values=d.appliedValues=raw;d.applied.bytes=bytes;d.enabled=o["enabled"].toBool();d.cycleMs=cycle.toInt();
        // For CAN, enabled is persisted queue membership; unsupported entries remain visible, but start rejects them.
        if(bus==Bus::Lin&&d.enabled&&!f.issue.isEmpty())return reject("不支持的帧不能启用发送");
    }
    for(const auto&f:all)if(!seen.contains(f.key)&&!(bus==Bus::Lin&&f.id>=60&&f.fields.isEmpty()))return reject("配置帧清单不完整");
    if(bus==Bus::Lin){
        QSet<QString> scheduleNames;for(const auto&v:root["schedules"].toArray()){
            const auto o=v.toObject();Schedule s;s.name=o["name"].toString();s.issue=o["issue"].toString();if(legacyDiagnosticIssue(s.issue))s.issue.clear();if(s.name.trimmed().isEmpty()||scheduleNames.contains(s.name)||!o["slots"].isArray())return reject("调度表名称为空、重复或缺少槽数组");scheduleNames.insert(s.name);
            for(const auto&sv:o["slots"].toArray()){const auto so=sv.toObject();ScheduleSlot entry{so["frame"].toString(),so["delayMs"].toString(),so["issue"].toString(),0};bool ok=false;const auto delay=entry.delayMs.toDouble(&ok);
                if(!ok||!std::isfinite(delay)||delay<=0)return reject("无效 delay");
                if(legacyDiagnosticIssue(entry.issue)){
                    for(const auto&f:all)if(f.id>=60&&(f.key==entry.frame||f.name==entry.frame)&&f.issue.isEmpty()){entry.frame=f.key;entry.issue.clear();break;}
                }
                if(!byKey.contains(entry.frame)){
                    // Preserve only a recognized unsupported source slot; configuration
                    // files cannot invent frame definitions or runnable commands.
                    bool known=false;for(const auto&original:next.database->schedules)for(const auto&slot:original.entries)if(slot.frame==entry.frame&&!slot.issue.isEmpty()){known=true;entry.issue=slot.issue;}
                    if(!known)return reject("调度引用未知帧："+entry.frame);
                }
                if(!entry.issue.isEmpty())s.issue=entry.issue;s.entries.append(entry);
            }work.schedules.append(s);
        }
        work.schedule=root["schedule"].toString();if(!work.schedule.isEmpty()&&!scheduleNames.contains(work.schedule))return reject("所选调度表不存在");
        if(!root["role"].isDouble()||root["role"].toDouble()!=root["role"].toInt(-1)||root["role"].toInt(-1)<0||root["role"].toInt()>2)return reject("未知 LIN 角色");
        work.role=LinRole(root["role"].toInt());work.node=root["node"].toString();
        if((work.role==LinRole::Master&&work.node!=next.database->master)||(work.role==LinRole::Slave&&(!next.database->nodes.contains(work.node)||work.node==next.database->master)))return reject("角色与实际节点不匹配");
    }else if(!root["schedules"].toArray().isEmpty())return reject("CAN 配置不能包含 LIN 调度表");
    if(bus==Bus::Can){
        if(root.contains("canNode")&&!root["canNode"].isString())return reject("无效 DBC 节点");
        work.canNode=root["canNode"].toString();work.canDirection=root.value("canDirection").toString("Tx");
        if((!work.canNode.isEmpty()&&!next.database->nodes.contains(work.canNode))||
           (root.contains("canDirection")&&!root["canDirection"].isString())||
           (work.canDirection!="Tx"&&work.canDirection!="Rx"&&work.canDirection!="Tx/Rx"))return reject("无效 DBC 节点或方向");
    }
    snapshot=next;return true;
}
}
