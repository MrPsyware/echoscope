#pragma once
#include <atomic>
#include <initializer_list>
#include <cctype>
#include <cmath>
#include <cstdint>
namespace fun {
inline bool separator(char c) { return !c || c==',' || std::isspace(static_cast<unsigned char>(c)); }
// A literal two-word item; commas between DR and EVIL are not the secret.
inline const char *afterMarker(const char *p) {
    const char *q=p;
    for(char c: {'d','r'}) { if(std::tolower(static_cast<unsigned char>(*q))!=c) return p; ++q; }
    if(!std::isspace(static_cast<unsigned char>(*q))) return p;
    while(std::isspace(static_cast<unsigned char>(*q))) ++q;
    for(char c: {'e','v','i','l'}) { if(std::tolower(static_cast<unsigned char>(*q))!=c) return p; ++q; }
    return separator(*q)?q:p;
}
inline bool enabledBy(const char *p) {
    while(*p) {
        while(*p && separator(*p)) ++p;
        if(!*p) break;
        if(afterMarker(p)!=p) return true;
        while(*p && !separator(*p)) ++p;
    }
    return false;
}
struct Flyby {
    std::atomic<bool> enabled{false},visible{false},pending{false};
    // Animation/schedule fields belong solely to the UI thread.
    bool armed=false,active=false;
    uint32_t next=0,started=0;
    float lane=0; int direction=1;
    static constexpr uint32_t duration=20000;
    static uint32_t interval(uint32_t random) { return 3600000u+random%7200001u; }
    void tick(uint32_t now,bool onRadar,uint32_t random) {
        const bool allowed=enabled.load(); visible=allowed && onRadar;
        if(!allowed) { active=armed=false; pending=false; return; }
        if(!armed) { armed=true; next=now+interval(random); }
        const bool requested=pending.exchange(false);
        if(!onRadar) { active=false; if(int32_t(now-next)>=0) next=now+interval(random); return; }
        if(active && uint32_t(now-started)>=duration) active=false;
        if(requested || (!active && int32_t(now-next)>=0)) {
            started=now; active=true; next=now+duration+interval(random);
            direction=(random&1)?1:-1; lane=(int((random>>1)%61)-30)/100.0f;
        }
    }
    // No aircraft objects, alerts, selections or telemetry are created.
    template<class Segment> void draw(uint32_t now,Segment segment) const {
        if(!active || !enabled.load()) return;
        const float t=float(uint32_t(now-started))/duration;
        if(t>=1) return;
        const float cx=(-1.35f+2.7f*t)*direction,cy=lane-.22f*(t-.5f);
        auto edge=[&](float x,float y,float xx,float yy,unsigned level) {
            x=cx+x*direction; xx=cx+xx*direction; y+=cy; yy+=cy;
            // Clip every stroke to the radar circle, including trail and boosters.
            const float dx=xx-x,dy=yy-y,a=dx*dx+dy*dy;
            if(a<1e-10f) return;
            const float b=2*(x*dx+y*dy),c=x*x+y*y-1,disc=b*b-4*a*c;
            if(disc<0) return;
            float lo=std::fmax(0.f,(-b-std::sqrt(disc))/(2*a)),hi=std::fmin(1.f,(-b+std::sqrt(disc))/(2*a));
            if(lo<=hi) segment(x+lo*dx,y+lo*dy,x+hi*dx,y+hi*dy,level);
        };
        // Long rounded fuselage and the two unmistakable rounded aft boosters.
        constexpr float body[][2]={{-.13f,-.043f},{.16f,-.043f},{.185f,-.032f},{.2f,-.015f},{.205f,0},{.2f,.015f},{.185f,.032f},{.16f,.043f},{-.13f,.043f}};
        for(unsigned i=1;i<sizeof(body)/sizeof(body[0]);++i) edge(body[i-1][0],body[i-1][1],body[i][0],body[i][1],100);
        for(float side:{-1.f,1.f}) {
            float px=-.13f+.075f,py=side*.055f;
            for(int i=1;i<=24;++i) { const float angle=i*6.2831853f/24; float x=-.13f+.075f*std::cos(angle),y=side*.055f+.063f*std::sin(angle); edge(px,py,x,y,100); px=x;py=y; }
        }
        edge(.13f,-.04f,.13f,.04f,55); // nose seam
        for(int i=0;i<6;++i) edge(-.23f-i*.045f,0,-.25f-i*.045f,0,45-i*6);
    }
};
}
