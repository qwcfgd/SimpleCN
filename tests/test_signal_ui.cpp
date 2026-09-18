#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTabWidget>
#include <QTableView>
#include <QLabel>
#include <QDialog>
#include <QTreeView>
#include <QFontDatabase>
#include "viewmodels/SignalTransmitViewModel.h"
#include "viewmodels/SignalTableModels.h"
#include "model/SignalCodec.h"
#include "views/ChannelPage.h"
#include "views/SignalTransmitPage.h"
#include "infrastructure/SettingsStore.h"
using namespace host;
using namespace host::signal;
class SignalUiTest:public QObject {
    Q_OBJECT
    QString fixture(const char*ext="dbc")const{return QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic."+ext;}
    QString key(quint32 id=291)const{return frameKey(Bus::Can,id);}
    ChannelSettings settings(bool lin=false,int port=1){auto s=ChannelSettings::defaults(lin?communication::Bus::Lin:communication::Bus::Can);s.simulation=true;s.hardwareKey=QString("preview:%1:%2").arg(lin?"LIN":"CAN").arg(port);s.handle=(lin?0xf200:0xf100)+port;return s;}
    void connectChannel(ChannelViewModel &vm){QTRY_VERIFY_WITH_TIMEOUT(vm.canConnect(),3000);vm.toggleConnection();QTRY_VERIFY_WITH_TIMEOUT(vm.connected(),3000);}
private slots:
    void initTestCase(){QApplication::setStyle("Fusion");QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");QApplication::setFont(QFont("Microsoft YaHei",9));QDir().mkpath(QCoreApplication::applicationDirPath()+"/artifacts");}
    void draftsAreAtomicAndWarningsCanSend(){SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY2(vm.importFile(fixture(),error),qPrintable(error));auto before=vm.working().frames[key()].applied;
        QVERIFY(!vm.editSignal(key(),1,"300",false,error));QCOMPARE(vm.working().frames[key()].applied.bytes,before.bytes);QVERIFY(vm.hasInvalidDraft());
        QVERIFY(!vm.editSignal(key(),0,"4",false,error));QCOMPARE(vm.working().frames[key()].applied.bytes,before.bytes);
        QVERIFY(vm.editSignal(key(),1,"200",false,error));auto after=vm.working().frames[key()];QCOMPARE(quint8(after.applied.bytes[0]),quint8(4));QCOMPARE(quint8(after.applied.bytes[1]),quint8(200));QVERIFY(!after.warnings.value(1).isEmpty());QVERIFY(!vm.hasInvalidDraft());
        QVERIFY(!vm.editPayload(key(),"FF",error));QCOMPARE(vm.working().frames[key()].applied.bytes,after.applied.bytes);QVERIFY(vm.editPayload(key(),"01 02 03 04 05 06 07 08",error));QCOMPARE(vm.working().frames[key()].applied.bytes,QByteArray::fromHex("0102030405060708"));
    }
    void backRestoreAndSaveBaseline(){SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY(vm.importFile(fixture(),error));const auto initial=vm.working().frames[key()].applied.bytes;QTemporaryDir temp;
        QVERIFY(vm.editSignal(key(),0,"4",false,error));QVERIFY(vm.save(temp.filePath("signal.json"),error));QVERIFY(vm.editSignal(key(),0,"5",false,error));vm.back();QCOMPARE(quint8(vm.working().frames[key()].applied.bytes[0]),quint8(4));vm.restore();QCOMPARE(vm.working().frames[key()].applied.bytes,initial);QVERIFY(!vm.canBack());
        QVERIFY(vm.read(temp.filePath("signal.json"),error));QVERIFY(vm.putCustom({},"Custom",0x600,1,1,error));QVERIFY(vm.editSignal(key(),0,"6",false,error));vm.restore();QCOMPARE(quint8(vm.working().frames[key()].applied.bytes[0]),quint8(4));QVERIFY(vm.working().customFrames.isEmpty());QVERIFY(!vm.canBack());
    }
    void customIdsUseWholeDatabaseAndAutomaticType(){SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY(vm.importFile(fixture(),error));QVERIFY(!vm.putCustom({},"duplicate",291,2,1,error));
        QVERIFY(vm.putCustom({},"Small",0x700,0,0,error));QVERIFY(!vm.frame(key(0x700))->extended);QVERIFY(vm.putCustom({},"Large",0x800,8,1,error));QVERIFY(vm.frame(frameKey(Bus::Can,0x800,true))->extended);
        QVERIFY(!vm.putCustom({},"Again",0x700,1,1,error));QVERIFY(vm.putCustom(key(0x700),"SmallChanged",0x700,8,1,error));QCOMPARE(vm.frame(key(0x700))->length,8);
        QVERIFY(!vm.putCustom(key(0x700),"Collision",0x800,1,1,error));QVERIFY(vm.removeCustom(key(0x700),error));
    }
    void rangeQuantizationAndDefaultUseShareModel(){SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY(vm.importFile(fixture(),error));SignalValueTableModel main(&vm),defaults(&vm,true);main.setFrame(key());defaults.setFrame(key());
        QCOMPARE(defaults.index(0,1).data().toString(),QString("7"));QCOMPARE(defaults.index(0,2).data().toString(),QString("2"));
        QVERIFY(defaults.setData(defaults.index(0,2),"5"));QCOMPARE(main.index(0,2).data().toString(),QString("5"));QCOMPARE(defaults.index(0,1).data().toString(),QString("7"));
        QVERIFY(main.setData(main.index(1,3),"1.25"));QCOMPARE(main.index(1,2).data().toString(),QString("12"));QCOMPARE(main.index(1,3).data().toString(),QString("1.2"));QVERIFY(main.index(1,6).data().toString().contains("量化"));
        QVERIFY(!main.setData(main.index(1,2),"300"));QCOMPARE(main.index(1,2).data().toString(),QString("300"));QVERIFY(main.index(1,2).data(Qt::BackgroundRole).isValid());
    }
    void rxDoesNotChangeTxOrHistory(){SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY(vm.importFile(fixture(),error));const auto tx=vm.working().frames[key()].applied.bytes;
        BusFrameEvent event;event.id=291;event.bytes=QByteArray::fromHex("0102030405060708");event.source=EventSource::Received;event.hardwareUs=42;vm.receive({event});QCOMPARE(vm.working().frames[key()].applied.bytes,tx);QVERIFY(vm.working().frames[key()].rx.received);QVERIFY(!vm.canBack());
        QVERIFY(vm.editSignal(key(),0,"3",false,error));vm.back();QVERIFY(vm.working().frames[key()].rx.received);QCOMPARE(vm.working().frames[key()].rx.hardwareUs,quint64(42));
    }
    void configurationTransactionsAnd64Bit(){SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY(vm.importFile(fixture(),error));QVERIFY(vm.editSignal(key(292),0,"18446744073709551614",false,error));QTemporaryDir temp;const auto path=temp.filePath("signals.json");QVERIFY(vm.save(path,error));
        SignalTransmitViewModel copy(Bus::Can);QVERIFY2(copy.read(path,error),qPrintable(error));QCOMPARE(copy.working().frames[key(292)].values[0].bits,~quint64(0)-1);QVERIFY(!copy.running());
        const auto before=copy.configuration();auto bad=before;bad["sha256"]="changed";QVERIFY(!copy.readConfiguration(bad,temp.path(),error));QCOMPARE(copy.configuration(),before);
        bad=before;auto frames=bad["frames"].toArray();auto f=frames[0].toObject();f["payload"]="ZZ";frames[0]=f;bad["frames"]=frames;QVERIFY(!copy.readConfiguration(bad,temp.path(),error));QCOMPARE(copy.configuration(),before);
        bad=before;bad["source"]="missing.dbc";QVERIFY(!copy.readConfiguration(bad,temp.path(),error));QCOMPARE(copy.configuration(),before);
    }
    void failedDraftSaveKeepsLastAppliedWholeFrame(){SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY(vm.importFile(fixture(),error));const auto before=vm.working().frames[key()].applied.bytes;QVERIFY(!vm.editSignal(key(),1,"300",false,error));QVERIFY(!vm.editSignal(key(),0,"6",false,error));QTemporaryDir dir;QVERIFY(vm.save(dir.filePath("a.json"),error));QVERIFY(error.contains("错误草稿"));SignalTransmitViewModel copy(Bus::Can);QVERIFY(copy.read(dir.filePath("a.json"),error));QCOMPARE(copy.working().frames[key()].applied.bytes,before);QVERIFY(!copy.hasInvalidDraft());
    }
    void runningCapabilityMatrixAndGeneration(){SignalTransmitViewModel vm(Bus::Lin);QString error;QVERIFY(vm.importFile(fixture("ldf"),error));vm.setAvailability(true,false,19200,2);QVERIFY(vm.setRole(LinRole::Slave,"Sensor",error));QSignalSpy starts(&vm,&SignalTransmitViewModel::startRequested);QSignalSpy updates(&vm,&SignalTransmitViewModel::payloadRequested);
        QVERIFY(vm.start(true,error));QVERIFY(!vm.canStructure());QVERIFY(vm.canData());QVERIFY(!vm.setRole(LinRole::Master,"Tester",error));QVERIFY(!vm.canBack());
        QVERIFY(vm.editSignal(frameKey(Bus::Lin,0x10),0,"4",false,error));QCOMPARE(updates.count(),1);const auto u=qvariant_cast<PayloadUpdate>(updates[0][0]);QCOMPARE(u.connection,quint64(2));
        RunStatus stopped;stopped.run=vm.status().run;stopped.state=RunState::Stopped;vm.applyStatus(stopped);QVERIFY(vm.setRole(LinRole::Monitor,"Tester",error));QVERIFY(vm.start(true,error));QVERIFY(!vm.canData());QVERIFY(!vm.editSignal(frameKey(Bus::Lin,0x10),0,"5",false,error));
    }
    void asyncImportDoesNotPublishHalfDatabase(){SignalTransmitViewModel vm(Bus::Can);vm.importAsync(fixture());QVERIFY(vm.importing());QVERIFY(!vm.canStructure());QTRY_VERIFY_WITH_TIMEOUT(!vm.importing(),5000);QCOMPARE(vm.definitions().size(),5);const auto digest=vm.database()->sha256;vm.importAsync("not-found.dbc");QTRY_VERIFY_WITH_TIMEOUT(!vm.importing(),3000);QCOMPARE(vm.database()->sha256,digest);}
    void scheduleEditsKeepUnsupportedSourceTablesInert(){SignalTransmitViewModel vm(Bus::Lin);QString error;QVERIFY(vm.importFile(fixture("ldf"),error));auto tables=vm.working().schedules;tables[0].entries[0].delayMs="20";
        QVERIFY2(vm.replaceSchedules(tables,"Main",error),qPrintable(error));QVERIFY(!vm.working().schedules.last().issue.isEmpty());vm.setAvailability(true,false,19200,1);QVERIFY(vm.selectSchedule("DiagnosticOnly",error));QVERIFY(!vm.start(true,error));QVERIFY(vm.selectSchedule("Main",error));
        tables[0].entries[0].frame="invented";QVERIFY(!vm.replaceSchedules(tables,"Main",error));QCOMPARE(vm.working().schedules[0].entries[0].delayMs,QString("20"));
    }
    void pageOrderAndOfflineEditing(){ChannelViewModel vm(settings());ChannelPage page(&vm);page.resize(1300,950);page.show();auto*tabs=page.findChild<QTabWidget*>("taskPages");QVERIFY(tabs);QCOMPARE(tabs->count(),3);QCOMPARE(tabs->tabText(0),QString("下载"));QCOMPARE(tabs->tabText(1),QString("UDS 诊断"));QCOMPARE(tabs->tabText(2),QString("信号发送"));tabs->setCurrentIndex(2);
        QString error;QVERIFY(vm.signalTransmission()->importFile(fixture(),error));QVERIFY(page.findChild<QLineEdit*>("signalFrameRaw")->isEnabled());QVERIFY(!page.findChild<QPushButton*>("signalStart")->isEnabled());
        QTest::qWait(50);QDir().mkpath(QCoreApplication::applicationDirPath()+"/artifacts");QVERIFY(page.grab().save(QCoreApplication::applicationDirPath()+"/artifacts/signal-can.png"));
    }
    void configurationControlsOnlyAppearInSecondaryDialog(){
        for(bool lin:{false,true}){
            ChannelViewModel vm(settings(lin));SignalTransmitPage page(vm.signalTransmission());page.resize(1000,700);page.show();QString error;
            QVERIFY(vm.signalTransmission()->importFile(fixture(lin?"ldf":"dbc"),error));
            auto*dialog=page.findChild<QDialog*>("signalSettingsDialog");QVERIFY(dialog);QVERIFY(!dialog->isVisible());
            for(const auto*name:{"signalImport","signalReload","signalBack","signalRestore","signalCommunication"}){
                auto*control=dialog->findChild<QPushButton*>(name);QVERIFY(control);QVERIFY(!control->isVisible());
            }
            QVERIFY(dialog->findChild<QLabel*>("signalDatabaseSummary"));QVERIFY(page.findChild<QTreeView*>("signalTree")->isVisible());
            auto*plan=page.findChild<QTableView*>("signalPlanTable");QVERIFY(plan->isVisible());QVERIFY(plan->currentIndex().isValid());
            QVERIFY(page.findChild<QTableView*>("signalValues")->isVisible());QVERIFY(page.findChild<QLineEdit*>("signalFrameRaw")->isVisible());
            QTimer::singleShot(50,&page,[&]{QVERIFY(dialog->isVisible());QVERIFY(dialog->findChild<QPushButton*>("signalImport")->isVisible());dialog->reject();});
            QTest::mouseClick(page.findChild<QPushButton*>("signalSettings"),Qt::LeftButton);QVERIFY(!dialog->isVisible());
        }
    }
    void workerCanSinglePeriodicStopAndConflicts(){ChannelViewModel vm(settings());ChannelPage page(&vm);QString error;auto*s=vm.signalTransmission();QVERIFY(s->importFile(fixture(),error));QVERIFY(s->setCanOptions(key(),true,1,error));connectChannel(vm);
        QVERIFY2(s->start(false,error),qPrintable(error));QTRY_VERIFY_WITH_TIMEOUT(!s->running(),3000);QCOMPARE(s->status().sent.value(key()),quint64(1));QVERIFY(vm.frames()->rowCount()>0);
        QVERIFY(s->start(true,error));QTRY_VERIFY_WITH_TIMEOUT(s->status().sent.value(key())>2,3000);QVERIFY(vm.busy());QVERIFY(!vm.canStart());QVERIFY(!page.findChild<QPushButton*>("udsSettings")->isEnabled());QVERIFY(!s->putCustom({},"blocked",0x600,1,1,error));
        QVERIFY(s->editSignal(key(),0,"4",false,error));s->stop();QTRY_VERIFY_WITH_TIMEOUT(!s->running(),3000);const auto count=s->status().sent.value(key());QTest::qWait(40);QCOMPARE(s->status().sent.value(key()),count);QVERIFY(!vm.busy());
        QVERIFY(s->start(true,error));QTRY_VERIFY(s->status().sent.value(key())>0);vm.toggleConnection();QTRY_VERIFY(!vm.connected());QTRY_VERIFY(!s->running());QTRY_VERIFY(!vm.pending());QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected());QVERIFY(!s->running());
    }
    void channelsRunIndependently(){ChannelViewModel first(settings(false,1)),second(settings(false,2));QString error;for(auto*vm:{&first,&second}){QVERIFY(vm->signalTransmission()->putCustom({},"raw",0x600,1,1,error));QVERIFY(vm->signalTransmission()->setCanOptions(key(0x600),true,1,error));connectChannel(*vm);QVERIFY(vm->signalTransmission()->start(true,error));}
        QTRY_VERIFY(first.signalTransmission()->status().sent.value(key(0x600))>1);QTRY_VERIFY(second.signalTransmission()->status().sent.value(key(0x600))>1);first.signalTransmission()->stop();QTRY_VERIFY(!first.busy());QVERIFY(second.busy());second.signalTransmission()->stop();QTRY_VERIFY(!second.busy());
    }
    void stoppedWorkerRejectsStaleRunCommands(){const auto config=settings();ChannelModel model(config);QSignalSpy ready(&model,&ChannelModel::ready),generation(&model,&ChannelModel::connectionGenerationChanged),statuses(&model,&ChannelModel::signalStatus);
        QTRY_VERIFY(ready.count());model.connectionRequested(config);QTRY_VERIFY(generation.count());TxPlan plan;plan.run=1;plan.connection=generation.last()[0].toULongLong();plan.periodic=true;plan.items.append({"CAN:STD:1536",0x600,false,QByteArray(1,0),1,1,{},false});
        auto last=[&]{return statuses.isEmpty()?RunStatus():qvariant_cast<RunStatus>(statuses.last()[0]);};model.signalsRequested(plan);QTRY_VERIFY(last().state==RunState::Running);model.signalStopRequested(1);QTRY_VERIFY(last().state==RunState::Stopped);
        const auto count=statuses.count();model.signalsRequested(plan);QTest::qWait(35);QCOMPARE(statuses.count(),count);plan.run=2;model.signalsRequested(plan);QTRY_VERIFY(last().run==2&&last().state==RunState::Running);model.signalStopRequested(1);QTest::qWait(25);QVERIFY(last().state==RunState::Running);model.signalStopRequested(2);QTRY_VERIFY(last().state==RunState::Stopped);
    }
    void workerLinMonitorAndSwitch(){ChannelViewModel vm(settings(true));ChannelPage page(&vm);QString error;auto*s=vm.signalTransmission();QVERIFY(s->importFile(fixture("ldf"),error));connectChannel(vm);QVERIFY(s->start(true,error));QTRY_VERIFY(s->status().state==RunState::Running);QVERIFY(s->selectSchedule("Alternate",error));QTRY_COMPARE_WITH_TIMEOUT(s->status().current,QString("Alternate"),3000);
        s->stop();QTRY_VERIFY(!s->running());QVERIFY(s->setRole(LinRole::Monitor,"Tester",error));QCOMPARE(page.findChild<QComboBox*>("signalLinRole")->currentIndex(),int(LinRole::Monitor));const auto count=vm.frames()->rowCount();QVERIFY(s->start(true,error));QTRY_VERIFY(s->status().state==RunState::Running);QTest::qWait(50);QCOMPARE(vm.frames()->rowCount(),count);QVERIFY(!page.findChild<QLineEdit*>("signalFrameRaw")->isEnabled());s->stop();QTRY_VERIFY(!s->running());
        page.resize(1300,950);page.show();page.findChild<QTabWidget*>("taskPages")->setCurrentIndex(2);QTest::qWait(30);QVERIFY(page.grab().save(QCoreApplication::applicationDirPath()+"/artifacts/signal-lin.png"));
    }
    void settingsVersion3RestoresStopped(){ChannelViewModel vm(settings());QString error;QVERIFY(vm.signalTransmission()->importFile(fixture(),error));QVERIFY(vm.signalTransmission()->setCanOptions(key(),true,1,error));QTemporaryDir dir;SettingsStore store(dir.filePath("channels.json"));QVERIFY(store.save({vm.snapshotSettings()},error));QVector<ChannelSettings>loaded;QVERIFY(store.load(loaded,error));QCOMPARE(loaded.size(),1);ChannelViewModel restored(loaded[0]);QTRY_COMPARE(restored.signalTransmission()->definitions().size(),5);QVERIFY(restored.signalTransmission()->working().frames[key()].enabled);QVERIFY(!restored.signalTransmission()->running());
        auto legacy=loaded[0].toJson();legacy.remove("signalConfiguration");ChannelSettings parsed;QVERIFY(ChannelSettings::fromJson(legacy,parsed,error));QVERIFY(parsed.signalConfiguration.isEmpty());
    }
};
QTEST_MAIN(SignalUiTest)
#include "test_signal_ui.moc"
