#pragma once
#include "ChannelTypes.h"
#include "../driverCan/tstPeakCan.h"
#include "../driverLin/tstPeakLin.h"

namespace communication {
class HardwareBackend {
public:
    virtual ~HardwareBackend() {}
    virtual Bus bus() const = 0;
    virtual HardwareChannels scan(QString &error) = 0;
    virtual bool open(const HardwareChannel &, const SoftwareChannelConfiguration &, QString &error) = 0;
    virtual bool close(QString &error) = 0;
    virtual Health health(QString &detail) = 0;
};
class PeakCanBackend : public HardwareBackend {
public:
    explicit PeakCanBackend(tstPeakCan &driver) : m_driver(driver) {}
    Bus bus() const override { return Bus::Can; }
    HardwareChannels scan(QString &error) override;
    bool open(const HardwareChannel &, const SoftwareChannelConfiguration &, QString &) override;
    bool close(QString &) override;
    Health health(QString &) override;
private:
    tstPeakCan &m_driver; // The driver must outlive its backend/session.
};
class PeakLinBackend : public HardwareBackend {
public:
    explicit PeakLinBackend(tstPeakLin &driver) : m_driver(driver) {}
    Bus bus() const override { return Bus::Lin; }
    HardwareChannels scan(QString &error) override;
    bool open(const HardwareChannel &, const SoftwareChannelConfiguration &, QString &) override;
    bool close(QString &) override;
    Health health(QString &) override;
private:
    tstPeakLin &m_driver;
};
}
