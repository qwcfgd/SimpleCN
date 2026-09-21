#include <QtTest>
#include <cmath>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QFontDatabase>
#include "views/SignalTransmitPage.h"
#include "views/PathFileDialog.h"
#include "views/SignalPlotCanvas.h"
#include "views/MainWindow.h"
#include "views/UiLanguageController.h"
#include "localization/Language.h"
#include "domain/TraceClock.h"
using namespace host;
using namespace host::signal;
class Revision141Test:public QObject {
    Q_OBJECT
    QString fixture(bool lin=false){return QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic."+(lin?"ldf":"dbc");}
private slots:
    void initTestCase(){QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QApplication::setFont(QFont("Microsoft YaHei",9));UiLanguageController::instance();}
    void cleanup(){Language::instance().setCode("zh_CN");}
    void editingDuringTraffic_data(){
        QTest::addColumn<bool>("lin");QTest::addColumn<int>("row");QTest::addColumn<int>("column");QTest::addColumn<QString>("input");QTest::addColumn<int>("raw");
        QTest::newRow("CAN raw")<<false<<1<<2<<QString("25")<<25;
        QTest::newRow("CAN physical")<<false<<1<<3<<QString("2.5")<<25;
        QTest::newRow("CAN enum")<<false<<0<<3<<QString("Of")<<0;
        QTest::newRow("LIN raw")<<true<<0<<2<<QString("3")<<3;
        QTest::newRow("LIN enum")<<true<<0<<3<<QString("On")<<2;
    }
    void editingDuringTraffic(){
        QFETCH(bool,lin);QFETCH(int,row);QFETCH(int,column);QFETCH(QString,input);QFETCH(int,raw);
        SignalTransmitViewModel vm(lin?Bus::Lin:Bus::Can);QString error;QVERIFY(vm.importFile(fixture(lin),error));if(!lin)QVERIFY(vm.selectCanNode("Tester","Tx",error));
        vm.setAvailability(true,false,lin?19200:500000,41);SignalTransmitPage page(&vm);page.resize(1400,900);page.show();QCoreApplication::processEvents();
        QVERIFY(vm.start(true,error));auto status=vm.status();status.state=RunState::Running;vm.applyStatus(status);
        auto *table=page.findChild<QTableView*>("signalValues");const auto cell=table->model()->index(row,column);QVERIFY(cell.flags()&Qt::ItemIsEditable);
        QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(cell).center());QCoreApplication::processEvents();
        auto *combo=table->findChild<QComboBox*>("signalEnumEditor");auto *edit=combo?combo->lineEdit():qobject_cast<QLineEdit*>(QApplication::focusWidget());QVERIFY(edit);edit->selectAll();QTest::keyClicks(edit,input);
        const QString draft=edit->text();const int cursor=edit->cursorPosition();
        BusFrameEvent rx;rx.bus=lin?Bus::Lin:Bus::Can;rx.id=lin?16:291;rx.bytes=QByteArray(lin?2:8,0);rx.source=EventSource::Received;rx.valid=true;
        for(int n=0;n<12;++n){vm.applyStatus(status);vm.receive({rx});QTest::qWait(10);QCOMPARE(edit->text(),draft);QCOMPARE(edit->cursorPosition(),cursor);}
        // Language refresh also notifies all model views while the draft is active.
        Language::instance().setCode("en");QCoreApplication::processEvents();QCOMPARE(edit->text(),draft);
        QSignalSpy updates(&vm,&SignalTransmitViewModel::payloadRequested);QTest::keyClick(combo?static_cast<QWidget*>(combo):edit,Qt::Key_Tab);
        QTRY_COMPARE(table->model()->index(row,2).data().toString(),QString::number(raw));QVERIFY(updates.count()>0);
        const auto update=qvariant_cast<PayloadUpdate>(updates.last().first());QCOMPARE(update.connection,quint64(41));QCOMPARE(update.run,vm.status().run);
        QCOMPARE(int(quint8(update.payload.bytes[row==1?1:0]))&(row==0?7:255),raw);
    }
    void liveSimulationPayloadAndTime(){
        auto settings=ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;settings.hardwareKey="preview:CAN:1";settings.handle=0xf101;
        ChannelViewModel channel(settings);auto *vm=channel.signalTransmission();QString error;QVERIFY(vm->importFile(fixture(),error));QVERIFY(vm->selectCanNode("Tester","Tx",error));
        QVERIFY(vm->setCanOptions(frameKey(Bus::Can,291),true,20,error));QTRY_VERIFY(channel.canConnect());channel.toggleConnection();QTRY_VERIFY(channel.connected()&&!channel.pending());
        QVERIFY(vm->start(true,error));QTRY_VERIFY(channel.frames()->history().size()>3);const auto before=channel.frames()->history().last();QElapsedTimer elapsed;elapsed.start();
        QVERIFY(vm->editSignal(frameKey(Bus::Can,291),1,"25",false,error));
        auto updated=[&]{for(const auto&r:channel.frames()->history())if(r.id==291&&r.payload.size()>1&&quint8(r.payload[1])==25)return true;return false;};QTRY_VERIFY(updated());
        QTest::qWait(1000);const auto after=channel.frames()->history().last();const double shownMs=after.relativeTime.toDouble()-before.relativeTime.toDouble();
        QVERIFY(shownMs>900);QVERIFY(std::abs(shownMs-elapsed.elapsed())<150);QCOMPARE(after.relativeTime,QString::number(after.timeUs/1000.0,'f',6));
        vm->stop();QTRY_VERIFY(!vm->running());
    }
    void enumPhysicalTicks(){
        const auto ticks=SignalPlotCanvas::enumTicks({{0,"Off"},{0.5,"Half"},{2,"On"}},-1,4,400);QMap<double,QString> labels;
        for(const auto &tick:ticks)labels[tick.value]=tick.text;
        QCOMPARE(labels.value(0),QString("Off"));QCOMPARE(labels.value(0.5),QString("Half"));QCOMPARE(labels.value(2),QString("On"));
        QCOMPARE(labels.value(-1),QString("-1"));QCOMPARE(labels.value(1),QString("1"));QCOMPARE(labels.value(3),QString("3"));QCOMPARE(labels.value(4),QString("4"));
        for(const auto &tick:SignalPlotCanvas::enumTicks({},0,100000,100)){QCOMPARE(std::floor(tick.value),tick.value);QVERIFY(!tick.text.isEmpty());}
    }
    void pasteFilePaths_data(){QTest::addColumn<QString>("extension");for(auto ext:{"bin","hex","dbc","ldf","cdd"})QTest::newRow(ext)<<QString(ext);}
    void pasteFilePaths(){
        QFETCH(QString,extension);QTemporaryDir dir;const auto folder=dir.filePath("路径 space");QVERIFY(QDir().mkpath(folder));const auto name=folder+"/测试 file."+extension;
        QFile file(name);QVERIFY(file.open(QIODevice::WriteOnly));file.write("test");file.close();Language::instance().setCode("en");
        PathFileDialog dialog(nullptr,"选择镜像",dir.path(),"Files (*."+extension+")");dialog.show();QCoreApplication::processEvents();auto *address=dialog.findChild<QLineEdit*>("fileAddress");QVERIFY(address);QTest::keyClick(&dialog,Qt::Key_L,Qt::ControlModifier);QCoreApplication::processEvents();QCOMPARE(QApplication::focusWidget(),address);
        auto paste=[&](const QString&text){address->setFocus();address->selectAll();QApplication::clipboard()->setText(text);QTest::keyClick(address,Qt::Key_V,Qt::ControlModifier);QTest::keyClick(address,Qt::Key_Return);QCoreApplication::processEvents();};
        paste(folder);QCOMPARE(QDir::cleanPath(dialog.directory().absolutePath()),QDir::cleanPath(folder));QVERIFY(dialog.isVisible());
        paste(folder+"/missing."+extension);QVERIFY(dialog.findChild<QLabel*>("fileAddressError")->isVisible());QVERIFY(dialog.isVisible());
        paste('"'+name+'"');QVERIFY(!dialog.findChild<QLabel*>("fileAddressError")->isVisible());QCOMPARE(dialog.selectedFiles().first(),name);QVERIFY(dialog.isVisible());
        if(extension=="cdd")dialog.grab().save(QCoreApplication::applicationDirPath()+"/revision141-file-picker.png");
        dialog.accept();QCOMPARE(dialog.result(),int(QDialog::Accepted));
    }
    void consistentPageBackground(){
        QTemporaryDir dir;MainWindow window(dir.filePath("project.json"),true);window.resize(1440,950);window.show();QCoreApplication::processEvents();
        for(auto *tabs:window.findChildren<QTabWidget*>("taskPages"))tabs->setCurrentIndex(1);QCoreApplication::processEvents();window.grab().save(QCoreApplication::applicationDirPath()+"/revision141-uds.png");
        for(auto name:{"downloadPanel","udsDiagnosticPage","signalTransmitPage"}){auto *page=window.findChild<QWidget*>(name);QVERIFY(page);const auto image=page->grab().toImage();QCOMPARE(image.pixelColor(image.width()/2,3),QColor(Qt::white));}
    }
};
QTEST_MAIN(Revision141Test)
#include "test_revision141.moc"
