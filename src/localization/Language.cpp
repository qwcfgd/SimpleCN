#include "Language.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
static void initLanguageResources(){Q_INIT_RESOURCE(language);}
namespace host {
namespace {
struct Pattern { QRegularExpression expression; QString translation; QStringList arguments; };
struct Catalog {
    QHash<QString,QString> exact;
    QVector<Pattern> patterns;
    QStringList fragments;
    Catalog(){
        initLanguageResources();QFile file(":/i18n/en.json");if(!file.open(QIODevice::ReadOnly))qFatal("Cannot load language catalog");
        const auto object=QJsonDocument::fromJson(file.readAll()).object();
        for(auto it=object.begin();it!=object.end();++it)exact.insert(it.key(),it.value().toString());
        auto keys=exact.keys();std::sort(keys.begin(),keys.end(),[](const QString&a,const QString&b){return a.size()>b.size();});
        const QRegularExpression placeholder("%[1-9][0-9]*");
        for(const auto &key:keys){
            auto matches=placeholder.globalMatch(key);if(!matches.hasNext()){fragments.append(key);continue;}
            Pattern pattern;pattern.translation=exact.value(key);QString expression="\\A";int pos=0;
            while(matches.hasNext()){
                const auto match=matches.next();expression+=QRegularExpression::escape(key.mid(pos,match.capturedStart()-pos));
                const int previous=pattern.arguments.indexOf(match.captured());
                if(previous>=0)expression+="\\g{"+QString::number(previous+1)+"}";
                else{expression+="(.*?)";pattern.arguments.append(match.captured());}
                pos=match.capturedEnd();
            }
            expression+=QRegularExpression::escape(key.mid(pos))+"\\z";
            pattern.expression=QRegularExpression(expression,QRegularExpression::DotMatchesEverythingOption);patterns.append(pattern);
        }
    }
    QString render(const QString &source,int depth=0)const{
        if(source.isEmpty()||depth>8)return source;
        auto found=exact.constFind(source);if(found!=exact.cend())return *found;
        static const QRegularExpression chinese("[\\x{3400}-\\x{9fff}]");if(!source.contains(chinese))return source;
        static const QRegularExpression timestamp("\\A[0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3}  ");
        const auto stamp=timestamp.match(source);if(stamp.hasMatch())return stamp.captured()+render(source.mid(stamp.capturedLength()),depth+1);
        for(const auto &pattern:patterns){const auto match=pattern.expression.match(source);if(!match.hasMatch())continue;
            QString result;int pos=0;auto placeholders=QRegularExpression("%[1-9][0-9]*").globalMatch(pattern.translation);
            while(placeholders.hasNext()){const auto slot=placeholders.next();result+=pattern.translation.mid(pos,slot.capturedStart()-pos);const int n=pattern.arguments.indexOf(slot.captured());result+=n<0?slot.captured():render(match.captured(n+1),depth+1);pos=slot.capturedEnd();}
            return result+pattern.translation.mid(pos);
        }
        if(source.contains('\n')){QStringList lines;for(const auto &line:source.split('\n'))lines.append(render(line,depth+1));return lines.join('\n');}
        // Composed application messages retain their source in the model. Use
        // longest complete catalog fragments, never character transliteration.
        QString result;int pos=0;
        while(pos<source.size()){
            int next=source.size();QString key;
            for(const auto &candidate:fragments){const int at=source.indexOf(candidate,pos);if(at>=0&&(at<next||(at==next&&candidate.size()>key.size()))){next=at;key=candidate;}}
            result+=source.mid(pos,next-pos);if(key.isEmpty())break;result+=exact.value(key);pos=next+key.size();
        }
        return result;
    }
};
const Catalog &catalog(){static const Catalog instance;return instance;}
}
Language::Language():QObject(QCoreApplication::instance()){}
Language &Language::instance(){static Language *language=new Language;return *language;}
void Language::setCode(const QString&code){const bool next=code=="en";if(m_english==next)return;m_english=next;emit changed();}
QString Language::text(const QString&source){return instance().english()?catalog().render(source):source;}
}
