#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QTableView>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTabWidget>
#include <QSortFilterProxyModel>
#include <QCheckBox>
#include <QFile>
#include <QSpinBox>
#include <QSplitter>
#include <QTabBar>
#include <QMouseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include "domain/CddConfiguration.h"
#include "views/ChannelPage.h"
#include "views/MainWindow.h"
#include "infrastructure/SettingsStore.h"
using namespace host;
class UdsUiTest : public QObject {
    Q_OBJECT
    QString fixture()const{return QString(TEST_SOURCE_DIR)+"/fixtures/diagnostic.cdd";}
    ChannelSettings settings(communication::Bus bus){auto s=ChannelSettings::defaults(bus);s.simulation=true;s.hardwareKey=bus==communication::Bus::Can?"preview:CAN:1":"preview:LIN:1";s.handle=bus==communication::Bus::Can?0xf101:0xf201;return s;}
    int find(ChannelViewModel &vm,const QString &qualifier){for(int i=0;i<vm.diagnosticServices()->rowCount();++i)if(vm.diagnosticServices()->service(i)->qualifier==qualifier)return i;return -1;}
    void select(ChannelPage &page,ChannelViewModel &vm,const QString &qualifier){auto table=page.findChild<QTableView*>("udsServiceTable");auto proxy=qobject_cast<QSortFilterProxyModel*>(table->model());table->setCurrentIndex(proxy->mapFromSource(vm.diagnosticServices()->index(find(vm,qualifier),0)));}
    void configure(ChannelPage &page,const std::function<void(QDialog*)> &work){
        QTimer::singleShot(0,&page,[&]{auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);work(dialog);dialog->reject();});
        page.findChild<QPushButton*>("udsSettings")->click();
    }
    void save(QDialog *dialog){dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();}
private slots:
    void sharedParametersAndProtocolIndicator_data(){
        QTest::addColumn<bool>("lin");QTest::newRow("CAN")<<false;QTest::newRow("LIN")<<true;
    }
    void sharedParametersAndProtocolIndicator(){
        QFETCH(bool,lin);QTemporaryDir temp;const auto bus=lin?communication::Bus::Lin:communication::Bus::Can;
        ChannelViewModel vm(settings(bus));ChannelPage page(&vm);const int bitrate=vm.settings().bitrate;
        auto open=[&](const char *button,const std::function<void(QDialog*)> &work){
            QTimer::singleShot(0,&page,[&]{auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);work(dialog);dialog->reject();});
            page.findChild<QPushButton*>(button)->click();
        };
        for(auto button:{"downloadParameters","udsSettings"})open(button,[&](QDialog *dialog){
            QVERIFY(!dialog->findChild<QWidget*>("uds_bitrate"));QVERIFY(!dialog->findChild<QWidget*>("cddCommunicationParameters"));
            auto baseline=dialog->findChild<QLineEdit*>("uds_default_p2Ms");auto value=dialog->findChild<QLineEdit*>("uds_p2Ms");
            QVERIFY(baseline->isReadOnly());QVERIFY(!value->isReadOnly());QCOMPARE(baseline->text(),value->text());
            QCOMPARE(dialog->findChild<QLabel*>("cddProtocolIndicator")->property("protocolState").toInt(),0);
            auto enabled=dialog->findChild<QCheckBox*>("uds_testerPresentEnabled");
            auto period=dialog->findChild<QLineEdit*>("uds_testerPresentMs");auto request=dialog->findChild<QLineEdit*>("uds_testerPresentRequest");
            QVERIFY(!period->isEnabled());QVERIFY(!request->isEnabled());enabled->setChecked(true);QVERIFY(period->isEnabled());QVERIFY(request->isEnabled());
        });
        QFile source(fixture());QVERIFY(source.open(QIODevice::ReadOnly));const auto original=source.readAll();
        auto writeCdd=[&](const QString &prefix,const QString &name){
            auto xml=original;const auto attributes=QString("<UNSDEF id=\"timing\" v=\"123\"><QUAL>%1.P2Client</QUAL></UNSDEF><UNSDEF id=\"baud\" v=\"9600\"><QUAL>%1.Baudrate</QUAL></UNSDEF>").arg(prefix).toUtf8();
            xml.replace("<ECUDOC>","<ECUDOC>"+attributes);QFile out(temp.filePath(name));if(!out.open(QIODevice::WriteOnly))return QString();out.write(xml);return out.fileName();
        };
        const auto valid=writeCdd(lin?"LIN":"CAN","valid.cdd"),mismatch=writeCdd(lin?"CAN":"LIN","mismatch.cdd");
        QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());
        open("udsSettings",[&](QDialog *dialog){
            auto path=dialog->findChild<QLineEdit*>("cddPath");path->setText(valid);QTest::keyClick(path,Qt::Key_Return);
            QCOMPARE(dialog->findChild<QLabel*>("cddProtocolIndicator")->property("protocolState").toInt(),1);
            QCOMPARE(dialog->findChild<QLineEdit*>("uds_default_p2Ms")->text(),QString("1000"));
            QCOMPARE(dialog->findChild<QLineEdit*>("uds_p2Ms")->text(),QString("123"));save(dialog);QVERIFY(!dialog->isVisible());
        });
        QCOMPARE(vm.settings().bitrate,bitrate);QCOMPARE(vm.settings().p2Ms,123);
        open("downloadParameters",[&](QDialog *dialog){
            QCOMPARE(dialog->findChild<QLineEdit*>("uds_p2Ms")->text(),QString("123"));
            dialog->findChild<QLineEdit*>("uds_p2Ms")->setText("234");save(dialog);
        });
        open("udsSettings",[&](QDialog *dialog){
            QCOMPARE(dialog->findChild<QLineEdit*>("uds_p2Ms")->text(),QString("234"));
            auto path=dialog->findChild<QLineEdit*>("cddPath");path->setText(mismatch);QTest::keyClick(path,Qt::Key_Return);
            QCOMPARE(dialog->findChild<QLabel*>("cddProtocolIndicator")->property("protocolState").toInt(),2);
            QVERIFY(dialog->findChild<QLabel*>("udsSettingsError")->text().isEmpty());save(dialog);QVERIFY(dialog->isVisible());
        });
        QCOMPARE(vm.settings().p2Ms,234);QCOMPARE(vm.settings().cddPath,valid);QString error;QVERIFY(!vm.loadCdd(mismatch,error));
        QCOMPARE(vm.settings().p2Ms,234);QCOMPARE(vm.settings().bitrate,bitrate);
    }
    void downloadDiagnosticInterlock_data(){sharedParametersAndProtocolIndicator_data();}
    void downloadDiagnosticInterlock(){
        QFETCH(bool,lin);QTemporaryDir temp;auto initial=settings(lin?communication::Bus::Lin:communication::Bus::Can);
        QFile image(temp.filePath("app.bin"));QVERIFY(image.open(QIODevice::WriteOnly));image.write(QByteArray(128,'x'));image.close();
        initial.applicationPath=image.fileName();initial.flashRequired=false;initial.p3Ms=500;
        ChannelViewModel vm(initial);ChannelPage page(&vm);QString error;QVERIFY(vm.loadCdd(fixture(),error));select(page,vm,"DefaultSession/Start");
        QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());
        auto start=page.findChild<QPushButton*>("startButton"),send=page.findChild<QPushButton*>("sendUdsRequest"),parameters=page.findChild<QPushButton*>("downloadParameters");
        QVERIFY(start->isEnabled());QVERIFY(send->isEnabled());
        QVERIFY(vm.sendDiagnostic(find(vm,"DefaultSession/Start"),QByteArray::fromHex("1081"),error));
        QVERIFY(!start->isEnabled());QVERIFY(!parameters->isEnabled());vm.start();QVERIFY(!vm.pending());QCOMPARE(vm.taskState(),TaskState::Idle);
        vm.cancel();QTRY_VERIFY(!vm.busy());QVERIFY(start->isEnabled());
        start->click();QVERIFY(vm.busy());QVERIFY(!send->isEnabled());QVERIFY(!parameters->isEnabled());
        QVERIFY(!vm.sendDiagnostic(find(vm,"DefaultSession/Start"),QByteArray::fromHex("1001"),error));
        QTRY_COMPARE(vm.taskState(),TaskState::Running);vm.cancel();QTRY_VERIFY(!vm.busy());QVERIFY(send->isEnabled());QVERIFY(parameters->isEnabled());
    }
    void communicationMappingAndDialogTransaction(){
        diag::Variant variant;variant.communication["CAN.ReqCanId"]={"Request","1813",{},"ECU"};
        variant.communication["CAN.P2Client"]={"P2","0.05","s","ECU"};variant.communication["LIN.P2Client"]={"P2","200","ms","ECU"};
        auto can=settings(communication::Bus::Can),lin=settings(communication::Bus::Lin);QString error;
        QVERIFY2(applyCddCommunication(variant,can,error),qPrintable(error));QCOMPARE(can.requestId,QString("715"));QCOMPARE(can.p2Ms,50);
        QVERIFY(applyCddCommunication(variant,lin,error));QCOMPARE(lin.p2Ms,200);QCOMPARE(lin.requestId,QString("7E0"));
        variant.communication["CAN.P2Client"].value="invalid";auto original=can.toJson();QVERIFY(!applyCddCommunication(variant,can,error));QCOMPARE(can.toJson(),original);
        ChannelViewModel vm(settings(communication::Bus::Can));ChannelPage page(&vm);
        configure(page,[&](QDialog *dialog){auto path=dialog->findChild<QLineEdit*>("cddPath");path->setText(QDir::current().relativeFilePath(fixture()));QTest::keyClick(path,Qt::Key_Return);
            QCOMPARE(path->text(),QFileInfo(fixture()).absoluteFilePath());
            QCOMPARE(dialog->findChild<QComboBox*>("cddVariant")->count(),2);dialog->findChild<QLineEdit*>("uds_p2Ms")->setText("333");});
        QVERIFY(vm.diagnosticDatabase().ecus.isEmpty());QCOMPARE(vm.settings().p2Ms,1000);
        configure(page,[&](QDialog *dialog){auto path=dialog->findChild<QLineEdit*>("cddPath");path->setText(fixture());QTest::keyClick(path,Qt::Key_Return);
            dialog->findChild<QComboBox*>("cddVariant")->setCurrentIndex(1);dialog->findChild<QLineEdit*>("uds_p2Ms")->setText("333");save(dialog);QVERIFY(!dialog->isVisible());});
        QCOMPARE(vm.settings().p2Ms,333);QCOMPARE(vm.settings().cddVariant,QString("other"));
        configure(page,[&](QDialog *dialog){auto path=dialog->findChild<QLineEdit*>("cddPath");path->setText("missing.cdd");save(dialog);QVERIFY(dialog->isVisible());});
        QCOMPARE(vm.settings().p2Ms,333);QCOMPARE(vm.settings().cddVariant,QString("other"));
        ChannelViewModel restored(vm.settings());QTRY_COMPARE(restored.diagnosticServices()->rowCount(),vm.diagnosticServices()->rowCount());QCOMPARE(restored.settings().p2Ms,333);
    }
    void frozenRequestAndCancellation(){
        auto initial=settings(communication::Bus::Can);initial.p2Ms=300;initial.p3Ms=400;
        ChannelViewModel vm(initial);ChannelPage page(&vm);QString error;QVERIFY(vm.loadCdd(fixture(),error));select(page,vm,"DefaultSession/Start");
        QVERIFY(!page.findChild<QCheckBox*>("udsRawMode"));auto command=page.findChild<QPlainTextEdit*>("udsCommand");command->setPlainText("10 81");
        QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());
        auto send=page.findChild<QPushButton*>("sendUdsRequest");auto button=page.findChild<QPushButton*>("udsSettings");QSignalSpy finished(&vm,&ChannelViewModel::diagnosticFinished);
        send->click();QCOMPARE(send->text(),QString("取消发送"));QVERIFY(!button->isEnabled());
        command->setPlainText("10 01");auto changed=vm.settings();changed.p2Ms=40;QVERIFY(!vm.setSettings(changed));
        QTest::qWait(120);QCOMPARE(finished.count(),0);QVERIFY(!button->isEnabled());send->click();
        QTRY_COMPARE(finished.count(),1);QTRY_VERIFY(!vm.busy());QVERIFY(!finished[0][0].toBool());QVERIFY(vm.diagnosticResult().contains("TX  10 81"));
        QCOMPARE(send->text(),QString("发送"));QVERIFY(button->isEnabled());
    }
    void testerPresentLocksSettingsUntilCancelled(){
        auto initial=settings(communication::Bus::Can);initial.testerPresentEnabled=true;initial.testerPresentMs=150;initial.p2Ms=50;initial.p3Ms=500;
        ChannelViewModel vm(initial);ChannelPage page(&vm);QString error;QVERIFY(vm.loadCdd(fixture(),error));
        QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());QSignalSpy finished(&vm,&ChannelViewModel::diagnosticFinished);
        QVERIFY(vm.sendDiagnostic(find(vm,"DefaultSession/Start"),QByteArray::fromHex("1001"),error));QTRY_COMPARE(finished.count(),1);QVERIFY(finished[0][0].toBool());
        QTRY_VERIFY(vm.diagnosticBusy());auto button=page.findChild<QPushButton*>("udsSettings");auto send=page.findChild<QPushButton*>("sendUdsRequest");
        QVERIFY(!button->isEnabled());QCOMPARE(send->text(),QString("取消发送"));send->click();QTRY_VERIFY(!vm.busy());QVERIFY(button->isEnabled());
        QTest::qWait(220);QVERIFY(!vm.busy());
    }
    void simulatedCanAndLin_data(){QTest::addColumn<bool>("lin");QTest::newRow("CAN")<<false;QTest::newRow("LIN")<<true;}
    void simulatedCanAndLin(){
        QFETCH(bool,lin);ChannelViewModel vm(settings(lin?communication::Bus::Lin:communication::Bus::Can));QString error;
        QVERIFY2(vm.loadCdd(fixture(),error),qPrintable(error));QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());
        QSignalSpy finished(&vm,&ChannelViewModel::diagnosticFinished);
        QVERIFY(vm.sendDiagnostic(find(vm,"VIN/Write"),QByteArray::fromHex("2ef190")+"TESTVIN0123456789",error));QVERIFY(vm.busy());QVERIFY(!vm.canStart());
        QVERIFY(!vm.loadCdd(fixture(),error));QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,5000);QVERIFY2(finished[0][0].toBool(),qPrintable(finished[0][2].toString()));QCOMPARE(finished[0][1].toByteArray(),QByteArray::fromHex("6ef190"));
        QVERIFY(vm.sendDiagnostic(find(vm,"VIN/Read"),QByteArray::fromHex("22f190"),error));QTRY_COMPARE_WITH_TIMEOUT(finished.count(),2,5000);QVERIFY2(finished[1][0].toBool(),qPrintable(finished[1][2].toString()));QCOMPARE(finished[1][1].toByteArray(),QByteArray::fromHex("62f190")+"TESTVIN0123456789");QVERIFY(vm.diagnosticResult().contains("TESTVIN0123456789"));QVERIFY(vm.frames()->rowCount()>4);
        QVERIFY(vm.sendDiagnostic(find(vm,"DefaultSession/Start"),QByteArray::fromHex("1001"),error));QTRY_COMPARE_WITH_TIMEOUT(finished.count(),3,5000);QVERIFY(finished[2][0].toBool());
        QVERIFY(vm.sendDiagnostic(find(vm,"VIN/Read"),QByteArray::fromHex("22f190"),error));vm.cancel();QTRY_COMPARE(finished.count(),4);QVERIFY(!finished[3][0].toBool());QVERIFY(!vm.diagnosticBusy());
        vm.toggleConnection();QTRY_VERIFY(!vm.connected()&&!vm.pending());
    }
    void parameterEditingSelectionAndPersistence(){
        QTemporaryDir temp;ChannelViewModel vm(settings(communication::Bus::Can));ChannelPage page(&vm);page.setAttribute(Qt::WA_DontShowOnScreen);page.resize(1260,880);page.show();
        auto tabs=page.findChild<QTabWidget*>("taskPages");QVERIFY(tabs);QCOMPARE(tabs->count(),3);QCOMPARE(tabs->tabText(2),QString("信号发送"));tabs->setCurrentIndex(1);
        QString error;QVERIFY(vm.loadCdd(fixture(),error));select(page,vm,"Number/Write");
        auto input=page.findChild<QLineEdit*>("udsField_2");QVERIFY(input);auto command=page.findChild<QPlainTextEdit*>("udsCommand");QVERIFY(command);input->setText("4660");QCOMPARE(command->toPlainText(),QString("2E F1 A0 34 12"));
        input->setText("65536");QVERIFY(!page.findChild<QPushButton*>("copyUdsCommand")->isEnabled());input->setText("4660");
        QVERIFY(!page.findChild<QCheckBox*>("udsRawMode"));command->setPlainText("2E F1 A0 78 56");QVERIFY(page.findChild<QPushButton*>("copyUdsCommand")->isEnabled());command->setPlainText("2E F1 A1 78 56");QVERIFY(!page.findChild<QPushButton*>("copyUdsCommand")->isEnabled());page.findChild<QPushButton*>("revertUdsCommand")->click();
        QVERIFY(!vm.selectDiagnosticTarget("unknown","Base",error));QCOMPARE(vm.settings().cddVariant,QString("base"));
        QVERIFY(vm.selectDiagnosticTarget("ExampleECU","Other",error));QCOMPARE(vm.settings().cddVariant,QString("other"));
        QVector<ChannelSettings> saved{vm.settings()},loaded;auto path=temp.filePath("channels.json");QVERIFY(SettingsStore(path).save(saved,error));QVERIFY(SettingsStore(path).load(loaded,error));QCOMPARE(loaded[0].cddPath,vm.settings().cddPath);QCOMPARE(loaded[0].cddVariant,QString("other"));
        const auto count=vm.diagnosticServices()->rowCount();QVERIFY(!vm.loadCdd(temp.filePath("missing.cdd"),error));QCOMPARE(vm.diagnosticServices()->rowCount(),count);
        select(page,vm,"VIN/Write");auto vin=qobject_cast<QLineEdit*>(page.findChild<QTableWidget*>("udsParameterTable")->cellWidget(0,1));QVERIFY(vin);vin->setText("TESTVIN0123456789");QCOMPARE(command->toPlainText(),QString("2E F1 90 54 45 53 54 56 49 4E 30 31 32 33 34 35 36 37 38 39"));
        QTest::qWait(80);QVERIFY(page.findChild<QPushButton*>("sendUdsRequest")->visibleRegion().contains(page.findChild<QPushButton*>("sendUdsRequest")->rect()));
        QDir().mkpath(QCoreApplication::applicationDirPath()+"/artifacts");QVERIFY(page.grab().save(QCoreApplication::applicationDirPath()+"/artifacts/uds-page.png"));
    }
    void rawUndoRestoresParameterRequest(){
        ChannelViewModel vm(settings(communication::Bus::Can));ChannelPage page(&vm);QString error;QVERIFY(vm.loadCdd(fixture(),error));select(page,vm,"DefaultSession/Start");
        auto*command=page.findChild<QPlainTextEdit*>("udsCommand");auto*revert=page.findChild<QPushButton*>("revertUdsCommand");QVERIFY(command);QVERIFY(revert);QVERIFY(!command->isReadOnly());QVERIFY(!revert->isEnabled());const auto original=command->toPlainText();
        command->setPlainText("10 81");command->setPlainText("not hex");QVERIFY(revert->isEnabled());revert->click();QCOMPARE(command->toPlainText(),original);QVERIFY(!revert->isEnabled());QVERIFY(!page.findChild<QCheckBox*>("udsRawMode"));QVERIFY(!page.findChild<QLabel*>("cddSummary"));QVERIFY(!page.findChild<QLabel*>("udsConditions"));
    }
    void differentCddsReplaceConfiguration(){
        QTemporaryDir temp;ChannelViewModel vm(settings(communication::Bus::Can));ChannelPage page(&vm);QString error;
        auto parameters=page.findChild<QTableWidget*>("udsParameterTable");auto command=page.findChild<QPlainTextEdit*>("udsCommand");
        auto revert=page.findChild<QPushButton*>("revertUdsCommand");
        QVERIFY(vm.loadCdd(fixture(),error));QCOMPARE(vm.diagnosticServices()->rowCount(),5);select(page,vm,"VIN/Write");
        auto vin=qobject_cast<QLineEdit*>(parameters->cellWidget(0,1));QVERIFY(vin);vin->setText("TESTVIN0123456789");command->setPlainText("2E F1 90 00");
        const auto replacement=temp.filePath("current.cdd");
        QVERIFY(QFile::copy(QString(TEST_SOURCE_DIR)+"/fixtures/alternate.cdd",replacement));
        QVERIFY2(vm.loadCdd(replacement,error),qPrintable(error));
        QCOMPARE(vm.settings().cddEcu,QString("benchEcu"));QCOMPARE(vm.settings().cddVariant,QString("benchA"));
        QCOMPARE(vm.diagnosticServices()->rowCount(),2);QCOMPARE(find(vm,"VIN/Write"),-1);QCOMPARE(find(vm,"Number/Write"),-1);QVERIFY(!revert->isEnabled());
        select(page,vm,"Label/Write");QCOMPARE(parameters->rowCount(),1);QCOMPARE(parameters->item(0,0)->text(),QString("Label"));
        auto label=qobject_cast<QLineEdit*>(parameters->cellWidget(0,1));QVERIFY(label);QVERIFY(label->text().isEmpty());label->setText("ABCD");
        QCOMPARE(command->toPlainText(),QString("2E 12 34 41 42 43 44"));label->setText("ABCDE");QVERIFY(!page.findChild<QPushButton*>("copyUdsCommand")->isEnabled());
        select(page,vm,"Calibration/Start");auto mode=qobject_cast<QComboBox*>(parameters->cellWidget(0,1));QVERIFY(mode);mode->setCurrentIndex(2);
        QCOMPARE(command->toPlainText(),QString("31 01 AB CD 02"));
        QVERIFY(vm.selectDiagnosticTarget("benchEcu","benchB",error));QCOMPARE(vm.diagnosticServices()->rowCount(),1);QCOMPARE(find(vm,"Label/Write"),-1);
        // Replacing the file at the same path must also discover new targets.
        QFile original(fixture());QVERIFY(original.open(QIODevice::ReadOnly));QFile overwritten(replacement);QVERIFY(overwritten.open(QIODevice::WriteOnly|QIODevice::Truncate));overwritten.write(original.readAll());overwritten.close();
        QVERIFY2(vm.loadCdd(replacement,error),qPrintable(error));QCOMPARE(vm.diagnosticServices()->rowCount(),5);QCOMPARE(vm.settings().cddEcu,QString("exampleEcu"));
        QCOMPARE(find(vm,"Calibration/Start"),-1);select(page,vm,"Number/Write");auto number=qobject_cast<QLineEdit*>(parameters->cellWidget(0,1));QVERIFY(number);QVERIFY(number->text().isEmpty());
        number->setText("4660");QCOMPARE(command->toPlainText(),QString("2E F1 A0 34 12"));
    }
    void automaticRepeat_data(){simulatedCanAndLin_data();}
    void automaticRepeat(){
        QFETCH(bool,lin);auto initial=settings(lin?communication::Bus::Lin:communication::Bus::Can);
        ChannelViewModel vm(initial);ChannelPage page(&vm);QString error;QVERIFY(vm.loadCdd(fixture(),error));select(page,vm,"Number/Read");
        configure(page,[&](QDialog *dialog){
            auto enabled=dialog->findChild<QCheckBox*>("udsAutoRepeat");
            auto count=dialog->findChild<QLineEdit*>("udsRepeatCount"),delay=dialog->findChild<QLineEdit*>("udsRepeatDelay");
            QVERIFY(!dialog->findChild<QLineEdit*>("udsRepeatCountDefault"));QVERIFY(!dialog->findChild<QLineEdit*>("udsRepeatDelayDefault"));
            QVERIFY(!count->isEnabled());QVERIFY(!delay->isEnabled());
            enabled->setChecked(true);QVERIFY(count->isEnabled());QVERIFY(delay->isEnabled());QVERIFY(!count->isReadOnly());QVERIFY(!delay->isReadOnly());
            count->setText("2");delay->setText("100");enabled->setChecked(false);
            QVERIFY(!count->isEnabled());QVERIFY(!delay->isEnabled());
            enabled->setChecked(true);QCOMPARE(count->text(),QString("2"));QCOMPARE(delay->text(),QString("100"));save(dialog);
        });
        auto repeat=page.findChild<QPushButton*>("sendUdsRequest");auto settingsButton=page.findChild<QPushButton*>("udsSettings");
        QCOMPARE(vm.settings().udsRepeatCount,2);QCOMPARE(vm.settings().udsRepeatDelayMs,100);QVERIFY(vm.settings().udsRepeatEnabled);
        QTemporaryDir temp;QVector<ChannelSettings> restored;QVERIFY(SettingsStore(temp.filePath("settings.json")).save({vm.settings()},error));QVERIFY(SettingsStore(temp.filePath("settings.json")).load(restored,error));QCOMPARE(restored[0].udsRepeatCount,2);QCOMPARE(restored[0].udsRepeatDelayMs,100);
        QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());QVERIFY(repeat->isEnabled());
        QSignalSpy finished(&vm,&ChannelViewModel::diagnosticFinished);QElapsedTimer timer;timer.start();repeat->click();
        QTRY_COMPARE(finished.count(),1);QVERIFY(vm.busy());QVERIFY(!settingsButton->isEnabled());QCOMPARE(repeat->text(),QString("取消发送"));QVERIFY(!vm.loadCdd(fixture(),error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(),3,3000);QVERIFY(timer.elapsed()>=200);QVERIFY(!vm.diagnosticBusy());QVERIFY(settingsButton->isEnabled());QCOMPARE(repeat->text(),QString("发送"));
        for(const auto &call:finished)QVERIFY2(call[0].toBool(),qPrintable(call[2].toString()));
        // Cancelling the delay must not leave a scheduled request behind.
        auto updated=vm.settings();updated.udsRepeatDelayMs=250;QVERIFY(vm.setSettings(updated));repeat->click();QTRY_COMPARE(finished.count(),4);QVERIFY(vm.diagnosticRepeating());repeat->click();QVERIFY(!vm.busy());QTest::qWait(350);QCOMPARE(finished.count(),4);
        repeat->click();QTRY_COMPARE(finished.count(),5);vm.toggleConnection();QTRY_VERIFY(!vm.connected()&&!vm.pending());QTest::qWait(350);QCOMPARE(finished.count(),5);QVERIFY(!vm.diagnosticRepeating());
    }
    void layoutAndAutomaticTargetSelection(){
        QTemporaryDir temp;MainWindow window(temp.filePath("layout.json"),true);window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1280,920);window.show();
        auto tabs=window.findChild<QTabWidget*>("channelTabs");QCOMPARE(tabs->tabPosition(),QTabWidget::West);
        auto vm=window.canChannel();auto page=window.findChild<ChannelPage*>("canPage");page->findChild<QTabWidget*>("taskPages")->setCurrentIndex(1);
        QVERIFY(!page->findChild<QPushButton*>("loadCdd"));QVERIFY(!page->findChild<QPushButton*>("applyCddTarget"));
        configure(*page,[&](QDialog *dialog){
            auto path=dialog->findChild<QLineEdit*>("cddPath");path->setText(fixture());QTest::keyClick(path,Qt::Key_Return);
            auto variant=dialog->findChild<QComboBox*>("cddVariant");QCOMPARE(variant->count(),2);variant->setCurrentIndex(1);save(dialog);
        });
        QCOMPARE(vm->settings().cddVariant,QString("other"));QTest::qWait(100);
        auto regions=page->findChild<QSplitter*>("channelRegions");auto outputs=page->findChild<QSplitter*>("outputSplit");
        QVERIFY(regions);QVERIFY(outputs);QVERIFY(page->findChild<QWidget*>("frameMonitorPanel")->height()>=qCeil(window.height()*0.30));
        QVERIFY(tabs->tabBar()->geometry().right()<page->mapTo(tabs,QPoint()).x());
        QCOMPARE(regions->count(),2);auto sizes=regions->sizes();regions->setSizes({sizes[0]+sizes[1]-1,1});QVERIFY(outputs->height()>=qCeil(window.height()*0.30));
        auto before=outputs->sizes();auto handle=outputs->handle(1);auto position=handle->rect().center();
        QTest::mousePress(handle,Qt::LeftButton,Qt::NoModifier,position);
        const auto destination=position-QPoint(60,0);
        QMouseEvent move(QEvent::MouseMove,QPointF(destination),QPointF(handle->mapToGlobal(destination)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(handle,&move);QTest::mouseRelease(handle,Qt::LeftButton,Qt::NoModifier,destination);
        QVERIFY(outputs->sizes()!=before);
        for(const auto &name:{"downloadSplit","udsMainSplit","udsDetailSplit"}){auto splitter=page->findChild<QSplitter*>(name);QVERIFY(splitter);QVERIFY(splitter->handleWidth()>=4);QVERIFY(!splitter->childrenCollapsible());}
        window.resize(1180,820);QTest::qWait(60);QVERIFY(outputs->height()>=qCeil(window.height()*0.30));
        auto taskTabs=page->findChild<QTabWidget*>("taskPages");taskTabs->setCurrentIndex(0);QTest::qWait(30);
        QCOMPARE(outputs->minimumHeight(),150);const auto downloadSizes=regions->sizes();
        taskTabs->setCurrentIndex(1);QTest::qWait(30);QVERIFY(outputs->minimumHeight()>=qCeil(window.height()*0.30));
        taskTabs->setCurrentIndex(0);QTest::qWait(30);QCOMPARE(regions->sizes(),downloadSizes);taskTabs->setCurrentIndex(1);
        QDir().mkpath(QCoreApplication::applicationDirPath()+"/artifacts");QVERIFY(window.grab().save(QCoreApplication::applicationDirPath()+"/artifacts/uds-layout.png"));
    }
    void negativeResponseAndDisconnect(){
        QTemporaryDir temp;QFile original(fixture());QVERIFY(original.open(QIODevice::ReadOnly));auto xml=original.readAll();xml.replace("<DATAOBJ dtref=\"word\"><QUAL>P2</QUAL>","<DATAOBJ dtref=\"missing\"><QUAL>P2</QUAL>");
        QFile modified(temp.filePath("negative.cdd"));QVERIFY(modified.open(QIODevice::WriteOnly));modified.write(xml);modified.close();
        ChannelViewModel vm(settings(communication::Bus::Can));QString error;QVERIFY(vm.loadCdd(modified.fileName(),error));QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());QSignalSpy finished(&vm,&ChannelViewModel::diagnosticFinished);
        QVERIFY(vm.repeatDiagnostic(find(vm,"DefaultSession/Start"),QByteArray::fromHex("1001"),error));QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,5000);QVERIFY(!finished[0][0].toBool());QCOMPARE(finished[0][1].toByteArray(),QByteArray::fromHex("7f1011"));QVERIFY(vm.diagnosticResult().contains("NRC"));QVERIFY(!vm.diagnosticRepeating());
        QVERIFY(vm.sendDiagnostic(find(vm,"VIN/Read"),QByteArray::fromHex("22f190"),error));vm.toggleConnection();QTRY_VERIFY(!vm.connected()&&!vm.pending());QTRY_COMPARE(finished.count(),2);QVERIFY(!vm.diagnosticBusy());
    }
    void styledAcceptancePage(){
        const auto path=QString(TEST_SOURCE_DIR)+"/../testsrc/ECH_Tst_V05.cdd";if(!QFile::exists(path))QSKIP("Local acceptance CDD is not distributed");
        QTemporaryDir temp;MainWindow window(temp.filePath("ui.json"),true);window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1280,860);window.show();
        auto vm=window.canChannel();auto page=window.findChild<ChannelPage*>("canPage");window.findChild<QTabWidget*>("channelTabs")->setCurrentIndex(0);
        QString error;QVERIFY2(vm->loadCdd(path,error),qPrintable(error));QCOMPARE(vm->diagnosticServices()->rowCount(),39);page->findChild<QTabWidget*>("taskPages")->setCurrentIndex(1);
        QCOMPARE(vm->settings().requestId,QString("715"));QCOMPARE(vm->settings().responseId,QString("795"));QCOMPARE(vm->settings().p2Ms,50);QCOMPARE(vm->settings().p2StarMs,5000);
        QCOMPARE(vm->settings().p3Ms,150);QCOMPARE(vm->settings().testerPresentMs,5000);QCOMPARE(vm->settings().testerPresentRequest,QString("3E00"));
        QCOMPARE(vm->settings().canNetwork["nAsMs"].toInt(),70);QCOMPARE(vm->settings().canNetwork["nArMs"].toInt(),70);QCOMPARE(vm->settings().canNetwork["nBsMs"].toInt(),150);QCOMPARE(vm->settings().canNetwork["nCrMs"].toInt(),150);QCOMPARE(vm->settings().canNetwork["padding"].toInt(),170);
        configure(*page,[&](QDialog *dialog){QCOMPARE(dialog->findChild<QLineEdit*>("uds_p2Ms")->text(),QString("50"));QCOMPARE(dialog->findChild<QLineEdit*>("uds_requestId")->text(),QString("715"));
            QVERIFY(!dialog->findChild<QTableWidget*>("cddCommunicationParameters"));QTest::qWait(30);
            QDir().mkpath(QCoreApplication::applicationDirPath()+"/artifacts");QVERIFY(dialog->grab().save(QCoreApplication::applicationDirPath()+"/artifacts/uds-settings.png"));});
        QTRY_VERIFY(vm->canConnect());vm->toggleConnection();QTRY_VERIFY(vm->connected()&&!vm->pending());
        select(*page,*vm,"VINDataIdentifier/Write");auto table=page->findChild<QTableWidget*>("udsParameterTable");auto vin=qobject_cast<QLineEdit*>(table->cellWidget(0,1));QVERIFY(vin);vin->setText("TESTVIN0123456789");
        auto send=page->findChild<QPushButton*>("sendUdsRequest");QVERIFY(send->isEnabled());QSignalSpy finished(vm,&ChannelViewModel::diagnosticFinished);send->click();QTRY_COMPARE(finished.count(),1);QVERIFY2(finished[0][0].toBool(),qPrintable(finished[0][2].toString()));
        select(*page,*vm,"VINDataIdentifier/Read");send->click();QTRY_COMPARE(finished.count(),2);QVERIFY2(finished[1][0].toBool(),qPrintable(finished[1][2].toString()));
        QTRY_VERIFY(vm->frames()->rowCount()>0);bool tx=false,padded=false;
        for(int row=0;row<vm->frames()->rowCount();++row){const auto id=vm->frames()->data(vm->frames()->index(row,5)).toString();const auto bytes=vm->frames()->data(vm->frames()->index(row,7)).toString();if(id.contains("715")){tx=true;padded|=bytes.endsWith("AA");}}
        QVERIFY(tx);QVERIFY(padded);
        QTest::qWait(100);QVERIFY(send->visibleRegion().contains(send->rect()));
        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath()+"/artifacts/uds-acceptance.png"));
    }
};
QTEST_MAIN(UdsUiTest)
#include "test_uds_ui.moc"
