#include "T008Target.h"
#include <algorithm>
namespace boot::t008 {
static quint32 le(const QByteArray &b,int at,int count=4){
    quint32 v=0;for(int i=0;i<count;++i)v|=quint32(quint8(b[at+i]))<<(8*i);return v;
}
static bool known(const QByteArray &b){return b!=QByteArray(b.size(),0)&&b!=QByteArray(b.size(),char(0xff));}
bool looksLikeImage(const FirmwareImage &image){
    for(const auto &s:image.segments){
        if(s.data.startsWith("T008"))return true;
        if(s.address<=0x20000&&quint64(s.address)+quint64(s.data.size())>=0x20004&&s.data.mid(int(0x20000-s.address),4)=="T008")return true;
    }return false;
}
static bool asciiField(const QByteArray &field,QByteArray &value){
    const int zero=field.indexOf('\0');value=zero<0?field:field.left(zero);
    if(zero>=0&&field.mid(zero)!=QByteArray(field.size()-zero,'\0'))return false;
    for(unsigned char c:value)if(c>0x7f)return false;
    return true;
}
static bool flatten(const FirmwareImage &image,quint32 base,const char *role,QByteArray &bytes,QString &error){
    auto segments=image.segments;std::sort(segments.begin(),segments.end(),[](const Segment&a,const Segment&b){return a.address<b.address;});
    quint64 next=base;bytes.clear();
    for(const auto&s:segments){
        if(s.data.isEmpty()){error=QString("T008 %1 contains an empty segment").arg(role);return false;}
        if(s.address!=next){
            const auto reason=bytes.isEmpty()?"unexpected base address":quint64(s.address)>next?"gap":"overlap";
            const auto hex=[](quint64 value){return "0x"+QString::number(value,16).toUpper().rightJustified(8,'0');};
            error=QString("T008 %1 %2: actual %3, expected %4").arg(role,reason,hex(s.address),hex(next));return false;
        }
        if(next+quint64(s.data.size())>0x100000000ULL){error=QString("T008 %1 segment address overflow").arg(role);return false;}
        bytes+=s.data;next+=s.data.size();
    }
    if(bytes.isEmpty()){error=QString("T008 %1 image is empty").arg(role);return false;}return true;
}
bool images(const FirmwareImage &app,const FirmwareImage &driver,Plan &out,QString &error){
    QByteArray bytes,d;
    if(!flatten(app,0x20000,"App",bytes,error)||!flatten(driver,0x20001400,"Driver",d,error))return false;
    auto bad=[&](const char *why){error=QString::fromLatin1(why);return false;};
    if(bytes.size()<0x1400||bytes.size()>0x56100)return bad("T008 App must contain only descriptor and complete application within the ECU transfer limit");
    const auto h=bytes.left(4096);
    if(h.left(8)!="T008APP2")return bad("T008 descriptor v2 magic mismatch");
    const quint32 length=le(h,8);const quint32 minBoot=le(h,12,2);
    if(length<1024||(length&7)||quint64(0x21000)+length>0x77000||quint64(bytes.size())!=4096ULL+length)
        return bad("T008 descriptor application length mismatch");
    QByteArray project,hardware,version;
    if(minBoot>999||!asciiField(h.mid(14,16),project)||!asciiField(h.mid(30,16),hardware)||!asciiField(h.mid(46,9),version))
        return bad("T008 descriptor v2 metadata invalid");
    if(h.mid(87)!=QByteArray(4009,char(0xff)))return bad("T008 descriptor reserved bytes mismatch");
    const quint32 sp=le(bytes,4096),reset=le(bytes,4100),entry=reset&~quint32(1);
    if(sp<=0x1fff9000||sp>0x20001400||(sp&7)||!(reset&1)||entry<0x21400||quint64(entry)+2>quint64(0x21000)+length)
        return bad("T008 application vector MSP/Thumb reset/range mismatch");
    if(d.size()<128||d.size()>8192||d.left(8)!="T008DRV1"||le(d,8,2)!=128||le(d,10,2)!=1||le(d,12,2)!=1||le(d,14,2)||
       le(d,16)!=quint32(d.size())||le(d,20)!=0x20001400||le(d,24)<128||le(d,24)>quint32(d.size())||le(d,28)!=256||
       le(d,52)!=15||le(d,56)!=4096||le(d,60)!=8||le(d,64)!=0||le(d,68)!=20000000||
       !known(d.mid(72,16))||d.mid(88,40)!=QByteArray(40,0))return bad("T008 native driver header/ABI/address mismatch");
    for(int i=0;i<5;++i){const auto entryOffset=le(d,32+4*i);if((entryOffset&1)||entryOffset<128||entryOffset>=le(d,24))return bad("T008 driver entry offset mismatch");}
    Plan p;p.application={{0x20000,h},{0x21000,bytes.mid(4096)}};p.driver={0x20001400,d};
    p.project=project;p.hardware=hardware;p.configuration=h.mid(55,16);p.calibration=h.mid(71,16);p.version=version;p.minBoot=quint16(minBoot);
    out=p;error.clear();return true;
}
bool response(const QByteArray &request,const QByteArray &pdu,bool suppressed,QString &error){
    if(request.isEmpty()){error="T008 empty request";return false;}
    const int sid=quint8(request[0]);int length=-1;QByteArray prefix;
    bool requestValid=false;
    switch(sid){
    case 0x10:case 0x11:case 0x85:requestValid=request.size()==2;break;
    case 0x14:requestValid=request==QByteArray::fromHex("14ffffff");break;
    case 0x22:requestValid=request==QByteArray::fromHex("22f180")||request==QByteArray::fromHex("22f189");break;
    case 0x27:requestValid=(request.size()==2&&(quint8(request[1])&1))||(request.size()==18&&!(quint8(request[1])&1));break;
    case 0x28:requestValid=request.size()==3;break;
    case 0x2e:requestValid=request.size()==22&&request.left(3)==QByteArray::fromHex("2ef184");break;
    case 0x31:
        requestValid=request==QByteArray::fromHex("31010203")||request==QByteArray::fromHex("3101ff02")||(request.size()==16&&request.left(4)==QByteArray::fromHex("3101ff01"));
        if(request.size()==13&&request.left(5)==QByteArray::fromHex("3101ff0044")){
            const auto address=readBe(request.mid(5,4)),length=readBe(request.mid(9,4));
            requestValid=address==0x20000&&length==0x57000;
        }break;
    case 0x34:requestValid=request.size()==11&&request.left(3)==QByteArray::fromHex("340044");break;
    case 0x36:requestValid=request.size()>=3&&request.size()<=1026;break;
    case 0x37:requestValid=request.size()==1;break;
    }
    if(!requestValid){error="T008 malformed or unsupported request";return false;}
    const bool nativeSuppression=(sid==0x10||sid==0x85||sid==0x28)&&(quint8(request[1])&0x80);
    if(suppressed!=nativeSuppression){error="T008 suppression flag/request mismatch";return false;}
    if(suppressed&&pdu.isEmpty()){error.clear();return true;}
    switch(sid){
    case 0x10:length=6;prefix=QByteArray(1,char(0x50))+char(quint8(request[1])&0x7f);break;
    case 0x11:length=2;prefix=QByteArray::fromHex("5101");break;
    case 0x14:length=1;prefix=QByteArray::fromHex("54");break;
    case 0x22:length=request.mid(1)==QByteArray::fromHex("f180")?17:12;prefix=QByteArray(1,char(0x62))+request.mid(1);break;
    case 0x27:length=(quint8(request[1])&1)?18:2;prefix=QByteArray(1,char(0x67))+request[1];break;
    case 0x28:case 0x85:length=2;prefix=QByteArray(1,char(sid+0x40))+char(quint8(request[1])&0x7f);break;
    case 0x2e:length=3;prefix=QByteArray::fromHex("6ef184");break;
    case 0x31:length=request==QByteArray::fromHex("3101dd01")?4:5;prefix=QByteArray(1,char(0x71))+request.mid(1,3)+(length==5?QByteArray(1,0):QByteArray());break;
    case 0x34:length=4;prefix=QByteArray::fromHex("74200402");break;
    case 0x36:length=2;prefix=QByteArray(1,char(0x76))+request[1];break;
    case 0x37:length=1;prefix=QByteArray::fromHex("77");break;
    }
    if(pdu.size()!=length||!pdu.startsWith(prefix)){error="T008 exact positive response/status/BSC mismatch";return false;}
    error.clear();return true;
}
QByteArray fingerprint(const QDate &date,const QByteArray &tester,QString &error){
    if(!date.isValid()||date.year()<2000||date.year()>2255||tester.size()!=16){error="T008 F184 requires binary date and 16 ASCII tester bytes";return {};}
    for(unsigned char c:tester)if(c<0x20||c>0x7e){error="T008 tester identifier must be printable ASCII";return {};}
    error.clear();return QByteArray(1,char(date.year()-2000))+char(date.month())+char(date.day())+tester;
}
}
