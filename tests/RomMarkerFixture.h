#pragma once
#include "protocol/FirmwareImage.h"
#include <cstring>
namespace marker_fixture {
inline void put32(QByteArray &data,int at,quint32 value){for(int i=0;i<4;++i)data[at+i]=char(value>>(8*i));}
inline void put64(QByteArray &data,int at,quint64 value){for(int i=0;i<8;++i)data[at+i]=char(value>>(56-8*i));}
inline void seal(QByteArray &data,quint32 kind,const QByteArray &stage="AST1",const QByteArray &date="20261010",int bootBytes=0xf000){
    const int at=kind==ROM_MARKER_DRIVER?0:kind==ROM_MARKER_BOOT?bootBytes-64:int(ROM_MARKER_APP_OFFSET);
    const int skip=kind==ROM_MARKER_DRIVER||kind==ROM_MARKER_APP?0:at;
    const int skipBytes=kind==ROM_MARKER_APP?4096:64;
    QByteArray marker(64,char(0xff));marker.replace(0,8,"S32K144T");
    const QByteArray type=kind==ROM_MARKER_DRIVER?"DRIVER":kind==ROM_MARKER_BOOT?"BOOT":kind==ROM_MARKER_APP?"APP":"CAL";
    marker.replace(8,type.size(),type);
    const auto dateCrc=RomMarker_Crc(0,reinterpret_cast<const uint8_t *>(date.constData()),8);
    marker.replace(16,8,stage+QByteArray::number(dateCrc>>48,16).rightJustified(4,'0').toUpper());marker.replace(24,8,date);
    marker.replace(32,16,QByteArray::number(data.size(),16).rightJustified(16,'0').toUpper());
    const auto *raw=reinterpret_cast<const uint8_t *>(data.constData());
    auto crc=RomMarker_Crc(0,raw,skip);crc=RomMarker_Crc(crc,raw+skip+skipBytes,data.size()-skip-skipBytes);
    put64(marker,48,crc);put64(marker,56,RomMarker_Crc(0,reinterpret_cast<const uint8_t *>(marker.constData()),56));
    data.replace(at,64,marker);
}
inline QByteArray boot(quint32 base=0x5000,const QByteArray &stage="AST1",int bytes=0xf000){
    QByteArray d(bytes,char(0xff));for(int i=0;i<4096;++i)d[i]=char(i*13);
    put32(d,0,0x20001000);for(int i=1;i<16;++i)put32(d,i*4,base+0x401);
    seal(d,ROM_MARKER_BOOT,stage,"20261010",bytes);return d;
}
inline QByteArray app(int bytes=0x3000,const QByteArray &stage="AST1"){
    QByteArray d(bytes,char(0xff));d.replace(0,8,"T008APP2");put32(d,8,quint32(bytes-4096));d.replace(46,9,"T008A0001");
    for(int i=4096;i<bytes;++i)d[i]=char(i*7);
    put32(d,4096,0x20001000);for(int i=1;i<16;++i)put32(d,4096+i*4,0x16401);
    seal(d,ROM_MARKER_APP,stage);return d;
}
inline QByteArray calib(const QByteArray &stage="AST1"){
    QByteArray d(4096,char(0x35));seal(d,ROM_MARKER_CAL,stage);return d;
}
inline QByteArray driver(const QByteArray &stage="AST1"){
    QByteArray d(0x900,char(0x32));for(int i=0;i<192;++i)d[i]=0;
    d.replace(64,8,"T008DRV1");d[72]=char(192);put32(d,80,d.size());put32(d,84,0x20001400);put32(d,88,d.size());put32(d,92,256);
    for(int i=0;i<5;++i)put32(d,96+i*4,192+i*2);
    put32(d,116,192);put32(d,120,4096);put32(d,124,1024);put32(d,128,0);put32(d,132,20000000);
    d.replace(136,16,QByteArray(16,'x'));put32(d,152,202);
    seal(d,ROM_MARKER_DRIVER,stage);return d;
}
}
