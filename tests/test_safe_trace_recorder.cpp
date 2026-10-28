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
public: bool fail=false;
protected: qint64 readData(char *,qint64) override {return -1;}
 qint64 writeData(const char *,qint64 n) override {return fail?-1:n;}
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
 assert(!failed.append("RX",1,1,QByteArray::fromHex("00"),0,0)); int writes=0; assert(!failed.sendIfHealthy([&]{++writes;return true;})); assert(writes==0);
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
 const auto rows=buffer.data().split('\n'); assert(rows.size()>=6);
 assert(rows[1].endsWith(",** ** ** ** ** ** ** **"));
 assert(rows[2].endsWith(",03 7F 27 22 ** ** ** **"));
 assert(rows[3].endsWith(",30 00 05 ** ** ** ** **"));
 assert(rows[4].endsWith(",02 50 03 ** ** ** ** **"));
 assert(rows[5].endsWith(",** ** ** ** ** ** ** **"));
 assert(rows[6].endsWith(",31 00 F1 ** ** ** ** **"));
 assert(rows[7].endsWith(",32 00 F9 ** ** ** ** **"));
 assert(rows[8].endsWith(",** ** ** ** ** ** ** **"));
 assert(rows[9].endsWith(",** ** ** **"));
 assert(rows[10].endsWith(",** ** ** ** ** ** ** **"));
 assert(rows[11].endsWith(",** ** ** **"));
 assert(rows[12].endsWith(",10 08 50 03 01 02 03 04"));
 assert(rows[13].endsWith(",07 50 01 02 03 04 05 06"));
 assert(rows[14].endsWith(",** ** ** ** ** ** ** **"));
 assert(rows[15].endsWith(",** ** ** ** ** ** ** **"));
 assert(rows[16].endsWith(",** ** ** ** ** ** ** **"));
 host::SafeTraceRecorder r2; assert(r2.open(dir.path())); assert(r2.path()!=r.path());
 host::SafeTraceRecorder bad; assert(!bad.open(dir.path()+"/missing/child")); assert(!bad.append("RX",1,0,{},0,0));
}
