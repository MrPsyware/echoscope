#pragma once
#include <cstdint>
namespace sky {
// A timestamp sampled just before opening setup must not look five minutes old.
inline bool setupExpired(uint32_t now,uint32_t opened) {
    return static_cast<int32_t>(now-opened)>=300000;
}

struct RadarControls {
    uint32_t lastInput=0;
    bool visible(uint32_t now) const { return uint32_t(now-lastInput)<30000; }
    void show(uint32_t now) { lastInput=now; }
    void rotate(uint32_t now) { if(visible(now)) show(now); }
};

struct Activity {
    static constexpr uint32_t filterTimeout=10000, sleepTimeout=3600000;
    uint32_t lastActivity=0,lastTouch=0, sleepAfterMs=sleepTimeout;
    bool sleeping=false,filterVisible=true;
    bool wakeOnWatch=false,autoAwake=false,watchPresent=false,suppressWatch=false;
    uint32_t watchSeen=0;
    void observeWatch(bool present,uint32_t now) { watchPresent=present; if(!present) suppressWatch=false; if(present) watchSeen=now; }
    bool freshWatch(uint32_t now) const { return watchPresent && uint32_t(now-watchSeen)<60000; }
    uint32_t pollInterval(uint32_t now) const { return sleeping && !freshWatch(now)?30000:5000; }
    bool updateWatch(uint32_t now) {
        const bool before=sleeping;
        if(sleeping && !suppressWatch && wakeOnWatch && freshWatch(now)) { sleeping=false; autoAwake=true; }
        else if(autoAwake && (!wakeOnWatch || !freshWatch(now))) { sleeping=true; autoAwake=false; }
        return before!=sleeping;
    }
    void sleepExplicitly() { sleeping=true; autoAwake=false; suppressWatch=true; }
    bool interact(uint32_t now,bool touch=false) {
        autoAwake=false; suppressWatch=false; const bool woke=sleeping; sleeping=false; lastActivity=now;
        if(touch) lastTouch=now;
        return woke;
    }
    bool tick(uint32_t now,bool held=false) {
        if(held) lastActivity=now;
        if(uint32_t(now-lastTouch)>=filterTimeout) filterVisible=false;
        if(!sleeping && !autoAwake && sleepAfterMs && uint32_t(now-lastActivity)>=sleepAfterMs) { sleeping=true; return true; }
        return false;
    }
    bool tapFilter(uint32_t now) {
        const bool cycle=filterVisible;
        filterVisible=true; lastTouch=now; return cycle;
    }
};
}
