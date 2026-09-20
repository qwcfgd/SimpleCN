#include "TraceReader.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtEndian>
#include <algorithm>
#include <cmath>
namespace host {
namespace {
quint8 b8(const QByteArray&b,int n){return quint8(b[n]);}
quint16 b16(const QByteArray&b,int n){return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(b.constData()+n));}
quint32 b32(const QByteArray&b,int n){return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(b.constData()+n));}
quint64 b64(const QByteArray&b,int n){return qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(b.constData()+n));}
const int lengths[]={0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
bool finish(FrameRecord &r,TraceLog &log){
    if(r.id>(r.bus==signal::Bus::Can?0x1fffffffu:61u)||r.length<0||r.length>(r.fd?64:8)||(!r.rtr&&r.payload.size()!=r.length)||r.timeUs<0){++log.skipped;return true;}
    if(log.frames.size()>=2000000){log.error="日志超过 2,000,000 帧加载上限";return false;}
    r.typed=true;r.identifier=QString("0x%1").arg(r.id,0,16).toUpper();r.data=QString::fromLatin1(r.payload.toHex(' ')).toUpper();
    if(!log.channels.contains(r.channel))log.channels.append(r.channel);
    log.frames.append(r);return true;
}
}
QString TraceReader::channelKey(signal::Bus bus,int n){return QString(bus==signal::Bus::Can?"CAN:%1":"LIN:%1").arg(n);}
TraceLog TraceReader::read(const QString &path){
    TraceLog log;QFile file(path);if(!file.open(QIODevice::ReadOnly)){log.error=file.errorString();return log;}
    if(file.size()>1024LL*1024*1024){log.error="日志文件超过 1 GiB 加载上限";return log;}
    if(QFileInfo(path).suffix().compare("asc",Qt::CaseInsensitive)==0){
        bool relative=false;int radix=16;qint64 clock=0;int lineNumber=0;
        while(!file.atEnd()){++lineNumber;const auto line=QString::fromUtf8(file.readLine()).trimmed();if(line.startsWith("base ")){relative=line.contains("timestamps relative");if(line.contains("base dec"))radix=10;continue;}
            const auto t=line.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);bool ok=false;const double sec=t.value(0).toDouble(&ok);if(!ok||!std::isfinite(sec))continue;
            FrameRecord r;r.timeUs=qRound64(sec*1e6);if(relative){clock+=r.timeUs;r.timeUs=clock;}int start=0,ch=0;QString id;
            if(t.value(1)=="CANFD"){r.fd=true;ch=t.value(2).toInt();const bool directionFirst=t.value(3).compare("Rx",Qt::CaseInsensitive)==0||t.value(3).compare("Tx",Qt::CaseInsensitive)==0;
                id=t.value(directionFirst?4:3);r.direction=t.value(directionFirst?3:4);int flags=5;
                // Vector may include an optional frame-name token between ID and flags.
                t.value(flags).toInt(&ok);if(!ok)++flags;
                r.brs=t.value(flags)=="1";r.esi=t.value(flags+1)=="1";
                r.length=t.value(directionFirst?flags+3:flags+4).toInt(&ok);start=directionFirst?flags+4:flags+5;
            }else if(t.value(1).startsWith('L',Qt::CaseInsensitive)){r.bus=signal::Bus::Lin;ch=t.value(1).mid(1).toInt();id=t.value(2);r.direction=t.value(3);r.length=t.value(4).toInt(&ok);start=5;}
            else {ch=t.value(1).toInt();id=t.value(2);r.direction=t.value(3);if(t.value(4)!="d"&&t.value(4)!="r"){++log.skipped;continue;}r.rtr=t.value(4)=="r";r.length=t.value(5).toInt(&ok);start=6;}
            if(!ok||ch<1||r.length<0||r.length>(r.fd?64:8)){++log.skipped;continue;}
            r.extended=id.endsWith('x',Qt::CaseInsensitive);if(r.extended)id.chop(1);r.id=id.toUInt(&ok,radix);if(!ok){++log.skipped;continue;}
            if(!r.rtr)for(int i=0;i<r.length;++i){const auto value=t.value(start+i).toUInt(&ok,radix);if(!ok||value>255)break;r.payload.append(char(value));}
            r.channel=channelKey(r.bus,ch);r.classicChecksum=r.bus==signal::Bus::Lin&&r.id>=60;
            if(!finish(r,log))return log;
        }
    }else if(QFileInfo(path).suffix().compare("blf",Qt::CaseInsensitive)==0){
        auto head=file.read(144);if(head.size()<72||head.left(4)!="LOGG"||b32(head,4)<72||b32(head,4)>file.size()){log.error="BLF 文件头无效";return log;}
        file.seek(b32(head,4));QByteArray pending;quint64 expanded=0;
        while(!file.atEnd()){
            const auto h=file.read(16);if(h.isEmpty())break;if(h.size()!=16||h.left(4)!="LOBJ"){log.error="BLF 容器结构无效";return log;}
            const quint32 size=b32(h,8);if(size<16||size>64*1024*1024||quint64(size-16)>quint64(file.bytesAvailable())){log.error="BLF 容器长度无效";return log;}
            const auto body=file.read(size-16);file.seek(file.pos()+size%4);if(b32(h,12)!=10){++log.skipped;continue;}
            if(body.size()<16){log.error="BLF 压缩头缺失";return log;}const auto rawSize=b32(body,8);expanded+=rawSize;
            if(rawSize>64*1024*1024||expanded>1024ULL*1024*1024){log.error="BLF 解压数据超过上限";return log;}
            QByteArray raw;if(b16(body,0)==0)raw=body.mid(16);else if(b16(body,0)==2){QByteArray packed(4,0);qToBigEndian(rawSize,reinterpret_cast<uchar*>(packed.data()));packed+=body.mid(16);raw=qUncompress(packed);}
            else{log.error="不支持的 BLF 压缩方式";return log;}
            if(raw.size()!=rawSize){log.error="BLF 容器解压长度不匹配";return log;}pending+=raw;int pos=0;
            while(pos+16<=pending.size()){
                // Object padding differs between object kinds; only zero padding is accepted.
                if(pending.mid(pos,4)!="LOBJ"){if(pending.at(pos)==char(0)){++pos;continue;}log.error="BLF 对象签名无效";return log;}
                const auto objectSize=b32(pending,pos+8),type=b32(pending,pos+12);const int header=b16(pending,pos+4);
                if(objectSize<quint32(header)||header<16||objectSize>64*1024*1024){log.error="BLF 对象长度无效";return log;}if(quint64(pos)+objectSize>quint64(pending.size()))break;
                const auto o=pending.mid(pos,objectSize);pos+=objectSize;
                if(header<32){++log.skipped;continue;}FrameRecord r;const auto stamp=b64(o,24);r.timeUs=(b32(o,16)&1)?qint64(stamp)*10:qint64(stamp/1000);const int a=header;int ch=0;bool tx=false,known=true;
                if((type==1||type==86||type==100)&&o.size()>=a+(type==100?84:16)){
                    ch=b16(o,a);tx=b8(o,a+2)&1;r.rtr=b8(o,a+2)&128;r.extended=b32(o,a+4)&0x80000000;r.id=b32(o,a+4)&0x1fffffff;
                    if(type==100){r.fd=b8(o,a+13)&1;r.length=lengths[b8(o,a+3)&15];r.brs=b8(o,a+13)&2;r.esi=b8(o,a+13)&4;r.payload=o.mid(a+20,qMin(int(b8(o,a+14)),r.length));}
                    else{r.length=b8(o,a+3);if(!r.rtr)r.payload=o.mid(a+8,r.length);}
                }else if(type==101&&o.size()>=a+40){
                    ch=b8(o,a);r.length=lengths[b8(o,a+1)&15];r.id=b32(o,a+4)&0x1fffffff;r.extended=b32(o,a+4)&0x80000000;const auto flags=b32(o,a+12);r.fd=flags&0x1000;r.brs=flags&0x2000;r.esi=flags&0x4000;r.rtr=flags&0x10;tx=b8(o,a+34)!=0;if(!r.rtr)r.payload=o.mid(a+40,qMin(r.length,int(b8(o,a+2))));
                }else if(type==11&&o.size()>=a+24){r.bus=signal::Bus::Lin;ch=b16(o,a);r.id=b8(o,a+2);r.length=b8(o,a+3);r.payload=o.mid(a+4,r.length);r.checksum=b16(o,a+16);tx=b8(o,a+18)!=0;r.classicChecksum=r.id>=60;}
                else if(type==57&&o.size()>=a+124){r.bus=signal::Bus::Lin;ch=b16(o,a+12);r.id=b8(o,a+37);r.length=b8(o,a+38);r.classicChecksum=b8(o,a+39)==0;r.payload=o.mid(a+112,r.length);r.checksum=b16(o,a+120);tx=b8(o,a+122)!=0;}
                else known=false;
                if(!known){++log.skipped;continue;}r.channel=channelKey(r.bus,ch);r.direction=tx?"Tx":"Rx";if(!finish(r,log))return log;
            }pending.remove(0,pos);
        }
        for(char c:pending)if(c!=0){log.error="BLF 末尾对象不完整";return log;}
    }else{log.error="仅支持 ASC 和 BLF";return log;}
    std::stable_sort(log.frames.begin(),log.frames.end(),[](const auto&a,const auto&b){return a.timeUs<b.timeUs;});
    if(log.frames.isEmpty()){log.error="日志中没有可回放的正常 CAN/LIN 帧";return log;}
    const auto origin=log.frames.first().timeUs;for(auto &r:log.frames)r.timeUs-=origin;
    log.durationUs=qMax(qint64(1000),log.frames.last().timeUs+1000);return log;
}
}
