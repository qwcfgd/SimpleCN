#pragma once
#include "communication/ChannelTypes.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonObject>
#include <limits>
#include "domain/ChannelDefaults.h"
#include "domain/SignalTypes.h"
namespace host {
struct ChannelSettings : ChannelPageInitialValues {
    QJsonObject signalConfiguration;
    QString cddPath,cddEcu,cddVariant;
    int udsRepeatCount=1,udsRepeatDelayMs=1000;
    bool udsRepeatEnabled=false;
    int p3Ms=0,p2ServerMs=0,p2StarServerMs=0,linSlotMs=0,linAsMs=1000,linCrMs=1000;
    QString testerPresentRequest="3E80";
    static ChannelSettings defaults(communication::Bus bus);
    bool toConfiguration(communication::SoftwareChannelConfiguration &out,QString &error) const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &,ChannelSettings &,QString &error);
};
struct FrameRecord {
    QString timestamp,channel,direction,identifier,data,status;
    int length=0;bool error=false,warning=false;
    QString relativeTime;
    // Display time has an explicit unit; raw capture values never reach the view.
    double relativeSeconds=std::numeric_limits<double>::quiet_NaN();
    QString intervalSeconds;
    // Numeric capture data is retained independently of display formatting.
    signal::Bus bus=signal::Bus::Can;
    quint32 id=0;
    bool extended=false,fd=false,rtr=false,brs=false,esi=false;
    QByteArray payload;
    qint64 captureUs=-1,timeUs=0,epochMs=0;
    quint64 hardwareUs=0;
    bool typed=false,echo=false,request=false,simulated=false,noResponse=false,monitorHidden=false,replay=false;
    int checksum=-1;bool classicChecksum=false;
    quint32 errorFlags=0;
};
using FrameBatch=QVector<FrameRecord>;
enum class TaskState {Idle,Running,Completed,Cancelled,Failed};
QString hardwareDeviceKey(const communication::HardwareChannel &);
QString connectionText(communication::ConnectionState);
QString imageDescription(const QString &path);
bool imageReady(const QString &path);
}
Q_DECLARE_METATYPE(host::ChannelSettings)
Q_DECLARE_METATYPE(host::FrameBatch)
Q_DECLARE_METATYPE(host::TaskState)
