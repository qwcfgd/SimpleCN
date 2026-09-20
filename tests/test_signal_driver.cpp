#include <QtTest>
#include <cstring>
#include "driverLin/tstPeakLin.h"
class SignalLinApi final:public PLinApiClass {
public:
    SignalLinApi():PLinApiClass(false){}
    bool isLoaded()const override{return true;}
    TLINHardwareMode mode=modNone;BYTE state=schNotRunning;DWORD breakpoint=0,suspended=0;
    QVector<TLINFrameEntry> entries=QVector<TLINFrameEntry>(64);QVector<TLINScheduleSlot> schedule;
    QStringList calls;int writes=0,updates=0;bool failEnable=false;
    TLINError GetAvailableHardware(HLINHW*out,WORD,int*count)override{*count=1;if(out)*out=21;return errOK;}
    TLINError GetHardwareParam(HLINHW,TLINHardwareParam p,void*out,WORD size)override{
        std::memset(out,0,size);
        if(p==hwpName){std::strncpy(static_cast<char*>(out),"Signal fake",size-1);return errOK;}
        if(p==hwpScheduleState){*static_cast<BYTE*>(out)=state;return errOK;}
        if(p==hwpScheduleSuspendedSlot){*static_cast<DWORD*>(out)=suspended;return errOK;}
        if(p==hwpConnectedClients)return errOK;
        int value=p==hwpScheduleActive?(state==schNotRunning?-1:0):p==hwpSerialNumber?123:p==hwpDeviceNumber?1:0;
        std::memcpy(out,&value,qMin(size_t(size),sizeof(value)));return errOK;
    }
    TLINError RegisterClient(LPSTR,DWORD,HLINCLIENT*client)override{*client=42;return errOK;}
    TLINError ConnectClient(HLINCLIENT,HLINHW)override{return errOK;}
    TLINError InitializeHardware(HLINCLIENT,HLINHW,TLINHardwareMode m,WORD)override{mode=m;state=schNotRunning;calls<<"initialize";return errOK;}
    TLINError SetClientFilter(HLINCLIENT,HLINHW,uint64)override{return errOK;}
    TLINError SetFrameEntry(HLINCLIENT,HLINHW,TLINFrameEntry*e)override{
        calls<<QString("entry:%1:%2").arg(e->FrameId).arg(e->Flags);if(failEnable&&e->FrameId==17&&(e->Flags&FRAME_FLAG_RESPONSE_ENABLE)){failEnable=false;return errUnknown;}entries[e->FrameId]=*e;return errOK;
    }
    TLINError SuspendSchedule(HLINCLIENT,HLINHW)override{calls<<"suspend";if(state!=schRunning)return errIllegalSchedulerState;state=schSuspended;return errOK;}
    TLINError SetScheduleBreakPoint(HLINCLIENT,HLINHW,int number,DWORD handle)override{calls<<"breakpoint";if(number==0)breakpoint=handle;return errOK;}
    TLINError SuspendKeepAlive(HLINCLIENT,HLINHW)override{calls<<"keepalive-stop";return errOK;}
    TLINError DeleteSchedule(HLINCLIENT,HLINHW,int)override{calls<<"delete";schedule.clear();return errOK;}
    TLINError SetSchedule(HLINCLIENT,HLINHW,int,TLINScheduleSlot*data,int count)override{calls<<"schedule";schedule.clear();for(int i=0;i<count;++i){data[i].Handle=100+i;schedule.append(data[i]);}return errOK;}
    TLINError StartSchedule(HLINCLIENT,HLINHW,int)override{calls<<"start";state=schRunning;return errOK;}
    TLINError UpdateByteArray(HLINCLIENT,HLINHW,BYTE id,BYTE index,BYTE length,BYTE*data)override{calls<<"update";++updates;std::memcpy(entries[id].InitialData+index,data,length);return errOK;}
    TLINError Write(HLINCLIENT,HLINHW,TLINMsg*)override{++writes;return errOK;}
    TLINError ResetHardwareConfig(HLINCLIENT,HLINHW)override{state=schNotRunning;for(auto&e:entries)e={};return errOK;}
    TLINError DisconnectClient(HLINCLIENT,HLINHW)override{return errOK;}
    TLINError RemoveClient(HLINCLIENT)override{return errOK;}
    TLINError GetErrorText(TLINError,BYTE,LPSTR text,WORD)override{std::strcpy(text,"injected SDK error");return errOK;}
};
class SignalDriverTest:public QObject {
    Q_OBJECT
    void open(tstPeakLin&lin){QVERIFY(lin.setHardwareHandle(21));QVERIFY(lin.setDevMode(modMaster));QVERIFY(lin.setDevBaudrate(19200));QVERIFY(lin.startDevice());}
    TLINFrameEntry frame(int id){TLINFrameEntry f={};f.FrameId=BYTE(id);f.Length=2;f.Direction=dirPublisher;f.ChecksumType=cstClassic;f.Flags=FRAME_FLAG_RESPONSE_ENABLE;f.InitialData[0]=0x11;f.InitialData[1]=0x22;return f;}
private slots:
    void diagnosticSlotsUseScheduleAndClassicChecksum(){
        SignalLinApi api;tstPeakLin lin(nullptr,&api);open(lin);auto request=frame(60),response=frame(61);request.Length=response.Length=8;response.Direction=dirSubscriber;response.Flags=0;
        QVERIFY(lin.signalInstallFrames({request,response}));TLINScheduleSlot a={},b={};a.Type=b.Type=sltUnconditional;a.FrameId[0]=60;b.FrameId[0]=61;a.Delay=15;b.Delay=20;
        QVERIFY(lin.signalStartSchedule({a,b,b,a}));QCOMPARE(api.schedule.size(),4);QCOMPARE(api.schedule[1].FrameId[0],BYTE(61));QCOMPARE(api.schedule[2].Delay,WORD(20));
        QVERIFY(lin.signalUpdateFrame(60,QByteArray(8,char(0x55))));QCOMPARE(api.entries[60].InitialData[7],BYTE(0x55));QCOMPARE(api.writes,0);
        QVERIFY(!lin.signalUpdateFrame(61,QByteArray(7,0)));request.ChecksumType=cstEnhanced;QVERIFY(!lin.signalInstallFrames({request}));a.FrameId[0]=62;QVERIFY(!lin.signalStartSchedule({a}));
    }
    void responseInstallStopAndMonitor(){SignalLinApi api;tstPeakLin lin(nullptr,&api);open(lin);QVERIFY(lin.signalConfigureMode(false));QCOMPARE(api.mode,TLINHardwareMode(modSlave));QVERIFY(lin.signalInstallFrames({frame(17)}));
        for(int i=0;i<64;++i)QCOMPARE(bool(api.entries[i].Flags&FRAME_FLAG_RESPONSE_ENABLE),i==17);QCOMPARE(api.entries[17].ChecksumType,TLINChecksumType(cstClassic));
        QVERIFY(lin.signalUpdateFrame(17,QByteArray::fromHex("abcd")));QCOMPARE(api.updates,1);QCOMPARE(api.writes,0);QCOMPARE(api.entries[17].InitialData[0],BYTE(0xab));
        QVERIFY(lin.signalStop());for(const auto&e:api.entries)QVERIFY(!(e.Flags&FRAME_FLAG_RESPONSE_ENABLE));QVERIFY(lin.signalConfigureMode(false));QVERIFY(lin.signalInstallFrames({}));QCOMPARE(api.writes,0);
    }
    void failedInstallLeavesNoResponses(){SignalLinApi api;tstPeakLin lin(nullptr,&api);open(lin);QVERIFY(lin.signalConfigureMode(false));api.failEnable=true;QVERIFY(!lin.signalInstallFrames({frame(16),frame(17)}));for(const auto&e:api.entries)QVERIFY(!(e.Flags&FRAME_FLAG_RESPONSE_ENABLE));}
    void breakpointMustSuspendAtFirstSlot(){SignalLinApi api;tstPeakLin lin(nullptr,&api);open(lin);QVERIFY(lin.signalInstallFrames({frame(16)}));TLINScheduleSlot slot={};slot.Type=sltUnconditional;slot.Delay=10;slot.FrameId[0]=16;
        QVERIFY(lin.signalStartSchedule({slot,slot}));QVERIFY(lin.signalRequestRoundBoundary());QCOMPARE(api.breakpoint,DWORD(100));bool reached=true;QVERIFY(lin.signalRoundBoundaryReached(reached));QVERIFY(!reached);
        api.state=schSuspended;api.suspended=101;QVERIFY(!lin.signalRoundBoundaryReached(reached));api.suspended=100;QVERIFY(lin.signalRoundBoundaryReached(reached));QVERIFY(reached);QCOMPARE(api.breakpoint,DWORD(0));
        QVERIFY(lin.signalStop());QVERIFY(lin.signalStop());QVERIFY(lin.signalInstallFrames({frame(16)}));QVERIFY(lin.signalStartSchedule({slot}));QVERIFY(lin.signalConfigureMode(false));QCOMPARE(api.mode,TLINHardwareMode(modSlave));QVERIFY(lin.signalConfigureMode(true));QCOMPARE(api.mode,TLINHardwareMode(modMaster));
    }
    void frameBoundarySuspendsWithoutClearingResponse(){SignalLinApi api;tstPeakLin lin(nullptr,&api);open(lin);QVERIFY(lin.signalInstallFrames({frame(16)}));TLINScheduleSlot slot={};slot.Type=sltUnconditional;slot.Delay=10;slot.FrameId[0]=16;
        QVERIFY(lin.signalStartSchedule({slot,slot}));QVERIFY(lin.signalRequestRoundBoundary());QVERIFY(lin.signalRequestFrameBoundary());QVERIFY(api.entries[16].Flags&FRAME_FLAG_RESPONSE_ENABLE);
        QVERIFY(lin.signalRequestFrameBoundary());bool reached=false;api.suspended=101;QVERIFY(lin.signalFrameBoundaryReached(reached));QVERIFY(reached);QCOMPARE(api.breakpoint,DWORD(0));QVERIFY(api.entries[16].Flags&FRAME_FLAG_RESPONSE_ENABLE);
        QVERIFY(lin.signalStop());QVERIFY(!lin.signalRequestFrameBoundary());
    }
    void supportsFull256SlotPoolAndRejectsInvalid(){SignalLinApi api;tstPeakLin lin(nullptr,&api);open(lin);TLINScheduleSlot slot={};slot.Type=sltUnconditional;slot.Delay=10;slot.FrameId[0]=16;QVector<TLINScheduleSlot> all(256,slot);QVERIFY(lin.signalStartSchedule(all));QCOMPARE(api.schedule.size(),256);all.append(slot);QVERIFY(!lin.signalStartSchedule(all));}
};
QTEST_APPLESS_MAIN(SignalDriverTest)
#include "test_signal_driver.moc"
