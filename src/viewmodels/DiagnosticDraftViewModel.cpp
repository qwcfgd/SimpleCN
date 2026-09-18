#include "DiagnosticDraftViewModel.h"
#include "domain/CddConfiguration.h"
namespace host {
bool DiagnosticDraftViewModel::loadDatabase(const QString&path,QString&error){
    diag::Database next;if(!diag::Database::load(path,next,error))return false;
    database=std::move(next);return true;
}
const diag::Ecu*DiagnosticDraftViewModel::ecu(const QString&key)const{
    const diag::Ecu*found=nullptr;
    for(const auto&e:database.ecus)if(e.id==key)return &e;
    for(const auto&e:database.ecus)if(e.name==key||e.qualifier==key){if(found)return nullptr;found=&e;}
    return found;
}
bool DiagnosticDraftViewModel::select(const QString&ecuKey,const QString&variantKey,bool importCommunication,int&protocol,QString&error){
    const auto*e=ecu(ecuKey);const diag::Variant*v=nullptr;protocol=2;
    if(e){for(const auto&item:e->variants)if(item.id==variantKey){v=&item;break;}
        if(!v)for(const auto&item:e->variants)if(item.name==variantKey||item.qualifier==variantKey){if(v){error="请选择 CDD 中有效且唯一的 ECU / Variant";return false;}v=&item;}}
    if(!e||!v){error="请选择 CDD 中有效且唯一的 ECU / Variant";return false;}
    auto next=settings;next.cddPath=database.path;next.cddEcu=e->id;next.cddVariant=v->id;
    protocol=cddProtocolState(*v,next.bus);
    if(protocol==2){error.clear();return false;}
    if(importCommunication&&!applyCddCommunication(*v,next,error))return false;
    settings=std::move(next);error.clear();return true;
}
DiagnosticPreview DiagnosticDraftViewModel::preview(const diag::Service*s,const diag::Values&values,bool manual,const QString&raw,bool suppress){
    DiagnosticPreview out;
    if(!s){out.error="请载入 CDD 并选择服务";return out;}
    if(manual){out.text=raw;if(diag::Codec::parseHex(raw,out.bytes,out.error))diag::Codec::validate(*s,out.bytes,out.error);return out;}
    const bool encoded=diag::Codec::encode(s->request,values,out.bytes,out.error);
    if(encoded&&suppress&&out.bytes.size()>1)out.bytes[1]=char(quint8(out.bytes[1])|0x80);
    if(encoded)diag::Codec::validate(*s,out.bytes,out.error);
    out.text=encoded?QString::fromLatin1(out.bytes.toHex(' ')).toUpper():diag::Codec::preview(s->request,values);return out;
}
}
