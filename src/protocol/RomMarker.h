#ifndef ROM_MARKER_H
#define ROM_MARKER_H
/* Shared host/ECU image contract. Keep the deployed ECU copy byte-identical. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define ROM_MARKER_BYTES 64u
#define ROM_MARKER_PAGE_BYTES 4096u
#define ROM_MARKER_APP_OFFSET (ROM_MARKER_PAGE_BYTES-ROM_MARKER_BYTES)
#define ROM_MARKER_BOOT_OFFSET (0x14000u-ROM_MARKER_BYTES)
#define ROM_MARKER_DRIVER 1u
#define ROM_MARKER_APP 2u
#define ROM_MARKER_BOOT 3u
#define ROM_MARKER_CAL 4u

typedef struct {
    uint8_t project[8],version[8],date[8];
    uint32_t kind,imageBytes;
    uint64_t bodyCrc;
} RomMarker;

#if defined(ROM_MARKER_EXTERN) && !defined(ROM_MARKER_IMPLEMENTATION)
uint64_t RomMarker_Crc(uint64_t,const uint8_t*,size_t);
uint64_t RomMarker_Read64(const uint8_t*);
int RomMarker_Hex(uint8_t);
int RomMarker_Date(const uint8_t[8]);
uint32_t RomMarker_Kind(const uint8_t[8]);
int RomMarker_Decode(const uint8_t[64],RomMarker*);
int RomMarker_Match(const RomMarker*,const RomMarker*);
int RomMarker_Image(const uint8_t*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,RomMarker*);
#else
#if defined(ROM_MARKER_IMPLEMENTATION)
#define ROM_MARKER_FN
#else
#define ROM_MARKER_FN static inline
#endif
ROM_MARKER_FN uint64_t RomMarker_Crc(uint64_t crc,const uint8_t *data,size_t bytes)
{
    static const uint64_t table[16]={
        UINT64_C(0x0000000000000000),UINT64_C(0x42F0E1EBA9EA3693),
        UINT64_C(0x85E1C3D753D46D26),UINT64_C(0xC711223CFA3E5BB5),
        UINT64_C(0x493366450E42ECDF),UINT64_C(0x0BC387AEA7A8DA4C),
        UINT64_C(0xCCD2A5925D9681F9),UINT64_C(0x8E224479F47CB76A),
        UINT64_C(0x9266CC8A1C85D9BE),UINT64_C(0xD0962D61B56FEF2D),
        UINT64_C(0x17870F5D4F51B498),UINT64_C(0x5577EEB6E6BB820B),
        UINT64_C(0xDB55AACF12C73561),UINT64_C(0x99A54B24BB2D03F2),
        UINT64_C(0x5EB4691841135847),UINT64_C(0x1C4488F3E8F96ED4)
    };
    size_t i;
    for(i=0u;i<bytes;++i){
        crc^=(uint64_t)data[i]<<56;
        crc=(crc<<4)^table[(uint32_t)(crc>>60)];
        crc=(crc<<4)^table[(uint32_t)(crc>>60)];
    }
    return crc;
}
ROM_MARKER_FN uint64_t RomMarker_Read64(const uint8_t *p)
{
    uint64_t value=0u;uint32_t i;
    for(i=0u;i<8u;++i)value=(value<<8)|p[i];
    return value;
}
ROM_MARKER_FN int RomMarker_Hex(uint8_t ch)
{
    if(ch>='0' && ch<='9')return (int)(ch-'0');
    if(ch>='A' && ch<='F')return (int)(ch-'A'+10u);
    return -1;
}
ROM_MARKER_FN int RomMarker_Date(const uint8_t date[8])
{
    static const uint8_t days[12]={31u,28u,31u,30u,31u,30u,31u,31u,30u,31u,30u,31u};
    uint32_t i,year=0u,month,day,limit;
    for(i=0u;i<8u;++i)if(date[i]<'0' || date[i]>'9')return 0;
    for(i=0u;i<4u;++i)year=year*10u+(uint32_t)(date[i]-'0');
    month=(uint32_t)(date[4]-'0')*10u+(uint32_t)(date[5]-'0');
    day=(uint32_t)(date[6]-'0')*10u+(uint32_t)(date[7]-'0');
    if(year<1u || month<1u || month>12u)return 0;
    limit=days[month-1u];
    if(month==2u && year%4u==0u && (year%25u!=0u || (year&15u)==0u))++limit;
    return day>=1u && day<=limit;
}
ROM_MARKER_FN uint32_t RomMarker_Kind(const uint8_t type[8])
{
    static const uint8_t types[4][8]={
        {'D','R','I','V','E','R',255u,255u},
        {'A','P','P',255u,255u,255u,255u,255u},
        {'B','O','O','T',255u,255u,255u,255u},
        {'C','A','L',255u,255u,255u,255u,255u}
    };
    uint32_t i;
    for(i=0u;i<4u;++i)if(memcmp(type,types[i],8u)==0)return i+1u;
    return 0u;
}
ROM_MARKER_FN int RomMarker_Decode(const uint8_t raw[64],RomMarker *out)
{
    uint32_t i,value=0u,minor=0u;int digit;uint64_t dateCrc;
    if(raw==NULL || out==NULL)return 1;
    memset(out,0,sizeof *out);
    if(memcmp(raw,"S32K144T",8u)!=0 || !RomMarker_Date(raw+24u) ||
       RomMarker_Crc(0u,raw,56u)!=RomMarker_Read64(raw+56u))return 1;
    out->kind=RomMarker_Kind(raw+8u);if(out->kind==0u)return 1;
    for(i=16u;i<20u;++i)if(!((raw[i]>='A' && raw[i]<='Z') || (raw[i]>='0' && raw[i]<='9')))return 1;
    for(i=20u;i<24u;++i){digit=RomMarker_Hex(raw[i]);if(digit<0)return 1;minor=(minor<<4)|(uint32_t)digit;}
    dateCrc=RomMarker_Crc(0u,raw+24u,8u);
    if(minor!=(uint32_t)(dateCrc>>48))return 1;
    /* The wire field is 16 ASCII hexadecimal digits; UDS addresses remain 32-bit. */
    for(i=32u;i<48u;++i){
        digit=RomMarker_Hex(raw[i]);if(digit<0 || (i<40u && digit!=0))return 1;
        if(i>=40u)value=(value<<4)|(uint32_t)digit;
    }
    if(value<ROM_MARKER_BYTES)return 1;
    memcpy(out->project,raw,8u);memcpy(out->version,raw+16u,8u);memcpy(out->date,raw+24u,8u);
    out->imageBytes=value;out->bodyCrc=RomMarker_Read64(raw+48u);return 0;
}
ROM_MARKER_FN int RomMarker_Match(const RomMarker *a,const RomMarker *b)
{
    return a!=NULL && b!=NULL && memcmp(a->project,b->project,8u)==0 &&
        memcmp(a->version,b->version,4u)==0;
}
ROM_MARKER_FN int RomMarker_Image(const uint8_t *data,uint32_t bytes,uint32_t markerOffset,
    uint32_t skipOffset,uint32_t skipBytes,uint32_t kind,RomMarker *out)
{
    uint64_t crc;
    if(data==NULL || bytes<64u || markerOffset>bytes-64u ||
       skipOffset>bytes || skipBytes>bytes-skipOffset ||
       markerOffset<skipOffset || markerOffset-skipOffset>skipBytes ||
       skipBytes-(markerOffset-skipOffset)<64u ||
       RomMarker_Decode(data+markerOffset,out)!=0 ||
       out->kind!=kind || out->imageBytes!=bytes)return 1;
    crc=RomMarker_Crc(0u,data,skipOffset);
    crc=RomMarker_Crc(crc,data+skipOffset+skipBytes,bytes-skipOffset-skipBytes);
    return crc==out->bodyCrc?0:1;
}
#undef ROM_MARKER_FN
#endif
#endif
