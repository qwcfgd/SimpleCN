#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QJsonArray>
#include "model/DatabaseImporter.h"
#include "model/SignalCodec.h"
#include "infrastructure/SignalConfigurationStore.h"
#include "protocol/CanTxScheduler.h"
#include "protocol/LinScheduleRunner.h"
#include "protocol/OperationCoordinator.h"
using namespace host::signal;
class RecordingLin final:public LinDevice {
public:
    QStringList calls;QSet<QString> publishers;bool reached=false,failInstall=false,failUpdate=false;QString current;
    bool configure(LinRole role,int,QString&)override{calls<<QString("role:%1").arg(int(role));return true;}
    bool stop(QString&)override{calls<<"stop";publishers.clear();return true;}
    bool install(const QVector<TxItem>&,const QSet<QString>&set,QString&e)override{calls<<"install";if(failInstall){e="injected install failure";return false;}publishers=set;return true;}
    bool start(const Schedule&s,const QVector<TxItem>&,QString&)override{calls<<"start:"+s.name;current=s.name;return true;}
    bool requestBoundary(QString&)override{calls<<"requestBoundary";return true;}
    bool boundary(bool&b,QString&)override{calls<<"boundary";b=reached;return true;}
    bool update(const TxItem&,QString&e)override{calls<<"update";if(failUpdate){e="injected update failure";return false;}return true;}
};
class SignalCodecTest:public QObject {
    Q_OBJECT
    QString fixture(const char*ext)const{return QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic."+ext;}
    QByteArray bytes(const char*ext)const{QFile f(fixture(ext));if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
    Database dbc()const{return DatabaseImporter::load(fixture("dbc"),Bus::Can).database;}
    Database ldf()const{return DatabaseImporter::load(fixture("ldf"),Bus::Lin).database;}
    static const FrameDefinition&find(const Database&db,const QString&name){for(const auto&f:db->frames)if(f.name==name)return f;return db->frames.first();}
    TxPlan linPlan(LinRole role=LinRole::Master)const{const auto db=ldf();TxPlan plan;plan.bus=Bus::Lin;plan.role=role;plan.node=role==LinRole::Slave?"Sensor":"Tester";plan.schedules=db->schedules;plan.schedule="Main";plan.run=4;
        for(const auto&f:db->frames){TxDraft draft;QString e;SignalCodec::initialize(f,Bus::Lin,draft,e);plan.items.append({f.key,f.id,false,draft.applied.bytes,0,1,f.publisher,f.classicChecksum});}return plan;}
private slots:
    void dbcFullGrammarAndInitialization(){const auto result=DatabaseImporter::load(fixture("dbc"),Bus::Can);QVERIFY2(result.database,qPrintable(result.error));const auto db=result.database;
        QCOMPARE(db->frames.size(),5);QVERIFY(db->nodes.contains("Tester"));const auto&f=find(db,"Packed");QString error;TxDraft draft;QVERIFY2(SignalCodec::initialize(f,Bus::Can,draft,error),qPrintable(error));QCOMPARE(draft.applied.bytes,QByteArray::fromHex("020cff0ffff00000"));
        QCOMPARE(SignalCodec::defaults(f.fields[0]).bits,quint64(7));QCOMPARE(SignalCodec::rawText(f.fields[2],SignalCodec::defaults(f.fields[2])),QString("-1"));
        QCOMPARE(f.cycleMs,10);QCOMPARE(find(db,"ExtendedSmall").id,quint32(291));QVERIFY(find(db,"ExtendedSmall").extended);QVERIFY(!find(db,"FloatUnsupported").issue.isEmpty());
    }
    void exactDecimalAndOverflow(){const auto db=dbc();QVERIFY(db);const auto&s=find(db,"Packed").fields[1];
        auto r=SignalCodec::parsePhysical(s,"1.2");QVERIFY(r.ok());QCOMPARE(r.raw.bits,quint64(12));QVERIFY(r.warning.isEmpty());
        r=SignalCodec::parsePhysical(s,"1.25");QVERIFY(r.ok());QCOMPARE(r.raw.bits,quint64(12));QCOMPARE(r.actual,QString("1.2"));QVERIFY(r.warning.contains("量化"));
        r=SignalCodec::parseRaw(s,"200");QVERIFY(r.ok());QVERIFY(r.warning.contains("min/max"));QVERIFY(!SignalCodec::parseRaw(s,"300").ok());QVERIFY(!SignalCodec::parsePhysical(s,"25.65").ok());QVERIFY(!SignalCodec::parseRaw(s,"1.2").ok());
        const auto&sign=find(db,"Packed").fields[2];r=SignalCodec::parsePhysical(sign,"-1.25");QVERIFY(r.ok());QCOMPARE(SignalCodec::rawText(sign,r.raw),QString("-12"));QCOMPARE(r.actual,QString("-1.2"));
        r=SignalCodec::parseRaw(sign,"0xFFF");QVERIFY(r.ok());QCOMPARE(SignalCodec::rawText(sign,r.raw),QString("-1"));QVERIFY(!SignalCodec::parseRaw(sign,"4095").ok());
    }
    void integer64NeverUsesDouble(){const auto db=dbc();QVERIFY(db);const auto&s=find(db,"Full64").fields[0];
        const auto r=SignalCodec::parseRaw(s,"18446744073709551615");QVERIFY(r.ok());QCOMPARE(r.raw.bits,~quint64(0));QCOMPARE(SignalCodec::physicalText(s,r.raw),QString("18446744073709551615"));
        QVERIFY(!SignalCodec::parseRaw(s,"18446744073709551616").ok());const auto p=SignalCodec::parsePhysical(s,"18446744073709551615");QVERIFY2(p.ok(),qPrintable(p.error));QCOMPARE(p.raw.bits,~quint64(0));
        SignalDefinition signed64=s;signed64.isSigned=true;signed64.minimum="-9223372036854775808";signed64.maximum="9223372036854775807";
        auto min=SignalCodec::parseRaw(signed64,"-9223372036854775808");QVERIFY(min.ok());QCOMPARE(min.raw.bits,quint64(1)<<63);QVERIFY(!SignalCodec::parseRaw(signed64,"9223372036854775808").ok());
    }
    void motorolaAndCrossByteKnownVectors(){FrameDefinition f;f.length=3;SignalDefinition s;s.start=7;s.width=12;s.littleEndian=false;f.fields.append(s);QByteArray bytes(3,0);QString error;
        QVERIFY(SignalCodec::encode(f,{{0xabc,{}}},bytes,error));QCOMPARE(bytes,QByteArray::fromHex("abc000"));QVector<RawValue> values;QVERIFY(SignalCodec::decode(f,bytes,values,error));QCOMPARE(values[0].bits,quint64(0xabc));
        f.fields[0].littleEndian=true;f.fields[0].start=5;bytes.fill(0);QVERIFY(SignalCodec::encode(f,{{0xabc,{}}},bytes,error));QCOMPARE(bytes,QByteArray::fromHex("805701"));QVERIFY(!SignalCodec::decode(f,QByteArray(1,0),values,error));
    }
    void muxKeepsInactiveCopies(){auto db=dbc();QVERIFY(db);const auto&f=find(db,"Multiplexed");QString error;TxDraft d;QVERIFY(SignalCodec::initialize(f,Bus::Can,d,error));QCOMPARE(d.applied.bytes,QByteArray::fromHex("001200"));
        d.values[0].bits=1;QVERIFY(SignalCodec::encode(f,d.values,d.applied.bytes,error));QCOMPARE(d.applied.bytes,QByteArray::fromHex("013412"));QCOMPARE(d.values[1].bits,quint64(18));
        auto values=d.values;QVERIFY(SignalCodec::decode(f,QByteArray::fromHex("014455"),values,error));QCOMPARE(values[1].bits,quint64(18));QCOMPARE(values[2].bits,quint64(0x5544));
    }
    void ldfScalarArraysSegmentsAndChecksum(){auto result=DatabaseImporter::load(fixture("ldf"),Bus::Lin);QVERIFY2(result.database,qPrintable(result.error));const auto db=result.database;QCOMPARE(db->frames.size(),4);QCOMPARE(db->bitrate,19200);
        QString error;TxDraft draft;QVERIFY(SignalCodec::initialize(find(db,"Control"),Bus::Lin,draft,error));QCOMPARE(draft.applied.bytes,QByteArray::fromHex("faff"));
        const auto&blob=find(db,"Blob");QVERIFY(blob.classicChecksum);QVERIFY(SignalCodec::initialize(blob,Bus::Lin,draft,error));QCOMPARE(draft.applied.bytes,QByteArray::fromHex("1234ff"));QCOMPARE(SignalCodec::defaults(blob.fields[0]).bytes,QByteArray::fromHex("ffff"));
        const auto&s=find(db,"Measurement").fields[0];QVERIFY(!SignalCodec::parsePhysical(s,"1.2").ok());auto inverse=SignalCodec::parsePhysical(s,"1.2",0);QVERIFY(inverse.ok());QCOMPARE(inverse.raw.bits,quint64(12));
        inverse=SignalCodec::parsePhysical(s,"1.2",1);QVERIFY(inverse.ok());QCOMPARE(inverse.raw.bits,quint64(112));
        QCOMPARE(SignalCodec::linPid(0x3c),quint8(0x3c));QCOMPARE(SignalCodec::linPid(0x3d),quint8(0x7d));
    }
    void ldfUnsupportedAndMalformedAreExplicit(){auto text=bytes("ldf");auto result=DatabaseImporter::ldf(text);QVERIFY(result.database);
        const auto&db=result.database;QVERIFY(SignalCodec::validateSchedule(db->schedules[0],db->frames,19200).isEmpty());QVERIFY(!SignalCodec::validateSchedule(db->schedules[2],db->frames,19200).isEmpty());QVERIFY(!db->schedules[3].issue.isEmpty());
        QVERIFY(!SignalCodec::validateSchedule(db->schedules[0],db->frames,1000).isEmpty());
        text.replace("Position, 0;","Missing, 0;");QVERIFY(!DatabaseImporter::ldf(text).database);
        text=bytes("ldf");text.replace("Command: 3, 2,","Command: 3, 8,");QVERIFY(!DatabaseImporter::ldf(text).database);
        text=bytes("ldf");text+="Unknown_semantics { a; }";result=DatabaseImporter::ldf(text);QVERIFY(!result.database);QVERIFY(result.error.contains("行"));
        text=bytes("ldf");text.chop(5);QVERIFY(!DatabaseImporter::ldf(text).database);
        text=bytes("ldf");text.replace("logical_value, 0, \"Off\";","ascii_value;");result=DatabaseImporter::ldf(text);QVERIFY(result.database);QVERIFY(!find(result.database,"Control").issue.isEmpty());
    }
    void sourceUtf8AndMalformedDbc(){QVERIFY(!DatabaseImporter::dbc("BO_ invalid").database);QByteArray bad=bytes("dbc");bad.append(char(0xff));QVERIFY(!DatabaseImporter::dbc(bad).database);
        const auto source=bytes("dbc");const auto digest=QCryptographicHash::hash(source,QCryptographicHash::Sha256);auto db=dbc();QVERIFY(db);QCOMPARE(db->sha256,QString::fromLatin1(digest.toHex()));QCOMPARE(bytes("dbc"),source);
    }
    void deadlinesDropMissesAndRespectBudget(){CanTxScheduler scheduler;QVector<TxItem>items;for(int i=0;i<1000;++i)items.append({QString::number(i),quint32(i),false,QByteArray(1,0),1,1,{},{}});
        scheduler.start(items,0);int sends=0;auto send=[&](const TxItem&,QString&){++sends;return true;};QCOMPARE(scheduler.pump(0,send,100),100);QCOMPARE(sends,100);
        QCOMPARE(scheduler.pump(10000,send,1000),1000);QCOMPARE(sends,1100);QCOMPARE(scheduler.statistics().missed.value("0"),quint64(9));QCOMPARE(scheduler.statistics().missed.value("999"),quint64(10));
        QCOMPARE(scheduler.pump(10000,send),0);scheduler.stop();QCOMPARE(scheduler.pump(100000,send),0);
    }
    void schedulerUpdatesWholeFramesAndStopsFailedItem(){CanTxScheduler scheduler;scheduler.start({{"a",1,false,QByteArray::fromHex("1122"),1,1,{},false},{"b",2,false,QByteArray(1,0),2,1,{},false}},0);
        scheduler.update("a",{QByteArray::fromHex("3344"),2});scheduler.update("a",{QByteArray::fromHex("5566"),1});QByteArray actual;
        scheduler.pump(0,[&](const TxItem&i,QString&e){if(i.key=="b"){e="write failed";return false;}actual=i.payload;return true;});QCOMPARE(actual,QByteArray::fromHex("3344"));QVERIFY(scheduler.running());QVERIFY(scheduler.statistics().failures.contains("b"));
    }
    void masterSwitchRequiresHardwareBoundary(){RecordingLin device;LinScheduleRunner runner(&device);QString error;auto plan=linPlan();QVERIFY(runner.start(plan,19200,0,error));
        QVERIFY(runner.switchTo("Alternate",100,error));QVERIFY(runner.status().state==RunState::SwitchPending);QVERIFY(runner.tick(9999999,error));QCOMPARE(device.current,QString("Main"));
        device.reached=true;QVERIFY(runner.tick(10000000,error));QCOMPARE(device.current,QString("Alternate"));QVERIFY(runner.status().state==RunState::Running);QCOMPARE(device.calls.last(),QString("start:Alternate"));
        QVERIFY(runner.switchTo("Main",10000001,error));QVERIFY(runner.stop(error));QVERIFY(runner.tick(10000002,error));QVERIFY(runner.status().state==RunState::Stopped);
    }
    void simulatedMasterFinishesLastSlotDelay(){LinScheduleRunner runner;QString error;auto plan=linPlan();QVector<quint32>events;runner.event=[&](const BusFrameEvent&e){events.append(e.id);};
        QVERIFY(runner.start(plan,19200,0,error));QVERIFY(runner.tick(0,error));QCOMPARE(events,QVector<quint32>{0x10});QVERIFY(runner.switchTo("Alternate",1,error));
        QVERIFY(runner.tick(10000,error));QCOMPARE(events.last(),quint32(0x11));QCOMPARE(runner.status().current,QString("Main"));QVERIFY(runner.tick(19999,error));QCOMPARE(events.size(),2);
        QVERIFY(runner.tick(20000,error));QCOMPARE(runner.status().current,QString("Alternate"));QCOMPARE(events.last(),quint32(0x12));
    }
    void hardwareSwitchDrainsOldEventsAndMeasuresInterval(){RecordingLin device;LinScheduleRunner runner(&device);QString error;auto plan=linPlan();QStringList notes;runner.notice=[&](const QString&s){notes.append(s);};
        QVERIFY(runner.start(plan,19200,0,error));BusFrameEvent old;old.bus=Bus::Lin;old.id=0x10;old.source=EventSource::HardwareEcho;old.hardwareUs=1000;runner.observe(old);QCOMPARE(runner.status().sent.value(frameKey(Bus::Lin,0x10)),quint64(1));
        QVERIFY(runner.switchTo("Alternate",1,error));device.reached=true;bool drained=false;runner.drainBeforeSwitch=[&]{return drained;};QVERIFY(runner.tick(2,error));QCOMPARE(device.current,QString("Main"));
        old.id=0x11;old.source=EventSource::Received;old.hardwareUs=11000;runner.observe(old);drained=true;QVERIFY(runner.tick(3,error));QCOMPARE(device.current,QString("Alternate"));
        BusFrameEvent first=old;first.id=0x12;first.hardwareUs=25000;runner.observe(first);QVERIFY(notes.last().contains("14.000 ms"));QVERIFY(notes.last().contains("硬件时间戳"));
    }
    void slaveWhitelistSwitchAndMonitor(){LinScheduleRunner sim;QString error;auto plan=linPlan(LinRole::Slave);QVERIFY(sim.start(plan,19200,0,error));QVERIFY(!sim.simulatedHeader(0x11).isEmpty());QVERIFY(sim.simulatedHeader(0x12).isEmpty());QVERIFY(sim.simulatedHeader(0x10).isEmpty());
        QVERIFY(sim.switchTo("Alternate",10,error));QVERIFY(sim.simulatedHeader(0x11).isEmpty());QVERIFY(!sim.simulatedHeader(0x12).isEmpty());QVERIFY(sim.stop(error));QVERIFY(sim.simulatedHeader(0x12).isEmpty());
        RecordingLin device;LinScheduleRunner runner(&device);QVERIFY(runner.start(plan,19200,0,error));device.calls.clear();QVERIFY(runner.switchTo("Alternate",100,error));QCOMPARE(device.calls,QStringList({"stop","install"}));QCOMPARE(device.publishers,QSet<QString>{frameKey(Bus::Lin,0x12)});
        device.failInstall=true;QVERIFY(!runner.switchTo("Main",200,error));QVERIFY(device.publishers.isEmpty());QVERIFY(runner.status().state==RunState::Faulted);
        device.failInstall=false;plan.role=LinRole::Monitor;QVERIFY(runner.start(plan,19200,300,error));QVERIFY(device.publishers.isEmpty());QVERIFY(runner.simulatedHeader(0x12).isEmpty());
    }
    void nonPublisherUpdatesNeverReachHardware(){RecordingLin device;LinScheduleRunner runner(&device);QString error;auto plan=linPlan(LinRole::Slave);QVERIFY(runner.start(plan,19200,0,error));device.calls.clear();
        QVERIFY(runner.update(frameKey(Bus::Lin,0x12),{QByteArray(1,'x'),2},error));QVERIFY(device.calls.isEmpty());QVERIFY(runner.update(frameKey(Bus::Lin,0x11),{QByteArray(2,'y'),2},error));QCOMPARE(device.calls,QStringList{"update"});
    }
    void coordinatorIsPerChannel(){host::OperationCoordinator a,b;using Task=host::OperationCoordinator::Task;QVERIFY(a.acquire(Task::Signal));QVERIFY(!a.acquire(Task::Diagnostic));QVERIFY(b.acquire(Task::Diagnostic));a.release(Task::Scan);QVERIFY(!a.permits(Task::Download));a.release(Task::Signal);QVERIFY(a.acquire(Task::Download));}
};
QTEST_APPLESS_MAIN(SignalCodecTest)
#include "test_signal_codec.moc"
