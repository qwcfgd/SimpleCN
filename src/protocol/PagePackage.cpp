#include "PagePackage.h"
#include "S32kLayout.h"
#include <QtEndian>
#include <algorithm>

namespace boot::page {
namespace {
constexpr int Page = 4096, Marker = Page - 1024;

bool reject(QString &error, const QString &why) {
    error = QStringLiteral("S32K144 CRC32 · ") + why;
    return false;
}
bool filled(const QByteArray &data, int start, int end, char value) {
    return std::all_of(data.cbegin()+start, data.cbegin()+end, [value](char c){ return c == value; });
}
bool recordHint(const QByteArray &data, int at) {
    if (data.size() < at+16) return false;
    const auto *p = reinterpret_cast<const quint8 *>(data.constData()+at);
    return data.mid(at,8)=="S32K144T" || RomMarker_Kind(p+8)!=0;
}
bool readRecord(const QByteArray &data, int at, quint32 &kind, quint32 &length, QString &error) {
    if (data.size()<at+64) return reject(error,"Incomplete integrity marker");
    const auto *p = reinterpret_cast<const quint8 *>(data.constData()+at);
    kind = RomMarker_Kind(p+8);
    if (!kind || data.mid(at,8)!="S32K144T" || !RomMarker_Date(p+24))
        return reject(error,"Invalid project, role or date");
    for (int i=16; i<20; ++i)
        if (!(p[i]>='A' && p[i]<='Z') && !(p[i]>='0' && p[i]<='9'))
            return reject(error,"Invalid version");
    const auto fingerprint = QByteArray::number(qulonglong(RomMarker_Crc(0,p+24,8)>>48),16).rightJustified(4,'0').toUpper();
    if (data.mid(at+20,4)!=fingerprint) return reject(error,"Invalid version date fingerprint");
    length=0;
    for (int i=32; i<40; ++i) {
        const int digit=RomMarker_Hex(p[i]);
        if (digit<0) return reject(error,"Length requires eight uppercase ASCII HEX digits");
        length=(length<<4)|quint32(digit);
    }
    if (!filled(data,at+40,at+56,0)) return reject(error,"Reserved marker bytes must be zero");
    if (crc32(data.mid(at,60))!=qFromBigEndian<quint32>(p+60)) return reject(error,"Marker CRC32 mismatch");
    return true;
}
bool member(const QByteArray &data, quint32 kind, quint32 &base, QString &error) {
    const bool driver=kind==ROM_MARKER_DRIVER;
    const int prefix=driver?1024:Page;
    if (data.size()<=prefix || (!driver && data.size()%Page)) return reject(error,"Invalid body size or page alignment");
    const auto *p=reinterpret_cast<const quint8 *>(data.constData());
    if (crc32(data.mid(prefix))!=qFromBigEndian<quint32>(p+prefix-1024+56)) return reject(error,"Body CRC32 mismatch");
    if (driver) {
        base=0;
        return true;
    }
    const bool app=kind==ROM_MARKER_APP;
    if (!filled(data,app?88:0,Marker,char(0xff))) return reject(error,"Invalid descriptor padding");
    if (kind==ROM_MARKER_CAL) {
        base=0;
        if (quint32(data.size())!=8192u) return reject(error,"Invalid Calib size");
        return true;
    }
    for (int i=Page; i<data.size(); i+=Page)
        if (filled(data,i,i+Page,char(0xff)))
            return reject(error,QString("%1 has an all-FF page at +0x%2").arg(app?"App":"Boot").arg(i,0,16));
    const quint32 stack=qFromLittleEndian<quint32>(p+Page);
    const quint32 reset=qFromLittleEndian<quint32>(p+Page+4);
    const quint32 entry=reset & ~1u;
    if (app) {
        if (data.left(8)!="T010APP1") return reject(error,"App project descriptor mismatch");
        const quint32 code=qFromLittleEndian<quint32>(p+8);
        if (code!=quint32(data.size()-Page)) return reject(error,"Invalid App descriptor body length");
        if (qFromLittleEndian<quint16>(p+12)>999) return reject(error,"Invalid minimum Boot version");
        for (const auto &field : {data.mid(14,16),data.mid(30,16),data.mid(46,9)}) {
            const int nul=field.indexOf(char(0));
            const int n=nul>=0?nul:int(field.size());
            if (!n || !filled(field,n,field.size(),0) || !std::all_of(field.cbegin(),field.cbegin()+n,
                [](char c){return c>=0x20 && c<=0x7e;})) return reject(error,"Invalid App identity");
        }
        for (const int at : {55,71})
            for (const char padding : {char(0),char(0xff),char(' ')})
                if (filled(data,at,at+16,padding)) return reject(error,"Empty App dependency ID");
        const quint8 slot=p[87];
        if (slot>1) return reject(error,"Invalid App slot tag");
        // Check vectors against the link origin encoded by the file slot, independently of the request.
        base=slot==1 ? 0x4b000u : 0x18000u;
        if (!s32kAppVectors(stack,reset,base,data.size())) return reject(error,"App vectors outside its linked body");
    } else {
        base=0x1000u;
    }
    if (stack<=0x1fff8000 || stack>0x20008000 || (stack&7) || !(reset&1) || entry<base+Page ||
        entry>=base+quint32(data.size())) return reject(error,"Invalid linked vectors");
    return true;
}
}

bool resembles(const FirmwareImage &image) {
    for (const auto &s:image.segments) {
        if (recordHint(s.data,Marker) && (s.data.startsWith("T010APP1") ||
            (s.data.size()>=16 && filled(s.data,0,16,char(0xff))) ||
            (s.data.size()>=Marker+64 && filled(s.data,Marker+40,Marker+56,0)))) return true;
        // Legacy driver lengths end with eight ASCII digits at offset 40.
        if (recordHint(s.data,0) && s.data.size()>=56 &&
            !std::all_of(s.data.cbegin()+40,s.data.cbegin()+48,[](char c){return RomMarker_Hex(quint8(c))>=0;}))
            return true;
    }
    return false;
}
bool inspect(const FirmwareImage &image, quint32 &kind, QVector<Segment> &members, QString &error) {
    kind=0;
    members.clear();
    if (image.segments.isEmpty() || (image.binary && image.segments.size()!=1)) return reject(error,"Empty or fragmented file");
    QVector<Segment> result;
    QByteArray stage,appIdentity;
    for (const auto &seg:image.segments) {
        if (seg.data.isEmpty()) return reject(error,"Empty segment");
        int cursor=0;
        while (cursor<seg.data.size()) {
            const int remaining=int(seg.data.size())-cursor;
            const bool driver=seg.data.mid(cursor,8)=="S32K144T";
            const int offset=driver?0:Marker;
            quint32 role=0,length=0;
            if (remaining<offset+64) return reject(error,"Truncated member");
            if (!readRecord(seg.data,cursor+offset,role,length,error)) return false;
            if (driver!=(role==ROM_MARKER_DRIVER) || (kind && kind!=role)) return reject(error,"Wrong or mixed roles");
            if (!length || length>quint32(remaining) || (!image.binary && length!=quint32(remaining)))
                return reject(error,"Declared length does not match continuous member");
            if (result.size()>=2) return reject(error,"Unexpected trailing member");
            const auto bytes=seg.data.mid(cursor,int(length));
            quint32 base=0;
            if (!member(bytes,role,base,error)) return false;
            if (!image.binary && (role==ROM_MARKER_APP || role==ROM_MARKER_BOOT) && seg.address!=base) return reject(error,"HEX address disagrees with linked slot");
            const auto version=bytes.mid(offset+16,4);
            if (!result.isEmpty() && (base<=result.last().address || version!=stage ||
                role!=ROM_MARKER_APP))
                return reject(error,"Duplicate, reversed or incompatible slots");
            if (role==ROM_MARKER_APP) {
                const auto identity=bytes.mid(12,75);
                if (!appIdentity.isEmpty() && identity!=appIdentity) return reject(error,"A/B App descriptor identities differ");
                appIdentity=identity;
            }
            result.append({image.binary ? base : seg.address,bytes});
            kind=role;
            stage=version;
            cursor+=int(length);
        }
    }
    members=result;
    error.clear();
    return true;
}

}
