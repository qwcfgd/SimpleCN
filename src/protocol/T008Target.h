#pragma once
#include "FirmwareImage.h"
#include <QDate>
namespace boot::t008 {
inline constexpr const char *Target="t008-bench-v1";
inline constexpr int JobDeadlineMs=1800000;
struct Plan {
    QVector<Segment> application;
    Segment driver;
    QByteArray project,hardware,configuration,calibration,version;
    quint16 minBoot=0;
};
bool images(const FirmwareImage &,const FirmwareImage &,Plan &,QString &);
bool looksLikeImage(const FirmwareImage &);
bool response(const QByteArray &request,const QByteArray &positive,bool suppressed,QString &error);
QByteArray fingerprint(const QDate &,const QByteArray &tester,QString &error);
}
