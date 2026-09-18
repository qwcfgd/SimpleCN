#include "SignalCodec.h"
#include "ExactDecimal.h"
#include <algorithm>
#include <limits>
namespace host::signal {
using namespace decimal;
quint64 SignalCodec::mask(int w){return w==64?~quint64(0):(w>0&&w<64?(quint64(1)<<w)-1:0);}
RawValue SignalCodec::defaults(const SignalDefinition &s){return s.array?RawValue{0,QByteArray(s.width/8,char(0xff))}:RawValue{mask(s.width),{}};}
static Int integer(const SignalDefinition&s,const RawValue &r){Int n=r.bits;if(s.isSigned&&s.width>0&&s.width<=64&&(r.bits&(quint64(1)<<(s.width-1))))n-=(Int(1)<<s.width);return n;}
QString SignalCodec::rawText(const SignalDefinition&s,const RawValue &r){return s.array?QString::fromLatin1(r.bytes.toHex(' ')).toUpper():decimal::text(integer(s,r));}
static const PhysicalRange *rangeFor(const SignalDefinition&s,quint64 bits){for(const auto &r:s.ranges)if(bits>=r.first&&bits<=r.last)return &r;return nullptr;}
QString SignalCodec::physicalText(const SignalDefinition&s,const RawValue&r){
    if(s.array||!s.conversion)return s.labels.contains(r.bits)?s.labels.value(r.bits):QStringLiteral("无换算定义");
    try {QString factor=s.factor,offset=s.offset;if(!s.ranges.isEmpty()){const auto p=rangeFor(s,r.bits);if(!p)return s.labels.value(r.bits,"无换算定义");factor=p->factor;offset=p->offset;}
        return decimal::text(Number(integer(s,r))*require(factor)+require(offset));
    }catch(...){return "无效换算定义";}
}
bool SignalCodec::parseBytes(const QString &text,int length,QByteArray &out,QString &error){
    QString compact=text;compact.remove(QRegularExpression("\\s"));
    if(compact.size()!=length*2||!QRegularExpression("^[0-9a-fA-F]*$").match(compact).hasMatch()){
        error=QString("需要恰好 %1 字节 HEX；未应用，继续上一有效值").arg(length);return false;
    }out=QByteArray::fromHex(compact.toLatin1());error.clear();return true;
}
ValueResult SignalCodec::parseRaw(const SignalDefinition &s,const QString &input){
    ValueResult r;
    if(s.array){parseBytes(input,s.width/8,r.raw.bytes,r.error);return r;}
    if(s.width<1||s.width>64){r.error="信号位宽必须为 1–64";return r;}
    QString t=input.trimmed();Int n=0;
    bool hex=t.startsWith("0x",Qt::CaseInsensitive);
    if(hex){bool ok=false;r.raw.bits=t.mid(2).toULongLong(&ok,16);if(!ok||(r.raw.bits&~mask(s.width))){r.error="十六进制位模式超出信号位宽";return r;}}
    else {
        if(!QRegularExpression("^[+-]?[0-9]+$").match(t).hasMatch()||t.size()>80){r.error="raw 必须是整数或 0x 位模式";return r;}
        Number value;if(!parse(t,value)){r.error="整数格式错误";return r;}n=value.n;
        const Int lo=s.isSigned?-(Int(1)<<(s.width-1)):Int(0);
        const Int hi=s.isSigned?(Int(1)<<(s.width-1))-1:(Int(1)<<s.width)-1;
        if(n<lo||n>hi){r.error=QString("位宽溢出：范围 %1..%2；继续上一有效值").arg(decimal::text(lo),decimal::text(hi));return r;}
        if(n<0)n+=(Int(1)<<s.width);r.raw.bits=n.convert_to<quint64>();
    }
    r.actual=physicalText(s,r.raw);r.warning=rangeWarning(s,r.raw);return r;
}
ValueResult SignalCodec::parsePhysical(const SignalDefinition &s,const QString &input,int rangeIndex){
    ValueResult result;
    if(s.array||!s.conversion){result.error="无可逆物理换算，请编辑 raw";return result;}
    Number requested;if(!parse(input,requested)){result.error="物理值格式错误";return result;}
    QVector<PhysicalRange> ranges=s.ranges;if(ranges.isEmpty())ranges.append({0,mask(s.width),s.factor,s.offset,s.unit});
    QVector<ValueResult> candidates;
    for(int i=0;i<ranges.size();++i){
        if(rangeIndex>=0&&rangeIndex!=i)continue;
        try {const auto &r=ranges[i];const auto factor=require(r.factor);if(factor.n==0)continue;
            const auto inverse=(requested-require(r.offset))/factor;
            const Int raw=inverse.n/inverse.d; // cpp_int division truncates toward zero, including negative values.
            if(!s.ranges.isEmpty()&&(raw<Int(r.first)||raw>Int(r.last)))continue;
            auto candidate=parseRaw(s,decimal::text(raw));
            if(!candidate.ok()){result=candidate;continue;}
            const auto actual=Number(integer(s,candidate.raw))*factor+require(r.offset);
            candidate.actual=decimal::text(actual);
            if(!(actual==requested)){if(!candidate.warning.isEmpty())candidate.warning+="；";candidate.warning+="向零量化：请求 "+input+" → 实际 "+candidate.actual;}
            candidates.append(candidate);
        }catch(...){result.error="无效物理换算";}
    }
    if(candidates.size()>1){result.error="分段反解有多个候选，请指定换算段或编辑 raw";return result;}
    if(candidates.size()==1)return candidates.first();
    if(result.error.isEmpty())result.error="没有可表示的物理反解，请编辑 raw";return result;
}
QString SignalCodec::rangeWarning(const SignalDefinition&s,const RawValue&r){
    if(s.array||!s.conversion||!s.ranges.isEmpty())return {};
    try{const auto actual=Number(integer(s,r))*require(s.factor)+require(s.offset);
        if((!s.minimum.isEmpty()&&actual<require(s.minimum))||(!s.maximum.isEmpty()&&require(s.maximum)<actual))return "超出数据库 min/max；允许发送";
    }catch(...){return "无效范围定义";}return {};
}
QVector<int> SignalCodec::bitPositions(const SignalDefinition&s){
    QVector<int> result;int bit=s.start;
    for(int i=0;i<s.width;++i){result.append(bit);bit=s.littleEndian?bit+1:(bit%8==0?bit+15:bit-1);}return result;
}
bool SignalCodec::isActive(const FrameDefinition&f,int field,const QVector<RawValue>&values){
    const auto &s=f.fields[field];if(!s.multiplexed)return true;
    for(int i=0;i<f.fields.size();++i)if(f.fields[i].selector)return i<values.size()&&values[i].bits==s.muxValue;
    return false;
}
bool SignalCodec::encode(const FrameDefinition&f,const QVector<RawValue>&values,QByteArray &bytes,QString &error){
    if(bytes.size()!=f.length||values.size()!=f.fields.size()){error="帧长度或信号数量不一致";return false;}
    if(!f.issue.isEmpty()){error=f.issue;return false;}
    auto next=bytes;
    for(int i=0;i<f.fields.size();++i){if(!isActive(f,i,values))continue;const auto &s=f.fields[i];const auto &v=values[i];
        if(!s.issue.isEmpty()){error=s.issue;return false;}
        const auto positions=bitPositions(s);
        if((s.array&&v.bytes.size()*8!=s.width)||(!s.array&&(v.bits&~mask(s.width)))){error="信号位宽溢出";return false;}
        for(int n=0;n<positions.size();++n){const int bit=positions[n];if(bit<0||bit>=bytes.size()*8){error="信号越过帧边界";return false;}
            const int source=s.littleEndian?n:s.width-1-n;
            const bool set=s.array?((quint8(v.bytes[source/8])>>(source%8))&1):((v.bits>>source)&1);
            next[bit/8]=char((quint8(next[bit/8])&~(1<<(bit%8)))|(int(set)<<(bit%8)));
        }
    }bytes=next;error.clear();return true;
}
bool SignalCodec::decode(const FrameDefinition&f,const QByteArray &bytes,QVector<RawValue>&values,QString &error){
    if(bytes.size()!=f.length){error="接收/编辑帧长度与数据库不符";return false;}
    values.resize(f.fields.size());
    auto extract=[&](int i){const auto &s=f.fields[i];RawValue value;if(s.array)value.bytes.fill(0,s.width/8);
        if(s.width<1||s.width>64)return false;
        const auto positions=bitPositions(s);for(int n=0;n<positions.size();++n){const int bit=positions[n];if(bit<0||bit>=bytes.size()*8)return false;
            if(!((quint8(bytes[bit/8])>>(bit%8))&1))continue;const int dest=s.littleEndian?n:s.width-1-n;
            if(s.array)value.bytes[dest/8]=char(quint8(value.bytes[dest/8])|(1<<(dest%8)));else value.bits|=quint64(1)<<dest;
        }values[i]=value;return true;};
    for(int i=0;i<f.fields.size();++i)if(f.fields[i].selector&&!extract(i)){error="复用选择器越界";return false;}
    for(int i=0;i<f.fields.size();++i)if(isActive(f,i,values)&&!extract(i)){error="信号越界";return false;}
    error.clear();return true;
}
bool SignalCodec::initialize(const FrameDefinition&f,Bus bus,TxDraft&draft,QString&error){
    TxDraft out;out.cycleMs=f.cycleMs;out.applied.bytes.fill(bus==Bus::Can?0:char(0xff),f.length);
    for(const auto &s:f.fields){auto value=defaults(s);if(!s.initial.isEmpty()){const auto parsed=parseRaw(s,s.initial);if(!parsed.ok()){error=s.name+": 文件初始值无效："+parsed.error;return false;}value=parsed.raw;}out.values.append(value);}
    if(f.issue.isEmpty()&&!encode(f,out.values,out.applied.bytes,error))return false;
    for(int i=0;i<f.fields.size();++i)out.warnings[i]=rangeWarning(f.fields[i],out.values[i]);
    out.applied.revision=1;out.appliedValues=out.values;draft=out;return true;
}
QString SignalCodec::validateFrame(const FrameDefinition&f){
    if(f.length<0||f.length>8)return "首版仅支持 0–8 字节经典 CAN / 1–8 字节 LIN";
    int selectors=0;QSet<QString> names;
    for(int i=0;i<f.fields.size();++i){const auto &s=f.fields[i];if(names.contains(s.name))return "重复信号名称："+s.name;names.insert(s.name);
        selectors+=s.selector;if(s.width<1||s.width>64||s.start<0||(s.array&&(s.width%8||s.start%8)))return "无效信号布局："+s.name;
        auto positions=bitPositions(s);for(int b:positions)if(b<0||b>=f.length*8)return "信号越过帧边界："+s.name;
        for(int j=0;j<i;++j){const auto &other=f.fields[j];if(s.multiplexed&&other.multiplexed&&s.muxValue!=other.muxValue)continue;
            const auto occupied=bitPositions(other);for(int b:positions)if(occupied.contains(b))return "活动信号位重叠："+s.name+" / "+other.name;}
    }
    if(selectors>1)return "不支持嵌套/多选择器复用";
    for(const auto&s:f.fields)if(s.multiplexed&&!selectors)return "复用分支缺少选择器";
    return {};
}
QString SignalCodec::validateSchedule(const Schedule&s,const QVector<FrameDefinition>&frames,int bitrate){
    if(!s.issue.isEmpty())return s.issue;
    if(s.entries.isEmpty()||s.entries.size()>256)return "硬件调度表需要 1–256 个槽";
    if(bitrate<1000||bitrate>20000)return "LIN 波特率超出范围";
    for(const auto&slot:s.entries){if(!slot.issue.isEmpty())return slot.issue;
        auto f=std::find_if(frames.begin(),frames.end(),[&](const auto&v){return v.key==slot.frame;});
        if(f==frames.end())return "调度引用未知帧："+slot.frame;if(!f->issue.isEmpty())return f->issue;
        Number delay;if(!parse(slot.delayMs,delay)||delay.n%delay.d!=0||delay.n<4*delay.d||delay.n>65535*delay.d)return "PLIN delay 必须为 4–65535 整数 ms；不进行舍入";
        // LIN maximum frame time: 1.4 * (34 header bits + 10*(data bytes+checksum)).
        if(delay.n*bitrate*10<delay.d*14000*(34+10*(f->length+1)))return "槽间隔不足以容纳当前波特率下的完整 LIN 帧";
    }return {};
}
quint8 SignalCodec::linPid(quint8 id){id&=0x3f;const int p0=((id>>0)^(id>>1)^(id>>2)^(id>>4))&1;const int p1=(~((id>>1)^(id>>3)^(id>>4)^(id>>5)))&1;return id|(p0<<6)|(p1<<7);}
}
