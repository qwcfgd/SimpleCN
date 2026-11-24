#pragma once
#include "communication/ChannelTypes.h"
#include <QMutex>
#include <QSet>
#include <functional>
#include <memory>
#include <cstddef>
namespace host::vectorxl {
using Status=qint16; using Handle=qint32; using Mask=quint64;
constexpr Status QueueEmpty=10;
// XL V3 public C ABI: Windows long is 32 bits on both x86 and x64.
// Layout reference: Vector XL Driver Library vxlapi.h, pack(1).
#pragma pack(push,1)
struct Channel {
    char name[32]; quint8 hwType,hwIndex,hwChannel;
    quint16 transceiverType,transceiverState,configError; quint8 channelIndex;
    Mask channelMask; quint32 channelCapabilities,channelBusCapabilities;
    quint8 isOnBus; quint32 connectedBusType; quint8 busParams[32];
    quint32 doNotUse,driverVersion,interfaceVersion,raw[10],serialNumber,articleNumber;
    char transceiverName[32]; quint32 specialCabFlags,dominantTimeout;
    quint8 dominantRecessiveDelay,recessiveDominantDelay,connectionInfo,timestamps;
    quint16 minVoltage,maxVoltage; quint32 maxBaudrate;
    quint8 fpgaCapabilities,deviceStatus; quint16 activeCapabilities,breakOffset,delimiterOffset;
    quint32 reserved[3];
};
struct Config {quint32 version,count,reserved[10];Channel channels[64];};
struct CanMessage {quint32 id;quint16 flags,dlc;quint64 reserved1;quint8 data[8];quint64 reserved2;};
struct LinMessage {quint8 id,dlc;quint16 flags;quint8 data[8],crc;};
struct Event {quint8 tag,channel;quint16 transaction,port;quint8 flags,reserved;quint64 ns;
    union {CanMessage can;LinMessage lin;quint8 raw[32];} data;};
struct LinParams {quint32 mode;qint32 bitrate;quint32 version,reserved;};
#pragma pack(pop)
static_assert(sizeof(Channel)==227 && offsetof(Channel,serialNumber)==147,"XL channel ABI");
static_assert(sizeof(Config)==14576 && sizeof(Event)==48 && offsetof(Event,data)==16,"XL V3 ABI");
struct Functions {
    std::function<Status()> openDriver,closeDriver;
    std::function<Status(Config*)> config;
    std::function<Status(Handle*,char*,Mask,Mask*,quint32,quint32,quint32)> openPort;
    std::function<Status(Handle)> closePort,flush;
    std::function<Status(Handle,Mask,quint32,quint32)> activate;
    std::function<Status(Handle,Mask)> deactivate;
    std::function<Status(Handle,quint32*,Event*)> receive;
    std::function<Status(Handle,Mask,quint32)> canBitrate;
    std::function<Status(Handle,Mask,int,int)> canMode;
    std::function<Status(Handle,Mask,quint32*,void*)> transmit;
    std::function<Status(Handle,Mask,LinParams)> linParams;
    std::function<Status(Handle,Mask,quint8*)> linDlc,linChecksum;
    std::function<Status(Handle,Mask,quint8,quint8*,quint8,quint16)> linSlave;
    std::function<Status(Handle,Mask,quint8,quint8)> linSwitch;
    std::function<Status(Handle,Mask,quint8,quint32)> linRequest;
    std::function<Status(Handle,Mask)> linWakeUp;
};
class Runtime {
public:
    Runtime(); explicit Runtime(Functions functions); ~Runtime();
    communication::HardwareChannels scan(communication::Bus,QString&);
    bool acquire(const communication::HardwareChannel&,Handle&,Mask&,quint8&,QString&);
    bool release(Handle&,Mask,QString&);
    bool result(Status,const QString&,QString&)const;
    bool available()const{return loaded;} QString libraryPath()const{return path;}
    QMutex mutex; Functions api;
private:
    communication::HardwareChannels enumerate(communication::Bus,Config&,QString&);
    bool initialize(); bool loaded=false; void *library=nullptr;QString error,path;QSet<Mask> leased;
};
std::shared_ptr<Runtime> sharedRuntime();
}
