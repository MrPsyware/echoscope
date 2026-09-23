#pragma once
#include <cstdint>
namespace sky {
struct ButtonDebounce {
    enum Event { None, Down, Up, Hold };
    bool raw=false,pressed=false,longSent=false;
    uint32_t changed=0,downAt=0;
    Event sample(bool value,uint32_t now) {
        if(value!=raw) { raw=value; changed=now; }
        if(raw!=pressed && uint32_t(now-changed)>=30) {
            pressed=raw;
            if(pressed) { downAt=now; longSent=false; return Down; }
            return Up;
        }
        if(pressed && !longSent && uint32_t(now-downAt)>=1500) {
            longSent=true; return Hold;
        }
        return None;
    }
};
}
