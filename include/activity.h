#pragma once
#include <cstdint>
namespace sky {
struct Activity {
    static constexpr uint32_t filterTimeout=10000, sleepTimeout=3600000;
    uint32_t lastActivity=0,lastTouch=0;
    bool sleeping=false,filterVisible=true;
    bool interact(uint32_t now,bool touch=false) {
        const bool woke=sleeping; sleeping=false; lastActivity=now;
        if(touch) lastTouch=now;
        return woke;
    }
    bool tick(uint32_t now,bool held=false) {
        if(held) lastActivity=now;
        if(uint32_t(now-lastTouch)>=filterTimeout) filterVisible=false;
        if(!sleeping && uint32_t(now-lastActivity)>=sleepTimeout) { sleeping=true; return true; }
        return false;
    }
    bool tapFilter(uint32_t now) {
        const bool cycle=filterVisible;
        filterVisible=true; lastTouch=now; return cycle;
    }
};
}
