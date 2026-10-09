#ifndef USER_GLOBALDATATYPE_H
#define USER_GLOBALDATATYPE_H
#pragma once

#include "dataTypeAlias.h"
#include "../dll/PCANBasic.h"
#include "../dll/PLinApi.h"

/************************************************/
/*                    General                   */
/************************************************/
struct StrtConditionDetect
{
    int64 cur;
    int64 total;
};

struct StrtProgressParam
{
    StrtConditionDetect stage;
    StrtConditionDetect idx;
    StrtConditionDetect suspendTime;
};

/************************************************/
/*                      CAN                     */
/************************************************/
static const byte CAN_MAX_MSG_NUM = 255;
static const byte CAN_MAX_DATA_LEN = 8;

typedef struct StrtMsgConfig
{
    byte msglen; //vaild frame number.
    dword id[CAN_MAX_MSG_NUM]; //id
    qword msgenum[CAN_MAX_MSG_NUM]; //msg enum.
} StrtMsgConfig;

// Can single message set.
typedef struct StrtCanBuf
{
    dword id; //ID, need to be point to StrtCanMultiBuf CanMultiBuf.id;
    byte len; //length, need to be point to StrtCanMultiBuf CanMultiBuf.len;
    byte data[CAN_MAX_DATA_LEN]; //data, need to be point to StrtCanMultiBuf CanMultiBuf.len;
} StrtCanBuf;

/************************************************/
/*                      LIN                     */
/************************************************/
static const byte LIN_MAX_SCHEDULE_NUM = LIN_MAX_SCHEDULES;
static const byte LIN_MAX_SLOT_NUM = 255;
static const byte LIN_MAX_FRAME_NUM = 64;
static const byte LIN_MAX_DATA_LEN = 8;
static const byte MST_TX = dirPublisher;
static const byte MST_RX = dirSubscriber;
static const byte DATA_MST = FRAME_FLAG_RESPONSE_ENABLE;
static const byte DATA_SLV = FRAME_FLAG_SINGLE_SHOT;
static const byte DATA_EMPTY = FRAME_FLAG_IGNORE_INIT_DATA;

// Lin hardware entry permission set.
typedef struct StrtFrameEntry
{
    float linstandard; //lin standard, before 2.0 chkrtype all use classic, after 2.0 chkrtype all use enhanced except 0x3c/0x3d.
    byte framelen; //vaild frame number.
    byte id[LIN_MAX_FRAME_NUM]; //id
    dword txframemalf[LIN_MAX_FRAME_NUM]; //Tx framemalf, each bit match to a specific LinFrame(max 32 nodes), the lin frame sequence refers to databaseLin.
    dword rxframemalf[LIN_MAX_FRAME_NUM]; //Rx framemalf, each bit match to a specific LinFrame(max 32 nodes), the lin frame sequence refers to databaseLin.
    byte direct[LIN_MAX_FRAME_NUM]; //direction
    byte datalen[LIN_MAX_FRAME_NUM]; //length
    word flag[LIN_MAX_FRAME_NUM]; //flag
    byte initdata[LIN_MAX_FRAME_NUM][LIN_MAX_DATA_LEN]; //initial data
    byte chkrtype[LIN_MAX_FRAME_NUM]; //checksum type
} StrtFrameEntry;

// LIN Master single frame set.
typedef struct StrtLinTxBuf
{
    byte data[LIN_MAX_DATA_LEN]; //Data by array format
    byte id; //ID
    byte* pdirect; //pointer of direction, after check the id, set to point to StrtFrameEntry FrameEntry.direct;
    byte* pdatalen; //pointer of DataLen, after check the id, set to point to StrtFrameEntry FrameEntry.direct;
    byte* pchkrtype; //pointer of DataLen, after check the id, set to point to StrtFrameEntry FrameEntry.direct;
} StrtLinTxBuf;

// Advanced Lin Master schedule and frame data set.
typedef struct StrtSchdSlot
{
    DWORD slotlen; //vaild slot number
    byte id[LIN_MAX_SLOT_NUM]; //id by byte format
    word delay[LIN_MAX_SLOT_NUM]; //delay by array format
} StrtSchdSlot;

typedef struct StrtLinSchdTxBuf
{
    byte data[LIN_MAX_SLOT_NUM][LIN_MAX_DATA_LEN]; //data by 2-D array format
    DWORD* pslotlen; //pointer of vaild slot number, need to be initialized to point to StrtSchdSlot SchdSlot.slotlen;
    byte* pid[LIN_MAX_SLOT_NUM]; //pointer of id, need to be initialized to point 1-by-1 to StrtSchdSlot SchdSlot.id;
    byte* pdirect[LIN_MAX_SLOT_NUM]; //pointer of direction, after check the *pid[idx], set to point 1-by-1 to StrtFrameEntry FrameEntry.direct;
    byte* pdatalen[LIN_MAX_SLOT_NUM]; //pointer of data length, after check the *pid[idx], set to point 1-by-1 to StrtFrameEntry FrameEntry.datalen;
    byte* pchkrtype[LIN_MAX_SLOT_NUM]; //pointer of checksumtype, after check the *pid[idx], set to point 1-by-1 to StrtFrameEntry FrameEntry.chkrtype;
} StrtLinSchdTxBuf;

typedef struct StrtLinSchdRxBuf
{
    byte data[LIN_MAX_SLOT_NUM][LIN_MAX_DATA_LEN]; //data by 2-D array format
    DWORD slotlen; //slot length
    byte id[LIN_MAX_SLOT_NUM]; //id
    byte direct[LIN_MAX_SLOT_NUM]; //direct
    byte datalen[LIN_MAX_SLOT_NUM]; //datalen
    byte chkrtype[LIN_MAX_SLOT_NUM]; //checksum type
    int errflag[LIN_MAX_SLOT_NUM]; //error flag
} StrtLinSchdRxBuf;

#endif //USER_GLOBALDATATYPE_H
