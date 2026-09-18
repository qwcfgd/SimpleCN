#include "CddDatabase.h"
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QSet>
#include <memory>
#include <vector>
#include <cmath>
#include <functional>

namespace host::diag {
namespace {
struct Node {
    QString tag,text; QMap<QString,QString> attrs;
    std::vector<std::unique_ptr<Node>> children;
    QString a(const QString &key,const QString &fallback={}) const {return attrs.value(key,fallback);}
    const Node *child(const QString &tag) const {for(const auto &c:children)if(c->tag==tag)return c.get();return nullptr;}
    QString content(const QString &tag) const {auto c=child(tag);return c?c->text.trimmed():QString();}
};
QString localized(const Node *n){
    if(!n)return {};
    for(const auto &c:n->children)if(c->tag=="TUV" && c->a("lang").startsWith("en"))return c->text.trimmed();
    for(const auto &c:n->children)if(c->tag=="TUV")return c->text.trimmed();
    return n->text.trimmed();
}
QString name(const Node *n){if(!n)return {};auto s=localized(n->child("NAME"));return s.isEmpty()?n->content("QUAL"):s;}
bool number(QString s,quint64 &out){
    s=s.trimmed();bool ok=false;
    if(s.startsWith('(')&&s.endsWith(')')){
        const auto parts=s.mid(1,s.size()-2).split(',');if(parts.isEmpty()||parts.size()>8)return false;
        out=0;for(auto p:parts){auto v=p.trimmed().toUInt(&ok,10);if(!ok||v>255)return false;out=(out<<8)|v;}return true;
    }
    out=s.toULongLong(&ok,s.startsWith("0x",Qt::CaseInsensitive)?16:10);return ok;
}
bool off(const QString &s){return s=="0"||s=="false"||s=="no";}
void descendants(const Node *n,const QString &tag,QVector<const Node*> &out){
    if(!n)return;for(const auto &c:n->children){if(c->tag==tag)out.append(c.get());descendants(c.get(),tag,out);}
}
class Parser {
public:
    QMap<QString,const Node*> ids;
    CommunicationParameters communicationDefaults() const {
        CommunicationParameters values;
        for(auto n:ids)if(n->tag.endsWith("DEF")&&n->attrs.contains("v")){
            const auto key=n->content("QUAL");
            if(key.startsWith("CAN.")||key.startsWith("LIN."))
                values[key]={name(n),n->a("v"),localized(n->child("UNIT")),"CDD 默认值"};
        }
        return values;
    }
    void communicationOverrides(const Node *n,CommunicationParameters &values,const QString &source) const {
        for(const auto &c:n->children){
            const auto definition=ids.value(c->a("attrref"));if(!definition||!c->attrs.contains("v"))continue;
            const auto key=definition->content("QUAL");
            if(key.startsWith("CAN.")||key.startsWith("LIN."))
                values[key]={name(definition),c->a("v"),localized(definition->child("UNIT")),source};
        }
    }
    QString error;
    int nodeCount=0;
    std::unique_ptr<Node> node(QXmlStreamReader &xml,int depth=0){
        if(depth>96||++nodeCount>250000){xml.raiseError("CDD structure exceeds parser limits");return {};}
        auto n=std::make_unique<Node>();n->tag=xml.name().toString();
        for(auto a:xml.attributes())n->attrs[a.name().toString()]=a.value().toString();
        auto id=n->a("id");if(!id.isEmpty()){
            if(ids.contains(id)){xml.raiseError("Duplicate CDD id: "+id);return {};}
            ids.insert(id,n.get());
        }
        while(!xml.atEnd()){
            xml.readNext();
            if(xml.isStartElement()){
                // Embedded documents/images have no diagnostic semantics.
                if(xml.name()==QStringLiteral("FILECONTENTS")){xml.skipCurrentElement();continue;}
                auto c=node(xml,depth+1);if(!c)return {};n->children.push_back(std::move(c));
            }else if(xml.isCharacters())n->text+=xml.text().toString();
            else if(xml.isEndElement())return n;
            else if(xml.isEntityReference()){xml.raiseError("Unresolved XML entity");return {};}
        }
        return {};
    }
    const Node *ref(const QString &id,QString &issue) const {
        const auto n=ids.value(id);if(!n&&!id.isEmpty())issue="未找到 CDD 引用："+id;return n;
    }
    struct Context {QMap<QString,QString> statics;QMap<QString,const Node*> proxies;};
    void mappings(const Node *n,Context &ctx,QString &issue){
        if(!n)return;
        for(const auto &c:n->children){
            if(c->tag=="STATICVALUE"){
                auto sh=ref(c->a("shstaticref"),issue);if(!sh)continue;
                for(const auto &r:sh->children)if(r->tag=="STATICCOMPREF")ctx.statics[r->a("idref")]=c->a("v");
            }else if(c->attrs.contains("shproxyref")){
                auto sh=ref(c->a("shproxyref"),issue);if(!sh)continue;
                for(const auto &r:sh->children)if(r->tag=="PROXYCOMPREF")ctx.proxies[r->a("idref")]=c.get();
            }
        }
    }
    void appendField(const Node *n,const Node *dt,Message &m,const QString &prefix,const Context &ctx){
        Field f;f.name=prefix+name(n);f.spec=n->a("spec");f.key=QString::number(m.fields.size());
        f.suppressible=n->a("respsupbit")=="1";
        const auto cv=dt?dt->child("CVALUETYPE"):nullptr;
        if(cv){
            f.bits=cv->a("bl").toInt();f.encoding=cv->a("enc","uns");
            if(cv->a("bo","21")!="21"&&cv->a("bo")!="12")m.issue="未知字节序："+cv->a("bo");
            f.littleEndian=cv->a("bo")=="12";f.array=cv->a("qty")=="field";
            if(f.array){f.minCount=cv->a("minsz","0").toInt();f.maxCount=cv->a("maxsz","4095").toInt();}
            auto sz=cv->a("sz","no");
            if(sz=="lszbyte")f.lengthBytes=1;
            else if(sz=="lsz2bytes")f.lengthBytes=2;
            else if(sz=="lsz3bytes")f.lengthBytes=3;
            else if(sz=="lsz4bytes")f.lengthBytes=4;
            else if(sz!="no")m.issue="不支持的长度编码："+sz;
            if(auto pv=dt->child("PVALUETYPE"))f.unit=pv->content("UNIT");
            if(auto comp=dt->child("COMP")){
                bool a=false,b=false;f.factor=comp->a("f","1").toDouble(&a);f.offset=comp->a("o","0").toDouble(&b);
                if(!a||!b||!std::isfinite(f.factor)||!std::isfinite(f.offset)||f.factor==0)m.issue="非法线性转换："+f.name;
                if(comp->attrs.contains("s")&&comp->attrs.contains("e")){
                    f.hasRange=number(comp->a("s"),f.minimum)&&number(comp->a("e"),f.maximum);
                    if(!f.hasRange)m.issue="非法数值范围："+f.name;
                }
            }
            for(const auto &c:dt->children){
                if(c->tag=="TEXTMAP"){
                    Choice choice;choice.text=localized(c->child("TEXT"));
                    if(number(c->a("s"),choice.first)&&number(c->a("e"),choice.last))f.choices.append(choice);
                    else m.issue="非法枚举值："+f.name;
                }
                if(c->tag=="EXCL"){
                    quint64 a=0,b=0;if(number(c->a("s"),a)&&number(c->a("e"),b))f.excluded.append({a,b});
                }
            }
        }else{
            f.bits=n->a("bl",n->a("minbl","8")).toInt();
            if(n->tag.contains("PROXYCOMP")){
                const int minBits=n->a("minbl","0").toInt(),maxBits=n->a("maxbl","32760").toInt();
                if(minBits%8||maxBits%8)m.issue="非字节对齐的原始数据区："+f.name;
                f.bits=8;f.array=true;f.encoding="hex";f.minCount=minBits/8;f.maxCount=maxBits/8;
                if(n->a("mustAtRT")=="0"||n->a("must")=="0")f.minCount=0;
            }else if(n->tag!="CONSTCOMP"&&n->tag!="GAPDATAOBJ")m.issue="字段缺少数据类型："+f.name;
        }
        if(n->tag=="GAPDATAOBJ"){f.constant=true;f.value=0;}
        if(n->tag=="CONSTCOMP"||ctx.statics.contains(n->a("id"))){
            f.constant=true;
            if(!number(ctx.statics.value(n->a("id"),n->a("v")),f.value))m.issue="非法常量："+f.name;
        }
        if(n->tag=="STATICCOMP"&&!f.constant)m.issue="静态参数缺少实例映射："+f.name;
        if(f.bits<1||f.bits>64||f.minCount<0||f.maxCount<f.minCount||f.maxCount>16777216)m.issue="无效字段长度："+f.name;
        if(f.littleEndian&&f.bits%8)m.issue="暂不支持非整字节的小端字段："+f.name;
        if(f.array&&f.bits%8)m.issue="暂不支持非整字节的数组元素："+f.name;
        if(!QStringList{"uns","sgn","asc","utf","utf8","bcd","flt","dbl","hex"}.contains(f.encoding))m.issue="未知编码："+f.encoding;
        m.fields.append(f);
    }
    void expand(const Node *n,Message &m,const Context &ctx,QString prefix={},QSet<const Node*> stack={}){
        if(!n)return;
        if(stack.contains(n)||stack.size()>64||m.fields.size()>4096){m.issue="CDD 循环引用或字段过多";return;}
        stack.insert(n);
        if(ctx.proxies.contains(n->a("id"))){expand(ctx.proxies.value(n->a("id")),m,ctx,prefix,stack);return;}
        const QString t=n->tag;
        if(t=="GODTCDATAOBJ"||t=="RECORDDATAOBJ"){
            const Node *dt=n->child("TEXTTBL");if(!dt)dt=n->child("RECORDDT");
            if(!dt)m.issue="内联数据类型缺失："+name(n);else appendField(n,dt,m,prefix,ctx);return;
        }
        if(t=="DIDDATAREF"||t=="DIDREF"){
            auto d=ref(n->a("didRef",n->a("idref")),m.issue);if(d)expand(d->child("STRUCTURE"),m,ctx,prefix,stack);return;
        }
        if(t=="DATAOBJ"||t=="STATICCOMP"||t=="CONSTCOMP"||t=="GAPDATAOBJ"||t.endsWith("PROXYCOMP")){
            auto dt=ref(n->a("dtref"),m.issue);
            if(dt&&(dt->tag=="STRUCTDT"||dt->tag=="STRUCTURE"))expand(dt,m,ctx,prefix+name(n)+" / ",stack);
            else appendField(n,dt,m,prefix,ctx);
            return;
        }
        if(t=="EOSITERCOMP"){
            // An open-ended repeated response remains visible as raw bytes. Never
            // pretend one repetition describes the whole record.
            Field f;f.key=QString::number(m.fields.size());f.name=prefix+name(n);f.encoding="hex";f.array=true;f.minCount=0;f.maxCount=4095;
            f.description="重复记录（原始 HEX）";m.fields.append(f);return;
        }
        if(t=="MUXCOMP"||t=="MUXDT"||t=="NUMITERCOMP"||t=="UNION"){
            m.issue="需要专用编解码的条件结构："+t+" / "+name(n);return;
        }
        static const QSet<QString> containers={"REQ","POS","SIMPLECOMPCONT","MUXCOMPCONT","CONTENTCOMP","STRUCTURE","STRUCT","STRUCTDT"};
        if(containers.contains(t)){for(const auto &c:n->children)expand(c.get(),m,ctx,prefix,stack);return;}
        if(t.endsWith("COMP")||t.endsWith("DATAOBJ")||t.endsWith("REF"))m.issue="不支持的报文字段："+t;
    }
    Service service(const Node *inst,const Node *s,const QString &group){
        Service result;result.id=s->a("id",s->a("oid"));result.qualifier=inst->content("QUAL")+"/"+s->content("QUAL");
        result.name=localized(s->child("SHORTCUTNAME"));if(result.name.isEmpty())result.name=name(inst)+" · "+name(s);
        result.group=group;result.conditions=s->a("mayBeExec");
        const Node *p=s;QSet<const Node*> visited;QVector<const Node*> templates;
        while(p&&p->tag!="PROTOCOLSERVICE"&&!p->child("REQ")){
            if(visited.contains(p)){result.issue="服务模板循环引用";return result;}visited.insert(p);
            templates.prepend(p);
            p=ref(p->a("tmplref"),result.issue);
        }
        if(!p){result.issue="服务模板缺失："+s->a("tmplref");return result;}
        result.physical=!off(s->a("phys",p->a("phys","1")));result.functional=!off(s->a("func",p->a("func","0")));
        Context ctx;auto it=ref(inst->a("tmplref"),result.issue);mappings(p,ctx,result.issue);mappings(it,ctx,result.issue);
        for(auto t:templates)if(t!=s)mappings(t,ctx,result.issue);
        mappings(inst,ctx,result.issue);mappings(s,ctx,result.issue);
        expand(p->child("REQ"),result.request,ctx);expand(p->child("POS"),result.response,ctx);
        if(result.request.fields.isEmpty())result.issue="请求定义为空";
        else {const auto &sid=result.request.fields.first();if(!sid.constant||sid.bits!=8||sid.value>255)result.issue="请求缺少固定 SID";else result.sid=int(sid.value);}
        if(!result.request.issue.isEmpty())result.issue=result.request.issue;
        if(s->a("sprmibonphys")=="never")for(auto &f:result.request.fields)f.suppressible=false;
        return result;
    }
    void instances(const Node *n,Variant &v,QString group={}){
        for(const auto &c:n->children){
            if(c->tag=="DIAGINST"){
                if(off(c->a("is_used","1"))||off(c->a("enabled","1")))continue;
                for(const auto &s:c->children)if(s->tag=="SERVICE"&&!off(s->a("is_used","1"))&&!off(s->a("enabled","1"))){
                    auto svc=service(c.get(),s.get(),group);
                    if(svc.physical||svc.functional)v.services.append(svc);
                }
            }else if(c->tag=="DIAGCLASS"||c->tag=="DIAGGROUP")instances(c.get(),v,name(c.get()));
        }
    }
    void exclusions(const Node *n,QSet<QString> &instances,QSet<QString> &services){
        for(const auto &c:n->children){
            if(c->tag=="DIAGINST"){
                const auto qualifier=c->content("QUAL");
                if(off(c->a("is_used","1"))||off(c->a("enabled","1")))instances.insert(qualifier);
                for(const auto &s:c->children)if(s->tag=="SERVICE"&&(off(s->a("is_used","1"))||off(s->a("enabled","1"))))
                    services.insert(qualifier+"/"+s->content("QUAL"));
            }else if(c->tag=="DIAGCLASS"||c->tag=="DIAGGROUP")exclusions(c.get(),instances,services);
        }
    }
};
}
bool Database::load(const QString &path,Database &out,QString &error){
    QFile f(path);if(!f.open(QIODevice::ReadOnly)){error="无法读取 CDD："+f.errorString();return false;}
    if(f.size()>32*1024*1024){error="CDD 文件超过 32 MiB";return false;}
    Database candidate;if(!parse(f.readAll(),candidate,error))return false;
    candidate.path=QFileInfo(path).absoluteFilePath();out=std::move(candidate);return true;
}
bool Database::parse(const QByteArray &bytes,Database &out,QString &error){
    error.clear();if(bytes.size()>32*1024*1024){error="CDD 文件超过 32 MiB";return false;}
    QXmlStreamReader xml(bytes);Parser parser;std::unique_ptr<Node> root;
    while(!xml.atEnd()){
        xml.readNext();
        if(xml.isDTD()&&!xml.entityDeclarations().isEmpty()){error="CDD 不允许自定义 XML 实体";return false;}
        if(xml.isStartElement()){
            if(root){error="CDD 存在多个根元素";return false;}root=parser.node(xml);
        }
    }
    if(xml.hasError()||!root){error=QString("CDD XML 第 %1 行：%2").arg(xml.lineNumber()).arg(xml.errorString());return false;}
    if(root->tag!="CANDELA"){error="文件不是 CANdela CDD";return false;}
    Database db;db.version=root->a("dtdvers");bool ok=false;int major=db.version.section('.',0,0).toInt(&ok);
    if(!ok||major<1||major>15){error="支持 CANdela 1–15；文件版本："+db.version;return false;}
    auto doc=root->child("ECUDOC");if(!doc){error="CDD 缺少 ECUDOC";return false;}
    for(const auto &e:doc->children)if(e->tag=="ECU"){
        Ecu ecu;ecu.id=e->a("id");ecu.name=name(e.get());ecu.qualifier=e->content("QUAL");
        QVector<const Node*> variants;descendants(e.get(),"VAR",variants);
        if(variants.isEmpty())variants.append(e.get());
        for(auto n:variants){Variant v;v.id=n->a("id");v.name=name(n);v.qualifier=n->content("QUAL");v.base=n->a("base")=="1";parser.instances(n,v);
            v.communication=parser.communicationDefaults();parser.communicationOverrides(e.get(),v.communication,"ECU: "+ecu.name);
            parser.communicationOverrides(n,v.communication,"Variant: "+v.name);ecu.variants.append(v);}
        // Explicit base references inherit by instance/service qualifier. Unrelated
        // variants are not silently mixed, and a panel selection must resolve.
        QVector<int> state(variants.size(),0);
        std::function<bool(int)> inherit=[&](int i){
            if(state[i]==2)return true;
            if(state[i]==1){error="Variant 基础引用存在循环："+ecu.variants[i].name;return false;}
            state[i]=1;
            auto baseRef=variants[i]->a("baseref",variants[i]->a("basevarref"));
            if(baseRef.isEmpty()){state[i]=2;return true;}
            int base=-1;for(int j=0;j<ecu.variants.size();++j)if(ecu.variants[j].id==baseRef)base=j;
            if(base<0){error="Variant 基础引用无效："+baseRef;return false;}
            if(!inherit(base))return false;
            ecu.variants[i].communication=ecu.variants[base].communication;
            parser.communicationOverrides(variants[i],ecu.variants[i].communication,"Variant: "+ecu.variants[i].name);
            auto merged=ecu.variants[base].services;
            QSet<QString> disabledInstances,disabledServices;parser.exclusions(variants[i],disabledInstances,disabledServices);
            for(int k=merged.size()-1;k>=0;--k)if(disabledInstances.contains(merged[k].qualifier.section('/',0,0))||disabledServices.contains(merged[k].qualifier))merged.removeAt(k);
            for(const auto &s:ecu.variants[i].services){int index=-1;for(int k=0;k<merged.size();++k)if(merged[k].qualifier==s.qualifier)index=k;if(index<0)merged.append(s);else merged[index]=s;}
            ecu.variants[i].services=merged;state[i]=2;return true;
        };
        for(int i=0;i<variants.size();++i)if(!inherit(i))return false;
        for(const auto &v:ecu.variants)for(const auto &s:v.services){
            if(!s.issue.isEmpty())db.warnings.append(s.name+"："+s.issue);
            if(!s.response.issue.isEmpty())db.warnings.append(s.name+" 响应："+s.response.issue);
        }
        db.ecus.append(ecu);
    }
    if(db.ecus.isEmpty()){error="CDD 中未找到 ECU";return false;}
    out=std::move(db);return true;
}
QString Field::constraint() const {
    QString s=array?QString("%1–%2 个 %3 bit 元素").arg(minCount).arg(maxCount).arg(bits):QString("%1 bit %2").arg(bits).arg(littleEndian?"小端":"大端");
    if(!unit.isEmpty())s+=" · "+unit;if(factor!=1||offset!=0)s+=QString(" · 物理值=原始值×%1+%2").arg(factor).arg(offset);
    if(!description.isEmpty())s+=" · "+description;return s;
}
QString Service::selector() const {
    QStringList parts;for(const auto &f:request.fields)if(f.constant&&(f.spec=="sub"||f.spec=="id"||f.spec=="accm"))parts.append(QString("%1=0x%2").arg(f.spec,QString::number(f.value,16).toUpper()));return parts.join(" ");
}
}
