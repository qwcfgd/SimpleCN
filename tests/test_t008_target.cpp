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
#include <QTableWidget>
#include <QTabWidget>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QFileDialog>
#include <QSettings>
#include "protocol/T008Target.h"
#include "protocol/FlashJob.h"
#include "protocol/CanTransport.h"
#include "protocol/LinTransport.h"
#include "infrastructure/SettingsStore.h"
#include "infrastructure/ChannelWorker.h"
#include "views/ChannelPage.h"
using namespace boot;
namespace {
void put(QByteArray &b,int at,quint32 v,int count=4){for(int i=0;i<count;++i)b[at+i]=char(v>>(8*i));}
FirmwareImage image(quint32 base,const QByteArray &data){FirmwareImage i;i.segments={{base,data}};i.size=data.size();return i;}
struct Fixture {
    FirmwareImage app,driver;
    Fixture(){
        QByteArray descriptor(4096,char(0xff)),data(0x800,char(0x55)),native(136,0);
        descriptor.replace(0,8,"T008APP2");put(descriptor,8,0x800);put(descriptor,12,1,2);
        descriptor.replace(14,16,QByteArray("S32K144TAUTOSAR").leftJustified(16,'\0'));descriptor.replace(30,16,QByteArray("Rev.A4.06.5").leftJustified(16,'\0'));
        descriptor.replace(46,9,"T008A0001");descriptor.replace(55,16,"T008-CFG-TEST001");descriptor.replace(71,16,"T008-CAL-TEST001");
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
    void t008VerifyOptionAndGenericCrc(){
        FlashProfile p;SimulatedUdsEcu ecu(p);ecu.t008VerifySkipped=true;
        auto request=[&](const QByteArray &r){const auto replies=ecu.handle(r);return replies.isEmpty()?QByteArray():replies.last();};
        QCOMPARE(request(QByteArray::fromHex("1002")).left(2),QByteArray::fromHex("5002"));
        QCOMPARE(request(QByteArray::fromHex("2711")).left(2),QByteArray::fromHex("6711"));
        SimulationKey key;QString error;const auto value=key.calculate(0x11,QByteArray::fromHex("12345678"),error);QVERIFY(error.isEmpty());
        QCOMPARE(request(QByteArray::fromHex("2712")+value),QByteArray::fromHex("6712"));
        QCOMPARE(request(QByteArray::fromHex("3400440002000000000004")).left(1),QByteArray::fromHex("74"));
        QCOMPARE(request(QByteArray::fromHex("360101020304")),QByteArray::fromHex("7601"));
        QCOMPARE(request(QByteArray::fromHex("37")),QByteArray::fromHex("77"));
        const auto base=QByteArray::fromHex("3101ff010002000000000004");
        QCOMPARE(request(base+QByteArray(4,'\0')),QByteArray::fromHex("7101ff0100"));
        QCOMPARE(request(base+QByteArray::fromHex("12345678")),QByteArray::fromHex("7101ff0100"));
        QCOMPARE(request(QByteArray::fromHex("3101ff01000200010000000400000000")),QByteArray::fromHex("7f3131"));
        QCOMPARE(request(QByteArray::fromHex("3101ff01000200000000000500000000")),QByteArray::fromHex("7f3131"));
        QCOMPARE(request(base+QByteArray(3,'\0')),QByteArray::fromHex("7f3113"));
        ecu.t008VerifySkipped=false;
        QCOMPARE(request(base+QByteArray(4,'\0')),QByteArray::fromHex("7101ff0101"));
        QCOMPARE(request(base+be32(crc32(QByteArray::fromHex("01020304")))),QByteArray::fromHex("7101ff0100"));
        MemoryTransport wire;Peer peer;wire.respond=[&](const QByteArray&r){return peer.reply(r);};
        FlashProfile generic;generic.simulationOnly=false;generic.keyProvider="seed-as-key";generic.applicationEraseBase=0x25000;generic.applicationEraseLength=0x1000;
        UdsSession session(wire,{100,100,2000,0});FlashJob job(session,generic,std::make_unique<TestKey>());QSignalSpy done(&job,&FlashJob::finished);
        const auto data=QByteArray("generic-ecu-data");QVERIFY(job.start(image(0x25000,data),{},error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        bool found=false;for(const auto &r:wire.requests)if(r.startsWith(QByteArray::fromHex("3101ff01"))){found=true;QCOMPARE(r.size(),16);QCOMPARE(r.mid(12,4),be32(crc32(data)));QVERIFY(r.mid(12,4)!=QByteArray(4,'\0'));}QVERIFY(found);
    }
    void identifyAndLoadBootBundle(){
        QTemporaryDir dir;QVERIFY(dir.isValid());Fixture f;
        auto save=[&](const QString &name,const QByteArray &data){QFile file(dir.filePath(name));if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size())return QString();return file.fileName();};
        QByteArray a(0xf000,char(0xa1)),b(0xf000,char(0xb2));put(a,0,0x20001000);put(a,4,0x5101);put(b,0,0x20001000);put(b,4,0x6d101);
        auto record=[](quint16 address,quint8 type,const QByteArray &data){QByteArray bytes;bytes+=char(data.size());bytes+=char(address>>8);bytes+=char(address);bytes+=char(type);bytes+=data;int sum=0;for(unsigned char v:bytes)sum+=v;bytes+=char(-sum);return QByteArray(":")+bytes.toHex().toUpper()+"\n";};
        auto makeHex=[&](bool paired,bool contiguous=false){QByteArray text;for(int slot=0;slot<(paired?2:1);++slot){const quint32 base=slot?(contiguous?0x14000:0x6d000):0x5000;const auto &data=slot?b:a;quint32 high=0xffffffff;
            for(int i=0;i<data.size();i+=16){const quint32 address=base+i;if((address>>16)!=high){high=address>>16;text+=record(0,4,QByteArray(1,char(high>>8))+char(high));}text+=record(quint16(address),0,data.mid(i,16));}}
            return text+record(0,1,{});};
        const auto driver=save("driver.bin",f.driver.segments[0].data),app=save("app.bin",f.app.segments[0].data);
        const auto bin=save("pair.bin",a+b),hex=save("pair.hex",makeHex(true)),single=save("single.hex",makeHex(false)),contiguous=save("contiguous.hex",makeHex(true,true));
        QVERIFY(!driver.isEmpty()&&!app.isEmpty()&&!bin.isEmpty()&&!hex.isEmpty()&&!single.isEmpty()&&!contiguous.isEmpty());
        FirmwareImage parsed,bundle;QString error;
        QVERIFY(FirmwareImage::load(driver,0x5000,parsed,error));QCOMPARE(FirmwareImage::identify(parsed),FirmwareImage::Kind::Driver);
        QVERIFY(FirmwareImage::load(app,0x5000,parsed,error));QCOMPARE(FirmwareImage::identify(parsed),FirmwareImage::Kind::Application);
        QVERIFY(FirmwareImage::load(bin,0x5000,parsed,error));QCOMPARE(FirmwareImage::identify(parsed),FirmwareImage::Kind::BootBundle);
        QVERIFY(FirmwareImage::load(hex,0x5000,parsed,error));QCOMPARE(FirmwareImage::identify(parsed),FirmwareImage::Kind::BootBundle);
        QVERIFY2(FirmwareImage::loadBootBundle(hex,bundle,error),qPrintable(error));QCOMPARE(bundle.segments.size(),1);QCOMPARE(bundle.segments[0].data,a+b);
        QVERIFY(FirmwareImage::loadBootBundle(bin,bundle,error));QCOMPARE(bundle.segments[0].data,a+b);
        QVERIFY(FirmwareImage::load(single,0x5000,parsed,error));QCOMPARE(FirmwareImage::identify(parsed),FirmwareImage::Kind::IncompleteBoot);
        QVERIFY(!FirmwareImage::loadBootBundle(single,bundle,error));QVERIFY(error.contains("both complete"));
        QVERIFY(!FirmwareImage::loadBootBundle(contiguous,bundle,error));QVERIFY(error.contains("both complete"));
        MemoryTransport wire;wire.respond=[](const QByteArray&){return QByteArray();};UdsSession session(wire,{100,100,2000,0});FlashJob job(session,profile("boot"),std::make_unique<TestKey>());
        QVERIFY(!job.start({},f.driver,parsed,error));QVERIFY(wire.requests.isEmpty());
    }
    void actualFirstInstallFiles(){
        const auto root=qEnvironmentVariable("T008_AB_DELIVERY");
        if(root.isEmpty())QSKIP("Set T008_AB_DELIVERY to the first-install directory");
        const QDir source(root);QString error;
        for(const auto &entry:QList<QPair<QString,FirmwareImage::Kind>>{
                {"driver.bin",FirmwareImage::Kind::Driver},{"driver.hex",FirmwareImage::Kind::Driver},
                {"app.bin",FirmwareImage::Kind::Application},{"app.hex",FirmwareImage::Kind::Application},
                {"boot-ab.bin",FirmwareImage::Kind::BootBundle}}){
            FirmwareImage image;
            QVERIFY2(FirmwareImage::load(source.filePath(entry.first),0x5000,image,error),qPrintable(entry.first+": "+error));
            QCOMPARE(image.identify(image),entry.second);
        }
        for(const auto &entry:QList<QPair<QString,quint32>>{{"driver",0x20001400u},{"app",0x15000u}}){
            FirmwareImage bin,hex;
            QVERIFY2(FirmwareImage::load(source.filePath(entry.first+".bin"),entry.second,bin,error),qPrintable(error));
            QVERIFY2(FirmwareImage::load(source.filePath(entry.first+".hex"),entry.second,hex,error),qPrintable(error));
            QMap<quint32,quint8> binBytes,hexBytes;
            for(const auto &s:bin.segments)for(int i=0;i<s.data.size();++i)binBytes.insert(s.address+quint32(i),quint8(s.data[i]));
            for(const auto &s:hex.segments)for(int i=0;i<s.data.size();++i)hexBytes.insert(s.address+quint32(i),quint8(s.data[i]));
            QVERIFY2(binBytes==hexBytes,qPrintable(entry.first+" BIN/HEX address or data mismatch"));
        }
    }
    void actualBootHexPair(){
        const auto root=qEnvironmentVariable("T008_AB_DELIVERY");if(root.isEmpty())QSKIP("Set T008_AB_DELIVERY to the A/B delivery directory");
        const QDir source(root);QFile af(source.filePath("boot-a.hex")),bf(source.filePath("boot-b.hex")),pair(source.filePath("boot-ab.bin"));
        QVERIFY(af.open(QIODevice::ReadOnly));QVERIFY(bf.open(QIODevice::ReadOnly));QVERIFY(pair.open(QIODevice::ReadOnly));
        QByteArray a=af.readAll(),b=bf.readAll();const auto eof=a.lastIndexOf(":00000001FF");QVERIFY(eof>=0);a.truncate(eof);a+=b;
        QTemporaryDir temp;QVERIFY(temp.isValid());QFile both(temp.filePath("boot-pair.hex"));QVERIFY(both.open(QIODevice::WriteOnly));QCOMPARE(both.write(a),qint64(a.size()));both.close();
        const auto pairBytes=pair.readAll();QCOMPARE(pairBytes.size(),0x1e000);
        FirmwareImage decoded;QString error;
        QVERIFY(!FirmwareImage::loadBootBundle(both.fileName(),decoded,error));QVERIFY(error.contains("both complete"));
        QVERIFY2(FirmwareImage::loadBootBundle(pair.fileName(),decoded,error),qPrintable(error));
        QCOMPARE(decoded.segments.size(),1);QCOMPARE(decoded.segments[0].data,pairBytes);
        FirmwareImage sparse;QVERIFY2(FirmwareImage::load(both.fileName(),0x5000,sparse,error),qPrintable(error));
        QByteArray seen(pairBytes.size(),0);
        for(const auto &segment:sparse.segments){
            for(int i=0;i<segment.data.size();++i){
                const quint64 address=quint64(segment.address)+quint64(i);
                const qint64 offset=(address>=0x5000&&address<0x14000)?qint64(address-0x5000):
                    (address>=0x6d000&&address<0x7c000)?qint64(0xf000+address-0x6d000):-1;
                QVERIFY(offset>=0&&offset<seen.size());QVERIFY(!seen.at(int(offset)));
                seen[int(offset)]=1;QCOMPARE(segment.data.at(i),pairBytes.at(int(offset)));
            }
        }
        int missing=0;for(int i=0;i<seen.size();++i)if(!seen.at(i)){++missing;QCOMPARE(quint8(pairBytes.at(i)),quint8(0xff));}
        QVERIFY(missing>0);
        QVERIFY(!FirmwareImage::loadBootBundle(af.fileName(),decoded,error));QVERIFY(error.contains("both complete"));
        QVERIFY(!FirmwareImage::loadBootBundle(bf.fileName(),decoded,error));QVERIFY(error.contains("both complete"));
    }
    void workerFileTableEntry_data(){QTest::addColumn<bool>("withDriver");QTest::addColumn<bool>("badBoot");QTest::addColumn<bool>("withApp");QTest::addColumn<bool>("actual");QTest::addColumn<bool>("legacyDisabled");QTest::newRow("driver and boot")<<true<<false<<false<<false<<false;QTest::newRow("boot and app")<<true<<false<<true<<false<<false;QTest::newRow("missing driver path")<<false<<false<<false<<false<<false;QTest::newRow("single slot rejected before sending")<<true<<true<<false<<false<<false;QTest::newRow("first-install full package")<<true<<false<<true<<true<<false;QTest::newRow("legacy disabled flag")<<true<<false<<false<<false<<true;}
    void workerFileTableEntry(){
        QFETCH(bool,withDriver);QFETCH(bool,badBoot);QFETCH(bool,withApp);QFETCH(bool,actual);QFETCH(bool,legacyDisabled);QTemporaryDir dir;QVERIFY(dir.isValid());
        auto save=[&](const QString &name,const QByteArray &data){QFile file(dir.filePath(name));if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size())return QString();return file.fileName();};
        Fixture fixture;QByteArray driverData=fixture.driver.segments[0].data,appData=fixture.app.segments[0].data;
        QByteArray bootData=QByteArray(0xf000,'A')+(badBoot?QByteArray():QByteArray(0xf000,'B'));
        QString driver,boot,app;
        if(actual){const auto root=qEnvironmentVariable("T008_AB_DELIVERY");if(root.isEmpty())QSKIP("Set T008_AB_DELIVERY to the first-install directory");
            const QDir source(root);driver=source.filePath("driver.bin");boot=qEnvironmentVariable("T008_TEST_AB_BOOT",source.filePath("boot-ab.bin"));app=source.filePath("app.bin");
            QFile d(driver),b(boot),a(app);QVERIFY(d.open(QIODevice::ReadOnly));QVERIFY(b.open(QIODevice::ReadOnly));QVERIFY(a.open(QIODevice::ReadOnly));
            driverData=d.readAll();bootData=b.readAll();appData=a.readAll();QCOMPARE(bootData.size(),0x1e000);QVERIFY(appData.size()>4096);
        }else{driver=save("driver.bin",driverData);boot=save("boot.bin",bootData);app=save("app.bin",appData);}
        QVERIFY(!driver.isEmpty()&&!boot.isEmpty()&&!app.isEmpty());
        auto s=host::ChannelSettings::defaults(communication::Bus::Can);s.simulation=true;s.hardwareKey="preview:CAN:1";s.handle=0xf101;s.flashRequired=withDriver&&!legacyDisabled;
        s.applicationPath.clear();s.flashPath.clear();s.downloadFiles=QJsonArray{QJsonObject{{"role","driver"},{"path",withDriver?driver:dir.filePath("missing.bin")}},QJsonObject{{"role","boot"},{"path",boot}}};
        if(withApp)s.downloadFiles.append(QJsonObject{{"role","app"},{"path",app}});
        auto p=FlashProfile{};p.flow="boot";p.stepEnabled["pre1003"]=true;p.resetWaitMs=100;s.downloadProfile=p.toJson();s.fingerprintHex.clear();s.applicationAddress="00015000";
        host::ChannelWorker worker(s);QSignalSpy stateSpy(&worker,&host::ChannelWorker::taskChanged);host::TaskState state=host::TaskState::Idle;QString lastStep,lastLog,stopReason;int progress=0,txFrames=0,rxFrames=0,expected=0;QByteArray assembling;QList<QByteArray> requests;
        QElapsedTimer elapsed,idle;elapsed.start();idle.start();QEventLoop loop;QTimer watchdog;watchdog.setInterval(1000);
        connect(&watchdog,&QTimer::timeout,this,[&]{if(elapsed.elapsed()>240000||idle.elapsed()>10000){stopReason=elapsed.elapsed()>240000?"overall 240 s limit":"no progress for 10 s";watchdog.stop();worker.cancelTask();loop.quit();}});
        connect(&worker,&host::ChannelWorker::logMessage,this,[&](const QString &text){lastLog=text;});
        connect(&worker,&host::ChannelWorker::taskChanged,this,[&](host::TaskState next,int value,const QString &step){state=next;progress=value;lastStep=step;idle.restart();if(next==host::TaskState::Completed||next==host::TaskState::Failed||next==host::TaskState::Cancelled)loop.quit();});
        connect(&worker,&host::ChannelWorker::observedFrames,this,[&](const host::FrameBatch &batch){for(const auto &f:batch){
            if(!f.simulated)continue;idle.restart();if(f.direction!="Tx"){++rxFrames;continue;}++txFrames;if(f.payload.isEmpty())continue;
            const int pci=quint8(f.payload[0]),type=pci>>4;if(type==0){const int n=pci&15;if(n>0&&n<8)requests.append(f.payload.mid(1,n));}
            else if(type==1&&f.payload.size()>=2){expected=((pci&15)<<8)|quint8(f.payload[1]);assembling=f.payload.mid(2,6);}
            else if(type==2&&expected>0){assembling+=f.payload.mid(1);if(assembling.size()>=expected){requests.append(assembling.left(expected));expected=0;assembling.clear();}}
        }});
        worker.initialize();worker.connectChannel(s);worker.startPreview(s);if(state==host::TaskState::Running){watchdog.start();loop.exec();}watchdog.stop();
        const auto diagnostic=QString("state=%1 progress=%2 elapsed=%3ms lastStep=%4 tx=%5 rx=%6 requests=%7 stop=%8 log=%9").arg(int(state)).arg(progress).arg(elapsed.elapsed()).arg(lastStep).arg(txFrames).arg(rxFrames).arg(requests.size()).arg(stopReason,lastLog);
        qInfo().noquote()<<"Worker diagnostic:"<<diagnostic<<"spyCount="<<stateSpy.size()<<"spyLast="<<(stateSpy.isEmpty()?QString("none"):QString::number(int(qvariant_cast<host::TaskState>(stateSpy.last()[0]))));
        worker.shutdown();
        if(!withDriver){QVERIFY2(state==host::TaskState::Idle,qPrintable(diagnostic));QCOMPARE(txFrames,0);return;}
        if(badBoot){QVERIFY2(state==host::TaskState::Failed,qPrintable(diagnostic));QVERIFY2(lastLog.contains("both complete"),qPrintable(diagnostic));QCOMPARE(txFrames,0);return;}
        QVERIFY2(state==host::TaskState::Completed,qPrintable(diagnostic));
        const auto first1003=requests.indexOf(QByteArray::fromHex("1003")),programming=requests.indexOf(QByteArray::fromHex("1002"));
        const auto bootErase=requests.indexOf(QByteArray::fromHex("3101ff0044000050000000f000"));
        const auto bootAttemptA=requests.indexOf(QByteArray::fromHex("340044000050000000f000"));
        const auto bootDownload=requests.indexOf(QByteArray::fromHex("3400440006d0000000f000"));
        const auto commit=requests.indexOf(QByteArray::fromHex("3101ff02")),reset=requests.indexOf(QByteArray::fromHex("1101"));
        QVERIFY2(first1003>=0&&programming>first1003&&bootErase>programming&&bootAttemptA>bootErase&&bootDownload>bootAttemptA&&commit>bootDownload&&reset>commit,qPrintable(diagnostic));
        QCOMPARE(requests.count(QByteArray::fromHex("3101ff0044000050000000f000")),1);
        QCOMPARE(requests.count(QByteArray::fromHex("340044000050000000f000")),1);QCOMPARE(requests.count(QByteArray::fromHex("3400440006d0000000f000")),1);
        const auto seed=requests.indexOf(QByteArray::fromHex("2711"));int key=-1,fingerprint=-1,verifyCount=0;
        quint32 lastAddress=0,lastLength=0;
        for(int i=0;i<requests.size();++i){const auto &r=requests[i];if(r.startsWith(QByteArray::fromHex("2712")))key=i;if(r.startsWith(QByteArray::fromHex("2ef184")))fingerprint=i;
            if(r.startsWith(QByteArray::fromHex("340044"))&&r.size()==11){lastAddress=readBe(r.mid(3,4));lastLength=readBe(r.mid(7,4));}
            if(r.startsWith(QByteArray::fromHex("3101ff01"))){++verifyCount;QCOMPARE(r.size(),16);QCOMPARE(readBe(r.mid(4,4)),lastAddress);QCOMPARE(readBe(r.mid(8,4)),lastLength);QCOMPARE(r.mid(12,4),QByteArray(4,'\0'));}}
        QVERIFY2(seed>programming&&key>seed&&fingerprint>key&&fingerprint<bootErase,qPrintable(diagnostic));
        if(withDriver){const auto driverDownload=requests.indexOf(QByteArray::fromHex("34004420001400")+be32(quint32(driverData.size())));QVERIFY(driverDownload>fingerprint&&driverDownload<bootErase);QByteArray sentDriver;for(int i=driverDownload+1;i<bootErase&&!requests[i].startsWith(char(0x37));++i)if(requests[i].startsWith(char(0x36)))sentDriver+=requests[i].mid(2);QCOMPARE(sentDriver,driverData);}
        else for(const auto &r:requests)QVERIFY(!r.startsWith(QByteArray::fromHex("34004420001400")));
        QCOMPARE(verifyCount,(withDriver?1:0)+1+(withApp?2:0));
        QByteArray sent;int bootBlocks=0;for(int i=bootDownload+1;i<commit;++i)if(requests[i].startsWith(char(0x36))){QCOMPARE(requests[i].size(),1026);sent+=requests[i].mid(2);++bootBlocks;}QCOMPARE(bootBlocks,60);QCOMPARE(sent,bootData.mid(0xf000,0xf000));
        if(withApp){
            const auto appErase=requests.indexOf(QByteArray::fromHex("3101ff00440001500000057000"));QVERIFY(appErase>commit&&appErase<reset);
            const auto desc=requests.indexOf(QByteArray::fromHex("3400440001500000001000"));const auto code=requests.indexOf(QByteArray::fromHex("34004400016000")+be32(quint32(appData.size()-4096)));
            QVERIFY(desc>appErase&&code>desc&&reset>code);QCOMPARE(requests.count(QByteArray::fromHex("3101ff02")),2);
            auto dataAfter=[&](int begin){QByteArray bytes;for(int i=begin+1;i<requests.size()&&!requests[i].startsWith(char(0x37));++i)if(requests[i].startsWith(char(0x36)))bytes+=requests[i].mid(2);return bytes;};
            QCOMPARE(dataAfter(desc),appData.left(4096));QCOMPARE(dataAfter(code),appData.mid(4096));
        }
        const auto post1003=requests.lastIndexOf(QByteArray::fromHex("1003")),clear=requests.indexOf(QByteArray::fromHex("14ffffff"));
        const auto comm=requests.indexOf(QByteArray::fromHex("288001")),dtc=requests.indexOf(QByteArray::fromHex("8581")),session=requests.indexOf(QByteArray::fromHex("1081")),identity=requests.lastIndexOf(QByteArray::fromHex("22f180"));
        QVERIFY2(post1003>reset&&clear>post1003&&comm>clear&&dtc>comm&&session>dtc&&identity>session,qPrintable(diagnostic));
    }
    void linFileTableDownload_data(){
        QTest::addColumn<bool>("withBoot");QTest::addColumn<bool>("withApp");
        QTest::newRow("driver and app")<<false<<true;QTest::newRow("driver and boot")<<true<<false;QTest::newRow("driver boot and app")<<true<<true;
    }
    void linFileTableDownload(){
        QFETCH(bool,withBoot);QFETCH(bool,withApp);QTemporaryDir dir;QVERIFY(dir.isValid());Fixture fixture;
        QByteArray bootData=QByteArray(0xf000,'A')+QByteArray(0xf000,'B');
        auto save=[&](const QString &name,const QByteArray &bytes){QFile file(dir.filePath(name));if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())return QString();return file.fileName();};
        const auto driver=save("driver.bin",fixture.driver.segments[0].data),app=save("app.bin",fixture.app.segments[0].data),boot=save("boot.bin",bootData);
        QVERIFY(!driver.isEmpty()&&!app.isEmpty()&&!boot.isEmpty());
        auto s=host::ChannelSettings::defaults(communication::Bus::Lin);s.simulation=true;s.hardwareKey="preview:LIN:1";s.handle=0xf201;s.nad="12";s.linSlotMs=1;s.flashRequired=false;s.applicationAddress="00015000";s.applicationLength="00057000";s.fingerprintHex.clear();
        s.downloadFiles=QJsonArray{QJsonObject{{"role","driver"},{"path",driver}}};if(withBoot)s.downloadFiles.append(QJsonObject{{"role","boot"},{"path",boot}});if(withApp)s.downloadFiles.append(QJsonObject{{"role","app"},{"path",app}});
        auto p=FlashProfile{};p.flow="boot";p.stepEnabled["pre1003"]=true;p.resetWaitMs=100;s.downloadProfile=p.toJson();
        host::ChannelWorker worker(s);host::TaskState state=host::TaskState::Idle;QString lastStep,lastLog,decodeError,stopReason;QList<QByteArray> requests,responses;LinCodec txCodec,rxCodec;int txCount=0,rxCount=0;
        QElapsedTimer elapsed,idle;elapsed.start();idle.start();QEventLoop loop;QTimer watchdog;watchdog.setInterval(1000);
        connect(&watchdog,&QTimer::timeout,this,[&]{if(elapsed.elapsed()>300000||idle.elapsed()>15000){stopReason=elapsed.elapsed()>300000?"overall limit":"no progress";watchdog.stop();worker.cancelTask();loop.quit();}});
        connect(&worker,&host::ChannelWorker::logMessage,this,[&](const QString &text){lastLog=text;});
        connect(&worker,&host::ChannelWorker::taskChanged,this,[&](host::TaskState next,int,const QString &step){state=next;lastStep=step;idle.restart();if(next==host::TaskState::Completed||next==host::TaskState::Failed||next==host::TaskState::Cancelled)loop.quit();});
        connect(&worker,&host::ChannelWorker::observedFrames,this,[&](const host::FrameBatch &batch){for(const auto &f:batch){
            if(!f.simulated||f.bus!=host::signal::Bus::Lin||f.payload.isEmpty())continue;idle.restart();QByteArray pdu;QString error;
            if(f.id==0x3c&&f.direction=="Tx"){++txCount;const auto result=txCodec.accept(0x12,f.payload,pdu,error);if(result==LinCodec::Invalid)decodeError=error;else if(result==LinCodec::Complete)requests.append(pdu);}
            else if(f.id==0x3d&&f.direction=="Rx"){++rxCount;const auto result=rxCodec.accept(0x12,f.payload,pdu,error);if(result==LinCodec::Invalid)decodeError=error;else if(result==LinCodec::Complete)responses.append(pdu);}
        }});
        worker.initialize();worker.connectChannel(s);worker.startPreview(s);if(state==host::TaskState::Running){watchdog.start();loop.exec();}watchdog.stop();worker.shutdown();
        const auto diagnostic=QString("state=%1 elapsed=%2 step=%3 tx=%4 rx=%5 stop=%6 decode=%7 log=%8").arg(int(state)).arg(elapsed.elapsed()).arg(lastStep).arg(txCount).arg(rxCount).arg(stopReason,decodeError,lastLog);
        QVERIFY2(state==host::TaskState::Completed,qPrintable(diagnostic));QVERIFY2(decodeError.isEmpty(),qPrintable(diagnostic));QVERIFY(txCount>0&&rxCount>0);
        const auto driver34=requests.indexOf(QByteArray::fromHex("34004420001400")+be32(quint32(fixture.driver.size)));QVERIFY(driver34>=0);
        auto dataAfter=[&](int begin){QByteArray bytes;for(int i=begin+1;i<requests.size()&&!requests[i].startsWith(char(0x37));++i)if(requests[i].startsWith(char(0x36)))bytes+=requests[i].mid(2);return bytes;};
        QCOMPARE(dataAfter(driver34),fixture.driver.segments[0].data);
        int previous=driver34;
        if(withBoot){const auto erase=requests.indexOf(QByteArray::fromHex("3101ff0044000050000000f000")),a=requests.indexOf(QByteArray::fromHex("340044000050000000f000")),b=requests.indexOf(QByteArray::fromHex("3400440006d0000000f000")),commit=requests.indexOf(QByteArray::fromHex("3101ff02"));
            QVERIFY(erase>driver34&&a>erase&&b>a&&commit>b);QVERIFY(responses.contains(QByteArray::fromHex("7f3470")));QCOMPARE(dataAfter(b),bootData.mid(0xf000));previous=commit;}
        if(withApp){const auto erase=requests.indexOf(QByteArray::fromHex("3101ff00440001500000057000")),desc=requests.indexOf(QByteArray::fromHex("3400440001500000001000")),code=requests.indexOf(QByteArray::fromHex("34004400016000")+be32(quint32(fixture.app.size-4096)));
            QVERIFY(erase>previous&&desc>erase&&code>desc);QCOMPARE(dataAfter(desc),fixture.app.segments[0].data.left(4096));QCOMPARE(dataAfter(code),fixture.app.segments[0].data.mid(4096));previous=code;}
        const auto reset=requests.indexOf(QByteArray::fromHex("1101")),post=requests.lastIndexOf(QByteArray::fromHex("1003"));QVERIFY(reset>previous&&post>reset);
    }
    void downloadFileTableOrderingAndRemoval_data(){QTest::addColumn<bool>("lin");QTest::newRow("CAN")<<false;QTest::newRow("LIN")<<true;}
    void downloadFileTableOrderingAndRemoval(){
        QFETCH(bool,lin);
        QTemporaryDir dir;QVERIFY(dir.isValid());QStringList paths;
        for(const auto name:{"driver.bin","boot.bin","app.bin"}){const auto path=dir.filePath(name);QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("x",1),qint64(1));paths.append(path);}
        auto settings=host::ChannelSettings::defaults(lin?communication::Bus::Lin:communication::Bus::Can);settings.simulation=true;
        settings.downloadFiles=QJsonArray{QJsonObject{{"role","app"},{"path",paths[2]}},QJsonObject{{"role","driver"},{"path",paths[0]}},QJsonObject{{"role","boot"},{"path",paths[1]}}};
        host::ChannelSettings restored;QString error;QVERIFY2(host::ChannelSettings::fromJson(settings.toJson(),restored,error),qPrintable(error));QCOMPARE(restored.downloadFiles,settings.downloadFiles);
        host::ChannelViewModel vm(restored);host::ChannelPage page(&vm);page.resize(1200,900);page.show();QTest::qWait(50);
        auto table=page.findChild<QTableWidget*>("downloadFiles");QVERIFY(table);QCOMPARE(table->rowCount(),3);
        QCOMPARE(table->item(0,0)->text(),QString("Driver"));QCOMPARE(table->item(1,0)->text(),QString("Boot"));QCOMPARE(table->item(2,0)->text(),QString("App"));
        QVERIFY(table->isVisible());const auto first=table->visualRect(table->model()->index(0,0));const auto second=table->visualRect(table->model()->index(1,0));QVERIFY(first.isValid());QVERIFY(second.isValid());
        QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,first.center());
        QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::ShiftModifier,second.center());
        QCOMPARE(table->selectionModel()->selectedRows().size(),2);
        auto remove=page.findChild<QPushButton*>("removeDownloadFiles");QVERIFY(remove);remove->click();QCOMPARE(table->rowCount(),1);
        QCOMPARE(table->item(0,0)->text(),QString("App"));QCOMPARE(vm.settings().downloadFiles.size(),1);
        bool heading=false;for(auto l:page.findChildren<QLabel*>("sectionTitle"))heading|=l->text()=="ROM 更新";QVERIFY(heading);
    }
    void downloadFileReplacementAndDirectory_data(){QTest::addColumn<bool>("lin");QTest::newRow("CAN")<<false;QTest::newRow("LIN")<<true;}
    void downloadFileReplacementAndDirectory(){
        QFETCH(bool,lin);
        QTemporaryDir temp;QVERIFY(temp.isValid());QDir root(temp.path());QVERIFY(root.mkdir("first"));QVERIFY(root.mkdir("second"));
        const auto first=root.filePath("first"),second=root.filePath("second");Fixture fixture;
        QByteArray bootData(0x1e000,'\0');put(bootData,0,0x20001000);put(bootData,4,0x5101);put(bootData,0xf000,0x20001000);put(bootData,0xf004,0x6d101);
        auto save=[](const QString &path,const QByteArray &bytes){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
        const auto driver1=QDir(first).filePath("driver1.bin"),driver2=QDir(second).filePath("driver2.bin");
        const auto app1=QDir(first).filePath("app1.bin"),app2=QDir(second).filePath("app2.bin");
        const auto boot1=QDir(first).filePath("boot1.bin"),boot2=QDir(second).filePath("boot2.bin");
        QVERIFY(save(driver1,fixture.driver.segments[0].data));QVERIFY(save(driver2,fixture.driver.segments[0].data));
        QVERIFY(save(app1,fixture.app.segments[0].data));QVERIFY(save(app2,fixture.app.segments[0].data));
        QVERIFY(save(boot1,bootData));QVERIFY(save(boot2,bootData));
        const auto previousFormat=QSettings::defaultFormat();struct RestoreSettingsFormat{QSettings::Format format;~RestoreSettingsFormat(){QSettings::setDefaultFormat(format);}} restore{previousFormat};
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        QSettings prefs("Qt-GeneralController","DownloadFiles");prefs.clear();prefs.setValue("lastDirectory",first);prefs.sync();
        auto settings=host::ChannelSettings::defaults(lin?communication::Bus::Lin:communication::Bus::Can);settings.simulation=true;
        settings.downloadFiles=QJsonArray{QJsonObject{{"role","driver"},{"path",driver1}},QJsonObject{{"role","driver"},{"path",driver2}},QJsonObject{{"role","app"},{"path",app1}},QJsonObject{{"role","boot"},{"path",boot1}}};
        host::ChannelViewModel vm(settings);host::ChannelPage page(&vm);page.resize(1200,900);page.show();QTest::qWait(50);
        auto table=page.findChild<QTableWidget*>("downloadFiles");auto add=page.findChild<QPushButton*>("addDownloadFiles");auto remove=page.findChild<QPushButton*>("removeDownloadFiles");auto defaults=page.findChild<QPushButton*>("defaultDownloadFiles");QVERIFY(table&&add&&remove&&defaults);
        QCOMPARE(table->rowCount(),3);QCOMPARE(table->item(0,1)->text(),driver2);QCOMPARE(vm.settings().downloadFiles.size(),4);
        QVERIFY(add->geometry().left()>=table->geometry().right());QVERIFY(remove->geometry().left()>=table->geometry().right());QVERIFY(defaults->geometry().left()>=table->geometry().right());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
        QString dialogStatus;auto choose=[&](const QString &path,const QString &expectedDir){
            bool found=false,initial=false,accepted=false;QTimer guard;guard.setSingleShot(true);guard.setInterval(5000);
            connect(&guard,&QTimer::timeout,&page,[&]{if(auto d=page.findChild<QFileDialog*>()){dialogStatus+=QString(" timeout dir=%1 selected=%2").arg(d->directory().absolutePath(),d->selectedFiles().join(";"));d->reject();}});guard.start();
            QTimer::singleShot(30,&page,[&]{auto d=page.findChild<QFileDialog*>();if(!d)return;found=true;initial=QDir::cleanPath(d->directory().absolutePath())==QDir::cleanPath(expectedDir);
                connect(d,&QDialog::accepted,&page,[&]{accepted=true;});d->setDirectory(QFileInfo(path).absolutePath());
                QTimer::singleShot(200,d,[d,path,&dialogStatus]{if(auto edit=d->findChild<QLineEdit*>("fileNameEdit"))edit->setText(QFileInfo(path).fileName());dialogStatus=QString("dir=%1 selected=%2").arg(d->directory().absolutePath(),d->selectedFiles().join(";"));QMetaObject::invokeMethod(d,"accept");});});
            add->click();guard.stop();dialogStatus+=QString(" found=%1 initial=%2 accepted=%3").arg(found).arg(initial).arg(accepted);return found&&initial&&accepted;
        };
        QVERIFY2(choose(driver2,first),qPrintable(dialogStatus));QCOMPARE(table->rowCount(),3);
        QVERIFY2(choose(app2,second),qPrintable(dialogStatus));QCOMPARE(table->rowCount(),3);
        QVERIFY2(choose(boot2,second),qPrintable(dialogStatus));QCOMPARE(table->rowCount(),3);
        QCOMPARE(table->item(0,1)->text(),driver2);QCOMPARE(table->item(1,1)->text(),boot2);QCOMPARE(table->item(2,1)->text(),app2);
        QCOMPARE(vm.settings().downloadFiles.size(),3);QCOMPARE(vm.settings().flashPath,driver2);QCOMPARE(vm.settings().applicationPath,app2);
        QCOMPARE(QSettings("Qt-GeneralController","DownloadFiles").value("lastDirectory").toString(),second);
        auto changed=vm.settings();changed.applicationAddress="00018000";QVERIFY(vm.setSettings(changed));
        defaults->click();QCOMPARE(table->rowCount(),3);QCOMPARE(table->item(0,1)->text(),driver2);QCOMPARE(table->item(1,1)->text(),boot1);QCOMPARE(table->item(2,1)->text(),app1);
        QCOMPARE(vm.settings().applicationAddress,QString("00018000"));
        table->selectionModel()->select(table->model()->index(2,0),QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        QCOMPARE(table->selectionModel()->selectedRows().size(),1);QVERIFY(remove->isEnabled());remove->click();QCOMPARE(table->rowCount(),2);
        auto tabs=page.findChild<QTabWidget*>("taskPages");QVERIFY(tabs);tabs->setCurrentIndex(1);tabs->setCurrentIndex(0);
        table->selectionModel()->select(table->model()->index(1,0),QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        QCOMPARE(table->selectionModel()->selectedRows().size(),1);QVERIFY(remove->isEnabled());remove->click();QCOMPARE(table->rowCount(),1);defaults->click();
        QCOMPARE(table->rowCount(),2);QCOMPARE(table->item(0,1)->text(),driver2);QCOMPARE(table->item(1,1)->text(),boot1);
        QCOMPARE(vm.settings().applicationAddress,QString("00018000"));
        table->selectionModel()->select(table->model()->index(0,0),QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);remove->click();QCOMPARE(table->rowCount(),1);
        page.hide();page.show();QCoreApplication::processEvents();
        table->selectionModel()->select(table->model()->index(0,0),QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);remove->click();QCOMPARE(table->rowCount(),0);
        defaults->click();QCOMPARE(table->rowCount(),1);QCOMPARE(table->item(0,1)->text(),boot1);
    }
    void downloadFilesDefaultRestoresEmptyEntry_data(){QTest::addColumn<bool>("lin");QTest::newRow("CAN")<<false;QTest::newRow("LIN")<<true;}
    void downloadFilesDefaultRestoresEmptyEntry(){
        QFETCH(bool,lin);
        QTemporaryDir temp;QVERIFY(temp.isValid());const auto path=temp.filePath("app.bin");QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write("x",1),qint64(1));f.close();
        auto settings=host::ChannelSettings::defaults(lin?communication::Bus::Lin:communication::Bus::Can);settings.simulation=true;
        host::ChannelViewModel vm(settings);host::ChannelPage page(&vm);page.show();QTest::qWait(30);
        auto table=page.findChild<QTableWidget*>("downloadFiles");auto defaults=page.findChild<QPushButton*>("defaultDownloadFiles");QVERIFY(table&&defaults);QCOMPARE(table->rowCount(),0);
        auto changed=vm.settings();changed.downloadFiles=QJsonArray{QJsonObject{{"role","app"},{"path",path}}};changed.applicationPath=path;QVERIFY(vm.setSettings(changed));QCOMPARE(table->rowCount(),1);
        defaults->click();QCOMPARE(table->rowCount(),0);QVERIFY(vm.settings().downloadFiles.isEmpty());QVERIFY(vm.settings().applicationPath.isEmpty());
    }
    void effectiveDownloadFilesUseLatestWithoutMutation(){
        const QJsonArray original{QJsonObject{{"role","app"},{"path","old"}},QJsonObject{{"role","driver"},{"path","driver"}},QJsonObject{{"role","app"},{"path","new"}}};
        QJsonArray selected;QVERIFY(host::effectiveDownloadFiles(original,selected));QCOMPARE(original.size(),3);QCOMPARE(selected.size(),2);
        QCOMPARE(selected[0].toObject().value("role").toString(),QString("driver"));QCOMPARE(selected[1].toObject().value("path").toString(),QString("new"));
        QVERIFY(!host::effectiveDownloadFiles(QJsonArray{QJsonObject{{"role","other"}}},selected));
    }
    void workerRequiresDriver_data(){
        QTest::addColumn<QString>("caseName");QTest::addColumn<bool>("lin");
        for(const auto lin:{false,true})for(const auto name:{"app without driver","boot without driver","driver without program","missing driver with legacy false","legacy path missing driver"})QTest::newRow(qPrintable(QString("%1-%2").arg(lin?"LIN":"CAN",name)))<<QString(name)<<lin;
    }
    void workerRequiresDriver(){
        QFETCH(QString,caseName);QFETCH(bool,lin);QTemporaryDir dir;QVERIFY(dir.isValid());
        auto file=[&](const QString &name){const auto path=dir.filePath(name);QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write("x",1)!=1)return QString();return path;};
        const auto driver=file("driver.bin"),app=file("app.bin"),boot=file("boot.bin");QVERIFY(!driver.isEmpty()&&!app.isEmpty()&&!boot.isEmpty());
        auto s=host::ChannelSettings::defaults(lin?communication::Bus::Lin:communication::Bus::Can);s.simulation=true;s.hardwareKey=lin?"preview:LIN:1":"preview:CAN:1";s.handle=lin?0xf201:0xf101;s.flashRequired=true;
        auto add=[&](const char *role,const QString &path){s.downloadFiles.append(QJsonObject{{"role",role},{"path",path}});};
        if(caseName=="app without driver")add("app",app);
        else if(caseName=="boot without driver")add("boot",boot);
        else if(caseName=="driver without program")add("driver",driver);
        else if(caseName=="missing driver with legacy false"){add("app",app);s.flashRequired=false;}
        else{s.flashPath=dir.filePath("missing.bin");s.applicationPath=app;s.flashRequired=false;}
        host::ChannelWorker worker(s);QSignalSpy states(&worker,&host::ChannelWorker::taskChanged),frames(&worker,&host::ChannelWorker::observedFrames);
        worker.initialize();worker.connectChannel(s);worker.startPreview(s);QTest::qWait(50);
        for(const auto &state:states)QVERIFY(qvariant_cast<host::TaskState>(state[0])!=host::TaskState::Running);
        for(const auto &batch:frames)for(const auto &frame:qvariant_cast<host::FrameBatch>(batch[0]))QVERIFY(frame.direction!="Tx");
        worker.shutdown();
    }
    void copiedConfigurationPreservesAddresses(){
        const auto path=qEnvironmentVariable("T008_TEST_COPIED_CONFIG");
        if(path.isEmpty())QSKIP("Set T008_TEST_COPIED_CONFIG to the copied channel configuration");
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes=file.readAll();file.close();
        const auto raw=QJsonDocument::fromJson(bytes).object().value("channels").toArray();QVERIFY(!raw.isEmpty());
        QVector<host::ChannelSettings> loaded;QString error;
        QVERIFY2(host::SettingsStore(path).load(loaded,error),qPrintable(error));QCOMPARE(loaded.size(),raw.size());
        for(int i=0;i<raw.size();++i){
            const auto channel=raw[i].toObject();
            if(channel.contains("applicationAddress"))QCOMPARE(loaded[i].applicationAddress,channel.value("applicationAddress").toString());
            if(channel.contains("applicationLength"))QCOMPARE(loaded[i].applicationLength,channel.value("applicationLength").toString());
            if(channel.contains("bootAddress"))QCOMPARE(loaded[i].bootAddress,channel.value("bootAddress").toString());
            if(channel.contains("bootEraseLength"))QCOMPARE(loaded[i].bootEraseLength,channel.value("bootEraseLength").toString());
        }
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),bytes);
    }
    void legacyAddressesAreUsed(){
        for(const auto bus:{communication::Bus::Can,communication::Bus::Lin}){
            const auto fresh=host::ChannelSettings::defaults(bus);
            QCOMPARE(fresh.applicationAddress,QString("00015000"));
            host::ChannelSettings roundTrip;QString roundTripError;
            QVERIFY2(host::ChannelSettings::fromJson(fresh.toJson(),roundTrip,roundTripError),qPrintable(roundTripError));
            QCOMPARE(roundTrip.applicationAddress,QString("00015000"));
        }
        auto old=host::ChannelSettings::defaults(communication::Bus::Can).toJson();
        old["bootAddress"]="00000000";old["bootEraseLength"]="00020000";old["applicationAddress"]="00020000";old["applicationLength"]="00057000";
        host::ChannelSettings restored;QString error;QVERIFY2(host::ChannelSettings::fromJson(old,restored,error),qPrintable(error));
        QCOMPARE(restored.bootAddress,QString("00000000"));QCOMPARE(restored.bootEraseLength,QString("00020000"));QCOMPARE(restored.applicationAddress,QString("00020000"));
        auto missingAddress=old;missingAddress.remove("applicationAddress");host::ChannelSettings legacyMissing;
        QVERIFY2(host::ChannelSettings::fromJson(missingAddress,legacyMissing,error),qPrintable(error));QCOMPARE(legacyMissing.applicationAddress,QString("00020000"));
        restored.simulation=true;host::ChannelViewModel vm(restored);host::ChannelPage page(&vm);bool inspected=false;
        QTimer::singleShot(0,&page,[&]{auto dialog=page.findChild<QDialog*>("downloadSettingsDialog");if(!dialog)return;
            auto boot=dialog->findChild<QLineEdit*>("bootAddress"),length=dialog->findChild<QLineEdit*>("bootEraseLength"),app=dialog->findChild<QLineEdit*>("settingsApplicationAddress");
            auto bootDefault=dialog->findChild<QLineEdit*>("download_default_bootAddress"),lengthDefault=dialog->findChild<QLineEdit*>("download_default_bootEraseLength");
            auto flow=dialog->findChild<QComboBox*>("downloadFlow");bool flowLabel=false;
            for(auto label:dialog->findChildren<QLabel*>())flowLabel|=label->text()=="下载起始流程";
            inspected=boot&&length&&app&&bootDefault&&lengthDefault&&flow&&flowLabel&&flow->itemText(0)=="App起始流程"&&flow->itemText(1)=="Boot起始流程"&&boot->text()=="00000000"&&length->text()=="00020000"&&app->text()=="00020000"&&bootDefault->text()=="00005000"&&lengthDefault->text()=="0000F000";
            dialog->reject();});
        QTimer::singleShot(12000,&page,[&]{if(auto dialog=page.findChild<QDialog*>("downloadSettingsDialog"))dialog->reject();});
        auto button=page.findChild<QPushButton*>("downloadSettings");QVERIFY(button);button->click();QVERIFY(inspected);
        Fixture f;Peer peer;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));MemoryTransport wire;wire.respond=[&](const QByteArray&r){return peer.reply(r);};
        auto p=profile("boot");p.target="generic";p.bootEraseBase=restored.bootAddress.toUInt(nullptr,16);p.bootEraseLength=restored.bootEraseLength.toUInt(nullptr,16);
        p.applicationEraseBase=restored.applicationAddress.toUInt(nullptr,16);p.applicationEraseLength=restored.applicationLength.toUInt(nullptr,16);
        UdsSession session(wire,{100,100,2000,0});FlashJob job(session,p,std::make_unique<TestKey>());QSignalSpy done(&job,&FlashJob::finished);
        QVERIFY(job.start(f.app,f.driver,image(0x5000,QByteArray(0x1e000,'B')),error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,15000);QVERIFY(done[0][0].toBool());
        QVERIFY(wire.requests.contains(QByteArray::fromHex("3101ff00440000000000020000")));
        QVERIFY(wire.requests.contains(QByteArray::fromHex("340044000050000000f000")));
        QVERIFY(wire.requests.contains(QByteArray::fromHex("3101ff00440002000000057000")));
    }
    void bootBundleSequence_data(){QTest::addColumn<bool>("withApp");QTest::newRow("Boot only")<<false;QTest::newRow("Boot then App")<<true;}
    void bootBundleSequence(){
        QFETCH(bool,withApp);Fixture f;f.app.segments[0].address=0x15000;
        f.driver.segments[0].data.resize(2084);f.driver.size=2084;
        const auto bundle=image(0x5000,QByteArray(0xf000,'A')+QByteArray(0xf000,'B'));Peer peer;QString error;
        QVERIFY(t008::images(f.app,f.driver,peer.plan,error));MemoryTransport wire;wire.respond=[&](const QByteArray &r){return peer.reply(r);};
        auto p=profile("boot");p.target="generic";p.applicationEraseBase=0x15000;p.applicationEraseLength=0x57000;p.stepEnabled["pre1003"]=true;
        UdsSession session(wire,{100,100,2000,0});FlashJob job(session,p,std::make_unique<TestKey>());QSignalSpy done(&job,&FlashJob::finished);
        QVERIFY2(job.start(withApp?f.app:FirmwareImage{},f.driver,bundle,error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,15000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        const auto eraseBoot=QByteArray::fromHex("3101ff0044000050000000f000");const auto eraseApp=QByteArray::fromHex("3101ff00440001500000057000");
        const auto dlBoot=QByteArray::fromHex("340044000050000000f000");const auto dlApp=QByteArray::fromHex("3400440001500000001000");
        QCOMPARE(wire.requests.count(eraseBoot),1);QCOMPARE(wire.requests.count(dlBoot),1);QCOMPARE(wire.requests.count(eraseApp),withApp?1:0);
        const int boot34=wire.requests.indexOf(dlBoot),firstCommit=wire.requests.indexOf(QByteArray::fromHex("3101ff02"));
        QVERIFY(boot34>wire.requests.indexOf(eraseBoot));QVERIFY(firstCommit>boot34);QCOMPARE(wire.requests.count(QByteArray::fromHex("3101ff02")),withApp?2:1);
        int blocks=0;QByteArray bootData;for(int i=boot34+1;i<firstCommit;++i)if(wire.requests[i].startsWith(char(0x36))){QCOMPARE(wire.requests[i].size(),1026);bootData+=wire.requests[i].mid(2);++blocks;}QCOMPARE(blocks,60);QCOMPARE(bootData,QByteArray(0xf000,'A'));
        if(withApp){QVERIFY(wire.requests.indexOf(eraseApp)>firstCommit);QVERIFY(wire.requests.indexOf(dlApp)>wire.requests.indexOf(eraseApp));}
        const auto req1003=QByteArray::fromHex("1003"),req1002=QByteArray::fromHex("1002");
        const auto seed=QByteArray::fromHex("2711"),key=QByteArray::fromHex("2712");
        QVERIFY(wire.requests.indexOf(req1003)<wire.requests.indexOf(req1002));QVERIFY(wire.requests.indexOf(req1002)<wire.requests.indexOf(seed));
        int keyIndex=-1,fingerprintIndex=-1,driverIndex=-1,verifyCount=0;
        for(int i=0;i<wire.requests.size();++i){const auto &r=wire.requests[i];
            if(r.startsWith(key))keyIndex=i;if(r.startsWith(QByteArray::fromHex("2ef184")))fingerprintIndex=i;
            if(r.startsWith(QByteArray::fromHex("34004420001400")))driverIndex=i;if(r.startsWith(QByteArray::fromHex("3101ff01")))++verifyCount;}
        QVERIFY(keyIndex>wire.requests.indexOf(seed));QVERIFY(fingerprintIndex>keyIndex);QVERIFY(driverIndex>fingerprintIndex);QVERIFY(wire.requests.indexOf(eraseBoot)>driverIndex);
        QCOMPARE(verifyCount,withApp?4:2);
        const int reset=wire.requests.indexOf(QByteArray::fromHex("1101"));QVERIFY(reset>wire.requests.lastIndexOf(QByteArray::fromHex("3101ff02")));
        QVERIFY(wire.requests.lastIndexOf(req1003)>reset);QVERIFY(wire.requests.indexOf(QByteArray::fromHex("14ffffff"))>reset);
        QCOMPARE(wire.requests.last(),QByteArray::fromHex("22f180"));
    }
    void bootSlotSelection_data(){
        QTest::addColumn<QString>("scenario");QTest::addColumn<bool>("checkNegative");
        QTest::newRow("A succeeds")<<QString("a_ok")<<true;
        QTest::newRow("A 78 then succeeds")<<QString("a_pending_ok")<<false;
        QTest::newRow("A pending then 70, B succeeds; check enabled")<<QString("fallback_pending")<<true;
        QTest::newRow("A 70, B succeeds; check disabled")<<QString("fallback")<<false;
        QTest::newRow("strict T008 fallback")<<QString("strict_fallback")<<false;
        QTest::newRow("both 70 stop")<<QString("both70")<<false;
        QTest::newRow("A other NRC stops")<<QString("a31")<<false;
        QTest::newRow("B other NRC stops")<<QString("b31")<<false;
        QTest::newRow("Driver 70 stops")<<QString("driver70")<<false;
        QTest::newRow("App 70 stops")<<QString("app70")<<false;
    }
    void bootSlotSelection(){
        QFETCH(QString,scenario);QFETCH(bool,checkNegative);Fixture f;f.app.segments[0].address=0x15000;Peer peer;QString error;
        QVERIFY(t008::images(f.app,f.driver,peer.plan,error));const auto bundle=image(0x5000,QByteArray(0xf000,'A')+QByteArray(0xf000,'B'));
        const auto a=QByteArray::fromHex("340044000050000000f000"),b=QByteArray::fromHex("3400440006d0000000f000"),driver=QByteArray::fromHex("34004420001400"),app=QByteArray::fromHex("34004400015000");
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){
            if(r==a){if(scenario=="a31")return QByteArray::fromHex("7f3431");
                if(scenario=="a_pending_ok"){QTimer::singleShot(5,&wire,[&wire]{emit wire.received(QByteArray::fromHex("74200402"));});return QByteArray::fromHex("7f3478");}
                if(scenario=="fallback_pending"){QTimer::singleShot(0,&wire,[&wire]{emit wire.received(QByteArray::fromHex("7f3470"));});return QByteArray::fromHex("7f3478");}
                if(scenario=="fallback"||scenario=="strict_fallback"||scenario=="both70"||scenario=="b31")return QByteArray::fromHex("7f3470");}
            if(r==b&&scenario=="both70")return QByteArray::fromHex("7f3470");
            if(r==b&&scenario=="b31")return QByteArray::fromHex("7f3431");
            if(r.startsWith(driver)&&scenario=="driver70")return QByteArray::fromHex("7f3470");
            if(r.startsWith(app)&&scenario=="app70")return QByteArray::fromHex("7f3470");
            return peer.reply(r);
        };
        auto p=profile("boot");if(scenario!="strict_fallback")p.target="generic";p.stepEnabled["pre1003"]=true;p.negativeResponseChecks["app34"]=checkNegative;p.applicationEraseBase=0x15000;p.applicationEraseLength=0x57000;
        UdsSession session(wire,{100,100,2000,0});QSignalSpy pending(&session,&UdsSession::notice);FlashJob job(session,p,std::make_unique<TestKey>());QSignalSpy done(&job,&FlashJob::finished),progress(&job,&FlashJob::progress);
        const bool withApp=scenario=="app70"||scenario=="fallback_pending";QVERIFY2(job.start(withApp?f.app:FirmwareImage{},f.driver,bundle,error),qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,15000);const bool success=scenario=="a_ok"||scenario=="a_pending_ok"||scenario=="fallback"||scenario=="strict_fallback"||scenario=="fallback_pending";
        QCOMPARE(done[0][0].toBool(),success);const auto erase=QByteArray::fromHex("3101ff0044000050000000f000");
        if(scenario=="driver70"){QCOMPARE(wire.requests.count(erase),0);QVERIFY(!wire.requests.contains(a));QVERIFY(!wire.requests.contains(b));return;}
        QCOMPARE(wire.requests.count(erase),1);QCOMPARE(wire.requests.count(a),1);
        const bool fallback=scenario=="fallback"||scenario=="strict_fallback"||scenario=="fallback_pending"||scenario=="both70"||scenario=="b31";
        QCOMPARE(wire.requests.count(b),fallback?1:0);if(fallback)QVERIFY(wire.requests.indexOf(b)>wire.requests.indexOf(a));
        if(scenario=="fallback_pending"||scenario=="a_pending_ok")QVERIFY(!pending.isEmpty());
        if(!success&&scenario!="app70"){
            const int first=wire.requests.indexOf(a);for(int i=first+1;i<wire.requests.size();++i)QVERIFY(!wire.requests[i].startsWith(char(0x36)));
            QVERIFY(!wire.requests.contains(QByteArray::fromHex("3101ff02")));QVERIFY(!wire.requests.contains(QByteArray::fromHex("1101")));return;
        }
        const auto selected=fallback?b:a;const int begin=wire.requests.indexOf(selected),commit=wire.requests.indexOf(QByteArray::fromHex("3101ff02"));QVERIFY(commit>begin);
        QByteArray payload;int blocks=0;for(int i=begin+1;i<commit;++i)if(wire.requests[i].startsWith(char(0x36))){QCOMPARE(wire.requests[i].size(),1026);payload+=wire.requests[i].mid(2);++blocks;}
        QCOMPARE(blocks,60);QCOMPARE(payload,QByteArray(0xf000,fallback?'B':'A'));
        const auto verify=QByteArray::fromHex(fallback?"3101ff010006d0000000f00000000000":"3101ff01000050000000f00000000000");QVERIFY(wire.requests.indexOf(verify)>begin&&wire.requests.indexOf(verify)<commit);
        if(scenario=="app70"){QVERIFY(!wire.requests.contains(b));QVERIFY(!wire.requests.contains(QByteArray::fromHex("1101")));return;}
        const auto reset=wire.requests.indexOf(QByteArray::fromHex("1101"));QVERIFY(reset>commit);QVERIFY(wire.requests.lastIndexOf(QByteArray::fromHex("1003"))>reset);
        if(withApp)QVERIFY(wire.requests.indexOf(QByteArray::fromHex("3101ff00440001500000057000"))>commit);
        QVERIFY(!progress.isEmpty());QCOMPARE(progress.last()[0].toInt(),100);
    }
    void simulatedBootSlotSelection_data(){QTest::addColumn<quint32>("active");QTest::addColumn<quint32>("selected");QTest::newRow("A active")<<quint32(0x5000)<<quint32(0x6d000);QTest::newRow("B active")<<quint32(0x6d000)<<quint32(0x5000);}
    void simulatedBootSlotSelection(){
        QFETCH(quint32,active);QFETCH(quint32,selected);FlashProfile p;SimulatedUdsEcu ecu(p);ecu.t008VerifySkipped=true;ecu.t008BootSelection=true;ecu.activeBootAddress=active;ecu.maxBlockLength=1026;
        auto request=[&](const QByteArray &r){const auto replies=ecu.handle(r);return replies.isEmpty()?QByteArray():replies.last();};
        QCOMPARE(request(QByteArray::fromHex("1002")).left(2),QByteArray::fromHex("5002"));QCOMPARE(request(QByteArray::fromHex("2711")).left(2),QByteArray::fromHex("6711"));
        SimulationKey key;QString error;const auto value=key.calculate(0x11,QByteArray::fromHex("12345678"),error);QVERIFY(error.isEmpty());QCOMPARE(request(QByteArray::fromHex("2712")+value),QByteArray::fromHex("6712"));
        QCOMPARE(request(QByteArray::fromHex("3101ff0044000050000000f000")),QByteArray::fromHex("7101ff0000"));
        const auto a=QByteArray::fromHex("340044000050000000f000"),b=QByteArray::fromHex("3400440006d0000000f000");const auto first=request(a);
        if(active==0x5000){QCOMPARE(first,QByteArray::fromHex("7f3470"));QCOMPARE(request(b),QByteArray::fromHex("74200402"));}else QCOMPARE(first,QByteArray::fromHex("74200402"));
        for(int i=0;i<60;++i){const auto packet=QByteArray(1,char(0x36))+char(i+1)+QByteArray(1024,'S');QCOMPARE(request(packet),QByteArray(1,char(0x76))+char(i+1));}
        QCOMPARE(request(QByteArray::fromHex("37")),QByteArray::fromHex("77"));
        QCOMPARE(request(QByteArray::fromHex("3101ff01")+be32(selected)+be32(0xf000)+QByteArray(4,'\0')),QByteArray::fromHex("7101ff0100"));
        QCOMPARE(request(QByteArray::fromHex("3101ff02")),QByteArray::fromHex("7101ff0200"));QCOMPARE(ecu.memory(selected),QByteArray(0xf000,'S'));
    }
    void recoverableSlotNegativePreservesKeepalive_data(){QTest::addColumn<QByteArray>("reply");QTest::addColumn<bool>("marked");QTest::addColumn<bool>("kept");
        QTest::newRow("matched slot 70")<<QByteArray::fromHex("7f3470")<<true<<true;
        QTest::newRow("other negative")<<QByteArray::fromHex("7f3431")<<true<<false;
        QTest::newRow("unmarked 70")<<QByteArray::fromHex("7f3470")<<false<<false;
    }
    void recoverableSlotNegativePreservesKeepalive(){
        QFETCH(QByteArray,reply);QFETCH(bool,marked);QFETCH(bool,kept);MemoryTransport wire;
        wire.respond=[&](const QByteArray &r){if(r==QByteArray::fromHex("1003"))return QByteArray::fromHex("500300140002");if(r.startsWith(char(0x34)))return reply;return QByteArray();};
        UdsSession session(wire,{20,30,1000,25});bool entered=false,done=false,ok=false;
        QVERIFY(session.request(QByteArray::fromHex("1003"),QByteArray::fromHex("5003"),[&](bool success,const QByteArray &,const QString &){entered=success;}));QTRY_VERIFY_WITH_TIMEOUT(entered,500);wire.requests.clear();
        QVERIFY(session.request(QByteArray::fromHex("340044000050000000f000"),QByteArray::fromHex("74"),[&](bool success,const QByteArray &,const QString &){done=true;ok=success;},false,false,marked?QByteArray::fromHex("7f3470"):QByteArray()));
        QTRY_VERIFY_WITH_TIMEOUT(done,500);QVERIFY(!ok);QTest::qWait(150);QCOMPARE(wire.requests.contains(QByteArray::fromHex("3e80")),kept);session.cancel();
    }
    void actualBootBundleIdentifies(){
        const auto path=qEnvironmentVariable("T008_TEST_AB_BOOT");if(path.isEmpty())QSKIP("Set T008_TEST_AB_BOOT to the current Boot bundle");
        FirmwareImage parsed,bundle;QString error;QVERIFY2(FirmwareImage::load(path,0x5000,parsed,error),qPrintable(error));
        QCOMPARE(FirmwareImage::identify(parsed),FirmwareImage::Kind::BootBundle);
        QVERIFY2(FirmwareImage::loadBootBundle(path,bundle,error),qPrintable(error));QCOMPARE(bundle.segments.size(),1);
        QFile source(path);QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(bundle.segments[0].data,source.readAll());
    }
    void actualSparseBootHexNotAcceptedAsFullBundle(){
        const auto path=qEnvironmentVariable("T008_TEST_AB_SPARSE");if(path.isEmpty())QSKIP("Set T008_TEST_AB_SPARSE to the sparse Boot HEX");
        FirmwareImage bundle;QString error;QVERIFY(!FirmwareImage::loadBootBundle(path,bundle,error));QVERIFY(error.contains("both complete"));
    }
    void actualBootBundleSequence(){
        const auto driverPath=qEnvironmentVariable("T008_TEST_AB_DRIVER"),bootPath=qEnvironmentVariable("T008_TEST_AB_BOOT"),appPath=qEnvironmentVariable("T008_TEST_AB_APP");
        if(driverPath.isEmpty()||bootPath.isEmpty()||appPath.isEmpty())QSKIP("Set all three candidate image paths");
        FirmwareImage driver,bundle,app;QString error;
        QVERIFY2(FirmwareImage::load(driverPath,0x20001400,driver,error),qPrintable(error));
        QVERIFY2(FirmwareImage::load(bootPath,0x5000,bundle,error),qPrintable(error));
        QVERIFY2(FirmwareImage::load(appPath,0x15000,app,error),qPrintable(error));
        for(int selected=0;selected<2;++selected){
            Peer peer;QVERIFY(t008::images(app,driver,peer.plan,error));MemoryTransport wire;
            wire.respond=[&](const QByteArray&r){if(selected&&r==QByteArray::fromHex("340044000050000000f000"))return QByteArray::fromHex("7f3470");return peer.reply(r);};
            auto p=profile("boot");p.target="generic";p.applicationEraseBase=0x15000;p.applicationEraseLength=0x57000;p.stepEnabled["pre1003"]=true;
            UdsSession session(wire,{100,100,2000,0});FlashJob job(session,p,std::make_unique<TestKey>());QSignalSpy done(&job,&FlashJob::finished);
            QVERIFY2(job.start(app,driver,bundle,error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
            QMap<quint32,QByteArray> sent;quint32 address=0;int bootBlocks=0,driverBegin=-1,appBegin=-1;
            for(int i=0;i<wire.requests.size();++i){const auto &r=wire.requests[i];
                if(r.startsWith(QByteArray::fromHex("340044"))){address=readBe(r.mid(3,4));if(address==0x20001400)driverBegin=i;if(address==0x15000)appBegin=i;}
                else if(r.startsWith(char(0x36))){sent[address]+=r.mid(2);if(address==(selected?0x6d000u:0x5000u)){QCOMPARE(r.size(),1026);++bootBlocks;}}
            }
            QCOMPARE(bootBlocks,60);
            const auto erase=wire.requests.indexOf(QByteArray::fromHex("3101ff0044000050000000f000"));
            const auto slot=wire.requests.indexOf(QByteArray::fromHex(selected?"3400440006d0000000f000":"340044000050000000f000"));
            const auto verify=wire.requests.indexOf(QByteArray::fromHex(selected?"3101ff010006d0000000f00000000000":"3101ff01000050000000f00000000000"));
            const auto commit=wire.requests.indexOf(QByteArray::fromHex("3101ff02"));
            const auto reset=wire.requests.indexOf(QByteArray::fromHex("1101"));
            QVERIFY(driverBegin>=0&&erase>driverBegin&&slot>erase&&verify>slot&&commit>verify&&appBegin>commit&&reset>appBegin);
            QCOMPARE(sent.value(0x20001400),driver.segments[0].data);QCOMPARE(sent.value(selected?0x6d000:0x5000),bundle.segments[0].data.mid(selected*0xf000,0xf000));
            QCOMPARE(sent.value(0x15000),app.segments[0].data.left(4096));QCOMPARE(sent.value(0x16000),app.segments[0].data.mid(4096));
        }
    }
    void selectedFileVersions(){
        Fixture f;t008::Plan p;QString error;QStringList mismatches;
        QVERIFY(t008::images(f.app,f.driver,p,error));
        QMap<QString,QString> expected;QVERIFY(t008::compareFileVersions(p,expected,mismatches,error));QVERIFY(mismatches.isEmpty());
        expected={{"driverAbi","1.1"},{"appVersion","T008A0001"},{"minimumBoot","001"}};
        QVERIFY(t008::compareFileVersions(p,expected,mismatches,error));QVERIFY(mismatches.isEmpty());
        expected["driverAbi"]="1.4";expected["appVersion"]="OTHER";expected["minimumBoot"]="002";
        QVERIFY(t008::compareFileVersions(p,expected,mismatches,error));QCOMPARE(mismatches.size(),3);
        QVERIFY(mismatches.join(" ").contains("1.1"));QVERIFY(mismatches.join(" ").contains("1.4"));
        expected.clear();p.hasDriverAbi=false;p.hasAppVersion=false;p.hasMinimumBoot=false;
        QVERIFY(t008::compareFileVersions(p,expected,mismatches,error));
        for(const auto key:{"driverAbi","appVersion","minimumBoot"}){expected.clear();expected[key]="1";QVERIFY(t008::compareFileVersions(p,expected,mismatches,error));QCOMPARE(mismatches.size(),1);QVERIFY(mismatches.first().contains("无法解析"));}
    }
    void packageDataWithoutLocalTargetRejection(){
        Fixture f;t008::Plan p;QString error;auto &d=f.driver.segments[0].data;
        put(d,12,99,2);put(d,52,0);put(d,60,7);put(d,32,1);f.driver.segments[0].address=0x20003000;
        auto &a=f.app.segments[0].data;put(a,8,1);put(a,4100,0);f.app.segments[0].address=0x30000;
        QVERIFY2(t008::images(f.app,f.driver,p,error),qPrintable(error));QCOMPARE(p.driver.address,quint32(0x20003000));
        QCOMPARE(p.application.size(),2);QCOMPARE(p.application[0].address,quint32(0x30000));QCOMPARE(p.application[1].address,quint32(0x31000));
        QCOMPARE(p.application[0].data+p.application[1].data,a);
    }
    void compatibilityProfile(){
        t008::Plan p;QString e;p.project="S32K144TAUTOSAR";p.hardware="Rev.A4.06.5";
        p.configuration="T008-CFG-TEST001";p.calibration="T008-CAL-TEST001";p.driverMinor=1;p.driverCapabilities=15;
        QVERIFY2(t008::compatibility(p,e),qPrintable(e));p.driverMinor=2;p.driverCapabilities=31;QVERIFY2(t008::compatibility(p,e),qPrintable(e));
        const auto good=p;
        p.project="OTHER";QVERIFY(t008::compatibility(p,e));p=good;
        p.hardware="OTHER";QVERIFY(t008::compatibility(p,e));p=good;
        p.configuration="OTHER";QVERIFY(t008::compatibility(p,e));p=good;
        p.calibration="OTHER";QVERIFY(t008::compatibility(p,e));p=good;
        p.driverMinor=3;QVERIFY(!t008::compatibility(p,e));p=good;
        p.driverCapabilities=15;QVERIFY(!t008::compatibility(p,e));p=good;
        p.driverMinor=1;QVERIFY(!t008::compatibility(p,e));
    }
    void driverAbiVersions(){
        Fixture f;t008::Plan plan;QString error;auto &d=f.driver.segments[0].data;
        QVERIFY2(t008::images(f.app,f.driver,plan,error),qPrintable(error));
        put(d,12,2,2);put(d,52,31);put(d,88,130);
        QVERIFY2(t008::images(f.app,f.driver,plan,error),qPrintable(error));
        const QByteArray valid=d;
        for(const auto &change:QList<QPair<int,quint32>>{{12,1},{12,3},{20,0x20001408},{28,255},{52,15},{52,30},{52,63},{56,2048},{60,16},{68,21000000},{88,0},{88,127},{88,128},{88,129},{88,136},{92,1},{16,135},{24,137}}){
            d=valid;put(d,change.first,change.second,change.first==12?2:4);
            QVERIFY2(t008::images(f.app,f.driver,plan,error),qPrintable(error));
        }
        d=valid;d[127]=1;QVERIFY(t008::images(f.app,f.driver,plan,error));
        d=valid;put(d,12,1,2);put(d,52,15);QVERIFY(t008::images(f.app,f.driver,plan,error));
        d=valid;put(d,12,4,2);put(d,52,64);put(d,60,1024);QVERIFY2(t008::images(f.app,f.driver,plan,error),qPrintable(error));
        const auto sectionOnly=d;
        for(const auto &change:QList<QPair<int,quint32>>{{12,3},{52,31},{52,65},{56,2048},{60,8},{68,1},{88,0},{88,129},{88,136},{92,1}}){
            d=sectionOnly;put(d,change.first,change.second,change.first==12?2:4);QVERIFY(t008::images(f.app,f.driver,plan,error));
        }
    }
    void descriptorV2(){
        Fixture f;auto &b=f.app.segments[0].data;
        b.replace(0,4096,QByteArray(4096,char(0xff)));b.replace(0,8,"T008APP2");put(b,8,0x800);put(b,12,1,2);
        b.replace(14,16,QByteArray("S32K144TAUTOSAR").leftJustified(16,'\0'));b.replace(30,16,QByteArray("Rev.A4.06.5").leftJustified(16,'\0'));
        b.replace(46,9,QByteArray("T008A0001"));b.replace(55,16,"T008-CFG-TEST001");b.replace(71,16,"T008-CAL-TEST001");
        t008::Plan p;QString error;QVERIFY2(t008::images(f.app,f.driver,p,error),qPrintable(error));
        QCOMPARE(p.project,QByteArray("S32K144TAUTOSAR"));QCOMPARE(p.hardware,QByteArray("Rev.A4.06.5"));QCOMPARE(p.version,QByteArray("T008A0001"));QCOMPARE(p.minBoot,quint16(1));
        QCOMPARE(p.configuration,QByteArray("T008-CFG-TEST001"));QCOMPARE(p.calibration,QByteArray("T008-CAL-TEST001"));
        auto original=b;put(b,12,1000,2);QVERIFY(t008::images(f.app,f.driver,p,error));QCOMPARE(p.minBoot,quint16(1000));b=original;
        put(b,8,0x7f8);QVERIFY(t008::images(f.app,f.driver,p,error));b=original;
        b[42]='X';QVERIFY(t008::images(f.app,f.driver,p,error));b=original;
        b[30]=char(0x80);QVERIFY(t008::images(f.app,f.driver,p,error));b=original;
        b[87]=0;QVERIFY(t008::images(f.app,f.driver,p,error));b=original;b[88]=0;QVERIFY(t008::images(f.app,f.driver,p,error));b=original;
        b[0]='X';QVERIFY(t008::images(f.app,f.driver,p,error));QVERIFY(!p.hasAppVersion&&!p.hasMinimumBoot);
        Fixture old;old.app.segments[0].data.replace(0,8,"T008APP1");QVERIFY(t008::images(old.app,old.driver,p,error));QVERIFY(!p.hasAppVersion);
        Fixture old80;auto &legacy=old80.app.segments[0].data;legacy.replace(30,9,QByteArray("HW1").leftJustified(9,'\0'));
        legacy.replace(39,9,"APP000001");legacy.replace(48,16,QByteArray(16,'C'));legacy.replace(64,16,QByteArray(16,'L'));legacy.replace(80,8,QByteArray(8,char(0xff)));
        QVERIFY(t008::images(old80.app,old80.driver,p,error));
    }
    void imagesAcceptMergedAndSplit(){
        Fixture f;t008::Plan p;QString error;QVERIFY2(t008::images(f.app,f.driver,p,error),qPrintable(error));
        QCOMPARE(p.application.size(),2);QCOMPARE(p.application[0].address,quint32(0x20000));QCOMPARE(p.application[0].data.size(),4096);QCOMPARE(p.application[1].data.size(),0x800);
        f.app.segments=p.application;QVERIFY(t008::images(f.app,f.driver,p,error));
    }
    void actualFirmwareFiles(){
        const QString appPath=qEnvironmentVariable("T008_TEST_APP_HEX"),driverPath=qEnvironmentVariable("T008_TEST_DRIVER_BIN");
        if(appPath.isEmpty()&&driverPath.isEmpty())QSKIP("Set both T008_TEST_APP_HEX and T008_TEST_DRIVER_BIN to validate actual public artifacts offline");
        QVERIFY(!appPath.isEmpty()&&!driverPath.isEmpty());FirmwareImage app,driver;QString error;t008::Plan plan;
        QVERIFY2(FirmwareImage::load(appPath,0x20000,app,error),qPrintable(error));
        QVERIFY2(FirmwareImage::load(driverPath,0x20001400,driver,error),qPrintable(error));
        QVERIFY2(t008::images(app,driver,plan,error),qPrintable(error));
        QVERIFY2(t008::compatibility(plan,error),qPrintable(error));
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
    void actualBinHexEquivalence(){
        const auto appBin=qEnvironmentVariable("T008_TEST_REAL_APP"),appHex=qEnvironmentVariable("T008_TEST_APP_HEX");
        const auto driverBin=qEnvironmentVariable("T008_TEST_DRIVER_BIN"),driverHex=qEnvironmentVariable("T008_TEST_DRIVER_HEX");
        if(appBin.isEmpty()||appHex.isEmpty()||driverBin.isEmpty()||driverHex.isEmpty())QSKIP("Set actual BIN and HEX paths for both images");
        FirmwareImage a,b,c,d;QString error;t008::Plan binPlan,hexPlan;
        QVERIFY2(FirmwareImage::load(appBin,0x20000,a,error),qPrintable(error));QVERIFY2(FirmwareImage::load(appHex,0x20000,b,error),qPrintable(error));
        QVERIFY2(FirmwareImage::load(driverBin,0x20001400,c,error),qPrintable(error));QVERIFY2(FirmwareImage::load(driverHex,0x20001400,d,error),qPrintable(error));
        QVERIFY2(t008::images(a,c,binPlan,error),qPrintable(error));QVERIFY2(t008::images(b,d,hexPlan,error),qPrintable(error));
        QVERIFY2(t008::compatibility(binPlan,error),qPrintable(error));QVERIFY2(t008::compatibility(hexPlan,error),qPrintable(error));
        QCOMPARE(binPlan.application.size(),hexPlan.application.size());
        for(int i=0;i<binPlan.application.size();++i){QCOMPARE(binPlan.application[i].address,hexPlan.application[i].address);QCOMPARE(binPlan.application[i].data,hexPlan.application[i].data);}
        QCOMPARE(binPlan.driver.address,hexPlan.driver.address);QCOMPARE(binPlan.driver.data,hexPlan.driver.data);
    }
    void addressDiagnostics(){
        Fixture f;QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto appPath=dir.filePath("app.bin"),driverPath=dir.filePath("driver.bin");
        QFile af(appPath),df(driverPath);QVERIFY(af.open(QIODevice::WriteOnly));QCOMPARE(af.write(f.app.segments[0].data),qint64(f.app.size));af.close();
        QVERIFY(df.open(QIODevice::WriteOnly));QCOMPARE(df.write(f.driver.segments[0].data),qint64(f.driver.size));df.close();
        auto settings=host::ChannelSettings::defaults(communication::Bus::Can);
        QCOMPARE(settings.applicationAddress,QString("00015000"));QCOMPARE(settings.flashAddress,QString("20001400"));
        settings.applicationAddress="00010000";settings.flashAddress="00000000";
        FirmwareImage app,driver;QString error;t008::Plan plan;
        QVERIFY(FirmwareImage::load(appPath,settings.applicationAddress.toUInt(nullptr,16),app,error));
        QVERIFY(FirmwareImage::load(driverPath,settings.flashAddress.toUInt(nullptr,16),driver,error));
        QVERIFY(t008::images(app,driver,plan,error));QCOMPARE(plan.application[0].address,quint32(0x10000));QCOMPARE(plan.driver.address,quint32(0));
        app=f.app;QVERIFY(t008::images(app,driver,plan,error));QCOMPARE(plan.driver.address,quint32(0));
        app.segments={{0x20000,f.app.segments[0].data.left(4096)},{0x21008,f.app.segments[0].data.mid(4096)}};
        QVERIFY(t008::images(app,f.driver,plan,error));QCOMPARE(plan.application[1].address,quint32(0x21008));
        app.segments[1].address=0x20ff8;QVERIFY(t008::images(app,f.driver,plan,error));QCOMPARE(plan.application[1].address,quint32(0x20ff8));
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
        t008::Plan p;QString error;QVERIFY2(t008::images(f.app,f.driver,p,error),qPrintable(error));
    }
    void ecuImageLimits(){
        QString error;t008::Plan plan;
        for(quint32 total:{0x56000u,0x56100u,0x57000u,0x57400u}){
            Fixture f;auto &bytes=f.app.segments[0].data;bytes.append(QByteArray(total-bytes.size(),char(0xff)));
            put(bytes,8,total-4096);
            QVERIFY(t008::images(f.app,f.driver,plan,error));QCOMPARE(plan.application[0].data.size()+plan.application[1].data.size(),int(total));
        }
        for(const auto field:QList<QPair<int,quint32>>{{28,0},{28,255},{28,257},{64,8}}){
            Fixture f;put(f.driver.segments[0].data,field.first,field.second);
            QVERIFY(t008::images(f.app,f.driver,plan,error));
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
        {auto j=p.toJson();auto controls=j["stepEnabled"].toObject();controls["app36"]=false;j["stepEnabled"]=controls;QVERIFY(!FlashProfile::fromJson(j,restored,e));}
        {auto j=p.toJson();auto controls=j["negativeResponseChecks"].toObject();controls["app36"]=false;j["negativeResponseChecks"]=controls;QVERIFY(FlashProfile::fromJson(j,restored,e));QVERIFY(restored.checksNegativeResponse("app36"));}
        {auto j=p.toJson();auto controls=j["timeoutChecks"].toObject();controls["app36"]=false;j["timeoutChecks"]=controls;QVERIFY(FlashProfile::fromJson(j,restored,e));QVERIFY(!restored.checksTimeout("app36"));}
        auto b=profile("boot");QVERIFY(!b.enabled("pre1001"));QVERIFY(b.enabled("pre1003"));QVERIFY(b.enabled("boot1002"));QVERIFY(!b.checksTimeout("post1001"));
        FlashProfile generic;generic.flow="app";generic.stepEnabled["app36"]=false;QVERIFY(!generic.enabled("app36"));QVERIFY(generic.checksNegativeResponse("app36"));
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
            QVERIFY(restored.enabled(id));QVERIFY(!restored.checksNegativeResponse(id));QVERIFY(!restored.checksTimeout(id));
            QVERIFY(restored.toJson()["stepEnabled"].toObject()[id].toBool());
        }
        for(const auto id:{"pre1001","pre2701","pre2702","pre220101","pre31010203","pre1003control","pre8502","pre2803"})QVERIFY(!restored.enabled(id));
        for(const auto map:{"stepEnabled","negativeResponseChecks","timeoutChecks"})for(const auto id:{"pre1001","pre1003"}){
            auto bad=old;auto flags=bad[map].toObject();flags[id]="false";bad[map]=flags;QVERIFY(!FlashProfile::fromJson(bad,restored,error));
            bad=old;flags=bad[map].toObject();flags["unknownStep"]=true;bad[map]=flags;QVERIFY(!FlashProfile::fromJson(bad,restored,error));
        }
        auto app=profile("app").toJson();auto flags=app["stepEnabled"].toObject();flags["pre1001"]=false;app["stepEnabled"]=flags;
        QVERIFY(!FlashProfile::fromJson(app,restored,error));
        for(const auto map:{"stepEnabled"}){
            auto bad=old;auto required=bad[map].toObject();required["boot1002"]=false;bad[map]=required;QVERIFY(!FlashProfile::fromJson(bad,restored,error));
        }
        auto legacy=old;legacy["feedbackChecks"]=legacy.take("negativeResponseChecks");
        QVERIFY(FlashProfile::fromJson(legacy,restored,error));QVERIFY(!restored.negativeResponseChecks["pre1001"].toBool());QVERIFY(!restored.negativeResponseChecks["pre1003"].toBool());
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
        QVERIFY(!t008::validateBlockLimit(1025,error));QVERIFY(t008::validateBlockLimit(1026,error));QVERIFY(wire.requests.isEmpty());
        f.app.segments[0].data[55]='X';t008::Plan differentIdentity;
        QVERIFY(t008::images(f.app,f.driver,differentIdentity,error));QVERIFY(t008::compatibility(differentIdentity,error));QVERIFY(wire.requests.isEmpty());
        Fixture broken;broken.app.segments[0].data[0]='X';t008::Plan opaqueImage;
        QVERIFY(t008::images(broken.app,broken.driver,opaqueImage,error));QVERIFY(wire.requests.isEmpty());
    }
    void bootNegativePolicy_data(){
        QTest::addColumn<bool>("strict");QTest::addColumn<bool>("check");QTest::addColumn<bool>("pending");
        for(bool strict:{false,true})for(bool check:{false,true})for(bool pending:{false,true})
            QTest::newRow(qPrintable(QString("%1-%2-%3").arg(strict).arg(check).arg(pending)))<<strict<<check<<pending;
    }
    void bootNegativePolicy(){
        QFETCH(bool,strict);QFETCH(bool,check);QFETCH(bool,pending);Fixture f;Peer peer;peer.inBoot=true;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){if(r==QByteArray::fromHex("1002")){
            if(pending){QTimer::singleShot(5,&wire,[&wire]{emit wire.received(QByteArray::fromHex("7f1022"));});return QByteArray::fromHex("7f1078");}
            return QByteArray::fromHex("7f1022");}return peer.reply(r);};
        auto p=profile("boot");if(!strict){p.target="generic";p.stepEnabled["pre1003"]=true;}
        p.negativeResponseChecks["boot1002"]=check;UdsSession session(wire,{100,100,2000,0});FlashJob job(session,p,std::make_unique<TestKey>());
        QSignalSpy done(&job,&FlashJob::finished);QVERIFY2(job.start(f.app,f.driver,error),qPrintable(error));
        if(check){QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QVERIFY(!done[0][0].toBool());QVERIFY(!wire.requests.contains(QByteArray::fromHex("2711")));}
        else{QTRY_VERIFY_WITH_TIMEOUT(wire.requests.contains(QByteArray::fromHex("2711")),3000);}
        job.cancel();
    }
    void post14NegativePolicy_data(){QTest::addColumn<bool>("check");QTest::newRow("unchecked")<<false;QTest::newRow("checked")<<true;}
    void post14NegativePolicy(){
        QFETCH(bool,check);Fixture f;Peer peer;peer.inBoot=true;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        peer.corruptRequest=QByteArray::fromHex("14ffffff");peer.replaceResponse=QByteArray::fromHex("7f1422");
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){return peer.reply(r);};auto p=profile("boot");p.negativeResponseChecks["post14"]=check;
        UdsSession session(wire,{100,100,2000,0});FlashJob job(session,p,std::make_unique<TestKey>());QSignalSpy done(&job,&FlashJob::finished);
        QVERIFY2(job.start(f.app,f.driver,error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);QVERIFY(!done[0][0].toBool());
        QVERIFY(wire.requests.contains(QByteArray::fromHex("14ffffff")));
        QCOMPARE(wire.requests.contains(QByteArray::fromHex("288001")),!check);
    }
    void responseDependency_data(){
        QTest::addColumn<bool>("strict");QTest::addColumn<QString>("mode");
        for(bool strict:{false,true})for(const auto mode:{"seed-timeout","seed-nrc","seed-pending","key-timeout","download-timeout"})
            QTest::newRow(qPrintable(QString("%1-%2").arg(strict).arg(mode)))<<strict<<QString(mode);
    }
    void responseDependency(){
        QFETCH(bool,strict);QFETCH(QString,mode);Fixture f;Peer peer;peer.inBoot=true;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){
            if(r==QByteArray::fromHex("2711")){if(mode=="seed-timeout")return QByteArray();if(mode=="seed-nrc")return QByteArray::fromHex("7f2722");if(mode=="seed-pending")return QByteArray::fromHex("7f2778");}
            if(r.startsWith(QByteArray::fromHex("2712"))&&mode=="key-timeout")return QByteArray();
            if(r.startsWith(QByteArray::fromHex("34004420001400"))&&mode=="download-timeout")return QByteArray();
            return peer.reply(r);
        };
        auto p=profile("boot");if(!strict){p.target="generic";p.stepEnabled["pre1003"]=true;}p.negativeResponseChecks["bootSeed"]=false;
        UdsSession session(wire,{20,40,500,0});FlashJob job(session,p,std::make_unique<TestKey>());QSignalSpy done(&job,&FlashJob::finished);
        QVERIFY2(job.start(f.app,f.driver,error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);QVERIFY(!done[0][0].toBool());
        if(mode.startsWith("seed")){QVERIFY(done[0][1].toString().contains(mode=="seed-nrc"?"NRC 22":"requires a valid security seed"));for(const auto &r:wire.requests)QVERIFY(!r.startsWith(QByteArray::fromHex("2712")));}
        else if(mode=="download-timeout"){QVERIFY(done[0][1].toString().contains("TransferData requires RequestDownload parameters"));for(const auto &r:wire.requests)QVERIFY(r.isEmpty()||quint8(r[0])!=0x36);}
        else{bool fingerprint=false;for(const auto &r:wire.requests)fingerprint|=r.startsWith(QByteArray::fromHex("2ef184"));QVERIFY(fingerprint);}
    }
    void seedReplyHandling_data(){
        QTest::addColumn<bool>("strict");QTest::addColumn<bool>("malformed");
        QTest::newRow("generic-zero")<<false<<false;QTest::newRow("strict-zero")<<true<<false;QTest::newRow("strict-short")<<true<<true;
    }
    void seedReplyHandling(){
        QFETCH(bool,strict);QFETCH(bool,malformed);Fixture f;Peer peer;peer.inBoot=true;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){if(r==QByteArray::fromHex("2711"))return QByteArray::fromHex("6711")+QByteArray(malformed?1:16,0);return peer.reply(r);};
        auto p=profile("boot");if(!strict){p.target="generic";p.stepEnabled["pre1003"]=true;}p.negativeResponseChecks["bootSeed"]=false;
        UdsSession session(wire,{20,40,500,0});FlashJob job(session,p,std::make_unique<TestKey>());QSignalSpy done(&job,&FlashJob::finished);
        QVERIFY2(job.start(f.app,f.driver,error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);
        QCOMPARE(done[0][0].toBool(),!malformed);bool keySent=false,fingerprint=false;
        for(const auto &r:wire.requests){keySent|=r.startsWith(QByteArray::fromHex("2712"));fingerprint|=r.startsWith(QByteArray::fromHex("2ef184"));}
        QVERIFY(!keySent);QCOMPARE(fingerprint,!malformed);
    }
    void uncheckedTimeoutIsolation_data(){
        QTest::addColumn<bool>("strict");QTest::addColumn<bool>("pending");
        for(bool strict:{false,true})for(bool pending:{false,true})QTest::newRow(qPrintable(QString("%1-%2").arg(strict).arg(pending)))<<strict<<pending;
    }
    void uncheckedTimeoutIsolation(){
        QFETCH(bool,strict);QFETCH(bool,pending);Fixture f;Peer peer;peer.inBoot=true;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){if(r==QByteArray::fromHex("1002"))return pending?QByteArray::fromHex("7f1078"):QByteArray();
            if(r==QByteArray::fromHex("2711"))return QByteArray::fromHex("7f2712");return peer.reply(r);};
        auto p=profile("boot");if(!strict){p.target="generic";p.stepEnabled["pre1003"]=true;}p.timeoutChecks["boot1002"]=false;
        UdsSession session(wire,{20,40,500,0});FlashJob job(session,p,std::make_unique<TestKey>());QSignalSpy done(&job,&FlashJob::finished);
        QVERIFY2(job.start(f.app,f.driver,error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);
        QVERIFY(wire.requests.contains(QByteArray::fromHex("2711")));QVERIFY(!done[0][0].toBool());
    }
    void timedOutTransferBlocksAmbiguousNextTransfer(){
        Fixture f;Peer peer;peer.inBoot=true;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));int transfers=0;
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){if(quint8(r[0])==0x36){++transfers;return QByteArray();}return peer.reply(r);};
        auto p=profile("boot");p.timeoutChecks["driver36"]=false;UdsSession session(wire,{20,40,500,0});FlashJob job(session,p,std::make_unique<TestKey>());QSignalSpy done(&job,&FlashJob::finished);
        QVERIFY2(job.start(f.app,f.driver,error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);
        QCOMPARE(transfers,1);QVERIFY(!done[0][0].toBool());QVERIFY(done[0][1].toString().contains("unresolved"));
    }
    void coreNegativeCannotBypass_data(){
        QTest::addColumn<bool>("strict");QTest::addColumn<QString>("id");
        for(bool strict:{false,true})for(const auto id:{"driver34","driver36","driver37","app34","app36","app37"})
            QTest::newRow(qPrintable(QString("%1-%2").arg(strict).arg(id)))<<strict<<QString(id);
    }
    void coreNegativeCannotBypass(){
        QFETCH(bool,strict);QFETCH(QString,id);Fixture f;Peer peer;peer.inBoot=true;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        quint32 address=0;bool injected=false;MemoryTransport wire;wire.respond=[&](const QByteArray &r){
            const int sid=quint8(r[0]);if(sid==0x34)address=readBe(r.mid(3,4));
            const bool driver=address==0x20001400;const auto name=QString(driver?"driver":"app")+QString::number(sid,16);
            if(name==id){injected=true;return QByteArray(1,char(0x7f))+char(sid)+char(0x22);}return peer.reply(r);
        };
        auto p=profile("boot");if(!strict){p.target="generic";p.stepEnabled["pre1003"]=true;}p.negativeResponseChecks[id]=false;
        UdsSession session(wire,{20,40,500,0});FlashJob job(session,p,std::make_unique<TestKey>());QSignalSpy done(&job,&FlashJob::finished);
        QVERIFY2(job.start(f.app,f.driver,error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);
        QVERIFY(injected);QVERIFY(!done[0][0].toBool());QVERIFY(done[0][1].toString().contains("NRC 22"));
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
    void genericV2LimitsTransferBlock(){
        Fixture f;Peer peer;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){return quint8(r[0])==0x34?QByteArray::fromHex("74201000"):peer.reply(r);};
        auto p=profile("app");p.target="generic";p.consecutiveFrameByteLimit=4095;
        UdsSession session(wire,{100,100,2000,0});FlashJob job(session,p,std::make_unique<TestKey>());
        QSignalSpy done(&job,&FlashJob::finished);QVERIFY(job.start(f.app,f.driver,error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);
        QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));int largest=0;
        for(const auto &r:wire.requests)if(!r.isEmpty()&&quint8(r[0])==0x36)largest=qMax(largest,r.size()-2);
        QCOMPARE(largest,1024);
    }
    void t008AppPhraseBlocks_data(){
        QTest::addColumn<int>("limit");QTest::addColumn<int>("expected");
        for(const auto pair:QList<QPair<int,int>>{{9,0},{10,0},{1024,0},{1025,0},{1026,1024},{4095,1024}})
            QTest::newRow(qPrintable(QString::number(pair.first)))<<pair.first<<pair.second;
    }
    void t008AppPhraseBlocks(){
        QFETCH(int,limit);QFETCH(int,expected);Fixture f;Peer peer;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){return quint8(r[0])==0x34?QByteArray::fromHex("74201000"):peer.reply(r);};
        auto p=profile("app");p.target="generic";p.consecutiveFrameByteLimit=limit;
        UdsSession session(wire,{100,100,2000,0});FlashJob job(session,p,std::make_unique<TestKey>());QSignalSpy done(&job,&FlashJob::finished);
        if(!expected){QVERIFY(job.start(f.app,f.driver,error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,15000);QVERIFY(!done[0][0].toBool());QVERIFY(done[0][1].toString().contains("1024 App bytes"));
            bool appDownload=false;for(const auto &r:wire.requests){if(r.startsWith(QByteArray::fromHex("34004400020000")))appDownload=true;else if(appDownload)QVERIFY(r.isEmpty()||quint8(r[0])!=0x36);}QVERIFY(appDownload);return;}
        QVERIFY2(job.start(f.app,f.driver,error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,15000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QByteArray driver,descriptor,app;quint32 address=0;int maxApp=0,maxDescriptor=0;
        for(const auto &r:wire.requests){if(r.startsWith(QByteArray::fromHex("340044")))address=readBe(r.mid(3,4));
            else if(!r.isEmpty()&&quint8(r[0])==0x36){const auto payload=r.mid(2);
                if(address==0x20001400)driver+=payload;else if(address==0x20000){descriptor+=payload;maxDescriptor=qMax(maxDescriptor,payload.size());}
                else if(address==0x21000){app+=payload;QCOMPARE(payload.size(),1024);maxApp=qMax(maxApp,payload.size());}}}
        QCOMPARE(driver,f.driver.segments[0].data);QCOMPARE(descriptor,f.app.segments[0].data.left(4096));QCOMPARE(app,f.app.segments[0].data.mid(4096));
        QCOMPARE(maxApp,expected);QCOMPARE(maxDescriptor,1024);
    }
    void flashGeometry_data(){
        QTest::addColumn<int>("minor");QTest::addColumn<int>("limit");QTest::addColumn<int>("tail");
        for(int minor:{1,2,4})for(int limit:{1026,4095})
            QTest::newRow(qPrintable(QString("abi%1-limit%2").arg(minor).arg(limit)))<<minor<<limit<<1024;
        for(int tail:{8,16,592})QTest::newRow(qPrintable(QString("invalid-tail%1").arg(tail)))<<4<<1026<<tail;
        QTest::newRow("actual-section")<<4<<1026<<-1;
    }
    void flashGeometry(){
        QFETCH(int,minor);QFETCH(int,limit);QFETCH(int,tail);Fixture f;QString error;
        if(tail<0){const auto app=qEnvironmentVariable("T008_TEST_REAL_APP"),driver=qEnvironmentVariable("T008_TEST_DRIVER_BIN");
            if(app.isEmpty()||driver.isEmpty())QSKIP("Set actual App and Driver BIN paths");
            QVERIFY2(FirmwareImage::load(app,0x20000,f.app,error),qPrintable(error));QVERIFY2(FirmwareImage::load(driver,0x20001400,f.driver,error),qPrintable(error));
        }else{
            auto &data=f.app.segments[0].data;data.resize(4096+4096+tail);for(int i=4096+0x408;i<data.size();++i)data[i]=char((i*7)&255);
            put(data,8,4096+tail);f.app.size=data.size();
            if(minor==2){put(f.driver.segments[0].data,12,2,2);put(f.driver.segments[0].data,52,31);put(f.driver.segments[0].data,88,130);}
            if(minor==4){put(f.driver.segments[0].data,12,4,2);put(f.driver.segments[0].data,52,64);put(f.driver.segments[0].data,60,1024);put(f.driver.segments[0].data,88,130);}
        }
        const auto &bytes=f.app.segments[0].data;Peer peer;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        quint32 address=0,offset=0;QByteArray receivedDriver,receivedDescriptor,receivedCode;int badBlocks=0;
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){
            if(r.startsWith(QByteArray::fromHex("340044"))){address=readBe(r.mid(3,4));offset=0;return QByteArray::fromHex("74201000");}
            if(!r.isEmpty()&&quint8(r[0])==0x36){const auto payload=r.mid(2);const quint32 at=address+offset;offset+=payload.size();
                if(address==0x20001400)receivedDriver+=payload;
                else{const bool valid=payload.size()==1024&&!(at%1024)&&((at&4095)+quint32(payload.size())<=4096);
                    if(!valid){++badBlocks;return QByteArray::fromHex("7f3631");}
                    if(address==0x20000)receivedDescriptor+=payload;else if(address==0x21000)receivedCode+=payload;}
            }
            return peer.reply(r);
        };
        auto p=profile("app");p.target="generic";p.consecutiveFrameByteLimit=limit;p.negativeResponseChecks["app36"]=true;
        UdsSession session(wire,{100,100,2000,0});FlashJob job(session,p,std::make_unique<TestKey>());QSignalSpy done(&job,&FlashJob::finished);
        QVERIFY2(job.start(f.app,f.driver,error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);
        if(tail>=0&&tail%1024){QVERIFY(!done[0][0].toBool());QCOMPARE(badBlocks,1);QCOMPARE(receivedDriver,f.driver.segments[0].data);QCOMPARE(receivedDescriptor,bytes.left(4096));QCOMPARE(receivedCode,bytes.mid(4096,4096));return;}
        QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));QCOMPARE(badBlocks,0);
        QCOMPARE(receivedDriver,f.driver.segments[0].data);QCOMPARE(receivedDescriptor,bytes.left(4096));QCOMPARE(receivedCode,bytes.mid(4096));
    }
    void t008RejectsSmallNegotiatedAppBlock(){
        Fixture f;Peer peer;QString error;QVERIFY(t008::images(f.app,f.driver,peer.plan,error));
        MemoryTransport wire;wire.respond=[&](const QByteArray &r){return quint8(r[0])==0x34?QByteArray::fromHex("74200009"):peer.reply(r);};
        auto p=profile("app");p.target="generic";UdsSession session(wire,{100,100,2000,0});FlashJob job(session,p,std::make_unique<TestKey>());
        QSignalSpy done(&job,&FlashJob::finished);QVERIFY(job.start(f.app,f.driver,error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,15000);
        QVERIFY(!done[0][0].toBool());QVERIFY(done[0][1].toString().contains("1024 App bytes"));
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
        QTest::newRow("download maximum")<<QByteArray::fromHex("34")<<QByteArray::fromHex("74200401")<<false<<false;
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
        for(const auto request:{"1083","8582","288301"})for(const auto error:{"CAN N_As transmit confirmation timeout","CAN N_Ar timeout","CAN N_Cr timeout","UDS transmit timeout","UDS absolute response deadline","UDS P2/P2* timeout"})
            QTest::newRow(qPrintable(QString(request)+" "+error))<<QByteArray::fromHex(request)<<QString(error);
    }
    void suppressedTransportFailures(){
        QFETCH(QByteArray,request);QFETCH(QString,error);Fixture f;Peer peer;QString e;
        QVERIFY(t008::images(f.app,f.driver,peer.plan,e));MemoryTransport wire;
        wire.respond=[&](const QByteArray &r){return peer.reply(r);};wire.failRequest=request;wire.failMessage=error;
        UdsSession session(wire,{20,20,500,0});FlashJob job(session,profile(),std::make_unique<TestKey>());QSignalSpy finished(&job,&FlashJob::finished);
        QVERIFY(job.start(f.app,f.driver,e));QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,5000);
        QVERIFY(!finished[0][0].toBool());QVERIFY(finished[0][1].toString().contains("Transport failure: "+error));QCOMPARE(wire.requests.last(),request);
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
    void uiNoFlashDriverMasterSwitch_data(){
        QTest::addColumn<bool>("lin");QTest::addColumn<QString>("flow");
        for(const auto lin:{false,true})for(const auto flow:{"app","boot"})QTest::newRow(qPrintable(QString("%1-%2").arg(lin?"LIN":"CAN",flow)))<<lin<<QString(flow);
    }
    void uiNoFlashDriverMasterSwitch(){
        QFETCH(bool,lin);QFETCH(QString,flow);
        auto settings=host::ChannelSettings::defaults(lin?communication::Bus::Lin:communication::Bus::Can);
        settings.simulation=true;settings.flashRequired=false;
        auto configured=FlashProfile{};configured.flow=flow;
        for(const auto id:{"driver34","driver36","driver37"})configured.stepEnabled[id]=false;
        settings.downloadProfile=configured.toJson();
        host::ChannelViewModel vm(settings);host::ChannelPage page(&vm);page.show();QCoreApplication::processEvents();
        bool inspected=false,masterAbsent=false,stepsEditable=true,stepsUnchecked=true;
        QTimer::singleShot(0,&page,[&]{
            auto dialog=page.findChild<QDialog*>("downloadSettingsDialog");if(!dialog)return;
            masterAbsent=!dialog->findChild<QCheckBox*>("flashRequired");
            for(const auto id:{"enable_driver34","enable_driver36","enable_driver37"}){auto step=dialog->findChild<QCheckBox*>(id);stepsEditable&=step&&step->isEnabled();stepsUnchecked&=step&&!step->isChecked();}
            inspected=true;auto buttons=dialog->findChild<QDialogButtonBox*>();if(buttons)buttons->button(QDialogButtonBox::Ok)->click();if(dialog->isVisible())dialog->reject();
        });
        QTimer::singleShot(12000,&page,[&]{if(auto dialog=page.findChild<QDialog*>("downloadSettingsDialog"))dialog->reject();});
        auto button=page.findChild<QPushButton*>("downloadSettings");QVERIFY(button);button->click();
        QVERIFY(inspected);QVERIFY(masterAbsent);QVERIFY(stepsEditable);QVERIFY(stepsUnchecked);QVERIFY(!vm.settings().flashRequired);
        FlashProfile saved;QString error;QVERIFY2(vm.flashProfile(saved,error),qPrintable(error));
        for(const auto id:{"driver34","driver36","driver37"})QVERIFY(!saved.enabled(id));
    }
    void uiBootStartingFlowPersists(){
        auto settings=host::ChannelSettings::defaults(communication::Bus::Can);
        settings.simulation=true;
        auto configured=FlashProfile{};configured.flow="app";configured.stepEnabled["pre1003"]=false;
        settings.downloadProfile=configured.toJson();
        host::ChannelViewModel vm(settings);host::ChannelPage page(&vm);page.show();QCoreApplication::processEvents();
        bool selected=false,checked=false,saveRequested=false;
        QTimer::singleShot(0,&page,[&]{
            auto dialog=page.findChild<QDialog*>("downloadSettingsDialog");if(!dialog)return;
            auto flow=dialog->findChild<QComboBox*>("downloadFlow");
            auto pre1003=dialog->findChild<QCheckBox*>("enable_pre1003");
            auto buttons=dialog->findChild<QDialogButtonBox*>();
            if(!flow||!pre1003||!buttons){dialog->reject();return;}
            const int bootIndex=flow->findData("boot");
            if(bootIndex<0){dialog->reject();return;}
            flow->setCurrentIndex(bootIndex);selected=flow->currentData().toString()=="boot";
            checked=pre1003->isChecked();saveRequested=true;
            buttons->button(QDialogButtonBox::Ok)->click();
            if(dialog->isVisible())dialog->reject();
        });
        QTimer::singleShot(12000,&page,[&]{if(auto dialog=page.findChild<QDialog*>("downloadSettingsDialog"))dialog->reject();});
        auto button=page.findChild<QPushButton*>("downloadSettings");QVERIFY(button);button->click();
        QVERIFY(selected);QVERIFY(checked);QVERIFY(saveRequested);
        FlashProfile saved;QString error;QVERIFY2(vm.flashProfile(saved,error),qPrintable(error));
        QCOMPARE(saved.flow,QString("boot"));QVERIFY(saved.enabled("pre1003"));
        host::ChannelSettings restored;QVERIFY2(host::ChannelSettings::fromJson(vm.settings().toJson(),restored,error),qPrintable(error));
        FlashProfile reloaded;QVERIFY2(FlashProfile::fromJson(restored.downloadProfile,reloaded,error),qPrintable(error));
        QCOMPARE(reloaded.flow,QString("boot"));QVERIFY(reloaded.enabled("pre1003"));
    }
    void uiV2CancelSendsNothing(){
        Fixture f;QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto app=dir.filePath("app.bin"),driver=dir.filePath("driver.bin");
        QFile a(app),d(driver);QVERIFY(a.open(QIODevice::WriteOnly));QCOMPARE(a.write(f.app.segments[0].data),qint64(f.app.segments[0].data.size()));a.close();
        QVERIFY(d.open(QIODevice::WriteOnly));QCOMPARE(d.write(f.driver.segments[0].data),qint64(f.driver.segments[0].data.size()));d.close();
        auto settings=host::ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;settings.applicationPath=app;settings.flashPath=driver;
        settings.applicationAddress="00020000";settings.flashAddress="20001400";settings.flashRequired=true;settings.checkAppVersion=true;settings.expectedAppVersion="DIFFERENT";
        host::ChannelViewModel vm(settings);host::ChannelPage page(&vm);auto *model=vm.findChild<host::ChannelModel*>();QVERIFY(model);
        QSignalSpy frames(model,&host::ChannelModel::framesReceived),review(&vm,&host::ChannelViewModel::packageConfirmationRequested);
        QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());
        bool seen=false;QTimer closer;closer.setInterval(10);connect(&closer,&QTimer::timeout,&page,[&]{
            auto *box=page.findChild<QMessageBox*>("packageConfirmation");if(!box)return;
            const auto text=box->text();seen=text.contains("T008A0001")&&text.contains("DIFFERENT");box->done(QMessageBox::No);
        });closer.start();vm.start();QTRY_COMPARE_WITH_TIMEOUT(vm.taskState(),host::TaskState::Cancelled,5000);closer.stop();
        QVERIFY(seen);QCOMPARE(review.size(),1);QCOMPARE(frames.size(),0);
    }
    void uiV2SmallLimitFailsBeforeAppTransfer(){
        Fixture f;QTemporaryDir dir;QVERIFY(dir.isValid());const auto app=dir.filePath("app.bin"),driver=dir.filePath("driver.bin");
        QFile a(app),d(driver);QVERIFY(a.open(QIODevice::WriteOnly));QCOMPARE(a.write(f.app.segments[0].data),qint64(f.app.segments[0].data.size()));a.close();
        QVERIFY(d.open(QIODevice::WriteOnly));QCOMPARE(d.write(f.driver.segments[0].data),qint64(f.driver.segments[0].data.size()));d.close();
        auto settings=host::ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;settings.applicationPath=app;settings.flashPath=driver;
        settings.applicationAddress="00020000";settings.flashAddress="20001400";settings.flashRequired=true;settings.downloadProfile["consecutiveFrameByteLimit"]=9;
        host::ChannelViewModel vm(settings);host::ChannelPage page(&vm);auto *model=vm.findChild<host::ChannelModel*>();QVERIFY(model);
        QSignalSpy frames(model,&host::ChannelModel::framesReceived),review(&vm,&host::ChannelViewModel::packageConfirmationRequested);
        QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());vm.start();
        QTRY_COMPARE_WITH_TIMEOUT(vm.taskState(),host::TaskState::Failed,15000);QCOMPARE(review.size(),0);QVERIFY(frames.size()>0);QVERIFY(vm.logs().join('\n').contains("1024 App bytes"));
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
        settings.applicationAddress="00020000";settings.flashAddress="20001400";settings.flashRequired=true;settings.applicationNote="INTERNAL-STATUS-DO-NOT-SHOW";settings.checkAppVersion=true;settings.expectedAppVersion="DIFFERENT";
        host::ChannelViewModel vm(settings);host::ChannelPage page(&vm);auto *model=vm.findChild<host::ChannelModel*>();QVERIFY(model);
        QSignalSpy frames(model,&host::ChannelModel::framesReceived);QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());
        bool seen=false;QTimer closer;closer.setInterval(10);connect(&closer,&QTimer::timeout,&page,[&]{
            auto *box=page.findChild<QMessageBox*>("packageConfirmation");if(!box)return;const auto text=box->text();
            seen=text.contains("T008A0001")&&text.contains("DIFFERENT")&&!text.contains("INTERNAL-STATUS-DO-NOT-SHOW");
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
        settings.applicationAddress="00020000";settings.flashAddress="20001400";settings.flashRequired=true;settings.downloadProfile["consecutiveFrameByteLimit"]=1026;settings.checkAppVersion=true;settings.expectedAppVersion="DIFFERENT";
        host::ChannelViewModel vm(settings);host::ChannelPage page(&vm);auto *model=vm.findChild<host::ChannelModel*>();QVERIFY(model);
        QByteArray current,driverBytes,descriptorBytes,codeBytes;int pduLength=0,maxAppBlock=0;quint32 address=0;
        auto acceptPdu=[&](const QByteArray &pdu){
            if(pdu.size()==11&&quint8(pdu[0])==0x34)address=readBe(pdu.mid(3,4));
            else if(pdu.size()>=3&&quint8(pdu[0])==0x36){if(address==0x20001400)driverBytes+=pdu.mid(2);else if(address==0x20000)descriptorBytes+=pdu.mid(2);else if(address==0x21000){codeBytes+=pdu.mid(2);maxAppBlock=qMax(maxAppBlock,pdu.size()-2);}}
        };
        connect(model,&host::ChannelModel::framesReceived,&page,[&](const host::FrameBatch &frames){
            for(const auto &frame:frames){if(!frame.simulated||frame.direction!="Tx"||frame.payload.isEmpty())continue;
                const auto b=frame.payload;const int pci=quint8(b[0])>>4;
                if(pci==0){const int n=quint8(b[0])&15;if(n&&b.size()>=n+1)acceptPdu(b.mid(1,n));}
                else if(pci==1&&b.size()==8){pduLength=((quint8(b[0])&15)<<8)|quint8(b[1]);current=b.mid(2);}
                else if(pci==2&&!current.isEmpty()){current+=b.mid(1);if(current.size()>=pduLength){acceptPdu(current.left(pduLength));current.clear();}}
            }
        });
        QTRY_VERIFY(vm.canConnect());vm.toggleConnection();QTRY_VERIFY(vm.connected()&&!vm.pending());
        bool replaced=false;QTimer closer;closer.setInterval(10);connect(&closer,&QTimer::timeout,&page,[&]{
            auto *box=page.findChild<QMessageBox*>("packageConfirmation");if(!box)return;
            QFile changed(app),changedDriver(driver);if(changed.open(QIODevice::WriteOnly|QIODevice::Truncate)&&changedDriver.open(QIODevice::WriteOnly|QIODevice::Truncate)){
                replaced=changed.write(QByteArray(32,'X'))==32&&changedDriver.write(QByteArray(32,'X'))==32;changed.close();changedDriver.close();}
            box->done(QMessageBox::Yes);
        });closer.start();vm.start();QTRY_VERIFY_WITH_TIMEOUT(replaced,5000);closer.stop();
        QTRY_VERIFY_WITH_TIMEOUT(vm.logs().join('\n').contains(digest,Qt::CaseInsensitive),5000);
        const int timeout=realApp.isEmpty()?40000:600000;
        QTRY_COMPARE_WITH_TIMEOUT(descriptorBytes.size(),4096,timeout);QTRY_COMPARE_WITH_TIMEOUT(codeBytes.size(),f.app.segments[0].data.size()-4096,timeout);
        // The T008 simulation advertises 1026 bytes including SID and BSC.
        QCOMPARE(driverBytes,f.driver.segments[0].data);QCOMPARE(descriptorBytes,f.app.segments[0].data.left(4096));QCOMPARE(codeBytes,f.app.segments[0].data.mid(4096));QCOMPARE(maxAppBlock,1024);vm.cancel();
    }
};
#ifdef T008_HEADLESS
QTEST_GUILESS_MAIN(T008TargetTest)
#else
QTEST_MAIN(T008TargetTest)
#endif
#include "test_t008_target.moc"
