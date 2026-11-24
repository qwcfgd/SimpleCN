#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QMessageBox>
#include <QLabel>
#include <QFile>
#include "views/MainWindow.h"
#include "views/ChannelHardwareEditor.h"
#include "views/SignalPlotDialog.h"
#include "views/SignalTransmitPage.h"
#include "views/SignalPlotCanvas.h"
#include "viewmodels/SignalTableModels.h"
#include "infrastructure/SignalTransmitter.h"
#include "infrastructure/TraceReader.h"
#include "infrastructure/TraceExporter.h"
#include "model/SignalCodec.h"
#include "domain/TraceClock.h"
using namespace host;using namespace host::signal;
class WorkbenchTest:public QObject{
    Q_OBJECT
    QString dbc()const{return QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic.dbc";}
    FrameRecord sample(qint64 us,quint32 id=291,int value=10,Bus bus=Bus::Can){FrameRecord r;r.bus=bus;r.id=id;r.typed=true;r.channel=bus==Bus::Can?"CAN1":"LIN1";r.timeUs=r.captureUs=us;r.epochMs=1700000000000+us/1000;r.payload=QByteArray(8,0);r.payload[1]=char(value);r.length=8;r.direction="Rx";return r;}
private slots:
    void manualLinAndScopedId(){
        SignalTransmitViewModel vm(Bus::Lin);QString error;QVERIFY(vm.createSchedule(error));QCOMPARE(vm.working().schedule,QString("Schedule_Default001"));QVERIFY(vm.putCustom({},"one",0x10,8,100,error));
        const auto old=frameKey(Bus::Lin,0x10);auto schedules=vm.working().schedules;auto second=schedules.first();second.name="other";schedules.append(second);QVERIFY(vm.replaceSchedules(schedules,schedules.first().name,error));
        QVERIFY(vm.changeId(old,0x11,error));const auto scopedKey=vm.working().schedules[0].entries[0].frame;QCOMPARE(vm.frame(scopedKey)->id,quint32(0x11));QCOMPARE(vm.working().schedules[1].entries[0].frame,old);
        QVERIFY(vm.putCustom({},"two",0x12,8,100,error));const auto config=vm.configuration();QVERIFY(!vm.changeId(scopedKey,0x12,error));QCOMPARE(vm.configuration(),config);
        vm.setRepeatCount(4);SignalTransmitViewModel restored(Bus::Lin);QVERIFY2(restored.readConfiguration(vm.configuration(),{},error),qPrintable(error));QCOMPARE(restored.working().repeatCount,4);QCOMPARE(restored.working().schedules.size(),2);
        QVERIFY(restored.setRole(LinRole::Slave,{},error));QCOMPARE(restored.working().node,QString("Slave"));
    }
    void linDatabaseIdIsolation(){
        SignalTransmitViewModel vm(Bus::Lin);QString error;QVERIFY(vm.importFile(QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic.ldf",error));
        const auto source=frameKey(Bus::Lin,0x10),target=frameKey(Bus::Lin,0x12);
        QVERIFY(vm.selectSchedule("Alternate",error));QVERIFY(vm.editPayload(target,"77",error));
        QVERIFY(vm.selectSchedule("Main",error));QVERIFY2(vm.changeId(source,0x12,error),qPrintable(error));
        const auto alias=vm.working().schedules[0].entries[0].frame;QVERIFY(alias!=target);
        QCOMPARE(vm.frame(alias)->name,QString("State"));QCOMPARE(vm.working().frames[alias].applied.bytes,QByteArray(1,char(5)));
        QCOMPARE(vm.working().frames[target].applied.bytes,QByteArray(1,char(0x77)));
        QCOMPARE(vm.working().schedules[1].entries[0].frame,target);QCOMPARE(vm.working().schedules[1].entries[2].frame,source);
        SignalTransmitViewModel restored(Bus::Lin);QVERIFY2(restored.readConfiguration(vm.configuration(),{},error),qPrintable(error));
        QCOMPARE(restored.frame(alias)->fields.size(),1);QCOMPARE(restored.working().frames[target].applied.bytes,QByteArray(1,char(0x77)));
        restored.setAvailability(true,false,19200,1);QSignalSpy starts(&restored,&SignalTransmitViewModel::startRequested);QVERIFY2(restored.start(false,error),qPrintable(error));
        auto plan=qvariant_cast<TxPlan>(starts[0][0]);LinScheduleRunner runner;QVERIFY2(runner.start(plan,19200,0,error),qPrintable(error));
    }
    void databaseRemapDefaults(){
        SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY(vm.importFile(dbc(),error));QVERIFY(vm.putCustom({},"manual",0x456,2,100,error));QVERIFY(vm.changeId(frameKey(Bus::Can,0x456),291,error));
        const auto *f=vm.frame(frameKey(Bus::Can,291));QVERIFY(f);TxDraft expected;QVERIFY(SignalCodec::initialize(*f,Bus::Can,expected,error));QCOMPARE(vm.working().frames[f->key].applied.bytes,expected.applied.bytes);QCOMPARE(vm.queuedDefinitions().size(),1);QCOMPARE(vm.queuedDefinitions().first().name,f->name);
        QVERIFY(vm.putCustom({},"other",0x457,8,100,error));auto before=vm.configuration();QVERIFY(!vm.changeId(frameKey(Bus::Can,0x457),291,error));QCOMPARE(vm.configuration(),before);
    }
    void finiteCanAndLin(){
        SignalTransmitter worker;TxPlan plan;plan.bus=Bus::Can;plan.repeatCount=3;plan.run=1;plan.items={{"a",0x10,false,QByteArray(1,'A'),10,1,{},false,true},{"b",0x11,false,QByteArray(1,'B'),20,1,{},false,true}};
        BusFrameEvents events;connect(&worker,&SignalTransmitter::events,this,[&](const BusFrameEvents&e){events+=e;});worker.start(plan,500000,true,nullptr,nullptr);QTRY_VERIFY_WITH_TIMEOUT(!worker.running(),1000);QCOMPARE(events.size(),6);
        QVector<qint64> a,b;for(const auto&e:events)(e.id==0x10?a:b).append(e.arrivalUs);QVERIFY(a.last()-a.first()>=20000);QVERIFY(b.last()-b.first()>=40000);
        LinScheduleRunner lin;plan={};plan.bus=Bus::Lin;plan.node="Master";plan.schedule="one";plan.repeatCount=2;plan.items={{frameKey(Bus::Lin,0x10),0x10,false,QByteArray(8,0),10,1,"Master",false,true}};plan.schedules={{"one",{},{{frameKey(Bus::Lin,0x10),"10",{},0}}}};
        QString error;int count=0;lin.event=[&](const BusFrameEvent&){++count;};QVERIFY(lin.start(plan,19200,0,error));QVERIFY(lin.tick(0,error));QVERIFY(lin.tick(10000,error));QVERIFY(lin.tick(20000,error));QCOMPARE(count,2);QCOMPARE(lin.status().state,RunState::Stopped);
    }
    void enabledControls(){
        SignalTransmitViewModel model(Bus::Lin);SignalTransmitPage page(&model);auto *vm=&model;QString error;QVERIFY(vm->createSchedule(error));QVERIFY(vm->putCustom({},"frame",0x10,8,100,error));
        vm->setAvailability(true,false,19200,1);auto *count=page.findChild<QSpinBox*>("signalRepeatCount");auto *once=page.findChild<QPushButton*>("signalSendOnce");auto *period=page.findChild<QPushButton*>("signalStart");QVERIFY(count&&once&&period);QVERIFY(once->isEnabled());QCOMPARE(once->text(),QString("多次调度"));QCOMPARE(period->text(),QString("周期调度"));
        QVERIFY(vm->setFrameEnabled(frameKey(Bus::Lin,0x10),false,error));QVERIFY(!count->isEnabled());QVERIFY(!once->isEnabled());QVERIFY(!period->isEnabled());QVERIFY(vm->setFrameEnabled(frameKey(Bus::Lin,0x10),true,error));QVERIFY(vm->setRole(LinRole::Slave,{},error));QVERIFY(!once->isEnabled());QVERIFY(!count->isEnabled());QVERIFY(period->isEnabled());
    }
    void nativeLogRoundTrip(){
        QTemporaryDir dir;QString error;FrameBatch rows{sample(0),sample(1000,0x12,30,Bus::Lin)};rows[0].direction="Tx";
        auto fd=sample(2000,0x1ABCDE);fd.extended=fd.fd=fd.brs=fd.esi=true;fd.length=12;fd.payload=QByteArray(12,char(0xAB));rows.append(fd);
        auto rtr=sample(3000,0x45);rtr.rtr=true;rtr.length=4;rtr.payload.clear();rows.append(rtr);
        for(const auto &ext:{"asc","blf"}){auto path=dir.filePath(QString("trace.")+ext);QVERIFY(TraceExporter::write(path,rows,error));const auto log=TraceReader::read(path);QVERIFY2(log.error.isEmpty(),qPrintable(log.error));QCOMPARE(log.frames.size(),4);QCOMPARE(log.channels.size(),2);for(int n=0;n<4;++n){QCOMPARE(log.frames[n].id,rows[n].id);QCOMPARE(log.frames[n].payload,rows[n].payload);QCOMPARE(log.frames[n].timeUs,rows[n].timeUs);QCOMPARE(log.frames[n].direction,rows[n].direction);QCOMPARE(log.frames[n].fd,rows[n].fd);QCOMPARE(log.frames[n].extended,rows[n].extended);QCOMPARE(log.frames[n].rtr,rows[n].rtr);QCOMPARE(log.frames[n].brs,rows[n].brs);QCOMPARE(log.frames[n].esi,rows[n].esi);}}
        const auto log=TraceReader::read(QString(TEST_SOURCE_DIR)+"/../testsrc/test.blf");if(QFile::exists(QString(TEST_SOURCE_DIR)+"/../testsrc/test.blf")){QVERIFY2(log.error.isEmpty(),qPrintable(log.error));QVERIFY(log.frames.size()>=6);}
    }
    void replayFastDirectionOverrideAndExtras(){
        SignalTransmitter worker;TxPlan plan;plan.bus=Bus::Can;plan.replay=true;plan.run=1;plan.repeatCount=2;plan.replayStartUs=captureTimeUs();plan.replayDurationUs=5001000;
        ReplayRecord a;a.id=0x10;a.payload=QByteArray(1,'A');a.length=1;a.rx=true;auto b=a;b.id=0x11;b.timeUs=5000000;b.rx=false;plan.replayFrames={a,b};plan.items={{frameKey(Bus::Can,0x10),0x10,false,QByteArray(1,'Z'),100,1,{},false,true},{frameKey(Bus::Can,0x12),0x12,false,QByteArray(1,'E'),1000,1,{},false,true}};
        BusFrameEvents events;connect(&worker,&SignalTransmitter::events,this,[&](const BusFrameEvents&e){events+=e;});worker.start(plan,500000,true,nullptr,nullptr);QTRY_VERIFY_WITH_TIMEOUT(!worker.running(),1000);int rx=0,tx=0,extra=0;for(const auto&e:events){if(e.id==0x10){++rx;QCOMPARE(e.bytes,QByteArray(1,'Z'));QCOMPARE(e.source,EventSource::Received);}else if(e.id==0x11){++tx;QCOMPARE(e.source,EventSource::Simulated);}else if(e.id==0x12)++extra;}QCOMPARE(rx,2);QCOMPARE(tx,2);QCOMPARE(extra,12);
    }
    void groupedDuplicatesAndSampling(){
        SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY(vm.importFile(dbc(),error));SignalPlotModel model(&vm);QVERIFY(model.addSignal(frameKey(Bus::Can,291),"Scaled"));QVERIFY(model.addSignal(frameKey(Bus::Can,291),"Scaled"));QVERIFY(model.series()[0].key!=model.series()[1].key);QVERIFY(model.series()[0].color!=model.series()[1].color);
        model.createGroup({0},"GroupA");model.createGroup({1},"GroupB");QVERIFY(model.series()[0].group!=model.series()[1].group);FrameBatch rows;for(int n=0;n<10000;++n)rows.append(sample(n*1000,291,n==5432?250:10));model.append(rows);const auto drawing=model.renderPoints(0,0,10000000);QVERIFY(drawing.size()<=1000);bool peak=false;for(const auto &p:drawing)peak|=p.point.value==25;QVERIFY(peak);QCOMPARE(model.renderPoints(0,100000,200000).size(),103);QCOMPARE(model.index(0,6).data().toString(),QString("1"));
        const auto saved=vm.configuration();SignalTransmitViewModel restored(Bus::Can);QVERIFY(restored.readConfiguration(saved,{},error));SignalPlotModel restoredPlot(&restored);QCOMPARE(restoredPlot.series().size(),2);QCOMPARE(restoredPlot.series()[0].groupName,QString("GroupA"));QCOMPARE(restoredPlot.series()[0].color.rgba(),model.series()[0].color.rgba());
    }

    void replayChannelMergeAndProjectPersistence(){
        QTemporaryDir dir;MainWindow window(dir.filePath("project.json"),true);auto *channel=window.canChannel();auto *vm=channel->signalTransmission();QString error;
        QTRY_VERIFY_WITH_TIMEOUT(channel->canConnect(),3000);channel->toggleConnection();QTRY_VERIFY_WITH_TIMEOUT(channel->connected()&&!channel->pending(),3000);
        FrameBatch rows{sample(0,0x10),sample(1000,0x11),sample(2000,0x12,10,Bus::Lin)};rows[0].channel="first";rows[1].channel="second";rows[2].channel="third";const auto path=dir.filePath("multi.blf");QVERIFY(TraceExporter::write(path,rows,error));
        vm->importReplay(path);QTRY_VERIFY_WITH_TIMEOUT(!vm->importing(),3000);QVERIFY2(vm->message().contains("已导入"),qPrintable(vm->message()));QCOMPARE(vm->replayLog().channels.size(),3);
        QJsonObject mapping;for(const auto &source:vm->replayLog().channels)mapping[source]=source.startsWith("CAN:")?channel->settings().softwareId:QString();vm->setReplayMapping(mapping);vm->setRepeatCount(2);
        QSignalSpy requested(vm,&SignalTransmitViewModel::startRequested);QVERIFY(vm->start(false,error));QCOMPARE(requested.size(),1);auto plan=qvariant_cast<TxPlan>(requested[0][0]);QCOMPARE(plan.replayFrames.size(),2);QVERIFY(plan.replay);QTRY_VERIFY_WITH_TIMEOUT(!vm->running(),3000);
        QTRY_COMPARE_WITH_TIMEOUT(channel->frames()->history().size(),4,3000);
        const auto saved=vm->configuration();SignalTransmitViewModel restored(Bus::Can);QVERIFY(restored.readConfiguration(saved,{},error));QTRY_VERIFY_WITH_TIMEOUT(!restored.importing(),3000);QCOMPARE(restored.working().replaySettings["mapping"].toObject(),mapping);QVERIFY(restored.replayConfigured());restored.resetReplay();QVERIFY(!restored.replayConfigured());
    }

    void linDelayUndoAndInitializedExpansion(){
        SignalTransmitViewModel vm(Bus::Lin);QString error;
        QVERIFY(vm.createSchedule(error));QVERIFY(vm.putCustom({},"short",0x10,2,100,error));
        QVERIFY(vm.setLinDelay(0,"250",error));vm.back();QCOMPARE(vm.working().schedules[0].entries[0].delayMs,QString("100"));
        QVERIFY(vm.setLinDelay(0,"350",error));vm.back();QCOMPARE(vm.working().schedules[0].entries[0].delayMs,QString("100"));
        QVERIFY(vm.editPayload(frameKey(Bus::Lin,0x10),"AA BB",error));
        QVERIFY(vm.changeId(frameKey(Bus::Lin,0x10),0x3c,error));
        auto key=vm.working().schedules[0].entries[0].frame;
        QCOMPARE(vm.working().frames[key].applied.bytes,QByteArray::fromHex("aabb000000000000"));
        SignalTransmitViewModel restored(Bus::Lin);QVERIFY(restored.readConfiguration(vm.configuration(),{},error));
        QCOMPARE(restored.working().frames[key].applied.bytes,QByteArray::fromHex("aabb000000000000"));
    }
    void unconfirmedRequestsAreAnnotations(){
        QTemporaryDir dir;QString error;
        auto request=sample(0);request.direction="TX";request.request=true;
        auto echo=request;echo.request=false;echo.echo=true;echo.direction="TX ECHO";echo.timeUs=100;
        auto received=sample(200,0x124);auto simulated=sample(300,0x125);simulated.simulated=true;simulated.direction="SIM TX";
        for(const auto &ext:{"asc","blf"}){
            auto path=dir.filePath(QString("trace.")+ext);
            QVERIFY(TraceExporter::write(path,{request,echo,received,simulated},error));
            auto log=TraceReader::read(path);QVERIFY2(log.error.isEmpty(),qPrintable(log.error));QCOMPARE(log.frames.size(),3);
            QCOMPARE(log.frames[0].id,request.id);QCOMPARE(log.frames[0].direction,QString("Tx"));
            QCOMPARE(log.frames[1].direction,QString("Rx"));QCOMPARE(log.frames[2].id,simulated.id);
            QVERIFY(TraceExporter::write(path,{request},error));QVERIFY(TraceReader::read(path).frames.isEmpty());
        }
        auto path=dir.filePath("trace.csv");QVERIFY(TraceExporter::write(path,{request,echo},error));QFile csv(path);QVERIFY(csv.open(QIODevice::ReadOnly));QVERIFY(csv.readAll().contains("TX ECHO"));
    }
    void restoreOpenPlotConfiguration(){
        SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY(vm.importFile(dbc(),error));
        SignalPlotDialog dialog(&vm);auto *model=dialog.plotModel();const auto f=vm.database()->frames.first();
        QVERIFY(model->addSignal(f.key,f.fields.first().name));model->setColor(0,QColor("#123456"));model->createGroup({0},"Saved");
        model->mark({0},0);
        auto *display=dialog.findChild<QComboBox*>("plotDisplayMode");auto *axes=dialog.findChild<QComboBox*>("plotAxisMode");
        auto *cursor=dialog.findChild<QCheckBox*>("plotCursor");auto *c1=dialog.findChild<QDoubleSpinBox*>("plotC1");
        display->setCurrentIndex(2);QMetaObject::invokeMethod(display,"activated",Q_ARG(int,2));axes->setCurrentIndex(1);QMetaObject::invokeMethod(axes,"activated",Q_ARG(int,1));
        cursor->click();c1->setValue(2.5);const auto config=vm.configuration();
        QVERIFY(model->addSignal(f.key,f.fields.first().name));model->setColor(0,Qt::red);
        display->setCurrentIndex(0);cursor->click();c1->setValue(9);
        QVERIFY2(vm.readConfiguration(config,{},error),qPrintable(error));
        QCOMPARE(model->rowCount(),1);QCOMPARE(model->series()[0].color,QColor("#123456"));QCOMPARE(model->series()[0].groupName,QString("Saved"));
        QCOMPARE(display->currentIndex(),2);QCOMPARE(axes->currentIndex(),1);QVERIFY(cursor->isChecked());QCOMPARE(c1->value(),2.5);
        QCOMPARE(vm.working().uiSettings.value("plotSeries"),config["uiSettings"].toObject()["plotSeries"]);
        QCOMPARE(vm.working().uiSettings.value("plotControls"),config["uiSettings"].toObject()["plotControls"]);
        QVERIFY(vm.readConfiguration(config,{},error));QCOMPARE(model->rowCount(),1);
    }
    void replayOwnerCannotEnterScheduleSwitch(){
        SignalTransmitViewModel vm(Bus::Lin);QString error;QVERIFY(vm.createSchedule(error));QVERIFY(vm.putCustom({},"one",0x10,8,100,error));QVERIFY(vm.createSchedule(error));
        SignalTransmitPage page(&vm);QSignalSpy schedules(&vm,&SignalTransmitViewModel::scheduleRequested),stops(&vm,&SignalTransmitViewModel::stopRequested);
        vm.setReplayGroupRunning(true);QVERIFY(!vm.selectSchedule("Schedule_Default001",error));QCOMPARE(schedules.size(),0);QCOMPARE(vm.status().state,RunState::Stopped);
        connect(&vm,&SignalTransmitViewModel::replayStopRequested,&vm,[&]{vm.setReplayGroupRunning(false);});
        vm.stop();QVERIFY(!vm.running());QCOMPARE(stops.size(),0);QCOMPARE(vm.status().state,RunState::Stopped);
        vm.setAvailability(true,false,19200,1);TxPlan plan;plan.bus=Bus::Lin;plan.replay=true;
        QVERIFY(vm.startReplayPlan(plan,error));QVERIFY(!vm.selectSchedule("Schedule_Default001",error));QCOMPARE(schedules.size(),0);
        vm.stop();QCOMPARE(stops.size(),1);
    }


    void backendSwitchDropsStaleHardwareBinding(){
        auto settings=ChannelSettings::defaults(communication::Bus::Can);settings.simulation=false;settings.hardwareKey="tosun:NOT-CONNECTED:CAN:0";settings.handle=0x7000abcd;
        ChannelViewModel vm(settings);ChannelHardwareEditor editor(&vm);auto *mode=editor.findChild<QComboBox*>("modeCombo");QVERIFY(mode);
        // An explicitly missing real-device selection must not carry into the simulator.
        QTRY_VERIFY_WITH_TIMEOUT(!vm.pending(),3000);mode->setCurrentIndex(1);
        QTRY_VERIFY_WITH_TIMEOUT(vm.canConnect(),5000);QVERIFY(vm.settings().simulation);QVERIFY(vm.settings().hardwareKey.startsWith("preview:CAN:"));
        // Exercise the reverse transition with an already populated old backend list.
        mode->setCurrentIndex(0);QTRY_VERIFY_WITH_TIMEOUT(!vm.pending(),5000);
        QVERIFY(!vm.settings().simulation);QVERIFY(!vm.settings().hardwareKey.startsWith("preview:"));
        mode->setCurrentIndex(1);QTRY_VERIFY_WITH_TIMEOUT(vm.canConnect(),5000);
        QVERIFY(vm.settings().hardwareKey.startsWith("preview:CAN:"));
    }

    void projectDirtyExit(){
        QTemporaryDir dir;MainWindow window(dir.filePath("project.json"),true);window.show();QVERIFY(!window.findChild<QLabel*>("versionBadge")->isVisible());QString error;auto *vm=window.canChannel()->signalTransmission();QVERIFY(vm->putCustom({},"test",0x321,8,100,error));bool prompted=false;
        QTimer::singleShot(50,&window,[&]{auto *box=window.findChild<QMessageBox*>("saveProjectOnClose");if(box){prompted=true;box->done(QMessageBox::Save);}});QVERIFY(window.close());QVERIFY(prompted);QVERIFY(QFile::exists(dir.filePath("project.json")));
    }
};
QTEST_MAIN(WorkbenchTest)
#include "test_workbench.moc"
