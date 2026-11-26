#pragma once
#include <QHash>
#include <QSet>
#include <QStringList>

namespace host::signal {
// IDs assigned here exist only in dbcppp input. Wire IDs and original names are
// restored before constructing the public database.
struct DbcMessageScope {
    quint64 wireId = 0;
    QString suffix;
    int line = 0;
    QHash<QString, QString> signalNames;
    QString issue;
};
class DbcDefinitionScope {
public:
    static QString structuralText(QString text);
    QString prepare(const QString &input, QStringList &notes);
    QHash<quint64, DbcMessageScope> messages;
};
}
