#pragma once
#include "communication/HardwareBackend.h"
#include <memory>
namespace host {
// Raw wire types are confined to infrastructure; application ViewModels are vendor independent.
class CanHardware : public communication::HardwareBackend {
public:
    communication::Bus bus()const override{return communication::Bus::Can;}
    virtual bool sendRaw(TPCANMsg)=0;
    virtual DWORD recvRaw(DWORD,TPCANMsg*,TPCANTimestamp*)=0;
    virtual unsigned long lastErrorCode()const=0;
    virtual QString lastErrorText()const=0;
    virtual bool enableEcho()=0;
};
class LinHardware : public communication::HardwareBackend {
public:
    communication::Bus bus()const override{return communication::Bus::Lin;}
    virtual bool sendRaw(TLINMsg)=0;
    virtual DWORD recvRaw(DWORD,TLINRcvMsg*)=0;
    virtual unsigned long lastErrorCode()const=0;
    virtual QString lastErrorText()const=0;
    virtual bool clearMsg()=0;
    virtual int getDevMode()const=0;
    virtual bool signalConfigureMode(bool)=0;
    virtual bool signalConfigureMonitor(){return signalConfigureMode(false);}
    virtual bool signalStop()=0;
    virtual bool signalInstallFrames(const QVector<TLINFrameEntry>&)=0;
    virtual bool signalStartSchedule(QVector<TLINScheduleSlot>)=0;
    virtual bool signalRequestFrameBoundary()=0;
    virtual bool signalFrameBoundaryReached(bool&)=0;
    virtual bool signalRequestRoundBoundary()=0;
    virtual bool signalRoundBoundaryReached(bool&)=0;
    virtual bool signalUpdateFrame(BYTE,const QByteArray&)=0;
    communication::Health hardwareHealth(QString *detail){QString text;const auto result=health(text);if(detail)*detail=text;return result;}
};
std::unique_ptr<CanHardware> createCanHardware();
std::unique_ptr<LinHardware> createLinHardware();
// SoftwareChannel owns the session adapter; the worker owns its I/O endpoint.
std::unique_ptr<communication::HardwareBackend> sessionBackend(communication::HardwareBackend&);
}
