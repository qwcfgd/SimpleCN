#include "TransmitPayload.h"
#include "SignalCodec.h"
#include <QRegularExpression>
namespace host::signal {
bool TransmitPayload::validLength(Bus bus,quint32 id,int length){
    return length>=(bus==Bus::Can?0:1)&&length<=8&&!(bus==Bus::Lin&&id>=60&&length!=8);
}
bool TransmitPayload::containsSignal(const FrameDefinition&f,int field,int length){
    if(field<0||field>=f.fields.size())return false;
    auto fits=[length](const SignalDefinition&s){for(int bit:SignalCodec::bitPositions(s))if(bit<0||bit>=length*8)return false;return true;};
    if(!fits(f.fields[field]))return false;
    if(f.fields[field].multiplexed)for(const auto&s:f.fields)if(s.selector&&!fits(s))return false;
    return true;
}
bool TransmitPayload::parse(const QString&text,int length,QByteArray&bytes,QString&error){
    auto hex=text;hex.remove(QRegularExpression("\\s"));
    if(length<0||length>8||hex.size()>length*2||hex.size()%2||hex.contains(QRegularExpression("[^0-9a-fA-F]"))){
        error=QString("请输入不超过 DLC（%1 字节）的完整 HEX 字节；未应用").arg(length);return false;
    }
    bytes=QByteArray::fromHex(hex.toLatin1()).leftJustified(length,char(0));error.clear();return true;
}
bool TransmitPayload::decode(const FrameDefinition&f,const QByteArray&bytes,QVector<RawValue>&values,QString&error){
    auto layout=f;layout.length=qMax(f.length,int(bytes.size()));
    // Missing tail bits are zero only for decoding. They never enter the wire payload.
    return SignalCodec::decode(layout,bytes.leftJustified(layout.length,char(0)),values,error);
}
bool TransmitPayload::encode(const FrameDefinition&f,const QVector<RawValue>&values,QByteArray&bytes,QString&error){
    const int length=bytes.size();auto layout=f;layout.length=qMax(f.length,length);
    auto full=bytes.leftJustified(layout.length,char(0));
    if(!SignalCodec::encode(layout,values,full,error))return false;
    bytes=full.left(length);return true;
}
}
