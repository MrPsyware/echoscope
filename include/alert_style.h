#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
namespace sky {
enum class AlertKind { None, Watch, Helicopter, Military };
enum class AlertEffect { Off, Steady, Pulse, Flash };
struct AlertStyle {
    uint32_t watch=0x68F3AE, helicopter=0x62D8F5, military=0xD59AF5;
    unsigned brightness=30, width=3, periodSeconds=4;
    AlertEffect effect=AlertEffect::Pulse;
    uint32_t color(AlertKind kind) const {
        return kind==AlertKind::Military?military:kind==AlertKind::Helicopter?helicopter:watch;
    }
    uint8_t opacity(uint32_t now) const {
        if(effect==AlertEffect::Off || !brightness) return 0;
        const unsigned period=periodSeconds*1000;
        const float phase=period?float(now%period)/period:0;
        float strength=1;
        if(effect==AlertEffect::Pulse) strength=0.1f+0.9f*(0.5f-0.5f*std::cos(phase*6.28318530718f));
        if(effect==AlertEffect::Flash) strength=phase<0.5f?1:0;
        return uint8_t(std::round(255*brightness/100.0f*strength));
    }
};
inline bool parseAlertColor(const char *text,uint32_t &out) {
    if(std::strlen(text)!=7 || text[0]!='#') return false;
    uint32_t value=0;
    for(int i=1;i<7;++i) {
        const char c=text[i];
        const int n=c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:-1;
        if(n<0) return false;
        value=(value<<4)|unsigned(n);
    }
    out=value; return true;
}
}
