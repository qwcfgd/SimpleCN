#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QMetaType>
#include <QSet>
#include <QSharedPointer>
#include <QStringList>
#include <QVector>

namespace host::signal {
enum class Bus { Can, Lin };
enum class LinRole { Master, Slave, Monitor };
enum class RunState { Stopped, Starting, Running, SwitchPending, Switching, Stopping, Faulted };
enum class EventSource { RequestAccepted, HardwareEcho, Received, Simulated, NoResponse };
struct PhysicalRange {
    quint64 first=0,last=0;
    QString factor="1",offset="0",unit;
};
struct SignalDefinition {
    QString name,publisher,unit,comment;
    QStringList receivers;
    int start=0,width=1,line=0;
    bool littleEndian=true,isSigned=false,array=false,selector=false;
    bool multiplexed=false;quint64 muxValue=0;
    QString factor="1",offset="0",minimum,maximum;
    bool conversion=true;
    QVector<PhysicalRange> ranges;
    QMap<quint64,QString> labels;
    QString initial,initialSource,issue;
    QMap<QString,QString> attributes;
};
struct FrameDefinition {
    QString key,name,publisher,comment,issue;
    QStringList transmitters;
    quint32 id=0;int length=0,line=0,cycleMs=0;
    bool extended=false,custom=false,classicChecksum=false;
    QVector<SignalDefinition> fields;
    QMap<QString,QString> attributes;
};
struct ScheduleSlot { QString frame,delayMs="10",issue;int line=0; };
struct Schedule { QString name,issue;QVector<ScheduleSlot> entries; };
struct DatabaseDefinition {
    Bus bus=Bus::Can;
    QString path,sha256,version,encoding="UTF-8",master;
    int bitrate=0;
    QStringList nodes,diagnostics;
    QMap<QString,QMap<QString,QString>> nodeAttributes;
    QVector<FrameDefinition> frames;
    QVector<Schedule> schedules;
};
using Database=QSharedPointer<const DatabaseDefinition>;
struct ImportResult { Database database;QString error; };
struct RawValue {
    quint64 bits=0;
    QByteArray bytes;
    bool operator==(const RawValue &o)const{return bits==o.bits&&bytes==o.bytes;}
    bool operator!=(const RawValue &o)const{return !(*this==o);}
};
struct ValueResult {
    RawValue raw;
    QString actual,warning,error;
    bool ok()const{return error.isEmpty();}
};
struct AppliedPayload { QByteArray bytes;quint64 revision=0; };
struct LastRxState {
    QByteArray bytes;quint64 hardwareUs=0; qint64 arrivalUs=0;
    bool received=false,valid=false;QString detail;
};
struct TxDraft {
    QVector<RawValue> values;
    QVector<RawValue> appliedValues;
    QMap<int,QString> inputs,errors,warnings;
    QString frameInput,frameError;
    AppliedPayload applied;
    LastRxState rx;
    bool enabled=false;int cycleMs=0;
};
struct WorkingSet {
    QString canNode,canDirection="Tx";
    QVector<FrameDefinition> customFrames;
    QMap<QString,TxDraft> frames;
    QVector<Schedule> schedules;
    LinRole role=LinRole::Master;QString node,schedule;
};
struct ConfigurationSnapshot { Database database;WorkingSet working; };
struct ConfigurationResult { ConfigurationSnapshot snapshot;QString error;bool ok=false; };
struct TxItem {
    QString key;quint32 id=0;bool extended=false;
    QByteArray payload;int periodMs=0;quint64 revision=0;
    QString publisher;bool classicChecksum=false;
};
struct TxPlan {
    Bus bus=Bus::Can;LinRole role=LinRole::Master;
    QString node,schedule;bool periodic=false;
    quint64 run=0,connection=0;QString databaseRevision;
    QVector<TxItem> items;QVector<Schedule> schedules;
};
struct PayloadUpdate { QString key;AppliedPayload payload;quint64 run=0,connection=0; };
struct BusFrameEvent {
    Bus bus=Bus::Can;quint32 id=0;bool extended=false;
    QByteArray bytes;bool valid=true;
    EventSource source=EventSource::Received;
    quint64 hardwareUs=0; qint64 arrivalUs=0;
    QString detail;
};
using BusFrameEvents=QVector<BusFrameEvent>;
struct RunStatus {
    RunState state=RunState::Stopped;quint64 run=0;
    QString current,pending,detail;
    QMap<QString,quint64> sent,missed;QMap<QString,QString> failures;
};
inline bool active(RunState s){return s!=RunState::Stopped&&s!=RunState::Faulted;}
inline QString frameKey(Bus b,quint32 id,bool ext=false){return QString("%1:%2:%3").arg(b==Bus::Can?"CAN":"LIN",ext?"EXT":"STD").arg(id);}
}
Q_DECLARE_METATYPE(host::signal::TxPlan)
Q_DECLARE_METATYPE(host::signal::PayloadUpdate)
Q_DECLARE_METATYPE(host::signal::RunStatus)
Q_DECLARE_METATYPE(host::signal::BusFrameEvents)
