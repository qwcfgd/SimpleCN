#include "DatabaseImporter.h"
#include "SignalCodec.h"
#include "ExactDecimal.h"
#include <stdexcept>
namespace host::signal {
namespace {
struct Token { QString text;int line=1,column=1;bool quoted=false; };
class ParseError : public std::runtime_error {public: explicit ParseError(const QString&s):std::runtime_error(s.toStdString()){} };
class Parser {
    QVector<Token> tokens;int at=0;
    DatabaseDefinition &db;
    QMap<QString,SignalDefinition> fields,encodings;
    QMap<QString,QString> representations,unsupportedFrames;
    bool header=false,haveNodes=false;
    const Token &peek()const{return tokens[qMin(at,int(tokens.size())-1)];}
    [[noreturn]] void fail(const QString&s)const{throw ParseError(QString("第 %1 行，第 %2 列：%3（%4）").arg(peek().line).arg(peek().column).arg(s,peek().text));}
    QString take(){if(peek().text=="<EOF>")fail("意外文件结束");return tokens[at++].text;}
    bool accept(const QString&s){if(peek().text==s){++at;return true;}return false;}
    void expect(const QString&s){if(!accept(s))fail("需要 "+s);}
    QString name(){if(peek().quoted||!QRegularExpression("^[A-Za-z_][A-Za-z0-9_]*$").match(peek().text).hasMatch())fail("需要标识符");return take();}
    QString number(){decimal::Number n;if(!decimal::parse(peek().text,n))fail("需要十进制数");return take();}
    quint64 natural(){auto t=peek().text;bool ok=false;const auto n=t.startsWith("0x",Qt::CaseInsensitive)?t.mid(2).toULongLong(&ok,16):t.toULongLong(&ok,10);if(!ok||t.startsWith('-'))fail("需要非负整数");take();return n;}
    int small(int maximum){auto n=natural();if(n>quint64(maximum))fail("整数超出允许范围");return int(n);}
    QString string(){if(!peek().quoted)fail("需要带引号的字符串");return take();}
    QString valueList(){QStringList values;while(peek().text!=";"){if(peek().text=="{"||peek().text=="}")fail("无效属性值");values.append(take());}expect(";");return values.join(' ');}
    // Explicitly recognized unsupported blocks are retained as diagnostics. Balanced
    // token parsing ensures malformed syntax cannot disappear through a regex skip.
    QString balanced(){expect("{");int depth=1;QStringList text;while(depth){auto t=take();if(t=="{")++depth;if(t=="}")--depth;if(depth)text.append(t);}return text.join(' ');}
    void unique(const QString&n,const QMap<QString,SignalDefinition>&map){if(map.contains(n))fail("重复定义 "+n);}
    void parseNodes(){
        if(haveNodes)fail("重复 Nodes");haveNodes=true;expect("{");
        while(!accept("}")){
            const auto kind=name();expect(":");
            if(kind=="Master"){if(!db.master.isEmpty())fail("重复主节点");db.master=name();db.nodes.append(db.master);expect(",");number();expect("ms");expect(",");number();expect("ms");expect(";");}
            else if(kind=="Slaves"){do{auto n=name();if(db.nodes.contains(n))fail("重复节点 "+n);db.nodes.append(n);}while(accept(","));expect(";");}
            else fail("未知节点定义");
        }
    }
    void parseSignals(bool diagnostic){expect("{");while(!accept("}")){
        SignalDefinition s;s.line=peek().line;s.name=name();unique(s.name,fields);expect(":");s.width=small(64);if(!s.width)fail("信号宽度必须大于 0");expect(",");
        if(accept("{")){s.array=true;QByteArray bytes;do{bytes.append(char(small(255)));}while(accept(","));expect("}");if(bytes.size()*8!=s.width)fail("数组初值长度与位宽不符");s.initial=QString::fromLatin1(bytes.toHex(' '));}
        else {if(s.width>16)fail("LIN 标量宽度必须为 1–16；更宽值须声明为字节数组");s.initial=QString::number(natural());}
        s.initialSource=diagnostic?"LDF Diagnostic_signals":"LDF Signals";s.conversion=false;
        if(!diagnostic){expect(",");s.publisher=name();while(accept(","))s.receivers.append(name());}
        expect(";");if(!SignalCodec::parseRaw(s,s.initial).ok())fail("文件初始值位宽溢出："+s.name);fields.insert(s.name,s);
    }}
    void parseFrames(bool diagnostic){expect("{");while(!accept("}")){
        FrameDefinition f;f.line=peek().line;f.name=name();expect(":");f.id=quint32(small(63));
        if(!diagnostic){expect(",");f.publisher=name();if(accept(","))f.length=small(8);else f.length=f.id<32?2:(f.id<48?4:8);}
        else {f.length=8;f.issue="首版不执行 LIN 诊断帧/槽";}
        if(!diagnostic&&(f.id>59||f.length<1))fail("普通 LIN 帧 ID/长度无效");
        f.key=frameKey(Bus::Lin,f.id);expect("{");
        while(!accept("}")){SignalDefinition s;s.line=peek().line;s.name=name();expect(",");s.start=small(63);expect(";");f.fields.append(s);}
        db.frames.append(f);
    }}
    void parseSchedules(){expect("{");QSet<QString> names;while(!accept("}")){
        Schedule s;s.name=name();if(names.contains(s.name))fail("重复调度表");names.insert(s.name);expect("{");
        while(!accept("}")){ScheduleSlot slot;slot.line=peek().line;slot.frame=name();
            if(peek().text=="{"){balanced();slot.issue="首版不执行配置/诊断命令槽："+slot.frame;}
            if(slot.frame=="MasterReq"||slot.frame=="SlaveResp")slot.issue="首版不执行诊断槽："+slot.frame;
            expect("delay");slot.delayMs=number();expect("ms");expect(";");s.entries.append(slot);
        }db.schedules.append(s);
    }}
    void parseEncodings(){expect("{");while(!accept("}")){
        SignalDefinition s;s.name=name();unique(s.name,encodings);s.conversion=false;expect("{");
        while(!accept("}")){const auto type=name();
            if(type=="logical_value"){expect(",");const auto value=natural();QString label;if(accept(","))label=string();expect(";");if(s.labels.contains(value))fail("重复逻辑枚举值");s.labels[value]=label;}
            else if(type=="physical_value"){PhysicalRange r;expect(",");r.first=natural();expect(",");r.last=natural();if(r.first>r.last)fail("分段 raw 范围倒置");expect(",");r.factor=number();expect(",");r.offset=number();if(accept(","))r.unit=string();expect(";");s.ranges.append(r);s.conversion=true;}
            else if(type=="bcd_value"||type=="ascii_value"){expect(";");s.issue="首版不支持 "+type;}
            else fail("未知编码语义："+type);
        }encodings[s.name]=s;
    }}
    void parseRepresentations(){expect("{");while(!accept("}")){const auto encoding=name();expect(":");do{const auto field=name();if(representations.contains(field))fail("重复信号表示");representations[field]=encoding;}while(accept(","));expect(";");}}
    void parseAttributes(){expect("{");while(!accept("}")){const auto node=name();if(db.nodeAttributes.contains(node))fail("重复节点属性");expect("{");QMap<QString,QString> attrs;
        const QSet<QString> supported={"LIN_protocol","configured_NAD","initial_NAD","product_id","response_error","fault_state_signals","P2_min","ST_min","N_As_timeout","N_Cr_timeout","response_tolerance","wakeup_time","poweron_time"};
        while(!accept("}")){const auto key=name();if(attrs.contains(key))fail("重复节点属性");if(key=="configurable_frames")attrs[key]=balanced();else{if(!supported.contains(key))fail("不支持的节点属性："+key);expect("=");attrs[key]=valueList();}}
        db.nodeAttributes[node]=attrs;
    }}
    void parseSpecialFrames(const QString&kind){expect("{");while(!accept("}")){const auto n=name();expect(":");valueList();if(unsupportedFrames.contains(n))fail("重复特殊帧");unsupportedFrames[n]="首版不执行 "+kind;}}
    void resolve(){
        if(!header||!haveNodes||db.master.isEmpty()||db.version.isEmpty()||db.bitrate==0)fail("缺少 LDF 必需的文件头、版本、速度或节点");
        for(auto it=fields.begin();it!=fields.end();++it){auto &s=it.value();if(!s.publisher.isEmpty()&&!db.nodes.contains(s.publisher))fail("未知信号发布者："+s.publisher);for(const auto&r:s.receivers)if(!db.nodes.contains(r))fail("未知订阅者："+r);}
        for(auto it=representations.begin();it!=representations.end();++it){if(!fields.contains(it.key())||!encodings.contains(it.value()))fail("未知信号/编码引用："+it.key());auto &s=fields[it.key()];const auto &e=encodings[it.value()];s.conversion=e.conversion;s.ranges=e.ranges;s.labels=e.labels;s.issue=e.issue;}
        QMap<quint32,int> idCounts;for(const auto&f:db.frames)++idCounts[f.id];
        QSet<QString> names;QMap<QString,QString> keyByName;
        for(auto &f:db.frames){if(names.contains(f.name)||unsupportedFrames.contains(f.name))fail("重复帧名称："+f.name);names.insert(f.name);
            // Keep distinct descriptions visible without assigning ambiguous bus
            // traffic to either one or silently overwriting its working copy.
            if(idCounts.value(f.id)>1){f.key+=":"+f.name;f.issue=QString("LIN ID 0x%1 对应多个帧定义；可浏览，暂不支持发送或按 ID 解码").arg(f.id,2,16,QChar('0'));}
            keyByName[f.name]=f.key;
            if(f.id<60&&!db.nodes.contains(f.publisher))fail("未知帧发布者："+f.name);
            f.classicChecksum=db.version.startsWith("1.")||db.nodeAttributes.value(f.publisher).value("LIN_protocol").startsWith("1.")||f.id>=60;
            for(auto &s:f.fields){if(!fields.contains(s.name))fail("未知帧信号引用："+s.name);int offset=s.start;s=fields[s.name];s.start=offset;
                if(f.id<60&&s.publisher!=f.publisher)fail("帧/信号发布者矛盾："+f.name+"/"+s.name);
                if(!s.issue.isEmpty())f.issue+=(f.issue.isEmpty()?QString():"；")+s.issue;
            }
            const auto issue=SignalCodec::validateFrame(f);if(!issue.isEmpty())fail(f.name+": "+issue);
            if(!f.issue.isEmpty())db.diagnostics.append(f.name+": "+f.issue);
        }
        for(auto it=db.nodeAttributes.begin();it!=db.nodeAttributes.end();++it)if(!db.nodes.contains(it.key()))fail("属性引用未知节点："+it.key());
        for(auto&s:db.schedules){for(auto&slot:s.entries){if(keyByName.contains(slot.frame))slot.frame=keyByName.value(slot.frame);
                else if(unsupportedFrames.contains(slot.frame))slot.issue=unsupportedFrames.value(slot.frame);
                else if(slot.issue.isEmpty())fail("调度引用未知帧："+slot.frame);
                if(!slot.issue.isEmpty())s.issue=slot.issue;}
            if(!s.issue.isEmpty())db.diagnostics.append(s.name+": "+s.issue);
        }
        if(db.frames.isEmpty())fail("没有可浏览的帧");
    }
public:
    Parser(const QString &text,DatabaseDefinition&d):db(d){
        int i=0,line=1,column=1;
        auto step=[&](){if(text[i]=='\n'){++line;column=1;}else ++column;++i;};
        while(i<text.size()){
            if(text[i].isSpace()){step();continue;}
            if(text.mid(i,2)=="//"){while(i<text.size()&&text[i]!='\n')step();continue;}
            if(text.mid(i,2)=="/*"){step();step();while(i<text.size()&&text.mid(i,2)!="*/")step();if(i==text.size())throw ParseError("未闭合块注释");step();step();continue;}
            Token token;token.line=line;token.column=column;const auto c=text[i];
            if(c=='"'){token.quoted=true;step();bool closed=false;while(i<text.size()){if(text[i]=='"'){step();closed=true;break;}if(text[i]=='\\'){step();if(i==text.size())break;const auto escaped=text[i];token.text+=escaped=='n'?QChar('\n'):escaped=='t'?QChar('\t'):escaped;step();}else{token.text+=text[i];step();}}if(!closed)throw ParseError(QString("第 %1 行：字符串未闭合").arg(line));}
            else if(c.isLetter()||c=='_'){while(i<text.size()&&(text[i].isLetterOrNumber()||text[i]=='_')){token.text+=text[i];step();}}
            else if(c.isDigit()||c=='-'||c=='+'){while(i<text.size()&&(text[i].isLetterOrNumber()||QString(".+-").contains(text[i]))){token.text+=text[i];step();}}
            else if(QString("{}:,;=%").contains(c)){token.text=c;step();}
            else throw ParseError(QString("第 %1 行，第 %2 列：不支持的字符 %3").arg(line).arg(column).arg(c));
            tokens.append(token);
        }tokens.append({"<EOF>",line,column,false});
    }
    void run(){QSet<QString> blocks;
        while(peek().text!="<EOF>"){const auto section=name();if(blocks.contains(section))fail("重复顶层定义："+section);blocks.insert(section);
            if(section=="LIN_description_file"){expect(";");header=true;}
            else if(section=="LIN_protocol_version"||section=="LIN_language_version"){expect("=");const auto v=string();if(v!="1.3"&&v!="2.0"&&v!="2.1"&&v!="2.2"&&v!="2.2A")fail("不支持的 LIN 版本："+v);if(section=="LIN_protocol_version")db.version=v;expect(";");}
            else if(section=="LIN_speed"){expect("=");const auto value=decimal::require(number())*decimal::Number(1000);if(value.n%value.d!=0||value.n<1000*value.d||value.n>20000*value.d)fail("无效 LIN_speed");db.bitrate=(value.n/value.d).convert_to<int>();expect("kbps");expect(";");}
            else if(section=="Channel_name"||section=="LDF_file_revision"){expect("=");string();expect(";");}
            else if(section=="Nodes")parseNodes();else if(section=="Signals")parseSignals(false);
            else if(section=="Diagnostic_signals")parseSignals(true);else if(section=="Frames")parseFrames(false);
            else if(section=="Diagnostic_frames")parseFrames(true);else if(section=="Schedule_tables")parseSchedules();
            else if(section=="Signal_encoding_types")parseEncodings();else if(section=="Signal_representation")parseRepresentations();
            else if(section=="Node_attributes")parseAttributes();
            else if(section=="Event_triggered_frames"||section=="Sporadic_frames")parseSpecialFrames(section);
            else if(section=="Diagnostic_addresses"||section=="Signal_groups"||section=="composite"){balanced();db.diagnostics.append(section+"：仅识别，不用于执行");}
            else fail("首版不支持顶层语义："+section);
        }resolve();
    }
};
}
ImportResult DatabaseImporter::ldf(const QByteArray&bytes,const QString&path){
    auto db=QSharedPointer<DatabaseDefinition>::create();db->bus=Bus::Lin;QString text,error;
    if(!prepare(bytes,path,*db,text,error))return {{},error};
    try{Parser parser(text,*db);parser.run();return {db,{}};}catch(const std::exception&e){return {{},path+": "+QString::fromUtf8(e.what())};}
}
}
