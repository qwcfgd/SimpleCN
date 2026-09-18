#pragma once
// dbcppp uses the GNU byteswap header on MinGW as well as on Linux.
#include <cstdint>
inline std::uint16_t bswap_16(std::uint16_t v){return __builtin_bswap16(v);}
inline std::uint32_t bswap_32(std::uint32_t v){return __builtin_bswap32(v);}
inline std::uint64_t bswap_64(std::uint64_t v){return __builtin_bswap64(v);}
