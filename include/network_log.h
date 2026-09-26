#pragma once
#include <Arduino.h>
// Application diagnostics only; ROM, panic and early core logs remain on USB.
class NetworkLog : public Print {
    static constexpr size_t capacity=8192;
    char buffer[capacity]{};
    uint64_t sequence=0;
    portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
public:
    using Print::write;
    size_t write(uint8_t c) override { return write(&c,1); }
    size_t write(const uint8_t *data,size_t n) override {
        Serial.write(data,n);
        portENTER_CRITICAL(&mux);
        for(size_t i=0;i<n;++i) buffer[(sequence++)%capacity]=char(data[i]);
        portEXIT_CRITICAL(&mux);
        return n;
    }
    size_t read(uint64_t &cursor,char *out,size_t max,bool &lost) {
        portENTER_CRITICAL(&mux);
        const uint64_t first=sequence>capacity?sequence-capacity:0;
        lost=cursor<first || cursor>sequence;
        if(lost) cursor=first;
        size_t n=0;
        while(cursor<sequence && n<max) out[n++]=buffer[(cursor++)%capacity];
        portEXIT_CRITICAL(&mux);
        return n;
    }
};
inline NetworkLog deviceLog;
