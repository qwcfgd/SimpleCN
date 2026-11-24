#include "FirmwareImage.h"
#include "S32kLayout.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QMap>
#include <algorithm>
namespace boot {
QByteArray be32(quint32 n){QByteArray b;for(int i=3;i>=0;--i)b.append(char(n>>(8*i)));return b;}
quint32 readBe(const QByteArray &b){quint32 n=0;for(unsigned char c:b)n=(n<<8)|c;return n;}
quint32 crc32(const QByteArray &b){quint32 c=0xffffffff;for(unsigned char v:b){c^=v;for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return c^0xffffffff;}
namespace {
quint32 little32(const QByteArray &bytes,int at){
    return quint32(quint8(bytes[at]))|(quint32(quint8(bytes[at+1]))<<8)|
        (quint32(quint8(bytes[at+2]))<<16)|(quint32(quint8(bytes[at+3]))<<24);
}
// Structural evidence for a linked image, not an integrity/security verdict.
// A B address only makes an alternate available; it never proves two images.
bool linkedBin(const QByteArray &bytes,quint32 base){
    if(bytes.isEmpty()||(bytes.size()&4095))return false;
    // Markers prove content; linked vectors independently prove the target slot.
    RomMarker marker;int vectorOffset=0;quint32 vectorLimit=quint32(bytes.size());
    const auto *raw=reinterpret_cast<const uint8_t *>(bytes.constData());
    if(bytes.size()>=4096 && RomMarker_Decode(raw+ROM_MARKER_APP_OFFSET,&marker)==0 && marker.kind==ROM_MARKER_APP){
        const auto imageLimit=marker.imageBytes;
        const quint64 codeEnd=4096ULL+little32(bytes,8);
        if(imageLimit<8192u||(imageLimit&4095u)||imageLimit>quint32(bytes.size())||
           codeEnd<8192u||(codeEnd&4095u)||codeEnd>imageLimit)return false;
        vectorLimit=quint32(codeEnd);
        // A slot container retains the original prefix marker. Its FF tail is
        // transport padding, outside the declared image and linked vectors.
        if(vectorLimit<quint32(bytes.size())){
            for(int i=int(vectorLimit);i<bytes.size();++i)if(quint8(bytes[i])!=0xff)return false;
            if(RomMarker_Image(raw,imageLimit,ROM_MARKER_APP_OFFSET,0,4096,ROM_MARKER_APP,&marker)!=0)return false;
        }
        vectorOffset=4096;
    }else if((bytes.size()==0xf000||bytes.size()==0x14000) && RomMarker_Decode(raw+bytes.size()-64,&marker)==0 && marker.kind==ROM_MARKER_BOOT){
        if(marker.imageBytes!=quint32(bytes.size()))return false;
    }else vectorOffset=-1;
    if(vectorOffset<0){
        const int bootAt=bytes.size()==0xf000||bytes.size()==0x14000?bytes.size()-64:-1;
        if((bytes.size()>=4096&&bytes.mid(ROM_MARKER_APP_OFFSET,8)=="S32K144T")||
           (bootAt>=0&&bytes.mid(bootAt,8)=="S32K144T"))return false;
    }
    if(vectorOffset>=0){
        if(bytes.size()<vectorOffset+64)return false;
        const auto stack=little32(bytes,vectorOffset);int handlers=0;
        if(stack<=0x1fff8000||stack>0x20008000||(stack&7))return false;
        for(int i=1;i<16;++i){const auto vector=little32(bytes,vectorOffset+i*4);
            if(!vector&&i!=1)continue;
            const auto target=vector&~quint32(1);
            if(!(vector&1)||target<base+quint32(vectorOffset)||quint64(target)>=quint64(base)+vectorLimit)return false;
            ++handlers;
        }
        return handlers>=3;
    }

    const bool t008=bytes.startsWith("T008APP2"),t010=bytes.startsWith("T010APP1");
    if(t008||t010){
        if(bytes.size()<8192)return false;
        const quint32 code=little32(bytes,8);const quint64 effective=4096ULL+code;
        if(!code||(code&4095)||effective>quint64(bytes.size()))return false;
        if(t010&&(s32kT010Slot(base)<0||quint8(bytes[87])!=s32kT010Slot(base)))return false;
        if(t010&&quint64(bytes.size())>(base==S32kUnified80.appBase||base==S32kUnified80.t010AppB?S32kUnified80.t010AppBytes:S32kLegacy60.t010AppBytes))return false;
        if(t010&&!s32kAppVectors(little32(bytes,4096),little32(bytes,4100),base,quint32(effective)))return false;
        if(t008&&quint8(bytes[87])!=0xff)return false;
        for(int i=88;i<4096;++i)if(quint8(bytes[i])!=0xff)return false;
        for(quint64 i=effective;i<quint64(bytes.size());++i)if(quint8(bytes[int(i)])!=0xff)return false;
        return true;
    }
    // ARM vector evidence also handles Boot files without an App descriptor.
    const quint32 stack=little32(bytes,0);int handlers=0;
    if(stack<=0x1fff8000||stack>0x20008000||(stack&7))return false;
    for(int i=1;i<16;++i){
        const quint32 vector=little32(bytes,i*4);
        if(!vector&&i!=1)continue;
        const quint32 target=vector&~quint32(1);
        if(!(vector&1)||target<base||quint64(target)>=quint64(base)+quint64(bytes.size()))return false;
        ++handlers;
    }
    return handlers>=3;
}
bool declaredImage(const QByteArray &bytes,RomMarker &marker){
    const bool app=bytes.startsWith("T008APP2")||bytes.startsWith("T010APP1");
    for(int at:{int(ROM_MARKER_APP_OFFSET),int(ROM_MARKER_BOOT_OFFSET),0xf000-64}){
        if(app!=(at==int(ROM_MARKER_APP_OFFSET))||bytes.size()<at+64)continue;
        if(RomMarker_Decode(reinterpret_cast<const uint8_t *>(bytes.constData()+at),&marker)!=0)continue;
        if((app&&marker.kind==ROM_MARKER_APP)||(!app&&marker.kind==ROM_MARKER_BOOT&&marker.imageBytes==quint32(at+64)))return true;
    }
    return false;
}
bool linkedBoot(const QByteArray &bytes,quint32 base){
    const int at=bytes.size()-64;
    if(at<0)return false;
    const auto *raw=reinterpret_cast<const uint8_t *>(bytes.constData()+at);
    if(bytes.mid(at,8)=="S32K144T"||RomMarker_Kind(raw+8)!=0u){
        RomMarker marker;QString error;
        if(!FirmwareImage::validateMarker({base,bytes},ROM_MARKER_BOOT,marker,error))return false;
    }
    return linkedBin(bytes,base);
}
}
bool FirmwareImage::load(const QString &path,quint32 base,FirmwareImage &out,QString &error){
    out={};error.clear();FirmwareImage result;QFile file(path);
    auto fail=[&](const QString &why){error=why;return false;};
    if(!file.open(QIODevice::ReadOnly))return fail("Cannot open image: "+file.errorString());
    constexpr qint64 limit=64*1024*1024;
    if(file.size()<=0||file.size()>limit)return fail("Image must contain 1..64 MiB");
    const QByteArray raw=file.readAll();
    if(file.error()!=QFileDevice::NoError)return fail(file.errorString());
    const auto suffix=QFileInfo(path).suffix().toLower();
    if(suffix=="bin"){
        result.binary=true;
        if(quint64(base)+quint64(raw.size())>0x100000000ULL)return fail("BIN address overflow");
        result.segments.append({base,raw});
    }else if(suffix=="hex"){
        QMap<quint32,QByteArray> records;quint64 upper=0;bool eof=false;int lineNo=0;
        for(QByteArray line:raw.split('\n')){
            ++lineNo;while(line.endsWith('\r'))line.chop(1);
            if(line.isEmpty())continue;
            auto bad=[&](const QString &why){return fail(QString("HEX line %1: %2").arg(lineNo).arg(why));};
            if(!line.startsWith(':')||line.size()<11||((line.size()-1)%2))return bad("Invalid record");
            for(int i=1;i<line.size();++i)if(!((line[i]>='0'&&line[i]<='9')||(line[i]>='A'&&line[i]<='F')||(line[i]>='a'&&line[i]<='f')))return bad("Non-hex digit");
            const auto b=QByteArray::fromHex(line.mid(1));const int n=quint8(b[0]);int sum=0;
            if(b.size()!=n+5)return bad("Byte count mismatch");
            for(unsigned char c:b)sum+=c;
            if(sum&255)return bad("Checksum mismatch");
            const quint32 offset=(quint8(b[1])<<8)|quint8(b[2]);const int type=quint8(b[3]);const auto data=b.mid(4,n);
            // Some production linkers append an empty type-08 terminator after the
            // standard EOF record. Accept only that exact, checksum-valid trailer.
            if(eof){if(type==8&&n==0&&!offset)continue;return bad("Data after EOF");}
            if(type==0){
                if(n==0)continue;
                const quint64 start=upper+offset,end=start+n;
                if(offset+quint32(n)>0x10000||end>0x100000000ULL)return bad("Address overflow");
                auto next=records.lowerBound(quint32(start));
                if(next!=records.end()&&quint64(next.key())<end)return bad("Overlapping data");
                if(next!=records.begin()){auto prev=next;--prev;if(quint64(prev.key())+quint64(prev.value().size())>start)return bad("Overlapping data");}
                records.insert(quint32(start),data);
            }else if(type==1){if(n!=0||offset)return bad("Invalid EOF");eof=true;}
            else if(type==2||type==4){if(n!=2||offset)return bad("Invalid extended address");upper=quint64(readBe(data))<<(type==2?4:16);}
            else if(type==3||type==5){if(n!=4||offset)return bad("Invalid entry address");}
            else return bad("Unsupported record type");
        }
        if(!eof)return fail("HEX EOF missing");
        for(auto it=records.cbegin();it!=records.cend();++it){
            if(!result.segments.isEmpty()&&quint64(result.segments.last().address)+quint64(result.segments.last().data.size())==it.key())result.segments.last().data+=it.value();
            else result.segments.append({it.key(),it.value()});
        }
    }else return fail("Only BIN and Intel HEX are supported");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(const auto &s:result.segments){result.size+=s.data.size();hash.addData(be32(s.address));hash.addData(be32(quint32(s.data.size())));hash.addData(s.data);}
    if(!result.size)return fail("Image has no data");
    result.sha256=hash.result();out=std::move(result);return true;
}
// Validate complete ranges before a protocol request can erase or write memory.
bool FirmwareImage::planSlots(const FirmwareImage &image,quint32 base,quint32 b,bool hasB,quint32 length,QVector<Segment> &primary,QVector<Segment> &alternate,QString &error){
    primary.clear();alternate.clear();error.clear();
    auto bad=[&](const char *why){error=QString::fromLatin1(why);return false;};
    const quint64 end=quint64(base)+length,bEnd=quint64(b)+length;
    if(!length||(base&0xfff)||(length&0xfff)||(hasB&&(b&0xfff))||end>0x100000000ULL||(hasB&&(bEnd>0x100000000ULL||(quint64(base)<bEnd&&quint64(b)<end))))return bad("Invalid, overflowing or overlapping slot address/length");
    if(image.segments.isEmpty())return bad("Target image is empty");
    if(image.binary){
        if(image.segments.size()!=1)return bad("BIN must contain one file payload");
        const auto &data=image.segments.first().data;
        if(data.isEmpty())return bad("BIN is empty");
        const int half=data.size()/2;
        const bool single=quint64(data.size())<=length&&(!hasB||linkedBin(data,base));
        const bool singleB=hasB&&quint64(data.size())<=length&&linkedBin(data,b);
        RomMarker first,second;const bool marked=declaredImage(data,first);
        const bool declaredPair=!marked||(quint64(first.imageBytes)<=quint64(half)&&declaredImage(data.mid(half),second)&&first.kind==second.kind&&first.imageBytes==second.imageBytes);
        const bool dual=hasB&&!(data.size()%2)&&quint64(half)<=length&&declaredPair&&
            linkedBin(data.left(half),base)&&linkedBin(data.mid(half),b);
        if((!marked&&single&&dual)||(singleB&&(single||dual)))return bad("BIN layout is ambiguous: both single and dual images fit; use address-linked HEX or clear B for an explicit single image");
        if(dual){primary.append({base,data.left(half)});alternate.append({b,data.mid(half)});}
        else if(single)primary.append({base,data});
        else if(singleB)primary.append({b,data});
        else return bad("BIN layout/range cannot be established: B is not proof of a dual image; use address-linked HEX or clear B for a single image");
    }else{
        for(const auto &seg:image.segments){
            const quint64 segEnd=quint64(seg.address)+quint64(seg.data.size());
            if(seg.data.isEmpty()||segEnd>0x100000000ULL)return bad("Invalid HEX segment");
            quint64 cursor=seg.address;
            while(cursor<segEnd){
                QVector<Segment> *destination=nullptr;quint64 slotEnd=0;
                if(cursor>=base&&cursor<end){destination=&primary;slotEnd=end;}
                else if(hasB&&cursor>=b&&cursor<bEnd){destination=&alternate;slotEnd=bEnd;}
                else return bad("HEX data outside configured slots; absolute addresses are never relocated");
                const quint64 chunkEnd=qMin(segEnd,slotEnd);
                destination->append({quint32(cursor),seg.data.mid(int(cursor-seg.address),int(chunkEnd-cursor))});cursor=chunkEnd;
            }
        }
        QVector<Segment> all=primary;all+=alternate;
        for(int i=0;i<all.size();++i)for(int j=0;j<i;++j)
            if(quint64(all[i].address)<quint64(all[j].address)+all[j].data.size()&&quint64(all[j].address)<quint64(all[i].address)+all[i].data.size())return bad("Overlapping HEX segments");
        // A HEX containing only B is already linked for B. Keep its absolute addresses.
        if(primary.isEmpty()){primary=alternate;alternate.clear();}
    }
    const auto byAddress=[](const Segment &a,const Segment &b){return a.address<b.address;};
    std::sort(primary.begin(),primary.end(),byAddress);std::sort(alternate.begin(),alternate.end(),byAddress);
    // 31 owns the erase capacity. Each 34 owns only the continuous bytes present
    // in its slot; never fill a HEX hole or trim/pad a BIN to the erase length.
    for(auto *slot:{&primary,&alternate}){
        if(slot->isEmpty())continue;
        const quint32 slotBase=slot->first().address==base?base:b;
        if(slot->first().address!=slotBase||(!hasB&&slotBase!=base))return bad("HEX must start at its configured slot address");
        QByteArray bytes;quint64 cursor=slotBase;
        for(const auto &seg:*slot){
            if(seg.address!=cursor)return bad("HEX slot must be continuous from its configured start; holes are not filled");
            bytes+=seg.data;cursor+=quint64(seg.data.size());
        }
        *slot={{slotBase,bytes}};
    }
    if(!image.binary&&!primary.isEmpty()&&!alternate.isEmpty()){
        RomMarker first,second;
        if(declaredImage(primary.first().data,first)&&declaredImage(alternate.first().data,second)&&
           (first.kind!=second.kind||first.imageBytes!=second.imageBytes))return bad("A/B package marker lengths differ");
    }
    for(const auto &seg:primary)if((seg.address&0xfff)||(seg.data.size()&0xfff))return bad("ROM RequestDownload address and length must be 4 KiB aligned; pad in compiler packaging, never relocate or pad here");
    for(const auto &seg:alternate)if((seg.address&0xfff)||(seg.data.size()&0xfff))return bad("ROM RequestDownload address and length must be 4 KiB aligned; pad in compiler packaging, never relocate or pad here");
    return true;
}
bool FirmwareImage::loadTarget(const QString &path,quint32 base,quint32 b,bool hasB,quint32 length,FirmwareImage &out,QString &error){
    FirmwareImage parsed;if(!load(path,QFileInfo(path).suffix().compare("bin",Qt::CaseInsensitive)==0?0:base,parsed,error))return false;
    if(parsed.binary){
        parsed.segments[0].address=base;QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(be32(base));hash.addData(be32(quint32(parsed.segments[0].data.size())));hash.addData(parsed.segments[0].data);parsed.sha256=hash.result();
    }
    QVector<Segment> a,alternate;if(!planSlots(parsed,base,b,hasB,length,a,alternate,error))return false;
    // Retain the source format and original bytes for reproducible snapshots.
    out=std::move(parsed);return true;
}
bool FirmwareImage::hasCurrentMarker(const FirmwareImage &image){
    for(const auto &part:image.segments)for(int offset:{0,int(ROM_MARKER_APP_OFFSET),int(0xf000-ROM_MARKER_BYTES),int(0x1e000-ROM_MARKER_BYTES),int(ROM_MARKER_BOOT_OFFSET),int(0x28000-ROM_MARKER_BYTES)}){
        if(part.data.size()<offset+int(ROM_MARKER_BYTES))continue;
        const auto *raw=reinterpret_cast<const uint8_t *>(part.data.constData()+offset);
        // Recognition only: a damaged project must still enter strict marker
        // validation rather than being offered the legacy/manual fallback.
        if(memcmp(raw,"S32K144T",8u)==0||RomMarker_Kind(raw+8u)!=0u)return true;
    }
    return false;
}
bool FirmwareImage::validateMarkerSet(const QVector<QPair<Segment,quint32>> &parts,QString &error,bool appBinContainer){
    RomMarker reference{};bool haveReference=false;
    for(const auto &part:parts){RomMarker marker;
        if(!validateMarker(part.first,part.second,marker,error)){
            const auto &data=part.first.data;const bool t010=data.startsWith("T010APP1");
            if(!appBinContainer||part.second!=ROM_MARKER_APP||data.size()<8192||(!t010&&!data.startsWith("T008APP2"))||
               !s32kApplicationLayout(t010,part.first.address,quint32(data.size()))||
               RomMarker_Decode(reinterpret_cast<const uint8_t *>(data.constData()+ROM_MARKER_APP_OFFSET),&marker)!=0||marker.kind!=ROM_MARKER_APP)return false;
            const quint64 effective=marker.imageBytes,codeEnd=4096ULL+little32(data,8);
            if(effective<8192u||(effective&4095u)||effective>=quint64(data.size())||effective<codeEnd)return false;
            for(quint64 i=effective;i<quint64(data.size());++i)if(quint8(data[int(i)])!=0xff)return false;
            if(!validateMarker({part.first.address,data.left(int(effective))},ROM_MARKER_APP,marker,error))return false;
        }
        if(haveReference&&!RomMarker_Match(&reference,&marker)){error="Selected ROM project, hardware or software stage does not match";return false;}
        reference=marker;haveReference=true;
    }
    error.clear();return true;
}
bool FirmwareImage::validateMarker(const Segment &segment,quint32 kind,RomMarker &marker,QString &error){
    const auto size=quint32(segment.data.size());quint32 at=0,skip=0,skipBytes=64;
    if(kind==ROM_MARKER_APP){at=ROM_MARKER_APP_OFFSET;skipBytes=4096;}
    else if(kind==ROM_MARKER_BOOT){at=skip=size==0xf000?0xf000-64:ROM_MARKER_BOOT_OFFSET;}
    else if(kind==ROM_MARKER_CAL){if(size<64){error="Calib marker missing";return false;}at=skip=size-64;}
    if(RomMarker_Image(reinterpret_cast<const uint8_t *>(segment.data.constData()),size,at,skip,skipBytes,kind,&marker)!=0){
        error="Invalid ROM marker, actual package length or CRC64";return false;
    }
    error.clear();return true;
}
bool FirmwareImage::validateCalibration(const FirmwareImage &image,quint32 base,quint32 length,QString &error){
    QVector<Segment> data,alternate;if(!planSlots(image,base,0,false,length,data,alternate,error))return false;
    if(data.size()!=1||data[0].address!=base){error="Calib requires continuous content at the configured address";return false;}
    RomMarker marker;return validateMarker(data[0],ROM_MARKER_CAL,marker,error);
}
bool FirmwareImage::loadCalibration(const QString &path,quint32 base,quint32 length,FirmwareImage &out,QString &error){
    FirmwareImage image;if(!load(path,base,image,error)||!validateCalibration(image,base,length,error))return false;out=std::move(image);return true;
}
FirmwareImage::Kind FirmwareImage::identify(const FirmwareImage &image){
    if(image.segments.isEmpty())return Kind::Unknown;
    const auto &first=image.segments.first();RomMarker marker;QString error;
    if(validateMarker(first,ROM_MARKER_DRIVER,marker,error))return Kind::Driver;
    if(validateMarker(first,ROM_MARKER_APP,marker,error))return Kind::Application;
    if(image.binary&&first.data.size()>=4096&&
       (first.data.startsWith("T008APP2")||first.data.startsWith("T010APP1"))&&
       RomMarker_Decode(reinterpret_cast<const uint8_t *>(first.data.constData()+ROM_MARKER_APP_OFFSET),&marker)==0&&
       marker.kind==ROM_MARKER_APP&&marker.imageBytes>=4096u&&marker.imageBytes<quint32(first.data.size())){
        bool ff=true;for(int i=int(marker.imageBytes);i<first.data.size();++i)if(quint8(first.data[i])!=0xff){ff=false;break;}
        if(ff&&validateMarker({first.address,first.data.left(int(marker.imageBytes))},ROM_MARKER_APP,marker,error))return Kind::Application;
    }
    if(image.binary&&first.data.startsWith("T010APP1")&&declaredImage(first.data,marker)){
        for(const auto *layout:{&S32kLegacy60,&S32kUnified80}){
            QVector<Segment> a,b;
            if(planSlots(image,layout->appBase,layout->t010AppB,true,layout->t010AppBytes,a,b,error)&&!a.isEmpty()&&!b.isEmpty()&&
               validateMarkerSet({{a.first(),ROM_MARKER_APP},{b.first(),ROM_MARKER_APP}},error,true))return Kind::Application;
        }
        return Kind::Unknown;
    }
    if(first.data.size()==4096&&validateMarker(first,ROM_MARKER_CAL,marker,error))return Kind::Calibration;
    for(const auto *layout:{&S32kLegacy60,&S32kUnified80}){
        const auto slotSize=layout->bootBytes,slotA=quint32(0x5000),slotB=layout->bootB;
        if(image.segments.size()==2&&first.address==slotA&&image.segments[1].address==slotB&&
           first.data.size()==int(slotSize)&&image.segments[1].data.size()==int(slotSize)&&
           linkedBoot(first.data,slotA)&&linkedBoot(image.segments[1].data,slotB))return Kind::BootBundle;
        if(image.segments.size()==1&&first.data.size()==int(slotSize)&&
           (linkedBoot(first.data,slotA)||linkedBoot(first.data,slotB)))return Kind::IncompleteBoot;
        if(image.segments.size()==1&&first.data.size()==int(2*slotSize)&&
           linkedBoot(first.data.left(slotSize),slotA)&&linkedBoot(first.data.mid(slotSize),slotB))return Kind::BootBundle;
    }
    if(hasCurrentMarker(image))return Kind::Unknown; // A corrupt current marker cannot fall back to legacy recognition.
    // Other target families keep their existing explicit/manual role workflow.
    if(first.data.startsWith("T008DRV1"))return Kind::Driver;
    if(first.data.startsWith("T008APP2")||first.data.startsWith("T010APP1"))return Kind::Application;
    return Kind::Unknown;
}
bool FirmwareImage::loadBootBundle(const QString &path,FirmwareImage &out,QString &error){
    FirmwareImage parsed;if(!load(path,0x5000,parsed,error))return false;
    bool valid=false;
    for(const auto *layout:{&S32kLegacy60,&S32kUnified80}){
        const int slotSize=int(layout->bootBytes);
        if(parsed.segments.size()==2&&parsed.segments[0].address==0x5000&&parsed.segments[0].data.size()==slotSize&&
           parsed.segments[1].address==layout->bootB&&parsed.segments[1].data.size()==slotSize&&
           linkedBoot(parsed.segments[0].data,0x5000)&&linkedBoot(parsed.segments[1].data,layout->bootB))valid=true;
        if(parsed.binary&&parsed.segments.size()==1&&parsed.segments[0].data.size()==2*slotSize&&
           linkedBoot(parsed.segments[0].data.left(slotSize),0x5000)&&
           linkedBoot(parsed.segments[0].data.mid(slotSize),layout->bootB))valid=true;
    }
    if(!valid){error="Boot bundle must contain two complete linked 60 KiB or 80 KiB slots";return false;}
    out=std::move(parsed);return true;
}
}
