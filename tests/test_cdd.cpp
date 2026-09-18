#include <QtTest>
#include <QFile>
#include "model/CddDatabase.h"
using namespace host::diag;
class CddTest : public QObject {
    Q_OBJECT
    QByteArray fixture(){QFile f(QString(TEST_SOURCE_DIR)+"/fixtures/diagnostic.cdd");if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
    Service get(const Database &db,const QString &qualifier,int variant=0){for(auto s:db.ecus[0].variants[variant].services)if(s.qualifier==qualifier)return s;return {};}
private slots:
    void communicationDefaultsAndTargetOverrides(){
        auto xml=fixture();xml.replace("<ECUDOC>",R"(<ECUDOC><ECUATTS>
          <UNSDEF id="p2" v="150"><QUAL>CAN.P2Client</QUAL><UNIT>ms</UNIT></UNSDEF>
          <UNSDEF id="req" v="1792" df="hex"><QUAL>CAN.ReqCanId</QUAL></UNSDEF>
          <UNSDEF id="linp2" v="300"><QUAL>LIN.P2Client</QUAL></UNSDEF>
          <UNSDEF id="fdp2" v="999"><QUAL>CAN_FD.P2Client</QUAL></UNSDEF>
        </ECUATTS>)");
        xml.replace("<ECU id=\"exampleEcu\">","<ECU id=\"exampleEcu\"><UNS attrref=\"p2\" v=\"70\"/><UNS attrref=\"req\" v=\"1813\"/>");
        xml.replace("<VAR id=\"base\">","<VAR id=\"base\"><UNS attrref=\"p2\" v=\"50\"/>");
        Database db;QString error;QVERIFY2(Database::parse(xml,db,error),qPrintable(error));
        auto base=db.ecus[0].variants[0].communication,derived=db.ecus[0].variants[1].communication;
        QCOMPARE(base["CAN.P2Client"].value,QString("50"));QVERIFY(base["CAN.P2Client"].source.startsWith("Variant:"));
        QCOMPARE(derived["CAN.P2Client"].value,QString("50"));QCOMPARE(derived["CAN.ReqCanId"].value,QString("1813"));
        QCOMPARE(base["LIN.P2Client"].value,QString("300"));QVERIFY(!base.contains("CAN_FD.P2Client"));
        xml.replace("<VAR id=\"other\" baseref=\"base\">","<VAR id=\"other\" baseref=\"base\"><UNS attrref=\"p2\" v=\"25\"/>");
        QVERIFY(Database::parse(xml,db,error));QCOMPARE(db.ecus[0].variants[1].communication["CAN.P2Client"].value,QString("25"));
    }
    void versionsAndTemplates(){
        for(const QByteArray version:{"2.0.5","8.0.0","12.0.0","14.0.0","15.0.4"}){
            Database db;QString error;auto xml=fixture();QVERIFY(!xml.isEmpty());xml.replace("15.0.4",version);
            QVERIFY2(Database::parse(xml,db,error),qPrintable(error));QCOMPARE(db.ecus.size(),1);QCOMPARE(db.ecus[0].variants.size(),2);QCOMPARE(db.ecus[0].variants[0].services.size(),5);QVERIFY2(db.warnings.isEmpty(),qPrintable(db.warnings.join('\n')));
            auto s=get(db,"Number/Write");QCOMPARE(s.sid,0x2e);QCOMPARE(s.request.fields.size(),3);QVERIFY(s.request.fields[2].littleEndian);
            QByteArray request;QVERIFY2(Codec::encode(s.request,{{"2","4660"}},request,error),qPrintable(error));QCOMPARE(request,QByteArray::fromHex("2ef1a03412"));
            QCOMPARE(Codec::expected(s,request),QByteArray::fromHex("6ef1a0"));
            auto other=get(db,"Number/Read",1);QVERIFY(Codec::encode(other.request,{},request,error));QCOMPARE(request,QByteArray::fromHex("22f1a1"));
        }
    }
    void recursiveVariantInheritanceAndExclusions(){
        auto xml=fixture();const QByteArray derived=R"(<VAR id="derived" baseref="other"><QUAL>Derived</QUAL><DIAGCLASS>
          <DIAGINST enabled="0"><QUAL>VIN</QUAL></DIAGINST>
          <DIAGINST><QUAL>Number</QUAL><SERVICE enabled="0"><QUAL>Write</QUAL></SERVICE></DIAGINST>
        </DIAGCLASS></VAR>)";
        xml.replace("<VAR id=\"base\">",derived+"<VAR id=\"base\">");
        Database db;QString error;QVERIFY2(Database::parse(xml,db,error),qPrintable(error));
        QCOMPARE(db.ecus[0].variants[0].services.size(),2);
        QByteArray request;QVERIFY(Codec::encode(get(db,"Number/Read").request,{},request,error));QCOMPARE(request,QByteArray::fromHex("22f1a1"));
        auto invalid=xml;invalid.replace("id=\"base\">","id=\"base\" baseref=\"derived\">");QVERIFY(!Database::parse(invalid,db,error));QVERIFY(error.contains("循环"));
        invalid=xml;invalid.replace("baseref=\"other\"","baseref=\"missing\"");QVERIFY(!Database::parse(invalid,db,error));
        // Defaults may live in a service template and be overridden by an instance.
        xml=fixture();xml.replace("<DCLSRVTMPL id=\"sessionTemplate\" tmplref=\"session\"/>","<DCLSRVTMPL id=\"sessionTemplate\" tmplref=\"session\"><STATICVALUE shstaticref=\"sessionMap\" v=\"3\"/></DCLSRVTMPL>");
        QVERIFY(Database::parse(xml,db,error));QVERIFY(Codec::encode(get(db,"DefaultSession/Start").request,{},request,error));QCOMPARE(request,QByteArray::fromHex("1001"));
        xml.replace("<STATICVALUE shstaticref=\"sessionMap\" v=\"1\"/>","");
        QVERIFY(Database::parse(xml,db,error));QVERIFY(Codec::encode(get(db,"DefaultSession/Start").request,{},request,error));QCOMPARE(request,QByteArray::fromHex("1003"));
    }
    void stringsValidationAndRawMode(){
        Database db;QString error;QVERIFY(Database::parse(fixture(),db,error));auto s=get(db,"VIN/Write");QByteArray request;
        QVERIFY(!Codec::encode(s.request,{},request,error));QVERIFY(!Codec::encode(s.request,{{"2","too short"}},request,error));
        QVERIFY(Codec::encode(s.request,{{"2","TESTVIN0123456789"}},request,error));QCOMPARE(request,QByteArray::fromHex("2ef190")+"TESTVIN0123456789");
        QVERIFY(Codec::validate(s,request,error));request[1]=char(0);QVERIFY(!Codec::validate(s,request,error));
        QVERIFY(!Codec::parseHex("2E F1 9",request,error));QVERIFY(!Codec::parseHex("2E zz",request,error));
        QVERIFY(!Codec::encode(s.request,{{"2",QString(17,QChar(0x4e2d))}},request,error));
        Values decoded;QVERIFY(Codec::decode(s.response,QByteArray::fromHex("6ef190"),decoded,error));QVERIFY(!Codec::decode(s.response,QByteArray::fromHex("6ef191"),decoded,error));
    }
    void bitFieldsAndNumericBoundaries(){
        Message m;Field a;a.key="a";a.bits=3;Field b;b.key="b";b.bits=5;Field c;c.key="c";c.bits=16;c.littleEndian=true;m.fields={a,b,c};QByteArray bytes;QString error;
        QVERIFY(Codec::encode(m,{{"a","5"},{"b","3"},{"c","0x1234"}},bytes,error));QCOMPARE(bytes,QByteArray::fromHex("a33412"));
        Values values;QVERIFY(Codec::decode(m,bytes,values,error));QCOMPARE(values["a"],QString("0x5"));
        QVERIFY(!Codec::encode(m,{{"a","8"},{"b","3"},{"c","0"}},bytes,error));
        Field large;large.key="x";large.bits=64;m.fields={large};QVERIFY(Codec::encode(m,{{"x","18446744073709551615"}},bytes,error));QCOMPARE(bytes,QByteArray(8,char(0xff)));
        Field signedByte;signedByte.key="s";signedByte.encoding="sgn";m.fields={signedByte};QVERIFY(Codec::encode(m,{{"s","-128"}},bytes,error));QCOMPARE(bytes,QByteArray::fromHex("80"));QVERIFY(!Codec::encode(m,{{"s","128"}},bytes,error));
        Field year;year.key="y";year.factor=1;year.offset=2000;m.fields={year};QVERIFY(Codec::encode(m,{{"y","2026"}},bytes,error));QCOMPARE(bytes,QByteArray::fromHex("1a"));
        signedByte.factor=0.5;signedByte.offset=-10;m.fields={signedByte};
        QVERIFY(Codec::encode(m,{{"s","-11"}},bytes,error));QCOMPARE(bytes,QByteArray::fromHex("fe"));QVERIFY(Codec::decode(m,bytes,values,error));QCOMPARE(values["s"],QString("-11"));
        QVERIFY(!Codec::encode(m,{{"s","54"}},bytes,error));QVERIFY(!Codec::encode(m,{{"s","-74.5"}},bytes,error));
    }
    void variableDataAndSimulationRoundtrip(){
        Message m;Field f;f.key="d";f.encoding="hex";f.array=true;f.minCount=1;f.maxCount=3;f.lengthBytes=1;m.fields={f};QString error;QByteArray bytes;Values decoded;
        QVERIFY(Codec::encode(m,{{"d","aa bb"}},bytes,error));QCOMPARE(bytes,QByteArray::fromHex("02aabb"));QVERIFY(Codec::decode(m,bytes,decoded,error));QCOMPARE(decoded["d"],QString("AA BB"));QVERIFY(!Codec::decode(m,QByteArray::fromHex("03aabb"),decoded,error));
        Database db;QVERIFY(Database::parse(fixture(),db,error));auto w=get(db,"VIN/Write"),r=get(db,"VIN/Read");QMap<int,QByteArray> memory;
        bytes=QByteArray::fromHex("2ef190")+"TESTVIN0123456789";QCOMPARE(Codec::simulationResponse(w,bytes,memory),QByteArray::fromHex("6ef190"));
        QCOMPARE(Codec::simulationResponse(r,QByteArray::fromHex("22f190"),memory),QByteArray::fromHex("62f190")+"TESTVIN0123456789");
    }
    void malformedAndMissingReferences(){
        Database db;QString error;QVERIFY(Database::parse(fixture(),db,error));const auto original=db.ecus[0].name;
        QVERIFY(!Database::parse("<CANDELA>",db,error));QCOMPARE(db.ecus[0].name,original);
        auto xml=fixture();xml.replace("15.0.4","16.0.0");QVERIFY(!Database::parse(xml,db,error));
        xml=fixture();xml.replace("dtref=\"littleWord\"","dtref=\"missing\"");QVERIFY(Database::parse(xml,db,error));QVERIFY(!get(db,"Number/Write").issue.isEmpty());
        xml=fixture();xml.replace("SYSTEM \"candela.dtd\"","[<!ENTITY injected SYSTEM 'file:///not-allowed'>]");QVERIFY(!Database::parse(xml,db,error));
    }
    void suppliedCdd(){
        const auto path=QString(TEST_SOURCE_DIR)+"/../testsrc/ECH_Tst_V05.cdd";if(!QFile::exists(path))QSKIP("Local acceptance CDD is not distributed");
        Database db;QString error;QVERIFY2(Database::load(path,db,error),qPrintable(error));QCOMPARE(db.version,QString("15.0.4"));QCOMPARE(db.ecus[0].variants[0].services.size(),39);
        int writable=0;for(const auto &s:db.ecus[0].variants[0].services){QVERIFY2(s.issue.isEmpty(),qPrintable(s.name+": "+s.issue));if(s.sid==0x2e)++writable;}
        QCOMPARE(writable,2);qInfo()<<"CDD services:"<<db.ecus[0].variants[0].services.size()<<"Warnings:"<<db.warnings;
        auto vin=get(db,"VINDataIdentifier/Write");QByteArray request;QVERIFY2(Codec::encode(vin.request,{{"2","TESTVIN0123456789"}},request,error),qPrintable(error));QCOMPARE(request,QByteArray::fromHex("2ef190")+"TESTVIN0123456789");
        auto fingerprint=get(db,"applicationSoftwareFingerprintDataIdentifier/Write");
        QVERIFY2(Codec::encode(fingerprint.request,{{"2","2026"},{"3","9"},{"4","17"},{"5","0123456789ABCDEF"}},request,error),qPrintable(error));
        QCOMPARE(request,QByteArray::fromHex("2ef1841a0911")+"0123456789ABCDEF");
    }
};
QTEST_GUILESS_MAIN(CddTest)
#include "test_cdd.moc"
