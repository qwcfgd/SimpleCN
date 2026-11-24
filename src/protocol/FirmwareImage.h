#pragma once
#include <QByteArray>
#include "RomMarker.h"
#include <QString>
#include <QVector>
#include <QPair>
namespace boot {
struct Segment { quint32 address=0; QByteArray data; };
struct FirmwareImage {
    QVector<Segment> segments;
    QByteArray sha256;
    bool binary=false; // Only BIN may be split by file offsets; HEX addresses are absolute.
    static bool planSlots(const FirmwareImage &,quint32 base,quint32 alternateBase,bool hasAlternate,quint32 length,QVector<Segment> &primary,QVector<Segment> &alternate,QString &error);
    static bool loadTarget(const QString &,quint32 base,quint32 alternateBase,bool hasAlternate,quint32 length,FirmwareImage &,QString &error);
    qint64 size=0;
    static bool load(const QString &path,quint32 binaryBase,FirmwareImage &out,QString &error);
    static bool hasCurrentMarker(const FirmwareImage &);
    static bool validateMarkerSet(const QVector<QPair<Segment,quint32>> &,QString &error,bool appBinContainer=false);
    static bool validateMarker(const Segment &,quint32 kind,RomMarker &,QString &error);
    static bool validateCalibration(const FirmwareImage &,quint32 base,quint32 length,QString &error);
    static bool loadCalibration(const QString &,quint32 base,quint32 length,FirmwareImage &,QString &error);
    enum class Kind { Unknown, Driver, Application, BootBundle, IncompleteBoot, Calibration };
    static Kind identify(const FirmwareImage &image);
    static bool loadBootBundle(const QString &path,FirmwareImage &out,QString &error);
};
quint32 crc32(const QByteArray &bytes);
QByteArray be32(quint32 value);
quint32 readBe(const QByteArray &bytes);
}
