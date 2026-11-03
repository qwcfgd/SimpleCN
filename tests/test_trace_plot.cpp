#include <QtTest>
#include <QApplication>
#include <QAbstractItemModelTester>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QTableView>
#include <QTreeView>
#include <QTreeWidget>
#include <QScrollBar>
#include <QLineEdit>
#include <QLabel>
#include <QWheelEvent>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QTableWidget>
#include <QDialogButtonBox>
#include <QToolButton>
#include <QMenu>
#include <QFileDialog>
#include <QInputDialog>
#include <cmath>
#include "model/FrameTableModel.h"
#include "model/DatabaseImporter.h"
#include "model/SignalCodec.h"
#include "infrastructure/TraceExporter.h"
#include "infrastructure/TraceReader.h"
#include "views/UiLanguageController.h"
#include "localization/Language.h"
#include "viewmodels/SignalPlotModel.h"
#include "views/SignalPlotDialog.h"
#include "views/SignalPlotCanvas.h"
#include "views/ChannelPage.h"
#include "model/FrameFilterProxy.h"
using namespace host;
using namespace host::signal;
class TracePlotTest : public QObject {
    Q_OBJECT
    QString fixture()const{return QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic.dbc";}
    QString artifacts()const{return QCoreApplication::applicationDirPath()+"/artifacts/trace";}
    FrameRecord frame(qint64 us=0,int first=2,int second=10,quint32 id=291){
        FrameRecord r;r.captureUs=us;r.timeUs=us;r.epochMs=1789718400000LL+us/1000;r.timestamp="12:00:00.000";
        r.bus=Bus::Can;r.channel="CAN1";r.id=id;r.typed=true;r.payload=QByteArray(8,0);r.payload[0]=char(first);r.payload[1]=char(second);r.length=8;
        r.identifier="0x"+QString::number(id,16).toUpper();r.data=r.payload.toHex(' ').toUpper();r.direction="RX";return r;
    }
    void idFilter(FrameFilterProxy&proxy,bool enabled,bool pass,const QVector<FrameIdRange>&ranges){
        FrameColumnFilter f;f.all=!pass;
        if(enabled){for(const auto&r:ranges){FrameCondition c;c.op=pass?FrameFilterOp::Between:FrameFilterOp::NotBetween;c.value=QString::number(r.first,16);c.upper=QString::number(r.last,16);f.conditions.append(c);}if(pass&&ranges.isEmpty())f.valuesEnabled=true;}
        QVERIFY(proxy.setColumnFilter(5,f));
    }
private slots:
    void initTestCase(){QApplication::setStyle("Fusion");QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");QApplication::setFont(QFont("Microsoft YaHei",9));QDir().mkpath(artifacts());}
    void idRangeParsing(){
        FrameIdRange range;
        for(const auto &rule:QVector<QPair<QString,int>>{{"0x120 - 12F",16},{"100100000-0b100101111",2},{"0o440-457",8},{"288 - 303",10}}){
            QVERIFY(parseFrameIdRange(rule.first,rule.second,0x1fffffff,range));QCOMPARE(range.first,quint32(0x120));QCOMPARE(range.last,quint32(0x12f));
        }
        QVERIFY(parseFrameIdRange("1FFFFFFF",16,0x1fffffff,range));QCOMPARE(range.last,quint32(0x1fffffff));
        QVERIFY(parseFrameIdRange("3F",16,0x3f,range));
        for(const auto &text:QStringList{""," ","-1","+1","1-","1-2-3","2-1","1G","20000000","FFFFFFFFFFFFFFFFF","1 2"})QVERIFY2(!parseFrameIdRange(text,16,0x1fffffff,range),qPrintable(text));
        QVERIFY(!parseFrameIdRange("40",16,0x3f,range));QVERIFY(!parseFrameIdRange("102",2,0x3f,range));QVERIFY(!parseFrameIdRange("8",8,0x3f,range));
        QVERIFY(!parseFrameIdRange("0x10",10,0x3f,range));QVERIFY(!parseFrameIdRange("1",3,0x3f,range));
    }
    void idFilterCompleteHistoryAndSignalChildren(){
        FrameTableModel model;FrameFilterProxy proxy;proxy.setSourceModel(&model);
        FrameBatch batch;for(int n=0;n<FrameTableModel::Capacity+8;++n)batch.append(frame(n*1000,2,10,0x120+n%4));
        model.append(batch);idFilter(proxy,true,true,{{0x121,0x122}});QCOMPARE(proxy.rowCount(),5000);
        QCOMPARE(proxy.filteredHistory().size(),5004);QCOMPARE(model.historyCount(),qint64(10008));
        for(const auto &format:QStringList{"asc","blf"}){QString error;const auto path=artifacts()+"/filtered-cache."+format;
            QVERIFY2(TraceExporter::write(path,proxy.filteredHistory(),error),qPrintable(error));const auto read=TraceReader::read(path);QVERIFY2(read.error.isEmpty(),qPrintable(read.error));
            // Replay readers normalize their first event to zero; exported timestamps retain the capture origin.
            QCOMPARE(read.frames.size(),5004);QCOMPARE(read.frames.first().timeUs,qint64(0));QCOMPARE(read.frames.last().timeUs,qint64(10005000));
            if(format=="asc"){QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.readAll().contains("0.001000 1 121 Rx"));}}
        model.setPaused(true);model.append({frame(10008000,2,10,0x121)});QCOMPARE(proxy.rowCount(),5000);QCOMPARE(proxy.filteredHistory().size(),5005);
        idFilter(proxy,true,false,{{0x121,0x122}});QCOMPARE(proxy.filteredHistory().size(),5004);
        idFilter(proxy,true,true,{});QCOMPARE(proxy.rowCount(),0);QVERIFY(proxy.filteredHistory().isEmpty());
        idFilter(proxy,true,false,{});QCOMPARE(proxy.rowCount(),10000);QCOMPARE(proxy.filteredHistory().size(),10009);
        idFilter(proxy,false,true,{{0x121,0x122}});QCOMPARE(proxy.rowCount(),10000);
        model.setPaused(false);model.clear();QAbstractItemModelTester tester(&proxy,QAbstractItemModelTester::FailureReportingMode::QtTest);model.setDatabase(DatabaseImporter::load(fixture(),Bus::Can).database);model.setRolling(true);
        model.append({frame(),frame(1000),frame(2000,2,10,0x124)});idFilter(proxy,true,true,{{0x123,0x123}});
        QCOMPARE(proxy.rowCount(),1);QCOMPARE(proxy.rowCount(proxy.index(0,0)),5);QCOMPARE(proxy.filteredHistory().size(),2);
        FrameColumnFilter text;FrameCondition condition;condition.value="Small";text.conditions.append(condition);proxy.setColumnFilter(7,text);QCOMPARE(proxy.rowCount(),0);QVERIFY(proxy.filteredHistory().isEmpty());
        condition.value="02 0A";text.conditions={condition};proxy.setColumnFilter(7,text);QCOMPARE(proxy.rowCount(),1);QCOMPARE(proxy.filteredHistory().size(),2);
        auto parent=proxy.index(0,0);QCOMPARE(proxy.selectedFrames({parent,proxy.index(1,5,parent)}).size(),1);
        idFilter(proxy,true,false,{{0x123,0x123}});QCOMPARE(proxy.rowCount(),1);QCOMPARE(proxy.filteredHistory().size(),1);
        proxy.setColumnFilter(7,{});QCOMPARE(proxy.rowCount(),1);QCOMPARE(proxy.filteredHistory().size(),1);
        idFilter(proxy,false,true,{});QCOMPARE(proxy.rowCount(),2);
        const auto selected=proxy.selectedFrames({proxy.index(1,7),proxy.index(0,0),proxy.index(1,0)});QCOMPARE(selected.size(),2);QCOMPARE(selected[0].captureUs,qint64(1000));
    }
    void shiftSelectionAndExportScopes(){
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
        auto settings=ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;ChannelViewModel vm(settings);ChannelPage page(&vm);page.resize(1460,980);page.show();QCoreApplication::processEvents();
        auto *tree=page.findChild<QTreeView*>("frameTable");auto *proxy=static_cast<FrameFilterProxy*>(tree->model());
        FrameBatch batch;for(int n=0;n<8;++n)batch.append(frame(n*1000,2,10,0x120+n));vm.frames()->append(batch);QCoreApplication::processEvents();
        auto click=[&](int row,Qt::KeyboardModifiers modifiers){const auto index=proxy->index(row,5);tree->scrollTo(index);QCoreApplication::processEvents();QTest::mouseClick(tree->viewport(),Qt::LeftButton,modifiers,tree->visualRect(index).center());};
        click(1,Qt::NoModifier);click(4,Qt::ShiftModifier);QCOMPARE(tree->selectionModel()->selectedRows().size(),4);
        const auto selectedPath=artifacts()+"/selected-rows.csv";QFile::remove(selectedPath);
        QTimer driver;driver.setInterval(20);connect(&driver,&QTimer::timeout,&page,[&]{
            if(auto *menu=page.findChild<QMenu*>("frameContextMenu")){if(menu->isVisible()){QCOMPARE(tree->selectionModel()->selectedRows().size(),4);QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(menu->actions()[0]).center());return;}}
            if(auto *file=page.findChild<QFileDialog*>()){file->selectNameFilter("CSV (*.csv)");file->findChild<QLineEdit*>("fileNameEdit")->setText(selectedPath);QMetaObject::invokeMethod(file,"accept");}
        });driver.start();
        const auto point=tree->visualRect(proxy->index(2,5)).center();QMetaObject::invokeMethod(tree,"customContextMenuRequested",Q_ARG(QPoint,point));driver.stop();
        QFile selected(selectedPath);QVERIFY2(selected.open(QIODevice::ReadOnly),qPrintable(vm.logs().join('\n')));const auto bytes=selected.readAll();QCOMPARE(bytes.count('\n'),5);QVERIFY(bytes.contains("0x121"));QVERIFY(bytes.contains("0x124"));QVERIFY(!bytes.contains("0x120"));
        idFilter(*proxy,true,true,{{0x122,0x123}});
        for(int scope=0;scope<2;++scope){
            const auto path=artifacts()+QString("/scope-%1.csv").arg(scope);QFile::remove(path);
            QTimer exportDriver;exportDriver.setInterval(20);connect(&exportDriver,&QTimer::timeout,&page,[&]{
                if(auto *input=page.findChild<QInputDialog*>()){if(input->isVisible()){input->findChild<QComboBox*>()->setCurrentIndex(scope);input->accept();return;}}
                if(auto *file=page.findChild<QFileDialog*>()){file->selectNameFilter("CSV (*.csv)");file->findChild<QLineEdit*>("fileNameEdit")->setText(path);QMetaObject::invokeMethod(file,"accept");}
            });exportDriver.start();page.findChild<QPushButton*>("exportFrames")->click();exportDriver.stop();
            QFile file(path);QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(vm.logs().join('\n')));QCOMPARE(file.readAll().count('\n'),scope?3:9);
        }
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,false);
    }
    void rollingHistoryAndNibbles(){
        FrameTableModel model;QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setRolling(true);model.append({frame()});
        for(int n=1;n<=10;++n){model.append({frame(n*1000)});QCOMPARE(model.rowCount(),1);QCOMPARE(model.index(0,7).data(FrameTableModel::NibbleAgeRole).toList()[0].toInt(),n);}
        model.append({frame(11000,3)});const auto ages=model.index(0,7).data(FrameTableModel::NibbleAgeRole).toList();
        QCOMPARE(ages[0].toInt(),10);QCOMPARE(ages[1].toInt(),0);
        model.append({frame(12000,7,10,292)});QCOMPARE(model.rowCount(),2);QCOMPARE(model.index(0,7).data(FrameTableModel::NibbleAgeRole).toList()[1].toInt(),0);
        auto ext=frame(13000);ext.extended=true;model.append({ext});QCOMPARE(model.rowCount(),3);
        auto other=frame(14000);other.channel="CAN2";model.append({other});QCOMPARE(model.rowCount(),4);
        QCOMPARE(model.history().size(),15);model.setRolling(false);QCOMPARE(model.rowCount(),15);
        QCOMPARE(model.index(14,1).data().toString(),QString("0.014"));QCOMPARE(model.index(14,2).data().toString(),QString("0.001"));
        model.setRolling(true);QCOMPARE(model.rowCount(),4);
    }
    void databaseChildrenAndReload(){
        FrameTableModel model;auto imported=DatabaseImporter::load(fixture(),Bus::Can);QVERIFY(imported.database);
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setDatabase(imported.database);model.setRolling(true);model.append({frame()});
        auto root=model.index(0,0);QCOMPARE(model.rowCount(root),5);
        QCOMPARE(model.index(1,5,root).data().toString(),QString("Small"));
        QCOMPARE(model.index(2,7,root).data().toString(),QString("1"));
        QCOMPARE(model.parent(model.index(1,5,root)),root);
        model.append({frame(1000,2,20)});QCOMPARE(model.index(2,7,root).data().toString(),QString("2"));
        model.setDatabase({});QCOMPARE(model.rowCount(model.index(0,0)),0);
        model.setDatabase(imported.database);QCOMPARE(model.rowCount(model.index(0,0)),5);
        model.clear();QCOMPARE(model.rowCount(),0);
    }
    void repeatedPayloadKeepsValidityAndDatabaseUpdates(){
        FrameTableModel model;auto imported=DatabaseImporter::load(fixture(),Bus::Can);QVERIFY(imported.database);
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setDatabase(imported.database);model.setRolling(true);
        auto value=[&]{return model.index(2,7,model.index(0,0)).data().toString();};
        model.append({frame()});model.append({frame(1000)});QCOMPARE(value(),QString("1"));
        QCOMPARE(model.index(0,7).data(FrameTableModel::NibbleAgeRole).toList()[0].toInt(),1);
        auto invalid=frame(2000);invalid.error=true;model.append({invalid});QCOMPARE(value(),QString("无有效数据"));
        QCOMPARE(model.index(0,7).data(FrameTableModel::NibbleAgeRole).toList()[0].toInt(),0);
        invalid=frame(3000);invalid.noResponse=true;model.append({invalid});QCOMPARE(value(),QString("无有效数据"));
        invalid=frame(4000);invalid.rtr=true;model.append({invalid});QCOMPARE(value(),QString("无有效数据"));
        model.append({frame(5000)});QCOMPARE(value(),QString("1"));
        invalid=frame(6000);invalid.payload.resize(1);invalid.length=1;model.append({invalid});QCOMPARE(value(),QString("无有效数据"));
        model.append({frame(7000)});QCOMPARE(value(),QString("1"));
        auto changed=QSharedPointer<DatabaseDefinition>::create(*imported.database);
        changed->frames[0].fields[1].factor="0.2";model.setDatabase(changed);QCOMPARE(value(),QString("2"));
        model.append({frame(8000)});QCOMPARE(value(),QString("2"));
        model.setPaused(true);model.setDatabase(imported.database);model.setPaused(false);QCOMPARE(value(),QString("1"));
    }
    void parentIndicesSurviveCapacityRebuildAndClear(){
        FrameTableModel model;auto imported=DatabaseImporter::load(fixture(),Bus::Can);QVERIFY(imported.database);
        model.setDatabase(imported.database);model.setRolling(true);FrameBatch batch;
        for(int n=0;n<=FrameTableModel::Capacity;++n){auto r=frame(n*1000);r.channel=QString::number(n);batch.append(r);}
        model.append(batch);QCOMPARE(model.rowCount(),FrameTableModel::Capacity);
        QCOMPARE(model.index(0,3).data().toString(),QString("1"));
        for(int row:{0,5000,9999}){const auto root=model.index(row,0);QCOMPARE(model.rowCount(root),5);QCOMPARE(model.index(1,5,root).parent(),root);}
        model.clear();model.append({frame()});const auto root=model.index(0,0);QCOMPARE(model.index(1,5,root).parent(),root);
    }
    void sameHistoryPositionCanGrowWithoutRedundantResets(){
        FrameTableModel model;model.append({frame()});model.setPaused(true);
        QSignalSpy reset(&model,&QAbstractItemModel::modelReset);
        model.setHistoryStart(0);QCOMPARE(reset.count(),0);
        model.append({frame(1000)});QCOMPARE(model.rowCount(),1);
        model.setHistoryStart(0);QCOMPARE(model.rowCount(),2);QCOMPARE(reset.count(),1);
        model.setHistoryStart(-1);QCOMPARE(reset.count(),1);
    }
    void boundedDisplayWithCompleteHistory(){
        FrameTableModel model;model.setRolling(true);FrameBatch batch;
        for(int n=0;n<FrameTableModel::Capacity+50;++n)batch.append(frame(n*1000));
        model.append(batch);QCOMPARE(model.rowCount(),1);QCOMPARE(model.history().size(),FrameTableModel::Capacity+50);
        model.setRolling(false);QCOMPARE(model.rowCount(),FrameTableModel::Capacity);
        QCOMPARE(model.index(0,1).data().toString(),QString("0.05"));
    }
    void completeCachePauseBrowseExportAndClear(){
        FrameTableModel model;FrameBatch batch;
        for(int n=0;n<30050;++n)batch.append(frame(qint64(n)*1000));
        model.append(batch);QCOMPARE(model.historyCount(),qint64(30050));QCOMPARE(model.rowCount(),10000);
        QCOMPARE(model.index(0,1).data().toString(),QString("20.05"));
        QCOMPARE(model.index(0,1).data(FrameTableModel::RelativeTimeSecondsRole).toDouble(),20.05);
        model.setPaused(true);const auto frozen=model.index(9999,1).data();
        QSignalSpy inserted(&model,&QAbstractItemModel::rowsInserted),changed(&model,&QAbstractItemModel::dataChanged),reset(&model,&QAbstractItemModel::modelReset),recorded(&model,&FrameTableModel::recorded);
        model.append({frame(30050000),frame(30051000)});
        QCOMPARE(model.historyCount(),qint64(30052));QCOMPARE(model.index(9999,1).data(),frozen);
        QCOMPARE(inserted.count(),0);QCOMPARE(changed.count(),0);QCOMPARE(reset.count(),0);QCOMPARE(recorded.count(),1);
        model.setHistoryStart(0);QCOMPARE(model.rowCount(),10000);QCOMPARE(model.index(0,1).data().toString(),QString("0"));
        model.setHistoryStart(15000);QCOMPARE(model.index(0,1).data().toString(),QString("15"));QCOMPARE(model.index(9999,1).data().toString(),QString("24.999"));
        QString error;
        for(const auto &format:QStringList{"csv","asc","blf"}){
            const auto path=artifacts()+"/complete-cache."+format;QVERIFY2(model.exportTrace(path,error),qPrintable(error));
            if(format=="csv"){
                QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();
                QCOMPARE(bytes.count('\n'),30053);QVERIFY(bytes.contains(QString("时刻/s").toUtf8()));QVERIFY(bytes.contains("\"30.051\""));
            }else{
                const auto log=TraceReader::read(path);QVERIFY2(log.error.isEmpty(),qPrintable(log.error));
                QCOMPARE(log.frames.size(),30052);QCOMPARE(log.frames.first().timeUs,qint64(0));QCOMPARE(log.frames.last().timeUs,qint64(30051000));
            }
        }
        model.setPaused(false);QCOMPARE(model.windowStart(),qint64(20052));QCOMPARE(model.index(9999,1).data().toString(),QString("30.051"));
        model.setPaused(true);model.clear();QCOMPARE(model.historyCount(),qint64(0));QCOMPARE(model.rowCount(),0);
        model.append({frame(90000000),frame(91000000)});QCOMPARE(model.historyCount(),qint64(2));QCOMPARE(model.rowCount(),0);
        model.setPaused(false);QCOMPARE(model.index(0,1).data().toString(),QString("0"));QCOMPARE(model.index(1,1).data().toString(),QString("1"));QCOMPARE(model.index(1,2).data().toString(),QString("1"));
    }
    void pauseControlsAndLanguage(){
        UiLanguageController::instance();auto settings=ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;
        ChannelViewModel vm(settings);ChannelPage page(&vm);page.resize(1460,980);page.show();QCoreApplication::processEvents();
        auto *pause=page.findChild<QCheckBox*>("framePause"),*rolling=page.findChild<QCheckBox*>("frameRolling");
        auto *scroll=page.findChild<QScrollBar*>("frameHistoryScroll");auto *tree=page.findChild<QTreeView*>("frameTable");
        QVERIFY(pause&&scroll&&tree);QVERIFY(!pause->isChecked());QVERIFY(!scroll->isVisible());
        FrameBatch batch;for(int n=0;n<20000;++n)batch.append(frame(qint64(n)*1000));vm.frames()->append(batch);
        pause->setChecked(true);QCoreApplication::processEvents();QVERIFY(scroll->isVisible());QCOMPARE(scroll->maximum(),10000);QCOMPARE(scroll->pageStep(),10000);
        const int position=tree->verticalScrollBar()->value();const auto shown=tree->model()->index(0,1).data();
        vm.frames()->append({frame(20000000)});QCOMPARE(tree->model()->index(0,1).data(),shown);QCOMPARE(tree->verticalScrollBar()->value(),position);
        scroll->setValue(0);QCOMPARE(tree->model()->index(0,1).data().toString(),QString("0"));QCOMPARE(tree->verticalScrollBar()->value(),0);
        auto *filter=static_cast<FrameFilterProxy*>(tree->model());FrameColumnFilter condition;FrameCondition c;c.value="no match";condition.conditions.append(c);filter->setColumnFilter(7,condition);QCOMPARE(tree->model()->rowCount(),0);QCOMPARE(vm.frames()->historyCount(),qint64(20001));filter->setColumnFilter(7,{});
        Language::instance().setCode("en");QCoreApplication::processEvents();QCOMPARE(pause->text(),QString("Pause updates"));
        QVERIFY(!pause->toolTip().contains(QString("暂停")));QVERIFY(scroll->toolTip().startsWith("Records "));QCOMPARE(scroll->accessibleName(),QString("Browse recorded frames"));
        QCOMPARE(tree->model()->headerData(1,Qt::Horizontal).toString(),QString("Relative time/s"));
        QCOMPARE(tree->model()->headerData(2,Qt::Horizontal).toString(),QString("Delta time/s"));
        page.grab().save(artifacts()+"/history-paused-en.png");Language::instance().setCode("zh_CN");QCoreApplication::processEvents();QCOMPARE(pause->text(),QString("暂停更新"));
        page.grab().save(artifacts()+"/history-paused-zh.png");pause->setChecked(false);QVERIFY(!scroll->isVisible());QVERIFY(rolling->isEnabled());
        rolling->setChecked(true);pause->setChecked(true);QCOMPARE(vm.frames()->rowCount(),10000);QVERIFY(!rolling->isEnabled());
        pause->setChecked(false);QCOMPARE(vm.frames()->rowCount(),1);QVERIFY(vm.frames()->rolling());
    }
    void pausedWindowKeepsSizeWhileCacheGrows(){
        auto settings=ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;ChannelViewModel vm(settings);ChannelPage page(&vm);page.show();
        FrameBatch initial;for(int n=0;n<100;++n)initial.append(frame(n*1000));vm.frames()->append(initial);
        auto *pause=page.findChild<QCheckBox*>("framePause");auto *scroll=page.findChild<QScrollBar*>("frameHistoryScroll");pause->setChecked(true);
        FrameBatch more;for(int n=100;n<20100;++n)more.append(frame(n*1000));vm.frames()->append(more);
        QCOMPARE(vm.frames()->rowCount(),100);QCOMPARE(scroll->maximum(),20000);QCOMPARE(scroll->pageStep(),100);QVERIFY(scroll->isVisible());
        scroll->setSliderDown(true);scroll->setValue(5000);const auto start=vm.frames()->windowStart();
        vm.frames()->append({frame(20100000)});QCOMPARE(scroll->maximum(),20000);QCOMPARE(vm.frames()->windowStart(),start);
        scroll->setSliderDown(false);QCOMPARE(vm.frames()->windowStart(),start);QCOMPARE(scroll->maximum(),10101);QCOMPARE(scroll->pageStep(),10000);
        vm.frames()->clear();QCOMPARE(scroll->maximum(),0);QVERIFY(!scroll->isVisible());
    }
    void multipleCursorDifferencesAndGaps(){
        SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY(vm.importFile(fixture(),error));SignalPlotModel model(&vm);
        QVERIFY(model.addSignal(frameKey(Bus::Can,291),"Small"));QVERIFY(model.addSignal(frameKey(Bus::Can,291),"Scaled"));
        model.append({frame(0,1,10),frame(1000000,3,20),frame(6000000,5,30),frame(11000001,7,40)});
        QCOMPARE(model.series()[0].points.size(),size_t(4));QCOMPARE(model.valueAt(0,500000).value,2.0);
        QCOMPARE(model.differenceAt(0,0,1000000),QString("2"));QCOMPARE(model.differenceAt(1,0,1000000),QString("1"));
        QVERIFY(model.valueAt(0,5000000).valid);QVERIFY(!model.valueAt(0,8000000).valid);
        QVERIFY(model.valueAt(0,11000001).valid);QVERIFY(!model.valueAt(0,12000000).valid);
        model.setCursors(true,true,0,1000000);QCOMPARE(model.index(0,6).data().toString(),QString("1"));QCOMPARE(model.index(1,7).data().toString(),QString("1"));
        auto invalid=frame(12000000);invalid.error=true;model.append({invalid,frame(13000000)});
        QVERIFY(!model.valueAt(0,12500000).valid);
        QCOMPARE(SignalCodec::differenceText("18446744073709551615","18446744073709551614"),QString("1"));
    }
    void allSamplesAndEchoDeduplication(){
        SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY(vm.importFile(fixture(),error));SignalPlotModel model(&vm);model.addSignal(frameKey(Bus::Can,291),"Scaled");
        FrameBatch frames;for(int n=0;n<40;++n)frames.append(frame(n*1000,2,n));vm.observeFrames(frames);QCOMPARE(model.series()[0].points.size(),size_t(40));
        auto request=frame(50000);request.request=true;auto echo=request;echo.request=false;echo.echo=true;echo.timeUs=echo.captureUs=51000;
        model.append({request,echo});QCOMPARE(model.series()[0].points.size(),size_t(41));
        QVERIFY(model.addSignal(frameKey(Bus::Can,291),"Small"));QCOMPARE(model.series()[0].points.size(),size_t(41));QCOMPARE(model.series()[1].points.size(),size_t(40));
        vm.clearTrace();QVERIFY(model.series()[0].points.empty());
    }
    void unrelatedFramesAndSuppressedEchoDoNotRefreshPlots(){
        SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY(vm.importFile(fixture(),error));SignalPlotModel model(&vm);
        QVERIFY(model.addSignal(frameKey(Bus::Can,291),"Scaled"));const auto initial=model.revision();
        model.append({frame(0,2,10,292)});model.append({});QCOMPARE(model.revision(),initial);
        auto request=frame(1000);request.request=true;model.append({request});QVERIFY(model.revision()>initial);
        const auto recorded=model.revision();const auto points=model.series()[0].points.size();
        auto echo=frame(2000);echo.echo=true;model.append({echo});QCOMPARE(model.revision(),recorded);QCOMPARE(model.series()[0].points.size(),points);
        auto errorFrame=frame(3000);errorFrame.error=true;model.append({errorFrame});QVERIFY(model.revision()>recorded);QVERIFY(!model.series()[0].points.back().valid);
    }
    void monitorControls(){
        auto settings=ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;
        ChannelViewModel vm(settings);ChannelPage page(&vm);page.resize(1366,800);page.show();
        auto *tree=page.findChild<QTreeView*>("frameTable");QVERIFY(tree);
        QVERIFY(tree->isColumnHidden(0));QVERIFY(!tree->isColumnHidden(1));QVERIFY(tree->isColumnHidden(2));QVERIFY(!page.findChild<QCheckBox*>("frameRolling")->isChecked());
        page.findChild<QCheckBox*>("frameTimeT")->setChecked(true);QVERIFY(!tree->isColumnHidden(0));
        QString error;QVERIFY(vm.signalTransmission()->importFile(fixture(),error));
        page.findChild<QCheckBox*>("frameRolling")->setChecked(true);vm.frames()->append({frame(),frame(1000,3)});
        tree->expandAll();QCOMPARE(vm.frames()->rowCount(),1);QTest::qWait(50);QVERIFY(page.grab().save(artifacts()+"/monitor.png"));
    }
    void plotInteractionAndScreenshots(){
        SignalTransmitViewModel vm(Bus::Can);QString error;QVERIFY(vm.importFile(fixture(),error));SignalPlotDialog dialog(&vm);auto *model=dialog.plotModel();
        model->addSignal(frameKey(Bus::Can,291),"Scaled");model->addSignal(frameKey(Bus::Can,291),"Signed");
        FrameBatch frames;for(int n=0;n<=160;++n){auto r=frame(n*100000,2,int(45+35*std::sin(n/12.0)));int current=int(40+30*std::cos(n/17.0));r.payload[2]=char(current);frames.append(r);}model->append(frames);
        dialog.show();auto *table=dialog.findChild<QTreeWidget*>("plotSignals");auto *canvas=dialog.findChild<SignalPlotCanvas*>();QVERIFY(table);QVERIFY(canvas);
        table->topLevelItem(0)->setSelected(true);table->topLevelItem(1)->setSelected(true);
        dialog.findChild<QCheckBox*>("plotDifference")->setChecked(true);dialog.findChild<QDoubleSpinBox*>("plotC1")->setValue(7);dialog.findChild<QDoubleSpinBox*>("plotC2")->setValue(12);
        QVERIFY(!table->isColumnHidden(2));QVERIFY(!table->isColumnHidden(3));
        dialog.findChild<QPushButton*>("plotFit")->click();
        auto *axes=dialog.findChild<QComboBox*>("plotAxisMode");auto *display=dialog.findChild<QComboBox*>("plotDisplayMode");
        for(int n=0;n<3;++n){axes->setCurrentIndex(n);QTest::qWait(50);QVERIFY(dialog.grab().save(artifacts()+QString("/plot-%1.png").arg(n)));}
        display->setCurrentIndex(1);QCOMPARE(canvas->visibleRows().size(),2);table->clearSelection();QCOMPARE(canvas->visibleRows().size(),0);display->setCurrentIndex(0);
        axes->setCurrentIndex(0);auto before=canvas->xRange();const QPointF position(canvas->width()/2,canvas->height()-25);
        QWheelEvent wheel(position,canvas->mapToGlobal(position.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(canvas,&wheel);QVERIFY(canvas->xRange().second-canvas->xRange().first<before.second-before.first);
        table->clearSelection();table->topLevelItem(0)->setSelected(true);table->setFocus();QTest::keyClick(table,Qt::Key_Delete);QCOMPARE(model->rowCount(),1);
    }
    void exportPreservesInputAndStableTimeOrder(){
        const FrameBatch input{frame(2000,2,10,0x125),frame(1000,2,10,0x124),frame(1000,2,10,0x123)};
        QString error;
        for(const auto &format:QStringList{"asc","blf"}){
            const auto path=artifacts()+"/unsorted."+format;QVERIFY2(TraceExporter::write(path,input,error),qPrintable(error));
            const auto read=TraceReader::read(path);QVERIFY2(read.error.isEmpty(),qPrintable(read.error));QCOMPARE(read.frames.size(),3);
            QCOMPARE(read.frames[0].id,quint32(0x124));QCOMPARE(read.frames[1].id,quint32(0x123));QCOMPARE(read.frames[2].id,quint32(0x125));
            const auto sortedPath=artifacts()+"/sorted."+format;QVERIFY2(TraceExporter::write(sortedPath,read.frames,error),qPrintable(error));
            const auto reread=TraceReader::read(sortedPath);QCOMPARE(reread.frames.size(),3);QCOMPARE(reread.frames[0].id,quint32(0x124));QCOMPARE(reread.frames[1].id,quint32(0x123));
        }
        QCOMPARE(input[0].id,quint32(0x125));QCOMPARE(input[0].timeUs,qint64(2000));
        const auto csv=artifacts()+"/unsorted.csv";QVERIFY2(TraceExporter::write(csv,input,error),qPrintable(error));
        QFile file(csv);QVERIFY(file.open(QIODevice::ReadOnly));const auto contents=file.readAll();
        QVERIFY(contents.indexOf("0x125")<contents.indexOf("0x124"));QVERIFY(contents.indexOf("0x124")<contents.indexOf("0x123"));
    }
    void exports(){
        FrameBatch rows;auto can=frame();rows.append(can);can.timeUs=can.captureUs=1000;can.extended=true;rows.append(can);
        can.timeUs=2000;can.extended=false;can.fd=true;can.brs=true;can.payload=QByteArray(32,char(0x5A));can.length=32;rows.append(can);
        can=frame(3000);can.rtr=true;can.payload.clear();can.length=8;rows.append(can);
        auto lin=frame(4000);lin.bus=Bus::Lin;lin.channel="LIN2";lin.id=0x12;lin.checksum=0x34;rows.append(lin);
        lin.timeUs=5000;lin.noResponse=true;lin.warning=true;lin.payload.clear();lin.status="无从节点响应";rows.append(lin);
        lin.timeUs=6000;lin.noResponse=false;lin.error=true;lin.errorFlags=4;rows.append(lin);
        QString error;for(const auto &format:QStringList{"csv","asc","blf"})QVERIFY2(TraceExporter::write(artifacts()+"/mixed."+format,rows,error),qPrintable(error));
        QFile blf(artifacts()+"/mixed.blf");QVERIFY(blf.open(QIODevice::ReadOnly));QCOMPARE(blf.read(4),QByteArray("LOGG"));
        auto bad=frame();bad.length=64;QVERIFY(!TraceExporter::write(artifacts()+"/bad.blf",{bad},error));
    }
    void suppliedAscExport(){
        QFile file(QString(TEST_SOURCE_DIR)+"/../testsrc/test.asc");if(!file.open(QIODevice::ReadOnly))QSKIP("Optional sample");
        FrameBatch rows;QJsonArray expected;
        while(!file.atEnd()&&rows.size()<3000){
            const auto tokens=QString::fromLatin1(file.readLine()).simplified().split(' ');bool ok=false;const double seconds=tokens.value(0).toDouble(&ok);if(!ok)continue;
            FrameRecord r;r.timeUs=qRound64(seconds*1e6);r.epochMs=1789718400000LL+r.timeUs/1000;r.typed=true;int dataStart=0;
            if(tokens.value(1)=="CANFD"){r.fd=true;r.channel=tokens.value(2);r.id=tokens.value(3).toUInt(nullptr,16);r.direction=tokens.value(4);r.brs=tokens.value(5)=="1";r.esi=tokens.value(6)=="1";r.length=tokens.value(9).toInt();dataStart=10;}
            else if(tokens.value(1).startsWith('L')){r.bus=Bus::Lin;r.channel=tokens.value(1);r.id=tokens.value(2).toUInt(nullptr,16);r.direction=tokens.value(3);r.length=tokens.value(4).toInt(&ok);if(!ok)continue;dataStart=5;r.checksum=tokens.last().toInt(nullptr,16);}
            else {r.channel=tokens.value(1);r.id=tokens.value(2).toUInt(nullptr,16);r.direction=tokens.value(3);if(tokens.value(4)!="d")continue;r.length=tokens.value(5).toInt();dataStart=6;}
            for(int n=0;n<r.length;++n)r.payload.append(char(tokens.value(dataStart+n).toUInt(nullptr,16)));
            if(r.length<0||r.length>64)continue;
            rows.append(r);expected.append(QJsonObject{{"bus",r.bus==Bus::Lin?"LIN":"CAN"},{"id",int(r.id)},{"fd",r.fd},{"channel",r.channel},{"tx",r.direction.compare("Tx",Qt::CaseInsensitive)==0},{"us",double(r.timeUs)},{"data",QString(r.payload.toHex())}});
        }
        QVERIFY(rows.size()==3000);QString error;for(const auto &format:QStringList{"asc","blf"})QVERIFY2(TraceExporter::write(artifacts()+"/sample."+format,rows,error),qPrintable(error));
        QFile manifest(artifacts()+"/sample.json");QVERIFY(manifest.open(QIODevice::WriteOnly));manifest.write(QJsonDocument(expected).toJson());
    }
};
QTEST_MAIN(TracePlotTest)
#include "test_trace_plot.moc"
