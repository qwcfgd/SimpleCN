#include "SafeTraceRecorder.h"
#include <QDir>
#include <QUuid>
#include <QStringList>
#include <QElapsedTimer>
namespace host {
bool SafeTraceRecorder::open(const QString &root){
 m_healthy=false;
 QDir parent(root); if(!parent.exists())return false;
 const QString name="run-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
 if(!parent.mkdir(name))return false;
 m_file.setFileName(parent.filePath(name+"/frames.csv"));
 if(!m_file.open(QIODevice::WriteOnly|QIODevice::NewOnly|QIODevice::Unbuffered))return false;
 return open(&m_file);
}
bool SafeTraceRecorder::open(QIODevice *device){
 m_device=device; m_sequence=0; m_previousWriteUs=-1; m_healthy=false;
 if(!device || !device->isOpen() || !device->isWritable())return false;
 const QByteArray header="sequence,host_epoch_ms,host_capture_us,direction,can_id,dlc,data,pcan_timestamp_us,host_read_end_us,send_call_us,previous_log_write_us,timeout_kind,timeout_limit_ms,timeout_elapsed_ms,can_frame_flags,hardware_operation,hardware_duration_us\n";
 m_healthy=m_device->write(header)==header.size() && (m_device!=&m_file || m_file.flush()); return m_healthy;
}
bool SafeTraceRecorder::append(const QString &direction,quint32 id,int dlc,const QByteArray &data,qint64 epochMs,qint64 captureUs,bool diagnosticId,const TraceTiming &timing){
 if(!m_healthy)return false;
 QStringList bytes; const int n=data.size();
 int keep=0;
 if(n && n<=8 && dlc==n && diagnosticId){
  const int kind=quint8(data[0])>>4;
  if(kind==2)keep=1; // Orphan and ordered CF are always redacted.
  else if(kind==3 && n>=3){
   const int fs=quint8(data[0])&15, st=quint8(data[2]);
   if(fs<=2 && (st<=0x7f || (st>=0xf1 && st<=0xf9)))keep=3;
  }
  else if(kind==0 && n>=2 && (quint8(data[0])&15)>=1 && (quint8(data[0])&15)<=7 && (quint8(data[0])&15)<=n-1){
   const int sid=quint8(data[1]); const int used=1+(quint8(data[0])&15);
   keep=(sid==0x27||sid==0x67)?qMin(3,used):used;
  }
  else if(kind==1 && n==8 && (((quint8(data[0])&15)<<8)|quint8(data[1]))>=8){const int sid=quint8(data[2]); keep=(sid==0x27||sid==0x67)?4:n;}
 }
 for(int i=0;i<n;++i)bytes<<(i<keep?QString("%1").arg(quint8(data[i]),2,16,QChar('0')).toUpper():QString("**"));

 const QByteArray line=QString("%1,%2,%3,%4,0x%5,%6,%7,%8,%9,%10,%11,%12,%13,%14,%15,%16,%17\n").arg(++m_sequence).arg(epochMs).arg(captureUs).arg(direction).arg(id,0,16).arg(dlc).arg(bytes.join(' ')).arg(timing.driverTimestampUs).arg(timing.hostReadEndUs).arg(timing.sendCallUs).arg(m_previousWriteUs).arg(timing.timeoutKind).arg(timing.timeoutLimitMs).arg(timing.timeoutElapsedMs).arg(timing.frameFormat).arg(timing.hardwareOperation).arg(timing.hardwareDurationUs).toUtf8();
 QElapsedTimer writeTimer;writeTimer.start();
 m_healthy=m_device->write(line)==line.size() && (m_device!=&m_file || m_file.flush());
 m_previousWriteUs=writeTimer.nsecsElapsed()/1000;return m_healthy;
}
bool SafeTraceRecorder::sendSignal(quint32 id,const QByteArray &data,bool extended,qint64 epochMs,qint64 captureUs,const std::function<bool()> &send){
 TraceTiming timing;timing.frameFormat=extended?"EXT":"STD";
 return append("TX request",id,data.size(),data,epochMs,captureUs,false,timing) && sendIfHealthy(send);
}
}
