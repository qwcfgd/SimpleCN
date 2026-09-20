#include "DatabaseImporter.h"
#include "SignalCodec.h"
#include "ExactDecimal.h"
#include <dbcppp/Network.h>
#include <QRegularExpression>
#include <sstream>
#include <mutex>
namespace host::signal {
static QString qs(const std::string&s){return QString::fromUtf8(s.data(),int(s.size()));}
static QString attribute(const dbcppp::IAttribute&a){return std::visit([](const auto &v)->QString{
    using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,std::string>)return qs(v);else if constexpr(std::is_same_v<T,double>)return QString::number(v,'g',17);else return QString::number(qlonglong(v));},a.Value());}
// dbcppp does not accept relation attribute declarations. Validate these metadata
// records separately and mask only their parser input, preserving source and lines.
static bool relationMetadata(QString &input,QStringList &notes,QString &error){
    const QString quoted=R"rx("(?:[^"\\]|\\.)*")rx";
    const QString number=R"rx([+-]?(?:0[xX][0-9A-Fa-f]+|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?))rx";
    const QString type="(?:(?:INT|HEX|FLOAT)\\s+"+number+"\\s+"+number+"|STRING|ENUM\\s+"+quoted+"(?:\\s*,\\s*"+quoted+")*)";
    const QRegularExpression declaration("^BA_DEF_REL_\\s+BU_(?:SG|EV|BO)_REL_\\s+"+quoted+"\\s+"+type+"\\s*;$");
    const QRegularExpression defaultValue("^BA_DEF_DEF_REL_\\s+"+quoted+"\\s+(?:"+quoted+"|"+number+")\\s*;$");
    bool inString=false,escaped=false;int count=0;
    for(int i=0;i<input.size();++i){
        const auto c=input[i];
        if(inString){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')inString=false;continue;}
        if(c=='"'){inString=true;continue;}
        if(input.mid(i,2)=="//"){while(i<input.size()&&input[i]!='\n')++i;continue;}
        if(input.mid(i,2)=="/*"){const auto end=input.indexOf("*/",i+2);if(end<0)break;i=end+1;continue;}
        if(c!='B')continue;
        const auto lineStart=input.lastIndexOf('\n',i)+1;
        if(!input.mid(lineStart,i-lineStart).trimmed().isEmpty())continue;
        const QString token=input.mid(i,15).startsWith("BA_DEF_DEF_REL_")?"BA_DEF_DEF_REL_":"BA_DEF_REL_";
        if(input.mid(i,token.size())!=token)continue;
        int next=i+token.size();while(next<input.size()&&(input[next]==' '||input[next]=='\t'))++next;
        // Bare keywords in NS_ are declarations of supported symbol names.
        if(next>=input.size()||input[next]=='\r'||input[next]=='\n')continue;
        bool quotedValue=false,escape=false;int end=next;
        for(;end<input.size();++end){const auto ch=input[end];if(escape){escape=false;continue;}if(quotedValue&&ch=='\\'){escape=true;continue;}if(ch=='"')quotedValue=!quotedValue;if(ch==';'&&!quotedValue)break;}
        const auto record=input.mid(i,end-i+1);
        if(end==input.size()||!(token=="BA_DEF_REL_"?declaration:defaultValue).match(record).hasMatch()){
            error=QString("第 %1 行：无效 DBC 关系属性声明").arg(input.left(i).count('\n')+1);return false;
        }
        for(int j=i;j<=end;++j)if(input[j]!='\n'&&input[j]!='\r')input[j]=' ';
        ++count;i=end;
    }
    if(count)notes.append(QString("已识别 %1 条 DBC 关系属性声明；不参与信号编码").arg(count));
    return true;
}
ImportResult DatabaseImporter::dbc(const QByteArray&bytes,const QString&path){
    auto db=QSharedPointer<DatabaseDefinition>::create();db->bus=Bus::Can;QString text,error;
    if(!prepare(bytes,path,*db,text,error))return {{},error};
    auto parserText=text;if(!relationMetadata(parserText,db->diagnostics,error))return {{},error};
    // The upstream parser writes diagnostics to std::cerr. Serialize imports while capturing it.
    static std::mutex parserMutex;std::unique_ptr<dbcppp::INetwork> network;std::ostringstream diagnostics;
    {std::lock_guard<std::mutex> lock(parserMutex);auto previous=std::cerr.rdbuf(diagnostics.rdbuf());
        try{std::istringstream input(parserText.toUtf8().toStdString());network=dbcppp::INetwork::LoadDBCFromIs(input);}
        catch(const std::exception&e){error=QString::fromUtf8(e.what());}
        catch(...){error="DBC 解析异常";}std::cerr.rdbuf(previous);
    }
    if(!network)return {{},path+": DBC 解析失败\n"+error+qs(diagnostics.str())};
    db->version=qs(network->Version());for(const auto&n:network->Nodes())db->nodes.append(qs(n.Name()));
    // Preserve decimal lexical values after the full grammar has been parsed by dbcppp.
    // This is a precision sidecar, not an alternative DBC parser.
    struct Lexical {QString factor,offset,minimum,maximum;int line=0;};
    QMap<QString,Lexical> lex;QMap<QString,QString> starts,cycles,defaults;
    QString currentId;int line=0;
    const QRegularExpression message("^\\s*BO_\\s+(\\d+)\\s+");
    const QRegularExpression sig("^\\s*SG_\\s+(\\w+)(?:\\s+\\w+)?\\s*:\\s*\\d+\\|\\d+@[01][+-]\\s*\\(\\s*([^,]+),\\s*([^\\)]+)\\)\\s*\\[\\s*([^|]+)\\|\\s*([^\\]]+)\\]");
    for(const auto&l:text.split('\n')){++line;auto m=message.match(l);if(m.hasMatch())currentId=m.captured(1);
        auto s=sig.match(l);if(s.hasMatch())lex[currentId+":"+s.captured(1)]={s.captured(2).trimmed(),s.captured(3).trimmed(),s.captured(4).trimmed(),s.captured(5).trimmed(),line};}
    auto collect=[&](const QString&pattern,auto fn){auto it=QRegularExpression(pattern).globalMatch(text);while(it.hasNext())fn(it.next());};
    collect("BA_DEF_DEF_\\s+\"(GenSigStartValue|GenMsgCycleTime)\"\\s+([^;]+);",[&](const auto&m){defaults[m.captured(1)]=m.captured(2).trimmed();});
    collect("BA_\\s+\"GenSigStartValue\"\\s+SG_\\s+(\\d+)\\s+(\\w+)\\s+([^;]+);",[&](const auto&m){starts[m.captured(1)+":"+m.captured(2)]=m.captured(3).trimmed();});
    collect("BA_\\s+\"GenMsgCycleTime\"\\s+BO_\\s+(\\d+)\\s+([^;]+);",[&](const auto&m){cycles[m.captured(1)]=m.captured(2).trimmed();});
    QSet<QString> keys,names;
    for(const auto&m:network->Messages()){
        FrameDefinition f;const auto encoded=m.Id();f.extended=(encoded&0x80000000ULL)!=0;f.id=quint32(encoded&0x1fffffffULL);
        if(encoded>0xffffffffULL||(!f.extended&&encoded>0x7ff)||((encoded&0x60000000ULL)!=0))return {{},"DBC CAN ID 无效："+QString::number(encoded)};
        f.key=frameKey(Bus::Can,f.id,f.extended);f.name=qs(m.Name());f.publisher=qs(m.Transmitter());f.comment=qs(m.Comment());
        if(!f.publisher.isEmpty())f.transmitters.append(f.publisher);
        for(const auto&node:m.MessageTransmitters())if(!f.transmitters.contains(qs(node)))f.transmitters.append(qs(node));
        if(m.MessageSize()>64)return {{},"DBC 帧长度无效："+f.name};f.length=int(m.MessageSize());
        if(keys.contains(f.key)||names.contains(f.name))return {{},"DBC 重复帧 ID/名称："+f.name};keys.insert(f.key);names.insert(f.name);
        for(const auto&a:m.AttributeValues())f.attributes[qs(a.Name())]=attribute(a);
        const auto id=QString::number(encoded);const auto cycle=cycles.value(id,defaults.value("GenMsgCycleTime","0"));decimal::Number c;
        if(!decimal::parse(cycle,c)||c.n%c.d!=0||c.n<0||c.n>decimal::Int(2147483647)*c.d)return {{},"无效 GenMsgCycleTime："+f.name};f.cycleMs=(c.n/c.d).convert_to<int>();
        for(const auto&s:m.Signals()){
            SignalDefinition v;v.name=qs(s.Name());v.publisher=f.publisher;v.start=int(s.StartBit());v.width=int(s.BitSize());
            if(s.StartBit()>512||s.BitSize()>512)return {{},"DBC 信号布局超限："+v.name};
            v.littleEndian=s.ByteOrder()==dbcppp::ISignal::EByteOrder::LittleEndian;v.isSigned=s.ValueType()==dbcppp::ISignal::EValueType::Signed;
            v.unit=qs(s.Unit());v.comment=qs(s.Comment());v.selector=s.MultiplexerIndicator()==dbcppp::ISignal::EMultiplexer::MuxSwitch;
            v.multiplexed=s.MultiplexerIndicator()==dbcppp::ISignal::EMultiplexer::MuxValue;v.muxValue=s.MultiplexerSwitchValue();
            const auto key=id+":"+v.name;if(!lex.contains(key))return {{},"无法保留 DBC 精确十进制定义："+f.name+"/"+v.name};
            const auto &l=lex[key];v.line=l.line;v.factor=l.factor;v.offset=l.offset;v.minimum=l.minimum;v.maximum=l.maximum;
            for(const auto &number:{v.factor,v.offset,v.minimum,v.maximum}){decimal::Number n;if(!decimal::parse(number,n))return {{},QString("第 %1 行：无效十进制数").arg(v.line)};}
            for(const auto&r:s.Receivers())v.receivers.append(qs(r));
            for(const auto&a:s.AttributeValues())v.attributes[qs(a.Name())]=attribute(a);
            for(const auto&label:s.ValueEncodingDescriptions()){const auto bits=quint64(label.Value())&SignalCodec::mask(v.width);if(v.labels.contains(bits)&&v.labels.value(bits)!=qs(label.Description()))return {{},"枚举冲突："+v.name};v.labels[bits]=qs(label.Description());}
            if(starts.contains(key)||defaults.contains("GenSigStartValue")){
                auto initial=starts.value(key,defaults.value("GenSigStartValue"));decimal::Number n;
                if(!decimal::parse(initial,n)||n.n%n.d!=0)return {{},"文件初始 raw 必须为整数："+v.name};
                v.initial=decimal::text(decimal::Int(n.n/n.d));v.initialSource=starts.contains(key)?"GenSigStartValue 实例值":"GenSigStartValue 属性默认值";
                const auto parsed=SignalCodec::parseRaw(v,v.initial);
                // DBC unsigned start attributes may describe a signed signal's bit pattern.
                if(!parsed.ok()&&v.isSigned&&n.n>=0&&n.n<=decimal::Int(SignalCodec::mask(v.width))*n.d)v.initial="0x"+QString::number((n.n/n.d).convert_to<quint64>(),16);
                if(!SignalCodec::parseRaw(v,v.initial).ok())return {{},"文件初始值位宽溢出："+f.name+"/"+v.name};
            }
            if(s.ExtendedValueType()!=dbcppp::ISignal::EExtendedValueType::Integer)v.issue="首版不支持 DBC 浮点信号";
            if(s.SignalMultiplexerValues_Size())v.issue="首版不支持 DBC 扩展/嵌套复用";
            if(!v.issue.isEmpty())f.issue=v.issue;f.fields.append(v);
        }
        if(f.issue.isEmpty())f.issue=SignalCodec::validateFrame(f);
        if(!f.issue.isEmpty())db->diagnostics.append(f.name+": "+f.issue);
        db->frames.append(f);
    }
    if(db->frames.isEmpty())return {{},"DBC 未包含报文"};
    if(!diagnostics.str().empty())db->diagnostics.append(qs(diagnostics.str()));return {db,{}};
}
}
