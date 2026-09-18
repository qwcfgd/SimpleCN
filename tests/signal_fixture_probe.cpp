#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "model/DatabaseImporter.h"
#include "model/SignalCodec.h"
using namespace host::signal;
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();if(args.size()!=3)return 2;
    const auto bus=args[1].endsWith(".dbc")?Bus::Can:Bus::Lin;
    const auto result=DatabaseImporter::load(args[1],bus);if(!result.database)return 3;
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
            QJsonObject values;for(int i=0;i<f.fields.size();++i)values[f.fields[i].name]=SignalCodec::rawText(f.fields[i],draft.values[i]);
            samples.append(QJsonObject{{"values",values},{"payload",QString::fromLatin1(payload.toHex())}});
        }
        frames.append(QJsonObject{{"name",f.name},{"id",int(f.id)},{"length",f.length},{"extended",f.extended},{"publisher",f.publisher},{"issue",f.issue},{"fields",fields},{"samples",samples}});
    }
    QFile output(args[2]);if(!output.open(QIODevice::WriteOnly))return 6;
    output.write(QJsonDocument(QJsonObject{{"frames",frames},{"nodes",QJsonArray::fromStringList(result.database->nodes)}}).toJson());return 0;
}
