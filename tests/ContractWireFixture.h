#pragma once
#include "protocol/FlashJob.h"

// Independent wire fixture: fixed routine IDs and request shapes. No hardware.
class ContractWire final:public boot::DiagnosticTransport {
public:
    QList<QByteArray> requests;QMap<quint32,QByteArray> written;
    bool rejectSecurity=false,rejectIdentity=false;quint64 epoch=0;quint32 address=0;
    QByteArray eraseRid=QByteArray::fromHex("ff00"),verifyRid=QByteArray::fromHex("dd02"),dependencyRid=QByteArray::fromHex("ff01");
    QByteArray responseFor(const QByteArray &request){
        QByteArray reply=request;
        const int sid=quint8(request[0]);
        if((sid==0x10||sid==0x28||sid==0x85)&&quint8(request[1])&0x80)return {};
        reply[0]=char(sid+0x40);
        if(sid==0x10)reply=reply.left(2)+QByteArray::fromHex("00140002");
        else if(sid==0x27)reply=reply.left(2)+((quint8(request[1])&1)?QByteArray::fromHex("12345678"):QByteArray());
        else if(sid==0x2e)reply=reply.left(3);
        else if(sid==0x22)reply=rejectIdentity?QByteArray::fromHex("7f2231"):reply.left(3)+QByteArray("SIM-ECU");
        else if(sid==0x14)reply=reply.left(1);
        else if(sid==0x31){
            const auto rid=request.mid(2,2);
            if(rid==eraseRid&&request.size()==13)reply=reply.left(4)+char(0);
            else if(rid==verifyRid&&request.size()==16)reply=rejectSecurity?QByteArray::fromHex("7f3131"):reply.left(4)+char(0);
            else if(rid==dependencyRid&&request.size()==4)reply=reply+char(0);
            else reply=QByteArray::fromHex("7f3131");
        }else if(sid==0x34){address=boot::readBe(request.mid(3,4));written[address].clear();reply=QByteArray::fromHex("74200402");}
        else if(sid==0x36){written[address]+=request.mid(2);reply=reply.left(2);}
        return reply;
    }
    bool send(const QByteArray &request,QString &)override{
        requests.append(request);const auto token=epoch;
        QTimer::singleShot(0,this,[this,request,token]{
            if(token!=epoch)return;
            emit sent();const auto reply=responseFor(request);
            if(!reply.isEmpty())emit received(reply);
        });return true;
    }
    void cancel()override{++epoch;}int maximumPdu()const override{return 4095;}
};
