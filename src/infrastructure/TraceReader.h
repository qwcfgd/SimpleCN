#pragma once
#include "domain/HostTypes.h"
#include <QMap>
namespace host {
struct TraceLog {FrameBatch frames;QStringList channels;QString error;int skipped=0;qint64 durationUs=0;};
class TraceReader {public:static TraceLog read(const QString&);static QString channelKey(signal::Bus,int);};
}
