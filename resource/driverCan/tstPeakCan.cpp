#include "tstPeakCan.h"
#include <algorithm>
#include <cstring>

tstPeakCan::tstPeakCan(void *parent, PCANBasicClass *api)
{
    SimTxCAN = false;
    SimRxCAN = false;

    //device parameter configure:
    Q_UNUSED(parent);
    t_ownsApi = api == nullptr;
    t_pPCANBasic = api ? api : new PCANBasicClass(); // the masked API pointer.

    t_PCANStatus = PCAN_ERROR_OK; // latest function response device status

    t_hHw = PCAN_USBBUS1;
    t_hAllHw[0] = PCAN_USBBUS1;
    t_hAllHw[1] = PCAN_USBBUS2;
    t_baudrate = PCAN_BAUD_500K;

    t_MsgRxMode = 0;

    t_MsgTx = {};
    t_BufTx = {};

    t_MsgRx = {};
    t_BufRx = {};

    t_MsgRxArrSize = 0;
    memset(t_MsgRxArr,0,sizeof(t_MsgRxArr));
    memset(t_BufRxArr,0,sizeof(t_BufRxArr));
}

tstPeakCan::~tstPeakCan()
{
    stopDevice();
    if (t_ownsApi) delete t_pPCANBasic;
}

bool tstPeakCan::startDevice()
{
    if (t_open) return true;
    if (SimTxCAN) { t_open = true; return true; }
    if (!t_pPCANBasic->isLoaded()) { t_PCANStatus = PCAN_ERROR_NODRIVER; return false; }
    const auto channels = availableHardware();
    if (t_PCANStatus != PCAN_ERROR_OK) return false;
    bool found = false;
    for (const auto &channel : channels) if (channel.handle == t_hHw) {
        found = true;
        if (!channel.available) { t_PCANStatus = PCAN_ERROR_HWINUSE; return false; }
    }
    if (!found) { t_PCANStatus = PCAN_ERROR_ILLHW; return false; }
    t_PCANStatus = t_pPCANBasic->Initialize(t_hHw,t_baudrate);
    if (t_PCANStatus != PCAN_ERROR_OK) return false;
    t_acquired = true;
    if (!setFilter(true)) {
        const auto error = t_PCANStatus;
        stopDevice(); t_PCANStatus = error;
        return false;
    }
    t_open = true;
    return true;
}

bool tstPeakCan::stopDevice()
{
    t_open = false;
    if (!t_acquired) { t_PCANStatus = PCAN_ERROR_OK; return true; }
    // Reset failure (including unplug) must never skip Uninitialize.
    const auto reset = t_pPCANBasic->Reset(t_hHw);
    const auto release = t_pPCANBasic->Uninitialize(t_hHw);
    t_acquired = false;
    t_MsgRxArrSize = 0;
    t_PCANStatus = release != PCAN_ERROR_OK ? release : reset;
    return t_PCANStatus == PCAN_ERROR_OK;
}

bool tstPeakCan::setDevChn(const byte sdevchn)
{
    if (sdevchn != 1 && sdevchn != 2) { t_PCANStatus = PCAN_ERROR_ILLPARAMVAL; return false; }
    return setHardwareHandle(sdevchn == 1 ? PCAN_USBBUS1 : PCAN_USBBUS2);
}

bool tstPeakCan::setDevBaudrate(const word sBaudrate)
{
    if (t_open) return false;
    switch (sBaudrate)
    {
    case 1000:
        t_baudrate = PCAN_BAUD_1M ;
        break;

    case 800:
        t_baudrate = PCAN_BAUD_800K;
        break;

    case 500:
        t_baudrate = PCAN_BAUD_500K;
        break;

    case 250:
        t_baudrate = PCAN_BAUD_250K;
        break;

    case 125:
        t_baudrate = PCAN_BAUD_125K;
        break;

    case 100:
        t_baudrate = PCAN_BAUD_100K;
        break;

    case 95:
        t_baudrate = PCAN_BAUD_95K;
        break;

    case 83:
        t_baudrate = PCAN_BAUD_83K;
        break;

    case 50:
        t_baudrate = PCAN_BAUD_50K;
        break;

    case 47:
        t_baudrate = PCAN_BAUD_47K;
        break;

    case 33:
        t_baudrate = PCAN_BAUD_33K;
        break;

    case 20:
        t_baudrate = PCAN_BAUD_20K;
        break;

    case 10:
        t_baudrate = PCAN_BAUD_10K;
        break;

    case 5:
        t_baudrate = PCAN_BAUD_5K;
        break;
    default:
        t_PCANStatus = PCAN_ERROR_ILLPARAMVAL;
        return false;

    }

    return true;
};

bool tstPeakCan::sendMsg(const StrtCanBuf &buf)
{
    TPCANMsg message = {};
    message.ID = buf.id; message.LEN = buf.len; message.MSGTYPE = PCAN_MESSAGE_STANDARD;
    if (buf.len > 8 || buf.id > 0x7ff) { t_PCANStatus = PCAN_ERROR_ILLDATA; return false; }
    std::memcpy(message.DATA,buf.data,buf.len);
    return sendRaw(message);
}

bool tstPeakCan::clearMsg()
{
    if (!t_open || SimTxCAN) return true;
    t_PCANStatus = t_pPCANBasic->Reset(t_hHw);
    return t_PCANStatus == PCAN_ERROR_OK;
}

DWORD tstPeakCan::recvMsg(const DWORD oncerecvnum, StrtCanBuf buf[])
{
    if (!buf || oncerecvnum == 0) return 0;
    const DWORD capacity = std::min<DWORD>(oncerecvnum,CAN_MAX_MSG_NUM);
    const DWORD count = recvRaw(capacity,t_MsgRxArr,t_MsgRxArrTime);
    DWORD copied = 0;
    for (DWORD i=0; i<count; ++i) {
        if (t_MsgRxArr[i].MSGTYPE & (PCAN_MESSAGE_RTR | PCAN_MESSAGE_STATUS | PCAN_MESSAGE_ERRFRAME)) continue;
        buf[copied].id = t_MsgRxArr[i].ID;
        buf[copied].len = t_MsgRxArr[i].LEN;
        std::memcpy(buf[copied].data,t_MsgRxArr[i].DATA,t_MsgRxArr[i].LEN);
        ++copied;
    }
    t_MsgRxArrSize = copied;
    return copied;
}

int tstPeakCan::getDevChn()
{
    return t_hHw;
};

int tstPeakCan::getDevBaudrate()
{
    return t_baudrate;
};

bool tstPeakCan::getFilter(int &filter)
{
    t_PCANStatus = t_pPCANBasic->GetValue(t_hHw,PCAN_MESSAGE_FILTER,&filter,sizeof(filter));

    if (t_PCANStatus != PCAN_ERROR_OK)
        return false;
    else
        return true;
}

bool tstPeakCan::setBufTx(const StrtCanBuf &buf)
{
    t_BufTx.id = buf.id;
    t_BufTx.len = buf.len;
    for (uint i = 0; i < buf.len; ++i)
        t_BufTx.data[i] = buf.data[i];

    return true;
};

bool tstPeakCan::setMsgTx(const StrtCanBuf &buf)
{

    t_MsgTx.ID = buf.id;
    t_MsgTx.LEN = buf.len;
    t_MsgTx.MSGTYPE = PCAN_MESSAGE_STANDARD;
    for (uint i = 0; i < buf.len; ++i )
        t_MsgTx.DATA[i] = buf.data[i];

    return true;
};

bool tstPeakCan::getBufRx(const DWORD recvnum, StrtCanBuf buf[])
{
    if (buf == NULL)
        return 0;

    for (uint i = 0; i < recvnum; ++i)
    {
        buf[i].id = t_BufRxArr[i].id;
        buf[i].len = t_BufRxArr[i].len;
        for (uint j = 0; j < t_BufRxArr[i].len; ++j )
            buf[i].data[j] = t_BufRxArr[i].data[j];
    }
    return true;
};

bool tstPeakCan::getMsgRx(const DWORD recvnum, StrtCanBuf buf[])
{
    if (buf == NULL)
        return 0;

    for (uint i = 0; i < recvnum; ++i)
    {
        buf[i].id = t_MsgRxArr[i].ID;
        buf[i].len = t_MsgRxArr[i].LEN;
        for (uint j = 0; j < t_MsgRxArr[i].LEN; ++j )
            buf[i].data[j] = t_MsgRxArr[i].DATA[j];
    }
    return true;
};

bool tstPeakCan::setFilter(const bool en)
{
    int setvalue;

    if (en == true)
    {
        setvalue = PCAN_FILTER_OPEN;
        t_PCANStatus = t_pPCANBasic->SetValue(t_hHw,PCAN_MESSAGE_FILTER,&setvalue,sizeof(setvalue));
    }
    else
    {
        setvalue = PCAN_FILTER_CLOSE;
        t_PCANStatus = t_pPCANBasic->SetValue(t_hHw,PCAN_MESSAGE_FILTER,&setvalue,sizeof(setvalue));
    }

    if (t_PCANStatus != PCAN_ERROR_OK)
        return false;
    else
        return true;
}

bool tstPeakCan::setFilter(const word startid, const word endid)
{
    int setvalue = PCAN_FILTER_CUSTOM;

    t_PCANStatus = t_pPCANBasic->SetValue(t_hHw,PCAN_MESSAGE_FILTER,&setvalue,sizeof(setvalue));
    if (t_PCANStatus == PCAN_ERROR_OK)
        t_PCANStatus = t_pPCANBasic->FilterMessages(t_hHw,startid,endid,PCAN_MESSAGE_STANDARD);

    if (t_PCANStatus != PCAN_ERROR_OK)
        return false;
    else
        return true;
}

communication::HardwareChannels tstPeakCan::availableHardware()
{
    communication::HardwareChannels result;
    DWORD count = 0;
    t_PCANStatus = t_pPCANBasic->GetValue(PCAN_NONEBUS,PCAN_ATTACHED_CHANNELS_COUNT,&count,sizeof(count));
    if (t_PCANStatus != PCAN_ERROR_OK) return result;
    if (count > 1024) { t_PCANStatus = PCAN_ERROR_RESOURCE; return result; }
    if (!count) return result;
    QVector<TPCANChannelInformation> info(static_cast<int>(count));
    t_PCANStatus = t_pPCANBasic->GetValue(PCAN_NONEBUS,PCAN_ATTACHED_CHANNELS,info.data(),count*sizeof(TPCANChannelInformation));
    if (t_PCANStatus != PCAN_ERROR_OK) return result;
    for (const auto &item : info) {
        communication::HardwareChannel channel;
        channel.bus = communication::Bus::Can; channel.handle = item.channel_handle;
        channel.deviceId = item.device_id; channel.controller = item.controller_number;
        channel.persistentIdentity = item.device_id != 0;
        channel.key = QString("can:%1:%2:%3").arg(item.device_type).arg(item.device_id).arg(item.controller_number);
        channel.label = QString("%1 / device %2 / channel %3 [0x%4]")
            .arg(QString::fromLatin1(item.device_name, static_cast<int>(strnlen(item.device_name,sizeof(item.device_name)))))
            .arg(item.device_id).arg(item.controller_number+1).arg(item.channel_handle,0,16);
        channel.available = item.channel_condition == PCAN_CHANNEL_AVAILABLE ||
                            (t_open && item.channel_handle == t_hHw);
        result.append(channel);
    }
    return result;
}
bool tstPeakCan::setHardwareHandle(quint32 handle)
{
    if (t_open || !handle || handle > 0xffff) { t_PCANStatus = PCAN_ERROR_ILLPARAMVAL; return false; }
    t_hHw = static_cast<TPCANHandle>(handle);
    return true;
}
communication::Health tstPeakCan::hardwareHealth()
{
    using communication::Health;
    if (!t_open) return Health::Removed;
    if (SimTxCAN) return Health::Ready;
    t_PCANStatus = t_pPCANBasic->GetStatus(t_hHw);
    if (t_PCANStatus == PCAN_ERROR_OK) return Health::Ready;
    if (t_PCANStatus & (PCAN_ERROR_INITIALIZE | PCAN_ERROR_NODRIVER | PCAN_ERROR_ILLHANDLE))
        return Health::Removed;
    if (t_PCANStatus & PCAN_ERROR_ANYBUSERR) return Health::BusWarning;
    return Health::Error;
}
bool tstPeakCan::isDeviceActive()
{
    const auto health = hardwareHealth();
    return health == communication::Health::Ready || health == communication::Health::BusWarning;
}
QString tstPeakCan::lastErrorText() const
{
    if (!t_pPCANBasic->isLoaded())
        return QString("PCAN DLL load failed (%1): %2").arg(t_pPCANBasic->libraryError()).arg(t_pPCANBasic->libraryPath());
    char text[256] = {};
    t_pPCANBasic->GetErrorText(t_PCANStatus,0,text);
    return QString("PCAN 0x%1: %2").arg(t_PCANStatus,0,16).arg(QString::fromLocal8Bit(text));
}
bool tstPeakCan::sendRaw(TPCANMsg message)
{
    if (!t_open) { t_PCANStatus = PCAN_ERROR_INITIALIZE; return false; }
    const bool extended = (message.MSGTYPE & PCAN_MESSAGE_EXTENDED) != 0;
    if (message.LEN > 8 || message.ID > (extended ? 0x1fffffffU : 0x7ffU) ||
        (message.MSGTYPE & ~(PCAN_MESSAGE_EXTENDED | PCAN_MESSAGE_RTR))) {
        t_PCANStatus = PCAN_ERROR_ILLDATA; return false;
    }
    t_PCANStatus = SimTxCAN ? PCAN_ERROR_OK : t_pPCANBasic->Write(t_hHw,&message);
    return t_PCANStatus == PCAN_ERROR_OK;
}
DWORD tstPeakCan::recvRaw(DWORD capacity, TPCANMsg messages[], TPCANTimestamp timestamps[])
{
    if (!messages || !capacity) return 0;
    if (!t_open) { t_PCANStatus = PCAN_ERROR_INITIALIZE; return 0; }
    capacity = std::min<DWORD>(capacity,CAN_MAX_MSG_NUM);
    if (SimRxCAN || SimTxCAN) { t_PCANStatus = PCAN_ERROR_QRCVEMPTY; return 0; }
    DWORD count = 0;
    while (count < capacity) {
        TPCANMsg msg = {}; TPCANTimestamp time = {};
        t_PCANStatus = t_pPCANBasic->Read(t_hHw,&msg,&time);
        if (t_PCANStatus != PCAN_ERROR_OK) break;
        if (msg.LEN > 8) { t_PCANStatus = PCAN_ERROR_ILLDATA; break; }
        messages[count] = msg;
        if (timestamps) timestamps[count] = time;
        ++count;
    }
    return count;
}
