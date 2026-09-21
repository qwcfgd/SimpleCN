#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "model/DatabaseImporter.h"
#include "model/SignalCodec.h"
#include "model/CddDatabase.h"
using namespace host::signal;
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();if(args.size()!=3)return 2;
    if(args[1].endsWith(".cdd",Qt::CaseInsensitive)){
        host::diag::Database db;QString error;const bool ok=host::diag::Database::load(args[1],db,error);
        QJsonArray ecus;for(const auto &ecu:db.ecus){QJsonArray variants;for(const auto &v:ecu.variants){QJsonArray services;for(const auto &s:v.services)services.append(QJsonObject{{"name",s.name},{"qualifier",s.qualifier},{"sid",s.sid},{"issue",s.issue},{"requestIssue",s.request.issue},{"responseIssue",s.response.issue},{"requestFields",s.request.fields.size()},{"responseFields",s.response.fields.size()}});variants.append(QJsonObject{{"name",v.name},{"services",services}});}ecus.append(QJsonObject{{"name",ecu.name},{"variants",variants}});}
        QFile output(args[2]);if(!output.open(QIODevice::WriteOnly))return 6;
        output.write(QJsonDocument(QJsonObject{{"error",error},{"version",db.version},{"ecus",ecus},{"warnings",QJsonArray::fromStringList(db.warnings)}}).toJson());return ok?0:3;
    }
    const auto bus=args[1].endsWith(".dbc",Qt::CaseInsensitive)?Bus::Can:Bus::Lin;
    const auto result=DatabaseImporter::load(args[1],bus);if(!result.database){QFile output(args[2]);if(output.open(QIODevice::WriteOnly))output.write(QJsonDocument(QJsonObject{{"error",result.error}}).toJson());return 3;}
    QJsonArray frames;
    for(const auto&f:result.database->frames){
        QJsonArray fields,samples;for(const auto&s:f.fields)fields.append(QJsonObject{{"name",s.name},{"start",s.start},{"width",s.width},{"array",s.array},{"signed",s.isSigned},{"littleEndian",s.littleEndian},{"initial",s.initial}});
        if(f.issue.isEmpty())for(int sample=0;sample<3;++sample){
            TxDraft draft;QString error;if(!SignalCodec::initialize(f,bus,draft,error))return 4;
            if(sample)for(int i=0;i<f.fields.size();++i){const auto&s=f.fields[i];auto&v=draft.values[i];
                if(s.array)v.bytes.fill(sample==1?0:char(0xa5),s.width/8);
                else v.bits=s.selector?quint64(sample-1):(sample==1?0:0xa5c3e17f01234567ULL)&SignalCodec::mask(s.width);
            }
            auto payload=draft.applied.bytes;if(!SignalCodec::encode(f,draft.values,payload,error))return 5;
            QVector<RawValue> decoded;if(!SignalCodec::decode(f,payload,decoded,error))return 7;
            for(int i=0;i<f.fields.size();++i)if(SignalCodec::isActive(f,i,draft.values)){
                const auto &expected=draft.values[i],&actual=decoded[i];
                if(f.fields[i].array?expected.bytes!=actual.bytes:expected.bits!=actual.bits)return 8;
            }
            QJsonObject values;for(int i=0;i<f.fields.size();++i)values[f.fields[i].name]=SignalCodec::rawText(f.fields[i],draft.values[i]);
            samples.append(QJsonObject{{"values",values},{"payload",QString::fromLatin1(payload.toHex())}});
        }
        frames.append(QJsonObject{{"key",f.key},{"name",f.name},{"id",int(f.id)},{"length",f.length},{"extended",f.extended},{"publisher",f.publisher},{"issue",f.issue},{"fields",fields},{"samples",samples}});
    }
    QJsonArray schedules;for(const auto&s:result.database->schedules){QJsonArray entries;for(const auto&slot:s.entries)entries.append(QJsonObject{{"frame",slot.frame},{"delayMs",slot.delayMs}});
        schedules.append(QJsonObject{{"name",s.name},{"slots",entries},{"issue",SignalCodec::validateSchedule(s,result.database->frames,result.database->bitrate)}});}
    QFile output(args[2]);if(!output.open(QIODevice::WriteOnly))return 6;
    output.write(QJsonDocument(QJsonObject{{"frames",frames},{"nodes",QJsonArray::fromStringList(result.database->nodes)},{"schedules",schedules},
        {"sha256",result.database->sha256},{"diagnostics",QJsonArray::fromStringList(result.database->diagnostics)}}).toJson());return 0;
}
