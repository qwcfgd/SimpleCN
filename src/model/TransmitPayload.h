#pragma once
#include "domain/SignalTypes.h"
namespace host::signal {
// Transmit bytes own the DLC. Database layouts remain immutable; only signals
// wholly covered by the current payload may be edited.
class TransmitPayload {
public:
    static bool validLength(Bus,quint32 id,int length);
    static bool containsSignal(const FrameDefinition&,int field,int length);
    static bool parse(const QString&,int length,QByteArray&,QString&error);
    static bool decode(const FrameDefinition&,const QByteArray&,QVector<RawValue>&,QString&error);
    static bool encode(const FrameDefinition&,const QVector<RawValue>&,QByteArray&,QString&error);
};
}
