#include "CddDatabase.h"
#include <QRegularExpression>
#include <cmath>
#include <cstring>
#include <limits>

namespace host::diag {
namespace {
bool rawAllowed(const Field &f,quint64 raw,QString &error){
    if(f.bits<64&&raw>=(quint64(1)<<f.bits)){error="超出位宽";return false;}
    if(f.hasRange){
        qint64 signedRaw=qint64(raw);if(f.bits<64&&(raw&(quint64(1)<<(f.bits-1))))signedRaw=qint64(raw|(~quint64(0)<<f.bits));
        if(f.encoding=="sgn"?(signedRaw<f.signedMinimum||signedRaw>f.signedMaximum):(raw<f.minimum||raw>f.maximum)){error="超出 CDD 数值范围";return false;}
    }
    for(auto x:f.excluded)if(raw>=x.first&&raw<=x.second){error="CDD 排除的数值";return false;}
    if(!f.choices.isEmpty()){
        bool found=false;for(const auto &c:f.choices)if(raw>=c.first&&raw<=c.last)found=true;
        if(!found){error="不在 CDD 枚举范围内";return false;}
    }
    return true;
}
bool scalar(const Field &f,const QString &text,quint64 &raw,QString &error){
    const auto s=text.trimmed();bool ok=false;raw=0;
    if(s.isEmpty()){error="必填参数未填写";return false;}
    if(s.startsWith("0x",Qt::CaseInsensitive))raw=s.mid(2).toULongLong(&ok,16);
    else if(f.encoding=="flt"||f.encoding=="dbl"){
        const double v=s.toDouble(&ok);if(!ok||!std::isfinite(v)){error="需要有限浮点数";return false;}
        if(f.encoding=="flt"&&f.bits==32){float x=float(v);if(!std::isfinite(x))ok=false;quint32 b=0;std::memcpy(&b,&x,4);raw=b;}
        else if(f.encoding=="dbl"&&f.bits==64)std::memcpy(&raw,&v,8);else ok=false;
    }else if(f.factor!=1||f.offset!=0){
        const double physical=s.toDouble(&ok),v=(physical-f.offset)/f.factor;
        const bool signedValue=f.encoding=="sgn";
        const double lower=signedValue?-std::ldexp(1.0,f.bits-1):0,upper=std::ldexp(1.0,f.bits-(signedValue?1:0));
        if(!ok||!std::isfinite(v)||std::abs(v-std::round(v))>1e-7||v<lower||v>=upper){error="物理值无法精确编码或超出范围";return false;}
        raw=signedValue?quint64(qint64(std::round(v))):quint64(std::round(v));
        if(signedValue&&f.bits<64)raw&=(quint64(1)<<f.bits)-1;
    }else if(f.encoding=="sgn"){
        const auto value=s.toLongLong(&ok,10);
        if(ok&&f.bits<64&&(value<-(qint64(1)<<(f.bits-1))||value>((qint64(1)<<(f.bits-1))-1)))ok=false;
        raw=quint64(value);if(f.bits<64)raw&=(quint64(1)<<f.bits)-1;
    }else if(f.encoding=="bcd"){
        ok=f.bits%4==0&&s.size()<=f.bits/4;
        for(QChar c:s){if(c<'0'||c>'9'){ok=false;break;}raw=(raw<<4)|quint64(c.unicode()-'0');}
    }else raw=s.toULongLong(&ok,10);
    if(!ok){error="数值格式或范围无效（十六进制需 0x 前缀）";return false;}
    return rawAllowed(f,raw,error);
}
void putBits(QByteArray &out,int &position,quint64 raw,int bits,bool little){
    for(int i=0;i<bits;++i){
        int source=little?(i/8)*8+7-i%8:bits-1-i;
        if(position/8>=out.size())out.append(char(0));
        if((raw>>source)&1)out[position/8]=char(quint8(out[position/8])|(1<<(7-position%8)));
        ++position;
    }
}
quint64 getBits(const QByteArray &bytes,int &position,int bits,bool little){
    quint64 raw=0;for(int i=0;i<bits;++i){const int target=little?(i/8)*8+7-i%8:bits-1-i;
        if((quint8(bytes[position/8])>>(7-position%8))&1)raw|=quint64(1)<<target;++position;}return raw;
}
bool textBytes(const Field &f,const QString &value,QByteArray &data,QString &error){
    if(f.encoding=="asc"){
        for(auto c:value)if(c.unicode()>127){error="仅允许 ASCII 字符";return false;}
        data=value.toLatin1();return true;
    }
    if(f.encoding=="utf"||f.encoding=="utf8"){
        if(f.bits==16){for(auto c:value){auto u=c.unicode();data.append(char(f.littleEndian?u:u>>8));data.append(char(f.littleEndian?u>>8:u));}}
        else data=value.toUtf8();return true;
    }
    return Codec::parseHex(value,data,error);
}
int fixedBits(const Field &f){return f.array?(f.minCount==f.maxCount?f.minCount*f.bits+f.lengthBytes*8:-1):f.bits;}
QString display(const Field &f,quint64 raw){
    if(f.encoding=="sgn"){
        qint64 v=qint64(raw);if(f.bits<64&&(raw&(quint64(1)<<(f.bits-1))))v=qint64(raw|(~quint64(0)<<f.bits));
        if(f.factor!=1||f.offset!=0)return QString::number(double(v)*f.factor+f.offset,'g',16);
        return QString::number(v);
    }
    if(f.encoding=="flt"&&f.bits==32){quint32 b=quint32(raw);float v=0;std::memcpy(&v,&b,4);return QString::number(v,'g',9);}
    if(f.encoding=="dbl"&&f.bits==64){double v=0;std::memcpy(&v,&raw,8);return QString::number(v,'g',17);}
    if(f.encoding=="bcd")return QString::number(raw,16);
    if(f.factor!=1||f.offset!=0)return QString::number(double(raw)*f.factor+f.offset,'g',16);
    return QString("0x%1").arg(raw,(f.bits+3)/4,16,QChar('0')).toUpper().replace("0X","0x");
}
QByteArray normalized(const Service &s,QByteArray bytes){
    int offset=0;for(const auto &f:s.request.fields){
        if(offset==8&&f.bits==8&&f.suppressible&&bytes.size()>1)bytes[1]=char(quint8(bytes[1])&0x7f);
        int size=fixedBits(f);if(size<0)break;offset+=size;
    }return bytes;
}
}
bool Codec::parseHex(const QString &input,QByteArray &bytes,QString &error){
    QString s=input;s.remove(QRegularExpression("\\s+"));
    if(s.size()%2||!QRegularExpression("^[0-9a-fA-F]*$").match(s).hasMatch()){error="HEX 需为完整字节，例如 2E F1 90";return false;}
    if(s.size()>8190){error="UDS 请求超过 4095 字节";return false;}
    bytes=QByteArray::fromHex(s.toLatin1());error.clear();return true;
}
bool Codec::encode(const Message &m,const Values &values,QByteArray &out,QString &error){
    out.clear();error=m.issue;if(!error.isEmpty())return false;
    QByteArray result;int pos=0;QMap<QString,quint64> rawValues;
    for(const auto &f:m.fields){
        QString why;
        if(f.array){
            QByteArray data;if(!textBytes(f,values.value(f.key),data,why)){error=f.name+"："+why;return false;}
            const int unit=f.bits/8;
            if(pos%8||unit<1||data.size()%unit||data.size()/unit<f.minCount||data.size()/unit>f.maxCount){error=f.name+"：长度应为 "+f.constraint();return false;}
            if(pos/8+f.lengthBytes+data.size()>4095){error="请求超过 4095 字节";return false;}
            if(!f.countKey.isEmpty()&&(!rawValues.contains(f.countKey)||(rawValues.value(f.countKey)&f.countMask)!=quint64(data.size()/unit))){error="重复记录数量与计数字段不一致："+f.name;return false;}
            if(f.lengthBytes){const auto count=quint64(data.size()/unit);if(count>=(quint64(1)<<(f.lengthBytes*8))){error="数组长度前缀溢出";return false;}putBits(result,pos,count,f.lengthBytes*8,f.littleEndian);}
            result+=data;pos+=data.size()*8;
        }else{
            quint64 raw=f.value;
            if(!f.constant&&!scalar(f,values.value(f.key),raw,why)){error=f.name+"："+why;return false;}
            if(f.bits<1||f.bits>64||(f.bits<64&&raw>=(quint64(1)<<f.bits))||pos+f.bits>4095*8){error="字段长度或常量无效："+f.name;return false;}
            putBits(result,pos,raw,f.bits,f.littleEndian);
            rawValues[f.key]=raw;
        }
    }
    if(pos%8){error="请求字段总长未按字节对齐";return false;}
    out=result;return true;
}
bool Codec::decode(const Message &m,const QByteArray &bytes,Values &out,QString &error){
    out.clear();error=m.issue;if(!error.isEmpty())return false;
    int pos=0;QMap<QString,quint64> rawValues;
    for(int i=0;i<m.fields.size();++i){const auto &f=m.fields[i];
        if(f.array){
            if(pos%8||f.bits%8){error="数组边界未按字节对齐："+f.name;return false;}
            int count=f.minCount;
            if(!f.countKey.isEmpty()){
                if(!rawValues.contains(f.countKey)||(rawValues.value(f.countKey)&f.countMask)>quint64(f.maxCount)){error="重复记录计数字段无效："+f.name;return false;}
                count=int(rawValues.value(f.countKey)&f.countMask);
            }else if(f.lengthBytes){
                if(pos+f.lengthBytes*8>bytes.size()*8){error="长度前缀不足："+f.name;return false;}
                auto n=getBits(bytes,pos,f.lengthBytes*8,f.littleEndian);if(n>4095){error="数组长度超限";return false;}count=int(n);
            }else if(f.minCount!=f.maxCount){
                int suffix=0;for(int j=i+1;j<m.fields.size();++j){int n=fixedBits(m.fields[j]);if(n<0){error="多个可变长度字段缺少长度界定";return false;}suffix+=n;}
                int remaining=bytes.size()*8-pos-suffix;
                if(remaining<0||remaining%f.bits){error="可变字段长度无效："+f.name;return false;}count=remaining/f.bits;
            }
            const qint64 length=qint64(count)*f.bits/8;
            if(count<f.minCount||count>f.maxCount||length>4095||pos/8+length>bytes.size()){error="字段长度不匹配："+f.name;return false;}
            auto data=bytes.mid(pos/8,int(length));pos+=int(length)*8;
            if(f.encoding=="asc"){
                for(auto c:data)if(quint8(c)>127){error="响应包含非 ASCII 字节："+f.name;return false;}
                out[f.key]=QString::fromLatin1(data);
            }else if(f.encoding=="utf"||f.encoding=="utf8"){
                if(f.bits==16){QString text;for(int k=0;k<data.size();k+=2)text+=QChar(f.littleEndian?(quint8(data[k])|(quint16(quint8(data[k+1]))<<8)):((quint16(quint8(data[k]))<<8)|quint8(data[k+1])));out[f.key]=text;}
                else {auto text=QString::fromUtf8(data);if(text.toUtf8()!=data){error="无效 UTF-8："+f.name;return false;}out[f.key]=text;}
            }else out[f.key]=QString::fromLatin1(data.toHex(' ')).toUpper();
        }else{
            if(f.bits<1||f.bits>64||pos+f.bits>bytes.size()*8){error="报文过短："+f.name;return false;}
            const auto raw=getBits(bytes,pos,f.bits,f.littleEndian);
            rawValues[f.key]=raw;
            if(f.constant&&raw!=f.value){error="常量/回显不匹配："+f.name;return false;}
            QString why;if(!f.constant&&!rawAllowed(f,raw,why)){error=f.name+"："+why;return false;}
            if(f.encoding=="bcd"){for(int b=0;b<f.bits;b+=4)if(((raw>>b)&15)>9){error="无效 BCD："+f.name;return false;}}
            out[f.key]=display(f,raw);
        }
    }
    if(pos!=bytes.size()*8){error="报文存在未定义的尾部数据";return false;}return true;
}
QString Codec::preview(const Message &m,const Values &values){
    QByteArray bytes;QString error;if(encode(m,values,bytes,error))return QString::fromLatin1(bytes.toHex(' ')).toUpper();
    QStringList parts;Message prefix;
    for(const auto &f:m.fields){if(f.constant)prefix.fields.append(f);else break;}
    if(encode(prefix,{},bytes,error)&&!bytes.isEmpty())parts.append(QString::fromLatin1(bytes.toHex(' ')).toUpper());
    for(const auto &f:m.fields)if(!f.constant)parts.append("<"+f.name+"> ");
    return parts.join(' ');
}
bool Codec::validate(const Service &s,const QByteArray &bytes,QString &error){
    if(!s.issue.isEmpty()){error=s.issue;return false;}
    if(bytes.isEmpty()||bytes.size()>4095||quint8(bytes[0])!=s.sid){error="SID 或请求长度无效";return false;}
    Values values;if(!decode(s.request,normalized(s,bytes),values,error))return false;
    if(s.sid==0x34||s.sid==0x35){
        if(bytes.size()<3){error="下载请求缺少 ALFID";return false;}
        const int a=quint8(bytes[2])&15,n=quint8(bytes[2])>>4;
        if(a==0||n==0||bytes.size()!=3+a+n){error="ALFID 与地址/长度字节数不匹配";return false;}
    }
    return true;
}
QByteArray Codec::expected(const Service &s,const QByteArray &request){
    QByteArray prefix;Message m;
    for(const auto &f:s.response.fields){if(!f.constant)break;m.fields.append(f);}
    QString error;encode(m,{},prefix,error);
    if(prefix.isEmpty())prefix=QByteArray(1,char(s.sid+0x40));
    // For services whose selector is supplied at runtime, validate its echo too.
    const auto req=normalized(s,request);
    int n=1;if(s.sid==0x22||s.sid==0x2e||s.sid==0x2f)n=3;
    else if(s.sid==0x31)n=4;
    else if(s.sid==0x10||s.sid==0x11||s.sid==0x19||s.sid==0x27||s.sid==0x28||s.sid==0x3e||s.sid==0x85||s.sid==0x36)n=2;
    if(prefix.size()<n&&req.size()>=n)prefix=QByteArray(1,char(s.sid+0x40))+req.mid(1,n-1);
    return prefix;
}
QString Codec::describeResponse(const Service &s,const QByteArray &bytes){
    if(bytes.isEmpty())return "未收到正响应（抑制正响应请求）";
    if(quint8(bytes[0])==0x7f&&bytes.size()==3){
        static const QMap<int,QString> names={{0x11,"服务不支持"},{0x12,"子功能不支持"},{0x13,"长度或格式错误"},{0x22,"条件不满足"},{0x24,"请求顺序错误"},{0x31,"请求超出范围"},{0x33,"安全访问被拒绝"},{0x35,"无效密钥"},{0x36,"超过尝试次数"},{0x37,"延迟时间未到"},{0x78,"响应待处理"},{0x7e,"当前会话不支持子功能"},{0x7f,"当前会话不支持服务"}};
        return QString("NRC 0x%1 · %2").arg(quint8(bytes[2]),2,16,QChar('0')).arg(names.value(quint8(bytes[2]),"ECU 负响应"));
    }
    Values values;QString error;if(!decode(s.response,bytes,values,error))return "响应字段解析："+error;
    QStringList lines;for(const auto &f:s.response.fields){auto value=values.value(f.key);quint64 raw=0;bool ok=false;raw=value.startsWith("0x")?value.mid(2).toULongLong(&ok,16):value.toULongLong(&ok);
        for(const auto &c:f.choices)if(ok&&raw>=c.first&&raw<=c.last){value+=" ("+c.text+")";break;}
        lines.append(f.name+" = "+value+(f.unit.isEmpty()?QString():" "+f.unit));}
    return lines.join('\n');
}
QByteArray Codec::simulationResponse(const Service &s,const QByteArray &request,QMap<int,QByteArray> &dids){
    auto nrc=[&](int code){return QByteArray::fromHex("7f")+char(s.sid)+char(code);};
    QString error;if(!validate(s,request,error))return nrc(0x13);
    int did=request.size()>=3?(quint8(request[1])<<8)|quint8(request[2]):0;
    if(s.sid==0x2e)dids[did]=request.mid(3);
    if(s.sid==0x22&&dids.contains(did))return expected(s,request)+dids.value(did);
    Values values;
    for(const auto &f:s.response.fields)if(!f.constant){
        if(f.array){int count=f.minCount;values[f.key]=(f.encoding=="asc"||f.encoding=="utf"||f.encoding=="utf8")?QString(count,'S'):QString::fromLatin1(QByteArray(qMin(4095,count*f.bits/8),0).toHex(' '));}
        else if(!f.choices.isEmpty())values[f.key]="0x"+QString::number(f.choices.first().first,16);
        else {quint64 raw=f.hasRange?(f.encoding=="sgn"?quint64(f.signedMinimum):f.minimum):0;if(f.bits<64)raw&=(quint64(1)<<f.bits)-1;values[f.key]="0x"+QString::number(raw,16);}
    }
    QByteArray response;if(!encode(s.response,values,response,error))return nrc(0x11);
    auto echo=expected(s,request);if(response.size()<echo.size())response=echo;else response.replace(0,echo.size(),echo);
    if(s.sid==0x10)response=echo+QByteArray::fromHex("003201f4");
    if(s.sid==0x34)response=QByteArray::fromHex("74200102");
    return response;
}
}
