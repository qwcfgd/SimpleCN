#pragma once
#include <QtGlobal>
namespace boot {
// Internal address contracts. File markers describe content, not this topology.
struct S32kLayout {
    quint32 bootBytes,bootB,appBase,appBytes,t010AppB,t010AppBytes;
};
inline constexpr S32kLayout S32kLegacy60{0xf000,0x6d000,0x15000,0x57000,0x49000,0x33000};
inline constexpr S32kLayout S32kUnified80{0x14000,0x68000,0x1a000,0x4d000,0x4b000,0x30000};
inline const S32kLayout *s32kBootLayout(quint32 base,quint32 bytes){
    if((base==0x5000||base==S32kUnified80.bootB)&&bytes==S32kUnified80.bootBytes)return &S32kUnified80;
    if((base==0x5000||base==S32kLegacy60.bootB)&&bytes&&bytes<=S32kLegacy60.bootBytes)return &S32kLegacy60;
    return nullptr;
}
inline bool s32kAppVectors(quint32 stack,quint32 reset,quint32 base,quint32 imageBytes){
    const quint64 entry=reset&~quint32(1);
    return stack>0x1fff9000&&stack<=0x20001400&&!(stack&7u)&&(reset&1u)&&
        entry>=quint64(base)+0x1400u&&entry<quint64(base)+imageBytes;
}
inline int s32kT010Slot(quint32 base){
    if(base==S32kLegacy60.appBase||base==S32kUnified80.appBase)return 0;
    if(base==S32kLegacy60.t010AppB||base==S32kUnified80.t010AppB)return 1;
    return -1;
}
}
