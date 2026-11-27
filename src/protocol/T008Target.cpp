#include "T008Target.h"
#include "S32kLayout.h"
#include <algorithm>
namespace boot::t008 {
static quint32 le(const QByteArray &b,int at,int count=4){
    quint32 v=0;for(int i=0;i<count;++i)v|=quint32(quint8(b[at+i]))<<(8*i);return v;
}
bool validateBlockLimit(int pduLimit,QString &error){
    if(pduLimit<1026){error="TransferData PDU limit must allow SID, BSC and 1024 ROM bytes";return false;}
    error.clear();return true;
}
bool compatibility(const Plan &p,QString &error){
    if(p.driverCapabilities!=15&&p.driverCapabilities!=31&&p.driverCapabilities!=64&&p.driverCapabilities!=192){error="Unsupported Driver operations";return false;}
    error.clear();return true;
}
bool looksLikeImage(const FirmwareImage &image){
    for(const auto &s:image.segments){
        if(s.data.startsWith("S32K144T")||s.data.startsWith("T008")||s.data.startsWith("T010"))return true;
    }return false;
}
bool looksLikeDriver(const FirmwareImage &image){
    if(image.segments.size()!=1)return false;
    return image.segments.first().data.startsWith("S32K144T")||image.segments.first().data.startsWith("T008DRV1");
}
static bool asciiField(const QByteArray &field,QByteArray &value){
    const int zero=field.indexOf('\0');value=zero<0?field:field.left(zero);
    if(zero>=0&&field.mid(zero)!=QByteArray(field.size()-zero,'\0'))return false;
    for(unsigned char c:value)if(c>0x7f)return false;
    return true;
}
bool images(const FirmwareImage &app,const FirmwareImage &driver,Plan &out,QString &error){
    if(app.segments.isEmpty()){error="App image is empty";return false;}
    Plan p;p.application=app.segments;const auto &first=app.segments.first();
    RomMarker marker;QString markerError;
    if(FirmwareImage::validateMarker(first,ROM_MARKER_APP,marker,markerError)){
        p.project=QByteArray(reinterpret_cast<const char *>(marker.project),8);
        p.hardware=QByteArray(reinterpret_cast<const char *>(marker.version),1);
        // F189 is the existing App descriptor version, independent of the ROM marker.
        p.hasAppVersion=asciiField(first.data.mid(46,9),p.version)&&!p.version.isEmpty();
    }else if(first.data.size()>=88&&(first.data.startsWith("T008APP2")||first.data.startsWith("T010APP1"))){
        asciiField(first.data.mid(14,16),p.project);asciiField(first.data.mid(30,16),p.hardware);
        p.hasAppVersion=asciiField(first.data.mid(46,9),p.version)&&!p.version.isEmpty();
    }
    if(driver.segments.size()==1){p.driver=driver.segments.first();const auto &d=p.driver.data;const int offset=d.startsWith("S32K144T")?64:0;
        if(d.size()>=offset+56)p.driverCapabilities=le(d,offset+52);
    }
    out=p;error.clear();return true;
}
bool validateCurrentDriver(const FirmwareImage &image,bool t010,QString &error,quint16 t010Minor){
    auto bad=[&]{error="Invalid Driver marker, entry table or operations";return false;};
    if(image.segments.size()!=1)return bad();
    const auto &d=image.segments[0].data;const auto bytes=quint32(d.size());
    const bool marked=d.startsWith("S32K144T");const int offset=marked?64:0;const quint32 headerBytes=marked?192u:128u;
    if(!marked&&!t010)return bad();
    if(marked){RomMarker marker;if(!FirmwareImage::validateMarker(image.segments[0],ROM_MARKER_DRIVER,marker,error))return false;}
    if(bytes<headerBytes||d.mid(offset,8)!="T008DRV1"||le(d,offset+8,2)!=headerBytes||le(d,offset+14,2)!=0)return bad();
    // Marked Drivers use the frozen 192-byte control format: these fields
    // are reserved zero, not the old 128-byte Driver ABI version.
    if(marked){
        if(le(d,offset+10,2)!=0u||le(d,offset+12,2)!=0u)return bad();
        if(t010&&t010Minor!=11u)return bad(); // Current T010 192 KiB profile only.
    }else if(t010&&(le(d,offset+10,2)!=1u||le(d,offset+12,2)!=t010Minor))return bad();
    const auto codeEnd=le(d,offset+24);
    if(le(d,offset+16)!=bytes||le(d,offset+20)!=0x20001400||codeEnd>bytes||le(d,offset+28)!=256||le(d,offset+52)!=192||le(d,offset+56)!=4096||le(d,offset+60)!=1024||le(d,offset+64)!=0||le(d,offset+68)!=20000000)return bad();
    const auto build=d.mid(offset+72,16);if(build==QByteArray(16,char(0))||build==QByteArray(16,char(0xff)))return bad();
    for(int i=0;i<5;++i){const auto entry=le(d,offset+32+i*4);if((entry&1)||entry<headerBytes||entry>=codeEnd)return bad();for(int j=0;j<i;++j)if(entry==le(d,offset+32+j*4))return bad();}
    const auto section=le(d,offset+88);if((section&1)||section<headerBytes||section>=codeEnd)return bad();
    for(int i=0;i<5;++i)if(section==le(d,offset+32+i*4))return bad();
    for(int i=92;i<128;++i)if(d[offset+i]!=0)return bad();
    error.clear();return true;
}
bool completeApplication(const FirmwareImage &app,quint32 base,quint32 capacity,bool t010,int slot,Plan &out,QString &error,bool slotContainer){
    auto bad=[&](const char *why){error=QString::fromUtf8(why);return false;};
    Q_UNUSED(capacity);
    if(app.segments.isEmpty())return bad("App image is empty");
    QByteArray complete;quint64 cursor=app.segments.first().address;
    for(const auto &segment:app.segments){
        if(segment.data.isEmpty()||segment.address!=cursor)return bad("App has a hole or overlap; bytes are never supplied by the host");
        cursor+=quint64(segment.data.size());complete+=segment.data;
    }
    if(complete.size()<4096||complete.left(8)!=(t010?QByteArray("T010APP1"):QByteArray("T008APP2")))return bad("App structural header does not match the target layout");
    const auto codeBytes=le(complete,8);
    if(!codeBytes||(codeBytes&0xfff))return bad("Compiler-provided codeBytes must be nonzero and 4 KiB aligned");
    const quint64 effectiveLength=4096ULL+codeBytes;
    const bool marked=complete.mid(ROM_MARKER_APP_OFFSET,8)=="S32K144T";
    quint64 transferLength=effectiveLength;
    if(marked){
        RomMarker marker;
        if(RomMarker_Decode(reinterpret_cast<const uint8_t *>(complete.constData()+ROM_MARKER_APP_OFFSET),&marker)!=0||marker.kind!=ROM_MARKER_APP)
            return bad("Invalid App length marker");
        transferLength=marker.imageBytes;
        if(transferLength<effectiveLength||(transferLength&4095u))return bad("App marker length does not cover its code within the file");
    }
    if(quint64(complete.size())!=transferLength){
        if(!slotContainer||transferLength>quint64(complete.size()))return bad("App coverage does not match its declared package length");
        for(quint64 i=transferLength;i<quint64(complete.size());++i)if(quint8(complete[int(i)])!=0xff)return bad("App explicit container tail must be FF");
    }
    for(quint64 i=effectiveLength;i<transferLength;++i)if(quint8(complete[int(i)])!=0xff)return bad("App bytes beyond code extent must be FF");
    if((t010&&(slot<0||slot>1||quint8(complete[87])!=slot))||(!t010&&quint8(complete[87])!=0xff))return bad("App slot marker does not match its linked address");
    if(t010){
        if(complete.size()<4160)return bad("App vector table is missing");
        const quint32 stack=le(complete,4096),reset=le(complete,4100);
        if(stack<=0x1fff9000||stack>0x20001400||(stack&7u)||!(reset&1u))return bad("Invalid App stack or reset vector");
        // Check the reset vector against the handlers in this file. Its UDS
        // destination is supplied independently by the operator.
        quint32 low=reset&~1u,high=low;
        for(int i=2;i<16;++i){
            const quint32 entry=le(complete,4096+4*i);
            if(!entry)continue;
            if(!(entry&1u))return bad("Invalid App handler vector");
            low=qMin(low,entry&~1u);high=qMax(high,entry&~1u);
        }
        if(quint64(high)-low>=codeBytes)return bad("App vectors exceed the declared code extent");
    }
    if(!marked&&!t010)return bad("App 64-byte marker missing");
    if(marked){
        RomMarker marker;
        // Validate exactly the bytes returned below; a container CRC cannot
        // authorize trimming a marker that still declares the whole container.
        if(!FirmwareImage::validateMarker({base,complete.left(int(transferLength))},ROM_MARKER_APP,marker,error))return false;
    }
    const int reservedEnd=marked?int(ROM_MARKER_APP_OFFSET):4096;
    for(int i=88;i<reservedEnd;++i)if(quint8(complete[i])!=0xff)return bad("App descriptor reserved bytes must be FF");
    // 31 still erases the exact slot. Only a validated BIN FF container tail
    // is omitted from 34; the caller's original file and HEX bytes stay intact.
    if(slotContainer)complete.truncate(int(transferLength));
    FirmwareImage selected;selected.segments={{base,complete}};Plan parsed;
    if(!images(selected,{},parsed,error))return false;
    parsed.application=selected.segments;out=std::move(parsed);error.clear();return true;
}
bool response(const QByteArray &request,const QByteArray &pdu,bool suppressed,QString &error){
    if(request.isEmpty()){error="T008 empty request";return false;}
    const int sid=quint8(request[0]);int length=-1;QByteArray prefix;
    bool requestValid=false;
    switch(sid){
    case 0x10:case 0x11:case 0x85:requestValid=request.size()==2;break;
    case 0x14:requestValid=request==QByteArray::fromHex("14ffffff");break;
    case 0x22:requestValid=request.size()==3;break;
    case 0x27:requestValid=(request.size()==2&&(quint8(request[1])&1))||(request.size()==18&&!(quint8(request[1])&1));break;
    case 0x28:requestValid=request.size()==3;break;
    case 0x2e:requestValid=request.size()==22&&request.left(3)==QByteArray::fromHex("2ef184");break;
    case 0x31:
        // The RID is selected in download settings, not by this response check.
        requestValid=request.startsWith(QByteArray::fromHex("3101"))&&
            (request.size()==4||request.size()==16||(request.size()==13&&quint8(request[4])==0x44));
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
    case 0x22:length=request.mid(1)==QByteArray::fromHex("f180")?17:request.mid(1)==QByteArray::fromHex("f189")?12:pdu.size()>3?pdu.size():-1;prefix=QByteArray(1,char(0x62))+request.mid(1);break;
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
