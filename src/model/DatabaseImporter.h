#pragma once
#include "domain/SignalTypes.h"
namespace host::signal {
class DatabaseImporter {
public:
    static ImportResult load(const QString &path,Bus);
    static ImportResult dbc(const QByteArray &,const QString &path=QString());
    static ImportResult ldf(const QByteArray &,const QString &path=QString());
    static bool prepare(const QByteArray &,const QString &,DatabaseDefinition &,QString &text,QString &error);
};
}
