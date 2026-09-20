#include "ChannelConfigurationViewModel.h"
#include "ChannelViewModel.h"
#include "infrastructure/SettingsStore.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
namespace host {
bool ChannelConfigurationViewModel::update(ChannelViewModel*target,ChannelSettings settings,const QVector<ChannelViewModel*>&channels,QString&error)const{
    error.clear();if(!target || !channels.contains(target)){error="通道已删除。";return false;}
    if(target->hardwareLocked()){error="请先断开当前通道并等待操作结束。";return false;}
    settings.softwareId=settings.softwareId.trimmed();
    for(auto*peer:channels)if(peer!=target){
        if(peer->settings().softwareId.compare(settings.softwareId,Qt::CaseInsensitive)==0){error="通道名称已存在，请使用其他名称。";return false;}
        if(peer->reservesHardware() && settings.handle && peer->settings().bus==settings.bus && peer->settings().simulation==settings.simulation && peer->settings().handle==settings.handle){error="所选硬件通道已被占用。";return false;}
    }
    if(!target->setSettings(settings)){error=target->error().isEmpty()?QString("无法应用通道配置，请等待当前操作结束。"):target->error();return false;}
    return true;
}
bool ChannelConfigurationViewModel::save(const QVector<ChannelViewModel*>&channels,QString&error)const{
    QVector<ChannelSettings> snapshots;for(const auto*vm:channels){if(vm->signalTransmission()->importing()){error="数据库正在导入，请稍后保存";return false;}snapshots.append(vm->snapshotSettings());}
    return SettingsStore(m_path).save(snapshots,error);
}
bool ChannelConfigurationViewModel::load(const QString&path,const QVector<ChannelViewModel*>&channels,QVector<ChannelSettings>&settings,QString&error)const{
    for(const auto*vm:channels)if(vm->hardwareLocked()){error="请先断开所有通道并等待操作结束。";return false;}
    if(!QFileInfo::exists(path)){error="未找到通道配置。";return false;}return SettingsStore(path).load(settings,error);
}
}

namespace host {
QByteArray ChannelConfigurationViewModel::snapshot(const QVector<ChannelViewModel*> &channels){
    QJsonArray data;for(const auto *vm:channels)data.append(vm->snapshotSettings().toJson());
    return QJsonDocument(data).toJson(QJsonDocument::Compact);
}
void ChannelConfigurationViewModel::markSaved(const QVector<ChannelViewModel*>&channels){m_savedProject=snapshot(channels);}
bool ChannelConfigurationViewModel::isDirty(const QVector<ChannelViewModel*>&channels)const{return m_savedProject!=snapshot(channels);}
}
