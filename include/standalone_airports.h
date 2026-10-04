#pragma once
#include "radar_model.h"
#include <cctype>
#include <cstdlib>
#include <cstdio>
namespace standalone {
struct Runway { const char *name; float lat1,lon1,lat2,lon2; };
struct Airport {
    const char *code,*icao,*name; float lat,lon; unsigned count; Runway runways[3];
};
struct CustomAirport { char code[9]{}; float lat=0,lon=0; };
struct CustomAirports { CustomAirport items[32]{}; unsigned count=0; };
inline bool parseAirports(const char *s,CustomAirports &out) {
    CustomAirports result;
    while(*s) {
        while(std::isspace(static_cast<unsigned char>(*s))) ++s;
        if(!*s) break;
        if(result.count==32) return false;
        CustomAirport a; unsigned n=0;
        while(std::isalnum(static_cast<unsigned char>(*s))) { if(n==8) return false; a.code[n++]=std::toupper(static_cast<unsigned char>(*s++)); }
        if(n<2) return false;
        while(*s==' ' || *s=='\t') ++s;
        if(*s!=':') return false;
        ++s; char *end; const double lon=strtod(s,&end); if(end==s) return false; s=end;
        while(*s==' ' || *s=='\t') ++s;
        if(*s!='/') return false;
        ++s; const double lat=strtod(s,&end); if(end==s) return false; s=end;
        if(!std::isfinite(lon)||!std::isfinite(lat)||std::abs(lon)>180||std::abs(lat)>90) return false;
        while(*s==' '||*s=='\t'||*s=='\r') ++s;
        if(*s && *s!='\n') return false;
        for(unsigned i=0;i<result.count;++i) if(!strcmp(a.code,result.items[i].code)) return false;
        a.lat=lat; a.lon=lon; result.items[result.count++]=a;
    }
    out=result; return true;
}
}
#include "standalone_airports_data.h"
namespace standalone {
struct NearbyAirport { Airport airport; float km; };
// Custom entries override matching built-in IATA/ICAO codes and are always offered.
inline unsigned nearby(double lat,double lon,const CustomAirports &custom,bool builtIn,
                       NearbyAirport *out,unsigned limit,float radius,const char *target="") {
    unsigned count=0;
    auto add=[&](const Airport &a) {
        const float km=sky::distance(sky::project(a.lat,a.lon,lat,lon));
        const bool pinned=*target && (!strcmp(target,a.code)||!strcmp(target,a.icao));
        if(!pinned && km>radius) return;
        unsigned at=0;
        while(at<count) {
            const bool otherPinned=*target && (!strcmp(target,out[at].airport.code)||!strcmp(target,out[at].airport.icao));
            if((pinned && !otherPinned)||(!otherPinned && km<out[at].km)) break;
            ++at;
        }
        if(at>=limit) return;
        if(count<limit) ++count;
        for(unsigned i=count-1;i>at;--i) out[i]=out[i-1];
        out[at]={a,km};
    };
    if(builtIn) for(const auto &a:builtInAirports) {
        bool overridden=false;
        for(unsigned i=0;i<custom.count;++i) if(!strcmp(custom.items[i].code,a.code)||!strcmp(custom.items[i].code,a.icao)) overridden=true;
        if(!overridden) add(a);
    }
    for(unsigned i=0;i<custom.count;++i) {
        const auto &c=custom.items[i]; add({c.code,c.code,"Custom airport",c.lat,c.lon,0,{}});
    }
    return count;
}
}
