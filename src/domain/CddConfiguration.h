#pragma once
#include "HostTypes.h"
#include "model/CddDatabase.h"
#include "protocol/CanTransport.h"
#include <cmath>

namespace host {
// 0: no transport metadata, 1: matching transport, 2: incompatible transport.
inline int cddProtocolState(const diag::Variant &variant,communication::Bus bus){
    bool can=false,lin=false;
    for(auto i=variant.communication.constBegin();i!=variant.communication.constEnd();++i){
        can|=i.key().startsWith("CAN.",Qt::CaseInsensitive);
        lin|=i.key().startsWith("LIN.",Qt::CaseInsensitive);
    }
    if(!can&&!lin)return 0;
    return (bus==communication::Bus::Can?can:lin)?1:2;
}

// Only the active transport's canonical CANdela attributes are applied. Other
// namespaces (CAN FD, extended addressing, DoIP) must not configure classic CAN.
inline bool applyCddCommunication(const diag::Variant &variant,ChannelSettings &settings,QString &error){
    auto s=settings;error.clear();
    if(cddProtocolState(variant,s.bus)==2){error="CDD 协议与当前通道不一致";return false;}
    const QString prefix=s.bus==communication::Bus::Can?"CAN.":"LIN.";
    auto value=[&](const QString &key,int &target,bool duration=false){
        auto it=variant.communication.constEnd();
        for(auto i=variant.communication.constBegin();i!=variant.communication.constEnd();++i)
            if(i.key().compare(prefix+key,Qt::CaseInsensitive)==0){it=i;break;}
        if(it==variant.communication.constEnd())return;
        QString raw=it->value.trimmed();bool ok=false;double number=0;
        if(raw.startsWith("0x",Qt::CaseInsensitive))number=raw.toULongLong(&ok,16);
        else if(raw.startsWith('(')&&raw.endsWith(')')){
            ok=true;for(auto part:raw.mid(1,raw.size()-2).split(',')){bool valid=false;auto byte=part.trimmed().toUInt(&valid);if(!valid||byte>255){ok=false;break;}number=number*256+byte;}
        }else number=raw.toDouble(&ok);
        if(duration){const auto unit=it->unit.trimmed().toLower();if(unit=="s")number*=1000;else if(unit=="us"||unit==QString::fromUtf8("µs"))number/=1000;}
        if(!ok||!std::isfinite(number)||number<0||number>2147483647.0){error="CDD 通信参数无效："+it.key()+" = "+raw;return;}
        target=int(std::ceil(number));
    };
    value("P2Client",s.p2Ms,true);value("P2ExClient",s.p2StarMs,true);
    value("P2Server",s.p2ServerMs,true);value("P2ExServer",s.p2StarServerMs,true);
    value("P3ClientPhys",s.p3Ms,true);value("S3Client",s.testerPresentMs,true);
    int completion=s.maxPendingMs;value("RC78CompletionTimeout",completion,true);
    // Zero means no CDD completion deadline; retain the application's finite bound.
    if(completion>0)s.maxPendingMs=completion;
    s.maxPendingMs=qMax(s.maxPendingMs,qMax(s.p2Ms,s.p2StarMs));
    int tester=-1;value("TesterPresentPhys",tester);
    if(tester>=0)s.testerPresentRequest=QString::number(tester,16).rightJustified(4,'0').toUpper();
    // Bitrate belongs to the connected hardware, never to CDD configuration.
    if(s.bus==communication::Bus::Can){
        int extended=s.extendedId?1:0;value("CanIdType",extended);
        if(extended>1){error="CDD CAN ID 类型不支持";return false;}s.extendedId=extended==1;
        auto id=[&](const char *key,QString &text){bool ok=false;int v=int(text.toUInt(&ok,16));value(key,v);text=QString::number(v,16).toUpper();};
        id("ReqCanId",s.requestId);id("ResCanId",s.responseId);id("ReqCanIdFunc",s.functionalId);
        const QMap<QString,QString> network={{"Blocksize","blockSize"},{"StMin","stMin"},{"TimeoutAs","nAsMs"},{"TimeoutAr","nArMs"},{"TimeoutBs","nBsMs"},{"TimeoutCr","nCrMs"},{"MaxLengthTpMessage","receiveCapacity"},{"CANFrameFillerByte","padding"}};
        const auto defaults=boot::CanOptions{}.toJson();
        for(auto i=network.constBegin();i!=network.constEnd();++i){int v=s.canNetwork.value(i.value()).toInt(defaults.value(i.value()).toInt());value(i.key(),v,i.key().startsWith("Timeout"));s.canNetwork[i.value()]=v;}
    }else{
        int nad=s.nad.toInt(nullptr,16);value("NAD",nad);s.nad=QString::number(nad,16).toUpper();
        value("TimeoutAs",s.linAsMs,true);value("TimeoutCr",s.linCrMs,true);value("SlotTime",s.linSlotMs,true);
    }
    if(!error.isEmpty())return false;
    communication::SoftwareChannelConfiguration config;if(!s.toConfiguration(config,error))return false;
    settings=s;return true;
}
}
