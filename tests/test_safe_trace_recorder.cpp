#include "infrastructure/SafeTraceRecorder.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QBuffer>
#include <QFile>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
class FailingDevice : public QIODevice {
public: bool fail=false; int calls=0;
protected: qint64 readData(char *,qint64) override {return -1;}
 qint64 writeData(const char *,qint64 n) override {++calls;return fail?-1:n;}
};
int main(int argc,char **argv){
 QCoreApplication app(argc,argv); QTemporaryDir dir(qEnvironmentVariable("QT_TRACE_TEST_ROOT",QDir::tempPath())+"/trace-XXXXXX"); assert(dir.isValid());
 host::SafeTraceRecorder r; assert(r.open(dir.path()));
 assert(r.append("TX request",0x715,8,QByteArray::fromHex("042712aabbcccccc"),1,2));
 assert(r.append("TX echo",0x715,8,QByteArray::fromHex("100a2712aabbccdd"),3,4));
 assert(r.append("TX echo",0x715,8,QByteArray::fromHex("21aabbccddeeff00"),5,6));
 assert(r.append("RX",0x795,8,QByteArray::fromHex("037f2722cccccccc"),7,8));
 assert(r.append("RX",0x795,8,QByteArray::fromHex("300000cccccccccc"),9,10));
 assert(r.append("RX",0x795,8,QByteArray::fromHex("046711aabbcccccc"),11,12));
 assert(r.append("RX",0x795,8,QByteArray::fromHex("10096711aabbccdd"),13,14));
 assert(r.append("RX",0x795,8,QByteArray::fromHex("2faabbccddeeff00"),15,16));
 assert(r.append("RX",0x123,8,QByteArray::fromHex("03aabbccddeeff00"),17,18,false));
 assert(r.append("RX",0x795,8,QByteArray::fromHex("1000aabbccddeeff"),19,20));
 assert(r.append("RX",0x795,8,QByteArray::fromHex("00aabbccddeeff00"),21,22));
 QFile f(r.path()); assert(f.open(QIODevice::ReadOnly)); auto b=f.readAll();
 assert(!b.contains("AA") && !b.contains("BB")); assert(b.contains("7F 27 22")); assert(b.contains("30 00 00")); assert(b.contains("21 ** **"));
 FailingDevice dev; assert(dev.open(QIODevice::WriteOnly|QIODevice::Unbuffered)); host::SafeTraceRecorder failed; assert(failed.open(&dev)); dev.fail=true;
 assert(!failed.append("RX",1,1,QByteArray::fromHex("00"),0,0)); assert(dev.calls==2); int writes=0; assert(!failed.sendIfHealthy([&]{++writes;return true;})); assert(writes==0);
 QBuffer buffer; assert(buffer.open(QIODevice::ReadWrite)); host::SafeTraceRecorder review; assert(review.open(&buffer));
 assert(review.append("RX",0x795,8,QByteArray::fromHex("3faabbccddeeff00"),1,2));
 assert(review.append("RX",0x795,8,QByteArray::fromHex("037f2722aabbccdd"),3,4));
 assert(review.append("RX",0x795,8,QByteArray::fromHex("300005aabbccddee"),5,6));
 assert(review.append("RX",0x795,8,QByteArray::fromHex("025003aabbccddee"),7,8));
 assert(review.append("RX",0x795,8,QByteArray::fromHex("300080aabbccddee"),9,10));
 assert(review.append("RX",0x795,8,QByteArray::fromHex("3100f1aabbccddee"),11,12));
 assert(review.append("RX",0x795,8,QByteArray::fromHex("3200f9aabbccddee"),13,14));
 assert(review.append("RX",0x795,8,QByteArray::fromHex("3000faaabbccddee"),15,16));
 assert(review.append("RX",0x795,4,QByteArray::fromHex("057f2722"),17,18));
 assert(review.append("RX",0x795,8,QByteArray::fromHex("10075003aabbccdd"),19,20));
 assert(review.append("RX",0x795,4,QByteArray::fromHex("10095003"),21,22));
 assert(review.append("RX",0x795,8,QByteArray::fromHex("1008500301020304"),23,24));
 assert(review.append("RX",0x795,8,QByteArray::fromHex("0750010203040506"),25,26));
 assert(review.append("RX",0x795,8,QByteArray::fromHex("0850010203040506"),27,28));
 assert(review.append("RX",0x795,7,QByteArray::fromHex("1008500301020304"),29,30));
 assert(review.append("RX",0x795,8,QByteArray::fromHex("40aabbccddeeff00"),31,32));
 host::TraceTiming timing;timing.driverTimestampUs=123;timing.hostReadEndUs=456;timing.sendCallUs=7;timing.hardwareOperation="CAN_READ_BATCH";timing.hardwareDurationUs=42;
 assert(review.append("TX echo",0x715,8,QByteArray::fromHex("042712aabbcccccc"),31,32,true,timing));
 host::TraceTiming deadline;deadline.timeoutKind="CAN_N_AS";deadline.timeoutLimitMs=70;deadline.timeoutElapsedMs=79;
 assert(review.append("TIMEOUT",0,0,{},33,34,false,deadline));
 host::TraceTiming scan;scan.hardwareOperation="CAN_ENUMERATE";scan.hardwareDurationUs=123456;
 assert(review.append("PROFILE",0,0,{},35,36,false,scan));
 const auto rows=buffer.data().split('\n'); assert(rows.size()>=20);
 assert(rows[0].startsWith("sequence,host_epoch_ms,host_capture_us,direction,can_id,dlc,data,"));
 assert(rows[0].contains(",hardware_operation,hardware_duration_us"));
 auto observed=rows[17].split(',');assert(observed.size()==17&&observed[6]=="04 27 12 ** ** ** ** **"&&observed[7]=="123"&&observed[8]=="456"&&observed[9]=="7"&&observed[10].toLongLong()>=0&&observed[15]=="CAN_READ_BATCH"&&observed[16]=="42");
 auto timeout=rows[18].split(',');assert(timeout.size()==17&&timeout[11]=="CAN_N_AS"&&timeout[12]=="70"&&timeout[13]=="79");
 auto profile=rows[19].split(',');assert(profile.size()==17&&profile[3]=="PROFILE"&&profile[6].isEmpty()&&profile[15]=="CAN_ENUMERATE"&&profile[16]=="123456");
 assert(rows[1].split(',').at(6)=="** ** ** ** ** ** ** **");
 assert(rows[2].split(',').at(6)=="03 7F 27 22 ** ** ** **");
 assert(rows[3].split(',').at(6)=="30 00 05 ** ** ** ** **");
 assert(rows[4].split(',').at(6)=="02 50 03 ** ** ** ** **");
 assert(rows[5].split(',').at(6)=="** ** ** ** ** ** ** **");
 assert(rows[6].split(',').at(6)=="31 00 F1 ** ** ** ** **");
 assert(rows[7].split(',').at(6)=="32 00 F9 ** ** ** ** **");
 assert(rows[8].split(',').at(6)=="** ** ** ** ** ** ** **");
 assert(rows[9].split(',').at(6)=="** ** ** **");
 assert(rows[10].split(',').at(6)=="** ** ** ** ** ** ** **");
 assert(rows[11].split(',').at(6)=="** ** ** **");
 assert(rows[12].split(',').at(6)=="10 08 50 03 01 02 03 04");
 assert(rows[13].split(',').at(6)=="07 50 01 02 03 04 05 06");
 assert(rows[14].split(',').at(6)=="** ** ** ** ** ** ** **");
 assert(rows[15].split(',').at(6)=="** ** ** ** ** ** ** **");
 assert(rows[16].split(',').at(6)=="** ** ** ** ** ** ** **");
 host::SafeTraceRecorder r2; assert(r2.open(dir.path())); assert(r2.path()!=r.path());
 host::SafeTraceRecorder bad; assert(!bad.open(dir.path()+"/missing/child")); assert(!bad.append("RX",1,0,{},0,0));
}
