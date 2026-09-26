#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "watchlist.h"
#include "alert_style.h"

namespace sky {
constexpr double pi = 3.14159265358979323846;
constexpr size_t maxAircraft = 64, trailLength = 192;
constexpr float ranges[] = {5, 10, 25, 50, 100};
struct Point { float east = 0, north = 0; };
inline Point project(double lat, double lon, double homeLat, double homeLon) {
    const double p = lat*pi/180, h = homeLat*pi/180;
    const double d = std::remainder((lon-homeLon)*pi/180, 2*pi);
    const double a = std::pow(std::sin((p-h)/2),2) + std::cos(h)*std::cos(p)*std::pow(std::sin(d/2),2);
    const double km = 6371.0088*2*std::atan2(std::sqrt(std::min(1.0,a)), std::sqrt(std::max(0.0,1-a)));
    const double bearing = std::atan2(std::sin(d)*std::cos(p), std::cos(h)*std::sin(p)-std::sin(h)*std::cos(p)*std::cos(d));
    return {float(km*std::sin(bearing)),float(km*std::cos(bearing))};
}
inline float distance(Point p) { return std::hypot(p.east,p.north); }
inline float bearing(Point p) { return std::fmod(float(std::atan2(p.east,p.north)*180/pi)+360,360); }
inline bool clipToCircle(Point &a,Point &b,float radius) {
    const Point d={b.east-a.east,b.north-a.north};
    const float aa=d.east*d.east+d.north*d.north;
    if(aa<1e-10f) return distance(a)<=radius;
    const float bb=2*(a.east*d.east+a.north*d.north);
    const float cc=a.east*a.east+a.north*a.north-radius*radius;
    const float discriminant=bb*bb-4*aa*cc;
    if(discriminant<0) return false;
    const float root=std::sqrt(discriminant);
    const float lo=std::max(0.0f,(-bb-root)/(2*aa));
    const float hi=std::min(1.0f,(-bb+root)/(2*aa));
    if(lo>hi) return false;
    const Point start=a;
    a={start.east+lo*d.east,start.north+lo*d.north};
    b={start.east+hi*d.east,start.north+hi*d.north}; return true;
}
enum class Filter { All, Military, Rotorcraft };
enum class AircraftKind { Unknown, Light, Large, Rotorcraft };
struct Aircraft {
    char hex[12]{}, callsign[16]{}, registration[16]{}, type[12]{}, description[80]{};
    AircraftKind kind=AircraftKind::Unknown;
    bool military=false;
    Point position;
    float altitude = NAN, speed = NAN, track = NAN, verticalRate = NAN, positionAge = 0;
    uint32_t received = 0;
};
inline bool matches(const Aircraft &a,Filter filter) { return filter==Filter::All || (filter==Filter::Military?a.military:a.kind==AircraftKind::Rotorcraft); }
inline int altitudeBand(float altitude) {
    return !std::isfinite(altitude)?5:altitude<5000?1:altitude<15000?2:altitude<30000?3:4;
}
inline const char *altitudeLabel(int band) {
    const char *names[]={"ALL ALT","<5k ft","5-15k ft","15-30k ft","30k+ ft","ALT UNKNOWN"};
    return names[std::max(0,std::min(5,band))];
}
inline uint32_t altitudeColor(float altitude) {
    const uint32_t colors[]={0x68F3AE,0x68F3AE,0x62D8F5,0x819FFF,0xD59AF5,0x63958E};
    return colors[altitudeBand(altitude)];
}
inline bool watched(const Aircraft &a,const Watches &w) {
    return (w.military && a.military) || (w.rotorcraft && a.kind==AircraftKind::Rotorcraft) ||
        w.types.matches(a.type,true) || w.registrations.matches(a.registration) || w.callsigns.matches(a.callsign);
}
inline AlertKind alertKind(const Aircraft &a,const Watches &w) {
    if(!watched(a,w)) return AlertKind::None;
    return a.military?AlertKind::Military:a.kind==AircraftKind::Rotorcraft?AlertKind::Helicopter:AlertKind::Watch;
}
struct Snapshot { std::array<Aircraft,maxAircraft> aircraft{}; size_t count = 0; };
inline float age(const Aircraft &a,uint32_t now) { return a.positionAge + uint32_t(now-a.received)/1000.0f; }
// Histories live separately from feed snapshots; the device allocates these in
// PSRAM, so longer trails do not enlarge the network task's stack or TLS heap.
struct Trail {
    char hex[12]{};
    std::array<Point,trailLength> points{};
    size_t count=0;
    void clear() { hex[0]=0; count=0; }
    void append(Point p) {
        if(count && distance({p.east-points[count-1].east,p.north-points[count-1].north})<0.01f) return;
        if(count>=2) {
            const Point start=points[count-2],mid=points[count-1];
            const Point segment={p.east-start.east,p.north-start.north};
            const float length=distance(segment);
            const float deviation=length>0?std::abs(segment.east*(mid.north-start.north)-segment.north*(mid.east-start.east))/length:1;
            const float forward=(p.east-mid.east)*(mid.east-start.east)+(p.north-mid.north)*(mid.north-start.north);
            if(length<2 && forward>=0 && deviation<0.02f) { points[count-1]=p; return; }
        }
        if(count==trailLength) {
            // Keep the start and sample the whole path more coarsely, rather
            // than dropping the start of a long encounter.
            for(size_t i=0;i<trailLength/2;++i) points[i]=points[i*2];
            count=trailLength/2;
        }
        points[count++]=p;
    }
};
struct Model {
    Snapshot data;
    Trail *trails=nullptr;
    char selected[12]{};
    int rangeIndex = 2;
    Filter filter=Filter::All;
    bool selectMode = false, altitudeMode=false;
    int altitudeFilter=0;
    Watches watches;
    bool details = false;
    bool demo = true;
    bool hasUpdate = false;
    uint32_t lastUpdate = 0;
    float range() const { return ranges[rangeIndex]; }
    void reset() {
        data.count=0; selected[0]=0; rangeIndex=2; filter=Filter::All; selectMode=false; altitudeMode=false; altitudeFilter=0;
        details=false; demo=true; hasUpdate=false; lastUpdate=0;
        if(trails) for(size_t i=0;i<maxAircraft;++i) trails[i].clear();
    }
    bool visible(const Aircraft &a,uint32_t now) const { return matches(a,filter) && (!altitudeFilter || altitudeBand(a.altitude)==altitudeFilter) && distance(a.position)<=range() && age(a,now)<=60; }
    AlertKind activeAlert(uint32_t now) const {
        AlertKind active=AlertKind::None;
        if(demo) return active;
        for(size_t i=0;i<data.count;++i) {
            const auto &a=data.aircraft[i];
            if(visible(a,now) && age(a,now)<=20) {
                const auto kind=alertKind(a,watches);
                if(int(kind)>int(active)) active=kind;
            }
        }
        return active;
    }
    bool watchAlert(uint32_t now) const { return activeAlert(now)!=AlertKind::None; }
    Aircraft *selection() {
        for(size_t i=0;i<data.count;++i) if(!std::strcmp(data.aircraft[i].hex,selected)) return &data.aircraft[i];
        return nullptr;
    }
    void select(size_t i) { if(i<data.count) std::strcpy(selected,data.aircraft[i].hex); }
    void normaliseSelection(uint32_t now) {
        if(details) return;
        auto *a=selection(); if(a && visible(*a,now)) return;
        selected[0]=0; float nearest=range()+1;
        for(size_t i=0;i<data.count;++i) if(visible(data.aircraft[i],now) && distance(data.aircraft[i].position)<nearest) {
            nearest=distance(data.aircraft[i].position); select(i);
        }
    }
    const Trail *trailFor(const char *hex) const {
        if(trails) for(size_t i=0;i<maxAircraft;++i) if(!std::strcmp(trails[i].hex,hex)) return &trails[i];
        return nullptr;
    }
    void updateTrails(uint32_t now) {
        if(!trails) return;
        for(size_t i=0;i<maxAircraft;++i) if(trails[i].hex[0]) {
            bool keep=false;
            for(size_t j=0;j<data.count;++j) if(!std::strcmp(trails[i].hex,data.aircraft[j].hex) && visible(data.aircraft[j],now)) { keep=true; break; }
            if(!keep) trails[i].clear();
        }
        for(size_t i=0;i<data.count;++i) if(visible(data.aircraft[i],now)) {
            Trail *t=nullptr;
            for(size_t j=0;j<maxAircraft;++j) if(!std::strcmp(trails[j].hex,data.aircraft[i].hex)) { t=&trails[j]; break; }
            if(!t) for(size_t j=0;j<maxAircraft;++j) if(!trails[j].hex[0]) { t=&trails[j]; std::strcpy(t->hex,data.aircraft[i].hex); break; }
            if(t) t->append(data.aircraft[i].position);
        }
    }
    void refresh(uint32_t now) { normaliseSelection(now); updateTrails(now); }
    void ingest(const Snapshot &next,uint32_t now) {
        // Break history when a previous position has aged out, even if the
        // same aircraft returns in this snapshot.
        if(trails) for(size_t i=0;i<data.count;++i) if(age(data.aircraft[i],now)>60) {
            for(size_t j=0;j<maxAircraft;++j) if(!std::strcmp(trails[j].hex,data.aircraft[i].hex)) trails[j].clear();
        }
        data=next; data.count=std::min(data.count,maxAircraft);
        hasUpdate=true; lastUpdate=now; refresh(now);
    }
    void rotate(int delta,uint32_t now) {
        if(!delta) return;
        if(!details && altitudeMode) { altitudeFilter=((altitudeFilter+delta)%6+6)%6; refresh(now); return; }
        if(!details && !selectMode) {
            rangeIndex=std::max(0,std::min(4,rangeIndex+delta)); refresh(now); return;
        }
        std::array<size_t,maxAircraft> choices{}; int count=0,idx=-1;
        for(size_t i=0;i<data.count;++i) if(visible(data.aircraft[i],now)) {
            if(!std::strcmp(data.aircraft[i].hex,selected)) idx=count;
            choices[count++]=i;
        }
        if(!count) { if(!details) selected[0]=0; return; }
        if(idx<0) idx=delta<0?count-1:0;
        else idx=((idx+delta)%count+count)%count;
        select(choices[idx]);
    }
    void cycleFilter(uint32_t now) { filter=Filter((int(filter)+1)%3); details=false; refresh(now); }
    void press(uint32_t now) {
        if(details) { details=false; refresh(now); return; }
        if(altitudeMode) altitudeMode=false;
        else if(selectMode) { selectMode=false; altitudeMode=true; }
        else selectMode=true;
        refresh(now);
    }
    void openSelected(uint32_t now) {
        normaliseSelection(now);
        auto *a=selection(); if(a && visible(*a,now)) details=true;
    }
    int hit(float east,float north,float tolerance,uint32_t now) const {
        int best=-1; float nearest=tolerance;
        for(size_t i=0;i<data.count;++i) {
            const auto &a=data.aircraft[i];
            if(!visible(a,now)) continue;
            float d=distance({a.position.east-east,a.position.north-north});
            if(d<nearest) { nearest=d; best=int(i); }
        }
        return best;
    }
};
} // namespace sky
