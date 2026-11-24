#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTreeView>
#include <QFontDatabase>
#include <QFile>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QTemporaryDir>
#include "infrastructure/ChannelWorker.h"
#include "protocol/FlashJob.h"
#include "views/ChannelPage.h"
#include "TestEventWait.h"
#include "RomMarkerFixture.h"
using namespace boot;
static QByteArray be16(quint16 value){return QByteArray(1,char(value>>8))+char(value);}
static QByteArray currentDriverFixture(){return marker_fixture::driver();}
struct PanelCanState {communication::SoftwareChannelConfiguration opened;QList<TPCANMsg> sent;};
class PanelCan final:public host::CanHardware {
public:
    explicit PanelCan(std::shared_ptr<PanelCanState> state):state(std::move(state)){}
    communication::HardwareChannels scan(QString &error)override{
        error.clear();communication::HardwareChannel h;h.bus=communication::Bus::Can;h.key="panel-test";h.handle=0x51;h.available=true;return {h};
    }
    bool open(const communication::HardwareChannel&,const communication::SoftwareChannelConfiguration &config,QString &error)override{state->opened=config;error.clear();return true;}
    bool close(QString &error)override{error.clear();return true;}
    communication::Health health(QString &error)override{error.clear();return communication::Health::Ready;}
    bool sendRaw(TPCANMsg frame)override{state->sent.append(frame);return true;}
    DWORD recvRaw(DWORD,TPCANMsg*,TPCANTimestamp*)override{return 0;}
    unsigned long lastErrorCode()const override{return PCAN_ERROR_QRCVEMPTY;}
    QString lastErrorText()const override{return {};}
    bool enableEcho()override{return true;}
private:std::shared_ptr<PanelCanState> state;
};

// Independent wire fixture: fixed routine IDs and request shapes. No hardware.
class ContractWire final:public DiagnosticTransport {
public:
    QList<QByteArray> requests;QMap<quint32,QByteArray> written;
    bool rejectSecurity=false,rejectIdentity=false;quint64 epoch=0;quint32 address=0;
    QByteArray eraseRid=QByteArray::fromHex("ff00"),verifyRid=QByteArray::fromHex("dd02"),dependencyRid=QByteArray::fromHex("ff01");
    bool send(const QByteArray &request,QString &)override{
        requests.append(request);const auto token=epoch;
        QTimer::singleShot(0,this,[this,request,token]{
            if(token!=epoch)return;emit sent();QByteArray reply=request;
            const int sid=quint8(request[0]);
            if((sid==0x10||sid==0x28||sid==0x85)&&quint8(request[1])&0x80)return;
            reply[0]=char(sid+0x40);
            if(sid==0x10)reply=reply.left(2)+QByteArray::fromHex("00140002");
            else if(sid==0x27)reply=reply.left(2)+((quint8(request[1])&1)?QByteArray::fromHex("12345678"):QByteArray());
            else if(sid==0x2e)reply=reply.left(3);
            else if(sid==0x22)reply=rejectIdentity?QByteArray::fromHex("7f2231"):reply.left(3)+QByteArray("SIM-ECU");
            else if(sid==0x14)reply=reply.left(1);
            else if(sid==0x31){
                const auto rid=request.mid(2,2);
                if(rid==eraseRid&&request.size()==13)reply=reply.left(4)+char(0);
                else if(rid==verifyRid&&request.size()==16)reply=rejectSecurity?QByteArray::fromHex("7f3131"):reply.left(4)+char(0);
                else if(rid==dependencyRid&&request.size()==4)reply=reply+char(0);
                else reply=QByteArray::fromHex("7f3131");
            }else if(sid==0x34){address=readBe(request.mid(3,4));written[address].clear();reply=QByteArray::fromHex("74200402");}
            else if(sid==0x36){written[address]+=request.mid(2);reply=reply.left(2);}
            emit received(reply);
        });return true;
    }
    void cancel()override{++epoch;}int maximumPdu()const override{return 4095;}
};
class DownloadFlowContract final:public QObject {
    Q_OBJECT
    static QByteArray linked(quint32 base,int bytes,char fill){QByteArray data(bytes,fill);for(int i=0;i<64;++i)data[i]=0;auto word=[&](int at,quint32 n){for(int i=0;i<4;++i)data[at+i]=char(n>>(8*i));};word(0,0x20001000);word(4,base+0x101);word(8,base+0x105);word(12,base+0x109);return data;}
    static QByteArray nativeApp(int slot,char fill){auto data=QByteArray(4096,char(0xff))+QByteArray(4096,fill);data.replace(0,8,"T010APP1");data[8]=0;data[9]=0x10;data[10]=data[11]=0;data[87]=char(slot);const quint32 base=slot?0x49000:0x15000;data.replace(4096,64,QByteArray(64,0));auto word=[&](int at,quint32 n){for(int i=0;i<4;++i)data[at+i]=char(n>>(8*i));};word(4096,0x20001000);for(int i=1;i<16;++i)word(4096+i*4,base+0x1401);return data;}
private slots:
    void initTestCase(){QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");}
    void fileInspectionIsReadOnlyAndKeepsManualFallback(){
        auto s=host::ChannelSettings::defaults(communication::Bus::Can);s.simulation=true;host::ChannelViewModel vm(s);
        const auto original=vm.settings().toJson();QSignalSpy changed(&vm,&host::ChannelViewModel::settingsChanged);
        QTemporaryDir temp(QCoreApplication::applicationDirPath()+"/download-inspection-XXXXXX");QVERIFY(temp.isValid());
        const QList<QPair<QByteArray,QString>> samples={{currentDriverFixture(),"driver"},{QByteArray("T008APP2").leftJustified(128,'A'),"app"},
            {QByteArray("T010APP1").leftJustified(128,'A'),"app"},{marker_fixture::boot(),"boot"},{QByteArray(4096,'?'),""}};
        int index=0;
        for(const auto &sample:samples){const auto path=temp.filePath(QString::number(index++)+".bin");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(sample.first),qint64(sample.first.size()));file.close();
            QString role="stale",error="stale";QVERIFY2(vm.inspectDownloadFile(path,role,error),qPrintable(error));QCOMPARE(role,sample.second);QVERIFY(error.isEmpty());}
        QString role="stale",error;QVERIFY(!vm.inspectDownloadFile(temp.filePath("missing.bin"),role,error));QVERIFY(role.isEmpty());QVERIFY(!error.isEmpty());
        QCOMPARE(vm.settings().toJson(),original);QCOMPARE(changed.size(),0);
    }
    void fileListCommitDerivesPathsAndRejectsAtomically(){
        auto s=host::ChannelSettings::defaults(communication::Bus::Can);s.simulation=true;host::ChannelViewModel vm(s);QString error;
        auto row=[](const char *role,const char *path,const char *id){return QJsonObject{{"role",role},{"path",path},{"id",id}};};
        const QJsonArray files={row("app","app.bin","a"),row("calib","cal.bin","c"),row("driver","driver.bin","d"),row("boot","boot.bin","b")};
        auto before=vm.settings().toJson();QVERIFY(vm.updateDownloadFiles(files,error));const auto &applied=vm.settings();
        QCOMPARE(applied.flashPath,QString("driver.bin"));QCOMPARE(applied.applicationPath,QString("app.bin"));
        QStringList roles;for(const auto &v:applied.downloadFiles)roles.append(v.toObject()["role"].toString());QCOMPARE(roles,QStringList({"driver","boot","calib","app"}));
        auto after=applied.toJson();for(const auto key:{"downloadFiles","flashPath","applicationPath"}){before.remove(key);after.remove(key);}QCOMPARE(after,before);
        const auto saved=applied.toJson();QSignalSpy commits(&vm,&host::ChannelViewModel::settingsChanged);
        auto duplicate=files;duplicate.append(row("app","replacement.bin","replacement"));QVERIFY(!vm.updateDownloadFiles(duplicate,error));QVERIFY(!error.isEmpty());QCOMPARE(vm.settings().toJson(),saved);QCOMPARE(commits.size(),0);
        auto source=vm.findChild<host::ChannelModel*>();QVERIFY(source);source->taskChanged(host::TaskState::Running,0,{});
        QVERIFY(!vm.updateDownloadFiles({},error));QCOMPARE(vm.settings().toJson(),saved);QCOMPARE(commits.size(),0);
        source->taskChanged(host::TaskState::Completed,100,{});QVERIFY(vm.updateDownloadFiles({},error));
        QVERIFY(vm.settings().downloadFiles.isEmpty());QVERIFY(vm.settings().flashPath.isEmpty());QVERIFY(vm.settings().applicationPath.isEmpty());
    }
    void frontPanelTransportAndRepeatReachWorker_data(){
        QTest::addColumn<QString>("target");
        QTest::newRow("generic")<<QString("generic");QTest::newRow("S32K current")<<QString("s32k144-t008-t009-v2");QTest::newRow("historical T008")<<QString(t008::Target);
    }
    void frontPanelTransportAndRepeatReachWorker(){
        QFETCH(QString,target);QTemporaryDir temp(QCoreApplication::applicationDirPath()+"/panel-worker-XXXXXX");QVERIFY(temp.isValid());
        const auto driver=temp.filePath("driver.bin"),boot=temp.filePath("boot.bin");
        for(const auto &fileData:{qMakePair(driver,currentDriverFixture()),qMakePair(boot,marker_fixture::boot())}){QFile f(fileData.first);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(fileData.second),qint64(fileData.second.size()));}
        auto s=host::ChannelSettings::defaults(communication::Bus::Can);s.simulation=false;s.hardwareKey="panel-test";s.handle=0x51;s.bitrate=250000;s.autoReconnect=true;
        s.downloadTxId="612";s.downloadRxId="6A2";s.functionalId="6DF";s.canNetwork["padding"]=0xaa;
        s.repeatDownloadEnabled=true;s.repeatDownloadCount=3;s.repeatDownloadIntervalMs=17;s.flashAddress="20001400";s.bootBAddress.clear();
        s.downloadFiles={QJsonObject{{"role","driver"},{"path",driver}},QJsonObject{{"role","boot"},{"path",boot}}};
        FlashProfile p;p.target=target;p.flow="boot";p.simulationOnly=false;p.keyProvider="seed-as-key";p.keyLibrary.clear();p.stepEnabled["pre1003"]=true;s.downloadProfile=p.toJson();
        const auto original=s.toJson();auto state=std::make_shared<PanelCanState>();
        host::ChannelWorker worker(s,[state]{return std::make_unique<PanelCan>(state);});QSignalSpy log(&worker,&host::ChannelWorker::logMessage);
        worker.initialize();worker.connectChannel(s);worker.startPreview(s);
        QVERIFY2(waitForTest([&]{return !state->sent.isEmpty();},500),qPrintable(log.isEmpty()?QString("no transmission"):log.last()[0].toString()));
        QCOMPARE(state->opened.bitrate,250000);QVERIFY(state->opened.autoReconnect);
        QCOMPARE(quint32(state->sent.first().ID),quint32(0x612));QCOMPARE(int(state->sent.first().DATA[3]),0xaa);
        bool countReached=false;for(const auto &entry:log)if(entry[0].toString().contains("1/3"))countReached=true;QVERIFY(countReached);
        QCOMPARE(s.toJson(),original);worker.cancelTask();worker.shutdown();
    }
    void currentDriverHonorsConfiguredRoutines_data(){
        QTest::addColumn<quint16>("verify");QTest::addColumn<quint16>("dependency");
        QTest::newRow("saved-T009")<<quint16(0xff01)<<quint16(0xff02);
        QTest::newRow("current-T008")<<quint16(0xdd02)<<quint16(0xff01);
        QTest::newRow("custom-menu-values")<<quint16(0xa001)<<quint16(0xb001);
    }
    void currentDriverHonorsConfiguredRoutines(){
        QFETCH(quint16,verify);QFETCH(quint16,dependency);
        FirmwareImage driver;driver.segments={{0x20001400,currentDriverFixture()}};QString error;
        QVERIFY2(t008::validateCurrentDriver(driver,false,error),qPrintable(error));
        FirmwareImage boot;boot.binary=true;boot.segments={{0x5000,marker_fixture::boot()}};
        FlashProfile p;p.flow="boot";p.bootHasB=false;p.resetWaitMs=0;p.verifyRoutine=verify;p.dependencyRoutine=dependency;
        p.negativeResponseChecks["driverVerify"]=true;p.negativeResponseChecks["appVerify"]=true;
        const auto saved=p.toJson();
        for(int round=0;round<2;++round){
            ContractWire wire;wire.verifyRid=be16(verify);wire.dependencyRid=be16(dependency);
            UdsSession session(wire,{20,20,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);
            QVERIFY2(job.start({},driver,boot,error),qPrintable(error));QVERIFY(waitForTest([&]{return done.size()==1;},1500));
            QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
            QVERIFY(wire.requests.contains(QByteArray::fromHex("3101")+be16(verify)+be32(0x5000)+be32(0xf000)+be32(0)));
            QVERIFY(wire.requests.contains(QByteArray::fromHex("3101")+be16(dependency)));QCOMPARE(p.toJson(),saved);
        }
    }
    void savedRoutinesRemainEditableInExistingMenu(){
        auto s=host::ChannelSettings::defaults(communication::Bus::Can);s.simulation=true;
        FlashProfile p;QString error;QVERIFY(FlashProfile::fromJson(s.downloadProfile,p,error));p.verifyRoutine=0xff01;p.dependencyRoutine=0xff02;s.downloadProfile=p.toJson();
        p.flow="boot";p.stepEnabled["pre1003"]=false;s.downloadProfile=p.toJson();
        host::ChannelViewModel vm(s);host::ChannelPage page(&vm);bool saved=false;
        QTimer::singleShot(0,&page,[&]{
            auto *d=page.findChild<QDialog*>("downloadSettingsDialog");QVERIFY(d);
            auto *verify=d->findChild<QLineEdit*>("verifyRoutine"),*dependency=d->findChild<QLineEdit*>("dependencyRoutine");
            QVERIFY(verify&&dependency);QVERIFY(!verify->isReadOnly()&&verify->isEnabled());QVERIFY(!dependency->isReadOnly()&&dependency->isEnabled());
            QVERIFY(!d->findChild<QCheckBox*>("enable_pre1003")->isChecked());
            verify->setText("DD02");dependency->setText("FF01");
            d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();saved=d->result()==QDialog::Accepted;if(!saved)d->reject();
        });page.findChild<QPushButton*>("downloadSettings")->click();QVERIFY(saved);
        QVERIFY(FlashProfile::fromJson(vm.settings().downloadProfile,p,error));QCOMPARE(p.verifyRoutine,quint16(0xdd02));QCOMPARE(p.dependencyRoutine,quint16(0xff01));
        QVERIFY(!p.enabled("pre1003"));
    }
    void identityObservationHonorsCheckboxAndNegativeCheck(){
        for(bool enabled:{false,true}){
            FlashProfile p;p.flow="boot";p.bootHasB=false;p.resetWaitMs=0;p.identityDid=0xf189;
            p.stepEnabled["identity"]=enabled;p.negativeResponseChecks["identity"]=true;
            FirmwareImage boot;boot.binary=true;boot.segments={{0x5000,linked(0x5000,4096,'B')}};
            ContractWire wire;wire.rejectIdentity=true;UdsSession session(wire,{20,20,1000,0});
            FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString error;
            QVERIFY2(job.start({}, {},boot,error),qPrintable(error));QVERIFY(waitForTest([&]{return done.size()==1;},1500));
            QCOMPARE(wire.requests.contains(QByteArray::fromHex("22f189")),enabled);
            QVERIFY(!done[0][0].toBool()); // Disabled checks keep the existing incomplete-result semantics.
            QVERIFY2(done[0][1].toString().contains(enabled?QString("NRC"):QString::fromUtf8("\u672a\u5b8c\u5168\u786e\u8ba4")),qPrintable(done[0][1].toString()));
        }
    }
    void plotBackfillWindowPreservesNewestFramesAndClearOrigin(){
        host::SignalTransmitViewModel vm(host::signal::Bus::Can);qint64 observed=0;
        connect(&vm,&host::SignalTransmitViewModel::framesObserved,&vm,[&](const host::FrameBatch &batch){observed+=batch.size();});
        for(int i=0;i<30000;++i){host::FrameRecord f;f.captureUs=100000+i*100;f.payload=be32(i);vm.observeFrames({f});}
        const auto history=vm.traceHistory();QCOMPARE(observed,qint64(30000));QCOMPARE(history.size(),10000);
        for(int i=0;i<history.size();++i){QCOMPARE(history[i].payload,be32(20000+i));QCOMPARE(history[i].timeUs,qint64((20000+i)*100));}
        vm.clearTrace();QVERIFY(vm.traceHistory().isEmpty());host::FrameRecord next;next.captureUs=9000000;vm.observeFrames({next});QCOMPARE(vm.traceHistory().first().timeUs,qint64(0));
    }
    void actualLengthsAndContinuousHex(){
        QString error;QVector<Segment> a,b;FirmwareImage image;image.binary=true;
        image.segments={{0x15000,QByteArray(8192,'A')}};
        QVERIFY(FirmwareImage::planSlots(image,0x15000,0,false,0x57000,a,b,error));QCOMPARE(a[0].data.size(),8192);
        image.segments[0].data=nativeApp(0,'A')+nativeApp(1,'B');
        QVERIFY(FirmwareImage::planSlots(image,0x15000,0x49000,true,0x33000,a,b,error));QCOMPARE(a[0].data.size(),8192);QCOMPARE(b[0].data.size(),8192);QCOMPARE(b[0].data,nativeApp(1,'B'));
        image.segments[0].data.chop(1);QVERIFY(!FirmwareImage::planSlots(image,0x15000,0x49000,true,0x33000,a,b,error));
        image.binary=false;image.segments={{0x15000,QByteArray(4096,'A')},{0x16000,QByteArray(4096,'a')},{0x49000,QByteArray(4096,'B')}};
        QVERIFY(FirmwareImage::planSlots(image,0x15000,0x49000,true,0x33000,a,b,error));QCOMPARE(a.size(),1);QCOMPARE(a[0].data.size(),8192);QCOMPARE(b[0].data.size(),4096);
        image.segments[1].address=0x17000;QVERIFY(!FirmwareImage::planSlots(image,0x15000,0x49000,true,0x33000,a,b,error));
        image.segments={{0x16000,QByteArray(4096,'A')}};QVERIFY(!FirmwareImage::planSlots(image,0x15000,0x49000,true,0x33000,a,b,error));
        image.segments={{0x15000,QByteArray(0x58000,'A')}};QVERIFY(!FirmwareImage::planSlots(image,0x15000,0,false,0x57000,a,b,error));
    }
    void bAddressDoesNotImplyDualBin(){
        FirmwareImage image;image.binary=true;QString error;QVector<Segment> a,b;
        image.segments={{0x15000,nativeApp(0,'A')}};
        QVERIFY(FirmwareImage::planSlots(image,0x15000,0x49000,true,0x33000,a,b,error));QCOMPARE(a[0].data,nativeApp(0,'A'));QVERIFY(b.isEmpty());
        image.segments={{0x5000,linked(0x5000,8192,'A')}};
        QVERIFY(FirmwareImage::planSlots(image,0x5000,0x6d000,true,0xf000,a,b,error));QCOMPARE(a[0].data.size(),8192);QVERIFY(b.isEmpty());
        image.segments={{0x5000,QByteArray(8192,'A')}};
        QVERIFY(!FirmwareImage::planSlots(image,0x5000,0x6d000,true,0xf000,a,b,error));
        image.segments={{0x5000,linked(0x5000,8192,'A')+linked(0x6d000,8192,'B')}};
        QVERIFY(!FirmwareImage::planSlots(image,0x5000,0x6d000,true,0xf000,a,b,error));QVERIFY(error.contains("ambiguous"));
    }
    void compilerNativeBinKeepsActualLength(){
        const auto path=qEnvironmentVariable("DOWNLOAD_CONTRACT_APP_BIN");
        if(path.isEmpty())QSKIP("Set DOWNLOAD_CONTRACT_APP_BIN for read-only validation of an actual compiler artifact");
        QString error;FirmwareImage image;QVector<Segment> a,b;t008::Plan plan;
        QVERIFY2(FirmwareImage::loadTarget(path,0x15000,0,false,0x57000,image,error),qPrintable(error));
        QVERIFY2(FirmwareImage::planSlots(image,0x15000,0,false,0x57000,a,b,error),qPrintable(error));
        FirmwareImage selected;selected.binary=true;selected.segments=a;
        QVERIFY2(t008::completeApplication(selected,0x15000,0x57000,false,0,plan,error,true),qPrintable(error));
        QFile original(path);QVERIFY(original.open(QIODevice::ReadOnly));const auto bytes=original.readAll();
        QCOMPARE(a.size(),1);QCOMPARE(a[0].data,bytes);QCOMPARE(plan.application[0].data,bytes);QVERIFY(bytes.size()<0x57000);
    }
    void compilerBootBinsKeepTheirLinkAddresses(){
        const auto root=qEnvironmentVariable("DOWNLOAD_CONTRACT_BOOT_ROOT");
        if(root.isEmpty())QSKIP("Set DOWNLOAD_CONTRACT_BOOT_ROOT for native Boot A/B validation");
        auto read=[&](const QString &name){QFile file(root+"/"+name);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();};
        const auto a=read("boot-a.bin"),b=read("boot-b.bin"),ab=read("boot-ab.bin");
        QCOMPARE(a.size(),0xf000);QCOMPARE(b.size(),0xf000);QCOMPARE(ab,a+b);
        FirmwareImage image;image.binary=true;QString error;QVector<Segment> primary,alternate;
        image.segments={{0x5000,a}};
        QVERIFY2(FirmwareImage::planSlots(image,0x5000,0x6d000,true,0xf000,primary,alternate,error),qPrintable(error));
        QCOMPARE(primary[0].data,a);QVERIFY(alternate.isEmpty());
        image.segments[0].data=ab;
        QVERIFY2(FirmwareImage::planSlots(image,0x5000,0x6d000,true,0xf000,primary,alternate,error),qPrintable(error));
        QCOMPARE(primary[0].data,a);QCOMPARE(alternate[0].data,b);QCOMPARE(alternate[0].address,quint32(0x6d000));
        image.segments[0].data=b+a;
        QVERIFY(!FirmwareImage::planSlots(image,0x5000,0x6d000,true,0xf000,primary,alternate,error));
    }
    void originalRomLoopAndOneFinalDependency(){
        FlashProfile p;p.flow="boot";p.resetWaitMs=0;p.bootEraseLength=0xf000;p.bootHasB=true;p.bootBBase=0x6d000;
        p.applicationEraseBase=0x15000;p.applicationEraseLength=0x57000;p.applicationHasB=false;
        FirmwareImage boot;boot.binary=true;boot.segments={{0x5000,marker_fixture::boot()+marker_fixture::boot(0x6d000)}};
        FirmwareImage app;app.binary=true;app.segments={{0x15000,marker_fixture::app()}};
        FirmwareImage driver;driver.segments={{0x20001400,marker_fixture::driver()}};
        FirmwareImage cal;const auto page=marker_fixture::calib();cal.segments={{0x7e000,page}};
        ContractWire wire;UdsSession session(wire,{20,20,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString error;
        QVERIFY2(job.start(app,driver,boot,cal,error),qPrintable(error));QVERIFY(waitForTest([&]{return done.size()==1;},3000));QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QList<QByteArray> control;for(const auto &r:wire.requests)if(quint8(r[0])!=0x36)control.append(r);
        QList<QByteArray> loop;for(const auto &r:control)if(quint8(r[0])==0x34||quint8(r[0])==0x37||r.startsWith(QByteArray::fromHex("3101")))loop.append(r);
        const QList<QByteArray> expected={QByteArray::fromHex("3400442000140000000900"),QByteArray::fromHex("37"),QByteArray::fromHex("3101dd02200014000000090000000000"),
            QByteArray::fromHex("3101ff0044000050000000f000"),QByteArray::fromHex("340044000050000000f000"),QByteArray::fromHex("37"),QByteArray::fromHex("3101dd02000050000000f00000000000"),
            QByteArray::fromHex("3101ff00440007e00000001000"),QByteArray::fromHex("3400440007e00000001000"),QByteArray::fromHex("37"),QByteArray::fromHex("3101dd020007e0000000100000000000"),
            QByteArray::fromHex("3101ff00440001500000057000"),QByteArray::fromHex("3400440001500000003000"),QByteArray::fromHex("37"),QByteArray::fromHex("3101dd02000150000000300000000000"),QByteArray::fromHex("3101ff01")};
        QCOMPARE(loop,expected);QCOMPARE(wire.written[0x5000],marker_fixture::boot());QCOMPARE(wire.written[0x15000],marker_fixture::app());QCOMPARE(wire.written[0x7e000],page);
        for(const auto &r:wire.requests)QVERIFY(!r.startsWith(QByteArray::fromHex("3101ff02")));
    }
    void ecuMustActuallyAcknowledgeSecurity(){
        FlashProfile p;p.flow="boot";p.resetWaitMs=0;p.applicationEraseBase=0x15000;p.applicationEraseLength=0x57000;p.negativeResponseChecks["appVerify"]=true;
        FirmwareImage app;app.binary=true;app.segments={{0x15000,QByteArray(4096,'A')}};
        ContractWire wire;wire.rejectSecurity=true;UdsSession session(wire,{20,20,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString error;
        QVERIFY(job.start(app,{},error));QVERIFY(waitForTest([&]{return done.size()==1;},1500));QVERIFY(!done[0][0].toBool());QVERIFY(!wire.requests.contains(QByteArray::fromHex("3101ff01")));QVERIFY(!wire.requests.contains(QByteArray::fromHex("1101")));
    }
    void thirtyOriginalCheckboxRulesAndNoProtocolSelector(){
        auto s=host::ChannelSettings::defaults(communication::Bus::Can);s.simulation=true;host::ChannelViewModel vm(s);host::ChannelPage page(&vm);bool checked=false;
        QTimer::singleShot(0,&page,[&]{auto d=page.findChild<QDialog*>("downloadSettingsDialog");QVERIFY(d);struct Close{QDialog *d;~Close(){d->reject();}}close{d};QVERIFY(!d->findChild<QComboBox*>("downloadTarget"));QCOMPARE(downloadSteps().size(),30);
            for(const auto &step:downloadSteps()){const QString id=step.id;auto enable=d->findChild<QCheckBox*>("enable_"+id),negative=d->findChild<QCheckBox*>("negative_"+id),timeout=d->findChild<QCheckBox*>("timeout_"+id);QVERIFY(enable&&negative&&timeout);QVERIFY(enable->isEnabled());QVERIFY(enable->isChecked());QCOMPARE(negative->isEnabled(),!requiresNegativeResponseCheck(id));QCOMPARE(timeout->isEnabled(),!suppressesPositiveResponse(id)&&!fixedUncheckedTimeout(id));}
            for(const auto id:{"app34","app36","app37","appVerify"})QVERIFY(d->findChild<QLabel*>("step_"+QString(id))->text().contains(QString::fromUtf8("\xC2\xB7 ")));
            checked=true;
        });page.findChild<QPushButton*>("downloadSettings")->click();QVERIFY(checked);
    }
    void singleFrameEventsAreBatchedWithoutLossAndClearStaysClear(){
        auto s=host::ChannelSettings::defaults(communication::Bus::Can);s.simulation=true;host::ChannelViewModel vm(s);
        auto source=vm.findChild<host::ChannelModel*>();QVERIFY(source);QSignalSpy recorded(vm.frames(),&host::FrameTableModel::recorded);
        for(int i=0;i<20000;++i){host::FrameRecord frame;frame.captureUs=i*100;frame.channel="CAN1";frame.direction="Rx";frame.typed=true;frame.id=0x715;frame.payload=be32(i);source->framesReceived({frame});}
        QCOMPARE(vm.frames()->historyCount(),qint64(0));
        QTRY_COMPARE(vm.frames()->historyCount(),qint64(20000));QCOMPARE(recorded.size(),1);
        QCOMPARE(vm.frames()->history().first().payload,be32(0));QCOMPARE(vm.frames()->history().last().payload,be32(19999));QCOMPARE(vm.frames()->rowCount(),host::FrameTableModel::Capacity);
        host::FrameRecord next;next.captureUs=2000000;next.channel="CAN1";next.direction="Rx";source->framesReceived({next});
        vm.flushMonitorFrames();QCOMPARE(vm.frames()->historyCount(),qint64(20001)); // Export/pause can take an exact snapshot.
        source->framesReceived({next});
        vm.frames()->clear();QTest::qWait(25);QCOMPARE(vm.frames()->historyCount(),qint64(0));
        source->framesReceived({next});QTRY_COMPARE(vm.frames()->historyCount(),qint64(1));QCOMPARE(vm.frames()->history().first().relativeSeconds,0.0);
    }
};
QTEST_MAIN(DownloadFlowContract)
#include "test_download_flow_contract.moc"
