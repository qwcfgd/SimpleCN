#ifndef USER_TSTPEAKLIN_H
#define USER_TSTPEAKLIN_H
#pragma once

#include "../global/dataTypeAlias.h"
#include "../global/globalDataType.h"

#include "plinApiClass.h"
#include "../communication/ChannelTypes.h"

class tstPeakLin
{
    /***************************************************************************************/
    /* Functions and variables were listed in seperated public/protected/privated keyword, */
    /* for more convenient management.                                                     */
    /***************************************************************************************/

public:
    /* Constructor function */
    explicit tstPeakLin(void *parent = nullptr, PLinApiClass *api = nullptr);
    ~tstPeakLin();

public:
    /* public functions */
    //following are the device interface function:
    bool startDevice();
    bool stopDevice();

    communication::HardwareChannels availableHardware();
    bool setHardwareHandle(quint32 handle);
    bool isOpen() const { return t_open; }
    unsigned long lastErrorCode() const { return t_LastLinErr; }
    QString lastErrorText() const;
    communication::Health hardwareHealth(QString *detail=nullptr);
    bool sendRaw(TLINMsg message);
    DWORD recvRaw(DWORD capacity, TLINRcvMsg messages[]);

    // Signal page operations. All callers must hold the software channel lease
    // and serialize calls on its worker. No extra SDK client is created.
    bool signalConfigureMode(bool master);
    bool signalStop();
    bool signalInstallFrames(const QVector<TLINFrameEntry> &frames);
    bool signalStartSchedule(QVector<TLINScheduleSlot> entries);
    bool signalRequestFrameBoundary();
    bool signalFrameBoundaryReached(bool &reached);
    bool signalRequestRoundBoundary();
    bool signalRoundBoundaryReached(bool &reached);
    bool signalUpdateFrame(BYTE id, const QByteArray &payload);


    //following are the channel function:
    bool setDevChn(const byte);
    bool setDevMode(const byte);
    bool setDevBaudrate(const word);

    //following are the database function:
    bool setFrameEntry(const StrtFrameEntry&);

    //following are the schedule function:
    bool setAllSchedule(const byte, const StrtSchdSlot[]);
    bool setSchedule(const byte, const StrtSchdSlot&);

    bool startSchedule(const byte);
    bool suspendSchedule();
    bool resumeSchedule();

    //following are the send linframe function:
    bool sendMsg(const StrtLinTxBuf&);
    bool setAllSchdData(const byte, const StrtLinSchdTxBuf[]);
    bool setSchdData(const byte, const StrtLinSchdTxBuf&);
    bool setSlotData(const byte, const byte, const StrtLinSchdTxBuf&);

    //following are the receive linframe function:
    bool clearMsg();
    DWORD recvMsg(const DWORD, StrtLinSchdRxBuf&);

    //following are the public get function:
    int getDevChn();
    int getDevMode();
    int getDevBaudrate();
    bool isDeviceActive();
    StrtLinSchdTxBuf* getAllSchdData(byte&);
    StrtLinSchdTxBuf& getSchdData(const byte);

protected:
    /* protected functions */
    TLINFrameEntry* getFrameEntry(const byte);
    DWORD getMsgRxSize();
    DWORD getMsgRxArrSize();

private:
    /* private functions */
    // set BufTx as middle layer format. MsgTx as driver layer format.
    bool setBufTx(const StrtLinTxBuf&);
    bool setMsgTx(const StrtLinTxBuf&);

    bool getBufRx(const DWORD,StrtLinSchdRxBuf&);
    bool getMsgRx(const DWORD,StrtLinSchdRxBuf&);

    // set BufTxArr[] as middle layer format. MsgTxArr[] as driver layer format.
    bool initAllSchdBufTxArr(const byte);
    bool setAllSchdBufTxArr(const byte,const StrtLinSchdTxBuf[]);
    bool setAllSchdMsgTxArr(const byte,const StrtLinSchdTxBuf[]);
    // set BufTxArr as middle layer format. MsgTxArr as driver layer format.
    bool initSchdBufTxArr(const byte);
    bool setSchdBufTxArr(const byte,const StrtLinSchdTxBuf&);
    bool setSchdMsgTxArr(const byte,const StrtLinSchdTxBuf&);
    // set a slot of BufTxArr as middle layer format. a slot of MsgTxArr as driver layer format.
    bool initSlotBufTxArr(const byte,const byte);
    bool setSlotBufTxArr(const byte,const byte,const StrtLinSchdTxBuf&);
    bool setSlotMsgTxArr(const byte,const byte,const StrtLinSchdTxBuf&);

    //get and set plin frame table for further initialize hardware lin frame filter.
    bool setClientFilter(); // usage for configure client filter.
    bool initFrameEntry(); // usage for init client and hardware.
    bool setFrameEntry(); // usage for set internal TLINFrameEntry FrameEntry.

    //get schedule slot. it can not be read before setschedule, which will cause pointer out of bounds failure.
    bool getSchedule(const byte,TLINScheduleSlot&,int&);

    //update data when operating schedule.
    bool updateSlotData(const byte, const byte, const byte[]);

public:
    /* public variables */
    bool t_SimTxLIN;
    bool t_SimRxLIN;

protected:
    /* protected variables */

private:
    /* private variables */
    bool t_ownsApi = true;
    bool t_open = false;
    DWORD t_signalFirstSlot = 0;
    bool t_acquired = false;
    //PLinApiClass *m_pPLinApi; // the masked API pointer.
    PLinApiClass *t_pPLinApi;

    //lin FB error code
    TLINError t_LastLinErr; // latest function response error code

    //client configuration
    LPSTR t_hClientName; // name of new client
    DWORD t_ClientMarkWindow; // requires client open a new mark window
    HLINCLIENT t_hClient; // client index

    // hardware configuration
    HLINHW t_hHw; //hardware ID and all hardware ID
    TLINHardwareMode t_HwMode; //hardware mode, master mode or slave mode
    WORD t_baudrate; //baudrate

    // client configuration
    UINT64 t_lMask;
    StrtFrameEntry t_sFrameEntry;
    TLINFrameEntry t_FrameEntry[LIN_MAX_FRAME_NUM], t_FrameEntryRd[LIN_MAX_FRAME_NUM];

    // lin frame configuration
    byte t_Schdlen;
    StrtSchdSlot t_sSlotWt[LIN_MAX_SCHEDULE_NUM];
    TLINScheduleSlot t_SchdSlotWt[LIN_MAX_SCHEDULE_NUM][LIN_MAX_SLOT_NUM], t_SchdSlotRd[LIN_MAX_SLOT_NUM];

    // following variables is for manual single Tx buffer.
    TLINMsg t_MsgTx; //send buffer;
    StrtLinTxBuf t_BufTx; //Tx sturct

    // following variables is for schedule Tx buffer.
    DWORD t_MsgTxArrSize[LIN_MAX_SCHEDULE_NUM]; // size of MsgTx array;
    TLINMsg t_MsgTxArr[LIN_MAX_SCHEDULE_NUM][LIN_MAX_SLOT_NUM]; // MsgTx array;
    StrtLinSchdTxBuf t_BufTxArr[LIN_MAX_SCHEDULE_NUM]; // BufTxArr sturct

    // following variables is for Rx buffer, whether for manual or schedule.
    DWORD t_MsgRxSize; //vaild buffer array size after filter;
    TLINRcvMsg t_MsgRx[LIN_MAX_SLOT_NUM]; //vaild recv buffer, filtered by original recv MsgRxArr.
    StrtLinSchdRxBuf t_BufRx; //RxData sturct

    // following variables is for multiple TxRx buffer.
    DWORD t_MsgRxArrSize; //recv buffer array size;
    TLINRcvMsg t_MsgRxArr[LIN_MAX_SLOT_NUM]; //recv buffer array;
    StrtLinSchdTxBuf t_BufRxArr; //RxArrData sturct

};

#endif //USER_TSTPEAKLIN_H
