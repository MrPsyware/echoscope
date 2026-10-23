#pragma once
#include "standalone_map_math.h"
#include <ArduinoJson.h>
#include <cctype>
#include <cstdio>
#include <cstring>
namespace rain {
constexpr unsigned maxFrames=13;
struct Frame { uint32_t time=0; char path[96]{}; };
struct Timeline {
    Frame frames[maxFrames]{}; unsigned count=0;
    bool parse(JsonVariantConst doc,uint32_t now) {
        count=0;
        if(std::strcmp(doc["host"] | "","https://tilecache.rainviewer.com")) return false;
        for(JsonObjectConst entry:doc["radar"]["past"].as<JsonArrayConst>()) {
            const uint32_t stamp=entry["time"] | 0u; const char *path=entry["path"] | "";
            if(!stamp || stamp>now+600 || uint64_t(stamp)+10800<now || (count && stamp<=frames[count-1].time)) continue;
            if(std::strncmp(path,"/v2/radar/",10) || !path[10] || std::strlen(path)>=sizeof(Frame::path)) continue;
            bool safe=true; for(const char *p=path+10;*p;++p) if(!std::isalnum(static_cast<unsigned char>(*p)) && *p!='-' && *p!='_') safe=false;
            if(!safe) continue;
            if(count==maxFrames) { for(unsigned i=1;i<count;++i) frames[i-1]=frames[i]; --count; }
            frames[count].time=stamp; std::snprintf(frames[count].path,sizeof(frames[count].path),"%s",path); ++count;
        }
        return count!=0;
    }
};
struct Pixel { uint8_t r,g,b,a; };
inline Pixel overlay(const uint8_t *rgba,uint8_t coverage,unsigned x,unsigned y) {
    if(coverage>127) return {0x71,0x87,0x82,uint8_t((x+y)%14<3?150:30)};
    return {rgba[0],rgba[1],rgba[2],uint8_t(unsigned(rgba[3])*210/255)};
}
inline int zoom(double lat,double km) { return std::min(7,standalone::mapZoom(lat,km)); }
inline bool stale(uint32_t frame,uint32_t now) { return !frame || frame>now+600 || uint64_t(frame)+2700<now; }
}
