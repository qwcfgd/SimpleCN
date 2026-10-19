// Synthetic ABI fixture, compiled only into qttemp; no target ECU algorithm.
#include <windows.h>
extern "C" __declspec(dllexport) int __cdecl GenerateKeyEx(
    unsigned char* seed,unsigned int size,unsigned int level,char*,
    unsigned char* key,unsigned int capacity,unsigned int& length){
    if(size<1||capacity<size)return 1;
    for(unsigned int i=0;i<size;++i)key[i]=static_cast<unsigned char>(seed[i]^0x5a^level);
    length=size;return 0;
}
