#pragma once
#include <ArduinoJson.h>
#include <cstdio>
#include "radar_model.h"
namespace sky {
inline float number(JsonVariantConst v) { return v.is<float>() ? v.as<float>() : NAN; }
template<size_t N> void copyText(char (&dest)[N],JsonVariantConst v) {
    const char *s=v.is<const char*>() ? v.as<const char*>() : "";
    std::snprintf(dest,N,"%s",s);
    size_t n=std::strlen(dest); while(n && dest[n-1]==' ') dest[--n]=0;
}
inline bool parseAircraft(JsonDocument &doc,Snapshot &out,double lat,double lon,uint32_t now) {
    JsonArrayConst array=doc["ac"].as<JsonArrayConst>();
    if(array.isNull()) return false;
    out.count=0;
    for(JsonObjectConst o:array) {
        const float alat=number(o["lat"]), alon=number(o["lon"]), seen=number(o["seen_pos"]);
        if(!std::isfinite(alat)||!std::isfinite(alon)||std::abs(alat)>90||std::abs(alon)>180||!std::isfinite(seen)||seen<0||seen>60) continue;
        if(o["alt_baro"].is<const char*>() && !std::strcmp(o["alt_baro"],"ground")) continue;
        Aircraft a; copyText(a.hex,o["hex"]); if(!a.hex[0]) continue;
        copyText(a.callsign,o["flight"]); copyText(a.registration,o["r"]); copyText(a.type,o["t"]);
        a.position=project(alat,alon,lat,lon); a.positionAge=seen; a.received=now;
        a.altitude=number(o["alt_baro"]); if(!std::isfinite(a.altitude)) a.altitude=number(o["alt_geom"]);
        a.speed=number(o["gs"]); a.track=number(o["track"]); a.verticalRate=number(o["baro_rate"]);
        if(distance(a.position)>100) continue;
        size_t at=out.count;
        if(at==maxAircraft) {
            at=0; for(size_t i=1;i<out.count;++i) if(distance(out.aircraft[i].position)>distance(out.aircraft[at].position)) at=i;
            if(distance(a.position)>=distance(out.aircraft[at].position)) continue;
        } else ++out.count;
        out.aircraft[at]=a;
    }
    std::sort(out.aircraft.begin(),out.aircraft.begin()+out.count,[](const Aircraft &a,const Aircraft &b){return std::strcmp(a.hex,b.hex)<0;});
    return true;
}
}
