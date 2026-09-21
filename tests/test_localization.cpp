#include <QtTest>
#include <QComboBox>
#include <QHeaderView>
#include <QTableView>
#include <QLabel>
#include <QPushButton>
#include <QDialog>
#include <QMouseEvent>
#include <QFontDatabase>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QScrollArea>
#include <QScrollBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include "localization/Language.h"
#include "views/MainWindow.h"
#include "views/UiLanguageController.h"
#include "views/SignalTransmitPage.h"
#include "views/SignalPlotDialog.h"
#include "views/SignalPlotCanvas.h"
#include "model/FrameTableModel.h"
#include "viewmodels/SignalTableModels.h"
using namespace host;
class LocalizationTest:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QApplication::setFont(QFont("Microsoft YaHei",9));}
    void cleanup(){Language::instance().setCode("zh_CN");}
    void catalog(){
        Language::instance().setCode("en");
        QCOMPARE(Language::text("时间"),QString("Time"));
        QFile f(":/i18n/en.json");QVERIFY(f.open(QIODevice::ReadOnly));
        const auto entries=QJsonDocument::fromJson(f.readAll()).object();QVERIFY(entries.size()>900);
        QRegularExpression chinese("[\\x{3400}-\\x{9fff}]"),placeholder("%[1-9][0-9]*");
        for(auto it=entries.begin();it!=entries.end();++it){
            if(it.key()!="简中")QVERIFY2(!it.value().toString().contains(chinese),qPrintable(it.key()));
            QStringList before,after;auto a=placeholder.globalMatch(it.key()),b=placeholder.globalMatch(it.value().toString());
            while(a.hasNext())before<<a.next().captured();while(b.hasNext())after<<b.next().captured();before.sort();after.sort();QCOMPARE(before,after);
            QString expanded=it.key();for(int n=20;n>0;--n)expanded.replace("%"+QString::number(n),QString("arg_%1").arg(n));
            if(it.key()!="简中")QVERIFY2(!Language::text(expanded).contains(chinese),qPrintable(expanded));
        }
        QCOMPARE(Language::text("12:34:56.789  时间"),QString("12:34:56.789  Time"));
    }
    void liveLanguage(){
        QTemporaryDir dir;MainWindow window(dir.filePath("settings.json"),true);window.show();
        auto *selector=window.findChild<QComboBox*>("languageSelector");QVERIFY(selector);QCOMPARE(selector->itemText(0),QString("简中"));QCOMPARE(selector->itemText(1),QString("Eng"));
        auto *input=window.findChild<QLineEdit*>("applicationPath");QVERIFY(input);input->setText("中文用户文件.bin");
        window.resize(1440,950);
        const auto count=window.channels().size();selector->setCurrentIndex(1);QCoreApplication::processEvents();
        UiLanguageController::instance().refresh();QCOMPARE(window.channels().size(),count);QCOMPARE(input->text(),QString("中文用户文件.bin"));
        QRegularExpression chinese("[\\x{3400}-\\x{9fff}]");
        for(auto *label:window.findChildren<QLabel*>())QVERIFY2(!label->text().contains(chinese),qPrintable(label->objectName()+": "+label->text()));
        for(auto *button:window.findChildren<QPushButton*>())QVERIFY2(!button->text().contains(chinese),qPrintable(button->text()));
        QDialog dialog(&window);QPushButton dynamic("取消",&dialog);dialog.show();QCoreApplication::processEvents();QCOMPARE(dynamic.text(),QString("Cancel"));
        window.grab().save(QCoreApplication::applicationDirPath()+"/language-en.png");
        selector->setCurrentIndex(0);QVERIFY(!Language::instance().english());QCOMPARE(dynamic.text(),QString("取消"));QCOMPARE(input->text(),QString("中文用户文件.bin"));
    }
    void columns_data(){QTest::addColumn<bool>("lin");QTest::newRow("CAN")<<false;QTest::newRow("LIN")<<true;}
    void columns(){
        QFETCH(bool,lin);const auto bus=lin?signal::Bus::Lin:signal::Bus::Can;
        SignalTransmitViewModel vm(bus);SignalTransmitPage page(&vm);page.resize(1400,800);page.show();QCoreApplication::processEvents();
        for(const auto name:{"signalPlanTable","signalValues"}){
            auto *table=page.findChild<QTableView*>(name);QVERIFY(table);auto *header=table->horizontalHeader();QVERIFY(header->sectionsMovable());QVERIFY(header->count()>2);
            QCOMPARE(header->sectionResizeMode(0),QHeaderView::Interactive);
            header->resizeSection(0,177);
            const QPoint start(header->sectionViewportPosition(0)+header->sectionSize(0)/2,header->height()/2);
            const QPoint end(header->sectionViewportPosition(2)+header->sectionSize(2)-10,header->height()/2);
            QTest::mousePress(header->viewport(),Qt::LeftButton,Qt::NoModifier,start);auto move=[&](QPoint point){QMouseEvent event(QEvent::MouseMove,QPointF(point),QPointF(header->viewport()->mapToGlobal(point)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(header->viewport(),&event);};move(start+QPoint(15,0));move(end);QTest::mouseRelease(header->viewport(),Qt::LeftButton,Qt::NoModifier,end);QCOMPARE(header->visualIndex(0),2);
            Language::instance().setCode("en");UiLanguageController::instance().refresh();QCOMPARE(header->sectionSize(0),177);QCOMPARE(header->visualIndex(0),2);
        }
        const auto saved=vm.configuration();SignalTransmitViewModel restored(bus);QString error;QVERIFY2(restored.readConfiguration(saved,QString(),error),qPrintable(error));SignalTransmitPage other(&restored);
        for(const auto name:{"signalPlanTable","signalValues"}){auto*h=other.findChild<QTableView*>(name)->horizontalHeader();QCOMPARE(h->visualIndex(0),2);QCOMPARE(h->sectionSize(0),177);}
    }
    void fractionalMicroseconds(){
        FrameTableModel model;FrameRecord start;start.relativeTime="0";FrameRecord next;next.relativeTime="1200123.674";model.append({start,next});
        QCOMPARE(model.index(1,1).data().toString(),QString("1200.123674"));
        model.clear();start.captureUs=1000;next.captureUs=1201123;model.append({start,next});QCOMPARE(model.index(1,1).data().toString(),QString("1200.123000"));
    }
    void singleClickEditing(){
        SignalTransmitViewModel vm(signal::Bus::Can);QString error;
        QVERIFY(vm.importFile(QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic.dbc",error));
        QVERIFY(vm.selectCanNode("Tester","Tx",error));SignalTransmitPage page(&vm);page.resize(1500,950);page.show();QCoreApplication::processEvents();
        auto *table=page.findChild<QTableView*>("signalValues");QVERIFY(table->model()->rowCount()>1);
        for(int column:{2,3}){
            const auto index=table->model()->index(1,column);QVERIFY(index.flags()&Qt::ItemIsEditable);
            table->scrollTo(index);QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(index).center());
            QCoreApplication::processEvents();auto *editor=QApplication::focusWidget();QVERIFY(editor);QVERIFY(table->isAncestorOf(editor));QVERIFY(qobject_cast<QLineEdit*>(editor)||qobject_cast<QComboBox*>(editor));
            QTest::keyClick(editor,Qt::Key_Escape);
        }
        const auto before=table->model()->index(0,2).data();QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(table->model()->index(0,0)).center());QCOMPARE(table->model()->index(0,2).data(),before);
    }
    void enumReadings(){
        SignalTransmitViewModel vm(signal::Bus::Can);QString error;QVERIFY(vm.importFile(QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic.dbc",error));SignalPlotModel model(&vm);QVERIFY(model.addSignal(signal::frameKey(signal::Bus::Can,291),"Small"));
        FrameRecord a;a.bus=signal::Bus::Can;a.id=291;a.payload=QByteArray(8,0);a.typed=true;a.timeUs=0;a.captureUs=0;auto b=a;b.timeUs=b.captureUs=1000000;b.payload[0]=2;model.append({a,b});
        QCOMPARE(model.index(0,6).data().toString(),QString("Ready"));model.setCursors(true,true,0,1000000);QCOMPARE(model.index(0,6).data().toString(),QString("Off"));QCOMPARE(model.index(0,7).data().toString(),QString("2"));
        model.setCursors(true,false,500000,0);QCOMPARE(model.index(0,6).data().toString(),QString("1"));
        SignalTransmitViewModel lin(signal::Bus::Lin);QVERIFY(lin.importFile(QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic.ldf",error));SignalPlotModel logical(&lin);QVERIFY(logical.addSignal(signal::frameKey(signal::Bus::Lin,16),"Command"));
        a.bus=signal::Bus::Lin;a.id=16;a.payload=QByteArray(2,0);a.payload[0]=2;logical.append({a});QCOMPARE(logical.series()[0].labels.value(2),QString("On"));QVERIFY(logical.valueAt(0,0).valid);QCOMPARE(logical.index(0,6).data().toString(),QString("On"));
    }
    void enumCompletionAndUndefinedRaw(){
        SignalTransmitViewModel vm(signal::Bus::Can);QString error;QVERIFY(vm.importFile(QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic.dbc",error));QVERIFY(vm.selectCanNode("Tester","Tx",error));
        SignalTransmitPage page(&vm);page.resize(1500,950);page.show();QCoreApplication::processEvents();auto *table=page.findChild<QTableView*>("signalValues");const auto cell=table->model()->index(0,3);
        QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(cell).center());QCoreApplication::processEvents();auto*combo=table->findChild<QComboBox*>("signalEnumEditor");QVERIFY(combo);QVERIFY(combo->isEditable());
        combo->lineEdit()->selectAll();QTest::keyClicks(combo->lineEdit(),"Of");QTest::keyClick(combo,Qt::Key_Tab);QCoreApplication::processEvents();QCOMPARE(table->model()->index(0,2).data().toString(),QString("0"));QCOMPARE(cell.data().toString(),QString("Off"));
        QVERIFY(table->model()->setData(table->model()->index(0,2),"5",Qt::EditRole));QCOMPARE(cell.data().toString(),QString("-"));QCOMPARE(table->model()->index(0,2).data().toString(),QString("5"));
        QCOMPARE(vm.working().frames.value(signal::frameKey(signal::Bus::Can,291)).applied.bytes[0]&7,5);
        SignalTransmitViewModel lin(signal::Bus::Lin);QVERIFY(lin.importFile(QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic.ldf",error));SignalValueTableModel values(&lin);values.setFrame(signal::frameKey(signal::Bus::Lin,16));SignalValueDelegate delegate;QWidget parent;auto *edit=delegate.createEditor(&parent,QStyleOptionViewItem(),values.index(0,3));auto *logical=qobject_cast<QComboBox*>(edit);QVERIFY(logical&&logical->isEditable());delegate.setEditorData(logical,values.index(0,3));logical->setEditText("3");delegate.setModelData(logical,&values,values.index(0,3));QCOMPARE(values.index(0,2).data().toString(),QString("3"));QCOMPARE(values.index(0,3).data().toString(),QString("-"));
    }
    void graphicsFitsHeight(){
        SignalTransmitViewModel vm(signal::Bus::Can);QString error;QVERIFY(vm.importFile(QString(TEST_SOURCE_DIR)+"/fixtures/signals-synthetic.dbc",error));SignalPlotDialog dialog(&vm);
        auto*model=dialog.findChild<SignalPlotModel*>();QVERIFY(model);for(int i=0;i<12;++i)QVERIFY(model->addSignal(signal::frameKey(signal::Bus::Can,291),"Small"));
        auto*canvas=dialog.findChild<SignalPlotCanvas*>();QVERIFY(canvas);canvas->setAxes(SignalPlotCanvas::Arrange);dialog.show();QCoreApplication::processEvents();canvas->refresh();QCoreApplication::processEvents();
        auto*scroll=dialog.findChild<QScrollArea*>();QVERIFY(scroll);QCOMPARE(scroll->verticalScrollBarPolicy(),Qt::ScrollBarAlwaysOff);QVERIFY(canvas->height()<=scroll->viewport()->height());
    }
};
QTEST_MAIN(LocalizationTest)
#include "test_localization.moc"
