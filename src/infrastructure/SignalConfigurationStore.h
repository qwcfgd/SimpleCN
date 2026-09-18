#pragma once
#include "domain/SignalTypes.h"
#include <QJsonObject>
namespace host::signal {
// Filesystem/serialization service; independent of Widgets, ViewModels and hardware.
class SignalConfigurationStore {
public:
    static QJsonObject serialize(const Database&,const WorkingSet&,const QString &directory={});
    static bool parse(const QJsonObject&,Bus,const QString &directory,ConfigurationSnapshot&,QString&);
    static bool read(const QString&,Bus,ConfigurationSnapshot&,QString&);
    static bool save(const QString&,const Database&,const WorkingSet&,QString&);
};
}
