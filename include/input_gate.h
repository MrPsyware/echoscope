#pragma once
#include <cstdint>
namespace sky {
// A press on the touchscreen can generate touch and mechanical-button events
// in either order. Defer a bare click briefly and consume it if touch overlaps.
struct InputGate {
    bool touching=false,buttonHeld=false,touchedDuringPress=false,pending=false,seenTouch=false;
    uint32_t lastTouch=0,releasedAt=0;
    void touchBegin(uint32_t now) { touching=true; seenTouch=true; lastTouch=now; pending=false; if(buttonHeld) touchedDuringPress=true; }
    void touchEnd(uint32_t now) { touching=false; seenTouch=true; lastTouch=now; pending=false; }
    void buttonBegin(uint32_t now) {
        buttonHeld=true; touchedDuringPress=touching || (seenTouch && uint32_t(now-lastTouch)<180); pending=false;
    }
    void buttonEnd(uint32_t now,bool longPress) {
        buttonHeld=false; pending=!longPress && !touchedDuringPress && !touching; releasedAt=now;
    }
    bool takeClick(uint32_t now) {
        if(!pending || uint32_t(now-releasedAt)<180) return false;
        pending=false; return !touching;
    }
};
}
