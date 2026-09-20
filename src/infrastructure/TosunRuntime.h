#pragma once
#include <QtGlobal>
#include <QString>
#include <QVector>
#include <QHash>
#include <QMutex>
#include <QElapsedTimer>
#include <functional>
#include <memory>
#include "communication/ChannelTypes.h"
namespace host::tosun {
#pragma pack(push,1)
struct Can {quint8 channel=0,properties=0,length=0,reserved=0;quint32 id=0;quint64 us=0;quint8 data[8]={};};
struct CanFd {quint8 channel=0,properties=0,length=0,fdProperties=0;quint32 id=0;quint64 us=0;quint8 data[64]={};};
struct Lin {quint8 channel=0,error=0,properties=0,length=0,id=0,checksum=0,status=0;quint64 us=0;quint8 data[8]={};};
#pragma pack(pop)
static_assert(sizeof(Can)==24,"libTSCAN CAN ABI");
static_assert(sizeof(CanFd)==80,"libTSCAN CAN FD receive ABI");
static_assert(sizeof(Lin)==23,"libTSCAN LIN ABI");
struct Device {QString product,serial;int canChannels=0,linChannels=0;};
struct Functions {
    std::function<quint32(quint32*)> scan;
    std::function<quint32(qint32,char**,char**,char**)> info;
    std::function<quint32(const char*,quintptr*)> connect;
    std::function<quint32(quintptr)> disconnect;
    std::function<quint32(quintptr,int,double,quint32)> configureCan;
    std::function<quint32(quintptr,int,double,double,int,int,quint32)> configureCanController;
    std::function<quint32(quintptr,const Can*)> sendCan;
    std::function<quint32(quintptr,Can*,qint32*,quint8,quint8)> readCan;
    std::function<quint32(quintptr,CanFd*,qint32*,quint8,quint8)> readCanFd;
    std::function<quint32(quintptr,int,double,quint8)> configureLin;
    std::function<quint32(quintptr,int,quint8)> linRole;
    std::function<quint32(quintptr,const Lin*)> sendLin;
    std::function<quint32(quintptr,Lin*,qint32*,quint8,quint8)> readLin;
    std::function<quint32(quintptr,int)> clearLin;
    std::function<quint32(quintptr,int)> resetLin;
    std::function<quint32(quint32,char**)> describe;
};
// One SDK initialization and one native connection per physical adapter.
// All SDK calls are serialized; ports share that handle through explicit leases.
class Runtime {
public:
    Runtime();
    explicit Runtime(Functions functions);
    ~Runtime();
    communication::HardwareChannels scan(communication::Bus,QString&);
    bool acquire(const communication::HardwareChannel&,quintptr&,int&,QString&);
    bool release(const QString&,QString&);
    bool present(const QString&,QString&);
    bool result(quint32 code,const QString &operation,QString &error)const;
    QString libraryPath()const{return m_path;}
    bool available()const{return m_loaded;}
    QMutex mutex;
    Functions api;
private:
    bool refresh(QString&);
    struct Connection {quintptr handle=0;int users=0;};
    QHash<QString,Connection> m_connections;
    QVector<Device> m_devices;
    QHash<QString,quint32> m_handles;
    QElapsedTimer m_scanClock;
    QString m_error,m_path;
    void *m_library=nullptr;
    std::function<void()> m_finalize;
    bool m_loaded=false;
};
std::shared_ptr<Runtime> sharedRuntime();
}
