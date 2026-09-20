#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTabWidget>
#include <QTableView>
#include <QLabel>
#include <QDialog>
#include <QMenu>
#include <QDialogButtonBox>
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
    void connectChannel(ChannelViewModel &vm){QTRY_VERIFY_WITH_TIMEOUT(vm.canConnect(),3000);vm.toggleConnection();QTRY_VERIFY_WITH_TIMEOUT(vm.connected()&&!vm.pending(),3000);}
private slots:
    void localDatabaseSamples_data(){QTest::addColumn<bool>("lin");QTest::newRow("test.dbc")<<false;QTest::newRow("test.ldf")<<true;}
    void localDatabaseSamples(){
        QFETCH(bool,lin);const auto ext=lin?QString("ldf"):QString("dbc");
        const auto folder=qEnvironmentVariable("HOST_SIGNAL_SAMPLE_DIR",QString(TEST_SOURCE_DIR)+"/../testsrc");
        const auto path=folder+"/test."+ext;if(!QFileInfo::exists(path))QSKIP("Optional local testsrc database not installed");
        QFile source(path);QVERIFY(source.open(QIODevice::ReadOnly));const auto original=source.readAll();source.close();
        ChannelViewModel vm(settings(lin));ChannelPage page(&vm);page.resize(1450,1000);page.show();
        page.findChild<QTabWidget*>("taskPages")->setCurrentIndex(2);auto*s=vm.signalTransmission();s->importAsync(path);
        QTRY_VERIFY_WITH_TIMEOUT(!s->importing(),10000);QVERIFY2(!s->database()->frames.isEmpty(),qPrintable(s->message()));
        QCOMPARE(s->database()->sha256,QString::fromLatin1(QCryptographicHash::hash(original,QCryptographicHash::Sha256).toHex()));
        QSet<QString> keys;int fields=0;for(const auto&f:s->definitions()){QVERIFY(!keys.contains(f.key));keys.insert(f.key);fields+=f.fields.size();}
        QVERIFY(fields>0);QVERIFY(page.findChild<QTreeView*>("signalTree")->model()->rowCount()>0);
        auto*plan=page.findChild<QTableView*>("signalPlanTable");if(!lin){QCOMPARE(plan->model()->rowCount(),0);auto*node=page.findChild<QComboBox*>("signalCanNode");node->setCurrentIndex(1);}
        QVERIFY(plan->model()->rowCount()>0);QVERIFY(plan->currentIndex().isValid());
        QVERIFY(!page.findChild<QLineEdit*>("signalFrameRaw"));QVERIFY(!plan->model()->index(0,lin?4:5).data().toString().isEmpty());
        QTest::qWait(60);const auto artifact=QCoreApplication::applicationDirPath()+"/artifacts/testsrc-"+ext;
        QVERIFY(page.grab().save(artifact+".png"));
        auto*dialog=page.findChild<QDialog*>("signalSettingsDialog");QVERIFY(dialog);bool captured=false;
        QTimer::singleShot(50,&page,[&]{captured=dialog->isVisible()&&dialog->grab().save(artifact+"-settings.png");dialog->reject();});
        QTest::mouseClick(page.findChild<QPushButton*>("signalSettings"),Qt::LeftButton);QVERIFY(captured);
        QTemporaryDir temp;QString error;QVERIFY2(s->save(temp.filePath("sample.json"),error),qPrintable(error));
        SignalTransmitViewModel restored(lin?Bus::Lin:Bus::Can);QVERIFY2(restored.read(temp.filePath("sample.json"),error),qPrintable(error));
        QCOMPARE(restored.configuration(),s->configuration());QVERIFY(!restored.running());
        connectChannel(vm);
        if(!lin){const auto f=s->definitions().first();QVERIFY2(s->setCanOptions(f.key,true,10,error),qPrintable(error));}
        QVERIFY2(s->start(lin,error),qPrintable(error));
        if(lin){QTRY_VERIFY(s->running());QTRY_VERIFY_WITH_TIMEOUT(vm.frames()->rowCount()>1,2000);s->stop();}
        QTRY_VERIFY_WITH_TIMEOUT(!s->running(),3000);QVERIFY(vm.frames()->rowCount()>0);
        vm.toggleConnection();QTRY_VERIFY(!vm.connected()&&!vm.pending());
        QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),original);
        qInfo()<<ext<<"frames"<<s->definitions().size()<<"signals"<<fields<<"schedules"<<s->working().schedules.size();
    }
    void initTestCase(){QApplication::setStyle("Fusion");QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");QApplication::setFont(QFont("Microsoft YaHei",9));QDir().mkpath(QCoreApplication::applicationDirPath()+"/artifacts");}
    void canNodeDirectionsAndQueuePersistence(){
        SignalTransmitViewModel vm(Bus::Can);QString error;CanTxTableModel table(&vm);QVERIFY(vm.importFile(fixture(),error));QCOMPARE(table.rowCount(),0);
        QVERIFY(vm.putCustom({},"Keep",0x600,2,10,error));QCOMPARE(table.rowCount(),1);
        for(const auto&node:vm.database()->nodes)for(const auto&direction:QStringList{"Tx","Rx","Tx/Rx"}){
            QVERIFY(vm.selectCanNode(node,direction,error));QSet<QString> expected{key(0x600)};
            for(const auto&f:vm.database()->frames){bool rx=false;for(const auto&field:f.fields)rx|=field.receivers.contains(node);
                if((direction!="Rx"&&f.publisher==node)||(direction!="Tx"&&rx))expected.insert(f.key);}
            QSet<QString> actual;for(int i=0;i<table.rowCount();++i)actual.insert(table.key(i));QCOMPARE(actual,expected);
            QVERIFY(vm.selectCanNode(node,direction,error));QCOMPARE(table.rowCount(),expected.size());
        }
        QVERIFY(vm.selectCanNode("Tester","Tx",error));const auto removed=table.key(0);const auto bytes=vm.working().frames[removed].applied.bytes;
        QVERIFY(vm.removeQueuedFrame(removed,error));QVERIFY(vm.frame(removed));QVERIFY(!vm.working().frames[removed].enabled);QCOMPARE(vm.working().frames[removed].applied.bytes,bytes);
        const auto saved=vm.configuration();SignalTransmitViewModel copy(Bus::Can);QVERIFY2(copy.readConfiguration(saved,{},error),qPrintable(error));QCOMPARE(copy.configuration(),saved);
        vm.back();QVERIFY(vm.working().frames[removed].enabled);vm.restore();QCOMPARE(table.rowCount(),0);
        auto bad=saved;bad["canDirection"]="bad";QVERIFY(!copy.readConfiguration(bad,{},error));QCOMPARE(copy.configuration(),saved);
        bad=saved;bad["canNode"]="missing";QVERIFY(!copy.readConfiguration(bad,{},error));QCOMPARE(copy.configuration(),saved);
        auto legacy=saved;legacy.remove("canNode");legacy.remove("canDirection");QVERIFY(copy.readConfiguration(legacy,{},error));QVERIFY(!copy.working().frames[removed].enabled);QVERIFY(copy.working().frames[key(0x600)].enabled);
    }
    void canAdditionalTransmittersAndUnsupportedQueue(){
        QFile source(fixture());QVERIFY(source.open(QIODevice::ReadOnly));auto data=source.readAll();data.insert(data.indexOf("CM_ BO_"),"BO_TX_BU_ 291 : Tester,ECU;\n\n");
        QTemporaryDir dir;const auto path=dir.filePath("multi.dbc");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(data),qint64(data.size()));file.close();
        SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY2(vm.importFile(path,error),qPrintable(error));QVERIFY(vm.selectCanNode("ECU","Tx",error));
        QCOMPARE(vm.queuedDefinitions().size(),2);QVERIFY(vm.working().frames[key()].enabled);QVERIFY(vm.working().frames[key(294)].enabled);
        SignalTransmitViewModel copy(Bus::Can);QVERIFY2(copy.readConfiguration(vm.configuration(),{},error),qPrintable(error));QCOMPARE(copy.configuration(),vm.configuration());
        copy.setAvailability(true,false,500000,1);QVERIFY(!copy.start(false,error));QVERIFY(copy.removeQueuedFrame(key(294),error));
        QSignalSpy sent(&copy,&SignalTransmitViewModel::startRequested);QVERIFY(copy.start(false,error));QCOMPARE(sent.count(),1);const auto plan=qvariant_cast<TxPlan>(sent.first().first());QCOMPARE(plan.items.size(),1);QCOMPARE(plan.items.first().key,key());
    }
    void payloadColumnEditsAndRuntimeLocks(){
        for(bool lin:{false,true}){
            SignalTransmitViewModel vm(lin?Bus::Lin:Bus::Can);QString error;QVERIFY(vm.importFile(fixture(lin?"ldf":"dbc"),error));
            CanTxTableModel can(&vm);LinScheduleTableModel schedule(&vm);QAbstractItemModel*table=lin?static_cast<QAbstractItemModel*>(&schedule):&can;
            if(!lin)QVERIFY(vm.setCanOptions(key(),true,10,error));const auto frame=lin?schedule.key(0):can.key(0);const auto cell=table->index(0,lin?4:5);
            QCOMPARE(table->headerData(cell.column(),Qt::Horizontal).toString(),QString("报文"));
            const auto data=QByteArray(vm.frame(frame)->length,char(1));QVERIFY(table->setData(cell,QString::fromLatin1(data.toHex(' '))));QCOMPARE(vm.working().frames[frame].applied.bytes,data);
            QVERIFY(!table->setData(cell,"ZZ"));QCOMPARE(vm.working().frames[frame].applied.bytes,data);QCOMPARE(cell.data(Qt::EditRole).toString(),QString("ZZ"));
            QVERIFY(table->setData(cell,QString::fromLatin1(data.toHex(' '))));QVERIFY(vm.editSignal(frame,0,"2",false,error));QCOMPARE(cell.data().toString(),QString::fromLatin1(vm.working().frames[frame].applied.bytes.toHex(' ')).toUpper());
            vm.setAvailability(true,false,19200,1);QVERIFY(vm.start(true,error));QVERIFY(cell.flags()&Qt::ItemIsEditable);
            if(!lin){QVERIFY(!vm.removeQueuedFrame(frame,error));QVERIFY(!vm.selectCanNode("Tester","Rx",error));QVERIFY(!(table->index(0,4).flags()&Qt::ItemIsEditable));}
        }
    }
    void canQueueContextMenu(){
        SignalTransmitViewModel vm(Bus::Can);SignalTransmitPage page(&vm);page.resize(1200,750);page.show();QString error;QVERIFY(vm.importFile(fixture(),error));
        auto*table=page.findChild<QTableView*>("signalPlanTable");QCOMPARE(table->model()->rowCount(),0);
        for(const auto*name:{"signalAddCan","signalEditCan","signalDeleteCan"})QVERIFY(!page.findChild<QPushButton*>(name));
        auto*node=page.findChild<QComboBox*>("signalCanNode");auto*direction=page.findChild<QComboBox*>("signalCanDirection");node->setCurrentIndex(node->findData("Tester"));direction->setCurrentText("Tx");
        const auto initial=table->model()->rowCount();QVERIFY(initial>0);
        const auto removed=vm.queuedDefinitions().first().key;bool deleted=false;
        QTimer::singleShot(20,&page,[&]{auto*menu=page.findChild<QMenu*>("signalPlanMenu");QVERIFY(menu);auto*action=menu->actions().first();QCOMPARE(action->text(),QString("删除报文"));action->trigger();menu->close();deleted=true;});
        table->customContextMenuRequested(table->visualRect(table->model()->index(0,0)).center());QVERIFY(deleted);QCOMPARE(table->model()->rowCount(),initial-1);QVERIFY(vm.frame(removed));
        bool created=false;QTimer::singleShot(20,&page,[&]{auto*menu=page.findChild<QMenu*>("signalPlanMenu");QVERIFY(menu);QCOMPARE(menu->actions().first()->objectName(),QString("signalCreateFrame"));
            QTimer::singleShot(20,&page,[&]{auto*dialog=page.findChild<QDialog*>("signalCustomDialog");QVERIFY(dialog);dialog->findChild<QLineEdit*>("signalCustomName")->setText("ContextCustom");dialog->findChild<QLineEdit*>("signalCustomId")->setText("600");QTest::mouseClick(dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),Qt::LeftButton);created=true;});
            menu->actions().first()->trigger();menu->close();});
        table->customContextMenuRequested(QPoint(5,table->viewport()->height()-5));QVERIFY(created);QCOMPARE(table->model()->rowCount(),initial);QVERIFY(vm.working().frames[key(0x600)].enabled);
        direction->setCurrentText("Rx");QVERIFY(vm.working().frames[key(0x600)].enabled);QVERIFY(vm.removeQueuedFrame(key(0x600),error));QVERIFY(!vm.frame(key(0x600)));
    }
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
        QString error;QVERIFY(vm.signalTransmission()->importFile(fixture(),error));QVERIFY(vm.signalTransmission()->selectCanNode("Tester","Tx",error));QVERIFY(page.findChild<QTableView*>("signalPlanTable")->model()->index(0,5).flags()&Qt::ItemIsEditable);QVERIFY(!page.findChild<QPushButton*>("signalStart")->isEnabled());
        QTest::qWait(50);QDir().mkpath(QCoreApplication::applicationDirPath()+"/artifacts");QVERIFY(page.grab().save(QCoreApplication::applicationDirPath()+"/artifacts/signal-can.png"));
    }
    void configurationControlsOnlyAppearInSecondaryDialog(){
        for(bool lin:{false,true}){
            ChannelViewModel vm(settings(lin));SignalTransmitPage page(vm.signalTransmission());page.resize(1000,700);page.show();QString error;
            QVERIFY(vm.signalTransmission()->importFile(fixture(lin?"ldf":"dbc"),error));if(!lin)QVERIFY(vm.signalTransmission()->selectCanNode("Tester","Tx",error));
            auto*dialog=page.findChild<QDialog*>("signalSettingsDialog");QVERIFY(dialog);QVERIFY(!dialog->isVisible());
            for(const auto*name:{"signalImport","signalReload","signalBack","signalRestore","signalCommunication"}){
                auto*control=dialog->findChild<QPushButton*>(name);QVERIFY(control);QVERIFY(!control->isVisible());
            }
            QVERIFY(dialog->findChild<QLabel*>("signalDatabaseSummary"));QVERIFY(page.findChild<QTreeView*>("signalTree")->isVisible());
            auto*plan=page.findChild<QTableView*>("signalPlanTable");QVERIFY(plan->isVisible());QVERIFY(plan->currentIndex().isValid());
            QVERIFY(page.findChild<QTableView*>("signalValues")->isVisible());QVERIFY(!page.findChild<QLineEdit*>("signalFrameRaw"));
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
        s->stop();QTRY_VERIFY(!s->running());QVERIFY(s->setRole(LinRole::Monitor,"Tester",error));QCOMPARE(page.findChild<QComboBox*>("signalLinRole")->currentIndex(),int(LinRole::Monitor));const auto count=vm.frames()->rowCount();QVERIFY(s->start(true,error));QTRY_VERIFY(s->status().state==RunState::Running);QTest::qWait(50);QCOMPARE(vm.frames()->rowCount(),count);QVERIFY(!(page.findChild<QTableView*>("signalPlanTable")->model()->index(0,4).flags()&Qt::ItemIsEditable));s->stop();QTRY_VERIFY(!s->running());
        page.resize(1300,950);page.show();page.findChild<QTabWidget*>("taskPages")->setCurrentIndex(2);QTest::qWait(30);QVERIFY(page.grab().save(QCoreApplication::applicationDirPath()+"/artifacts/signal-lin.png"));
    }
    void settingsVersion3RestoresStopped(){ChannelViewModel vm(settings());QString error;QVERIFY(vm.signalTransmission()->importFile(fixture(),error));QVERIFY(vm.signalTransmission()->setCanOptions(key(),true,1,error));QTemporaryDir dir;SettingsStore store(dir.filePath("channels.json"));QVERIFY(store.save({vm.snapshotSettings()},error));QVector<ChannelSettings>loaded;QVERIFY(store.load(loaded,error));QCOMPARE(loaded.size(),1);ChannelViewModel restored(loaded[0]);QTRY_COMPARE(restored.signalTransmission()->definitions().size(),5);QVERIFY(restored.signalTransmission()->working().frames[key()].enabled);QVERIFY(!restored.signalTransmission()->running());
        auto legacy=loaded[0].toJson();legacy.remove("signalConfiguration");ChannelSettings parsed;QVERIFY(ChannelSettings::fromJson(legacy,parsed,error));QVERIFY(parsed.signalConfiguration.isEmpty());
    }
};
QTEST_MAIN(SignalUiTest)
#include "test_signal_ui.moc"
