#include <QtTest>
#include "infrastructure/SettingsStore.h"
#include <algorithm>
#include <QApplication>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include "views/MainWindow.h"
#include "views/ChannelHardwareEditor.h"
#include "protocol/PluginKey.h"
#include "protocol/SimulatedLinEcu.h"
#include "protocol/LinTransport.h"
#include "localization/Language.h"
#include "views/MainWindowDefaults.h"
using namespace boot;
using namespace host;
static QByteArray hex(const char *s){return QByteArray::fromHex(s);}
struct WireBench {
    SimulatedLinEcu ecu;LinTransport transport;UdsSession session;
    explicit WireBench(FlashProfile profile):
        ecu(1,profile),transport(1,1,100,[this](quint8 id,const QByteArray &bytes,QString &error){
            if(!ecu.write(id,bytes,error))return false;
            if(id==0x3c){
                // Exercise the exact confirmation-driven path used by online PLIN.
                const auto copy=bytes;
                QTimer::singleShot(0,&transport,[this,copy]{transport.confirmTransmitted(copy,true);});
            }else{const auto f=ecu.takeResponseFrame();if(!f.isEmpty())transport.receiveFrame(id,f);}
            return true;
        }),session(transport,{60,5500,6000,0}){
        transport.requireTransmitConfirmation(true);
        QObject::connect(&transport,&DiagnosticTransport::cancelled,&transport,[this]{ecu.clearWire();});
    }
};
class DownloadRevisionTest : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    static FlashProfile profile(const QString &flow,bool checked=false){
        FlashProfile p;p.flow=flow;p.resetWaitMs=1;
        for(const auto &step:downloadSteps()){
            p.negativeResponseChecks[step.id]=checked;
            p.timeoutChecks[step.id]=checked;
        }
        return p;
    }
    static FirmwareImage image(quint32 address,int n){
        FirmwareImage result;result.segments.append({address,QByteArray(n,char(0x5a))});result.size=n;return result;
    }
private slots:
    void initTestCase(){
        Q_INIT_RESOURCE(resources);
        QFile theme(":/theme.qss");QVERIFY(theme.open(QIODevice::ReadOnly));
        qApp->setStyleSheet(QString::fromUtf8(theme.readAll()));
    }
    void downloadButtonReadiness_data(){
        QTest::addColumn<bool>("lin");QTest::addColumn<bool>("connectFirst");
        QTest::newRow("LIN connect then import")<<true<<true;
        QTest::newRow("LIN import then connect")<<true<<false;
        QTest::newRow("CAN connect then import")<<false<<true;
        QTest::newRow("CAN import then connect")<<false<<false;
    }
    void downloadButtonReadiness(){
        QFETCH(bool,lin);QFETCH(bool,connectFirst);
        auto settings=ChannelSettings::defaults(lin?communication::Bus::Lin:communication::Bus::Can);
        settings.simulation=true;settings.downloadProfile["resetWaitMs"]=1;
        ChannelViewModel vm(settings);ChannelPage page(&vm);
        auto button=page.findChild<QPushButton*>("startButton");
        auto hint=page.findChild<QLabel*>("downloadStartHint");QVERIFY(button);QVERIFY(hint);
        QTRY_VERIFY(vm.canConnect());QVERIFY(!button->isEnabled());QVERIFY(!hint->isHidden());
        QCOMPARE(hint->text(),QString("请先连接软件通道"));
        auto connectChannel=[&]{vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());};
        if(connectFirst)connectChannel();
        const auto app=temp.filePath(QString("readiness-%1-%2.bin").arg(lin).arg(connectFirst));
        QFile file(app);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QByteArray(32,1));file.close();
        QVERIFY(vm.chooseImage(false,app));
        if(!connectFirst)connectChannel();
        QVERIFY(!button->isEnabled());QCOMPARE(hint->text(),QString("此配置需要有效的 Flash Driver 镜像"));
        auto next=vm.settings();next.flashRequired=false;QVERIFY(vm.setSettings(next));
        QVERIFY(button->isEnabled());QVERIFY(hint->isHidden());
        next.flashRequired=true;QVERIFY(vm.setSettings(next));QVERIFY(!button->isEnabled());
        QVERIFY(vm.chooseImage(true,app));QVERIFY(button->isEnabled());QVERIFY(hint->isHidden());
        QSignalSpy requested(vm.findChild<ChannelModel*>(),&ChannelModel::previewRequested);
        button->click();QCOMPARE(requested.count(),1);QVERIFY(!button->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(vm.taskState(),TaskState::Completed,6000);
        QTRY_VERIFY(button->isEnabled());QVERIFY(hint->isHidden());
        auto model=vm.findChild<ChannelModel*>();
        model->taskChanged(TaskState::Running,0,"busy");QVERIFY(!button->isEnabled());QVERIFY(!hint->isHidden());
        model->taskChanged(TaskState::Completed,100,"done");QVERIFY(button->isEnabled());
        vm.toggleConnection();QTRY_VERIFY(!vm.connected()&&!vm.pending());QVERIFY(!button->isEnabled());
        QCOMPARE(hint->text(),QString("请先连接软件通道"));
    }
    void onlineDependenciesRejectBeforeWorker(){
        auto settings=ChannelSettings::defaults(communication::Bus::Lin);
        const auto app=temp.filePath("online-preflight.bin");QFile file(app);
        QVERIFY(file.open(QIODevice::WriteOnly));file.write("image");file.close();
        settings.applicationPath=app;settings.flashPath=app;
        ChannelViewModel vm(settings);ChannelPage page(&vm);
        auto model=vm.findChild<ChannelModel*>();QVERIFY(model);
        QSignalSpy ready(model,&ChannelModel::ready);QTRY_VERIFY(!ready.isEmpty());
        // Inject ViewModel state only. No real channel is opened and no request
        // may reach the worker while online dependencies are missing.
        model->stateChanged(communication::ConnectionState::Connected,{});
        model->healthChanged(communication::Health::Ready,{});
        auto button=page.findChild<QPushButton*>("startButton");
        auto hint=page.findChild<QLabel*>("downloadStartHint");QVERIFY(button);QVERIFY(hint);
        QSignalSpy requested(model,&ChannelModel::previewRequested);
        QVERIFY(button->isEnabled());QVERIFY(!hint->isHidden());
        QCOMPARE(hint->text(),QString("请配置已授权的安全访问 DLL"));
        button->click();QCOMPARE(requested.count(),0);QVERIFY(!vm.pending());
        QCOMPARE(vm.error(),hint->text());QCOMPARE(vm.taskState(),TaskState::Idle);
        auto task=page.findChild<QLabel*>("taskText");QVERIFY(task);
        QCOMPARE(task->text(),QString("27 dll未加载"));QVERIFY(vm.downloadStartBlocked());
        QVERIFY(task->property("downloadStartBlocked").toBool());
        QCOMPARE(task->palette().color(QPalette::WindowText),QColor("#dc2626"));
        page.resize(1200,700);page.setAttribute(Qt::WA_DontShowOnScreen);page.show();
        QDir().mkpath("artifacts");QVERIFY(page.grab().save("artifacts/download-dll-missing.png"));
        Language::instance().setCode("en");QCOMPARE(Language::text("27 dll未加载"),QString("27 DLL not loaded"));
        Language::instance().setCode("zh_CN");
        auto next=vm.settings();next.downloadProfile["keyLibrary"]=temp.filePath("missing.dll");
        QVERIFY(vm.setSettings(next));QVERIFY(!vm.downloadStartBlocked());QCOMPARE(task->text(),QString("等待开始"));
        QVERIFY(button->isEnabled());QVERIFY(hint->text().contains("27 DLL"));
        button->click();QCOMPARE(requested.count(),0);QVERIFY(vm.error().contains("27 DLL"));
        // An existing directory is not a valid DLL file.
        next.downloadProfile["keyLibrary"]=temp.path();QVERIFY(vm.setSettings(next));
        button->click();QCOMPARE(requested.count(),0);QVERIFY(vm.error().contains("27 DLL"));
        next.downloadProfile["keyLibrary"]=app;QVERIFY(vm.setSettings(next));
        if(!QFileInfo::exists(QCoreApplication::applicationDirPath()+"/seedkey/SeedkeyBridge32.exe")){
            QVERIFY(button->isEnabled());QVERIFY(hint->text().contains("SeedkeyBridge32.exe"));
            button->click();QCOMPARE(requested.count(),0);QVERIFY(vm.error().contains("SeedkeyBridge32.exe"));
        }
        model->healthChanged(communication::Health::Removed,{});QVERIFY(!button->isEnabled());
        QCOMPARE(hint->text(),QString("硬件通道状态异常"));
    }
    void onlineCanStillRequiresTargetIntegration(){
        auto settings=ChannelSettings::defaults(communication::Bus::Can);
        settings.applicationPath=temp.filePath("online-preflight.bin");settings.flashPath=settings.applicationPath;
        ChannelViewModel vm(settings);ChannelPage page(&vm);auto model=vm.findChild<ChannelModel*>();
        QSignalSpy ready(model,&ChannelModel::ready);QTRY_VERIFY(!ready.isEmpty());
        model->stateChanged(communication::ConnectionState::Connected,{});
        model->healthChanged(communication::Health::Ready,{});
        QVERIFY(!vm.canStart());QVERIFY(!page.findChild<QPushButton*>("startButton")->isEnabled());
        QVERIFY(page.findChild<QLabel*>("downloadStartHint")->text().contains("真实 CAN"));
    }
    void appAndBootWireSequence_data(){
        QTest::addColumn<QString>("flow");QTest::addColumn<bool>("checked");
        QTest::newRow("APP unchecked")<<QString("app")<<false;
        QTest::newRow("APP checked")<<QString("app")<<true;
        QTest::newRow("Boot unchecked")<<QString("boot")<<false;
        QTest::newRow("Boot checked")<<QString("boot")<<true;
    }
    void appAndBootWireSequence(){
        QFETCH(QString,flow);QFETCH(bool,checked);
        auto p=profile(flow,checked);WireBench bench(p);
        FlashJob job(bench.session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished),events(&job,&FlashJob::stepEvent);
        QString error;const auto app=image(0xff00,32),driver=image(0x10000000,16);
        QVERIFY2(job.start(app,driver,error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,6000);
        QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        const auto requests=bench.ecu.requests;
        if(flow=="app"){
            QCOMPARE(requests[0],hex("1001"));QCOMPARE(requests[1],hex("1003"));QCOMPARE(requests[2],hex("2701"));
            for(const auto request:{"1083","8582","288301"})QVERIFY(requests.contains(hex(request)));
        }
        else {QCOMPARE(requests[0],hex("1002"));QVERIFY(!requests.contains(hex("2701")));QVERIFY(!requests.contains(hex("220101")));}
        const int programming=requests.indexOf(hex("1002"));QVERIFY(programming>=0);
        QCOMPARE(requests[programming+1],hex("2711"));QVERIFY(requests[programming+2].startsWith(hex("2712")));
        QCOMPARE(requests[programming+3],hex("2ef1840101"));
        QCOMPARE(requests[programming+4],hex("3400441000000000000010"));
        QVERIFY(requests.contains(hex("3101ff00440000ff0000000020")));
        const int reset=requests.indexOf(hex("1101"));QVERIFY(reset>programming);
        QCOMPARE(requests.mid(reset),QList<QByteArray>({hex("1101"),hex("1003"),hex("14ffffff"),hex("288001"),hex("8581"),hex("1081"),hex("22f180")}));
        int suppressedCompletions=0;for(const auto &args:events)if(args[0].toString().contains("正响应抑制完成"))++suppressedCompletions;
        QCOMPARE(suppressedCompletions,flow=="app"?6:3);
        QCOMPARE(bench.ecu.memory(0xff00),app.segments[0].data);QCOMPARE(bench.ecu.memory(0x10000000),driver.segments[0].data);
        QJsonArray trace;for(const auto &r:requests)trace.append(QString::fromLatin1(r.toHex(' ')));
        QDir().mkpath("artifacts");QFile f("artifacts/flow-"+flow+(checked?"-checked":"-unchecked")+".json");QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{{"flow",flow},{"checked",checked},{"requests",trace},{"imageMatches",true},{"physicalBusUsed",false}}).toJson());
    }
    void negativeResponsePolicy_data(){
        QTest::addColumn<bool>("checked");QTest::addColumn<int>("service");QTest::addColumn<bool>("success");
        QTest::newRow("unchecked ignores NRC")<<false<<0x85<<true;
        QTest::newRow("checked stops NRC")<<true<<0x85<<false;
        QTest::newRow("seed data always required")<<false<<0x27<<false;
        QTest::newRow("block length always required")<<false<<0x34<<false;
    }
    void negativeResponsePolicy(){
        QFETCH(bool,checked);QFETCH(int,service);QFETCH(bool,success);
        auto p=profile("app",checked);WireBench bench(p);
        bench.ecu.faults.negativeService=service;bench.ecu.faults.negativeCode=0x22;
        FlashJob job(bench.session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished),notice(&job,&FlashJob::notice),events(&job,&FlashJob::stepEvent);
        QString error;QVERIFY(job.start(image(0xff00,16),{},error));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,6000);
        QCOMPARE(done[0][0].toBool(),success);
        if(success){QCOMPARE(notice.count(),2);QVERIFY(done[0][1].toString().contains("未全部确认"));}
        else {QVERIFY(!bench.ecu.requests.contains(hex("1101")));QVERIFY(done[0][1].toString().contains("NRC"));}
        if(service==0x85){
            bool found=false;for(const auto &args:events)found|=args[0].toString().contains("负响应NRC(0x22)");
            QVERIFY(found);
        }
    }
    void stepEnableControlsExecution(){
        auto p=profile("app",true);p.stepEnabled=ChannelPageInitialValues::initialStepEnabled("app");
        p.stepEnabled["pre8502"]=false;
        WireBench bench(p);FlashJob job(bench.session,p,std::make_unique<SimulationKey>());
        QSignalSpy done(&job,&FlashJob::finished);QString error;
        QVERIFY(job.start(image(0xff00,16),{},error));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,6000);
        QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QVERIFY(!bench.ecu.requests.contains(hex("8582")));
        QVERIFY(bench.ecu.requests.contains(hex("288301")));
    }
    void pendingAndTimeout_data(){
        QTest::addColumn<bool>("checked");QTest::addColumn<bool>("drop");
        QTest::newRow("checked 78")<<true<<false;QTest::newRow("unchecked 78")<<false<<false;
        QTest::newRow("checked timeout")<<true<<true;QTest::newRow("unchecked timeout")<<false<<true;
    }
    void pendingAndTimeout(){
        QFETCH(bool,checked);QFETCH(bool,drop);
        auto p=profile("app",checked);WireBench bench(p);
        if(drop)bench.ecu.faults.dropService=0x14;
        else {bench.ecu.faults.pendingService=0x14;bench.ecu.faults.pendingCount=2;}
        FlashJob job(bench.session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished),events(&job,&FlashJob::stepEvent);
        QString error;QVERIFY(job.start(image(0xff00,16),{},error));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,6000);
        QCOMPARE(done[0][0].toBool(),!drop||!checked);
        if(drop&&checked)QVERIFY(done[0][1].toString().contains("timeout"));
        if(drop&&!checked)QVERIFY(done[0][1].toString().contains("未全部确认"));
        if(drop){
            bool found=false;for(const auto &args:events)found|=args[0].toString().contains("响应超时(")&&args[0].toString().endsWith("ms)");
            QVERIFY(found);
        }
    }

    void seedkeyRejectsInvalidInputs(){
        PluginKey key("missing-provider.dll");QString error;
        QVERIFY(key.calculate(1,hex("12345678"),error).isEmpty());QVERIFY(error.contains("16-byte"));
    }
    void defaultsAndDownloadDialog(){
        auto defaults=ChannelSettings::defaults(communication::Bus::Lin);QString error;
        ChannelSettings loaded;QVERIFY(ChannelSettings::fromJson(QJsonObject{{"bus","LIN"}},loaded,error));
        QCOMPARE(loaded.toJson(),defaults.toJson());QCOMPARE(defaults.nad,QString("01"));
        QVERIFY(defaults.flashRequired);
        QVERIFY(!defaults.rxdEnabled);QVERIFY(!defaults.toJson()["rxdEnabled"].toBool());
        QCOMPARE(defaults.downloadProfile["flow"].toString(),QString("app"));
        for(const auto &step:downloadSteps()){
            QVERIFY(defaults.downloadProfile["stepEnabled"].toObject()[step.id].toBool());
            QCOMPARE(defaults.downloadProfile["negativeResponseChecks"].toObject()[step.id].toBool(),ChannelPageInitialValues::feedbackChecked);
            QCOMPARE(defaults.downloadProfile["timeoutChecks"].toObject()[step.id].toBool(),
                !suppressesPositiveResponse(step.id)&&ChannelPageInitialValues::feedbackChecked);
        }
        MainWindow window(temp.path()+"/channels.json",true);window.setAttribute(Qt::WA_DontShowOnScreen);window.show();
        auto vm=window.canChannel();auto page=window.findChild<ChannelPage*>("canPage");
        auto linVm=window.linChannel();QVERIFY(linVm);
        auto linPage=window.findChild<ChannelPage*>("linPage");QVERIFY(linPage);
        window.findChild<QTabWidget*>("channelTabs")->setCurrentIndex(1);QTest::qWait(20);
        auto rxd=linPage->findChild<QCheckBox*>("rxdEnabled");QVERIFY(rxd);QVERIFY(rxd->isVisible());QVERIFY(!rxd->isChecked());
        rxd->setChecked(true);QVERIFY(linVm->settings().rxdEnabled);QVERIFY(linVm->settings().toJson()["rxdEnabled"].toBool());
        QVERIFY(!page->findChild<QComboBox*>("modeCombo"));
        ChannelHardwareEditor hardwareEditor(vm);auto modes=hardwareEditor.findChild<QComboBox*>("modeCombo");QVERIFY(modes);
        QCOMPARE(modes->itemText(0),QString("在线硬件"));QCOMPARE(modes->itemText(1),QString("模拟模式"));QCOMPARE(modes->currentIndex(),1);
        window.findChild<QTabWidget*>("channelTabs")->setCurrentIndex(0);
        auto quick=vm->settings();quick.p2Ms=30;quick.p2StarMs=60;QVERIFY(vm->setSettings(quick));
        QTimer::singleShot(30,&window,[&]{
            auto dialog=page->findChild<QDialog*>("downloadSettingsDialog");QVERIFY(dialog);
            auto keyRow=dialog->findChild<QWidget*>("keyLibraryRow");QVERIFY(keyRow);
            auto form=qobject_cast<QFormLayout*>(keyRow->parentWidget()->layout());QVERIFY(form);
            int keyRowIndex=-1;QFormLayout::ItemRole keyRole;
            form->getWidgetPosition(keyRow,&keyRowIndex,&keyRole);QCOMPARE(keyRowIndex,0);
            QVERIFY(dialog->findChild<QPushButton*>("browseKeyLibrary"));
            QVERIFY(dialog->findChild<QLineEdit*>("keyLibrary"));
            auto flow=dialog->findChild<QComboBox*>("downloadFlow");QVERIFY(flow);QCOMPARE(flow->currentData().toString(),QString("app"));
            auto flashRequired=dialog->findChild<QCheckBox*>("flashRequired");QVERIFY(flashRequired);QVERIFY(flashRequired->isChecked());
            QCOMPARE(flashRequired->text(),QString("Flash Driver使能"));
            QCOMPARE(flow->count(),2);QCOMPARE(flow->itemText(0),QString("App下载流程"));QCOMPARE(flow->itemText(1),QString("Boot下载流程"));
            QCOMPARE(dialog->findChild<QLabel*>("feedbackDecisionTitle")->text(),QString("反馈判定"));
            auto limit=dialog->findChild<QSpinBox*>("consecutiveFrameByteLimit");QVERIFY(limit);
            QCOMPARE(limit->value(),4095);QCOMPARE(limit->suffix(),QString(" Byte"));
            auto first=dialog->findChild<QCheckBox*>("negative_pre1001");QVERIFY(first);
            auto stepGrid=qobject_cast<QGridLayout*>(first->parentWidget()->layout());QVERIFY(stepGrid);
            for(int i=0;i<downloadSteps().size();++i){
                const auto &step=downloadSteps()[i];
                auto stepEnable=dialog->findChild<QCheckBox*>("enable_"+QString(step.id));
                auto negative=dialog->findChild<QCheckBox*>("negative_"+QString(step.id));
                auto timeout=dialog->findChild<QCheckBox*>("timeout_"+QString(step.id));
                QVERIFY(stepEnable);QVERIFY(negative);QVERIFY(timeout);QVERIFY(stepEnable->isChecked());
                QVERIFY(negative->isEnabled());QCOMPARE(timeout->isEnabled(),!suppressesPositiveResponse(step.id));
                QVERIFY(!negative->isChecked());QVERIFY(!timeout->isChecked());
                int row=0,column=0,rowSpan=0,columnSpan=0;
                stepGrid->getItemPosition(stepGrid->indexOf(stepEnable),&row,&column,&rowSpan,&columnSpan);
                const int rowsPerColumn=(downloadSteps().size()+1)/2;
                QCOMPARE(row,i%rowsPerColumn+1);QCOMPARE(column,(i/rowsPerColumn)*5+1);
                stepGrid->getItemPosition(stepGrid->indexOf(negative),&row,&column,&rowSpan,&columnSpan);
                QCOMPARE(row,i%rowsPerColumn+1);QCOMPARE(column,(i/rowsPerColumn)*5+2);
                stepGrid->getItemPosition(stepGrid->indexOf(timeout),&row,&column,&rowSpan,&columnSpan);
                QCOMPARE(row,i%rowsPerColumn+1);QCOMPARE(column,(i/rowsPerColumn)*5+3);
            }
            const auto labels=dialog->findChildren<QLabel*>();bool has27Dll=false,hasOldDllText=false;
            for(const auto value:labels){has27Dll|=value->text()=="27 DLL";hasOldDllText|=value->text().contains("External Generate",Qt::CaseInsensitive);}
            QVERIFY(has27Dll);QVERIFY(!hasOldDllText);
            dialog->findChild<QCheckBox*>("negative_pre1001")->setChecked(true);
            dialog->findChild<QCheckBox*>("timeout_pre1001")->setChecked(true);flow->setCurrentIndex(1);
            for(const auto &step:downloadSteps()){
                auto stepEnable=dialog->findChild<QCheckBox*>("enable_"+QString(step.id));
                QVERIFY(stepEnable->isEnabled());QCOMPARE(stepEnable->isChecked(),!step.appOnly);
                QCOMPARE(dialog->findChild<QCheckBox*>("negative_"+QString(step.id))->isEnabled(),!step.appOnly);
                QCOMPARE(dialog->findChild<QCheckBox*>("timeout_"+QString(step.id))->isEnabled(),!step.appOnly&&!suppressesPositiveResponse(step.id));
            }
            QVERIFY(first->parentWidget()->styleSheet().contains("indicator:disabled"));
            QCOMPARE(dialog->findChild<QLabel*>("step_pre1001")->text(),QString("01 · 10 01 · 默认会话"));
            for(const auto id:{"pre1003control","pre8502","pre2803","post2800","post8501","post1001"}){
                auto timeout=dialog->findChild<QCheckBox*>("timeout_"+QString(id));
                QVERIFY(timeout);QVERIFY(!timeout->isChecked());QVERIFY(!timeout->isEnabled());
            }
            flow->setCurrentIndex(0);QVERIFY(dialog->findChild<QCheckBox*>("enable_pre1001")->isChecked());
            QVERIFY(dialog->findChild<QCheckBox*>("negative_pre1001")->isChecked());
            QVERIFY(dialog->findChild<QCheckBox*>("timeout_pre1001")->isChecked());flow->setCurrentIndex(1);
            flashRequired->setChecked(false);
            for(const auto id:{"driver34","driver36","driver37","driverVerify"}){
                auto enabled=dialog->findChild<QCheckBox*>("enable_"+QString(id));
                QVERIFY(enabled);QVERIFY(!enabled->isChecked());QVERIFY(!enabled->isEnabled());
            }
            QDir().mkpath("artifacts");dialog->grab().save("artifacts/download-settings-boot.png");
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        page->findChild<QPushButton*>("downloadSettings")->click();
        QCOMPARE(vm->settings().downloadProfile["flow"].toString(),QString("boot"));
        QVERIFY(!vm->settings().flashRequired);
        QVERIFY(!page->findChild<QLineEdit*>("flashPath")->isEnabled());
        QVERIFY(!page->findChild<QPushButton*>("browseFlash")->isEnabled());
        QVERIFY(!vm->settings().downloadProfile["stepEnabled"].toObject()["pre1001"].toBool());
        QVERIFY(vm->settings().downloadProfile["stepEnabled"].toObject()["boot1002"].toBool());
        QVERIFY(vm->settings().downloadProfile["negativeResponseChecks"].toObject()["pre1001"].toBool());
        QVERIFY(vm->settings().downloadProfile["timeoutChecks"].toObject()["pre1001"].toBool());
        for(const auto id:{"driver34","driver36","driver37","driverVerify"})
            QVERIFY(!vm->settings().downloadProfile["stepEnabled"].toObject()[id].toBool());
        QVERIFY(SettingsStore(temp.path()+"/saved.json").save({vm->settings()},error));
        QVector<ChannelSettings> restored;QVERIFY(SettingsStore(temp.path()+"/saved.json").load(restored,error));
        QCOMPARE(restored[0].downloadProfile,vm->settings().downloadProfile);
        const auto path=temp.path()+"/test.bin";QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QByteArray(32,1));file.close();
        QVERIFY(vm->chooseImage(false,path));QTRY_VERIFY(vm->canConnect());vm->toggleConnection();QTRY_VERIFY(vm->connected()&&!vm->pending());
        vm->start();QTRY_COMPARE_WITH_TIMEOUT(vm->taskState(),TaskState::Completed,6000);
        QString firstTx;for(const auto &line:vm->logs())if(line.contains("step10 10 02编程会话发送")){firstTx=line;break;}
        QVERIFY(!firstTx.isEmpty());
        const auto logs=vm->logs();
        QVERIFY(std::any_of(logs.cbegin(),logs.cend(),[](const QString &line){return line.contains("step10 10 02编程会话正响应");}));
        QVERIFY(std::any_of(logs.cbegin(),logs.cend(),[](const QString &line){return line.contains("step20 36传输数据第1包发送");}));
        for(const auto &line:logs)QVERIFY(!line.contains("UDS TX")&&!line.contains("UDS RX"));
        vm->toggleConnection();QTRY_VERIFY(!vm->connected());
    }
};
QTEST_MAIN(DownloadRevisionTest)
#include "test_download_revision.moc"
