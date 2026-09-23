#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace sky {
constexpr size_t photoHeaderSize=392,photoMaxBytes=photoHeaderSize+200*150*2;
inline bool photoPacket(const char *data,size_t length,unsigned &w,unsigned &h) {
    if(length<photoHeaderSize || std::memcmp(data,"ECP1",4)) return false;
    auto b=reinterpret_cast<const unsigned char*>(data);
    w=b[4]|unsigned(b[5])<<8; h=b[6]|unsigned(b[7])<<8;
    return w>0 && w<=200 && h>0 && h<=150 && length==photoHeaderSize+w*h*2
        && data[8] && std::memchr(data+8,0,128) && std::memchr(data+136,0,256)
        && !std::strncmp(data+136,"https://www.planespotters.net/",sizeof("https://www.planespotters.net/")-1);
}
}
