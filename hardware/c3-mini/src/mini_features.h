#pragma once
#include "model.h"
#include <cctype>
#include <cstdlib>
namespace mini {
inline unsigned backlightDuty(unsigned percent){return 100-std::min(100u,percent);}
struct Airport {char code[9]{};float lon=0,lat=0;};
struct AirportList {Airport items[32]{};unsigned count=0;};
// Exact, case-insensitive comma/space-separated identifiers, never substring matches.
inline bool matches(const char *list,const char *value){
    if(!*value)return false;
    while(*list){
        while(*list==','||std::isspace(static_cast<unsigned char>(*list)))++list;
        const char *start=list;while(*list&&*list!=','&&!std::isspace(static_cast<unsigned char>(*list)))++list;
        size_t n=list-start;if(n!=strlen(value))continue;
        bool equal=n>0;for(size_t i=0;i<n;++i)if(std::toupper(static_cast<unsigned char>(start[i]))!=std::toupper(static_cast<unsigned char>(value[i])))equal=false;
        if(equal)return true;
    }return false;
}
inline bool validWatch(const char *s){
    if(strlen(s)>95)return false;
    for(;*s;++s)if(!std::isalnum(static_cast<unsigned char>(*s))&&*s!='-'&&*s!=','&&!std::isspace(static_cast<unsigned char>(*s)))return false;
    return true;
}
struct Watchlist {
    char types[96]{},registrations[96]{},calls[96]{};
    bool contains(const Aircraft &a)const{return matches(types,a.type)||matches(registrations,a.reg)||matches(calls,a.call);}
};
// Reject the entire edit on malformed lines, duplicate codes or overflow.
inline bool parseAirports(const char *s,AirportList &out){
    AirportList result;
    while(*s){
        while(std::isspace(static_cast<unsigned char>(*s)))++s;
        if(!*s)break;
        if(result.count==32)return false;
        Airport a;unsigned n=0;
        while(std::isalnum(static_cast<unsigned char>(*s))){if(n==8)return false;a.code[n++]=std::toupper(static_cast<unsigned char>(*s++));}
        if(n<2)return false;
        while(*s==' '||*s=='\t')++s;
        if(*s++!=':')return false;
        char *end;double lon=strtod(s,&end);if(end==s)return false;s=end;
        while(*s==' '||*s=='\t')++s;
        if(*s++!='/')return false;
        double lat=strtod(s,&end);if(end==s)return false;s=end;
        if(!std::isfinite(lon)||!std::isfinite(lat)||fabs(lon)>180||fabs(lat)>90)return false;
        while(*s==' '||*s=='\t'||*s=='\r')++s;
        if(*s&&*s!='\n')return false;
        for(unsigned i=0;i<result.count;++i)if(!strcmp(a.code,result.items[i].code))return false;
        a.lon=lon;a.lat=lat;result.items[result.count++]=a;
    }
    out=result;return true;
}
struct TrailPoint {float east=0,north=0,alt=NAN;};
struct Trail {char hex[9]{};TrailPoint points[32]{};unsigned count=0;uint32_t updated=0;};
struct Trails {
    Trail tracks[capacity]{};uint32_t received=0;
    void clear(){for(auto &t:tracks)t.count=0;received=0;}
    void update(const Snapshot &s){
        if(!s.received){clear();return;}if(s.received==received)return;received=s.received;
        for(auto &t:tracks){bool found=false;for(unsigned i=0;i<s.count;++i)if(!strcmp(t.hex,s.aircraft[i].hex))found=true;if(!found)t.count=0;}
        for(unsigned i=0;i<s.count;++i){const auto &a=s.aircraft[i];Trail *t=nullptr;
            for(auto &candidate:tracks)if(candidate.count&&!strcmp(candidate.hex,a.hex)){t=&candidate;break;}
            if(!t)for(auto &candidate:tracks)if(!candidate.count){t=&candidate;strcpy(t->hex,a.hex);break;}
            if(!t)continue;
            if(t->count&&uint32_t(s.received-t->updated)>30000)t->count=0;
            t->updated=s.received;
            if(t->count){const auto &p=t->points[t->count-1];if(std::hypot(p.east-a.east,p.north-a.north)<.02f)continue;}
            if(t->count==32){for(unsigned j=1;j<32;++j)t->points[j-1]=t->points[j];--t->count;}
            t->points[t->count++]={a.east,a.north,a.alt};
        }
    }
};
}
#include "airports_uk.h"
