#pragma once
#include "FirmwareImage.h"
#include <QDate>
#include <QMap>
#include <QStringList>
namespace boot::t008 {
inline constexpr const char *Target="t008-bench-v1";
inline constexpr int JobDeadlineMs=1800000;
struct Plan {
    QVector<Segment> application;
    Segment driver;
    QByteArray project,hardware,version;
    quint32 driverCapabilities=0;
    bool hasAppVersion=false;
};
bool images(const FirmwareImage &,const FirmwareImage &,Plan &,QString &);
// Strict current S32K144 App format, with explicit compiler-produced 4 KiB padding.
bool validateCurrentDriver(const FirmwareImage &,bool t010,QString &error,quint16 t010Minor=9);
bool completeApplication(const FirmwareImage &,quint32 base,quint32 eraseCapacity,bool t010,int slot,Plan &,QString &,bool slotContainer=false);
bool compareFileVersions(const Plan &,const QMap<QString,QString> &,QStringList &,QString &);
bool compatibility(const Plan &,QString &);
bool validateBlockLimit(int pduLimit,QString &error);
bool looksLikeImage(const FirmwareImage &);
bool looksLikeDriver(const FirmwareImage &);
bool response(const QByteArray &request,const QByteArray &positive,bool suppressed,QString &error);
QByteArray fingerprint(const QDate &,const QByteArray &tester,QString &error);
}
