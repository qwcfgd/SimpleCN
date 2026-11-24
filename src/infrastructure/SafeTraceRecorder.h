#pragma once
#include <QFile>
#include <QString>
#include <QByteArray>
#include <functional>
namespace host {
struct TraceTiming {
 qint64 driverTimestampUs=-1,hostReadEndUs=-1,sendCallUs=-1;
 QString timeoutKind,frameFormat; int timeoutLimitMs=-1; qint64 timeoutElapsedMs=-1;
 QString hardwareOperation; qint64 hardwareDurationUs=-1;
};
class SafeTraceRecorder {
public:
 bool open(const QString &root);
 bool open(QIODevice *device);
 bool sendIfHealthy(const std::function<bool()> &send) const { return m_healthy && send(); }
 bool append(const QString &direction,quint32 id,int dlc,const QByteArray &data,qint64 epochMs,qint64 captureUs,bool diagnosticId=true,const TraceTiming &timing=TraceTiming{});
 bool sendSignal(quint32 id,const QByteArray &data,bool extended,qint64 epochMs,qint64 captureUs,const std::function<bool()> &send);
 bool healthy() const { return m_healthy; }
 QString path() const { return m_file.fileName(); }
private:
 QFile m_file;
 QIODevice *m_device=nullptr;
 quint64 m_sequence=0;
 qint64 m_previousWriteUs=-1;
 bool m_healthy=false;
};
}
