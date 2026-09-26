#pragma once
#include <cstdint>
#include <cstring>
namespace sky {
constexpr size_t mapBytes=8+420*420*2;
inline bool validMap(const char *p,size_t n) {
    return n==mapBytes && !std::memcmp(p,"ECM1\xa4\x01\xa4\x01",8);
}
struct Station {
    char name[32]{};
    float az=0,el=0,km=0;
    uint32_t nextRise=0;
};
}
