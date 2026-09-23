#pragma once
#include <cstdint>
namespace sky {
struct NetworkPolicy {
    bool wasConnected=false;
    uint32_t disconnectedAt=0;
    bool fallback(bool configured,bool connected,uint32_t now) {
        if(connected || wasConnected) disconnectedAt=now;
        wasConnected=connected;
        return !connected && (!configured || uint32_t(now-disconnectedAt)>=30000);
    }
};
}
