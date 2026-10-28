#pragma once
#include <QFile>
#include <QString>
#include <QByteArray>
#include <functional>
namespace host {
class SafeTraceRecorder {
public:
 bool open(const QString &root);
 bool open(QIODevice *device);
 bool sendIfHealthy(const std::function<bool()> &send) const { return m_healthy && send(); }
 bool append(const QString &direction,quint32 id,int dlc,const QByteArray &data,qint64 epochMs,qint64 captureUs,bool diagnosticId=true);
 bool healthy() const { return m_healthy; }
 QString path() const { return m_file.fileName(); }
private:
 QFile m_file;
 QIODevice *m_device=nullptr;
 quint64 m_sequence=0;
 bool m_healthy=false;
};
}
