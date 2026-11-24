#include <QtTest>
#include "RomMarkerFixture.h"
#include "protocol/T008Target.h"
#include "protocol/SimulatedUdsEcu.h"
using namespace boot;
class MarkerWire final:public DiagnosticTransport {
public:
    SimulatedUdsEcu ecu;bool corruptFinalApp=false;
    explicit MarkerWire(const FlashProfile &profile):ecu(profile){ecu.maxBlockLength=1026;ecu.t008BootSelection=true;ecu.setMemory(0x5000,marker_fixture::boot());ecu.setMemory(0x15000,marker_fixture::app());ecu.setMemory(0x7e000,marker_fixture::calib());}
    bool send(const QByteArray &p,QString &error)override{error.clear();QTimer::singleShot(0,this,[this,p]{
        emit sent();if(corruptFinalApp&&p==QByteArray::fromHex("3101ff01")){auto data=ecu.memory(0x15000);data[4096]=char(quint8(data[4096])^1);ecu.setMemory(0x15000,data);}
        for(const auto &reply:ecu.handle(p))emit received(reply);
    });return true;}
    void cancel()override{}int maximumPdu()const override{return 4095;}
};
class RomMarkerTest:public QObject {
    Q_OBJECT
private slots:
    void actualCompilerArtifacts(){
        const auto root=qEnvironmentVariable("T008_MARKER_ARTIFACT_ROOT"),driver=qEnvironmentVariable("T008_MARKER_DRIVER_ROOT");
        if(root.isEmpty()||driver.isEmpty())QSKIP("Set current native artifact roots for read-only validation");
        struct Item{QString path;quint32 base,kind;};
        const QVector<Item> items={{driver+"/driver",0x20001400,ROM_MARKER_DRIVER},{root+"/app/app",0x15000,ROM_MARKER_APP},
            {root+"/boot-a/boot-a",0x5000,ROM_MARKER_BOOT},{root+"/boot-b/boot-b",0x6d000,ROM_MARKER_BOOT},{root+"/calib/calib",0x7e000,ROM_MARKER_CAL}};
        QString e;QVector<QPair<Segment,quint32>> selected;
        for(const auto &item:items){FirmwareImage bin,hex;RomMarker marker;
            QVERIFY2(FirmwareImage::load(item.path+".bin",item.base,bin,e),qPrintable(e));
            QVERIFY2(FirmwareImage::load(item.path+".hex",item.base,hex,e),qPrintable(e));
            QCOMPARE(bin.segments.size(),1);QCOMPARE(hex.segments.size(),1);QCOMPARE(bin.segments[0].data,hex.segments[0].data);QCOMPARE(hex.segments[0].address,item.base);
            QVERIFY2(FirmwareImage::validateMarker(bin.segments[0],item.kind,marker,e),qPrintable(e));selected.append({bin.segments[0],item.kind});
            if(item.kind==ROM_MARKER_DRIVER)QVERIFY2(t008::validateCurrentDriver(bin,false,e),qPrintable(e));
            if(item.kind==ROM_MARKER_APP){t008::Plan p;QVERIFY2(t008::completeApplication(bin,0x15000,0x57000,false,0,p,e),qPrintable(e));QCOMPARE(p.version,QByteArray("T008A0001"));QCOMPARE(p.application[0].data,bin.segments[0].data);}
            if(item.kind==ROM_MARKER_BOOT){QVector<Segment> a,b;
                if(item.base==0x6d000){
                    // A B-linked single BIN must not be silently relocated to configured A.
                    QVERIFY(!FirmwareImage::planSlots(bin,0x5000,0x6d000,true,0xf000,a,b,e));
                    QVERIFY2(FirmwareImage::planSlots(bin,item.base,0,false,0xf000,a,b,e),qPrintable(e));
                }else QVERIFY2(FirmwareImage::planSlots(bin,0x5000,0x6d000,true,0xf000,a,b,e),qPrintable(e));
                QCOMPARE(a.size()+b.size(),1);QCOMPARE((a.isEmpty()?b:a).first().data,bin.segments[0].data);
                QVERIFY2(FirmwareImage::planSlots(hex,0x5000,0x6d000,true,0xf000,a,b,e),qPrintable(e));
                QCOMPARE(a.first().address,item.base);QCOMPARE(a.first().data,bin.segments[0].data);
            }
        }
        QVERIFY2(FirmwareImage::validateMarkerSet(selected,e),qPrintable(e));
        FirmwareImage bundle;QVERIFY2(FirmwareImage::loadBootBundle(root+"/boot-ab/boot-ab.bin",bundle,e),qPrintable(e));QCOMPARE(FirmwareImage::identify(bundle),FirmwareImage::Kind::BootBundle);
        QVector<Segment> a,b;QVERIFY2(FirmwareImage::planSlots(bundle,0x5000,0x6d000,true,0xf000,a,b,e),qPrintable(e));QCOMPARE(a.first().data.size(),0xf000);QCOMPARE(b.first().data.size(),0xf000);
    }
    void bootSlotGeometry_data(){
        QTest::addColumn<int>("bytes");QTest::addColumn<quint32>("alternate");
        QTest::newRow("legacy60")<<0xf000<<quint32(0x6d000);
        QTest::newRow("unified80")<<0x14000<<quint32(0x68000);
    }
    void bootSlotGeometry(){
        QFETCH(int,bytes);QFETCH(quint32,alternate);
        const auto first=marker_fixture::boot(0x5000,"AST1",bytes),second=marker_fixture::boot(alternate,"AST1",bytes);
        RomMarker marker;QString error;
        QVERIFY2(FirmwareImage::validateMarker({0x5000,first},ROM_MARKER_BOOT,marker,error),qPrintable(error));
        QCOMPARE(marker.imageBytes,quint32(bytes));
        FirmwareImage bundle;bundle.binary=true;bundle.segments={{0x5000,first+second}};
        QVector<Segment> a,b;QVERIFY2(FirmwareImage::planSlots(bundle,0x5000,alternate,true,bytes,a,b,error),qPrintable(error));
        QCOMPARE(a.first().data,first);QCOMPARE(b.first().data,second);
        auto corrupt=second;corrupt[bytes-128]=char(quint8(corrupt[bytes-128])^1);
        QVERIFY(!FirmwareImage::validateMarker({alternate,corrupt},ROM_MARKER_BOOT,marker,error));
        auto wrongLength=first;wrongLength[bytes-64+47]='1';
        marker_fixture::put64(wrongLength,bytes-8,RomMarker_Crc(0,reinterpret_cast<const uint8_t*>(wrongLength.constData()+bytes-64),56));
        QVERIFY(!FirmwareImage::validateMarker({0x5000,wrongLength},ROM_MARKER_BOOT,marker,error));
    }
    void didVersionIsIndependentOfMarker(){
        auto a=marker_fixture::app(),b=a;marker_fixture::seal(b,ROM_MARKER_APP,"AST1","20261011");
        FirmwareImage first,second;first.segments={{0x15000,a}};second.segments={{0x15000,b}};
        t008::Plan x,y;QString e;QVERIFY(t008::images(first,{},x,e));QVERIFY(t008::images(second,{},y,e));
        QVERIFY(x.hasAppVersion);QCOMPARE(x.version,QByteArray("T008A0001"));QCOMPARE(y.version,x.version);
        QVERIFY(a.mid(ROM_MARKER_APP_OFFSET+16,8)!=b.mid(ROM_MARKER_APP_OFFSET+16,8));
        second.segments[0].data.replace(46,9,"T008A0002");QVERIFY(t008::images(second,{},y,e));QCOMPARE(y.version,QByteArray("T008A0002"));
    }
    void bootLastPageIsCovered(){auto d=marker_fixture::boot();d[0xef00]=char(quint8(d[0xef00])^1);RomMarker m;QString e;QVERIFY(!FirmwareImage::validateMarker({0x5000,d},ROM_MARKER_BOOT,m,e));}
    void ecmaVector(){const QByteArray d="123456789";QCOMPARE(quint64(RomMarker_Crc(0,reinterpret_cast<const uint8_t *>(d.constData()),d.size())),quint64(0x6c40df5f0b497347ULL));}
    void coverage_data(){QTest::addColumn<quint32>("kind");for(auto k:{ROM_MARKER_DRIVER,ROM_MARKER_APP,ROM_MARKER_BOOT,ROM_MARKER_CAL})QTest::newRow(qPrintable(QString::number(k)))<<quint32(k);}
    void coverage(){QFETCH(quint32,kind);auto d=kind==ROM_MARKER_DRIVER?marker_fixture::driver():kind==ROM_MARKER_APP?marker_fixture::app():kind==ROM_MARKER_BOOT?marker_fixture::boot():marker_fixture::calib();RomMarker marker;QString e;
        QVERIFY2(FirmwareImage::validateMarker({0,d},kind,marker,e),qPrintable(e));QCOMPARE(marker.imageBytes,quint32(d.size()));QCOMPARE(QByteArray(reinterpret_cast<const char *>(marker.version),8),QByteArray("AST17CFF"));
        const int body=kind==ROM_MARKER_DRIVER?200:kind==ROM_MARKER_APP?4096:0;d[body]=char(quint8(d[body])^1);QVERIFY(!FirmwareImage::validateMarker({0,d},kind,marker,e));}
    void protectedLength(){auto d=marker_fixture::app();const int at=ROM_MARKER_APP_OFFSET;d[at+47]='1';marker_fixture::put64(d,at+56,RomMarker_Crc(0,reinterpret_cast<const uint8_t *>(d.constData()+at),56));RomMarker m;QString e;QVERIFY(!FirmwareImage::validateMarker({0x15000,d},ROM_MARKER_APP,m,e));}
    void stageMatching(){auto a=marker_fixture::app(),b=marker_fixture::calib("AST2");RomMarker x,y;QString e;QVERIFY(FirmwareImage::validateMarker({0x15000,a},ROM_MARKER_APP,x,e));QVERIFY(FirmwareImage::validateMarker({0x7e000,b},ROM_MARKER_CAL,y,e));QVERIFY(!RomMarker_Match(&x,&y));}
    void separateBootSlots(){const auto a=marker_fixture::boot(0x5000),b=marker_fixture::boot(0x6d000);FirmwareImage f;f.binary=true;f.segments={{0x5000,a+b}};QVector<Segment> x,y;QString e;QVERIFY2(FirmwareImage::planSlots(f,0x5000,0x6d000,true,0xf000,x,y,e),qPrintable(e));QCOMPARE(x.first().data,a);QCOMPARE(y.first().data,b);RomMarker ma,mb;QVERIFY(FirmwareImage::validateMarker(x.first(),ROM_MARKER_BOOT,ma,e));QVERIFY(FirmwareImage::validateMarker(y.first(),ROM_MARKER_BOOT,mb,e));QVERIFY(ma.bodyCrc!=mb.bodyCrc);}
    void markedDriverControlIsNotLegacyAbi(){
        FirmwareImage image;image.segments={{0x20001400,marker_fixture::driver()}};QString error;
        QVERIFY2(t008::validateCurrentDriver(image,true,error,11),qPrintable(error));
        QVERIFY(t008::validateCurrentDriver(image,false,error));
        QVERIFY(!t008::validateCurrentDriver(image,true,error,9));
        QVERIFY(!t008::validateCurrentDriver(image,true,error,10));
        const auto good=image.segments[0].data;
        for(int offset:{74,76,78}){
            image.segments[0].data=good;image.segments[0].data[offset]=1;
            marker_fixture::seal(image.segments[0].data,ROM_MARKER_DRIVER);
            QVERIFY(!t008::validateCurrentDriver(image,true,error,11));
            QVERIFY(!t008::validateCurrentDriver(image,false,error));
        }
        image.segments[0].data=good;image.segments[0].data[192]=char(quint8(image.segments[0].data[192])^1);
        QVERIFY(!t008::validateCurrentDriver(image,true,error,11));
    }
    void driverEntries(){FirmwareImage f;f.segments={{0x20001400,marker_fixture::driver()}};QString e;QVERIFY2(t008::validateCurrentDriver(f,false,e),qPrintable(e));marker_fixture::put32(f.segments[0].data,96,0x900);marker_fixture::seal(f.segments[0].data,ROM_MARKER_DRIVER);QVERIFY(!t008::validateCurrentDriver(f,false,e));}
    void reservedControlFieldsRejectLegacyVersionBytes(){FirmwareImage f;auto data=marker_fixture::driver();data[74]=char(0xff);data[76]=char(0xff);marker_fixture::seal(data,ROM_MARKER_DRIVER);f.segments={{0x20001400,data}};QString e;QVERIFY(!t008::validateCurrentDriver(f,false,e));}
    void badMarkerNeverUsesLegacyIdentification(){FirmwareImage f;auto data=marker_fixture::app();data[4096]=char(quint8(data[4096])^1);f.segments={{0x15000,data}};QCOMPARE(FirmwareImage::identify(f),FirmwareImage::Kind::Unknown);}
    void damagedProjectStillRequiresMarkerValidation(){FirmwareImage f;auto data=marker_fixture::app();data[ROM_MARKER_APP_OFFSET]='X';f.segments={{0x15000,data}};QVERIFY(FirmwareImage::hasCurrentMarker(f));QCOMPARE(FirmwareImage::identify(f),FirmwareImage::Kind::Unknown);RomMarker m;QString e;QVERIFY(!FirmwareImage::validateMarker(f.segments[0],ROM_MARKER_APP,m,e));}
    void dependencyActivation_data(){QTest::addColumn<bool>("corrupt");QTest::newRow("verified")<<false;QTest::newRow("flash-corruption")<<true;}
    void dependencyActivation(){QFETCH(bool,corrupt);FlashProfile p;p.target="s32k144-t008-t009-v2";p.flow="boot";p.stepEnabled["pre1003"]=true;p.bootEraseLength=0xf000;p.bootBBase=0x6d000;p.applicationEraseBase=0x15000;p.applicationEraseLength=0x57000;p.resetWaitMs=0;
        MarkerWire wire(p);wire.corruptFinalApp=corrupt;SessionOptions options;options.p2Ms=10;UdsSession session(wire,options);FlashJob job(session,p,std::make_unique<SimulationKey>());QSignalSpy done(&job,&FlashJob::finished);
        FirmwareImage app,driver,boot,cal;app.segments={{0x15000,marker_fixture::app()}};driver.segments={{0x20001400,marker_fixture::driver()}};boot.binary=true;boot.segments={{0x5000,marker_fixture::boot()+marker_fixture::boot(0x6d000)}};cal.segments={{0x7e000,marker_fixture::calib()}};QString error;
        QVERIFY2(job.start(app,driver,boot,cal,error),qPrintable(error));QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(),10000);QCOMPARE(done.first().first().toBool(),!corrupt);QCOMPARE(wire.ecu.activeBootAddress,corrupt?quint32(0x5000):quint32(0x6d000));QCOMPARE(wire.ecu.resets,corrupt?0:1);
    }
};
QTEST_GUILESS_MAIN(RomMarkerTest)
#include "test_rom_marker.moc"
