#include "TraceExporter.h"
#include "model/SignalCodec.h"
#include <QDataStream>
#include <QFileInfo>
#include <QLocale>
#include <QMap>
#include <QSaveFile>
#include <algorithm>
namespace host {
namespace {
void u8(QByteArray &b,quint8 n){b.append(char(n));}
void u16(QByteArray &b,quint16 n){u8(b,n);u8(b,n>>8);}
void u32(QByteArray &b,quint32 n){u16(b,n);u16(b,n>>16);}
void u64(QByteArray &b,quint64 n){u32(b,n);u32(b,n>>32);}
QByteArray base(quint32 type,quint32 length,quint16 header=32){
    QByteArray b("LOBJ",4);u16(b,header);u16(b,1);u32(b,length);u32(b,type);return b;
}
QByteArray object(quint32 type,const QByteArray &body,qint64 us){
    QByteArray b=base(type,32+body.size());u32(b,2);u16(b,0);u16(b,0);u64(b,quint64(qMax(qint64(0),us))*1000);b+=body;
    while(b.size()%4)b.append(char(0));return b;
}
void systemTime(QByteArray &b,qint64 ms){
    const auto dt=QDateTime::fromMSecsSinceEpoch(ms);const auto d=dt.date();const auto t=dt.time();
    u16(b,d.year());u16(b,d.month());u16(b,d.dayOfWeek()%7);u16(b,d.day());u16(b,t.hour());u16(b,t.minute());u16(b,t.second());u16(b,t.msec());
}
QString clean(QString s){s.replace('\r',' ');s.replace('\n',' ');return s;}
QByteArray annotation(const QString &text){
    // BLF application text is an ANSI field. Preserve non-ASCII metadata with
    // reversible Unicode escapes instead of writing invalid UTF-8 into it.
    QByteArray out;for(QChar c:text){if(c.unicode()<128)out.append(char(c.unicode()));else out+=QString("\\u%1").arg(c.unicode(),4,16,QChar('0')).toLatin1();}return out;
}
QString hex(quint32 n){return QString::number(n,16).toUpper();}
int dlc(int n){const int lengths[]={0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};for(int i=0;i<16;++i)if(lengths[i]>=n)return i;return 15;}
quint8 checksum(const FrameRecord &r){
    if(r.checksum>=0)return quint8(r.checksum);
    unsigned sum=(r.classicChecksum||r.id>=60)?0:signal::SignalCodec::linPid(quint8(r.id));
    for(auto c:r.payload){sum+=quint8(c);if(sum>255)sum-=255;}return quint8(~sum);
}
QString source(const FrameRecord &r){return r.simulated?"SIM":r.request?"TX request":r.echo?"TX echo":r.direction;}
QByteArray csv(const FrameBatch &rows){
    QByteArray b("\xEF\xBB\xBF");
    const auto escape=[](QString s){if(!s.isEmpty()&&QString("=+-@").contains(s.front()))s.prepend('\'');s.replace('"',"\"\"");return "\""+s+"\"";};
    auto line=[&](const QStringList &cells){QStringList out;for(auto s:cells)out<<escape(s);b+=(out.join(',')+"\r\n").toUtf8();};
    line({"时间","时刻/s","绝对时间/s","软件通道","方向","ID","长度","数据","状态"});
    for(const auto &r:rows)line({r.timestamp,r.relativeTime,r.intervalSeconds,r.channel,r.direction,r.identifier,QString::number(r.length),r.data,r.status});
    return b;
}
}
bool TraceExporter::write(const QString &path,const FrameBatch &input,QString &error,QString format){
    error.clear();if(format.isEmpty())format=QFileInfo(path).suffix().toLower();
    if(format!="csv"&&format!="asc"&&format!="blf"){error="请选择 CSV、ASC 或 BLF 扩展名。";return false;}
    const auto byTime=[](const auto&a,const auto&b){return a.timeUs<b.timeUs;};
    // Capture history is already ordered. Keep it shared/const unless sorting
    // is actually needed; mutable Qt container iteration would detach it.
    FrameBatch sorted;
    const bool reorder=format!="csv"&&!std::is_sorted(input.cbegin(),input.cend(),byTime);
    if(reorder){sorted=input;std::stable_sort(sorted.begin(),sorted.end(),byTime);}
    const auto &rows=reorder?sorted:input;
    QMap<QString,int> channels;
    if(format!="csv")for(const auto &r:rows){
        const auto key=QString::number(int(r.bus))+":"+r.channel;
        if(!channels.contains(key))channels[key]=channels.size()+1;
        if(!r.noResponse&&!r.error&&!r.rtr&&(r.payload.size()!=r.length||r.length>(r.fd?64:8)||r.length<0)){
            error="报文长度不完整，无法导出："+r.channel+" / "+r.identifier;return false;
        }
    }
    QByteArray bytes;const qint64 epoch=rows.isEmpty()?QDateTime::currentMSecsSinceEpoch():rows.first().epochMs-rows.first().timeUs/1000;
    if(format=="csv")bytes=csv(rows);
    else if(format=="asc"){
        const auto date=QLocale::c().toString(QDateTime::fromMSecsSinceEpoch(epoch),"ddd MMM dd hh:mm:ss.zzz AP yyyy");
        bytes=("date "+date+"\r\nbase hex timestamps absolute\r\ninternal events logged\r\n// version 10.0.0\r\n").toUtf8();
        for(auto it=channels.begin();it!=channels.end();++it)bytes+=QString("// channel %1 = %2\r\n").arg(it.value()).arg(clean(it.key())).toUtf8();
        bytes+=("Begin TriggerBlock "+date+"\r\n0.000000 Start of measurement\r\n").toUtf8();
        for(const auto &r:rows){
            const auto t=QString::number(qMax(qint64(0),r.timeUs)/1e6,'f',6);const int ch=channels.value(QString::number(int(r.bus))+":"+r.channel);
            if(r.request&&!r.simulated){bytes+=("// "+t+" TX request (unconfirmed) "+clean(r.channel)+" "+r.identifier+" "+r.data+"\r\n").toUtf8();continue;}
            const auto dir=(r.echo||r.request||r.direction.contains("TX",Qt::CaseInsensitive))?"Tx":"Rx";
            const auto data=QString::fromLatin1(r.payload.toHex(' ')).toUpper();const auto id=hex(r.id)+(r.extended?"x":"");
            QString line;
            if(r.bus==signal::Bus::Lin){
                if(r.noResponse)line=QString("// %1 L%2 %3 %4 - no response").arg(t).arg(ch).arg(id,dir);
                else if(r.error)line=QString("// %1 L%2 %3 %4 - error 0x%5").arg(t).arg(ch).arg(id,dir,hex(r.errorFlags));
                else line=QString("%1 L%2 %3 %4 %5 %6 checksum = %7").arg(t).arg(ch).arg(id,dir).arg(r.length).arg(data,QString("%1").arg(checksum(r),2,16,QChar('0')).toUpper());
            }else if(r.error)line=QString("%1 %2 ErrorFrame").arg(t).arg(ch);
            else if(r.fd)line=QString("%1 CANFD %2 %3 %4  %5 %6 %7 %8 %9 0 0 0 0 0 0 0 0").arg(t).arg(ch).arg(dir,id).arg(int(r.brs)).arg(int(r.esi)).arg(dlc(r.length),0,16).arg(r.length).arg(data);
            else line=QString("%1 %2 %3 %4 %5 %6 %7").arg(t).arg(ch).arg(id,dir,r.rtr?"r":"d").arg(r.length).arg(r.rtr?QString():data);
            bytes+=(line+"\r\n").toUtf8();
            if(r.request||r.simulated||r.error||r.noResponse)bytes+=("// "+t+" "+source(r)+" "+clean(r.status)+"\r\n").toUtf8();
        }bytes+="End TriggerBlock\r\n";
    }else{
        QByteArray objects;quint32 count=0;
        for(auto it=channels.begin();it!=channels.end();++it){
            const auto text=annotation(QString("channel %1 = %2").arg(it.value()).arg(it.key()));QByteArray b;u32(b,0);u32(b,0);u32(b,text.size());u32(b,0);b+=text;objects+=object(65,b,0);++count;
        }
        for(const auto &r:rows){
            if(r.request&&!r.simulated){
                const auto text=annotation("TX request (unconfirmed) "+r.channel+" "+r.identifier+" "+r.data);
                QByteArray note;u32(note,0);u32(note,0);u32(note,text.size());u32(note,0);note+=text;objects+=object(65,note,r.timeUs);++count;continue;
            }
            QByteArray b;quint32 type=1;const int ch=channels.value(QString::number(int(r.bus))+":"+r.channel);
            const bool tx=r.echo||r.request||r.direction.contains("TX",Qt::CaseInsensitive);
            if(r.bus==signal::Bus::Lin){
                u16(b,ch);u8(b,r.id);u8(b,qBound(0,r.length,8));
                if(r.noResponse){type=15;b+=QByteArray(4,0);}
                else if(r.error){type=14;b+=QByteArray(4,0);u8(b,0x40);u8(b,0);u8(b,0);u8(b,0);u32(b,0);}
                else {type=11;b+=r.payload.leftJustified(8,0,true);b+=QByteArray(4,0);u16(b,checksum(r));u8(b,r.request?2:tx?1:0);b+=QByteArray(5,0);}
            }else if(r.error){type=2;u16(b,ch);u16(b,0);}
            else {
                u16(b,ch);u8(b,(tx?1:0)|(r.rtr?128:0));u8(b,r.fd?dlc(r.length):r.length);u32(b,r.id|(r.extended?0x80000000u:0));
                if(r.fd){type=100;u32(b,0);u8(b,0);u8(b,1|(r.brs?2:0)|(r.esi?4:0));u8(b,r.payload.size());u8(b,0);u32(b,0);b+=r.payload.leftJustified(64,0,true);u32(b,0);}
                else b+=r.payload.leftJustified(8,0,true);
            }
            objects+=object(type,b,r.timeUs);++count;
            if(r.simulated||r.request||r.error||r.noResponse){
                const auto text=annotation(source(r)+" "+r.identifier+" "+r.data+" "+r.status+" flags=0x"+hex(r.errorFlags));
                QByteArray note;u32(note,0);u32(note,0);u32(note,text.size());u32(note,0);note+=text;objects+=object(65,note,r.timeUs);++count;
            }
        }
        QByteArray containers;quint64 uncompressed=144;
        for(int pos=0;pos<objects.size();pos+=128*1024){
            const auto raw=objects.mid(pos,128*1024);const auto compressed=qCompress(raw,6).mid(4);
            QByteArray c=base(10,32+compressed.size(),16);u16(c,2);c+=QByteArray(6,0);u32(c,raw.size());u32(c,0);c+=compressed;
            c+=QByteArray(c.size()%4,0);containers+=c;uncompressed+=32+raw.size();
        }
        bytes="LOGG";u32(bytes,144);u32(bytes,4080200);u8(bytes,0);u8(bytes,6);u8(bytes,1);u8(bytes,0);
        u64(bytes,144+containers.size());u64(bytes,uncompressed);u32(bytes,count);u32(bytes,0);
        systemTime(bytes,epoch);systemTime(bytes,epoch+(rows.isEmpty()?0:rows.last().timeUs/1000));bytes+=QByteArray(72,0);bytes+=containers;
    }
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){error="报文导出失败："+file.errorString();return false;}return true;
}
}
