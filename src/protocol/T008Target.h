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
    QByteArray project,hardware,configuration,calibration,version;
    quint16 minBoot=0;
    quint16 driverMajor=0,driverMinor=0;
    quint32 driverCapabilities=0;
    bool hasDriverAbi=false,hasAppVersion=false,hasMinimumBoot=false;
};
bool images(const FirmwareImage &,const FirmwareImage &,Plan &,QString &);
bool compareFileVersions(const Plan &,const QMap<QString,QString> &,QStringList &,QString &);
bool compatibility(const Plan &,QString &);
bool validateBlockLimit(int pduLimit,QString &error);
bool looksLikeImage(const FirmwareImage &);
bool looksLikeDriver(const FirmwareImage &);
bool response(const QByteArray &request,const QByteArray &positive,bool suppressed,QString &error);
QByteArray fingerprint(const QDate &,const QByteArray &tester,QString &error);
}
