#pragma once
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>
namespace communication {
enum class Bus { Can, Lin };
enum class ConnectionState { Missing, Available, Connecting, Connected, Disconnecting, Fault };
enum class Health { Ready, BusWarning, Sleeping, Removed, Error };
struct HardwareChannel {
    Bus bus = Bus::Can;
    quint32 handle = 0;
    quint32 deviceId = 0;
    quint32 controller = 0;
    QString key;
    QString label;
    bool available = true;
    bool persistentIdentity = false; // False when the SDK supplies only a runtime identity.
    bool operator==(const HardwareChannel &b) const {
        return bus==b.bus && handle==b.handle && key==b.key && label==b.label && available==b.available && persistentIdentity==b.persistentIdentity;
    }
};
using HardwareChannels = QVector<HardwareChannel>;
struct UdsConfiguration {
    QString profileId = QStringLiteral("default");
    int p2Ms = 1000;
    int p2StarMs = 5500;
    int testerPresentMs = 2000;
    int maxPendingMs = 60000;
    quint8 programmingSession = 2;
    quint8 securityLevel = 0x11;
    bool isValid() const {
        return !profileId.isEmpty() && p2Ms>0 && p2StarMs>0 &&
               testerPresentMs>=0 && maxPendingMs>=p2StarMs;
    }
};
struct TransportConfiguration {
    quint32 requestId = 0x715;
    quint32 responseId = 0x795;
    quint32 functionalId = 0x7df;
    bool extendedId = false;
    quint8 nad = 1;
};
struct SoftwareChannelConfiguration {
    QString softwareId;             // Logical channel, never an SDK handle.
    Bus bus = Bus::Can;
    QString hardwareKey;
    quint32 preferredHandle = 0;     // Explicit selection from the current enumeration.
    int bitrate = 500000;            // Bits/s for both buses.
    int linMode = 2;
    bool autoReconnect = false;
    UdsConfiguration uds;            // Independent configuration per software channel.
    TransportConfiguration transport;
    bool isValid() const {
        return !softwareId.isEmpty() && uds.isValid() && bitrate>0 &&
               (bus!=Bus::Lin || (bitrate>=1000 && bitrate<=20000 && (linMode==1 || linMode==2)
                                 && transport.nad>=1 && transport.nad<=0x7e));
    }
};
}
Q_DECLARE_METATYPE(communication::HardwareChannels)
Q_DECLARE_METATYPE(communication::ConnectionState)
Q_DECLARE_METATYPE(communication::Health)
