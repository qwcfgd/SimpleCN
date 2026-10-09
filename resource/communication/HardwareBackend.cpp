#include "HardwareBackend.h"
namespace communication {
HardwareChannels PeakCanBackend::scan(QString &error) {
    auto channels=m_driver.availableHardware();
    error=m_driver.lastErrorCode()==PCAN_ERROR_OK ? QString() : m_driver.lastErrorText();
    return channels;
}
bool PeakCanBackend::open(const HardwareChannel &h,const SoftwareChannelConfiguration &c,QString &error) {
    if (c.bitrate%1000 || c.bitrate/1000>65535) { error="Unsupported CAN bitrate"; return false; }
    const bool ok=m_driver.setHardwareHandle(h.handle) &&
        m_driver.setDevBaudrate(static_cast<word>(c.bitrate/1000)) && m_driver.startDevice();
    error=ok ? QString() : m_driver.lastErrorText(); return ok;
}
bool PeakCanBackend::close(QString &error) {
    const bool ok=m_driver.stopDevice(); error=ok ? QString() : m_driver.lastErrorText(); return ok;
}
Health PeakCanBackend::health(QString &detail) {
    const auto h=m_driver.hardwareHealth();
    detail=h==Health::Ready ? QString() : m_driver.lastErrorText(); return h;
}
HardwareChannels PeakLinBackend::scan(QString &error) {
    auto channels=m_driver.availableHardware();
    error=m_driver.lastErrorCode()==errOK ? QString() : m_driver.lastErrorText(); return channels;
}
bool PeakLinBackend::open(const HardwareChannel &h,const SoftwareChannelConfiguration &c,QString &error) {
    const bool ok=m_driver.setHardwareHandle(h.handle) &&
        m_driver.setDevMode(static_cast<byte>(c.linMode)) &&
        m_driver.setDevBaudrate(static_cast<word>(c.bitrate)) && m_driver.startDevice();
    error=ok ? QString() : m_driver.lastErrorText(); return ok;
}
bool PeakLinBackend::close(QString &error) {
    const bool ok=m_driver.stopDevice(); error=ok ? QString() : m_driver.lastErrorText(); return ok;
}
Health PeakLinBackend::health(QString &detail) {
    return m_driver.hardwareHealth(&detail);
}
}
