#pragma once
#include <array>
#include <variant>
#include <QMetaType>
#include <QSharedPointer>
#include <QString>
#include <QUuid>
#include <QVector>

namespace host {
struct PlinReadFields {
    quint8 type, pid, length, direction, checksumType, checksum;
    quint32 errorFlags;
};
struct PcanReadFields {
    quint32 id;
    quint8 messageType, length;
    quint32 millis;
    quint16 millisOverflow, micros;
};
struct PlinWriteFields {
    quint64 submission;
    qint64 beginUs;
    quint32 result;
    quint8 pid, length, direction, checksumType, checksum;
};
struct LinCancelFields { QString origin; };
struct PeakRead {
    QUuid session;
    quint64 sequence=0, clockDomain=0, timestampUs=0;
    QString channel, hardwareKey;
    quint32 hardwareHandle=0, boundHandle=0, sdkReadStatus=0;
    qint64 hostCaptureUs=0;
    std::array<quint8,8> data{};
    std::variant<PlinReadFields,PcanReadFields,PlinWriteFields,LinCancelFields> fields;
};
using PeakReadPtr=QSharedPointer<const PeakRead>;
using PeakReadBatch=QVector<PeakReadPtr>;
}
Q_DECLARE_METATYPE(host::PeakReadBatch)
