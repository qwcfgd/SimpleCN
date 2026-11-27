#include <QtTest>
#include <QApplication>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include "localization/Language.h"
#include "views/UiLanguageController.h"
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTemporaryDir>
#include "protocol/FirmwareImage.h"
#include "protocol/FlashJob.h"
#include "protocol/SimulatedUdsEcu.h"
#include <optional>
#include "protocol/T008Target.h"
#include "domain/HostTypes.h"
#include "views/ChannelPage.h"
#include "TestEventWait.h"
#include "RomMarkerFixture.h"
using namespace boot;using namespace host;
class CurrentWire final:public DiagnosticTransport {
public:
    QList<QByteArray> requests;SimulatedUdsEcu ecu;quint64 epoch=0;
    std::function<std::optional<QByteArray>(const QByteArray &)> overrideReply;
    explicit CurrentWire(const FlashProfile &p):ecu(p){ecu.maxBlockLength=1026;ecu.setMemory(0x5000,marker_fixture::boot());ecu.setMemory(0x15000,marker_fixture::app(8192));ecu.setMemory(0x7e000,marker_fixture::calib());}
    bool send(const QByteArray &request,QString &)override{requests+=request;const auto e=epoch;QTimer::singleShot(0,this,[this,request,e]{if(e!=epoch)return;emit sent();
        if(overrideReply){const auto reply=overrideReply(request);if(reply){if(!reply->isEmpty())emit received(*reply);return;}}
        for(auto reply:ecu.handle(request)){if(reply.startsWith(char(0x50)))reply=reply.left(2)+QByteArray::fromHex("00140002");emit received(reply);}
    });return true;}
    void cancel()override{++epoch;}int maximumPdu()const override{return 4095;}
};
class CalibRevisionTest final:public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    static void le(QByteArray &b,int at,quint32 n,int count=4){for(int i=0;i<count;++i)b[at+i]=char(n>>(8*i));}
    static QByteArray linkedBoot(quint32 base,char fill){Q_UNUSED(fill);return marker_fixture::boot(base);}
    static FirmwareImage app(bool t010=false,int slot=0,quint32 base=0x15000){
        if(!t010){auto bytes=marker_fixture::app(8192);for(int i=1;i<16;++i)le(bytes,4096+i*4,base+0x1401);marker_fixture::seal(bytes,ROM_MARKER_APP);FirmwareImage result;result.segments={{base,bytes}};return result;}
        QByteArray bytes(8192,char(0x5a));bytes.replace(0,4096,QByteArray(4096,char(0xff)));bytes.replace(0,8,t010?"T010APP1":"T008APP2");le(bytes,8,4096);le(bytes,12,1,2);
        bytes.replace(14,16,"PROJECT-EXAMPLE1");bytes.replace(30,16,"HARDWARE-EXAMPLE");bytes.replace(46,9,t010?"T010A0001":"T008A0001");bytes.replace(55,16,"CONFIG-EXAMPLE01");bytes.replace(71,16,"CALIB-EXAMPLE001");bytes[87]=t010?char(slot):char(0xff);
        le(bytes,4096,0x20001000);for(int i=1;i<16;++i)le(bytes,4096+i*4,base+0x1401);
        FirmwareImage result;result.segments={{base,bytes}};return result;
    }
    static QByteArray calib(){return marker_fixture::calib();}
    static FirmwareImage currentDriver(bool t010=false){
        if(!t010){FirmwareImage result;result.segments={{0x20001400,marker_fixture::driver()}};return result;}
        QByteArray data(2124,char(0x55));data.replace(0,128,QByteArray(128,char(0)));data.replace(0,8,"T008DRV1");le(data,8,128,2);le(data,10,1,2);le(data,12,t010?9:8,2);le(data,16,data.size());le(data,20,0x20001400);le(data,24,data.size());le(data,28,256);
        for(int i=0;i<5;++i)le(data,32+i*4,128+2*i);le(data,52,192);le(data,56,4096);le(data,60,1024);le(data,68,20000000);data.replace(72,16,QByteArray(16,'D'));le(data,88,138);
        FirmwareImage result;result.segments={{0x20001400,data}};return result;
    }
    static FirmwareImage currentCalib(){FirmwareImage result;result.binary=true;result.segments={{0x7e000,calib()}};return result;}
    static FlashProfile currentProfile(bool t010=false){FlashProfile p;p.target=t010?"s32k144-t010-v2":"s32k144-t008-t009-v2";p.flow="boot";p.stepEnabled["pre1003"]=true;p.resetWaitMs=0;p.bootEraseLength=0xf000;p.bootBBase=0x6d000;p.applicationEraseBase=0x15000;p.applicationEraseLength=t010?0x33000:0x57000;return p;}
    static QStringList order(QTableWidget *t){QStringList result;for(int i=0;i<t->rowCount();++i)result+=t->item(i,0)->data(Qt::UserRole).toString();return result;}
    static QSet<QString> selected(QTableWidget *t){QSet<QString> result;for(const auto &i:t->selectionModel()->selectedRows())result.insert(t->item(i.row(),0)->data(Qt::UserRole+1).toString());return result;}
private slots:
    void uniqueRolesAndOldConfiguration(){
        QJsonArray source={QJsonObject{{"role","app"},{"path","app.bin"}},QJsonObject{{"role","calib"},{"path","calib.bin"}},QJsonObject{{"role","driver"},{"path","driver.bin"}},QJsonObject{{"role","boot"},{"path","boot.bin"}}};
        QJsonArray result;QString e;QVERIFY(effectiveDownloadFiles(source,result,&e));QCOMPARE(result[0].toObject()["role"].toString(),QString("driver"));QCOMPARE(result[1].toObject()["role"].toString(),QString("boot"));QCOMPARE(result[2].toObject()["role"].toString(),QString("calib"));QCOMPARE(result[3].toObject()["role"].toString(),QString("app"));
        const auto original=source;source.append(source.last());const auto duplicate=source;QVERIFY(!effectiveDownloadFiles(source,result,&e));QVERIFY(e.contains(QString::fromUtf8("重复")));QCOMPARE(source,duplicate);QVERIFY(result.isEmpty());
        auto s=ChannelSettings::defaults(communication::Bus::Can);s.downloadFiles=source;auto json=s.toJson();ChannelSettings preserved;preserved.softwareId="KEEP";QVERIFY(!ChannelSettings::fromJson(json,preserved,e));QCOMPARE(preserved.softwareId,QString("KEEP"));QCOMPARE(json["downloadFiles"].toArray(),source);
        s.downloadFiles=original;ChannelSettings restored;QVERIFY2(ChannelSettings::fromJson(s.toJson(),restored,e),qPrintable(e));QCOMPARE(restored.calibAddress,s.calibAddress);QCOMPARE(restored.calibLength,s.calibLength);
    }
    void calibrationRanges(){
        QString e;FirmwareImage i;i.binary=true;i.segments={{0x7e000,calib()}};QVERIFY(FirmwareImage::validateCalibration(i,0x7e000,4096,e));
        for(auto base:{0x7c000u,0x7d000u,0x14000u,0x48000u})QVERIFY2(FirmwareImage::validateCalibration(i,base,4096,e),qPrintable(e));
        QVERIFY(FirmwareImage::validateCalibration(i,0x7e000,8192,e));
        for(auto range:{qMakePair(0x7e001u,4096u),qMakePair(0x7e000u,0u),qMakePair(0x7e000u,2048u),qMakePair(0xfffff000u,8192u)})QVERIFY2(FirmwareImage::validateCalibration(i,range.first,range.second,e),qPrintable(e));
        i.segments[0].data.chop(1);QVERIFY(!FirmwareImage::validateCalibration(i,0x7e000,4096,e));
        i.binary=false;i.segments={{0x7d000,QByteArray(4096,'C')}};QVERIFY(!FirmwareImage::validateCalibration(i,0x7e000,4096,e));
        i.segments={{0x7e000,QByteArray(2048,'C')},{0x7e900,QByteArray(1792,'C')}};QVERIFY(!FirmwareImage::validateCalibration(i,0x7e000,4096,e));
    }
    void calibrationUsesConfiguredEraseRange_data(){
        QTest::addColumn<bool>("binary");QTest::addColumn<bool>("eraseRejected");QTest::addColumn<quint32>("eraseLength");
        QTest::newRow("BIN-relocated")<<true<<false<<quint32(4096);
        QTest::newRow("HEX-relocated-larger-erase")<<false<<false<<quint32(8192);
        QTest::newRow("BIN-ECU-rejects-range")<<true<<true<<quint32(8192);
        QTest::newRow("HEX-ECU-rejects-range")<<false<<true<<quint32(4096);
    }
    void calibrationUsesConfiguredEraseRange(){
        QFETCH(bool,binary);QFETCH(bool,eraseRejected);QFETCH(quint32,eraseLength);
        const quint32 base=0x90000;const auto page=calib();QByteArray fileBytes=page;
        if(!binary){
            auto record=[](quint16 address,int type,const QByteArray &payload){QByteArray r;r+=char(payload.size());r+=char(address>>8);r+=char(address);r+=char(type);r+=payload;quint8 sum=0;for(unsigned char b:r)sum+=b;r+=char(-sum);return QByteArray(":")+r.toHex().toUpper()+"\n";};
            fileBytes=record(0,4,QByteArray::fromHex("0009"));for(int at=0;at<page.size();at+=32)fileBytes+=record(quint16(at),0,page.mid(at,32));fileBytes+=record(0,1,{});
        }
        const auto path=temp.filePath(binary?"relocated-calib.bin":"relocated-calib.hex");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(fileBytes),qint64(fileBytes.size()));file.close();
        QString error;FirmwareImage image;QVERIFY2(FirmwareImage::loadCalibration(path,base,eraseLength,image,error),qPrintable(error));QCOMPARE(image.segments[0].address,base);QCOMPARE(image.segments[0].data,page);
        if(!binary){FirmwareImage mismatch;QVERIFY(!FirmwareImage::loadCalibration(path,base+4096,eraseLength,mismatch,error));}
        auto p=currentProfile();p.calibBase=base;p.calibLength=eraseLength;p.negativeResponseChecks["erase"]=true;CurrentWire wire(p);
        const auto erase=QByteArray::fromHex("3101ff0044")+be32(base)+be32(eraseLength);
        if(eraseRejected)wire.overrideReply=[erase](const QByteArray &r)->std::optional<QByteArray>{if(r==erase)return QByteArray::fromHex("7f3131");return std::nullopt;};
        UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);
        QVERIFY2(job.start({},currentDriver(),{},image,error),qPrintable(error));QVERIFY(waitForTest([&]{return done.size()==1;},1500));QCOMPARE(done[0][0].toBool(),!eraseRejected);QVERIFY(wire.requests.contains(erase));
        const auto download=QByteArray::fromHex("340044")+be32(base)+be32(quint32(page.size()));QCOMPARE(wire.requests.contains(download),!eraseRejected);
        QVERIFY(!wire.requests.contains(QByteArray::fromHex("3101ff00440007e00000001000")));if(!eraseRejected)QCOMPARE(wire.ecu.memory(base),page);
    }
    void calibrationMarkerIsValidatedAndNeverRepaired(){
        QString error;RomMarker marker;auto page=marker_fixture::calib();const auto original=page;
        QVERIFY(FirmwareImage::validateMarker({0x7e000,page},ROM_MARKER_CAL,marker,error));QCOMPARE(marker.kind,quint32(ROM_MARKER_CAL));QCOMPARE(page,original);
        page[0]=char(quint8(page[0])^1);QVERIFY(!FirmwareImage::validateMarker({0x7e000,page},ROM_MARKER_CAL,marker,error));
        page=original;page[4095]=char(quint8(page[4095])^1);QVERIFY(!FirmwareImage::validateMarker({0x7e000,page},ROM_MARKER_CAL,marker,error));
    }
    void descriptorCoverage(){
        QString e;t008::Plan plan;auto image=app();QVERIFY2(t008::completeApplication(image,0x15000,0x57000,false,0,plan,e),qPrintable(e));QCOMPARE(plan.application.size(),1);QCOMPARE(plan.application[0].data.size(),8192);
        auto split=image;split.segments={{0x15000,image.segments[0].data.left(4096)},{0x16000,image.segments[0].data.mid(4096)}};QVERIFY(t008::completeApplication(split,0x15000,0x57000,false,0,plan,e));
        split.segments[1].address=0x17000;QVERIFY(!t008::completeApplication(split,0x15000,0x57000,false,0,plan,e));
        auto missing=image;missing.segments[0].data.chop(1024);QVERIFY(!t008::completeApplication(missing,0x15000,0x57000,false,0,plan,e));
        for(int bad:{0,1024,0x57000}){auto badHeader=image;le(badHeader.segments[0].data,8,quint32(bad));QVERIFY(!t008::completeApplication(badHeader,0x15000,0x57000,false,0,plan,e));}
        auto padding=image;padding.segments[0].data[88]=0;QVERIFY(!t008::completeApplication(padding,0x15000,0x57000,false,0,plan,e));
        auto b=app(true,1,0x49000);QVERIFY(t008::completeApplication(b,0x49000,0x33000,true,1,plan,e));QVERIFY(!t008::completeApplication(b,0x49000,0x33000,true,0,plan,e));QVERIFY(!t008::completeApplication(b,0x49000,0x33000,false,0,plan,e));
        auto unknown=image;unknown.segments[0].data[0]='X';QVERIFY(!t008::completeApplication(unknown,0x15000,0x57000,false,0,plan,e));
    }
    void driverHeaderMustBeFullyValidated(){
        QString error;auto driver=currentDriver();QVERIFY(t008::validateCurrentDriver(driver,false,error));auto t010=driver;QVERIFY(t008::validateCurrentDriver(t010,true,error,11));le(t010.segments[0].data,74,1,2);le(t010.segments[0].data,76,9,2);marker_fixture::seal(t010.segments[0].data,ROM_MARKER_DRIVER);QVERIFY(!t008::validateCurrentDriver(t010,true,error,11));
        for(int offset:{8,14,16,20,24,28,32,52,56,60,64,68,88,92}){auto bad=driver;bad.segments[0].data[64+offset]=char(0xff);marker_fixture::seal(bad.segments[0].data,ROM_MARKER_DRIVER);QVERIFY2(!t008::validateCurrentDriver(bad,false,error),qPrintable(QString::number(offset)));}
        auto bad=driver;bad.segments[0].address=0x20000000;QVERIFY(t008::validateCurrentDriver(bad,false,error));
    }
    void currentOrderRomSecurityAndFinalDependency(){
        auto p=currentProfile();CurrentWire wire(p);wire.ecu.t008BootSelection=true;
        FirmwareImage boot;boot.binary=true;boot.segments={{0x5000,linkedBoot(0x5000,'A')+linkedBoot(0x6d000,'B')}};
        const auto image=app(),calibration=currentCalib(),driver=currentDriver();QString e;UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);
        QVERIFY2(job.start(image,driver,boot,calibration,e),qPrintable(e));QVERIFY(waitForTest([&]{return done.size()==1;},3000));QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QList<quint32> addresses;int security=0,dependencies=0;for(const auto &r:wire.requests){
            if(r.startsWith(QByteArray::fromHex("340044")))addresses+=readBe(r.mid(3,4));
            if(r.startsWith(QByteArray::fromHex("3101dd02"))){++security;QCOMPARE(r.size(),16);QCOMPARE(r.mid(12),QByteArray(4,char(0)));}
            if(r.startsWith(QByteArray::fromHex("3101ff01"))){++dependencies;QCOMPARE(r,QByteArray::fromHex("3101ff01"));}
            QVERIFY(!r.startsWith(QByteArray::fromHex("3101ff02")));
        }
        QCOMPARE(addresses,QList<quint32>({0x20001400,0x6d000,0x7e000,0x15000}));QCOMPARE(security,4);QCOMPARE(dependencies,1);
        QCOMPARE(wire.ecu.memory(0x6d000),linkedBoot(0x6d000,'B'));QCOMPARE(wire.ecu.memory(0x7e000),calibration.segments[0].data);QCOMPARE(wire.ecu.memory(0x15000),image.segments[0].data);
    }
    void currentDependencyFailureStops_data(){QTest::addColumn<int>("status");for(int n:{1,2,3,4,0x72,-1})QTest::newRow(qPrintable(QString::number(n)))<<n;}
    void currentDependencyFailureStops(){
        QFETCH(int,status);auto p=currentProfile();p.negativeResponseChecks["dependency"]=true;p.timeoutChecks["dependency"]=true;CurrentWire wire(p);int dependencies=0;
        wire.overrideReply=[&](const QByteArray &r)->std::optional<QByteArray>{if(r==QByteArray::fromHex("3101ff01")){++dependencies;if(status<0)return QByteArray();if(status==0x72)return QByteArray::fromHex("7f3172");return QByteArray::fromHex("7101ff01")+char(status);}return std::nullopt;};
        UdsSession session(wire,{20,20,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString e;
        QVERIFY2(job.start(app(),currentDriver(),{},currentCalib(),e),qPrintable(e));QVERIFY(waitForTest([&]{return done.size()==1;},2000));QVERIFY(!done[0][0].toBool());QCOMPARE(dependencies,1);QVERIFY(!wire.requests.contains(QByteArray::fromHex("1101")));
    }
    void t010AndFutureDualDual_data(){QTest::addColumn<bool>("t010");QTest::newRow("T010 single Boot dual App")<<true;QTest::newRow("future dual Boot dual App")<<false;}
    void t010AndFutureDualDual(){
        QFETCH(bool,t010);auto p=currentProfile(t010);p.applicationEraseLength=8192;p.applicationHasB=true;p.applicationBBase=t010?0x49000:0x41000;p.identityDid=0xf189;
        auto a=app(t010,0,0x15000),b=app(t010,1,p.applicationBBase);if(t010)b.segments[0].data.replace(46,9,"T010B0002");else marker_fixture::seal(b.segments[0].data,ROM_MARKER_APP,"AST1","20261011");FirmwareImage combined;combined.binary=true;combined.segments={{0x15000,a.segments[0].data+b.segments[0].data}};
        FirmwareImage boot;if(!t010){boot.binary=true;boot.segments={{0x5000,linkedBoot(0x5000,'A')+linkedBoot(0x6d000,'B')}};}
        CurrentWire wire(p);wire.ecu.t008BootSelection=!t010;
        wire.overrideReply=[&](const QByteArray &r)->std::optional<QByteArray>{if(r==QByteArray::fromHex("3101ff00440001500000002000"))return QByteArray::fromHex("7f3131");if(r==QByteArray::fromHex("22f189"))return QByteArray::fromHex("62f189")+(t010?QByteArray("T010B0002"):b.segments[0].data.mid(46,9));return std::nullopt;};
        UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString e;
        QVERIFY2(job.start(combined,currentDriver(t010),boot,currentCalib(),e),qPrintable(e));QVERIFY(waitForTest([&]{return done.size()==1;},3000));QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));QVERIFY(done[0][1].toString().contains(QString::fromUtf8("F189 版本匹配")));
        QCOMPARE(wire.ecu.memory(p.applicationBBase),b.segments[0].data);int count=0;for(const auto &r:wire.requests)if(r.startsWith(QByteArray::fromHex("3101dd02"))&&readBe(r.mid(4,4))==p.applicationBBase){++count;QCOMPARE(readBe(r.mid(8,4)),quint32(8192));}QCOMPARE(count,1);
    }
    void absentCalibNeverTriggersAWritingTarget(){
        auto p=currentProfile();CurrentWire wire(p);wire.ecu.setMemory(0x7e000,{});UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString e;
        QVERIFY(job.start(app(),currentDriver(),{},e));QVERIFY(waitForTest([&]{return done.size()==1;},1000));QVERIFY(!done[0][0].toBool());QVERIFY(done[0][1].toString().contains("status 4"));
        for(const auto &r:wire.requests){QVERIFY(r!=QByteArray::fromHex("3101ff00440007e00000001000"));QVERIFY(r!=QByteArray::fromHex("3400440007e00000001000"));}
    }
    void actualCompilerArtifacts(){
        const auto t008=qEnvironmentVariable("CALIB_TEST_T008_ROOT"),t010=qEnvironmentVariable("CALIB_TEST_T010_ROOT");if(t008.isEmpty()||t010.isEmpty())QSKIP("Set both compiler artifact roots for read-only offline validation");
        QString e;for(bool layout:{false,true}){
            const QDir root(layout?t010:t008);FirmwareImage driver;QVERIFY2(FirmwareImage::load(root.filePath("S32K144_FlashDriver_IAR/build/driver.bin"),0x20001400,driver,e),qPrintable(e));QVERIFY2(t008::validateCurrentDriver(driver,layout,e),qPrintable(e));
            if(!layout){for(const auto suffix:{"bin","hex"}){FirmwareImage image;t008::Plan plan;QVERIFY2(FirmwareImage::load(root.filePath(QString("S32K144_App_IAR/build/app/app.%1").arg(suffix)),0x15000,image,e),qPrintable(e));QVERIFY2(t008::completeApplication(image,0x15000,0x57000,false,0,plan,e),qPrintable(e));QCOMPARE(plan.application[0].data.size(),143360);}}
        }
    }
    void actualSlotContainersAndNonFfTail(){
        const auto t008=qEnvironmentVariable("CALIB_TEST_T008_ROOT"),t010=qEnvironmentVariable("CALIB_TEST_T010_ROOT");if(t008.isEmpty()||t010.isEmpty())QSKIP("Set both compiler artifact roots for read-only offline validation");
        QString e;for(bool layout:{false,true}){
            QDir root(layout?t010:t008);const auto path=root.filePath(layout?"S32K144_App_IAR/build/app-slot-ab.bin":"S32K144_App_IAR/build/app/app-slot.bin");FirmwareImage container;const quint32 length=layout?0x33000:0x57000;
            QVERIFY2(FirmwareImage::load(path,0x15000,container,e),qPrintable(e));QVector<Segment> primary,alternate;QVERIFY2(FirmwareImage::planSlots(container,0x15000,0x49000,layout,length,primary,alternate,e),qPrintable(e));QCOMPARE(alternate.isEmpty(),!layout);
            for(int n=0;n<(layout?2:1);++n){FirmwareImage selected;selected.binary=true;selected.segments=n?alternate:primary;const auto base=n?0x49000:0x15000;t008::Plan plan;
                QVERIFY2(t008::completeApplication(selected,base,length,layout,n,plan,e,true),qPrintable(e));QCOMPARE(plan.application[0].data.size(),int(length));
                FirmwareImage canonical;QVERIFY2(FirmwareImage::load(root.filePath(layout?QString("S32K144_App_IAR/build/app-%1/app.bin").arg(n?"b":"a"):QString("S32K144_App_IAR/build/app/app.bin")),base,canonical,e),qPrintable(e));QVERIFY(plan.application[0].data.startsWith(canonical.segments[0].data));QCOMPARE(plan.application[0].data,selected.segments[0].data);
                selected.segments.last().data[selected.segments.last().data.size()-1]=0;QVERIFY(!t008::completeApplication(selected,base,length,layout,n,plan,e,true));QVERIFY(e.contains("tail must be FF"));
            }
        }
    }
    void calibrationRunsOneFinalDependency(){
        auto p=currentProfile();CurrentWire wire(p);UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString e;
        QVERIFY(job.start({},currentDriver(),{},currentCalib(),e));QVERIFY(waitForTest([&]{return done.size()==1;},1000));QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QCOMPARE(wire.requests.count(QByteArray::fromHex("3101ff01")),1);QCOMPARE(wire.requests.count(QByteArray::fromHex("3101ff02")),0);
    }
    void currentPreflightFailsBeforeAnyRequest(){
        auto p=currentProfile();CurrentWire wire(p);UdsSession session(wire,{50,100,1000,0});QString e;auto invalid=currentDriver();invalid.segments[0].data[12]=4;
        FlashJob oldDriver(session,p,std::make_unique<SimulationKey>());QVERIFY(!oldDriver.start(app(),invalid,{},currentCalib(),e));QVERIFY(wire.requests.isEmpty());
        auto broken=app();broken.segments[0].data.chop(4096);FlashJob partial(session,p,std::make_unique<SimulationKey>());QVERIFY(!partial.start(broken,currentDriver(),{},currentCalib(),e));QVERIFY(wire.requests.isEmpty());
        auto badCal=currentCalib();badCal.segments[0].data[4095]=char(0);FlashJob badIdentity(session,p,std::make_unique<SimulationKey>());QVERIFY(!badIdentity.start(app(),currentDriver(),{},badCal,e));QVERIFY(wire.requests.isEmpty());
        auto protectedPage=p;protectedPage.applicationEraseBase=0x7d000;FlashJob config(session,protectedPage,std::make_unique<SimulationKey>());auto relocated=app(false,0,0x7d000);QVERIFY2(config.start(relocated,currentDriver(),{},currentCalib(),e),qPrintable(e));config.cancel();
    }
    void roleEditingPreservesIdentityAndSelection(){
        auto s=ChannelSettings::defaults(communication::Bus::Can);s.simulation=true;s.downloadFiles={QJsonObject{{"role","app"},{"path","app.bin"},{"id","APP-ID"}},QJsonObject{{"role","driver"},{"path","driver.bin"},{"id","DRIVER-ID"}},QJsonObject{{"role","boot"},{"path","boot.bin"},{"id","BOOT-ID"}}};
        ChannelViewModel vm(s);ChannelPage page(&vm);QTRY_VERIFY(vm.canConnect()&&!vm.pending());auto table=page.findChild<QTableWidget*>("downloadFiles");QVERIFY(table);QCOMPARE(order(table),QStringList({"driver","boot","app"}));
        table->selectionModel()->select(table->model()->index(0,0),QItemSelectionModel::Select|QItemSelectionModel::Rows);table->selectionModel()->select(table->model()->index(2,0),QItemSelectionModel::Select|QItemSelectionModel::Rows);const auto ids=selected(table);
        QTimer::singleShot(0,&page,[&]{auto d=page.findChild<QInputDialog*>();QVERIFY(d);auto combo=d->findChild<QComboBox*>();QVERIFY(combo);combo->setCurrentIndex(combo->findText("Calib"));d->accept();});
        QVERIFY(QMetaObject::invokeMethod(table,"cellDoubleClicked",Qt::DirectConnection,Q_ARG(int,2),Q_ARG(int,0)));
        QCOMPARE(order(table),QStringList({"driver","boot","calib"}));QCOMPARE(table->item(2,1)->text(),QString("app.bin"));QCOMPARE(table->item(2,0)->data(Qt::UserRole+1).toString(),QString("APP-ID"));QCOMPARE(selected(table),ids);
        const auto before=vm.settings().downloadFiles;QString warning;QTimer answer;answer.setInterval(5);connect(&answer,&QTimer::timeout,&page,[&]{if(auto d=page.findChild<QInputDialog*>()){auto combo=d->findChild<QComboBox*>();if(combo)combo->setCurrentIndex(combo->findText("Boot"));d->accept();}if(auto box=page.findChild<QMessageBox*>()){warning=box->text();box->accept();}});answer.start();
        QVERIFY(QMetaObject::invokeMethod(table,"cellDoubleClicked",Qt::DirectConnection,Q_ARG(int,2),Q_ARG(int,0)));answer.stop();QVERIFY(!warning.isEmpty());QCOMPARE(vm.settings().downloadFiles,before);QCOMPARE(selected(table),ids);QCOMPARE(order(table),QStringList({"driver","boot","calib"}));
    }
    void settingsRefreshDuringRoleDialogPreservesFileIdentity(){
        auto s=ChannelSettings::defaults(communication::Bus::Can);s.simulation=true;s.downloadFiles={QJsonObject{{"role","app"},{"path","original.bin"}},QJsonObject{{"role","driver"},{"path","driver.bin"}}};
        ChannelViewModel vm(s);ChannelPage page(&vm);QVERIFY(waitForTest([&]{return vm.canConnect()&&!vm.pending();},1000));auto table=page.findChild<QTableWidget*>("downloadFiles");QVERIFY(table);
        table->selectionModel()->select(table->model()->index(1,0),QItemSelectionModel::Select|QItemSelectionModel::Rows);const auto ids=selected(table);QCOMPARE(ids.size(),1);
        auto updated=vm.settings();updated.rxdEnabled=!updated.rxdEnabled;QVERIFY(vm.setSettings(updated));QCOMPARE(selected(table),ids);
        bool refreshed=false;QTimer::singleShot(0,&page,[&]{auto d=page.findChild<QInputDialog*>();QVERIFY(d);auto latest=vm.settings();latest.rxdEnabled=!latest.rxdEnabled;QVERIFY(vm.setSettings(latest));refreshed=true;auto combo=d->findChild<QComboBox*>();QVERIFY(combo);combo->setCurrentIndex(combo->findText("Calib"));d->accept();});
        QVERIFY(QMetaObject::invokeMethod(table,"cellDoubleClicked",Qt::DirectConnection,Q_ARG(int,1),Q_ARG(int,0)));QVERIFY(refreshed);QCOMPARE(order(table),QStringList({"driver","calib"}));QCOMPARE(table->item(1,1)->text(),QString("original.bin"));QCOMPARE(selected(table),ids);
    }
    void slotLengthBinTransfersDeclaredCoverage_data(){QTest::addColumn<bool>("dual");QTest::newRow("single")<<false;QTest::newRow("dual")<<true;}
    void slotLengthBinTransfersDeclaredCoverage(){
        QFETCH(bool,dual);auto p=currentProfile(dual);p.applicationHasB=dual;p.applicationBBase=dual?0x49000:0;const auto original=app(dual);auto bin=original;bin.binary=true;bin.segments[0].data+=QByteArray(int(p.applicationEraseLength)-8192,char(0xff));
        // The compiler marker covers its declared image; the BIN container adds only erased FF bytes.
        if(dual){auto b=app(true,1,0x49000).segments[0].data;b+=QByteArray(int(p.applicationEraseLength)-8192,char(0xff));bin.segments[0].data+=b;}
        CurrentWire wire(p);wire.overrideReply=[dual](const QByteArray &r)->std::optional<QByteArray>{if(dual&&r==QByteArray::fromHex("3101ff00440001500000033000"))return QByteArray::fromHex("7f3131");return std::nullopt;};
        UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString e;const auto preserved=bin.segments[0].data;
        QVERIFY2(job.start(bin,currentDriver(dual),{},currentCalib(),e),qPrintable(e));QVERIFY(waitForTest([&]{return done.size()==1;},1500));QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));QCOMPARE(bin.segments[0].data,preserved);
        const quint32 base=dual?0x49000:0x15000;QVERIFY(wire.requests.contains(QByteArray::fromHex("3101ff0044")+be32(base)+be32(p.applicationEraseLength)));QVERIFY(wire.requests.contains(QByteArray::fromHex("340044")+be32(base)+be32(8192)));QVERIFY(wire.requests.contains(QByteArray::fromHex("3101ff01")));QCOMPARE(wire.ecu.memory(base).size(),8192);
    }
    void appRequestDownloadUsesMarkerLength_data(){
        QTest::addColumn<int>("mode");QTest::newRow("native-BIN")<<0;QTest::newRow("continuous-HEX")<<1;QTest::newRow("BIN-with-external-FF-tail")<<2;QTest::newRow("BIN-marker-declares-full-slot")<<3;
    }
    void appRequestDownloadUsesMarkerLength(){
        QFETCH(int,mode);auto p=currentProfile();auto image=app();image.binary=mode!=1;
        const quint32 declared=mode==3?p.applicationEraseLength:0x4000u;
        image.segments[0].data+=QByteArray(int(declared)-image.segments[0].data.size(),char(0xff));marker_fixture::seal(image.segments[0].data,ROM_MARKER_APP);
        if(mode==2)image.segments[0].data+=QByteArray(int(p.applicationEraseLength-declared),char(0xff));
        const auto preserved=image.segments[0].data;CurrentWire wire(p);UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString error;
        QVERIFY2(job.start(image,currentDriver(),{},currentCalib(),error),qPrintable(error));QVERIFY(waitForTest([&]{return done.size()==1;},2000));QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QVERIFY(wire.requests.contains(QByteArray::fromHex("3101ff0044")+be32(p.applicationEraseBase)+be32(p.applicationEraseLength)));
        QVERIFY(wire.requests.contains(QByteArray::fromHex("340044")+be32(p.applicationEraseBase)+be32(declared)));QCOMPARE(wire.ecu.memory(p.applicationEraseBase).size(),int(declared));QCOMPARE(image.segments[0].data,preserved);
    }
    void ecuOwnsBinDestination_data(){
        QTest::addColumn<quint32>("address");QTest::addColumn<quint32>("eraseBytes");QTest::addColumn<bool>("deny");
        QTest::newRow("ECU accepts Calib pair for App bytes")<<quint32(0x7e000)<<quint32(0x1000)<<false;
        QTest::newRow("unaligned request refused")<<quint32(0x70003)<<quint32(0x15)<<true;
        QTest::newRow("zero erase length refused")<<quint32(0x1a000)<<quint32(0)<<true;
        QTest::newRow("separate encodable fields exceed 32-bit end")<<quint32(0xfffffff3)<<quint32(0x100)<<true;
    }
    void ecuOwnsBinDestination(){
        QFETCH(quint32,address);QFETCH(quint32,eraseBytes);QFETCH(bool,deny);
        auto p=currentProfile();p.applicationEraseBase=address;p.applicationEraseLength=eraseBytes;
        auto image=app();image.binary=true;const auto bytes=image.segments.first().data;
        const auto erase=QByteArray::fromHex("3101ff0044")+be32(address)+be32(eraseBytes);
        const auto download=QByteArray::fromHex("340044")+be32(address)+be32(quint32(bytes.size()));
        CurrentWire wire(p);
        if(deny)wire.overrideReply=[&](const QByteArray &req)->std::optional<QByteArray>{
            if(req==erase)return QByteArray::fromHex("7f3131");return std::nullopt;
        };
        UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());
        QSignalSpy done(&job,&FlashJob::finished);QString e;
        QVERIFY2(job.start(image,currentDriver(),e),qPrintable(e));QVERIFY(waitForTest([&]{return !done.isEmpty();},3000));
        QCOMPARE(wire.requests.count(erase),1);
        if(deny){QVERIFY(!done.first()[0].toBool());QCOMPARE(wire.requests.last(),erase);QVERIFY(!wire.requests.contains(download));}
        else{
            QVERIFY(wire.requests.indexOf(download)>wire.requests.indexOf(erase));QCOMPARE(wire.ecu.memory(address),bytes);
            // This simulated ECU detects the role conflict at final dependency checking, after transfer.
            QVERIFY(!done.first()[0].toBool());QVERIFY(done.first()[1].toString().contains("status 4"));
        }
    }
    void wrongDriverAddressUsesEcuRefusal(){
        auto p=currentProfile();auto driver=currentDriver();driver.binary=true;driver.segments.first().address=0x20002203;
        const auto request=QByteArray::fromHex("340044")+be32(driver.segments.first().address)+be32(quint32(driver.segments.first().data.size()));
        CurrentWire wire(p);wire.overrideReply=[&](const QByteArray &r)->std::optional<QByteArray>{
            if(r==request)return QByteArray::fromHex("7f3431");return std::nullopt;
        };
        UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QString e;
        QSignalSpy done(&job,&FlashJob::finished);QVERIFY2(job.start(app(),driver,e),qPrintable(e));
        QVERIFY(waitForTest([&]{return !done.isEmpty();},2000));QVERIFY(!done.first()[0].toBool());QCOMPARE(wire.requests.last(),request);
    }
    void appMarkerLengthDoesNotAuthorizeNonFfAfterCode(){
        auto p=currentProfile();auto image=app();image.binary=true;image.segments[0].data+=QByteArray(8192,char(0xff));image.segments[0].data[8192]=0;marker_fixture::seal(image.segments[0].data,ROM_MARKER_APP);
        CurrentWire wire(p);UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QString error;
        QVERIFY(!job.start(image,currentDriver(),{},currentCalib(),error));QVERIFY(wire.requests.isEmpty());
    }
    void slotLengthBinRejectsNonFfTailAndInvalidCoverage(){
        auto p=currentProfile();const auto validApp=app();FirmwareImage bin=validApp;bin.binary=true;bin.segments[0].data+=QByteArray(int(p.applicationEraseLength)-8192,char(0xff));QString e;
        auto reject=[&](const FirmwareImage &bad){CurrentWire wire(p);UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());const bool accepted=job.start(bad,currentDriver(),{},currentCalib(),e);return !accepted&&wire.requests.isEmpty();};
        for(int offset:{8192,int(p.applicationEraseLength)-1}){auto bad=bin;bad.segments[0].data[offset]=0;QVERIFY(reject(bad));QVERIFY(e.contains("tail must be FF"));}
        auto bad=bin;bad.segments[0].data.chop(1);QVERIFY(!reject(bad));bad=bin;bad.segments[0].data.append(char(0xff));QVERIFY(!reject(bad));bad=bin;le(bad.segments[0].data,8,p.applicationEraseLength);QVERIFY(reject(bad));
        bad=bin;bad.binary=false;QVERIFY(reject(bad)); // Absolute-address HEX never gains the BIN tail exception.
        bad=validApp;bad.binary=true;QVERIFY(!reject(bad)); // Native BIN uses its actual aligned length, independent of 31 capacity.
    }
    void genericSingleTargetKeepsOriginalUnalignedRanges(){
        FlashProfile p;p.resetWaitMs=0;p.applicationEraseBase=0x21003;p.applicationEraseLength=0x5000;FirmwareImage raw,driver;raw.binary=true;raw.segments={{0x21003,QByteArray(3001,'A')}};driver.segments={{0x20000000,QByteArray(255,'D')}};
        CurrentWire wire(p);UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString e;
        QVERIFY2(job.start(raw,driver,e),qPrintable(e));QVERIFY(waitForTest([&]{return done.size()==1;},1000));QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));QVERIFY(wire.requests.contains(QByteArray::fromHex("340044")+be32(0x21003)+be32(3001)));QCOMPARE(wire.ecu.memory(0x21003),raw.segments[0].data);
    }
    void continuousGenericFilesUseOne34Each(){
        FlashProfile p;p.resetWaitMs=0;p.applicationEraseBase=0x21003;p.applicationEraseLength=0x5000;
        const QByteArray code(3001,'A'),ram(255,'D');FirmwareImage image,driver;image.segments={{0x21003,code.left(1500)},{0x21003+1500,code.mid(1500)}};driver.segments={{0x20000000,ram.left(100)},{0x20000064,ram.mid(100)}};
        CurrentWire wire(p);UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString error;
        QVERIFY2(job.start(image,driver,error),qPrintable(error));QVERIFY(waitForTest([&]{return done.size()==1;},1500));QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QList<QByteArray> requests;int blocks=0;for(const auto &r:wire.requests){if(r.startsWith(QByteArray::fromHex("340044")))requests+=r;if(r.startsWith(char(0x36)))++blocks;}
        QCOMPARE(requests,QList<QByteArray>({QByteArray::fromHex("340044")+be32(0x20000000)+be32(255),QByteArray::fromHex("340044")+be32(0x21003)+be32(3001)}));QVERIFY(blocks>2);QCOMPARE(wire.ecu.memory(0x21003),code);QCOMPARE(wire.ecu.memory(0x20000000),ram);
    }
    void noncontinuousFileAndDifferent31StartSendNothing_data(){QTest::addColumn<int>("mode");QTest::newRow("App-hole")<<0;QTest::newRow("Driver-hole")<<1;QTest::newRow("App-start-differs-from31")<<2;}
    void noncontinuousFileAndDifferent31StartSendNothing(){
        QFETCH(int,mode);FlashProfile p;p.resetWaitMs=0;p.applicationEraseBase=0x21003;p.applicationEraseLength=0x5000;
        FirmwareImage image,driver;image.segments={{0x21003,QByteArray(3001,'A')}};driver.segments={{0x20000000,QByteArray(255,'D')}};
        if(mode==0)image.segments={{0x21003,QByteArray(100,'A')},{0x21003+101,QByteArray(100,'B')}};
        if(mode==1)driver.segments={{0x20000000,QByteArray(100,'D')},{0x20000065,QByteArray(100,'E')}};
        if(mode==2)image.segments[0].address+=1;
        CurrentWire wire(p);UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QString error;QVERIFY(!job.start(image,driver,error));QVERIFY(wire.requests.isEmpty());
    }
    void mismatchedHexStopsFromStartButton_data(){
        QTest::addColumn<QString>("role");QTest::addColumn<bool>("lin");
        for(const auto *role:{"driver","boot","calib","app"})for(bool lin:{false,true})
            QTest::newRow(qPrintable(QString(role)+(lin?"-LIN":"-CAN")))<<QString(role)<<lin;
    }
    void mismatchedHexStopsFromStartButton(){
        QFETCH(QString,role);QFETCH(bool,lin);
        const QString hexPath=temp.filePath("other-origin.hex"),binPath=temp.filePath("other-file.bin");
        QFile hex(hexPath),bin(binPath);QVERIFY(hex.open(QIODevice::WriteOnly));QVERIFY(bin.open(QIODevice::WriteOnly));
        hex.write(":01012300AB30\n:00000001FF\n");hex.close();bin.write("data");bin.close();
        auto s=ChannelSettings::defaults(lin?communication::Bus::Lin:communication::Bus::Can);s.simulation=true;
        s.flashAddress="00000300";s.applicationAddress="00000400";s.bootAddress="00000500";s.calibAddress="00000600";
        s.downloadFiles.append(QJsonObject{{"role","driver"},{"path",role=="driver"?hexPath:binPath}});
        if(role=="driver")s.downloadFiles.append(QJsonObject{{"role","app"},{"path",binPath}});
        else s.downloadFiles.append(QJsonObject{{"role",role},{"path",hexPath}});
        ChannelViewModel vm(s);ChannelPage page(&vm);page.show();auto model=vm.findChild<ChannelModel*>();QVERIFY(model);
        QSignalSpy frames(model,&ChannelModel::framesReceived);
        QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());
        auto button=page.findChild<QPushButton*>("startButton");QVERIFY(button&&button->isEnabled());button->click();
        QTRY_COMPARE_WITH_TIMEOUT(vm.taskState(),TaskState::Failed,3000);
        const auto warnings=vm.logs().join('\n');
        const QString configured=role=="driver"?s.flashAddress:role=="boot"?s.bootAddress:role=="calib"?s.calibAddress:s.applicationAddress;
        QVERIFY2(warnings.contains("HEX")&&warnings.contains("00000123")&&warnings.contains(configured),qPrintable(warnings));
        const auto *status=page.findChild<QLabel*>("taskText");QVERIFY(status);QVERIFY(status->text().contains("00000123"));
        QCOMPARE(frames.size(),0);
        QVERIFY(hex.open(QIODevice::ReadOnly));QCOMPARE(hex.readAll(),QByteArray(":01012300AB30\n:00000001FF\n"));
    }
    void genericWorkerLoadsShortBinWithoutSlotGate(){
        const auto appPath=temp.filePath("generic-short.bin"),driverPath=temp.filePath("generic-driver.bin");for(const auto &pair:{qMakePair(appPath,QByteArray(301,'A')),qMakePair(driverPath,QByteArray(255,'D'))}){QFile f(pair.first);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(pair.second),qint64(pair.second.size()));}
        auto s=ChannelSettings::defaults(communication::Bus::Can);s.simulation=true;s.applicationAddress="00021003";s.applicationLength="00005000";s.applicationBAddress.clear();s.downloadFiles={QJsonObject{{"role","app"},{"path",appPath}},QJsonObject{{"role","driver"},{"path",driverPath}}};
        ChannelViewModel vm(s);QVERIFY(waitForTest([&]{return vm.canConnect()&&!vm.pending();},1000));vm.toggleConnection();QVERIFY(waitForTest([&]{return vm.connected()&&!vm.pending();},1000));vm.start();QVERIFY(waitForTest([&]{return vm.taskState()==TaskState::Completed||vm.taskState()==TaskState::Failed;},15000));QVERIFY2(vm.taskState()==TaskState::Completed,qPrintable(vm.logs().join(";")));vm.toggleConnection();QVERIFY(waitForTest([&]{return !vm.connected()&&!vm.pending();},1000));
    }
    void currentDriverUsesValidatedLayoutWithoutProtocolSelector(){
        auto p=currentProfile();p.target="generic";CurrentWire wire(p);UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QString e;
        QVERIFY2(job.start(app(),currentDriver(),{},currentCalib(),e),qPrintable(e));job.cancel();
        auto invalid=currentDriver();invalid.segments[0].data[12]=char(0xff);p=currentProfile();FlashJob reject(session,p,std::make_unique<SimulationKey>());QVERIFY(!reject.start(app(),invalid,{},currentCalib(),e));
    }
    void originalProfileUiRulesAndTranslations(){
        auto s=ChannelSettings::defaults(communication::Bus::Can);s.simulation=true;s.downloadProfile=currentProfile().toJson();ChannelViewModel vm(s);ChannelPage page(&vm);bool checked=false;
        QTimer::singleShot(0,&page,[&]{auto d=page.findChild<QDialog*>("downloadSettingsDialog");QVERIFY(d);struct CloseDialog{QDialog *dialog;~CloseDialog(){Language::instance().setCode("zh_CN");dialog->reject();}}close{d};
            QVERIFY(!d->findChild<QComboBox*>("downloadTarget"));QCOMPARE(d->findChild<QLineEdit*>("verifyRoutine")->text(),QString("DD02"));QCOMPARE(d->findChild<QLineEdit*>("dependencyRoutine")->text(),QString("FF01"));
            for(const auto id:{"enable_appVerify","enable_dependency","enable_driverVerify","negative_appVerify","timeout_appVerify"}){auto check=d->findChild<QCheckBox*>(id);QVERIFY(check);QVERIFY(check->isEnabled());}
            Language::instance().setCode("en");UiLanguageController::instance().refresh();const QRegularExpression chinese("[\\x{3400}-\\x{9fff}]");
            for(auto label:d->findChildren<QLabel*>())QVERIFY2(!label->text().contains(chinese),qPrintable(label->objectName()+": "+label->text()));
            checked=true;
        });page.findChild<QPushButton*>("downloadSettings")->click();Language::instance().setCode("zh_CN");QVERIFY(checked);
    }
    void calibrationParametersRemainEditable_data(){QTest::addColumn<bool>("lin");QTest::newRow("CAN")<<false;QTest::newRow("LIN")<<true;}
    void calibrationParametersRemainEditable(){
        QFETCH(bool,lin);auto s=ChannelSettings::defaults(lin?communication::Bus::Lin:communication::Bus::Can);s.simulation=true;ChannelViewModel vm(s);ChannelPage page(&vm);bool saved=false;
        QTimer::singleShot(0,&page,[&]{auto d=page.findChild<QDialog*>("downloadSettingsDialog");QVERIFY(d);struct CloseDialog{QDialog *dialog;~CloseDialog(){dialog->reject();}}close{d};
            auto address=d->findChild<QLineEdit*>("calibAddress"),length=d->findChild<QLineEdit*>("calibLength");QVERIFY(address&&length);QVERIFY(address->isEnabled()&&length->isEnabled());
            address->setText("00090000");length->setText("00002000");auto buttons=d->findChild<QDialogButtonBox*>();QVERIFY(buttons);buttons->button(QDialogButtonBox::Ok)->click();saved=d->result()==QDialog::Accepted;
        });page.findChild<QPushButton*>("downloadSettings")->click();QVERIFY(saved);QCOMPARE(vm.settings().calibAddress,QString("00090000"));QCOMPARE(vm.settings().calibLength,QString("00002000"));
        QString error;ChannelSettings restored;QVERIFY(ChannelSettings::fromJson(vm.settings().toJson(),restored,error));QCOMPARE(restored.calibAddress,vm.settings().calibAddress);QCOMPARE(restored.calibLength,vm.settings().calibLength);
    }
    void downloadParameterLayout(){
        auto s=ChannelSettings::defaults(communication::Bus::Can);s.simulation=true;ChannelViewModel vm(s);ChannelPage page(&vm);bool checked=false;
        QTimer::singleShot(0,&page,[&]{auto d=page.findChild<QDialog*>("downloadSettingsDialog");QVERIFY(d);
            auto field=[&](const char *name){return d->findChild<QLineEdit*>(name);};auto form=[](QLineEdit *f){return qobject_cast<QFormLayout*>(f->parentWidget()->parentWidget()->layout());};auto row=[](QFormLayout *layout,QLineEdit *f){int n=-1;QFormLayout::ItemRole role;layout->getWidgetPosition(f->parentWidget(),&n,&role);return n;};
            auto cal=field("calibAddress"),length=field("calibLength"),fp=field("fingerprintHex"),appLength=field("settingsApplicationLength"),tx=field("downloadTxId"),rx=field("downloadRxId");QVERIFY(cal&&length&&fp&&appLength&&tx&&rx);
            auto left=form(cal),right=form(tx);QVERIFY(left&&right);QCOMPARE(row(left,cal),row(left,appLength)+1);QCOMPARE(row(left,length),row(left,cal)+1);QCOMPARE(row(left,fp),left->rowCount()-1);QCOMPARE(row(right,tx),1);QCOMPARE(row(right,rx),2);
            QCOMPARE(cal->text().toUInt(nullptr,16),quint32(0x7e000));QCOMPARE(length->text().toUInt(nullptr,16),quint32(0x1000));
            const auto screenshot=qEnvironmentVariable("CALIB_UI_SCREENSHOT");if(!screenshot.isEmpty()){QTest::qWait(50);auto content=d->findChild<QScrollArea*>()->widget();content->adjustSize();QCoreApplication::processEvents();QVERIFY(content->grab().save(screenshot));}
            checked=true;d->reject();});page.findChild<QPushButton*>("downloadSettings")->click();QVERIFY(checked);
    }
};
QTEST_MAIN(CalibRevisionTest)
#include "test_calib_revision.moc"
