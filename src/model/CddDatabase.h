#pragma once
#include <QByteArray>
#include <QMap>
#include <QStringList>
#include <QVector>
#include <QMetaType>

namespace host::diag {
struct Choice { quint64 first=0,last=0; QString text; };
struct Field {
    QString key,name,spec,encoding="uns",unit,description;
    int bits=8,minCount=1,maxCount=1,lengthBytes=0;
    bool littleEndian=false,array=false,constant=false,suppressible=false;
    quint64 value=0;
    double factor=1,offset=0;
    QVector<Choice> choices;
    QVector<QPair<quint64,quint64>> excluded;
    bool hasRange=false;
    quint64 minimum=0,maximum=0;
    QString constraint() const;
};
using Values=QMap<QString,QString>;
struct Message { QVector<Field> fields; QString issue; };
struct Service {
    QString id,name,qualifier,group,conditions,issue;
    int sid=-1;
    bool physical=true,functional=false;
    Message request,response;
    QString selector() const;
};
struct CommunicationParameter { QString name,value,unit,source; };
using CommunicationParameters=QMap<QString,CommunicationParameter>;
struct Variant { QString id,name,qualifier; bool base=false; QVector<Service> services; CommunicationParameters communication; };
struct Ecu { QString id,name,qualifier; QVector<Variant> variants; };
struct Database {
    QString path,version;
    QVector<Ecu> ecus;
    QStringList warnings;
    static bool load(const QString &path,Database &out,QString &error);
    static bool parse(const QByteArray &xml,Database &out,QString &error);
};
class Codec {
public:
    static bool parseHex(const QString &,QByteArray &,QString &);
    static bool encode(const Message &,const Values &,QByteArray &,QString &);
    static bool decode(const Message &,const QByteArray &,Values &,QString &);
    static QString preview(const Message &,const Values &);
    static QByteArray expected(const Service &,const QByteArray &request);
    static bool validate(const Service &,const QByteArray &,QString &);
    static QString describeResponse(const Service &,const QByteArray &);
    // Deterministic, explicitly simulated values; never used to fill a user's request.
    static QByteArray simulationResponse(const Service &,const QByteArray &,QMap<int,QByteArray> &);
};
struct Request { Service service; QByteArray bytes; };
}
Q_DECLARE_METATYPE(host::diag::Request)
