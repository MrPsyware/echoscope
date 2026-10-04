#pragma once
#include <cstdint>
namespace sky {
// A press on the touchscreen can generate touch and mechanical-button events
// in either order. Defer a bare click briefly and consume it if touch overlaps.
struct InputGate {
    bool touching=false,buttonHeld=false,touchedDuringPress=false,pending=false,seenTouch=false,second=false,doublePending=false,turned=false;
    uint32_t lastTouch=0,releasedAt=0;
    void touchBegin(uint32_t now) { if(!buttonHeld) turned=false; touching=true; seenTouch=true; lastTouch=now; pending=false; second=false; doublePending=false; if(buttonHeld) touchedDuringPress=true; }
    void touchEnd(uint32_t now) { touching=false; seenTouch=true; lastTouch=now; pending=false; second=false; doublePending=false; }
    void buttonBegin(uint32_t now) {
        // Queued button timestamps can precede a touch callback already handled
        // by LVGL after a slow frame. Count nearby touches in either direction.
        const int32_t touchDelta=static_cast<int32_t>(now-lastTouch);
        turned=false;
        second=pending && uint32_t(now-releasedAt)<=400;
        buttonHeld=true; touchedDuringPress=touching || (seenTouch && touchDelta>-180 && touchDelta<180); pending=false;
    }
    void buttonEnd(uint32_t now,bool longPress) {
        buttonHeld=false; pending=!longPress && !touchedDuringPress && !touching; releasedAt=now;
        if(pending && second) { doublePending=true; pending=false; } second=false;
    }
    void cancelPress() { turned=true; pending=false; second=false; doublePending=false; touchedDuringPress=true; }
    bool takeDouble() { bool result=doublePending && !touching; doublePending=false; return result; }
    bool takeClick(uint32_t now,uint32_t delay=180) {
        if(!pending || uint32_t(now-releasedAt)<delay) return false;
        pending=false; return !touching;
    }
};
}
