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
    bool requestBoundary(QString&,bool round=true)override{calls<<(round?"requestRoundBoundary":"requestFrameBoundary");return true;}
    bool boundary(bool&b,QString&,bool=true)override{calls<<"boundary";b=reached;return true;}
    bool update(const TxItem&,QString&e)override{calls<<"update";if(failUpdate){e="injected update failure";return false;}return true;}
};
class SignalCodecTest:public QObject {
    Q_OBJECT
    QString fixture(const char*ext)const{return QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic."+ext;}
    QByteArray bytes(const char*ext)const{QFile f(fixture(ext));if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
    Database dbc()const{return DatabaseImporter::load(fixture("dbc"),Bus::Can).database;}
    Database ldf()const{return DatabaseImporter::load(fixture("ldf"),Bus::Lin).database;}
    static const FrameDefinition&find(const Database&db,const QString&name){for(const auto&f:db->frames)if(f.name==name)return f;return db->frames.first();}
    TxPlan linPlan(LinRole role=LinRole::Master)const{const auto db=ldf();TxPlan plan;plan.bus=Bus::Lin;plan.periodic=true;plan.role=role;plan.node=role==LinRole::Slave?"Sensor":"Tester";plan.schedules=db->schedules;plan.schedule="Main";plan.run=4;
        for(const auto&f:db->frames){TxDraft draft;QString e;SignalCodec::initialize(f,Bus::Lin,draft,e);plan.items.append({f.key,f.id,false,draft.applied.bytes,0,1,f.publisher,f.classicChecksum});}return plan;}
private slots:
    void legacyEncodingAndIndependentSignals(){
#ifdef Q_OS_WIN
        auto source=bytes("ldf");source+="\n/* "+QByteArray::fromHex("d6d0cec4")+" */\n";
        const auto imported=DatabaseImporter::ldf(source);QVERIFY2(imported.database,qPrintable(imported.error));QVERIFY(imported.database->diagnostics.join(' ').contains("GB18030"));
        DatabaseDefinition definition;QString text,error;QVERIFY(DatabaseImporter::prepare(QByteArray::fromHex("d6d0cec4"),{},definition,text,error));QCOMPARE(text,QString::fromUtf8("中文"));
        QVERIFY(!DatabaseImporter::prepare(QByteArray::fromHex("81"),{},definition,text,error));
#endif
        const QByteArray independent="VERSION \"\"\nNS_ :\nBS_:\nBU_: Node\nBO_ 3221225472 VECTOR__INDEPENDENT_SIG_MSG: 0 Node\n SG_ Detached : 0|8@1+ (1,0) [0|255] \"\" Node\n";
        const auto result=DatabaseImporter::dbc(independent);QVERIFY2(result.database,qPrintable(result.error));QCOMPARE(result.database->frames.size(),1);QCOMPARE(result.database->frames.first().fields.first().name,QString("Detached"));QVERIFY(!result.database->frames.first().issue.isEmpty());
        auto invalid=independent;invalid.replace("VECTOR__INDEPENDENT_SIG_MSG","Normal");QVERIFY(!DatabaseImporter::dbc(invalid).database);
    }
    void unusedInvalidLinEncodingIsDiagnosed(){
        auto hex=bytes("ldf");hex.replace("physical_value, 0, 100,","physical_value, 0x00, 0x64,");QVERIFY(DatabaseImporter::ldf(hex).database);
        auto source=bytes("ldf");source.replace("Signal_encoding_types {","Signal_encoding_types { Unused { physical_value, -2560, 2559, 0.1, 0, \"degC\"; }");
        auto result=DatabaseImporter::ldf(source);QVERIFY2(result.database,qPrintable(result.error));QCOMPARE(result.database->frames.size(),ldf()->frames.size());QVERIFY(result.database->diagnostics.join(' ').contains("-2560"));
        source.replace("Segments: Position","Unused: Position");result=DatabaseImporter::ldf(source);
        QVERIFY(!result.database||!find(result.database,"Measurement").issue.isEmpty());
    }
    void dbcRelationDeclarationsDoNotChangePayload(){
        auto source=bytes("dbc");
        source+="\nBA_DEF_REL_ BU_SG_REL_ \"Timeout\" INT 0 65535;\nBA_DEF_DEF_REL_ \"Timeout\" 0;\n";
        source+="BA_DEF_REL_ BU_BO_REL_ \"Label\" STRING;\nBA_DEF_DEF_REL_ \"Label\" \"text; with \\\"quotes\\\"\";\n";
        const auto result=DatabaseImporter::dbc(source);QVERIFY2(result.database,qPrintable(result.error));QCOMPARE(result.database->frames.size(),dbc()->frames.size());
        QVERIFY(result.database->diagnostics.join('\n').contains("4 条"));
        TxDraft draft;QString error;QVERIFY(SignalCodec::initialize(find(result.database,"Packed"),Bus::Can,draft,error));QCOMPARE(draft.applied.bytes,QByteArray::fromHex("020cff0ffff00000"));
        auto malformed=source;malformed.replace("INT 0 65535;","INT broken;");QVERIFY(!DatabaseImporter::dbc(malformed).database);
        malformed=source;malformed+="BA_DEF_DEF_REL_ \"unfinished\" \"value;";QVERIFY(!DatabaseImporter::dbc(malformed).database);
        source.replace("CM_ BO_ 291", "CM_ \"BA_DEF_REL_ inside a comment; remains text\";\n/*\nBA_DEF_REL_ not a declaration;\n*/\nCM_ BO_ 291");
        const auto commented=DatabaseImporter::dbc(source);QVERIFY2(commented.database,qPrintable(commented.error));QVERIFY(commented.database->diagnostics.join('\n').contains("4 条"));
    }
    void duplicateLinIdsRemainDistinctAndNotTransmittable(){
        auto source=bytes("ldf");source.replace("Frames {","Frames {\nAlias: 0x10, Tester, 2 { Command, 0; }\n");
        const auto result=DatabaseImporter::ldf(source);QVERIFY2(result.database,qPrintable(result.error));
        const auto&original=find(result.database,"Control");const auto&alias=find(result.database,"Alias");
        QCOMPARE(original.id,alias.id);QVERIFY(original.key!=alias.key);QVERIFY(!original.issue.isEmpty());QVERIFY(!alias.issue.isEmpty());
        QVERIFY(!SignalCodec::validateSchedule(result.database->schedules.first(),result.database->frames,19200).isEmpty());
        QSet<QString> keys;for(const auto&f:result.database->frames){QVERIFY(!keys.contains(f.key));keys.insert(f.key);TxDraft d;QString e;QVERIFY(SignalCodec::initialize(f,Bus::Lin,d,e));}
        source.replace("Alias: 0x10, Tester","Alias: 0x10, Unknown");QVERIFY(!DatabaseImporter::ldf(source).database);
    }
    void decimalMillisecondSlotsKeepTheirTiming(){
        QCOMPARE(SignalCodec::scheduleDelayMs("10.000"),10);QCOMPARE(SignalCodec::scheduleDelayMs("1e1"),10);
        QCOMPARE(SignalCodec::scheduleDelayMs("10.001"),-1);QCOMPARE(SignalCodec::scheduleDelayMs("65536"),-1);
        auto plan=linPlan();for(auto&slot:plan.schedules[0].entries)slot.delayMs="10.000";
        LinScheduleRunner runner;QString error;int events=0;runner.event=[&](const BusFrameEvent&){++events;};
        QVERIFY(runner.start(plan,19200,0,error));QVERIFY(runner.tick(0,error));QCOMPARE(events,1);
        QVERIFY(runner.tick(9999,error));QCOMPARE(events,1);QVERIFY(runner.tick(10000,error));QCOMPARE(events,2);
        QVERIFY(runner.tick(19999,error));QCOMPARE(events,2);QVERIFY(runner.tick(20000,error));QCOMPARE(events,3);
    }
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
    void ldfScalarArraysSegmentsAndChecksum(){auto result=DatabaseImporter::load(fixture("ldf"),Bus::Lin);QVERIFY2(result.database,qPrintable(result.error));const auto db=result.database;QCOMPARE(db->frames.size(),5);QCOMPARE(db->bitrate,19200);
        QString error;TxDraft draft;QVERIFY(SignalCodec::initialize(find(db,"Control"),Bus::Lin,draft,error));QCOMPARE(draft.applied.bytes,QByteArray::fromHex("faff"));
        const auto&blob=find(db,"Blob");QVERIFY(blob.classicChecksum);QVERIFY(SignalCodec::initialize(blob,Bus::Lin,draft,error));QCOMPARE(draft.applied.bytes,QByteArray::fromHex("1234ff"));QCOMPARE(SignalCodec::defaults(blob.fields[0]).bytes,QByteArray::fromHex("ffff"));
        const auto&s=find(db,"Measurement").fields[0];QVERIFY(!SignalCodec::parsePhysical(s,"1.2").ok());auto inverse=SignalCodec::parsePhysical(s,"1.2",0);QVERIFY(inverse.ok());QCOMPARE(inverse.raw.bits,quint64(12));
        inverse=SignalCodec::parsePhysical(s,"1.2",1);QVERIFY(inverse.ok());QCOMPARE(inverse.raw.bits,quint64(112));
        QCOMPARE(SignalCodec::linPid(0x3c),quint8(0x3c));QCOMPARE(SignalCodec::linPid(0x3d),quint8(0x7d));
    }
    void ldfUnsupportedAndMalformedAreExplicit(){auto text=bytes("ldf");auto result=DatabaseImporter::ldf(text);QVERIFY(result.database);
        const auto&db=result.database;QVERIFY(SignalCodec::validateSchedule(db->schedules[0],db->frames,19200).isEmpty());QVERIFY(!SignalCodec::validateSchedule(db->schedules[2],db->frames,19200).isEmpty());QVERIFY(SignalCodec::validateSchedule(db->schedules[3],db->frames,19200).isEmpty());
        QVERIFY(!SignalCodec::validateSchedule(db->schedules[0],db->frames,1000).isEmpty());
        text.replace("Position, 0;","Missing, 0;");QVERIFY(!DatabaseImporter::ldf(text).database);
        text=bytes("ldf");text.replace("Command: 3, 2,","Command: 3, 8,");QVERIFY(!DatabaseImporter::ldf(text).database);
        text=bytes("ldf");text+="Unknown_semantics { a; }";result=DatabaseImporter::ldf(text);QVERIFY(!result.database);QVERIFY(result.error.contains("行"));
        text=bytes("ldf");text.chop(5);QVERIFY(!DatabaseImporter::ldf(text).database);
        text=bytes("ldf");text.replace("logical_value, 0, \"Off\";","ascii_value;");result=DatabaseImporter::ldf(text);QVERIFY(result.database);QVERIFY(!find(result.database,"Control").issue.isEmpty());
    }
    void sourceSignalCommentsAndDiagnosticValidation(){
        auto text=bytes("ldf");text.replace("Command: 3, 2, Tester, Sensor, Actuator;","Command: 3, 2, Tester, Sensor, Actuator; // Command annotation");auto result=DatabaseImporter::ldf(text);QVERIFY(result.database);QCOMPARE(find(result.database,"Control").fields.first().comment,QString("Command annotation"));QVERIFY(find(result.database,"Measurement").fields.first().comment.isEmpty());
        text=bytes("dbc");text.replace("BA_DEF_ SG_", "CM_ SG_ 291 Small \"Small annotation\";\nBA_DEF_ SG_");result=DatabaseImporter::dbc(text);QVERIFY2(result.database,qPrintable(result.error));QCOMPARE(find(result.database,"Packed").fields.first().comment,QString("Small annotation"));
        text=bytes("ldf");text+="\nDiagnostic_frames { Bad: 62 {} }";QVERIFY(!DatabaseImporter::ldf(text).database);
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
    void simulatedMasterSwitchesAfterCurrentFrame(){LinScheduleRunner runner;QString error;auto plan=linPlan();QVector<quint32>events;runner.event=[&](const BusFrameEvent&e){events.append(e.id);};
        QVERIFY(runner.start(plan,19200,0,error));QVERIFY(runner.tick(0,error));QCOMPARE(events,QVector<quint32>{0x10});QVERIFY(runner.switchTo("Alternate",1,error));
        QVERIFY(runner.tick(1000,error));QCOMPARE(events.size(),1);QCOMPARE(runner.status().current,QString("Main"));
        QVERIFY(runner.tick(5000,error));QCOMPARE(runner.status().current,QString("Alternate"));QCOMPARE(events,QVector<quint32>({0x10,0x12}));
    }
    void hardwareSwitchDrainsOldEventsAndMeasuresInterval(){RecordingLin device;LinScheduleRunner runner(&device);QString error;auto plan=linPlan();QStringList notes;runner.notice=[&](const QString&s){notes.append(s);};
        QVERIFY(runner.start(plan,19200,0,error));BusFrameEvent old;old.bus=Bus::Lin;old.id=0x10;old.source=EventSource::HardwareEcho;old.hardwareUs=1000;runner.observe(old);QCOMPARE(runner.status().sent.value(frameKey(Bus::Lin,0x10)),quint64(1));
        QVERIFY(runner.switchTo("Alternate",1,error));device.reached=true;bool drained=false;runner.drainBeforeSwitch=[&]{return drained;};QVERIFY(runner.tick(2,error));QCOMPARE(device.current,QString("Main"));
        old.id=0x11;old.source=EventSource::Received;old.hardwareUs=11000;runner.observe(old);drained=true;QVERIFY(runner.tick(10000,error));QCOMPARE(device.current,QString("Alternate"));
        BusFrameEvent first=old;first.id=0x12;first.hardwareUs=25000;runner.observe(first);QVERIFY(notes.last().contains("14.000 ms"));QVERIFY(notes.last().contains("硬件时间戳"));
    }
    void slaveWhitelistSwitchAndMonitor(){LinScheduleRunner sim;QString error;auto plan=linPlan(LinRole::Slave);QVERIFY(sim.start(plan,19200,0,error));QVERIFY(!sim.simulatedHeader(0x11).isEmpty());QVERIFY(sim.simulatedHeader(0x12).isEmpty());QVERIFY(sim.simulatedHeader(0x10).isEmpty());
        QVERIFY(sim.switchTo("Alternate",10,error));QVERIFY(sim.tick(10,error));QVERIFY(sim.simulatedHeader(0x11).isEmpty());QVERIFY(!sim.simulatedHeader(0x12).isEmpty());QVERIFY(sim.stop(error));QVERIFY(sim.simulatedHeader(0x12).isEmpty());
        RecordingLin device;LinScheduleRunner runner(&device);QVERIFY(runner.start(plan,19200,0,error));device.calls.clear();QVERIFY(runner.switchTo("Alternate",100,error));QVERIFY(runner.tick(10000,error));QCOMPARE(device.calls,QStringList({"stop","install"}));QCOMPARE(device.publishers,QSet<QString>{frameKey(Bus::Lin,0x12)});
        device.failInstall=true;QVERIFY(runner.switchTo("Main",20000,error));QVERIFY(!runner.tick(30000,error));QVERIFY(device.publishers.isEmpty());QVERIFY(runner.status().state==RunState::Faulted);
        device.failInstall=false;plan.role=LinRole::Monitor;QVERIFY(runner.start(plan,19200,300,error));QVERIFY(device.publishers.isEmpty());QVERIFY(runner.simulatedHeader(0x12).isEmpty());
    }
    void nonPublisherUpdatesNeverReachHardware(){RecordingLin device;LinScheduleRunner runner(&device);QString error;auto plan=linPlan(LinRole::Slave);QVERIFY(runner.start(plan,19200,0,error));device.calls.clear();
        QVERIFY(runner.update(frameKey(Bus::Lin,0x12),{QByteArray(1,'x'),2},error));QVERIFY(device.calls.isEmpty());QVERIFY(runner.update(frameKey(Bus::Lin,0x11),{QByteArray(2,'y'),2},error));QCOMPARE(device.calls,QStringList{"update"});
    }
    void canEnableUpdatesWaitForRoundAndCanResumeFromEmpty(){
        CanTxScheduler scheduler;QVector<TxItem> items{{"a",1,false,QByteArray(1,0),10,1,{},false,true},{"b",2,false,QByteArray(1,0),10,1,{},false,true},{"c",3,false,QByteArray(1,0),10,1,{},false,false}};
        QStringList sent;auto send=[&](const TxItem&i,QString&){sent<<i.key;return true;};scheduler.start(items,0);
        QCOMPARE(scheduler.pump(0,send,1),1);scheduler.setEnabled("b",false);scheduler.setEnabled("c",true);
        QCOMPARE(scheduler.pump(0,send,1),1);QCOMPARE(sent,QStringList({"a","b"}));
        scheduler.pump(10000,send);QCOMPARE(sent,QStringList({"a","b","a","c"}));
        scheduler.setEnabled("a",false);scheduler.setEnabled("c",false);scheduler.pump(20000,send);QCOMPARE(sent.size(),4);QVERIFY(scheduler.running());
        scheduler.setEnabled("b",true);scheduler.pump(30000,send);scheduler.pump(40000,send);QCOMPARE(sent.last(),QString("b"));QCOMPARE(sent.size(),5);
        scheduler.stop();QVERIFY(!scheduler.running());
    }
    void linEnableUpdatesWaitForFullRound(){
        LinScheduleRunner runner;auto plan=linPlan();QString error;QVector<quint32> ids;runner.event=[&](const BusFrameEvent&e){ids<<e.id;};
        QVERIFY(runner.start(plan,19200,0,error));QVERIFY(runner.tick(0,error));QVERIFY(runner.setEnabled(frameKey(Bus::Lin,0x11),false,1,error));
        QVERIFY(runner.tick(10000,error));QCOMPARE(ids,QVector<quint32>({0x10,0x11}));QVERIFY(runner.tick(20000,error));QCOMPARE(ids.last(),quint32(0x10));
        QVERIFY(runner.setEnabled(frameKey(Bus::Lin,0x10),false,20001,error));QVERIFY(runner.tick(30000,error));QCOMPARE(ids.size(),3);QCOMPARE(runner.status().state,RunState::Running);
        QVERIFY(runner.setEnabled(frameKey(Bus::Lin,0x11),true,30001,error));QVERIFY(runner.tick(31000,error));QCOMPARE(ids.last(),quint32(0x11));QVERIFY(runner.stop(error));
    }
    void hardwareEnableUsesRoundAndManualSwitchUsesFrame(){
        RecordingLin device;LinScheduleRunner runner(&device);QString error;auto plan=linPlan();QVERIFY(runner.start(plan,19200,0,error));
        QVERIFY(runner.setEnabled(frameKey(Bus::Lin,0x10),false,1,error));QVERIFY(device.calls.contains("requestRoundBoundary"));QVERIFY(device.publishers.contains(frameKey(Bus::Lin,0x10)));
        QVERIFY(runner.tick(10000,error));QVERIFY(device.publishers.contains(frameKey(Bus::Lin,0x10)));device.reached=true;QVERIFY(runner.tick(20000,error));QVERIFY(!device.publishers.contains(frameKey(Bus::Lin,0x10)));
        QVERIFY(runner.switchTo("Alternate",20001,error));QVERIFY(device.calls.contains("requestFrameBoundary"));QVERIFY(runner.tick(20002,error));QCOMPARE(device.current,QString("Main"));QVERIFY(runner.tick(30000,error));QCOMPARE(device.current,QString("Alternate"));
    }
    void slaveEnableWaitsForObservedRound(){
        LinScheduleRunner runner;QString error;auto plan=linPlan(LinRole::Slave);QVERIFY(runner.start(plan,19200,0,error));
        QVERIFY(runner.setEnabled(frameKey(Bus::Lin,0x11),false,1,error));QVERIFY(runner.tick(100000,error));QVERIFY(!runner.simulatedHeader(0x11).isEmpty());
        BusFrameEvent e;e.bus=Bus::Lin;e.id=0x10;runner.observe(e);QVERIFY(runner.tick(100001,error));QVERIFY(!runner.simulatedHeader(0x11).isEmpty());
        e.id=0x11;runner.observe(e);QVERIFY(runner.tick(100002,error));QVERIFY(runner.simulatedHeader(0x11).isEmpty());
    }
    void monitorDoesNotRequireExecutableSchedule(){RecordingLin device;LinScheduleRunner runner(&device);QString error;auto plan=linPlan(LinRole::Monitor);plan.schedule.clear();QVERIFY(runner.start(plan,19200,0,error));QVERIFY(device.publishers.isEmpty());
        QVERIFY(runner.switchTo("Fractional",1,error));QVERIFY(runner.tick(10000,error));QCOMPARE(runner.status().current,QString("Fractional"));QVERIFY(device.publishers.isEmpty());QVERIFY(!device.calls.contains("start:Fractional"));
    }
    void coordinatorIsPerChannel(){host::OperationCoordinator a,b;using Task=host::OperationCoordinator::Task;QVERIFY(a.acquire(Task::Signal));QVERIFY(!a.acquire(Task::Diagnostic));QVERIFY(b.acquire(Task::Diagnostic));a.release(Task::Scan);QVERIFY(!a.permits(Task::Download));a.release(Task::Signal);QVERIFY(a.acquire(Task::Download));}
};
QTEST_APPLESS_MAIN(SignalCodecTest)
#include "test_signal_codec.moc"
