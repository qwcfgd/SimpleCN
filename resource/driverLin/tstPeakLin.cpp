#include "tstPeakLin.h"
#include <algorithm>
#include <cstring>

tstPeakLin::tstPeakLin(void *parent, PLinApiClass *api)
{
    t_SimTxLIN = false;
    t_SimRxLIN = false;

    //device parameter configure:
    Q_UNUSED(parent);
    t_ownsApi = api == nullptr;
    t_pPLinApi = api ? api : new PLinApiClass(); // the masked API pointer.

    t_LastLinErr = 0x0000; // latest function response error code

    static char clientName[] = "PEAK-Controller";
    t_hClientName = clientName; // name of new client
    t_ClientMarkWindow = 0; // requires client open a new mark window
    t_hClient = 0; // client index

    t_hHw = 0;
    t_HwMode = modNone;
    t_baudrate = 0;

    t_lMask = 0xFFFFFFFFFFFFFFFF;
    t_sFrameEntry = {};

    memset(t_FrameEntry,0,sizeof(t_FrameEntry));
    for (uint i = 0; i < sizeof(t_FrameEntry)/sizeof(*t_FrameEntry); ++i)
        t_FrameEntry[i].FrameId = i;
    memset(t_FrameEntryRd,0,sizeof(t_FrameEntryRd));

    t_Schdlen = 0;
    memset(t_SchdSlotWt,0,sizeof(t_SchdSlotWt));
    memset(t_SchdSlotRd,0,sizeof(t_SchdSlotRd));
    memset(t_sSlotWt,0,sizeof(t_sSlotWt));

    t_MsgTx = {};
    t_BufTx = {};

    memset(t_MsgTxArrSize,0,sizeof(t_MsgTxArrSize));
    memset(t_MsgTxArr,0,sizeof(t_MsgTxArr));
    memset(t_BufTxArr,0,sizeof(t_BufTxArr));
    for(uint i = 0; i < sizeof(t_BufTxArr)/sizeof(*t_BufTxArr); ++i)
    {
        t_BufTxArr[i].pslotlen = &t_sSlotWt[i].slotlen;
        for(uint j = 0; j < LIN_MAX_SLOT_NUM; ++j)
            t_BufTxArr[i].pid[j] = &t_sSlotWt[i].id[j];
    }

    t_MsgRxSize = 0;
    memset(t_MsgRx,0,sizeof(t_MsgRx));
    t_BufRx = {};

    t_MsgRxArrSize = 0;
    memset(t_MsgRxArr,0,sizeof(t_MsgRxArr));
    t_BufRxArr = {};

}

tstPeakLin::~tstPeakLin()
{
    stopDevice();
    if (t_ownsApi) delete t_pPLinApi;
}

bool tstPeakLin::startDevice()
{
    if (t_open) return true;
    if (t_SimTxLIN) { t_open = true; return true; }
    if (!t_pPLinApi->isLoaded()) { t_LastLinErr = errManagerNotLoaded; return false; }
    if (!t_hHw || (t_HwMode!=modMaster && t_HwMode!=modSlave) || t_baudrate<1000 || t_baudrate>20000) {
        t_LastLinErr = errWrongParameterValue; return false;
    }
    const auto channels = availableHardware();
    if (t_LastLinErr != errOK) return false;
    bool found = false;
    for (const auto &channel : channels) if (channel.handle==t_hHw) found = true;
    if (!found) { t_LastLinErr = errIllegalHardware; return false; }
    // Refuse reconfiguration of a channel used by another client.
    BYTE clients[255] = {};
    t_LastLinErr = t_pPLinApi->GetHardwareParam(t_hHw,hwpConnectedClients,clients,sizeof(clients));
    if (t_LastLinErr != errOK) return false;
    for (BYTE client : clients) if (client) { t_LastLinErr = errIllegalHardwareState; return false; }
    t_LastLinErr = t_pPLinApi->RegisterClient(t_hClientName,0,&t_hClient);
    if (t_LastLinErr != errOK || !t_hClient) {
        const auto error = t_LastLinErr == errOK ? errIllegalClient : t_LastLinErr;
        if (t_hClient) stopDevice();
        t_LastLinErr = error; return false;
    }
    t_LastLinErr = t_pPLinApi->ConnectClient(t_hClient,t_hHw);
    if (t_LastLinErr == errOK) {
        t_acquired = true;
        // Always initialize a fresh connection, including a replug with the same settings.
        t_LastLinErr = t_pPLinApi->InitializeHardware(t_hClient,t_hHw,t_HwMode,t_baudrate);
        if (t_LastLinErr == errOK && initFrameEntry()) {
            t_lMask = 0xffffffffffffffffULL;
            t_LastLinErr = t_pPLinApi->SetClientFilter(t_hClient,t_hHw,t_lMask);
            if (t_LastLinErr == errOK) { t_open = true; return true; }
        }
    }
    const auto error = t_LastLinErr;
    stopDevice(); t_LastLinErr = error;
    return false;
}

bool tstPeakLin::stopDevice()
{
    t_open = false;
    if (!t_hClient) { t_acquired = false; t_LastLinErr = errOK; return true; }
    TLINError firstError = errOK;
    const auto remember = [&firstError](TLINError error) { if (firstError==errOK && error!=errOK) firstError=error; };
    if (t_acquired) {
        BYTE clients[255] = {};
        const auto query = t_pPLinApi->GetHardwareParam(t_hHw,hwpConnectedClients,clients,sizeof(clients));
        bool other = false;
        for (BYTE client : clients) if (client && client!=t_hClient) other = true;
        // Never reset shared configuration when ownership cannot be established.
        if (query==errOK && !other) {
            BYTE scheduleState = schNotRunning;
            if (t_pPLinApi->GetHardwareParam(t_hHw,hwpScheduleState,&scheduleState,sizeof(scheduleState))==errOK && scheduleState==schRunning)
                remember(t_pPLinApi->SuspendSchedule(t_hClient,t_hHw));
            remember(t_pPLinApi->ResetHardwareConfig(t_hClient,t_hHw));
        }
        remember(t_pPLinApi->DisconnectClient(t_hClient,t_hHw));
    }
    remember(t_pPLinApi->RemoveClient(t_hClient));
    t_hClient = 0;
    t_acquired = false;
    t_MsgRxSize = t_MsgRxArrSize = 0;
    t_Schdlen = 0;
    t_LastLinErr = firstError;
    return firstError==errOK;
}

bool tstPeakLin::setDevChn(const byte sdevchn)
{
    return setHardwareHandle(sdevchn);
}

bool tstPeakLin::setDevMode(const byte sdevMode)
{
    if (t_open || (sdevMode!=modMaster && sdevMode!=modSlave)) { t_LastLinErr=errWrongParameterValue; return false; }
    t_HwMode=sdevMode; return true;
}

bool tstPeakLin::setDevBaudrate(const word sBaudrate)
{
    if (t_open || sBaudrate<1000 || sBaudrate>20000) { t_LastLinErr=errWrongParameterValue; return false; }
    t_baudrate=sBaudrate; return true;
}

bool tstPeakLin::setFrameEntry(const StrtFrameEntry& frameentry)
{
    if (!t_open || frameentry.framelen>LIN_MAX_FRAME_NUM) return false;
    for (uint i=0; i<frameentry.framelen; ++i)
        if (frameentry.id[i]>=LIN_MAX_FRAME_NUM || frameentry.datalen[i]>8) return false;
    bool rlt = false;

    t_sFrameEntry.linstandard = frameentry.linstandard;
    t_sFrameEntry.framelen = frameentry.framelen;

    for (uint i = 0; i < frameentry.framelen; ++i)
    {
        t_sFrameEntry.id[i] = frameentry.id[i];

        if (frameentry.id[i] == 0x3c)
        {
            t_sFrameEntry.datalen[i] = 8;
            t_sFrameEntry.direct[i] = t_HwMode == modMaster? MST_TX:MST_RX;
            t_sFrameEntry.chkrtype[i] = cstClassic;
            t_sFrameEntry.flag[i] = t_HwMode == modMaster? DATA_MST:DATA_SLV;
        }
        else if(frameentry.id[i] == 0x3d)
        {
            t_sFrameEntry.datalen[i] = 8;
            t_sFrameEntry.direct[i] = MST_RX;
            t_sFrameEntry.chkrtype[i] = cstClassic;
            t_sFrameEntry.flag[i] = DATA_EMPTY;
        }
        else
        {
            t_sFrameEntry.datalen[i] = frameentry.datalen[i];
            t_sFrameEntry.direct[i] = frameentry.direct[i];
            t_sFrameEntry.chkrtype[i] = cstEnhanced;
            t_sFrameEntry.flag[i] = frameentry.flag[i];
        }

        if(t_sFrameEntry.linstandard < 2.0f)
            t_sFrameEntry.chkrtype[i] = cstClassic;

        for (uint j = 0; j < frameentry.datalen[i]; ++j)
            t_sFrameEntry.initdata[i][j] = frameentry.initdata[i][j];
    }

    if (setFrameEntry() == true)
        rlt = setClientFilter();

    return rlt;
}


bool tstPeakLin::setAllSchedule(const byte schdlen, const StrtSchdSlot SchdSlot[])
{
    if (SchdSlot == NULL)
        return false;

    if (schdlen > LIN_MAX_SCHEDULE_NUM)
        return false;

    for(uint i = 0; i < schdlen; i ++)
    {
        bool rlt = setSchedule(i, SchdSlot[i]);
        if (rlt != true)
            return false;
    }

    t_Schdlen = schdlen;

    return true;
}

bool tstPeakLin::setSchedule(const byte schdidx, const StrtSchdSlot &SchdSlot)
{
    if (!t_open) return false;
    for (uint i=0; i<std::min<DWORD>(SchdSlot.slotlen,LIN_MAX_SLOT_NUM); ++i)
        if (SchdSlot.id[i]>=LIN_MAX_FRAME_NUM || !SchdSlot.delay[i]) return false;
    if (schdidx >= LIN_MAX_SCHEDULE_NUM)
        return false;
    if (SchdSlot.slotlen > LIN_MAX_SLOT_NUM)
        return false;

    t_sSlotWt[schdidx].slotlen = SchdSlot.slotlen;

    for(uint i = 0; i < SchdSlot.slotlen; i ++)
    {
        t_SchdSlotWt[schdidx][i].FrameId[0] = t_sSlotWt[schdidx].id[i] = SchdSlot.id[i];
        t_SchdSlotWt[schdidx][i].Delay = t_sSlotWt[schdidx].delay[i] = SchdSlot.delay[i];

        if (SchdSlot.id[i] == 0x3c)
            t_SchdSlotWt[schdidx][i].Type = sltMasterRequest;
        else if (SchdSlot.id[i] == 0x3d)
            t_SchdSlotWt[schdidx][i].Type = sltSlaveResponse;
        else
            t_SchdSlotWt[schdidx][i].Type = sltUnconditional;
    }

    t_LastLinErr = t_pPLinApi->SetSchedule(t_hClient,t_hHw,schdidx,t_SchdSlotWt[schdidx],SchdSlot.slotlen);

    if (t_LastLinErr != errOK)
        return false;
    else
        return true;
}

bool tstPeakLin::startSchedule(const byte schdidx)
{
    if (!t_open || schdidx>=t_Schdlen) return false;
    t_LastLinErr = t_pPLinApi->StartSchedule(t_hClient,t_hHw,schdidx);

    if (t_LastLinErr != errOK)
    {
        return false;
    }
    else
        return true;
}

bool tstPeakLin::suspendSchedule()
{
    if (!t_open) return true;
    t_LastLinErr = t_pPLinApi->SuspendSchedule(t_hClient,t_hHw);

    if (t_LastLinErr != errOK)
        return false;
    else
        return true;
}

bool tstPeakLin::resumeSchedule()
{
    if (!t_open) return false;
    t_LastLinErr = t_pPLinApi->ResumeSchedule(t_hClient,t_hHw);

    if (t_LastLinErr != errOK)
        return false;
    else
        return true;
}

bool tstPeakLin::sendMsg(const StrtLinTxBuf& buf)
{
    if (!t_open) { t_LastLinErr=errIllegalHardwareState; return false; }
    if (t_SimTxLIN) return true;
    if (!setBufTx(buf) || !setMsgTx(t_BufTx)) return false;
    t_LastLinErr=t_pPLinApi->Write(t_hClient,t_hHw,&t_MsgTx);
    return t_LastLinErr==errOK;
}

bool tstPeakLin::setAllSchdData(const byte schdlen, const StrtLinSchdTxBuf buf[])
{
    bool rlt = false;

    if (buf == NULL)
        return false;

    if (t_SimTxLIN == false)
    {
        if (initAllSchdBufTxArr(schdlen))
            if (setAllSchdBufTxArr(schdlen,buf))
                rlt = setAllSchdMsgTxArr(schdlen,t_BufTxArr);
    }
    else
    {
        rlt = true;
    }

    return rlt;
}

bool tstPeakLin::setSchdData(const byte schdidx, const StrtLinSchdTxBuf& buf)
{
    bool rlt = false;

    if (t_SimTxLIN == false)
    {
        if (initSchdBufTxArr(schdidx))
            if (setSchdBufTxArr(schdidx,buf))
                rlt = setSchdMsgTxArr(schdidx,t_BufTxArr[schdidx]);
    }
    else
    {
        rlt = true;
    }
    return rlt;
};

bool tstPeakLin::setSlotData(const byte schdidx, const byte slotidx, const StrtLinSchdTxBuf& buf)
{
    bool rlt = false;

    if (t_SimTxLIN == false)
    {
        if (initSlotBufTxArr(schdidx,slotidx))
            if (setSlotBufTxArr(schdidx,slotidx,buf))
                rlt = setSlotMsgTxArr(schdidx,slotidx,t_BufTxArr[schdidx]);
    }
    else
    {
        rlt = true;
    }
    return rlt;
};

bool tstPeakLin::clearMsg()
{
    if (!t_open || t_SimTxLIN) return true;
    t_LastLinErr=t_pPLinApi->ResetClient(t_hClient);
    return t_LastLinErr==errOK;
}

DWORD tstPeakLin::recvMsg(const DWORD oncerecvnum, StrtLinSchdRxBuf& buf)
{
    buf = {};
    const DWORD count=recvRaw(std::min<DWORD>(oncerecvnum,LIN_MAX_SLOT_NUM),t_MsgRxArr);
    DWORD copied=0;
    for (DWORD i=0; i<count; ++i) {
        const auto &msg=t_MsgRxArr[i];
        if (msg.Type!=mstStandard || msg.Length>8) continue;
        buf.id[copied]=msg.FrameId & 0x3f;
        buf.datalen[copied]=msg.Length;
        buf.direct[copied]=msg.Direction;
        buf.chkrtype[copied]=msg.ChecksumType;
        buf.errflag[copied]=msg.ErrorFlags;
        std::memcpy(buf.data[copied],msg.Data,msg.Length);
        ++copied;
    }
    buf.slotlen=t_MsgRxSize=copied;
    return copied;
}

int tstPeakLin::getDevChn()
{
    return t_hHw;
};

int tstPeakLin::getDevMode()
{
    return t_HwMode;
};

int tstPeakLin::getDevBaudrate()
{
    return t_baudrate;
};

bool tstPeakLin::isDeviceActive()
{
    const auto health=hardwareHealth();
    return health==communication::Health::Ready || health==communication::Health::Sleeping ||
           health==communication::Health::BusWarning;
}

StrtLinSchdTxBuf* tstPeakLin::getAllSchdData(byte &schdlen)
{
    schdlen = t_Schdlen;
    initAllSchdBufTxArr(t_Schdlen);
    return t_BufTxArr;
};

StrtLinSchdTxBuf& tstPeakLin::getSchdData(const byte schdidx)
{
    initSchdBufTxArr(schdidx);
    return t_BufTxArr[schdidx];
};

TLINFrameEntry* tstPeakLin::getFrameEntry(const byte frameidx)
{
    // Before a frame entry can be read from the hardware, the frame id
    // of the wanted entry must be set
    t_FrameEntryRd[frameidx].FrameId = frameidx;
    // Read the information of the specified frame entry from the hardware.
    t_LastLinErr = t_pPLinApi->GetFrameEntry(t_hHw, &t_FrameEntryRd[frameidx]);

    if (t_LastLinErr != errOK)
        return NULL;

    return &t_FrameEntryRd[frameidx];
}

DWORD tstPeakLin::getMsgRxSize()
{
    return t_MsgRxSize;
};

DWORD tstPeakLin::getMsgRxArrSize()
{
    return t_MsgRxArrSize;
};

bool tstPeakLin::setBufTx(const StrtLinTxBuf& buf)
{
    bool rlt = false;

    for(uint i = 0; i < t_sFrameEntry.framelen; ++i)
    {
        if (buf.id == t_sFrameEntry.id[i])
        {
            rlt = true;

            t_BufTx.id = buf.id;
            t_BufTx.pdirect = &t_sFrameEntry.direct[i];
            t_BufTx.pdatalen = &t_sFrameEntry.datalen[i];
            t_BufTx.pchkrtype = &t_sFrameEntry.chkrtype[i];
            for (uint j = 0; j < *t_BufTx.pdatalen; ++j)
                t_BufTx.data[j] = buf.data[j];
            break;
        }
    }
    return rlt;
};


bool tstPeakLin::setMsgTx(const StrtLinTxBuf& buf)
{

    byte protectid = buf.id;

    t_LastLinErr = t_pPLinApi->GetPID(&protectid);
    if (t_LastLinErr != errOK) return false;
    t_MsgTx.FrameId = protectid;
    t_MsgTx.Length = *buf.pdatalen;
    t_MsgTx.Direction = *buf.pdirect;
    t_MsgTx.ChecksumType = *buf.pchkrtype;
    for (uint i = 0; i < *buf.pdatalen; ++i )
        t_MsgTx.Data[i] = buf.data[i];

    t_LastLinErr = t_pPLinApi->CalculateChecksum(&t_MsgTx);

    if (t_LastLinErr != errOK)
        return false;
    else
        return true;
};

bool tstPeakLin::getBufRx(const DWORD recvnum, StrtLinSchdRxBuf& buf)
{

    for (uint i = 0; i < recvnum; ++i )
    {
        buf.id[i] = t_BufRx.id[i];
        buf.datalen[i] = t_BufRx.datalen[i];
        buf.direct[i] = t_BufRx.direct[i];
        buf.chkrtype[i] = t_BufRx.chkrtype[i];
        for (uint j = 0; j < t_BufRx.datalen[i]; ++j )
            buf.data[i][j] = t_BufRx.data[i][j];

    }
    return true;
};


bool tstPeakLin::getMsgRx(const DWORD recvnum, StrtLinSchdRxBuf &buf)
{

    for (uint i = 0; i < recvnum; ++i )
    {
        buf.id[i] = t_MsgRx[i].FrameId & 0x3f;
        buf.datalen[i] = t_MsgRx[i].Length;
        buf.direct[i] = t_MsgRx[i].Direction;
        buf.chkrtype[i] = t_MsgRx[i].ChecksumType;
        buf.errflag[i] = t_MsgRx[i].ErrorFlags;
        for (uint j = 0; j < t_MsgRx[i].Length; ++j )
            buf.data[i][j] = t_MsgRx[i].Data[j];

    }
    buf.slotlen = recvnum;

    return true;
};

bool tstPeakLin::initAllSchdBufTxArr(const byte schdlen)
{

    if (schdlen > LIN_MAX_SCHEDULE_NUM)
        return false;

    for(uint i = 0; i < schdlen; ++i)
    {
        bool rlt = initSchdBufTxArr(i);
        if (rlt != true)
            return false;
    }
    return true;
};

bool tstPeakLin::setAllSchdBufTxArr(const byte schdlen, const StrtLinSchdTxBuf buf[])
{

    if (schdlen > LIN_MAX_SCHEDULE_NUM)
        return false;

    for(uint i = 0; i < schdlen; ++i)
    {
        bool rlt = setSchdBufTxArr(i,buf[i]);
        if (rlt != true)
            return false;
    }
    return true;
};

bool tstPeakLin::setAllSchdMsgTxArr(const byte schdlen, const StrtLinSchdTxBuf buf[])
{
    if (buf == NULL)
        return false;

    if (schdlen > LIN_MAX_SCHEDULE_NUM)
        return false;

    for(uint i = 0; i < schdlen; ++i)
    {
        bool rlt = setSchdMsgTxArr(i, buf[i]);
        if (rlt != true)
            return false;
    }
    return true;
};

bool tstPeakLin::initSchdBufTxArr(const byte schdidx)
{

    if (schdidx >= LIN_MAX_SCHEDULE_NUM)
        return false;

    for(uint i = 0; i < *t_BufTxArr[schdidx].pslotlen; ++i)
    {
        bool rlt = initSlotBufTxArr(schdidx,i);
        if (rlt != true)
            return false;
    }
    return true;
};

bool tstPeakLin::setSchdBufTxArr(const byte schdidx,const StrtLinSchdTxBuf& buf)
{

    if (schdidx >= LIN_MAX_SCHEDULE_NUM)
        return false;

    for(uint i = 0; i < *t_BufTxArr[schdidx].pslotlen; ++i)
    {
        bool rlt = setSlotBufTxArr(schdidx,i,buf);
        if (rlt != true)
            return false;
    }
    return true;
};

bool tstPeakLin::setSchdMsgTxArr(const byte schdidx, const StrtLinSchdTxBuf& buf)
{

    if (schdidx >= LIN_MAX_SCHEDULE_NUM)
        return false;

    if (!buf.pslotlen || *buf.pslotlen>LIN_MAX_SLOT_NUM) return false;
    t_MsgTxArrSize[schdidx] = 0;
    for(uint i = 0; i < *buf.pslotlen; ++i)
    {
        bool rlt = setSlotMsgTxArr(schdidx,i,buf);

        if (rlt != true)
            return false;
        else
            t_MsgTxArrSize[schdidx] = t_MsgTxArrSize[schdidx] + 1;
    }
    return true;
};

bool tstPeakLin::initSlotBufTxArr(const byte schdidx, const byte slotidx)
{

    if (schdidx >= LIN_MAX_SCHEDULE_NUM)
        return false;
    if (slotidx >= LIN_MAX_SLOT_NUM)
        return false;

    bool rlt = false;

    // update t_BufTxArr if the id can be found into t_sFrameEntry.
    for(uint i = 0; i < t_sFrameEntry.framelen; ++i)
    {
        if (*t_BufTxArr[schdidx].pid[slotidx] == t_sFrameEntry.id[i])
        {
            rlt = true;
            // t_BufTxArr pslotlen and pid had been pointed to t_sSlotWt when Constructor initializing.

            // update t_BufTxArr pdirect, pdatalen and pchkrtype from t_sFrameEntry.
            t_BufTxArr[schdidx].pdirect[slotidx] = &t_sFrameEntry.direct[i];
            t_BufTxArr[schdidx].pdatalen[slotidx] = &t_sFrameEntry.datalen[i];
            t_BufTxArr[schdidx].pchkrtype[slotidx] = &t_sFrameEntry.chkrtype[i];
            break;
        }
    }
    return rlt;
};

bool tstPeakLin::setSlotBufTxArr(const byte schdidx, const byte slotidx, const StrtLinSchdTxBuf& buf)
{

    if (schdidx >= LIN_MAX_SCHEDULE_NUM)
        return false;
    if (slotidx >= LIN_MAX_SLOT_NUM)
        return false;

    bool rlt = false;

    // update t_BufTxArr if the id can be found into t_sFrameEntry.
    for(uint i = 0; i < t_sFrameEntry.framelen; ++i)
    {
        if (*t_BufTxArr[schdidx].pid[slotidx] == t_sFrameEntry.id[i])
        {
            rlt = true;
            for (uint j = 0; j < t_sFrameEntry.datalen[i]; ++j)
                t_BufTxArr[schdidx].data[slotidx][j] = buf.data[slotidx][j];
            break;
        }
    }
    return rlt;
};

bool tstPeakLin::setSlotMsgTxArr(const byte schdidx, const byte slotidx, const StrtLinSchdTxBuf& buf)
{

    if (schdidx >= LIN_MAX_SCHEDULE_NUM)
        return false;
    if (slotidx >= LIN_MAX_SLOT_NUM)
        return false;

    if (!buf.pid[slotidx] || !buf.pdatalen[slotidx] || !buf.pdirect[slotidx] ||
        !buf.pchkrtype[slotidx] || *buf.pdatalen[slotidx]>8) return false;
    byte pid = *buf.pid[slotidx];

    t_LastLinErr = t_pPLinApi->GetPID(&pid);
    t_MsgTxArr[schdidx][slotidx].FrameId = pid;
    t_MsgTxArr[schdidx][slotidx].Length = *buf.pdatalen[slotidx];
    t_MsgTxArr[schdidx][slotidx].Direction = *buf.pdirect[slotidx];
    t_MsgTxArr[schdidx][slotidx].ChecksumType = *buf.pchkrtype[slotidx];

    for (uint i = 0; i < *buf.pdatalen[slotidx]; ++i )
    {
        t_MsgTxArr[schdidx][slotidx].Data[i] = buf.data[slotidx][i];
    }

    if (t_LastLinErr != errOK) return false;
    t_LastLinErr = t_pPLinApi->CalculateChecksum(&t_MsgTxArr[schdidx][slotidx]);
    if (t_LastLinErr != errOK) return false;

    //update slot data by newest updated t_MsgTxArr.
    if (!updateSlotData(*buf.pid[slotidx],*buf.pdatalen[slotidx],buf.data[slotidx])) return false;

    if (t_LastLinErr != errOK)
        return false;
    else
        return true;
}

bool tstPeakLin::setClientFilter()
{
    uint64 mask = 0x0000000000000000;

    for (uint i = 0; i < t_sFrameEntry.framelen; ++i)
    {
        mask |= (1ull << t_sFrameEntry.id[i]);
    }

    t_lMask = mask;
    t_LastLinErr = t_pPLinApi->SetClientFilter(t_hClient,t_hHw,t_lMask);

    if (t_LastLinErr != errOK)
        return false;
    else
        return true;
}

bool tstPeakLin::initFrameEntry()
{
    for (uint i = 0; i < LIN_MAX_FRAME_NUM; ++i)
    {
        t_FrameEntry[i].FrameId = i;
        t_FrameEntry[i].Length = 8;
        t_FrameEntry[i].Direction = dirDisabled;
        t_FrameEntry[i].ChecksumType = cstCustom;
        t_FrameEntry[i].Flags = FRAME_FLAG_IGNORE_INIT_DATA;
        for (uint j = 0; j < sizeof(t_FrameEntry[i].InitialData); ++j)
            t_FrameEntry[i].InitialData[j] = 0x00;

        t_LastLinErr = t_pPLinApi->SetFrameEntry(t_hClient,t_hHw,&t_FrameEntry[i]);

        if (t_LastLinErr != errOK)
            return false;
    }
    if (t_LastLinErr != errOK)
        return false;
    else
        return true;
}

bool tstPeakLin::setFrameEntry()
{
    int curi;
    for (uint i = 0; i < t_sFrameEntry.framelen; ++i)
    {
        curi = t_sFrameEntry.id[i];

        if (t_FrameEntry[curi].FrameId == curi)
        {
            t_FrameEntry[curi].Length = t_sFrameEntry.datalen[i];
            t_FrameEntry[curi].Direction = t_sFrameEntry.direct[i];
            t_FrameEntry[curi].ChecksumType = t_sFrameEntry.chkrtype[i];
            t_FrameEntry[curi].Flags = t_sFrameEntry.flag[i];
            for (uint j = 0; j < t_sFrameEntry.datalen[i]; ++j)
                t_FrameEntry[curi].InitialData[j] = t_sFrameEntry.initdata[i][j];

            t_LastLinErr = t_pPLinApi->SetFrameEntry(t_hClient,t_hHw,&t_FrameEntry[curi]);

            if (t_LastLinErr != errOK)
                return false;
        }
    }
    if (t_LastLinErr != errOK)
        return false;
    else
        return true;
}

bool tstPeakLin::getSchedule(const byte schdidx, TLINScheduleSlot& pschdslot, int& slotlen)
{

    t_LastLinErr = t_pPLinApi->GetSchedule(t_hHw,schdidx,&pschdslot,sizeof(t_SchdSlotRd)/sizeof(*t_SchdSlotRd),&slotlen);

    if (t_LastLinErr != errOK)
        return false;
    else
        return true;
}

bool tstPeakLin::updateSlotData(const byte id, const byte len, const byte data[])
{
    if (data == NULL || id>=64 || len>8 || !t_open)
        return false;

    if (t_SimTxLIN == false)
    {
        byte dataoffset = 0;
        t_LastLinErr = t_pPLinApi->UpdateByteArray(t_hClient,t_hHw,id,dataoffset,len,(byte*)data);

        if (t_LastLinErr != errOK)
            return false;
        else
            return true;
    }
    else
    {
        return true;
    }
};

communication::HardwareChannels tstPeakLin::availableHardware()
{
    communication::HardwareChannels result;
    int count=0;
    t_LastLinErr=t_pPLinApi->GetAvailableHardware(nullptr,0,&count);
    if (t_LastLinErr!=errOK && t_LastLinErr!=errBufferInsufficient) return result;
    if (count<0 || count>1024) { t_LastLinErr=errWrongParameterValue; return result; }
    if (!count) { t_LastLinErr=errOK; return result; }
    QVector<HLINHW> handles(count);
    t_LastLinErr=t_pPLinApi->GetAvailableHardware(handles.data(),static_cast<WORD>(handles.size()*sizeof(HLINHW)),&count);
    if (t_LastLinErr!=errOK) return result;
    if (count<0 || count>handles.size()) { t_LastLinErr=errBufferInsufficient; return result; }
    for (int i=0;i<count;++i) {
        char name[256]={};
        int device=0,controller=0,serial=0;
        const HLINHW hw=handles[i];
        const auto query=[&](TLINHardwareParam parameter,void *buffer,WORD size) {
            t_LastLinErr=t_pPLinApi->GetHardwareParam(hw,parameter,buffer,size);
            return t_LastLinErr==errOK;
        };
        if (!query(hwpName,name,sizeof(name)) || !query(hwpDeviceNumber,&device,sizeof(device)) ||
            !query(hwpChannelNumber,&controller,sizeof(controller)) || !query(hwpSerialNumber,&serial,sizeof(serial)))
            return {};
        BYTE clients[255]={};
        if (!query(hwpConnectedClients,clients,sizeof(clients))) return {};
        communication::HardwareChannel channel;
        channel.bus=communication::Bus::Lin; channel.handle=hw;
        channel.deviceId=device; channel.controller=controller;
        channel.persistentIdentity=serial!=0;
        channel.key=serial ? QString("lin:%1:%2").arg(static_cast<quint32>(serial)).arg(controller)
                           : QString("lin:runtime:%1:%2").arg(device).arg(controller);
        channel.label=QString("%1 / serial %2 / device %3 / SDK channel %4 [0x%5]")
            .arg(QString::fromLatin1(name,static_cast<int>(strnlen(name,sizeof(name)))))
            .arg(static_cast<quint32>(serial)).arg(device).arg(controller).arg(hw,0,16);
        for (BYTE client : clients) if (client && client!=t_hClient) channel.available=false;
        result.append(channel);
    }
    t_LastLinErr=errOK;
    return result;
}
bool tstPeakLin::setHardwareHandle(quint32 handle)
{
    if (t_open || !handle || handle>0xffff) { t_LastLinErr=errWrongParameterValue; return false; }
    t_hHw=static_cast<HLINHW>(handle); return true;
}
communication::Health tstPeakLin::hardwareHealth(QString *detail)
{
    using communication::Health;
    const auto result=[detail](Health health,const QString &message) {
        if (detail) *detail=message; return health;
    };
    if (!t_open) return result(Health::Removed,QStringLiteral("LIN channel is closed"));
    if (t_SimTxLIN) return result(Health::Ready,QStringLiteral("Simulation"));
    TLINHardwareStatus status={};
    t_LastLinErr=t_pPLinApi->GetStatus(t_hHw,&status);
    if (t_LastLinErr==errIllegalHardware || t_LastLinErr==errIllegalClient)
        return result(Health::Removed,lastErrorText());
    if (t_LastLinErr!=errOK) return result(Health::Error,lastErrorText());
    if (status.Mode!=t_HwMode || status.Status==hwsNotInitialized)
        return result(Health::Error,QStringLiteral("LIN hardware is not initialized in the selected mode"));
    switch (status.Status) {
    case hwsSleep: return result(Health::Sleeping,QStringLiteral("LIN bus sleeping; adapter remains connected"));
    case hwsActive: return result(Health::Ready,QStringLiteral("LIN bus active"));
    case hwsVBatMissing: return result(Health::BusWarning,QStringLiteral("LIN external supply (VBAT) missing; USB remains connected"));
    case hwsShortGround: return result(Health::BusWarning,QStringLiteral("LIN bus shorted to ground; USB remains connected"));
    default: return result(Health::BusWarning,QString("LIN bus state %1").arg(status.Status));
    }
}
QString tstPeakLin::lastErrorText() const
{
    if (!t_pPLinApi->isLoaded())
        return QString("PLIN DLL load failed (%1): %2").arg(t_pPLinApi->libraryError()).arg(t_pPLinApi->libraryPath());
    char text[256]={};
    t_pPLinApi->GetErrorText(t_LastLinErr,0x09,text,sizeof(text));
    if (!text[0]) {
        if (t_LastLinErr==errManagerNotLoaded) return QStringLiteral("PLIN 1002: PLIN Manager service is not running or not installed");
        if (t_LastLinErr==errManagerNotResponding) return QStringLiteral("PLIN 1003: PLIN Manager is not responding");
    }
    return QString("PLIN %1: %2").arg(t_LastLinErr).arg(QString::fromLocal8Bit(text));
}
bool tstPeakLin::sendRaw(TLINMsg message)
{
    if (!t_open) { t_LastLinErr=errIllegalHardwareState; return false; }
    if (message.FrameId>0x3f || message.Length>8 ||
        (message.Direction!=dirPublisher && message.Direction!=dirSubscriber && message.Direction!=dirSubscriberAutoLength)) {
        t_LastLinErr=errWrongParameterValue; return false;
    }
    // Public raw API accepts the six-bit ID. PID and checksum belong to the SDK adapter.
    if (message.FrameId==0x3c || message.FrameId==0x3d) message.ChecksumType=cstClassic;
    if (t_SimTxLIN) return true;
    t_LastLinErr=t_pPLinApi->GetPID(&message.FrameId);
    if (t_LastLinErr!=errOK) return false;
    // Subscriber writes send only the header; the response checksum belongs to the slave.
    if (message.Direction==dirPublisher) {
        t_LastLinErr=t_pPLinApi->CalculateChecksum(&message);
        if (t_LastLinErr!=errOK) return false;
    }
    t_LastLinErr=t_pPLinApi->Write(t_hClient,t_hHw,&message);
    return t_LastLinErr==errOK;
}
DWORD tstPeakLin::recvRaw(DWORD capacity, TLINRcvMsg messages[])
{
    if (!messages || !capacity) return 0;
    if (!t_open) { t_LastLinErr=errIllegalHardwareState; return 0; }
    if (t_SimRxLIN || t_SimTxLIN) { t_LastLinErr=errRcvQueueEmpty; return 0; }
    int count=0;
    const int limit=static_cast<int>(std::min<DWORD>(capacity,LIN_MAX_SLOT_NUM));
    t_LastLinErr=t_pPLinApi->ReadMulti(t_hClient,messages,limit,&count);
    if (t_LastLinErr!=errOK && t_LastLinErr!=errRcvQueueEmpty) return 0;
    if (count<0 || count>limit) { t_LastLinErr=errBufferInsufficient; return 0; }
    return static_cast<DWORD>(count);
}
