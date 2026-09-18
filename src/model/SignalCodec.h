#pragma once
#include "domain/SignalTypes.h"
namespace host::signal {
class SignalCodec {
public:
    static quint64 mask(int width);
    static RawValue defaults(const SignalDefinition &);
    static QString rawText(const SignalDefinition &,const RawValue &);
    static QString physicalText(const SignalDefinition &,const RawValue &);
    static ValueResult parseRaw(const SignalDefinition &,const QString &);
    // rangeIndex is required only when a segmented inverse has multiple candidates.
    static ValueResult parsePhysical(const SignalDefinition &,const QString &,int rangeIndex=-1);
    static QString rangeWarning(const SignalDefinition &,const RawValue &);
    static QVector<int> bitPositions(const SignalDefinition &);
    static bool isActive(const FrameDefinition &,int field,const QVector<RawValue> &);
    static bool encode(const FrameDefinition &,const QVector<RawValue> &,QByteArray &,QString &);
    static bool decode(const FrameDefinition &,const QByteArray &,QVector<RawValue> &,QString &);
    static bool initialize(const FrameDefinition &,Bus,TxDraft &,QString &);
    static bool parseBytes(const QString &,int,QByteArray &,QString &);
    static QString validateFrame(const FrameDefinition &);
    static QString validateSchedule(const Schedule &,const QVector<FrameDefinition> &,int bitrate);
    static quint8 linPid(quint8 id);
};
}
