#pragma once
#include "domain/HostTypes.h"
namespace host {
class TraceExporter {
public:
    static bool write(const QString &path,const FrameBatch &,QString &error,QString format={});
};
}
