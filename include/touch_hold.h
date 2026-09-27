#pragma once
#include <cstdint>
namespace sky {
struct TouchHold {
    uint32_t started=0; bool fired=false;
    void begin(uint32_t now) { started=now; fired=false; }
    bool update(uint32_t now,bool allowed) {
        if(fired || !allowed || uint32_t(now-started)<2000) return false;
        fired=true; return true;
    }
};
}
