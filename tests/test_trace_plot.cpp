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
#include <QWheelEvent>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <cmath>
#include "model/FrameTableModel.h"
#include "model/DatabaseImporter.h"
#include "model/SignalCodec.h"
#include "infrastructure/TraceExporter.h"
#include "viewmodels/SignalPlotModel.h"
#include "views/SignalPlotDialog.h"
#include "views/SignalPlotCanvas.h"
#include "views/ChannelPage.h"
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
private slots:
    void initTestCase(){QApplication::setStyle("Fusion");QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");QApplication::setFont(QFont("Microsoft YaHei",9));QDir().mkpath(artifacts());}
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
        QCOMPARE(model.index(14,1).data().toString(),QString("14"));QCOMPARE(model.index(14,2).data().toString(),QString("1.000"));
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
    void boundedHistory(){
        FrameTableModel model;model.setRolling(true);FrameBatch batch;
        for(int n=0;n<FrameTableModel::Capacity+50;++n)batch.append(frame(n*1000));
        model.append(batch);QCOMPARE(model.rowCount(),1);QCOMPARE(model.history().size(),FrameTableModel::Capacity);
        model.setRolling(false);QCOMPARE(model.rowCount(),FrameTableModel::Capacity);
        QCOMPARE(model.index(0,1).data().toString(),QString("50"));
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
