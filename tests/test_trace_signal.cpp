#include <QtTest>
#include <QBuffer>
#include <QTemporaryDir>
#include <QFile>
#include <QQueue>
#include <cstring>
#include "infrastructure/SignalTransmitter.h"
#include "infrastructure/SafeTraceRecorder.h"
#include "infrastructure/BusHardware.h"
#include "infrastructure/ChannelWorker.h"
#include "protocol/OperationCoordinator.h"
using namespace host;
class FakeCan final : public CanHardware {
public:
 int writes=0;bool echoEnabled=false;QVector<TPCANMsg> queue;
 communication::HardwareChannels scan(QString&)override{return {};}
 bool open(const communication::HardwareChannel&,const communication::SoftwareChannelConfiguration&,QString&)override{return true;}
 bool close(QString&)override{return true;}
 communication::Health health(QString&)override{return communication::Health::Ready;}
 bool enableEcho()override{echoEnabled=true;return true;}
 bool sendRaw(TPCANMsg msg)override{++writes;msg.MSGTYPE=TPCANMessageType(msg.MSGTYPE|PCAN_MESSAGE_ECHO);queue.append(msg);return true;}
 DWORD recvRaw(DWORD n,TPCANMsg*m,TPCANTimestamp*t)override{if(!n||queue.isEmpty())return 0;*m=queue.takeFirst();if(t){*t={};t->millis=123;}return 1;}
 unsigned long lastErrorCode()const override{return PCAN_ERROR_OK;}
 QString lastErrorText()const override{return "fake CAN error";}
};
class FailDevice final : public QIODevice {
public: bool fail=false;int calls=0;
protected:qint64 readData(char*,qint64)override{return -1;}qint64 writeData(const char*,qint64 n)override{++calls;return fail?-1:n;}
};
class PipelineCanFake final : public CanHardware {
public:
 HANDLE event=CreateEvent(nullptr,FALSE,FALSE,nullptr);bool opened=false;int attempts=0,failAt=0;unsigned long status=PCAN_ERROR_OK;QVector<TPCANMsg> sent;QQueue<TPCANMsg> incoming;
 ~PipelineCanFake()override{if(event)CloseHandle(event);}
 communication::HardwareChannels scan(QString &e)override{e.clear();communication::HardwareChannel h;h.bus=communication::Bus::Can;h.handle=0x51;h.key="can:pipeline:1";h.available=true;h.persistentIdentity=true;return {h};}
 bool open(const communication::HardwareChannel&,const communication::SoftwareChannelConfiguration&,QString &e)override{opened=true;e.clear();return true;}
 bool close(QString &e)override{opened=false;incoming.clear();e.clear();return true;}
 communication::Health health(QString &e)override{e.clear();return opened?communication::Health::Ready:communication::Health::Removed;}
 bool enableEcho()override{return true;}void *receiveEventHandle()const override{return event;}
 bool sendRaw(TPCANMsg m)override{++attempts;if(failAt==attempts){status=PCAN_ERROR_QXMTFULL;return false;}status=PCAN_ERROR_OK;sent.append(m);return true;}
 DWORD recvRaw(DWORD cap,TPCANMsg *out,TPCANTimestamp *time)override{DWORD n=0;while(n<cap&&!incoming.isEmpty()){out[n]=incoming.dequeue();if(time)time[n]={};++n;}return n;}
 unsigned long lastErrorCode()const override{return status;}QString lastErrorText()const override{return status==PCAN_ERROR_QXMTFULL?"fake transmit queue full":QString();}
 CanBusState classifyStatus(unsigned long)const override{return CanBusState::Ready;}CanBusState busState()override{return CanBusState::Ready;}
 void echo(int n){auto m=sent[n];m.MSGTYPE=BYTE(m.MSGTYPE|PCAN_MESSAGE_ECHO);incoming.enqueue(m);}
 void rx(const QByteArray &bytes){TPCANMsg m={};m.ID=0x7e8;m.LEN=8;std::memset(m.DATA,0xff,8);std::memcpy(m.DATA,bytes.constData(),size_t(bytes.size()));incoming.enqueue(m);}
 void deliver(){SetEvent(event);}
};
class TraceSignalTest:public QObject {
 Q_OBJECT
 static signal::TxPlan plan(){signal::TxPlan p;p.bus=signal::Bus::Can;p.run=1;p.periodic=false;p.repeatCount=1;p.items={{"CAN:STD:825",0x339,false,QByteArray::fromHex("aabbccddeeff0102"),10,1,{},false,true}};return p;}
 static void guard(SignalTransmitter &tx,SafeTraceRecorder &trace,FakeCan &hw){
  tx.guardedCanSend=[&](const signal::TxItem &item,QString &error){
   TPCANMsg msg={};msg.ID=item.id;msg.LEN=BYTE(item.payload.size());msg.MSGTYPE=item.extended?PCAN_MESSAGE_EXTENDED:PCAN_MESSAGE_STANDARD;std::memcpy(msg.DATA,item.payload.constData(),size_t(item.payload.size()));
   if(!trace.sendSignal(item.id,item.payload,item.extended,1,2,[&]{return hw.sendRaw(msg);})){error="Trace or CAN write failed";return false;}return true;
  };
 }
private slots:
 void workerPeerBlockAndSeparation_data(){
  QTest::addColumn<int>("bs");QTest::addColumn<int>("st");
  for(int bs:{1,2,3,4,5})QTest::newRow(qPrintable(QString("BS%1").arg(bs)))<<bs<<0;
  QTest::newRow("millisecond")<<0<<1;QTest::newRow("microsecond-100")<<0<<0xf1;QTest::newRow("microsecond-900")<<0<<0xf9;
 }
 void workerPeerBlockAndSeparation(){
  QFETCH(int,bs);QFETCH(int,st);QTemporaryDir temp;QVERIFY(temp.isValid());QFile image(temp.filePath("app.bin"));QVERIFY(image.open(QIODevice::WriteOnly));QCOMPARE(image.write(QByteArray(100,'b')),qint64(100));image.close();
  auto settings=ChannelSettings::defaults(communication::Bus::Can);settings.simulation=false;settings.hardwareKey="can:pipeline:1";settings.handle=0x51;settings.flashRequired=false;settings.applicationPath=image.fileName();
  boot::FlashProfile profile;profile.flow="boot";profile.keyProvider="seed-as-key";profile.simulationOnly=false;for(const auto &step:boot::downloadSteps())profile.stepEnabled[step.id]=false;
  profile.stepEnabled["app34"]=true;profile.stepEnabled["app36"]=true;settings.downloadProfile=profile.toJson();
  PipelineCanFake *fake=nullptr;ChannelWorker worker(settings,[&]()->std::unique_ptr<CanHardware>{auto p=std::make_unique<PipelineCanFake>();fake=p.get();return p;});
  worker.initialize();worker.connectChannel(settings);QVERIFY(fake&&fake->opened&&fake->event);worker.startPreview(settings);QTRY_COMPARE_WITH_TIMEOUT(fake->sent.size(),1,2000);
  fake->echo(0);fake->rx(QByteArray::fromHex("300000"));fake->deliver();QTRY_COMPARE_WITH_TIMEOUT(fake->sent.size(),2,2000);
  fake->echo(1);fake->rx(QByteArray::fromHex("0474200402"));fake->deliver();QTRY_COMPARE_WITH_TIMEOUT(fake->sent.size(),3,2000);
  fake->echo(2);fake->rx(QByteArray(1,char(0x30))+char(bs)+char(st));fake->deliver();const int first=st?1:qMin(bs,4);
  QTRY_COMPARE_WITH_TIMEOUT(fake->sent.size(),3+first,2000);QTest::qWait(20);QCOMPARE(fake->sent.size(),3+first);
  if(st){fake->echo(3);fake->deliver();QTRY_COMPARE_WITH_TIMEOUT(fake->sent.size(),5,2000);worker.cancelTask();worker.shutdown();return;}
  int already=0;if(bs>4){fake->echo(3);fake->deliver();already=1;QTRY_COMPARE_WITH_TIMEOUT(fake->sent.size(),3+bs,2000);}
  fake->rx(QByteArray::fromHex("300000"));fake->deliver();QTest::qWait(20);QCOMPARE(fake->sent.size(),3+bs);
  for(int i=3+already;i<3+bs;++i)fake->echo(i);fake->deliver();QTRY_VERIFY_WITH_TIMEOUT(fake->sent.size()>3+bs,2000);worker.cancelTask();worker.shutdown();
 }
 void workerAndTransportBoundDownloadWindow_data(){QTest::addColumn<int>("failAt");QTest::newRow("window and cancellation")<<0;QTest::newRow("queue full")<<6;QTest::newRow("early pending and final")<<-1;QTest::newRow("out of order echoes")<<-2;QTest::newRow("confirmation timeout")<<-3;}
 void workerAndTransportBoundDownloadWindow(){
  QFETCH(int,failAt);
  QTemporaryDir temp;QVERIFY(temp.isValid());QFile image(temp.filePath("app.bin"));QVERIFY(image.open(QIODevice::WriteOnly));QCOMPARE(image.write(QByteArray(100,'a')),qint64(100));image.close();
  auto settings=ChannelSettings::defaults(communication::Bus::Can);settings.simulation=false;settings.hardwareKey="can:pipeline:1";settings.handle=0x51;settings.flashRequired=false;settings.applicationPath=image.fileName();if(failAt==-3)settings.canNetwork["nAsMs"]=300;
  boot::FlashProfile profile;profile.flow="boot";profile.keyProvider="seed-as-key";profile.simulationOnly=false;for(const auto &step:boot::downloadSteps())profile.stepEnabled[step.id]=false;
  profile.stepEnabled["app34"]=true;profile.stepEnabled["app36"]=true;settings.downloadProfile=profile.toJson();
  PipelineCanFake *fake=nullptr;ChannelWorker worker(settings,[&]()->std::unique_ptr<CanHardware>{auto p=std::make_unique<PipelineCanFake>();fake=p.get();return p;});
  bool acknowledged=false;connect(&worker,&ChannelWorker::taskChanged,&worker,[&](TaskState,int,const QString &detail){if(detail.contains("TransferData step completed"))acknowledged=true;});
  worker.initialize();worker.connectChannel(settings);QVERIFY(fake&&fake->opened&&fake->event);fake->failAt=failAt;worker.startPreview(settings);
  QTRY_COMPARE_WITH_TIMEOUT(fake->sent.size(),1,2000);QCOMPARE(int(fake->sent[0].DATA[0]>>4),1);
  fake->echo(0);fake->rx(QByteArray::fromHex("300000"));fake->deliver();QTRY_COMPARE_WITH_TIMEOUT(fake->sent.size(),2,2000);
  fake->echo(1);fake->rx(QByteArray::fromHex("0474200402"));fake->deliver();QTRY_COMPARE_WITH_TIMEOUT(fake->sent.size(),3,2000);
  QCOMPARE(int(fake->sent[2].DATA[0]>>4),1);QCOMPARE(int(fake->sent[2].DATA[2]),0x36);
  fake->echo(2);fake->rx(QByteArray::fromHex("300000"));fake->deliver();
  if(failAt>0){QTRY_COMPARE_WITH_TIMEOUT(fake->attempts,failAt,2000);QCOMPARE(fake->sent.size(),5);for(int i=3;i<5;++i)fake->echo(i);fake->deliver();QTest::qWait(30);QCOMPARE(fake->attempts,failAt);QSignalSpy rejected(&worker,&ChannelWorker::diagnosticFinished);diag::Request request;request.raw=true;request.bytes=QByteArray::fromHex("1003");worker.sendDiagnostic(settings,request);QCOMPARE(rejected.size(),1);QVERIFY(rejected.last()[2].toString().contains("unresolved"));worker.shutdown();return;}
  QTRY_COMPARE_WITH_TIMEOUT(fake->sent.size(),7,2000);QTest::qWait(20);QCOMPARE(fake->sent.size(),7);
  if(failAt==-3){
   QTest::qWait(400);QCOMPARE(fake->sent.size(),7);QSignalSpy rejected(&worker,&ChannelWorker::diagnosticFinished);diag::Request request;request.raw=true;request.bytes=QByteArray::fromHex("1003");worker.sendDiagnostic(settings,request);
   QCOMPARE(rejected.size(),1);QVERIFY(rejected.last()[2].toString().contains("unresolved"));worker.shutdown();return;
  }
  if(failAt==-2){
   fake->echo(4);fake->deliver();QTest::qWait(30);QCOMPARE(fake->sent.size(),7);
   QSignalSpy rejected(&worker,&ChannelWorker::diagnosticFinished);diag::Request request;request.raw=true;request.bytes=QByteArray::fromHex("1003");worker.sendDiagnostic(settings,request);
   QCOMPARE(rejected.size(),1);QVERIFY(rejected.last()[2].toString().contains("unresolved"));worker.shutdown();return;
  }
  if(failAt==-1){
   int echoCursor=3;
   while(echoCursor<17){
    QTRY_VERIFY_WITH_TIMEOUT(fake->sent.size()>echoCursor,2000);const int end=fake->sent.size();QVERIFY(end-echoCursor<=4);QVERIFY(end<=17);
    if(end==17){fake->rx(QByteArray::fromHex("037f3678"));fake->rx(QByteArray::fromHex("027601"));fake->deliver();QTest::qWait(20);QVERIFY(!acknowledged);}
    for(int i=echoCursor;i<end;++i)fake->echo(i);fake->deliver();echoCursor=end;
   }
   QTRY_VERIFY_WITH_TIMEOUT(acknowledged,2000);worker.shutdown();return;
  }
  for(int i=3;i<7;++i)fake->echo(i);fake->deliver();QTRY_VERIFY_WITH_TIMEOUT(fake->sent.size()>7,2000);
  worker.cancelTask();const int stopped=fake->sent.size();for(int i=7;i<stopped;++i)fake->echo(i);fake->deliver();QTest::qWait(30);QCOMPARE(fake->sent.size(),stopped);worker.shutdown();
 }
 void normalSignalAndEchoAreRecordedWithoutPayload(){
  QBuffer out;QVERIFY(out.open(QIODevice::ReadWrite));SafeTraceRecorder trace;QVERIFY(trace.open(&out));FakeCan hw;SignalTransmitter tx;guard(tx,trace,hw);
  tx.start(plan(),500000,false,&hw,nullptr);QTRY_COMPARE(hw.writes,1);QVERIFY(hw.echoEnabled);
  TPCANMsg echo={};TPCANTimestamp stamp={};QCOMPARE(hw.recvRaw(1,&echo,&stamp),DWORD(1));QVERIFY(echo.MSGTYPE&PCAN_MESSAGE_ECHO);QCOMPARE(echo.ID,DWORD(0x339));QCOMPARE(echo.LEN,BYTE(8));
  TraceTiming timing;timing.frameFormat=echo.MSGTYPE&PCAN_MESSAGE_EXTENDED?"EXT":"STD";timing.driverTimestampUs=quint64(stamp.millis)*1000;
  QVERIFY(trace.append("TX echo",echo.ID,echo.LEN,QByteArray(reinterpret_cast<const char*>(echo.DATA),echo.LEN),3,4,false,timing));
  const auto rows=out.data().split('\n');QVERIFY(rows.size()>=4);const auto sent=rows[1].split(',');const auto received=rows[2].split(',');
  QCOMPARE(sent.size(),17);QCOMPARE(sent[3],QByteArray("TX request"));QCOMPARE(sent[4],QByteArray("0x339"));QCOMPARE(sent[5],QByteArray("8"));QCOMPARE(sent[6],QByteArray("** ** ** ** ** ** ** **"));QCOMPARE(sent[14],QByteArray("STD"));
  QCOMPARE(received[3],QByteArray("TX echo"));QCOMPARE(received[6],QByteArray("** ** ** ** ** ** ** **"));QCOMPARE(received[7],QByteArray("123000"));QCOMPARE(received[14],QByteArray("STD"));QVERIFY(!out.data().contains("AA"));QVERIFY(!out.data().contains("BB"));
 }
 void failedTraceStopsBeforeHardwareWrite(){
  FailDevice out;QVERIFY(out.open(QIODevice::WriteOnly));SafeTraceRecorder trace;QVERIFY(trace.open(&out));out.fail=true;FakeCan hw;SignalTransmitter tx;guard(tx,trace,hw);
  tx.start(plan(),500000,false,&hw,nullptr);QTest::qWait(30);QCOMPARE(hw.writes,0);QVERIFY(!trace.healthy());QCOMPARE(out.calls,2);
  int writes=0;QVERIFY(!trace.sendSignal(0x715,QByteArray::fromHex("042712aabbcccccc"),false,5,6,[&]{++writes;return true;}));QCOMPARE(writes,0);QCOMPARE(out.calls,2);
 }
 void failedTraceAfterFirstWriteBlocksLaterWrites(){
  FailDevice out;QVERIFY(out.open(QIODevice::WriteOnly));SafeTraceRecorder trace;QVERIFY(trace.open(&out));FakeCan hw;SignalTransmitter tx;guard(tx,trace,hw);
  auto p=plan();p.repeatCount=2;tx.start(p,500000,false,&hw,nullptr);QCOMPARE(hw.writes,1);out.fail=true;
  TPCANMsg echo={};TPCANTimestamp timestamp={};QCOMPARE(hw.recvRaw(1,&echo,&timestamp),DWORD(1));
  signal::BusFrameEvent confirmed;confirmed.id=echo.ID;confirmed.bytes=QByteArray(reinterpret_cast<const char*>(echo.DATA),echo.LEN);confirmed.source=signal::EventSource::HardwareEcho;tx.observe({confirmed});
  QTest::qWait(60);QCOMPARE(hw.writes,1);QVERIFY(!trace.healthy());QCOMPARE(out.calls,3);
 }
 void diagnosticLookingSignalIsFullyRedacted(){
  QBuffer out;QVERIFY(out.open(QIODevice::ReadWrite));SafeTraceRecorder trace;QVERIFY(trace.open(&out));int writes=0;
  QVERIFY(trace.sendSignal(0x715,QByteArray::fromHex("042712aabbcccccc"),false,1,2,[&]{++writes;return true;}));
  QVERIFY(trace.sendSignal(0x18daf110,QByteArray::fromHex("042712aabbcccccc"),true,3,4,[&]{++writes;return true;}));QCOMPARE(writes,2);
  const auto rows=out.data().split('\n');const auto standard=rows[1].split(',');const auto extended=rows[2].split(',');
  QCOMPARE(standard[6],QByteArray("** ** ** ** ** ** ** **"));QCOMPARE(extended[6],QByteArray("** ** ** ** ** ** ** **"));QCOMPARE(standard[14],QByteArray("STD"));QCOMPARE(extended[14],QByteArray("EXT"));
 }
 void signalAndDownloadRemainExclusive(){OperationCoordinator c;QVERIFY(c.acquire(OperationCoordinator::Task::Download));QVERIFY(!c.permits(OperationCoordinator::Task::Signal));c.release(OperationCoordinator::Task::Download);QVERIFY(c.acquire(OperationCoordinator::Task::Signal));QVERIFY(!c.permits(OperationCoordinator::Task::Download));}
};
QTEST_GUILESS_MAIN(TraceSignalTest)
#include "test_trace_signal.moc"
