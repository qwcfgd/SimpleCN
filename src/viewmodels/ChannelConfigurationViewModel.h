#pragma once
#include "domain/HostTypes.h"
#include <QObject>
namespace host {
class ChannelViewModel;
class ChannelConfigurationViewModel : public QObject {
    Q_OBJECT
public:
    explicit ChannelConfigurationViewModel(QString path,QObject*parent=nullptr):QObject(parent),m_path(std::move(path)){}
    bool save(const QVector<ChannelViewModel*>&,QString&error)const;
    bool load(const QString&,const QVector<ChannelViewModel*>&,QVector<ChannelSettings>&,QString&error)const;
    bool update(ChannelViewModel*,ChannelSettings,const QVector<ChannelViewModel*>&,QString&error)const;
private:QString m_path;
};
}
