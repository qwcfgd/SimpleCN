#include <QtTest>
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QMap>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QScrollArea>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include "protocol/FlashJob.h"
#include "protocol/SimulatedUdsEcu.h"
#include "domain/HostTypes.h"
#include "views/ChannelPage.h"
#include "infrastructure/ChannelWorker.h"
#include "TestEventWait.h"
#include "RomMarkerFixture.h"
using namespace boot;
class SlotWire final:public DiagnosticTransport {
public:
    QList<QByteArray> requests;SimulatedUdsEcu ecu;std::function<QByteArray(const QByteArray &)> intercept;quint64 epoch=0;
    explicit SlotWire(FlashProfile p={}):ecu(std::move(p)){}
    bool send(const QByteArray &r,QString &)override{requests+=r;const auto e=epoch;QTimer::singleShot(0,this,[this,r,e]{if(e!=epoch)return;emit sent();auto reply=intercept?intercept(r):QByteArray();if(!reply.isEmpty()){emit received(reply);return;}for(auto p:ecu.handle(r)){if(p.startsWith(char(0x50)))p=p.left(2)+QByteArray::fromHex("00140002");emit received(p);}});return true;}
    void cancel()override{++epoch;}int maximumPdu()const override{return 4095;}
};
class DualSlotTest final:public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    QString file(const QString &name,const QByteArray &data){const auto path=temp.filePath(name);QFile f(path);if(!f.open(QIODevice::WriteOnly))return {};f.write(data);return path;}
    static QByteArray linked(quint32 base,int bytes,char fill){
        QByteArray data(bytes,fill);for(int i=0;i<64;++i)data[i]=0;
        auto word=[&](int at,quint32 n){for(int i=0;i<4;++i)data[at+i]=char(n>>(8*i));};
        word(0,0x20001000);word(4,base+0x101);word(8,base+0x105);word(12,base+0x109);return data;
    }
    static FirmwareImage bin(quint32 a,char first,bool dual){FirmwareImage i;i.binary=true;const quint32 b=a==0x5000?0x10000:0x30000;i.segments={{a,linked(a,4096,first)+(dual?linked(b,4096,char(first+1)):QByteArray())}};return i;}
    static FlashProfile profile(){FlashProfile p;p.resetWaitMs=0;p.bootEraseBase=0x5000;p.bootBBase=0x10000;p.bootEraseLength=4096;p.applicationEraseBase=0x20000;p.applicationBBase=0x30000;p.applicationEraseLength=4096;p.applicationHasB=true;return p;}
    static quint32 address(const QByteArray &r){return readBe(r.mid(5,4));}
    static FirmwareImage t010Driver(int minor){
        QByteArray d(2124,char(0x55));d.replace(0,128,QByteArray(128,0));d.replace(0,8,"T008DRV1");
        auto word=[&](int at,quint32 n,int count=4){for(int i=0;i<count;++i)d[at+i]=char(n>>(8*i));};
        word(8,128,2);word(10,1,2);word(12,quint32(minor),2);word(16,d.size());word(20,0x20001400);word(24,d.size());word(28,256);
        for(int i=0;i<5;++i)word(32+i*4,128+2*i);
        word(52,192);word(56,4096);word(60,1024);word(68,20000000);d.replace(72,16,QByteArray(16,'D'));word(88,138);
        FirmwareImage image;image.segments={{0x20001400,d}};return image;
    }
    static QByteArray t010App(quint32 base,int slot){
        QByteArray d(8192,char(0x5a));d.replace(0,4096,QByteArray(4096,char(0xff)));d.replace(0,8,"T010APP1");
        marker_fixture::put32(d,8,4096);d[87]=char(slot);marker_fixture::put32(d,4096,0x20001000);
        for(int i=1;i<16;++i)marker_fixture::put32(d,4096+i*4,base+0x1401);
        return d;
    }
    static QByteArray intelHex(const QVector<Segment> &parts){
        QByteArray result;
        auto record=[&](int type,int offset,const QByteArray &bytes){
            QByteArray body;body.append(char(bytes.size()));body.append(char(offset>>8));body.append(char(offset));body.append(char(type));body+=bytes;
            int sum=0;for(unsigned char b:body)sum+=b;body.append(char(-sum));result+=':'+body.toHex().toUpper()+'\n';
        };
        for(const auto &part:parts){quint32 upper=0xffffffff;
            for(int at=0;at<part.data.size();at+=16){const auto address=part.address+quint32(at);
                if((address>>16)!=upper){upper=address>>16;QByteArray high;high.append(char(upper>>8));high.append(char(upper));record(4,0,high);}
                record(0,int(address&0xffff),part.data.mid(at,16));
            }
        }
        record(1,0,{});return result;
    }
private slots:
    void fresh80DefaultsAndLegacyJson(){
        const auto fresh=host::ChannelSettings::defaults(communication::Bus::Can);
        QCOMPARE(fresh.flashAddress,QString("20001400"));QCOMPARE(fresh.applicationAddress,QString("0001A000"));QCOMPARE(fresh.applicationLength,QString("0004D000"));
        QCOMPARE(fresh.bootAddress,QString("00005000"));QCOMPARE(fresh.bootBAddress,QString("00068000"));QCOMPARE(fresh.bootEraseLength,QString("00014000"));QVERIFY(fresh.applicationBAddress.isEmpty());
        QString error;host::ChannelSettings old;
        QVERIFY(host::ChannelSettings::fromJson({{"bus","CAN"}},old,error));
        QCOMPARE(old.applicationAddress,QString("00020000"));QCOMPARE(old.applicationLength,QString("00057000"));QCOMPARE(old.bootBAddress,QString("0006D000"));QCOMPARE(old.bootEraseLength,QString("0000F000"));
        auto saved=fresh.toJson();saved["applicationAddress"]="00015000";saved["applicationLength"]="00033000";saved["applicationBAddress"]="00049000";saved["bootEraseLength"]="0000F000";saved["bootBAddress"]="0006D000";
        host::ChannelSettings restored;QVERIFY(host::ChannelSettings::fromJson(saved,restored,error));QCOMPARE(restored.toJson(),saved);
    }
    void boot60And80Files_data(){QTest::addColumn<bool>("new80");QTest::newRow("legacy60")<<false;QTest::newRow("unified80")<<true;}
    void boot60And80Files(){
        QFETCH(bool,new80);const quint32 b=new80?0x68000:0x6d000;const int size=new80?0x14000:0xf000;
        const auto a=marker_fixture::boot(0x5000,"AST1",size),other=marker_fixture::boot(b,"AST1",size);
        QString error;FirmwareImage image;QVector<Segment> primary,alternate;
        const auto pairPath=file(new80?"boot80.bin":"boot60.bin",a+other);
        QVERIFY2(FirmwareImage::loadTarget(pairPath,0x5000,b,true,quint32(size),image,error),qPrintable(error));QCOMPARE(FirmwareImage::identify(image),FirmwareImage::Kind::BootBundle);
        QVERIFY(FirmwareImage::loadBootBundle(pairPath,image,error));QVERIFY(FirmwareImage::planSlots(image,0x5000,b,true,quint32(size),primary,alternate,error));QCOMPARE(primary[0].data,a);QCOMPARE(alternate[0].data,other);
        const auto hexPath=file(new80?"boot80.hex":"boot60.hex",intelHex({{0x5000,a},{b,other}}));
        QVERIFY(FirmwareImage::loadBootBundle(hexPath,image,error));QCOMPARE(image.segments[1].address,b);
        auto bad=intelHex({{0x5000,a},{b,other}});bad[bad.indexOf('\n')-1]=bad[bad.indexOf('\n')-1]=='0'?'1':'0';QVERIFY(!FirmwareImage::load(file("checksum.hex",bad),0x5000,image,error));
        auto damaged=a;damaged[100]=char(damaged[100]^1);FirmwareImage corrupt;corrupt.segments={{0x5000,damaged}};QCOMPARE(FirmwareImage::identify(corrupt),FirmwareImage::Kind::Unknown);
        FirmwareImage single;single.binary=true;single.segments={{0x5000,other}};QVERIFY(FirmwareImage::planSlots(single,0x5000,b,true,quint32(size),primary,alternate,error));QCOMPARE(primary[0].address,b);QVERIFY(alternate.isEmpty());
        RomMarker marker;QVERIFY(FirmwareImage::validateMarker({b,other},ROM_MARKER_BOOT,marker,error));
    }
    void freshFileEntryAdaptsOnlyUntouchedDefaults(){
        const auto appPath=file("fresh-t010.bin",t010App(0x1a000,0));
        const auto driverPath=file("fresh-driver.bin",t010Driver(11).segments[0].data);
        const auto oldDriver=file("old-driver.bin",t010Driver(9).segments[0].data);
        auto fresh=host::ChannelSettings::defaults(communication::Bus::Can);fresh.simulation=true;
        for(bool driverFirst:{false,true}){
            host::ChannelViewModel vm(fresh);const auto before=vm.settings().downloadProfile;
            QVERIFY(vm.chooseImage(driverFirst,driverFirst?driverPath:appPath));QCOMPARE(vm.settings().applicationLength,QString("0004D000"));
            QVERIFY(vm.chooseImage(!driverFirst,driverFirst?appPath:driverPath));
            QCOMPARE(vm.settings().applicationAddress,QString("0001A000"));QCOMPARE(vm.settings().applicationLength,QString("00030000"));QCOMPARE(vm.settings().applicationBAddress,QString("0004B000"));QVERIFY(vm.settings().bootBAddress.isEmpty());
            auto expected=before;expected["target"]="s32k144-t010-v2";QCOMPARE(vm.settings().downloadProfile,expected);
        }
        QString error;host::ChannelSettings restored;QVERIFY(host::ChannelSettings::fromJson(fresh.toJson(),restored,error));
        for(int mode=0;mode<4;++mode){
            auto settings=mode==0?restored:fresh;if(mode==1)settings.applicationLength="00033000";
            host::ChannelViewModel vm(settings);
            if(mode==2){auto edit=vm.settings();edit.applicationLength="00033000";QVERIFY(vm.setSettings(edit));edit=vm.settings();edit.applicationLength="0004D000";QVERIFY(vm.setSettings(edit));}
            QVERIFY(vm.chooseImage(true,mode==3?oldDriver:driverPath));QVERIFY(vm.chooseImage(false,appPath));
            QCOMPARE(vm.settings().applicationLength,mode==1?QString("00033000"):QString("0004D000"));QCOMPARE(vm.settings().bootBAddress,QString("00068000"));
        }
        host::ChannelViewModel table(fresh);QJsonArray files{QJsonObject{{"role","driver"},{"path",driverPath}},QJsonObject{{"role","app"},{"path",appPath}}};
        QVERIFY(table.updateDownloadFiles(files,error));QCOMPARE(table.settings().applicationLength,QString("00030000"));QVERIFY(table.settings().bootBAddress.isEmpty());
        auto wrong=t010App(0x15000,0);host::ChannelViewModel bad(fresh);QVERIFY(bad.chooseImage(true,driverPath));QVERIFY(bad.chooseImage(false,file("wrong-vector.bin",wrong)));QCOMPARE(bad.settings().applicationLength,QString("0004D000"));
    }
    void explicitTargetsRemainAuthoritative_data(){
        QTest::addColumn<QString>("target");QTest::addColumn<int>("entry");QTest::addColumn<int>("origin");
        for(const auto &target:{QString("t008-bench-v1"),QString("s32k144-t008-t009-v2")})for(int entry=0;entry<3;++entry)for(int origin=0;origin<3;++origin)
            QTest::newRow(qPrintable(QString("%1-entry%2-origin%3").arg(target).arg(entry).arg(origin)))<<target<<entry<<origin;
    }
    void explicitTargetsRemainAuthoritative(){
        QFETCH(QString,target);QFETCH(int,entry);QFETCH(int,origin);
        const auto appPath=file("explicit-target-app.bin",t010App(0x1a000,0)),driverPath=file("explicit-target-driver.bin",t010Driver(11).segments[0].data);
        auto settings=host::ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;FlashProfile profile;QString error;
        QVERIFY(FlashProfile::fromJson(settings.downloadProfile,profile,error));profile.target=target;profile.name="explicit settings";
        if(origin==2)settings.downloadProfile=profile.toJson();
        host::ChannelViewModel vm(settings);
        if(origin!=2){QVERIFY(vm.updateDownloadSettings(vm.settings(),profile,error));QVERIFY(!vm.settings().defaultMemoryLayout);}
        if(origin==1){profile.target="generic";QVERIFY(vm.updateDownloadSettings(vm.settings(),profile,error));QVERIFY(!vm.settings().defaultMemoryLayout);}
        auto expected=vm.settings().toJson();
        if(entry==2){QJsonArray files{QJsonObject{{"role","driver"},{"path",driverPath}},QJsonObject{{"role","app"},{"path",appPath}}};QVERIFY(vm.updateDownloadFiles(files,error));}
        else{const bool driverFirst=entry==0;QVERIFY(vm.chooseImage(driverFirst,driverFirst?driverPath:appPath));QVERIFY(vm.chooseImage(!driverFirst,driverFirst?appPath:driverPath));}
        auto actual=vm.settings().toJson();for(const auto *key:{"flashPath","applicationPath","downloadFiles"}){expected.remove(key);actual.remove(key);}QCOMPARE(actual,expected);
        QCOMPARE(vm.settings().downloadProfile.value("target").toString(),origin==1?QString("generic"):target);
        if(origin!=2)QVERIFY(!vm.settings().defaultMemoryLayout);
        QVERIFY(vm.flashProfile(profile,error));profile.applicationEraseBase=vm.settings().applicationAddress.toUInt(nullptr,16);profile.applicationEraseLength=vm.settings().applicationLength.toUInt(nullptr,16);profile.applicationBBase=vm.settings().applicationBAddress.toUInt(nullptr,16);profile.applicationHasB=!vm.settings().applicationBAddress.isEmpty();
        FirmwareImage app,driver;QVERIFY(FirmwareImage::load(appPath,profile.applicationEraseBase,app,error));QVERIFY(FirmwareImage::load(driverPath,0x20001400,driver,error));
        SlotWire wire(profile);UdsSession session(wire,{});FlashJob job(session,profile,std::make_unique<SimulationKey>());QVERIFY(!job.start(app,driver,error));QVERIFY(wire.requests.isEmpty());
        if(origin!=1)QVERIFY(error.contains("conflicts"));
    }
    void singleApp60And80Transfer_data(){QTest::addColumn<bool>("new80");QTest::newRow("legacy60")<<false;QTest::newRow("unified80")<<true;}
    void singleApp60And80Transfer(){
        QFETCH(bool,new80);const quint32 base=new80?0x1a000:0x15000,capacity=new80?0x4d000:0x57000;
        auto bytes=marker_fixture::app(8192);for(int i=1;i<16;++i)marker_fixture::put32(bytes,4096+i*4,base+0x1401);marker_fixture::seal(bytes,ROM_MARKER_APP);
        FirmwareImage image;QString error;QVERIFY(FirmwareImage::loadTarget(file("single-app.bin",bytes+QByteArray(int(capacity)-bytes.size(),char(0xff))),base,0,false,capacity,image,error));
        t008::Plan plan;QVERIFY2(t008::completeApplication(image,base,capacity,false,0,plan,error,true),qPrintable(error));QCOMPARE(plan.application[0].data,bytes);
        FirmwareImage hex;QVERIFY(FirmwareImage::load(file("single-app.hex",intelHex({{base,bytes}})),base,hex,error));QVERIFY(t008::completeApplication(hex,base,capacity,false,0,plan,error));QCOMPARE(plan.application[0].data,bytes);
        auto bad=image;bad.segments[0].data[bad.segments[0].data.size()-1]=0;QVERIFY(!t008::completeApplication(bad,base,capacity,false,0,plan,error,true));
    }
    void t010LayoutTransfer_data(){QTest::addColumn<bool>("new80");QTest::addColumn<bool>("useB");QTest::addColumn<bool>("generic");for(bool n:{false,true})for(bool b:{false,true})for(bool g:{false,true})QTest::newRow(qPrintable(QString("%1-slot%2-%3").arg(n?192:60).arg(b?'B':'A').arg(g?"generic":"T010")))<<n<<b<<g;}
    void t010LayoutTransfer(){
        QFETCH(bool,new80);QFETCH(bool,useB);QFETCH(bool,generic);const quint32 a=new80?0x1a000:0x15000,b=new80?0x4b000:0x49000,capacity=new80?0x30000:0x33000;
        const auto payloadA=t010App(a,0),payloadB=t010App(b,1);const auto bytes=payloadA+QByteArray(int(capacity)-payloadA.size(),char(0xff))+payloadB+QByteArray(int(capacity)-payloadB.size(),char(0xff));
        FlashProfile p;p.target="s32k144-t010-v2";p.flow="app";p.resetWaitMs=0;p.applicationEraseBase=a;p.applicationBBase=b;p.applicationHasB=true;p.applicationEraseLength=capacity;p.bootEraseLength=new80?0x14000:0xf000;p.bootBBase=new80?0x68000:0x6d000;
        if(generic){auto json=host::ChannelSettings::defaults(communication::Bus::Can).toJson();json["applicationAddress"]=QString::number(a,16);json["applicationLength"]=QString::number(capacity,16);json["applicationBAddress"]=QString::number(b,16);host::ChannelSettings restored;QString error;QVERIFY(host::ChannelSettings::fromJson(json,restored,error));p.applicationEraseBase=restored.applicationAddress.toUInt(nullptr,16);p.applicationEraseLength=restored.applicationLength.toUInt(nullptr,16);p.applicationBBase=restored.applicationBAddress.toUInt(nullptr,16);p.target="generic";}
        FirmwareImage image;QString error;QVERIFY2(FirmwareImage::loadTarget(file("app-pair.bin",bytes),a,b,true,capacity,image,error),qPrintable(error));
        SlotWire wire(p);wire.ecu.maxBlockLength=1026;
        wire.intercept=[=](const QByteArray &r){return useB&&r==QByteArray::fromHex("3101ff0044")+be32(a)+be32(capacity)?QByteArray::fromHex("7f3131"):QByteArray();};
        UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);
        QVERIFY2(job.start(image,t010Driver(new80?11:9),error),qPrintable(error));QVERIFY(waitForTest([&]{return done.size()==1;},3000));QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        const auto selected=useB?b:a;QVERIFY(wire.requests.contains(QByteArray::fromHex("3101ff0044")+be32(selected)+be32(capacity)));QVERIFY(wire.requests.contains(QByteArray::fromHex("340044")+be32(selected)+be32(8192)));
        QCOMPARE(wire.ecu.memory(selected),useB?payloadB:payloadA);QCOMPARE(image.segments[0].data,bytes);
        QVector<Segment> primary,alternate;FirmwareImage single;single.binary=true;single.segments={{a,payloadB}};QVERIFY(FirmwareImage::planSlots(single,a,b,true,capacity,primary,alternate,error));QCOMPARE(primary[0].address,b);
        FirmwareImage hex;QVERIFY(FirmwareImage::load(file("app-pair.hex",intelHex({{a,payloadA},{b,payloadB}})),a,hex,error));QVERIFY(FirmwareImage::planSlots(hex,a,b,true,capacity,primary,alternate,error));QCOMPARE(primary[0].address,a);QCOMPARE(alternate[0].address,b);
    }
    void markedPairedSlotsRequireOriginalPrefixCrcAndVectors(){
        auto a=t010App(0x1a000,0),b=t010App(0x4b000,1);
        marker_fixture::seal(a,ROM_MARKER_APP);marker_fixture::seal(b,ROM_MARKER_APP);
        const auto pair=a+QByteArray(0x30000-a.size(),char(0xff))+b+QByteArray(0x30000-b.size(),char(0xff));
        FirmwareImage image;QString error;
        const auto path=file("marked-pair.bin",pair);
        QVERIFY2(FirmwareImage::loadTarget(path,0x1a000,0x4b000,true,0x30000,image,error),qPrintable(error));
        QCOMPARE(FirmwareImage::identify(image),FirmwareImage::Kind::Application);
        auto settings=host::ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;
        host::ChannelViewModel vm(settings);QString role;
        QVERIFY2(vm.inspectDownloadFile(path,role,error),qPrintable(error));QCOMPARE(role,QString("app"));
        for(int mode=0;mode<4;++mode){auto bad=pair;
            if(mode==0)bad[5000]=char(quint8(bad[5000])^1);
            if(mode==1)bad[0x3000]=0;
            if(mode==2){marker_fixture::put32(bad,4100,0x1a000+0x3001);auto prefix=bad.left(a.size());marker_fixture::seal(prefix,ROM_MARKER_APP);bad.replace(0,prefix.size(),prefix);}
            if(mode==3){auto slot=bad.left(0x30000);marker_fixture::seal(slot,ROM_MARKER_APP);bad.replace(0,slot.size(),slot);}
            QVERIFY(!FirmwareImage::loadTarget(file(QString("bad-marked-pair-%1.bin").arg(mode),bad),0x1a000,0x4b000,true,0x30000,image,error));
        }
    }
    void freshMarkedFilesAdaptCanAndLinDefaults(){
        auto app=t010App(0x1a000,0);marker_fixture::seal(app,ROM_MARKER_APP);
        const auto appPath=file("fresh-marked-t010.bin",app);
        const auto driverPath=file("fresh-marked-driver.bin",marker_fixture::driver());
        for(auto bus:{communication::Bus::Can,communication::Bus::Lin})for(bool driverFirst:{false,true}){
            auto settings=host::ChannelSettings::defaults(bus);settings.simulation=true;
            host::ChannelViewModel vm(settings);
            QVERIFY(vm.chooseImage(driverFirst,driverFirst?driverPath:appPath));
            QVERIFY(vm.chooseImage(!driverFirst,driverFirst?appPath:driverPath));
            QCOMPARE(vm.settings().applicationLength,QString("00030000"));
            QCOMPARE(vm.settings().applicationBAddress,QString("0004B000"));
            QVERIFY(vm.settings().bootBAddress.isEmpty());
            QCOMPARE(vm.settings().downloadProfile.value("target").toString(),QString("s32k144-t010-v2"));
        }
    }
    void actualHost192FirmwareArtifacts(){
        const auto path=qEnvironmentVariable("HOST80_T010_ARTIFACTS");if(path.isEmpty())QSKIP("Set the final 192 KiB nine-target artifact manifest for read-only file validation");
        QFile input(path);QVERIFY2(input.open(QIODevice::ReadOnly),qPrintable(path));QJsonParseError parse;const auto document=QJsonDocument::fromJson(input.readAll(),&parse);QCOMPARE(parse.error,QJsonParseError::NoError);QVERIFY(document.isObject());
        const auto manifest=document.object(),driverInfo=manifest.value("driver").toObject();const auto records=manifest.value("images").toArray();QCOMPARE(records.size(),9);
        const auto fileHash=[](const QString &name){QFile f(name);return f.open(QIODevice::ReadOnly)?QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex():QByteArray();};
        QCOMPARE(fileHash(driverInfo.value("path").toString()),driverInfo.value("sha256").toString().toLatin1());
        FirmwareImage driver;QString error;QVERIFY2(FirmwareImage::load(driverInfo.value("path").toString(),0x20001400,driver,error),qPrintable(error));QVERIFY2(t008::validateCurrentDriver(driver,true,error,11),qPrintable(error));QVERIFY(!t008::validateCurrentDriver(driver,true,error,9));QVERIFY(!t008::validateCurrentDriver(driver,true,error,10));
        FirmwareImage driverHex;QCOMPARE(fileHash(driverInfo.value("hex").toString()),driverInfo.value("hex_sha256").toString().toLatin1());
        QVERIFY2(FirmwareImage::load(driverInfo.value("hex").toString(),0x20001400,driverHex,error),qPrintable(error));QCOMPARE(driverHex.segments.size(),1);QCOMPARE(driverHex.segments[0].address,driver.segments[0].address);QCOMPARE(driverHex.segments[0].data,driver.segments[0].data);QVERIFY(t008::validateCurrentDriver(driverHex,true,error,11));
        QMap<QString,QByteArray> verifiedApps;QStringList seen;int bootCount=0,appCount=0;
        for(const auto &value:records){const auto record=value.toObject();const auto role=record.value("role").toString(),profile=record.value("profile").toString();const auto key=profile+":"+role;QVERIFY(!seen.contains(key));seen+=key;
            const auto binPath=record.value("bin").toString(),hexPath=record.value("hex").toString();QCOMPARE(fileHash(binPath),record.value("bin_sha256").toString().toLatin1());QCOMPARE(fileHash(hexPath),record.value("hex_sha256").toString().toLatin1());
            const quint32 base=record.value("base").toString().toUInt(nullptr,16),capacity=record.value("capacity").toString().toUInt(nullptr,16);FirmwareImage bin,hex;
            QVERIFY2(FirmwareImage::load(binPath,base,bin,error),qPrintable(error));QVERIFY2(FirmwareImage::load(hexPath,base,hex,error),qPrintable(error));QCOMPARE(bin.segments.size(),1);
            if(role=="boot"){
                ++bootCount;QCOMPARE(base,quint32(0x5000));QCOMPARE(capacity,quint32(0x14000));QCOMPARE(bin.segments[0].data.size(),int(capacity));QCOMPARE(FirmwareImage::identify(bin),FirmwareImage::Kind::IncompleteBoot);
                for(const auto &segment:hex.segments){QVERIFY(segment.address>=base);QVERIFY(quint64(segment.address)+quint64(segment.data.size())<=quint64(base)+capacity);QCOMPARE(segment.data,bin.segments[0].data.mid(int(segment.address-base),segment.data.size()));}
                FlashProfile p;p.target="s32k144-t010-v2";p.flow="boot";p.applicationEraseBase=0x1a000;p.applicationEraseLength=0x30000;SlotWire wire(p);UdsSession session(wire,{});FlashJob job(session,p,std::make_unique<SimulationKey>());QVERIFY(!job.start({},driver,bin,error));QVERIFY(wire.requests.isEmpty());
            }else{
                ++appCount;QVERIFY(role=="app-a"||role=="app-b");QCOMPARE(base,role=="app-a"?quint32(0x1a000):quint32(0x4b000));QCOMPARE(capacity,quint32(0x30000));
                QVector<Segment> binaryParts,binaryOther,hexParts,hexOther;QVERIFY2(FirmwareImage::planSlots(bin,0x1a000,0x4b000,true,capacity,binaryParts,binaryOther,error),qPrintable(error));QVERIFY(binaryOther.isEmpty());QCOMPARE(binaryParts.first().address,base);
                QVERIFY2(FirmwareImage::planSlots(hex,0x1a000,0x4b000,true,capacity,hexParts,hexOther,error),qPrintable(error));QVERIFY(hexOther.isEmpty());QCOMPARE(hexParts.first().address,base);
                FirmwareImage binarySelected,hexSelected;binarySelected.segments=binaryParts;hexSelected.segments=hexParts;t008::Plan bp,hp;
                QVERIFY2(t008::completeApplication(binarySelected,base,capacity,true,base==0x4b000?1:0,bp,error,true),qPrintable(error));QVERIFY2(t008::completeApplication(hexSelected,base,capacity,true,base==0x4b000?1:0,hp,error),qPrintable(error));
                QCOMPARE(bp.application.size(),1);QCOMPARE(hp.application.size(),1);QCOMPARE(bp.application[0].data,hp.application[0].data);QCOMPARE(crc32(bp.application[0].data),crc32(hp.application[0].data));verifiedApps.insert(key,bp.application[0].data);
            }
            qInfo().noquote()<<profile<<role<<QString::number(base,16)<<"file preflight PASS";
        }
        QCOMPARE(bootCount,3);QCOMPARE(appCount,6);
        const auto pairs=manifest.value("pairs").toArray();QCOMPARE(pairs.size(),3);QStringList pairProfiles;
        for(const auto &value:pairs){const auto pair=value.toObject();const auto profile=pair.value("profile").toString(),path=pair.value("file").toString();QVERIFY(!pairProfiles.contains(profile));pairProfiles+=profile;
            QCOMPARE(fileHash(path),pair.value("sha256").toString().toLatin1());FirmwareImage paired;QVERIFY2(FirmwareImage::loadTarget(path,0x1a000,0x4b000,true,0x30000,paired,error),qPrintable(error));QCOMPARE(FirmwareImage::identify(paired),FirmwareImage::Kind::Application);QCOMPARE(paired.segments.size(),1);QCOMPARE(paired.segments[0].data.size(),0x60000);
            QVector<Segment> primary,alternate;QVERIFY2(FirmwareImage::planSlots(paired,0x1a000,0x4b000,true,0x30000,primary,alternate,error),qPrintable(error));QCOMPARE(primary.size(),1);QCOMPARE(alternate.size(),1);QCOMPARE(primary[0].address,quint32(0x1a000));QCOMPARE(alternate[0].address,quint32(0x4b000));
            FirmwareImage a,b;a.segments=primary;b.segments=alternate;t008::Plan ap,bp;QVERIFY2(t008::completeApplication(a,0x1a000,0x30000,true,0,ap,error,true),qPrintable(error));QVERIFY2(t008::completeApplication(b,0x4b000,0x30000,true,1,bp,error,true),qPrintable(error));
            QVERIFY(verifiedApps.contains(profile+":app-a"));QVERIFY(verifiedApps.contains(profile+":app-b"));QCOMPARE(ap.application[0].data,verifiedApps[profile+":app-a"]);QCOMPARE(bp.application[0].data,verifiedApps[profile+":app-b"]);
            qInfo().noquote()<<profile<<"paired slots preflight PASS";
        }
    }
    void t010MspMatchesStartupWindow(){
        for(quint32 stack:{0x1fff9000u,0x1fff9008u,0x20001400u,0x20001408u,0x200013fcu}){
            const bool valid=stack==0x1fff9008u||stack==0x20001400u;auto data=t010App(0x1a000,0);marker_fixture::put32(data,4096,stack);
            FirmwareImage app;app.binary=true;app.segments={{0x1a000,data}};t008::Plan plan;QString error;
            QCOMPARE(t008::completeApplication(app,0x1a000,0x30000,true,0,plan,error,true),valid);
            QVector<Segment> primary,alternate;QCOMPARE(FirmwareImage::planSlots(app,0x1a000,0x4b000,true,0x30000,primary,alternate,error),valid);
        }
    }
    void explicitBinMarkerTailContract(){
        auto prefix=marker_fixture::app(8192);auto container=prefix+QByteArray(0x57000-prefix.size(),char(0xff));QString error;
        const auto check=[&](const QByteArray &data,bool bin,quint32 base=0x15000){return FirmwareImage::validateMarkerSet({qMakePair(Segment{base,data},quint32(ROM_MARKER_APP))},error,bin);};
        QVERIFY(check(container,true));QVERIFY(!check(container,false));
        auto bad=container;bad[5000]=char(quint8(bad[5000])^1);QVERIFY(!check(bad,true));
        bad=container;bad[bad.size()-1]=0;QVERIFY(!check(bad,true));QVERIFY(!check(container,true,0x7d000));
        marker_fixture::seal(container,ROM_MARKER_APP);QVERIFY(check(container,true));QVERIFY(check(container,false));
    }
    void markedBinTailPreservesDeclaredMarker(){
        const quint32 base=0x15000,capacity=0x57000;const auto prefix=marker_fixture::app(8192);const auto container=prefix+QByteArray(int(capacity)-prefix.size(),char(0xff));QString error;RomMarker marker;
        FirmwareImage app;app.binary=true;app.segments={{base,container}};t008::Plan plan;
        QVERIFY2(t008::completeApplication(app,base,capacity,false,0,plan,error,true),qPrintable(error));QCOMPARE(plan.application.size(),1);QCOMPARE(plan.application[0].data,prefix);QVERIFY(FirmwareImage::validateMarker(plan.application[0],ROM_MARKER_APP,marker,error));QCOMPARE(app.segments[0].data,container);
        auto bad=app;bad.segments[0].data[5000]=char(quint8(bad.segments[0].data[5000])^1);QVERIFY(!t008::completeApplication(bad,base,capacity,false,0,plan,error,true));
        bad=app;marker_fixture::seal(bad.segments[0].data,ROM_MARKER_APP);QVERIFY(FirmwareImage::validateMarker(bad.segments[0],ROM_MARKER_APP,marker,error));QVERIFY(t008::completeApplication(bad,base,capacity,false,0,plan,error,true));QCOMPARE(plan.application[0].data,bad.segments[0].data);
        bad.segments[0].data[5000]=char(quint8(bad.segments[0].data[5000])^1);
        FlashProfile profile;profile.target="s32k144-t008-t009-v2";profile.flow="app";profile.applicationEraseBase=base;profile.applicationEraseLength=capacity;FirmwareImage driver;driver.segments={{0x20001400,marker_fixture::driver()}};
        SlotWire wire(profile);UdsSession session(wire,{});FlashJob job(session,profile,std::make_unique<SimulationKey>());QVERIFY(!job.start(bad,driver,error));QVERIFY(wire.requests.isEmpty());
        const auto canonical=marker_fixture::app(int(capacity));
        for(bool bin:{false,true}){FirmwareImage full;full.binary=bin;full.segments={{base,canonical}};QVERIFY2(t008::completeApplication(full,base,capacity,false,0,plan,error,bin),qPrintable(error));QCOMPARE(plan.application[0].data,canonical);QVERIFY(FirmwareImage::validateMarker(plan.application[0],ROM_MARKER_APP,marker,error));}
    }
    void markedBinPairUsesHalfFileLength_data(){QTest::addColumn<bool>("padded");QTest::newRow("compact")<<false;QTest::newRow("slot-padded")<<true;}
    void markedBinPairUsesHalfFileLength(){
        QFETCH(bool,padded);auto a=t010App(0x1a000,0),b=t010App(0x4b000,1);marker_fixture::seal(a,ROM_MARKER_APP);marker_fixture::seal(b,ROM_MARKER_APP);
        if(padded){a+=QByteArray(0x30000-a.size(),char(0xff));b+=QByteArray(0x30000-b.size(),char(0xff));}
        FirmwareImage image;image.binary=true;image.segments={{0x1a000,a+b}};const auto original=image.segments[0].data;QString error;QVector<Segment> primary,alternate;QCOMPARE(FirmwareImage::identify(image),FirmwareImage::Kind::Application);
        QVERIFY2(FirmwareImage::planSlots(image,0x1a000,0x4b000,true,0x30000,primary,alternate,error),qPrintable(error));QCOMPARE(primary.size(),1);QCOMPARE(alternate.size(),1);QCOMPARE(primary[0].data,a);QCOMPARE(alternate[0].data,b);QCOMPARE(image.segments[0].data,original);
        image.segments={{0x1a000,a}};QVERIFY(FirmwareImage::planSlots(image,0x1a000,0x4b000,true,0x30000,primary,alternate,error));QVERIFY(alternate.isEmpty());
    }
    void markedPairRejectsDifferentOrDamagedLengthInformation(){
        auto a=t010App(0x1a000,0),b=t010App(0x4b000,1);marker_fixture::seal(a,ROM_MARKER_APP);b+=QByteArray(8192,char(0xff));marker_fixture::seal(b,ROM_MARKER_APP);a+=QByteArray(8192,char(0xff));
        QString error;QVector<Segment> primary,alternate;FirmwareImage image;image.binary=true;image.segments={{0x1a000,a+b}};QVERIFY(!FirmwareImage::planSlots(image,0x1a000,0x4b000,true,0x30000,primary,alternate,error));
        image.binary=false;image.segments={{0x1a000,a},{0x4b000,b}};QVERIFY(!FirmwareImage::planSlots(image,0x1a000,0x4b000,true,0x30000,primary,alternate,error));
        a=t010App(0x1a000,0);b=t010App(0x4b000,1);marker_fixture::seal(a,ROM_MARKER_APP);marker_fixture::seal(b,ROM_MARKER_APP);b[int(ROM_MARKER_APP_OFFSET)+56]=char(quint8(b[int(ROM_MARKER_APP_OFFSET)+56])^1);image.binary=true;image.segments={{0x1a000,a+b}};QVERIFY(!FirmwareImage::planSlots(image,0x1a000,0x4b000,true,0x30000,primary,alternate,error));
    }
    void rejectsSuperseded152AndAbi10(){
        FlashProfile current;current.target="s32k144-t010-v2";current.flow="app";current.applicationEraseBase=0x1a000;current.applicationEraseLength=0x30000;current.applicationBBase=0x4b000;current.applicationHasB=true;
        FirmwareImage app;app.binary=true;app.segments={{0x1a000,t010App(0x1a000,0)}};QString error;
        for(int mode=0;mode<5;++mode){auto profile=current;auto input=app;
            if(mode==1||mode==2){profile.applicationEraseLength=0x26000;profile.applicationBBase=0x40000;}
            if(mode==3)input.segments[0].data=t010App(0x40000,1);
            if(mode==4){const auto a=t010App(0x1a000,0),b=t010App(0x40000,1);input.segments[0].data=a+QByteArray(0x26000-a.size(),char(0xff))+b+QByteArray(0x26000-b.size(),char(0xff));}
            SlotWire wire(profile);UdsSession session(wire,{});FlashJob job(session,profile,std::make_unique<SimulationKey>());QVERIFY(!job.start(input,t010Driver(mode==0||mode==1?10:11),error));QVERIFY(wire.requests.isEmpty());
        }
        for(quint32 forbidden:{0x4a000u,0x7b000u}){FirmwareImage hex;hex.segments={{0x1a000,t010App(0x1a000,0)},{forbidden,QByteArray(4096,char(0xff))}};SlotWire wire(current);UdsSession session(wire,{});FlashJob job(session,current,std::make_unique<SimulationKey>());QVERIFY(!job.start(hex,t010Driver(11),error));QVERIFY(wire.requests.isEmpty());}
        QVERIFY(!t008::validateCurrentDriver(t010Driver(10),true,error,11));QVERIFY(!t008::validateCurrentDriver(t010Driver(10),true,error,9));
    }
    void rejectsMixedLayoutsAndDriverBeforeSend(){
        FlashProfile p;p.target="s32k144-t010-v2";p.flow="app";p.applicationEraseBase=0x1a000;p.applicationEraseLength=0x30000;p.applicationBBase=0x4b000;p.applicationHasB=true;
        FirmwareImage app;app.binary=true;app.segments={{0x1a000,t010App(0x1a000,0)}};QString error;
        for(int mode=0;mode<8;++mode){auto profile=p;auto input=app;FirmwareImage boot;
            if(mode==1)profile.applicationEraseLength=0x33000;
            if(mode==2)input.segments[0].data[87]=1;
            if(mode==5)profile.target="s32k144-t008-t009-v2";
            if(mode==6){profile.target="generic";profile.applicationBBase=0x49000;}
            if(mode==7)profile.target="generic";
            if(mode==4)marker_fixture::put32(input.segments[0].data,4100,0x16401);
            if(mode==3)boot.segments={{0x5000,linked(0x5000,0x14000,'B')}};
            SlotWire wire(profile);UdsSession session(wire,{});FlashJob job(session,profile,std::make_unique<SimulationKey>());
            QVERIFY(!job.start(input,t010Driver(mode==0||mode==7?9:11),boot,error));QVERIFY(wire.requests.isEmpty());
        }
        QVERIFY(t008::validateCurrentDriver(t010Driver(9),true,error,9));QVERIFY(!t008::validateCurrentDriver(t010Driver(9),true,error,11));QVERIFY(t008::validateCurrentDriver(t010Driver(11),true,error,11));QVERIFY(!t008::validateCurrentDriver(t010Driver(11),true,error,9));
    }
    void binAndHexValidation(){
        QString e;FirmwareImage out;QVector<Segment> a,b;
        QVERIFY(FirmwareImage::loadTarget(file("single.bin",QByteArray(4096,'A')),0x5000,0,false,8192,out,e));QVERIFY(out.binary);
        QVERIFY(FirmwareImage::planSlots(out,0x5000,0,false,8192,a,b,e));QCOMPARE(a[0].data,QByteArray(4096,'A'));QVERIFY(b.isEmpty());
        QVERIFY(FirmwareImage::loadTarget(file("dual.bin",linked(0x5000,4096,'A')+linked(0x10000,4096,'B')),0x5000,0x10000,true,4096,out,e));
        QVERIFY(FirmwareImage::planSlots(out,0x5000,0x10000,true,4096,a,b,e));QCOMPARE(b[0].address,quint32(0x10000));QCOMPARE(b[0].data,linked(0x10000,4096,'B'));
        QVERIFY(!FirmwareImage::loadTarget(file("bad.bin",QByteArray(4097,'X')),0x5000,0x10000,true,4096,out,e));
        QVERIFY(!FirmwareImage::loadTarget(temp.filePath("dual.bin"),0x5000,0,false,4096,out,e));
        QVERIFY(!FirmwareImage::loadTarget(temp.filePath("dual.bin"),0xfffff000,0x10000,true,4096,out,e));
        FirmwareImage hex;hex.segments={{0x5000,QByteArray(4096,'A')},{0x6000,QByteArray(4096,'C')},{0x10000,QByteArray(8192,'B')}};
        QVERIFY2(FirmwareImage::planSlots(hex,0x5000,0x10000,true,0x3000,a,b,e),qPrintable(e));QCOMPARE(a.size(),1);QCOMPARE(a[0].data.size(),8192);QCOMPARE(b.size(),1);
        hex.segments[2].data.chop(4096);QVERIFY(FirmwareImage::planSlots(hex,0x5000,0x10000,true,0x3000,a,b,e));QCOMPARE(b[0].data.size(),4096);
        hex.segments={{0x10000,QByteArray(4096,'B')}};QVERIFY(FirmwareImage::planSlots(hex,0x5000,0x10000,true,4096,a,b,e));QCOMPARE(a[0].address,quint32(0x10000));QVERIFY(b.isEmpty());
        hex.segments={{0x5000,QByteArray(4096,'A')+QByteArray(4096,'B')}};
        QVERIFY(FirmwareImage::planSlots(hex,0x5000,0x6000,true,4096,a,b,e));QCOMPARE(a[0].data,QByteArray(4096,'A'));QCOMPARE(b[0].data,QByteArray(4096,'B'));
        QVERIFY(FirmwareImage::planSlots(hex,0x6000,0x5000,true,4096,a,b,e));QCOMPARE(a[0].address,quint32(0x6000));QCOMPARE(b[0].address,quint32(0x5000));
        for(const auto seg:QVector<Segment>{{0x5001,QByteArray(4096,'X')},{0x5000,QByteArray(4095,'X')},{0x4000,QByteArray(4096,'X')},{0x5000,QByteArray(8192,'X')}}){hex.segments={seg};QVERIFY(!FirmwareImage::planSlots(hex,0x5000,0x10000,true,4096,a,b,e));}
        hex.segments={{0x5000,QByteArray(4096,'A')},{0x5000,QByteArray(4096,'B')}};QVERIFY(!FirmwareImage::planSlots(hex,0x5000,0x10000,true,4096,a,b,e));
        QVERIFY(!FirmwareImage::planSlots(bin(0x5000,'A',true),0x5000,0x5000,true,4096,a,b,e));
        QVERIFY(!FirmwareImage::planSlots(bin(0x5000,'A',false),0x5001,0x10000,true,4096,a,b,e));
    }
    void compatibilityMatrix_data(){QTest::addColumn<bool>("dualBoot");QTest::addColumn<bool>("dualApp");QTest::addColumn<bool>("bootB");QTest::addColumn<bool>("appB");for(bool db:{false,true})for(bool da:{false,true})for(bool bb:{false,true})for(bool ab:{false,true})if((!bb||db)&&(!ab||da))QTest::newRow(qPrintable(QString("boot%1-app%2-select%3%4").arg(db).arg(da).arg(bb).arg(ab)))<<db<<da<<bb<<ab;}
    void compatibilityMatrix(){
        QFETCH(bool,dualBoot);QFETCH(bool,dualApp);QFETCH(bool,bootB);QFETCH(bool,appB);auto p=profile();p.bootHasB=dualBoot;p.applicationHasB=dualApp;SlotWire wire;wire.ecu.maxBlockLength=1026;wire.ecu.t008VerifySkipped=true;
        wire.intercept=[&](const QByteArray &r){if(r.startsWith(QByteArray::fromHex("3101ff0044"))&&((bootB&&address(r)==p.bootEraseBase)||(appB&&address(r)==p.applicationEraseBase)))return QByteArray::fromHex("7f3131");return QByteArray();};
        UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString e;
        QVERIFY2(job.start(bin(p.applicationEraseBase,'a',dualApp),{},bin(p.bootEraseBase,'A',dualBoot),e),qPrintable(e));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QCOMPARE(wire.ecu.memory(bootB?p.bootBBase:p.bootEraseBase),linked(bootB?p.bootBBase:p.bootEraseBase,4096,bootB?'B':'A'));QCOMPARE(wire.ecu.memory(appB?p.applicationBBase:p.applicationEraseBase),linked(appB?p.applicationBBase:p.applicationEraseBase,4096,appB?'b':'a'));
        for(const auto &r:wire.requests)if(r.startsWith(QByteArray::fromHex("340044"))){QVERIFY(!(readBe(r.mid(3,4))&0xfff));QVERIFY(!(readBe(r.mid(7,4))&0xfff));}
    }
    void appOnlyAndContinuousHex(){
        for(bool useB:{false,true}){
            auto p=profile();SlotWire wire;wire.ecu.maxBlockLength=1026;wire.ecu.t008VerifySkipped=true;
            wire.intercept=[&](const QByteArray &r){return useB&&r==QByteArray::fromHex("3101ff00440002000000001000")?QByteArray::fromHex("7f3131"):QByteArray();};
            UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString e;
            QVERIFY(job.start(bin(p.applicationEraseBase,'a',true),{},e));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,2000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));QCOMPARE(wire.ecu.memory(useB?p.applicationBBase:p.applicationEraseBase),linked(useB?p.applicationBBase:p.applicationEraseBase,4096,useB?'b':'a'));
        }
        auto p=profile();p.applicationEraseLength=0x3000;SlotWire wire;wire.ecu.maxBlockLength=1026;wire.ecu.t008VerifySkipped=true;
        FirmwareImage hex;hex.segments={{0x20000,QByteArray(4096,'a')},{0x22000,QByteArray(4096,'c')},{0x30000,QByteArray(4096,'b')},{0x32000,QByteArray(4096,'d')}};
        wire.intercept=[](const QByteArray &r){return r==QByteArray::fromHex("3101ff00440002000000003000")?QByteArray::fromHex("7f3131"):QByteArray();};
        UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString e;
        QVERIFY(!job.start(hex,{},e));QVERIFY(wire.requests.isEmpty());hex.segments[1].address=0x21000;hex.segments[3].address=0x31000;
        QVERIFY2(job.start(hex,{},e),qPrintable(e));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,2000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QCOMPARE(wire.ecu.memory(0x30000),QByteArray(4096,'b')+QByteArray(4096,'d'));
        QVERIFY(!wire.requests.contains(QByteArray::fromHex("3400440003100000001000")));
    }
    void candidateVersionsAndBObservation(){
        auto header=[](const char *version,int minimumBoot){QByteArray data(8192,char(0xff));data.replace(0,8,"T008APP2");data[8]=0;data[9]=0x10;data[10]=data[11]=0;data[12]=char(minimumBoot);data[13]=0;data.replace(46,9,version);return data;};
        const auto a=header("T008A0001",1),b=header("T008B0002",2);QString e;
        auto settings=host::ChannelSettings::defaults(communication::Bus::Can);settings.simulation=true;settings.hardwareKey="preview:CAN:1";settings.handle=0xf101;
        settings.applicationAddress="00020000";settings.applicationBAddress="00030000";settings.applicationLength="00002000";
        settings.checkAppVersion=true;settings.expectedAppVersion="T008A0001";
        const auto appPath=file("version-pair.bin",a+b),driverPath=file("version-driver.bin",QByteArray(16,'D'));
        settings.downloadFiles={QJsonObject{{"role","driver"},{"path",driverPath}},QJsonObject{{"role","app"},{"path",appPath}}};
        host::ChannelWorker worker(settings);QSignalSpy review(&worker,&host::ChannelWorker::packageConfirmationRequested),frames(&worker,&host::ChannelWorker::observedFrames);
        worker.initialize();worker.connectChannel(settings);worker.startPreview(settings);
        QVERIFY(waitForTest([&]{return review.size()==1;},1000));const auto explanation=review[0][1].toString();
        QVERIFY(explanation.contains("App B"));QVERIFY(explanation.contains("T008B0002"));QVERIFY(!explanation.contains("minimumBoot"));QVERIFY(!explanation.contains("App A"));
        worker.confirmPackage(review[0][0].toULongLong(),false);worker.shutdown();
        auto p=profile();p.applicationEraseLength=8192;p.identityDid=0xf189;SlotWire wire;wire.ecu.maxBlockLength=1026;wire.ecu.t008VerifySkipped=true;
        wire.intercept=[&](const QByteArray &r){if(r==QByteArray::fromHex("3101ff00440002000000002000"))return QByteArray::fromHex("7f3131");if(r==QByteArray::fromHex("22f189"))return QByteArray::fromHex("62f189")+QByteArray("T008B0002");return QByteArray();};
        FirmwareImage app;app.binary=true;app.segments={{p.applicationEraseBase,a+b}};
        UdsSession session(wire,{50,100,1000,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);
        QVERIFY2(job.start(app,{},e),qPrintable(e));QVERIFY(waitForTest([&]{return done.size()==1;},1000));QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QVERIFY(done[0][1].toString().contains(QString::fromUtf8("F189 版本匹配")));QVERIFY(!done[0][1].toString().contains(QString::fromUtf8("不匹配")));
    }
    void stopRules_data(){QTest::addColumn<QString>("mode");for(const auto mode:{"both31","a72","b72","single31","34_70","34_31","eraseTimeout","wrongSid"})QTest::newRow(mode)<<QString(mode);}
    void stopRules(){
        QFETCH(QString,mode);auto p=profile();p.negativeResponseChecks["erase"]=true;p.timeoutChecks["erase"]=true;p.bootHasB=mode!="single31";SlotWire wire;wire.ecu.maxBlockLength=1026;wire.ecu.t008VerifySkipped=true;
        wire.intercept=[&](const QByteArray &r){
            if(r.startsWith(QByteArray::fromHex("3101ff0044"))){const auto a=address(r);
                if(mode=="eraseTimeout")return QByteArray::fromHex("7f3178");
                if(mode=="wrongSid")return QByteArray::fromHex("7f3431");
                if(mode=="a72")return QByteArray::fromHex("7f3172");
                if(mode=="both31"||mode=="single31"||(mode=="b72"&&a==p.bootEraseBase))return QByteArray::fromHex("7f3131");
                if(mode=="b72")return QByteArray::fromHex("7f3172");
            }
            if(r.startsWith(QByteArray::fromHex("340044"))&&mode.startsWith("34_"))return QByteArray::fromHex(mode=="34_70"?"7f3470":"7f3431");return QByteArray();};
        UdsSession session(wire,{20,20,60,0});FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);QString e;
        QVERIFY2(job.start({}, {},bin(p.bootEraseBase,'A',mode!="single31"),e),qPrintable(e));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,1000);QVERIFY(!done[0][0].toBool());
        int erases=0,downloads=0;for(const auto &r:wire.requests){if(r.startsWith(QByteArray::fromHex("3101ff0044")))++erases;if(r.startsWith(char(0x34)))++downloads;QVERIFY(!r.startsWith(char(0x36)));QVERIFY(r!=QByteArray::fromHex("1101"));}
        QCOMPARE(erases,(mode=="both31"||mode=="b72")?2:1);QCOMPARE(downloads,mode.startsWith("34_")?1:0);
    }
    void validateBeforeSending(){
        auto p=profile();QString e;SlotWire wire;UdsSession session(wire,{});FlashJob job(session,p,std::make_unique<SimulationKey>());
        auto invalid=bin(p.applicationEraseBase,'a',false);invalid.segments[0].data.chop(1);QVERIFY(!job.start(invalid,{},e));QVERIFY(wire.requests.isEmpty());
        p.applicationEraseBase=p.bootEraseBase;FlashJob overlap(session,p,std::make_unique<SimulationKey>());QVERIFY(!overlap.start(bin(p.applicationEraseBase,'a',false),{},bin(p.bootEraseBase,'A',false),e));QVERIFY(wire.requests.isEmpty());
        p=profile();FlashJob driverOverlap(session,p,std::make_unique<SimulationKey>());FirmwareImage driver;driver.segments={{p.bootEraseBase,QByteArray(10,'D')}};QVERIFY(!driverOverlap.start({},driver,bin(p.bootEraseBase,'A',false),e));QVERIFY(wire.requests.isEmpty());
    }
    void uiAndSettings(){
        auto s=host::ChannelSettings::defaults(communication::Bus::Can);s.simulation=true;s.bootBAddress="00010000";s.applicationBAddress="00030000";host::ChannelSettings restored;QString e;
        QVERIFY(host::ChannelSettings::fromJson(s.toJson(),restored,e));QCOMPARE(restored.bootBAddress,s.bootBAddress);QCOMPARE(restored.applicationBAddress,s.applicationBAddress);
        host::ChannelViewModel vm(restored);host::ChannelPage page(&vm);bool checked=false;
        QTimer::singleShot(0,&page,[&]{auto d=page.findChild<QDialog*>("downloadSettingsDialog");if(!d)return;auto b=d->findChild<QLineEdit*>("bootBAddress"),a=d->findChild<QLineEdit*>("applicationBAddress");if(!a||!b){d->reject();return;}QCOMPARE(b->text(),s.bootBAddress);QCOMPARE(a->text(),s.applicationBAddress);
            auto app=d->findChild<QLineEdit*>("appSecurityLevel"),boot=d->findChild<QLineEdit*>("bootSecurityLevel");QVERIFY(app);QVERIFY(boot);
            // The App security row immediately follows Boot security in the same form.
            auto form=qobject_cast<QFormLayout*>(boot->parentWidget()->parentWidget()->layout());QVERIFY(form);int ar=-1,br=-1;QFormLayout::ItemRole role;form->getWidgetPosition(app->parentWidget(),&ar,&role);form->getWidgetPosition(boot->parentWidget(),&br,&role);QCOMPARE(ar,br+1);
            const auto screenshot=qEnvironmentVariable("SLOT_UI_SCREENSHOT");if(!screenshot.isEmpty()){QTest::qWait(50);auto content=d->findChild<QScrollArea*>()->widget();content->adjustSize();QCoreApplication::processEvents();QVERIFY(content->grab().save(screenshot));}
            b->setText("00040000");a->setText("00050000");checked=true;d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
        page.findChild<QPushButton*>("downloadSettings")->click();QVERIFY(checked);QCOMPARE(vm.settings().bootBAddress,QString("00040000"));QCOMPARE(vm.settings().applicationBAddress,QString("00050000"));
    }
};
QTEST_MAIN(DualSlotTest)
#include "test_dual_slot.moc"
