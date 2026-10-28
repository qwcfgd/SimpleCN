#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include "protocol/T008Target.h"
#include "protocol/FlashJob.h"
#include "protocol/CanTransport.h"
#include "infrastructure/SettingsStore.h"
#include "views/ChannelPage.h"
using namespace boot;
namespace {
void put(QByteArray &b,int at,quint32 v,int count=4){for(int i=0;i<count;++i)b[at+i]=char(v>>(8*i));}
FirmwareImage image(quint32 base,const QByteArray &data){FirmwareImage i;i.segments={{base,data}};i.size=data.size();return i;}
struct Fixture {
    FirmwareImage app,driver;
    Fixture(){
        QByteArray descriptor(4096,char(0xff)),data(0x408,char(0x55)),native(136,0);
        descriptor.replace(0,8,"T008APP2");put(descriptor,8,0x408);put(descriptor,12,1,2);
        descriptor.replace(14,16,QByteArray("S32K144TAUTOSAR").leftJustified(16,'\0'));descriptor.replace(30,16,QByteArray("Rev.A4.06.5").leftJustified(16,'\0'));
        descriptor.replace(46,9,"T008A0001");descriptor.replace(55,16,QByteArray(16,'C'));descriptor.replace(71,16,QByteArray(16,'L'));
        put(data,0,0x20001400);put(data,4,0x21401);
        native.replace(0,8,"T008DRV1");put(native,8,128,2);put(native,10,1,2);put(native,12,1,2);put(native,16,136);put(native,20,0x20001400);put(native,24,136);put(native,28,256);
        for(int n=0;n<5;++n)put(native,32+4*n,128);
        put(native,52,15);put(native,56,4096);put(native,60,8);put(native,68,20000000);native.replace(72,16,QByteArray(16,'D'));
        app=image(0x20000,descriptor+data);driver=image(0x20001400,native);
    }
};
FlashProfile profile(QString flow="app"){
    FlashProfile p;p.applicationEraseBase=0x20000;p.applicationEraseLength=0x57000;p.target=t008::Target;p.flow=flow;p.simulationOnly=false;p.keyProvider="external-generatekeyex";
    p.keyLibrary="configured-external.dll";p.eraseRoutine=0xff00;p.verifyRoutine=0xff01;p.dependencyRoutine=0xff02;p.identityDid=0xf180;p.resetWaitMs=0;return p;
}
class TestKey final:public KeyProvider {
public:int bytes=16;QByteArray calculate(quint8,const QByteArray &,QString &error)override{error.clear();return QByteArray(bytes,'K');}
};
class MemoryTransport final:public DiagnosticTransport {
public:
    QList<QByteArray> requests;std::function<QByteArray(const QByteArray &)> respond;quint64 epoch=0;
    QByteArray failRequest;QString failMessage;
    bool send(const QByteArray &p,QString &error)override{
        error.clear();requests.append(p);const auto e=epoch;
        QTimer::singleShot(0,this,[this,p,e]{if(e!=epoch)return;if(p==failRequest){emit failed(failMessage);return;}emit sent();const auto reply=respond(p);if(e==epoch&&!reply.isEmpty())emit received(reply);});return true;
    }
    void cancel()override{++epoch;emit cancelled();}
    int maximumPdu()const override{return 4095;}
};
struct Peer {
    t008::Plan plan;bool inBoot=false,reset=false;QByteArray corruptRequest,replaceResponse;bool append=false,silent=false;
    bool strictSessions=false,strictPreconditions=false,securityUnlocked=false;int session=1,programmingResponses=0;
    QByteArray reply(const QByteArray &r){
        QByteArray p;const int sid=quint8(r[0]);
        if(sid==0x10){
            const int next=quint8(r[1])&0x7f;
            // Authoritative Boot Dcm masks for 01/02/03: 07/06/05.
            // Session reference bits are default(1), programming(2), extended(3).
            const int bit=session==1?1:session==2?2:4;
            const int mask=next==1?7:next==2?6:next==3?5:0;
            if(strictSessions&&!(mask&bit))p=QByteArray::fromHex("7f107e");
            else{
                session=next;securityUnlocked=false;if(next==2){inBoot=true;++programmingResponses;}
                if(quint8(r[1])&0x80)return {};
                p=QByteArray::fromHex("50")+r[1]+QByteArray::fromHex("00140002");
            }
        }
        else if(sid==0x27){if(!(quint8(r[1])&1))securityUnlocked=true;p=QByteArray::fromHex("67")+r[1]+((quint8(r[1])&1)?QByteArray(16,'S'):QByteArray());}
        else if(r==QByteArray::fromHex("22f189"))p=QByteArray::fromHex("62f189")+plan.version;
        else if(r==QByteArray::fromHex("22f180"))p=QByteArray::fromHex("62f180")+QByteArray(14,'I');
        else if(sid==0x31){
            // App authoritative RID0203 permits extended session, locked level only.
            if(strictPreconditions&&!inBoot&&r==QByteArray::fromHex("31010203")&&(session!=3||securityUnlocked))p=QByteArray::fromHex("7f3133");
            else p=QByteArray::fromHex("71")+r.mid(1,3)+(r==QByteArray::fromHex("3101dd01")?QByteArray():QByteArray(1,0));
        }
        else if(sid==0x2e)p=QByteArray::fromHex("6ef184");
        else if(sid==0x34)p=QByteArray::fromHex("74200402");
        else if(sid==0x36)p=QByteArray::fromHex("76")+r[1];
        else if(sid==0x37)p=QByteArray::fromHex("77");
        else if(sid==0x11){reset=true;session=1;inBoot=false;p=QByteArray::fromHex("5101");}
        else if(sid==0x14)p=QByteArray::fromHex("54");
        else if(sid==0x28||sid==0x85)return {};
        if(!corruptRequest.isEmpty()&&r.startsWith(corruptRequest))return silent?QByteArray():append?p+char(0):replaceResponse;
        return p;
    }
};
}
class T008TargetTest final:public QObject {
    Q_OBJECT
private slots:
    void descriptorV2(){
        Fixture f;auto &b=f.app.segments[0].data;
        b.replace(0,4096,QByteArray(4096,char(0xff)));b.replace(0,8,"T008APP2");put(b,8,0x408);put(b,12,1,2);
        b.replace(14,16,QByteArray("S32K144TAUTOSAR").leftJustified(16,'\0'));b.replace(30,16,QByteArray("Rev.A4.06.5").leftJustified(16,'\0'));
        b.replace(46,9,QByteArray("T008A0001"));b.replace(55,16,QByteArray(16,'C'));b.replace(71,16,QByteArray(16,'L'));
        t008::Plan p;QString error;QVERIFY2(t008::images(f.app,f.driver,p,error),qPrintable(error));
        QCOMPARE(p.project,QByteArray("S32K144TAUTOSAR"));QCOMPARE(p.hardware,QByteArray("Rev.A4.06.5"));QCOMPARE(p.version,QByteArray("T008A0001"));QCOMPARE(p.minBoot,quint16(1));
        QCOMPARE(p.configuration,QByteArray(16,'C'));QCOMPARE(p.calibration,QByteArray(16,'L'));
        auto original=b;put(b,12,1000,2);QVERIFY(!t008::images(f.app,f.driver,p,error));b=original;
        b[42]='X';QVERIFY(!t008::images(f.app,f.driver,p,error));b=original;
        b[30]=char(0x80);QVERIFY(!t008::images(f.app,f.driver,p,error));b=original;
        b[87]=0;QVERIFY(!t008::images(f.app,f.driver,p,error));b=original;b[88]=0;QVERIFY(!t008::images(f.app,f.driver,p,error));b=original;
        b[0]='X';QVERIFY(!t008::images(f.app,f.driver,p,error));
        Fixture old;old.app.segments[0].data.replace(0,8,"T008APP1");QVERIFY(!t008::images(old.app,old.driver,p,error));
        Fixture old80;auto &legacy=old80.app.segments[0].data;legacy.replace(30,9,QByteArray("HW1").leftJustified(9,'\0'));
        legacy.replace(39,9,"APP000001");legacy.replace(48,16,QByteArray(16,'C'));legacy.replace(64,16,QByteArray(16,'L'));legacy.replace(80,8,QByteArray(8,char(0xff)));
        QVERIFY(!t008::images(old80.app,old80.driver,p,error));
    }
    void imagesAcceptMergedAndSplit(){
        Fixture f;t008::Plan p;QString error;QVERIFY2(t008::images(f.app,f.driver,p,error),qPrintable(error));
        QCOMPARE(p.application.size(),2);QCOMPARE(p.application[0].address,quint32(0x20000));QCOMPARE(p.application[0].data.size(),4096);QCOMPARE(p.application[1].data.size(),0x408);
        f.app.segments=p.application;QVERIFY(t008::images(f.app,f.driver,p,error));
    }
    void actualFirmwareFiles(){
        const QString appPath=qEnvironmentVariable("T008_TEST_APP_HEX"),driverPath=qEnvironmentVariable("T008_TEST_DRIVER_BIN");
        if(appPath.isEmpty()&&driverPath.isEmpty())QSKIP("Set both T008_TEST_APP_HEX and T008_TEST_DRIVER_BIN to validate actual public artifacts offline");
        QVERIFY(!appPath.isEmpty()&&!driverPath.isEmpty());FirmwareImage app,driver;QString error;t008::Plan plan;
        QVERIFY2(FirmwareImage::load(appPath,0x20000,app,error),qPrintable(error));
        QVERIFY2(FirmwareImage::load(driverPath,0x20001400,driver,error),qPrintable(error));
        QVERIFY2(t008::images(app,driver,plan,error),qPrintable(error));
        QCOMPARE(plan.application.size(),2);QCOMPARE(plan.driver.address,quint32(0x20001400));
        QCOMPARE(plan.application[0].address,quint32(0x20000));QCOMPARE(plan.application[1].address,quint32(0x21000));
        const QString configurations=qEnvironmentVariable("T008_TEST_CONFIG_DIR");
        if(!configurations.isEmpty())for(const auto flow:{"boot","app"}){
            host::SettingsStore store(configurations+"/"+flow+".json");QVector<host::ChannelSettings> loaded;
            QVERIFY2(store.load(loaded,error),qPrintable(error));QCOMPARE(loaded.size(),1);
            const auto &settings=loaded[0];FlashProfile p;QVERIFY2(FlashProfile::fromJson(settings.downloadProfile,p,error),qPrintable(error));
            QVERIFY(p.isT008());QCOMPARE(p.flow,QString(flow));QVERIFY(!settings.simulation);QVERIFY(settings.flashRequired);
            QCOMPARE(settings.flashAddress,QString("20001400"));QCOMPARE(settings.applicationAddress,QString("00020000"));
            QCOMPARE(QString(settings.flashPath).replace('\\','/'),QString(driverPath).replace('\\','/'));
            QCOMPARE(QString(settings.applicationPath).replace('\\','/'),QString(appPath).replace('\\','/'));
        }
    }
    void addressDiagnostics(){
        Fixture f;QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto appPath=dir.filePath("app.bin"),driverPath=dir.filePath("driver.bin");
        QFile af(appPath),df(driverPath);QVERIFY(af.open(QIODevice::WriteOnly));QCOMPARE(af.write(f.app.segments[0].data),qint64(f.app.size));af.close();
        QVERIFY(df.open(QIODevice::WriteOnly));QCOMPARE(df.write(f.driver.segments[0].data),qint64(f.driver.size));df.close();
        auto settings=host::ChannelSettings::defaults(communication::Bus::Can);
        QCOMPARE(settings.applicationAddress,QString("00020000"));QCOMPARE(settings.flashAddress,QString("20001400"));
        settings.applicationAddress="00010000";settings.flashAddress="00000000";
        FirmwareImage app,driver;QString error;t008::Plan plan;
        QVERIFY(FirmwareImage::load(appPath,settings.applicationAddress.toUInt(nullptr,16),app,error));
        QVERIFY(FirmwareImage::load(driverPath,settings.flashAddress.toUInt(nullptr,16),driver,error));
        QVERIFY(!t008::images(app,driver,plan,error));
        QCOMPARE(error,QString("T008 App unexpected base address: actual 0x00010000, expected 0x00020000"));
        app=f.app;QVERIFY(!t008::images(app,driver,plan,error));
        QCOMPARE(error,QString("T008 Driver unexpected base address: actual 0x00000000, expected 0x20001400"));
        app.segments={{0x20000,f.app.segments[0].data.left(4096)},{0x21008,f.app.segments[0].data.mid(4096)}};
        QVERIFY(!t008::images(app,f.driver,plan,error));
        QCOMPARE(error,QString("T008 App gap: actual 0x00021008, expected 0x00021000"));
        app.segments[1].address=0x20ff8;QVERIFY(!t008::images(app,f.driver,plan,error));
        QCOMPARE(error,QString("T008 App overlap: actual 0x00020FF8, expected 0x00021000"));
    }
    void rejectImages_data(){
        QTest::addColumn<QString>("defect");for(const auto d:{"joint","gap","overlap","length","layout","complement","reserved","entry","msp","driver-base","driver-abi","driver-minor0","driver-minor2","driver-entry","driver-bss","driver-reserved","extra"})QTest::newRow(d)<<QString(d);
    }
    void rejectImages(){
        QFETCH(QString,defect);Fixture f;auto &a=f.app.segments[0].data;auto &d=f.driver.segments[0].data;
        if(defect=="joint")f.app.segments.prepend({0,QByteArray(8,0)});
        if(defect=="gap"){f.app.segments={{0x20000,a.left(4096)},{0x21008,a.mid(4104)}};}
        if(defect=="overlap")f.app.segments.append({0x21000,a.mid(4096)});
        if(defect=="length")a.chop(1);
        if(defect=="layout")put(a,12,1000,2);
        if(defect=="complement")a[42]='X';
        if(defect=="reserved")a[200]=0;
        if(defect=="entry")put(a,4100,0x21400);
        if(defect=="msp")put(a,4096,0x20001408);
        if(defect=="driver-base")f.driver.segments[0].address+=8;
        if(defect=="driver-abi")put(d,10,2,2);
        if(defect=="driver-minor0")put(d,12,0,2);
        if(defect=="driver-minor2")put(d,12,2,2);
        if(defect=="driver-entry")put(d,32,129);
        if(defect=="driver-bss")put(d,64,8192);
        if(defect=="driver-reserved")d[100]=1;
        if(defect=="extra")f.app.segments.append({0x77000,QByteArray(8,0)});
        t008::Plan p;QString error;QVERIFY(!t008::images(f.app,f.driver,p,error));QVERIFY(!error.isEmpty());
    }
    void ecuImageLimits(){
        QString error;t008::Plan plan;
        for(quint32 total:{0x56100u,0x56108u,0x57000u}){
            Fixture f;auto &bytes=f.app.segments[0].data;bytes.append(QByteArray(total-bytes.size(),char(0xff)));
            put(bytes,8,total-4096);
            QCOMPARE(t008::images(f.app,f.driver,plan,error),total==0x56100u);
        }
        for(const auto field:QList<QPair<int,quint32>>{{28,0},{28,255},{28,257},{64,8}}){
            Fixture f;put(f.driver.segments[0].data,field.first,field.second);
            QVERIFY(!t008::images(f.app,f.driver,plan,error));
        }
    }
    void identityAndFingerprint(){
        Fixture f;t008::Plan p;QString e;QVERIFY(t008::images(f.app,f.driver,p,e));
        const auto fp=t008::fingerprint(QDate(2026,10,25),"QT-T008-BENCH001",e);QCOMPARE(fp.size(),19);QCOMPARE(fp.left(3),QByteArray::fromHex("1a0a19"));
        QVERIFY(t008::fingerprint(QDate(1999,1,1),"QT-T008-BENCH001",e).isEmpty());QVERIFY(t008::fingerprint(QDate(2026,1,1),QByteArray(16,1),e).isEmpty());
    }
    void responseContract(){
        QString e;QVERIFY(t008::response(QByteArray::fromHex("3101ff01")+QByteArray(12,0),QByteArray::fromHex("7101ff0100"),false,e));
        QVERIFY(!t008::response(QByteArray::fromHex("3101ff01"),QByteArray::fromHex("7101ff0100"),false,e));
        QVERIFY(!t008::response(QByteArray::fromHex("10"),{},false,e));
        QVERIFY(!t008::response(QByteArray::fromHex("220101"),QByteArray::fromHex("620101"),false,e));
        QVERIFY(t008::response(QByteArray::fromHex("8582"),{},true,e));
        QVERIFY(!t008::response(QByteArray::fromHex("8582"),{},false,e));
        QVERIFY(!t008::response(QByteArray::fromHex("8582"),QByteArray::fromHex("7f8522"),true,e));
        QVERIFY(!t008::response(QByteArray::fromHex("3101ff02"),QByteArray::fromHex("7101ff020000"),false,e));
    }
    void policyAndPersistence(){
        auto p=profile();QString e;FlashProfile restored;QVERIFY(FlashProfile::fromJson(p.toJson(),restored,e));QCOMPARE(restored.target,QString(t008::Target));
        for(const auto key:{"simulationOnly","keyProvider","keyLibrary","target","flow","verifyRoutine"}){
            auto j=p.toJson();if(QString(key)=="simulationOnly")j[key]=true;else if(QString(key)=="verifyRoutine")j[key]=0xff00;else j[key]="";
            QVERIFY2(!FlashProfile::fromJson(j,restored,e),key);
        }
        for(const auto map:{"stepEnabled","negativeResponseChecks","timeoutChecks"}){auto j=p.toJson();auto controls=j[map].toObject();controls["app36"]=false;j[map]=controls;QVERIFY(!FlashProfile::fromJson(j,restored,e));}
        auto b=profile("boot");QVERIFY(!b.enabled("pre1001"));QVERIFY(b.enabled("pre1003"));QVERIFY(b.enabled("boot1002"));QVERIFY(!b.checksTimeout("post1001"));
        FlashProfile generic;generic.flow="app";generic.stepEnabled["app36"]=false;QVERIFY(!generic.enabled("app36"));QVERIFY(!generic.checksNegativeResponse("app36"));
        auto settings=host::ChannelSettings::defaults(communication::Bus::Can);settings.simulation=false;settings.securityLevel=0x11;settings.downloadProfile=p.toJson();
        QTemporaryDir dir;QVERIFY(dir.isValid());host::SettingsStore store(dir.filePath("channels.json"));QVERIFY2(store.save({settings},e),qPrintable(e));
        QVector<host::ChannelSettings> loaded;QVERIFY2(store.load(loaded,e),qPrintable(e));QCOMPARE(loaded.size(),1);QVERIFY(FlashProfile::fromJson(loaded[0].downloadProfile,restored,e));QCOMPARE(restored.target,QString(t008::Target));
    }
    void bootProfileMigration(){
        QString error;FlashProfile restored;auto old=profile("boot").toJson();
        for(const auto map:{"stepEnabled","negativeResponseChecks","timeoutChecks"}){
            auto flags=old[map].toObject();flags["pre1001"]=false;flags["pre1003"]=false;old[map]=flags;
        }
        QVERIFY2(FlashProfile::fromJson(old,restored,error),qPrintable(error));
        for(const auto id:{"pre1003"}){
            QVERIFY(restored.enabled(id));QVERIFY(restored.checksNegativeResponse(id));QVERIFY(restored.checksTimeout(id));
            for(const auto map:{"stepEnabled","negativeResponseChecks","timeoutChecks"})QVERIFY(restored.toJson()[map].toObject()[id].toBool());
        }
        for(const auto id:{"pre1001","pre2701","pre2702","pre220101","pre31010203","pre1003control","pre8502","pre2803"})QVERIFY(!restored.enabled(id));
        for(const auto map:{"stepEnabled","negativeResponseChecks","timeoutChecks"})for(const auto id:{"pre1001","pre1003"}){
            auto bad=old;auto flags=bad[map].toObject();flags[id]="false";bad[map]=flags;QVERIFY(!FlashProfile::fromJson(bad,restored,error));
            bad=old;flags=bad[map].toObject();flags["unknownStep"]=true;bad[map]=flags;QVERIFY(!FlashProfile::fromJson(bad,restored,error));
        }
        auto app=profile("app").toJson();auto flags=app["stepEnabled"].toObject();flags["pre1001"]=false;app["stepEnabled"]=flags;
        QVERIFY(!FlashProfile::fromJson(app,restored,error));
        for(const auto map:{"stepEnabled","negativeResponseChecks","timeoutChecks"}){
            auto bad=old;auto required=bad[map].toObject();required["boot1002"]=false;bad[map]=required;QVERIFY(!FlashProfile::fromJson(bad,restored,error));
        }
        auto legacy=old;legacy["feedbackChecks"]=legacy.take("negativeResponseChecks");
        QVERIFY(FlashProfile::fromJson(legacy,restored,error));QVERIFY(!restored.negativeResponseChecks["pre1001"].toBool());QVERIFY(restored.negativeResponseChecks["pre1003"].toBool());
        FlashProfile generic;generic.flow="boot";QVERIFY(!generic.enabled("pre1001"));QVERIFY(!generic.enabled("pre1003"));
        generic.stepEnabled["pre1001"]=false;QVERIFY(FlashProfile::fromJson(generic.toJson(),restored,error));QVERIFY(!restored.enabled("pre1001"));
    }
    void bootSessionMatrix(){
        for(int initial:{1,2,3})for(int requested:{1,2,3}){
            Peer peer;peer.strictSessions=true;peer.inBoot=true;peer.session=initial;
            const QByteArray req=QByteArray::fromHex("10")+char(requested);const auto response=peer.reply(req);
            const bool allowed=requested==1||(requested==2&&initial!=1)||(requested==3&&initial!=2);
            if(allowed){QCOMPARE(response.left(2),QByteArray::fromHex("50")+char(requested));QCOMPARE(peer.session,requested);}
            else{QCOMPARE(response,QByteArray::fromHex("7f107e"));QCOMPARE(peer.session,initial);}
        }
    }
    void bootMatrixNegativeControl(){
        Fixture f;Peer peer;peer.inBoot=true;peer.strictSessions=true;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        MemoryTransport wire;wire.respond=[&](const QByteArray&r){return peer.reply(r);};UdsSession session(wire,{20,20,1000,0});
        bool identity=false;QVERIFY(session.request(QByteArray::fromHex("22f180"),QByteArray::fromHex("62f180"),[&](bool ok,const QByteArray &,const QString &){identity=ok;}));QTRY_VERIFY(identity);
        bool finished=false,success=true;QByteArray response;
        QVERIFY(session.request(QByteArray::fromHex("1002"),QByteArray::fromHex("5002"),[&](bool ok,const QByteArray &p,const QString &){finished=true;success=ok;response=p;}));
        QTRY_VERIFY(finished);QVERIFY(!success);QCOMPARE(response,QByteArray::fromHex("7f107e"));QCOMPARE(peer.session,1);
        QCOMPARE(wire.requests,QList<QByteArray>({QByteArray::fromHex("22f180"),QByteArray::fromHex("1002")}));
    }
    void bootNormalizationFailure_data(){
        QTest::addColumn<QByteArray>("request");QTest::addColumn<bool>("silent");
        for(const auto request:{"1003"})for(bool silent:{false,true})QTest::newRow(qPrintable(QString(request)+(silent?" timeout":" NRC")))<<QByteArray::fromHex(request)<<silent;
    }
    void bootNormalizationFailure(){
        QFETCH(QByteArray,request);QFETCH(bool,silent);Fixture f;Peer peer;peer.inBoot=true;peer.strictSessions=true;peer.corruptRequest=request;peer.silent=silent;peer.replaceResponse=request.startsWith(QByteArray::fromHex("31"))?QByteArray::fromHex("7f3133"):QByteArray::fromHex("7f107e");QString error;
        QVERIFY(t008::images(f.app,f.driver,peer.plan,error));MemoryTransport wire;wire.respond=[&](const QByteArray&r){return peer.reply(r);};
        UdsSession session(wire,{20,20,1000,0});FlashJob job(session,profile("boot"),std::make_unique<TestKey>());QSignalSpy finished(&job,&FlashJob::finished);
        QVERIFY(job.start(f.app,f.driver,error));QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,2500);QVERIFY(!finished[0][0].toBool());QCOMPARE(wire.requests.last(),request);
        for(const auto &r:wire.requests)QVERIFY(quint8(r[0])!=0x34&&quint8(r[0])!=0x36&&quint8(r[0])!=0x27&&!r.startsWith(QByteArray::fromHex("3101ff00")));
        QVERIFY(!wire.requests.contains(QByteArray::fromHex("1002")));const int count=wire.requests.size();QTest::qWait(50);QCOMPARE(wire.requests.size(),count);
    }
    void fullFlow_data(){
        QTest::addColumn<QString>("flow");QTest::addColumn<int>("initialSession");
        QTest::newRow("App entry")<<QString("app")<<1;
        QTest::newRow("Boot default")<<QString("boot")<<1;QTest::newRow("Boot extended")<<QString("boot")<<3;
    }
    void screenshotEntrySequence_data(){
        QTest::addColumn<QString>("flow");QTest::newRow("app")<<QString("app");QTest::newRow("boot")<<QString("boot");
    }
    void invalidConfigurationSendsNothing(){
        Fixture f;MemoryTransport wire;UdsSession session(wire,{100,100,2000,0});QString error;
        for(int n:{2,18,20}){auto p=profile();p.fingerprintData=QByteArray(n,'A');FlashJob job(session,p,std::make_unique<TestKey>());QVERIFY(!job.start(f.app,f.driver,error));QVERIFY(wire.requests.isEmpty());}
        auto p=profile();p.applicationEraseLength=0x56000;FlashJob job(session,p,std::make_unique<TestKey>());QVERIFY(!job.start(f.app,f.driver,error));QVERIFY(wire.requests.isEmpty());
    }
    void genericV2UsesTwoSegments(){
        Fixture f;Peer peer;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){return peer.reply(r);};
        auto p=profile("app");p.target="generic";UdsSession session(wire,{100,100,2000,0});FlashJob job(session,p,std::make_unique<TestKey>());
        QSignalSpy done(&job,&FlashJob::finished);QVERIFY(job.start(f.app,f.driver,error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);
        QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QCOMPARE(wire.requests.count(QByteArray::fromHex("3101ff00440002000000057000")),1);
        QCOMPARE(wire.requests.count(QByteArray::fromHex("3101ff00440002100000000408")),0);
    }
    void screenshotEntrySequence(){
        QFETCH(QString,flow);Fixture f;Peer peer;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        peer.inBoot=flow=="boot";MemoryTransport wire;wire.respond=[&](const QByteArray &r){return peer.reply(r);};
        UdsSession session(wire,{100,100,2000,0});FlashJob job(session,profile(flow),std::make_unique<TestKey>());
        QVERIFY(job.start(f.app,f.driver,error));QTRY_VERIFY_WITH_TIMEOUT(wire.requests.size()>=6,3000);
        QVERIFY(!wire.requests.contains(QByteArray::fromHex("3101dd01")));
        if(flow=="boot"){
            QCOMPARE(wire.requests[0],QByteArray::fromHex("1003"));QCOMPARE(wire.requests[1],QByteArray::fromHex("1002"));
            QCOMPARE(wire.requests[2],QByteArray::fromHex("2711"));QVERIFY(wire.requests[3].startsWith(QByteArray::fromHex("2712")));
        }else{
            QCOMPARE(wire.requests[0],QByteArray::fromHex("1001"));QCOMPARE(wire.requests[1],QByteArray::fromHex("1003"));
            QCOMPARE(wire.requests[2],QByteArray::fromHex("2701"));QVERIFY(wire.requests[3].startsWith(QByteArray::fromHex("2702")));
            QCOMPARE(wire.requests[4],QByteArray::fromHex("22f180"));QCOMPARE(wire.requests[5],QByteArray::fromHex("31010203"));
        }
        job.cancel();
    }
    void fullFlow(){
        QFETCH(QString,flow);QFETCH(int,initialSession);Fixture f;Peer peer;QString error;
        const auto realApp=qEnvironmentVariable("T008_TEST_REAL_APP"),realDriver=qEnvironmentVariable("T008_TEST_DRIVER_BIN");
        if(!realApp.isEmpty()){QVERIFY2(FirmwareImage::load(realApp,0x20000,f.app,error),qPrintable(error));QVERIFY2(FirmwareImage::load(realDriver,0x20001400,f.driver,error),qPrintable(error));}
        QVERIFY(t008::images(f.app,f.driver,peer.plan,error));peer.inBoot=flow=="boot";peer.session=initialSession;peer.strictSessions=true;
        MemoryTransport wire;wire.respond=[&](const QByteArray&r){return peer.reply(r);};SessionOptions timing{20,20,1000,0};UdsSession session(wire,timing);
        auto configured=profile(flow);configured.fingerprintData=QByteArray::fromHex("1A0A1C544553542D504C414345484F4C444552");
        FlashJob job(session,configured,std::make_unique<TestKey>());QSignalSpy finish(&job,&FlashJob::finished);QSignalSpy notices(&job,&FlashJob::notice);
        QVERIFY2(job.start(f.app,f.driver,error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(finish.size(),1,5000);QVERIFY2(finish[0][0].toBool(),qPrintable(finish[0][1].toString()));
        const auto eraseDescriptor=QByteArray::fromHex("3101ff00440002000000057000"),eraseCode=QByteArray::fromHex("3101ff00440002100000000408");
        QCOMPARE(wire.requests.count(eraseDescriptor),1);QCOMPARE(wire.requests.count(eraseCode),0);
        QVERIFY(!wire.requests.contains(QByteArray::fromHex("3101ff00440002000000001000")));
        QCOMPARE(wire.requests.count(QByteArray::fromHex("3101ff02")),1);QCOMPARE(wire.requests.count(QByteArray::fromHex("22f1a0")),0);
        QCOMPARE(wire.requests.contains(QByteArray::fromHex("31010203")),flow=="app");QVERIFY(!wire.requests.contains(QByteArray::fromHex("220101")));
        QCOMPARE(wire.requests.count(QByteArray::fromHex("1002")),1);QCOMPARE(peer.programmingResponses,1);
        if(flow=="boot"){
            QCOMPARE(wire.requests.mid(0,2),QList<QByteArray>({QByteArray::fromHex("1003"),QByteArray::fromHex("1002")}));QVERIFY(!wire.requests.contains(QByteArray::fromHex("3101dd01")));
            for(const auto &r:wire.requests)QVERIFY(!r.startsWith(QByteArray::fromHex("2701"))&&!r.startsWith(QByteArray::fromHex("2702")));
            for(const auto &r:{"1083","8582","288301"})QVERIFY(!wire.requests.contains(QByteArray::fromHex(r)));
        }else{
            const int precondition=wire.requests.indexOf(QByteArray::fromHex("31010203"));
            QVERIFY(precondition>wire.requests.indexOf(QByteArray::fromHex("1003")));
            QVERIFY(precondition>wire.requests.indexOf(QByteArray::fromHex("22f180")));QVERIFY(wire.requests.indexOf(QByteArray::fromHex("22f180"))>wire.requests.indexOf(QByteArray::fromHex("2701")));
            QVERIFY(wire.requests.contains(QByteArray::fromHex("2701")));bool appKey=false;for(const auto &r:wire.requests)appKey|=r.startsWith(QByteArray::fromHex("2702"));QVERIFY(appKey);
            for(const auto &r:{"1083","8582","288301"})QVERIFY(wire.requests.contains(QByteArray::fromHex(r)));
        }
        int verify=0,download=0;for(const auto&r:wire.requests){if(r.startsWith(QByteArray::fromHex("3101ff01"))){++verify;QCOMPARE(r.size(),16);}if(quint8(r[0])==0x34)++download;if(r.startsWith(QByteArray::fromHex("2ef184")))QCOMPARE(r.size(),22);}
        QCOMPARE(wire.requests.count(QByteArray::fromHex("2EF1841A0A1C544553542D504C414345484F4C444552")),1);
        QCOMPARE(verify,3);QCOMPARE(download,3);QCOMPARE(wire.requests.last(),QByteArray::fromHex("22f180"));QVERIFY(!job.running());QVERIFY(!notices.isEmpty());
        qint64 transferred=0;QList<QByteArray> downloads;
        for(const auto &r:wire.requests){if(quint8(r[0])==0x36)transferred+=r.size()-2;if(quint8(r[0])==0x34)downloads.append(r);}
        QCOMPARE(transferred,f.driver.size+f.app.size);QCOMPARE(wire.requests.count(QByteArray::fromHex("37")),3);
        QCOMPARE(downloads.size(),3);QVERIFY(downloads[0].startsWith(QByteArray::fromHex("34004420001400")));
        QCOMPARE(downloads[1],QByteArray::fromHex("3400440002000000001000"));QVERIFY(downloads[2].startsWith(QByteArray::fromHex("34004400021000")));
        QCOMPARE(readBe(downloads[2].right(4)),quint32(peer.plan.application[1].data.size()));
        const int firstErase=wire.requests.indexOf(eraseDescriptor);
        const int descDownload=wire.requests.indexOf(downloads[1]),codeDownload=wire.requests.indexOf(downloads[2]);
        QVERIFY(firstErase<descDownload&&descDownload<codeDownload);
        bool descVerify=false;for(int i=descDownload+1;i<codeDownload;++i)descVerify|=wire.requests[i].startsWith(QByteArray::fromHex("3101ff01"));QVERIFY(descVerify);
        const int final=wire.requests.indexOf(QByteArray::fromHex("3101ff02")),reset=wire.requests.indexOf(QByteArray::fromHex("1101"));
        QVERIFY(final>wire.requests.lastIndexOf(QByteArray::fromHex("37")));QVERIFY(reset>final);
        for(const auto &r:{"1003","14ffffff","288001","8581","1081","22f180"})QVERIFY(wire.requests.mid(reset+1).contains(QByteArray::fromHex(r)));
    }
    void appPreconditionRejectionStopsBeforeUnlock(){
        Fixture f;Peer peer;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        peer.strictSessions=true;peer.strictPreconditions=true;peer.corruptRequest=QByteArray::fromHex("31010203");peer.replaceResponse=QByteArray::fromHex("7f3133");
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){return peer.reply(r);};
        UdsSession session(wire,SessionOptions{20,20,1000,0});FlashJob job(session,profile(),std::make_unique<TestKey>());
        QSignalSpy finish(&job,&FlashJob::finished);QVERIFY2(job.start(f.app,f.driver,error),qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finish.size(),1,5000);QVERIFY(!finish[0][0].toBool());
        QCOMPARE(wire.requests.count(QByteArray::fromHex("31010203")),1);
        QVERIFY(wire.requests.contains(QByteArray::fromHex("2701")));for(const auto &r:wire.requests){QVERIFY(quint8(r[0])!=0x34);QVERIFY(r!=QByteArray::fromHex("1002"));}
    }
    void finalVersionObservation_data(){
        QTest::addColumn<QByteArray>("response");QTest::addColumn<QString>("status");QTest::addColumn<bool>("silent");
        QTest::newRow("match")<<(QByteArray::fromHex("62f189")+QByteArray("T008A0001"))<<QString::fromUtf8("版本匹配")<<false;
        QTest::newRow("different")<<(QByteArray::fromHex("62f189")+QByteArray("BAD000001"))<<QString::fromUtf8("版本不匹配")<<false;
        QTest::newRow("unavailable")<<QByteArray::fromHex("7f2231")<<QString::fromUtf8("无法读取")<<false;
        QTest::newRow("timeout")<<QByteArray()<<QString::fromUtf8("无法读取")<<true;
    }
    void finalVersionObservation(){
        QFETCH(QByteArray,response);QFETCH(QString,status);QFETCH(bool,silent);Fixture f;Peer peer;QString e;
        QVERIFY(t008::images(f.app,f.driver,peer.plan,e));peer.corruptRequest=QByteArray::fromHex("22f189");peer.replaceResponse=response;peer.silent=silent;
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){return peer.reply(r);};
        UdsSession session(wire,{20,20,1000,20});auto settings=profile();settings.identityDid=0xf189;FlashJob job(session,settings,std::make_unique<TestKey>());QSignalSpy finished(&job,&FlashJob::finished);
        QVERIFY(job.start(f.app,f.driver,e));QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,5000);
        QVERIFY(finished[0][0].toBool());QVERIFY(finished[0][1].toString().contains(status,Qt::CaseInsensitive));
        QVERIFY(!session.busy());QCOMPARE(wire.requests.last(),QByteArray::fromHex("22f189"));const int n=wire.requests.size();QTest::qWait(100);QCOMPARE(wire.requests.size(),n);
    }
    void strictFailures_data(){
        QTest::addColumn<QByteArray>("request");QTest::addColumn<QByteArray>("reply");QTest::addColumn<bool>("append");QTest::addColumn<bool>("silent");
        QTest::newRow("seed length")<<QByteArray::fromHex("2701")<<QByteArray::fromHex("670101")<<false<<false;
        QTest::newRow("seed NRC")<<QByteArray::fromHex("2701")<<QByteArray::fromHex("7f2733")<<false<<false;
        QTest::newRow("download maximum")<<QByteArray::fromHex("34")<<QByteArray::fromHex("74200403")<<false<<false;
        QTest::newRow("transfer wrong BSC")<<QByteArray::fromHex("36")<<QByteArray::fromHex("7600")<<false<<false;
        QTest::newRow("transfer extra byte")<<QByteArray::fromHex("36")<<QByteArray()<<true<<false;
        QTest::newRow("transfer exit extra byte")<<QByteArray::fromHex("37")<<QByteArray()<<true<<false;
        QTest::newRow("FF01 status")<<QByteArray::fromHex("3101ff01")<<QByteArray::fromHex("7101ff0101")<<false<<false;
        QTest::newRow("FF02 extra byte")<<QByteArray::fromHex("3101ff02")<<QByteArray()<<true<<false;
        QTest::newRow("post14 extra byte")<<QByteArray::fromHex("14ffffff")<<QByteArray()<<true<<false;
        QTest::newRow("timeout")<<QByteArray::fromHex("34")<<QByteArray()<<false<<true;
        QTest::newRow("SPRMIB NRC")<<QByteArray::fromHex("8582")<<QByteArray::fromHex("7f8522")<<false<<false;
        QTest::newRow("SPRMIB pending then timeout")<<QByteArray::fromHex("8582")<<QByteArray::fromHex("7f8578")<<false<<false;
        QTest::newRow("communication pending then timeout")<<QByteArray::fromHex("288301")<<QByteArray::fromHex("7f2878")<<false<<false;
    }
    void strictFailures(){
        QFETCH(QByteArray,request);QFETCH(QByteArray,reply);QFETCH(bool,append);QFETCH(bool,silent);Fixture f;Peer peer;QString e;QVERIFY(t008::images(f.app,f.driver,peer.plan,e));
        peer.corruptRequest=request;peer.replaceResponse=reply;peer.append=append;peer.silent=silent;MemoryTransport wire;wire.respond=[&](const QByteArray&r){
            if(r.startsWith(request)&&(quint8(r[0])==0x85||quint8(r[0])==0x28))return reply;return peer.reply(r);
        };
        UdsSession session(wire,{20,20,500,0});FlashJob job(session,profile(),std::make_unique<TestKey>());QSignalSpy finished(&job,&FlashJob::finished);
        QVERIFY(job.start(f.app,f.driver,e));QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,5000);QVERIFY(!finished[0][0].toBool());QVERIFY(!job.running());
        const int count=wire.requests.size();QTest::qWait(50);QCOMPARE(wire.requests.size(),count);
    }
    void suppressedTransportFailures_data(){
        QTest::addColumn<QByteArray>("request");QTest::addColumn<QString>("error");
        for(const auto request:{"1083","8582","288301"})for(const auto error:{"CAN N_As transmit confirmation timeout","CAN N_Ar timeout","CAN N_Cr timeout","UDS transmit timeout","UDS absolute response deadline"})
            QTest::newRow(qPrintable(QString(request)+" "+error))<<QByteArray::fromHex(request)<<QString(error);
    }
    void suppressedTransportFailures(){
        QFETCH(QByteArray,request);QFETCH(QString,error);Fixture f;Peer peer;QString e;
        QVERIFY(t008::images(f.app,f.driver,peer.plan,e));MemoryTransport wire;
        wire.respond=[&](const QByteArray &r){return peer.reply(r);};wire.failRequest=request;wire.failMessage=error;
        UdsSession session(wire,{20,20,500,0});FlashJob job(session,profile(),std::make_unique<TestKey>());QSignalSpy finished(&job,&FlashJob::finished);
        QVERIFY(job.start(f.app,f.driver,e));QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,5000);
        QVERIFY(!finished[0][0].toBool());QVERIFY(finished[0][1].toString().contains(error));QCOMPARE(wire.requests.last(),request);
        const int count=wire.requests.size();QTest::qWait(50);QCOMPARE(wire.requests.size(),count);QVERIFY(!job.running());
    }
    void keyLengthAndCancel(){
        Fixture f;Peer peer;QString e;QVERIFY(t008::images(f.app,f.driver,peer.plan,e));MemoryTransport wire;wire.respond=[&](const QByteArray&r){return peer.reply(r);};
        UdsSession session(wire,{20,20,500,0});auto key=std::make_unique<TestKey>();key->bytes=15;FlashJob invalid(session,profile(),std::move(key));QSignalSpy finished(&invalid,&FlashJob::finished);
        QVERIFY(invalid.start(f.app,f.driver,e));QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,2000);QVERIFY(!finished[0][0].toBool());
        wire.requests.clear();FlashJob cancelled(session,profile(),std::make_unique<TestKey>());QVERIFY(cancelled.start(f.app,f.driver,e));cancelled.cancel();const int count=wire.requests.size();QTest::qWait(80);QCOMPARE(wire.requests.size(),count);QVERIFY(!cancelled.running());
    }
    void strictCan_data(){
        QTest::addColumn<QByteArray>("bytes");QTest::addColumn<bool>("accept");
        QTest::newRow("payload zero preserved")<<QByteArray::fromHex("057101ff0300cccc")<<true;
        QTest::newRow("padding zero rejected")<<QByteArray::fromHex("057101ff030000cc")<<false;
        QTest::newRow("short DLC")<<QByteArray::fromHex("057101ff0300")<<false;
    }
    void strictCan(){
        QFETCH(QByteArray,bytes);QFETCH(bool,accept);CanOptions o;o.strictClassicCc=true;o.padding=0xcc;CanTransport wire(o,[](const CanFrame&,quint64,QString&){return true;});wire.listen();
        QSignalSpy received(&wire,&DiagnosticTransport::received),failed(&wire,&DiagnosticTransport::failed);wire.receiveFrame({0x795,false,false,false,false,bytes});
        QCOMPARE(received.size(),accept?1:0);QCOMPARE(failed.size(),accept?0:1);if(accept)QCOMPARE(received[0][0].toByteArray(),QByteArray::fromHex("7101ff0300"));
    }
    void strictCanFlowAndConsecutive(){
        CanOptions o;o.strictClassicCc=true;o.padding=0xcc;CanTransport *ptr=nullptr;CanTransport wire(o,[&](const CanFrame&,quint64 token,QString&){ptr->confirmTransmitted(token);return true;});ptr=&wire;
        QSignalSpy failed(&wire,&DiagnosticTransport::failed),received(&wire,&DiagnosticTransport::received);QString e;QVERIFY(wire.send(QByteArray(10,'A'),e));QTest::qWait(10);
        wire.receiveFrame({0x795,false,false,false,false,QByteArray::fromHex("30080000cccccccc")});QCOMPARE(failed.size(),1);
        wire.listen();wire.receiveFrame({0x795,false,false,false,false,QByteArray::fromHex("10087101ff030000")});QTest::qWait(10);
        wire.receiveFrame({0x795,false,false,false,false,QByteArray::fromHex("210000cccccccccc")});QCOMPARE(received.size(),1);QCOMPARE(received[0][0].toByteArray().size(),8);
        wire.listen();wire.receiveFrame({0x795,false,false,false,false,QByteArray::fromHex("10087101ff030000")});QTest::qWait(10);
        wire.receiveFrame({0x795,false,false,false,false,QByteArray::fromHex("21000000cccccccc")});QCOMPARE(failed.size(),2);
        wire.listen();wire.receiveFrame({0x795,true,false,false,false,QByteArray(8,0)});QCOMPARE(failed.size(),3);
        CanOptions restored;QVERIFY(CanOptions::fromJson(o.toJson(),restored,e));QVERIFY(restored.strictClassicCc);restored.extended=true;QVERIFY(!restored.valid(e));
    }
    void uiDefaultsToGenericUds(){
        // PreviewBackend is the sole backend in this test; no hardware SDK is constructed.
        auto settings=host::ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;settings.downloadProfile=profile("boot").toJson();
        host::ChannelViewModel vm(settings);host::ChannelPage page(&vm);page.show();QCoreApplication::processEvents();
        bool checked=false;
        QTimer::singleShot(0,&page,[&]{
            auto dialog=page.findChild<QDialog*>("downloadSettingsDialog");if(!dialog)return;
            auto app=dialog->findChild<QLineEdit*>("appSecurityLevel");auto boot=dialog->findChild<QLineEdit*>("bootSecurityLevel");
            checked=!dialog->findChild<QComboBox*>("downloadTarget")&&app&&boot&&!app->isReadOnly()&&!boot->isReadOnly();
            if(!checked){dialog->reject();return;}
            app->setText("03");boot->setText("21");
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        auto button=page.findChild<QPushButton*>("downloadSettings");QVERIFY(button);button->click();QVERIFY(checked);
        FlashProfile saved;QString e;QVERIFY(vm.flashProfile(saved,e));QCOMPARE(saved.target,QString("generic"));
        QCOMPARE(saved.appSecurityLevel,quint8(0x03));QCOMPARE(saved.securityLevel,quint8(0x21));
    }
    void uiV2CancelSendsNothing(){
        Fixture f;QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto app=dir.filePath("app.bin"),driver=dir.filePath("driver.bin");
        QFile a(app),d(driver);QVERIFY(a.open(QIODevice::WriteOnly));QCOMPARE(a.write(f.app.segments[0].data),qint64(f.app.segments[0].data.size()));a.close();
        QVERIFY(d.open(QIODevice::WriteOnly));QCOMPARE(d.write(f.driver.segments[0].data),qint64(f.driver.segments[0].data.size()));d.close();
        auto settings=host::ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;settings.applicationPath=app;settings.flashPath=driver;
        settings.applicationAddress="00020000";settings.flashAddress="20001400";settings.flashRequired=true;
        host::ChannelViewModel vm(settings);host::ChannelPage page(&vm);auto *model=vm.findChild<host::ChannelModel*>();QVERIFY(model);
        QSignalSpy frames(model,&host::ChannelModel::framesReceived),review(&vm,&host::ChannelViewModel::packageConfirmationRequested);
        QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());
        bool seen=false;QTimer closer;closer.setInterval(10);connect(&closer,&QTimer::timeout,&page,[&]{
            auto *box=page.findChild<QMessageBox*>("packageConfirmation");if(!box)return;
            const auto text=box->text();seen=text.contains("S32K144TAUTOSAR")&&text.contains("Rev.A4.06.5")&&text.contains("T008A0001")&&text.contains("Minimum Boot: 1")&&text.contains("43 43")&&text.contains("4C 4C");box->done(QMessageBox::No);
        });closer.start();vm.start();QTRY_COMPARE_WITH_TIMEOUT(vm.taskState(),host::TaskState::Cancelled,5000);closer.stop();
        QVERIFY(seen);QCOMPARE(review.size(),1);QCOMPARE(frames.size(),0);
    }
    void actualGoldenUiReview(){
        const auto app=qEnvironmentVariable("T008_TEST_GOLDEN_APP"),driver=qEnvironmentVariable("T008_TEST_DRIVER_BIN");
        if(app.isEmpty()||driver.isEmpty())QSKIP("Golden format-only paths not provided");
        FirmwareImage image,flash;QString error;boot::t008::Plan plan;
        QVERIFY2(FirmwareImage::load(app,0x20000,image,error),qPrintable(error));QVERIFY2(FirmwareImage::load(driver,0x20001400,flash,error),qPrintable(error));
        QVERIFY2(t008::images(image,flash,plan,error),qPrintable(error));
        QCOMPARE(plan.project,QByteArray("S32K144TAUTOSAR"));QCOMPARE(plan.hardware,QByteArray("Rev.A4.06.5"));
        QCOMPARE(plan.version,QByteArray("T008A0001"));QCOMPARE(plan.minBoot,quint16(1));
        const auto expectedBinding=qEnvironmentVariable("T008_TEST_HEADER_BINDING","210A66E669E6AC5539AC4131829BDD66").toLatin1();
        QCOMPARE(QCryptographicHash::hash(plan.application[0].data.left(88),QCryptographicHash::Sha256).left(16).toHex().toUpper(),expectedBinding);
        auto settings=host::ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;settings.applicationPath=app;settings.flashPath=driver;
        settings.applicationAddress="00020000";settings.flashAddress="20001400";settings.flashRequired=true;
        host::ChannelViewModel vm(settings);host::ChannelPage page(&vm);auto *model=vm.findChild<host::ChannelModel*>();QVERIFY(model);
        QSignalSpy frames(model,&host::ChannelModel::framesReceived);QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());
        bool seen=false;QTimer closer;closer.setInterval(10);connect(&closer,&QTimer::timeout,&page,[&]{
            auto *box=page.findChild<QMessageBox*>("packageConfirmation");if(!box)return;const auto text=box->text();
            seen=text.contains("S32K144TAUTOSAR")&&text.contains("Rev.A4.06.5")&&text.contains("T008A0001")&&text.contains("Minimum Boot: 1")&&
                 text.contains(QString::fromLatin1(plan.configuration.toHex(' ')).toUpper())&&text.contains(QString::fromLatin1(plan.calibration.toHex(' ')).toUpper());
            box->done(QMessageBox::No);
        });closer.start();vm.start();QTRY_COMPARE_WITH_TIMEOUT(vm.taskState(),host::TaskState::Cancelled,5000);closer.stop();QVERIFY(seen);QCOMPARE(frames.size(),0);
    }
    void uiV2ConfirmationUsesSnapshot(){
        Fixture f;const auto realApp=qEnvironmentVariable("T008_TEST_REAL_APP"),realDriver=qEnvironmentVariable("T008_TEST_DRIVER_BIN");
        if(!realApp.isEmpty()){QString loadError;QVERIFY(!realDriver.isEmpty());QVERIFY2(FirmwareImage::load(realApp,0x20000,f.app,loadError),qPrintable(loadError));QVERIFY2(FirmwareImage::load(realDriver,0x20001400,f.driver,loadError),qPrintable(loadError));}
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto app=dir.filePath("app.bin"),driver=dir.filePath("driver.bin");
        QFile a(app),d(driver);QVERIFY(a.open(QIODevice::WriteOnly));QCOMPARE(a.write(f.app.segments[0].data),qint64(f.app.segments[0].data.size()));a.close();
        QVERIFY(d.open(QIODevice::WriteOnly));QCOMPARE(d.write(f.driver.segments[0].data),qint64(f.driver.segments[0].data.size()));d.close();
        FirmwareImage original;QString error;QVERIFY(FirmwareImage::load(app,0x20000,original,error));const auto digest=QString::fromLatin1(original.sha256.toHex());
        auto settings=host::ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;settings.applicationPath=app;settings.flashPath=driver;
        settings.applicationAddress="00020000";settings.flashAddress="20001400";settings.flashRequired=true;
        host::ChannelViewModel vm(settings);host::ChannelPage page(&vm);auto *model=vm.findChild<host::ChannelModel*>();QVERIFY(model);
        QByteArray current,descriptorBytes,codeBytes;int pduLength=0;quint32 address=0;
        auto acceptPdu=[&](const QByteArray &pdu){
            if(pdu.size()==11&&quint8(pdu[0])==0x34)address=readBe(pdu.mid(3,4));
            else if(pdu.size()>=3&&quint8(pdu[0])==0x36){if(address==0x20000)descriptorBytes+=pdu.mid(2);else if(address==0x21000)codeBytes+=pdu.mid(2);}
        };
        connect(model,&host::ChannelModel::framesReceived,&page,[&](const host::FrameBatch &frames){
            for(const auto &frame:frames){if(!frame.simulated||frame.direction!="SIM TX"||frame.payload.isEmpty())continue;
                const auto b=frame.payload;const int pci=quint8(b[0])>>4;
                if(pci==0){const int n=quint8(b[0])&15;if(n&&b.size()>=n+1)acceptPdu(b.mid(1,n));}
                else if(pci==1&&b.size()==8){pduLength=((quint8(b[0])&15)<<8)|quint8(b[1]);current=b.mid(2);}
                else if(pci==2&&!current.isEmpty()){current+=b.mid(1);if(current.size()>=pduLength){acceptPdu(current.left(pduLength));current.clear();}}
            }
        });
        QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());
        bool replaced=false;QTimer closer;closer.setInterval(10);connect(&closer,&QTimer::timeout,&page,[&]{
            auto *box=page.findChild<QMessageBox*>("packageConfirmation");if(!box)return;
            QFile changed(app);if(changed.open(QIODevice::WriteOnly|QIODevice::Truncate)){replaced=changed.write(QByteArray(32,'X'))==32;changed.close();}
            box->done(QMessageBox::Yes);
        });closer.start();vm.start();QTRY_VERIFY_WITH_TIMEOUT(replaced,5000);closer.stop();
        QTRY_VERIFY_WITH_TIMEOUT(vm.logs().join('\n').contains(digest,Qt::CaseInsensitive),5000);
        const int timeout=realApp.isEmpty()?40000:600000;
        QTRY_COMPARE_WITH_TIMEOUT(descriptorBytes.size(),4096,timeout);QTRY_COMPARE_WITH_TIMEOUT(codeBytes.size(),f.app.segments[0].data.size()-4096,timeout);
        QCOMPARE(descriptorBytes,f.app.segments[0].data.left(4096));QCOMPARE(codeBytes,f.app.segments[0].data.mid(4096));vm.cancel();
    }
};
#ifdef T008_HEADLESS
QTEST_GUILESS_MAIN(T008TargetTest)
#else
QTEST_MAIN(T008TargetTest)
#endif
#include "test_t008_target.moc"
