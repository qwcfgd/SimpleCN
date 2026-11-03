#include "T008Target.h"
#include <algorithm>
namespace boot::t008 {
static quint32 le(const QByteArray &b,int at,int count=4){
    quint32 v=0;for(int i=0;i<count;++i)v|=quint32(quint8(b[at+i]))<<(8*i);return v;
}
bool validateBlockLimit(int pduLimit,QString &error){
    if(pduLimit<1026){error="T008 TransferData PDU limit must allow SID, BSC and 1024 App bytes";return false;}
    error.clear();return true;
}
bool compatibility(const Plan &p,QString &error){
    auto bad=[&](const char *why){error=QString::fromLatin1(why);return false;};
    if(!((p.driverMinor==1&&p.driverCapabilities==15)||(p.driverMinor==2&&p.driverCapabilities==31)||(p.driverMinor==4&&p.driverCapabilities==64)))
        return bad("T008 profile v1: unsupported Driver ABI/capabilities");
    error.clear();return true;
}
bool looksLikeImage(const FirmwareImage &image){
    for(const auto &s:image.segments){
        if(s.data.startsWith("T008"))return true;
        if(s.address<=0x20000&&quint64(s.address)+quint64(s.data.size())>=0x20004&&s.data.mid(int(0x20000-s.address),4)=="T008")return true;
    }return false;
}
bool looksLikeDriver(const FirmwareImage &image){
    for(const auto &s:image.segments)if(s.address==0x20001400&&s.data.startsWith("T008DRV1"))return true;
    return false;
}
static bool asciiField(const QByteArray &field,QByteArray &value){
    const int zero=field.indexOf('\0');value=zero<0?field:field.left(zero);
    if(zero>=0&&field.mid(zero)!=QByteArray(field.size()-zero,'\0'))return false;
    for(unsigned char c:value)if(c>0x7f)return false;
    return true;
}
bool images(const FirmwareImage &app,const FirmwareImage &driver,Plan &out,QString &error){
    if(app.segments.isEmpty()){error="无法构造下载数据：App 文件为空";return false;}
    Plan p;p.application=app.segments;
    const auto first=app.segments.first();
    if(first.data.size()>4096){p.application.removeFirst();p.application.prepend({first.address+4096,first.data.mid(4096)});p.application.prepend({first.address,first.data.left(4096)});}
    if(first.data.size()>=87&&first.data.startsWith("T008APP2")){
        const auto h=first.data.left(87);p.minBoot=quint16(le(h,12,2));p.hasMinimumBoot=true;
        asciiField(h.mid(14,16),p.project);asciiField(h.mid(30,16),p.hardware);
        p.hasAppVersion=asciiField(h.mid(46,9),p.version)&&!p.version.isEmpty();
        p.configuration=h.mid(55,16);p.calibration=h.mid(71,16);
    }
    if(driver.segments.size()==1){p.driver=driver.segments.first();const auto &d=p.driver.data;
        if(d.size()>=14&&d.startsWith("T008DRV1")){p.driverMajor=quint16(le(d,10,2));p.driverMinor=quint16(le(d,12,2));p.hasDriverAbi=true;}
        if(d.size()>=56)p.driverCapabilities=le(d,52);
    }
    out=p;error.clear();return true;
}
bool compareFileVersions(const Plan &p,const QMap<QString,QString> &expected,QStringList &mismatches,QString &error){
    mismatches.clear();error.clear();
    for(auto it=expected.cbegin();it!=expected.cend();++it){
        QString label,actual;bool available=false;
        if(it.key()=="driverAbi"){label="Driver ABI";available=p.hasDriverAbi;actual=QString("%1.%2").arg(p.driverMajor).arg(p.driverMinor);}
        else if(it.key()=="appVersion"){label="App 版本";available=p.hasAppVersion;actual=QString::fromLatin1(p.version);}
        else if(it.key()=="minimumBoot"){label="包内最低 Boot 版本";available=p.hasMinimumBoot;actual=QString::number(p.minBoot).rightJustified(3,'0');}
        else{error="未知版本字段";return false;}
        if(!available){mismatches.append(label+"：文件值无法解析，填写="+it.value().trimmed());continue;}
        if(it.value().trimmed().isEmpty()){mismatches.append(label+"：文件="+actual+"，填写值为空");continue;}
        if(actual!=it.value().trimmed())mismatches.append(QString("%1：文件=%2，填写=%3").arg(label,actual,it.value().trimmed()));
    }
    return true;
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
        if(request.size()==13&&request.left(5)==QByteArray::fromHex("3101ff0044"))requestValid=true;
        break;
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
    case 0x34:length=pdu.size();prefix=QByteArray::fromHex("74");break;
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
