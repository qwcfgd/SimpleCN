#ifndef USER_TSTPEAKCAN_H
#define USER_TSTPEAKCAN_H
#pragma once

#include "../global/dataTypeAlias.h"
#include "../global/globalDataType.h"

#include "pcanBasicClass.h"
#include "../communication/ChannelTypes.h"

class tstPeakCan
{
    /***************************************************************************************/
    /* Functions and variables were listed in seperated public/protected/privated keyword, */
    /* for more convenient management.                                                     */
    /***************************************************************************************/

public:
    /* Constructor function */
    explicit tstPeakCan(void *parent = nullptr, PCANBasicClass *api = nullptr);
    ~tstPeakCan();

public:
    /* public functions */
    //following are the device interface function:
    bool startDevice();
    bool stopDevice();

    communication::HardwareChannels availableHardware();
    bool setHardwareHandle(quint32 handle);
    bool isOpen() const { return t_open; }
    unsigned long lastErrorCode() const { return t_PCANStatus; }
    QString lastErrorText() const;
    communication::Health hardwareHealth();
    bool isDeviceActive();
    bool sendRaw(TPCANMsg message);
    DWORD recvRaw(DWORD capacity, TPCANMsg messages[], TPCANTimestamp timestamps[] = nullptr);


    //following are the channel function:
    bool setDevChn(const byte);
    bool setDevBaudrate(const word);

    //following are the send linframe function:
    bool sendMsg(const StrtCanBuf&);

    //following are the receive linframe function:
    bool clearMsg();
    DWORD recvMsg(const DWORD, StrtCanBuf[]);

    //following are the public get function:
    int getDevChn();
    int getDevBaudrate();

protected:
    /* protected functions */
    bool getFilter(int&);

private:
    /* private functions */
    // set BufTx as middle layer format. MsgTx as driver layer format.
    bool setBufTx(const StrtCanBuf&);
    bool setMsgTx(const StrtCanBuf&);

    // set BufRx as middle layer format.
    bool getBufRx(const DWORD, StrtCanBuf[]);
    bool getMsgRx(const DWORD, StrtCanBuf[]);

    //get and set pcan frame table for further initialize hardware can frame filter.
    bool setFilter(const bool); // usage for configure client filter.
    bool setFilter(const word, const word); // usage for configure client filter.

public:
    /* public variables */
    bool SimTxCAN;
    bool SimRxCAN;

protected:
    /* protected variables */

private:
    /* private variables */
    bool t_ownsApi = true;
    bool t_open = false;
    bool t_acquired = false;
    //PLinApiClass *m_pPLinApi; // the masked API pointer.
    PCANBasicClass *t_pPCANBasic;

    //lin FB error code
    TPCANStatus t_PCANStatus; // latest function response device status

    // hardware configuration

    TPCANHandle t_hHw, t_hAllHw[2]; //hardware ID and all hardware ID
    TPCANBaudrate t_baudrate; //baudrate

    // receive message configuration
    int t_MsgRxMode; //reading mode, Timer Mode, Event Mode or Manual Mode

    // following variables is for manual single Tx buffer.
    TPCANTimestamp t_MsgTxTime; //time stamp;
    TPCANMsg t_MsgTx; //send buffer;
    StrtCanBuf t_BufTx; //Tx sturct

    // following variables is for Rx buffer, whether for manual or schedule.
    TPCANTimestamp t_MsgRxTime; //time stamp;
    TPCANMsg t_MsgRx; //recv buffer;
    StrtCanBuf t_BufRx; //RxData sturct

    DWORD t_MsgRxArrSize;
    TPCANTimestamp t_MsgRxArrTime[CAN_MAX_MSG_NUM]; //time stamp;
    TPCANMsg t_MsgRxArr[CAN_MAX_MSG_NUM]; //recv buffer;
    StrtCanBuf t_BufRxArr[CAN_MAX_MSG_NUM]; //RxData sturct

};

#endif //USER_TSTPEAKCAN_H
