#include "DbcDefinitionScope.h"
#include <QRegularExpression>
#include <QVector>
#include <algorithm>

namespace host::signal {
namespace {
struct Field { QString name; int pos, line; };
struct Message {
    quint64 id, parserId;
    QString name;
    int pos, size, line;
    QVector<Field> fields;
};
struct Edit { int pos, size; QString value; };

} // namespace

// Keep positions and line breaks intact, including those in DBC multiline comments.
QString DbcDefinitionScope::structuralText(QString text) {
    bool quoted=false, escaped=false, lineComment=false, blockComment=false;
    for(int i=0;i<text.size();++i) {
        const QChar c=text[i], next=i+1<text.size()?QChar(text[i+1]):QChar();
        if(c=='\n') { lineComment=false; continue; }
        if(lineComment) { text[i]=' '; continue; }
        if(blockComment) {
            text[i]=' ';
            if(c=='*' && next=='/') { text[++i]=' '; blockComment=false; }
        } else if(quoted) {
            if(!escaped && c=='"') quoted=false;
            else { text[i]=' '; escaped=!escaped && c=='\\'; }
        } else if(c=='"') { quoted=true; escaped=false; }
        else if(c=='/' && (next=='/' || next=='*')) {
            blockComment=next=='*'; lineComment=!blockComment; text[i]=' ';text[++i]=' ';
        }
    }
    return text;
}

QString DbcDefinitionScope::prepare(const QString &input, QStringList &notes) {
    const QString syntax=structuralText(input);
    const QRegularExpression declaration(R"(^[ \t]*(?:BO_[ \t]+(\d+)[ \t]+(\w+)[ \t]*:|SG_[ \t]+(\w+)(?:[ \t]+\w+)?[ \t]*:))",
                                          QRegularExpression::MultilineOption);
    QVector<Message> frames;
    QHash<quint64,QVector<int>> groups;
    QSet<quint64> occupied;
    QVector<Edit> edits;
    int line=1, previous=0;
    auto lineAt=[&](int pos) {
        while(previous<pos) if(syntax.at(previous++)=='\n') ++line;
        return line;
    };
    auto it=declaration.globalMatch(syntax);
    while(it.hasNext()) {
        const auto m=it.next(); const int n=lineAt(m.capturedStart());
        if(!m.captured(1).isEmpty()) {
            const auto id=m.captured(1).toULongLong();
            groups[id].append(frames.size()); occupied.insert(id);
            frames.append({id,id,m.captured(2),int(m.capturedStart(1)),int(m.capturedLength(1)),n,{}});
        } else if(!frames.isEmpty()) {
            frames.last().fields.append({m.captured(3),int(m.capturedStart(3)),n});
        }
    }
    quint64 next=0x80000000ULL;
    QSet<quint64> needsReferences;
    for(auto &frame:frames) {
        DbcMessageScope info;
        info.line=frame.line; info.wireId=frame.id;
        if(groups[frame.id].size()>1) {
            while(occupied.contains(next)) ++next;
            frame.parserId=next++; occupied.insert(frame.parserId);
            edits.append({frame.pos,frame.size,QString::number(frame.parserId)});
            needsReferences.insert(frame.id);
            info.suffix="/"+frame.name;
            int homonyms=0;
            for(int index:groups[frame.id]) if(frames[index].name==frame.name) ++homonyms;
            if(homonyms>1) info.suffix+="/"+QString::number(frame.line);
        }
        QHash<QString,int> counts;
        for(const auto &field:frame.fields) ++counts[field.name];
        for(const auto &field:frame.fields) if(counts[field.name]>1) {
            QString alias="SimpleCN_field_"+QString::number(field.line);
            while(counts.contains(alias)||info.signalNames.contains(alias)) alias+='_';
            edits.append({field.pos,int(field.name.size()),alias});
            info.signalNames.insert(alias,field.name);
            needsReferences.insert(frame.id);
        }
        messages.insert(frame.parserId,info);
    }
    if(needsReferences.isEmpty()) return input;
    for(auto group=groups.cbegin();group!=groups.cend();++group) if(group.value().size()>1) {
        QStringList names;
        for(int index:group.value()) names.append(frames[index].name);
        notes.append(QString("CAN ID 0x%1 有多个候选：%2。请在报文树中选择使用的定义。")
                     .arg(group.key()&0x1fffffff,0,16).arg(names.join(", ")));
    }

    // BO_ targets have no declaration identity. SG_ targets can belong to a
    // unique candidate if its signal name occurs exactly once in the ID group.
    // Mask unresolved records only in parser input. Source and line evidence stay intact.
    const QRegularExpression reference(
        R"rx(\b(?:(?:CM_|BA_[ \t\r\n]+"[^"]*")[ \t\r\n]+(BO_|SG_)|(VAL_|SIG_VALTYPE_|SIG_TYPE_REF_|SG_MUL_VAL_|SIG_GROUP_|BO_TX_BU_))[ \t\r\n]+(\d+)\b(?:[ \t]+(\w+))?)rx");
    it=reference.globalMatch(syntax);
    previous=0;line=1;
    while(it.hasNext()) {
        const auto m=it.next();
        const auto id=m.captured(3).toULongLong();
        if(!needsReferences.contains(id)) continue;
        const QString kind=m.captured(1).isEmpty()?m.captured(2):m.captured(1);
        const bool signal=kind=="SG_" || kind=="VAL_" || kind=="SIG_VALTYPE_" ||
                          kind=="SIG_TYPE_REF_" || kind=="SG_MUL_VAL_";
        const auto candidates=groups.value(id);
        int count=0, owner=-1;
        for(int index:candidates) {
            if(signal) {
                for(const auto &field:frames[index].fields) if(field.name==m.captured(4)) { ++count; owner=index; }
            } else { ++count; owner=index; }
        }
        if(count==1) {
            if(kind=="SG_MUL_VAL_") {
                const auto rest=syntax.mid(m.capturedEnd()).trimmed().section(QRegularExpression("\\s+"),0,0);
                int selectors=0;
                for(const auto &field:frames[owner].fields) if(field.name==rest) ++selectors;
                if(selectors!=1) count=0;
            }
        }
        if(count==1) {
            if(frames[owner].parserId!=id)
                edits.append({int(m.capturedStart(3)),int(m.capturedLength(3)),QString::number(frames[owner].parserId)});
        } else {
            const int end=syntax.indexOf(';',m.capturedEnd(3));
            if(end<0) continue; // Leave malformed input for the grammar parser.
            const int n=lineAt(m.capturedStart());
            QString blank=input.mid(m.capturedStart(),end-m.capturedStart()+1);
            for(auto &ch:blank) if(ch!='\n'&&ch!='\r') ch=' ';
            edits.append({int(m.capturedStart()),int(blank.size()),blank});
            notes.append(QString("第 %1 行：0x%2 的 %3 引用无法唯一归属，未应用。")
                         .arg(n).arg(id&0x1fffffff,0,16).arg(kind));
            if(kind=="SIG_VALTYPE_"||kind=="SIG_TYPE_REF_"||kind=="SG_MUL_VAL_")
                for(int index:candidates) messages[frames[index].parserId].issue="信号类型或复用引用无法唯一归属";
        }
    }
    std::sort(edits.begin(),edits.end(),[](const Edit &a,const Edit &b){return a.pos>b.pos;});
    QString result=input;
    for(const auto &edit:edits) result.replace(edit.pos,edit.size,edit.value);
    return result;
}
}
