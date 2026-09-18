#pragma once
#include "domain/HostTypes.h"
#include "model/CddDatabase.h"
namespace host {
struct DiagnosticPreview { QByteArray bytes;QString text,error; };
// A dialog-local transaction. Cancel discards it without changing the channel.
class DiagnosticDraftViewModel {
public:
    DiagnosticDraftViewModel(ChannelSettings settings,diag::Database database)
        :settings(std::move(settings)),database(std::move(database)){}
    ChannelSettings settings;
    diag::Database database;
    bool loadDatabase(const QString&,QString&);
    const diag::Ecu *ecu(const QString&)const;
    bool select(const QString &ecuKey,const QString &variantKey,bool importCommunication,int &protocolState,QString&);
    static DiagnosticPreview preview(const diag::Service*,const diag::Values&,bool manual,const QString &raw,bool suppress);
};
}
