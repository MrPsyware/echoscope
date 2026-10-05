#pragma once
#include <atomic>
#include <initializer_list>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace fun {
inline bool separator(char c) { return !c || c==',' || std::isspace(static_cast<unsigned char>(c)); }
enum class Kind : uint8_t { None=0, DrEvil=1, NyanCat=2, Santa=3, UFO=4 };
inline uint8_t kindMask(Kind kind) { return kind==Kind::None?0:uint8_t(1u<<(unsigned(kind)-1)); }
inline Kind fromHeader(const char *text) {
    if(!std::strcmp(text,"dr-evil")) return Kind::DrEvil;
    if(!std::strcmp(text,"nyan-cat")) return Kind::NyanCat;
    if(!std::strcmp(text,"santa")) return Kind::Santa;
    if(!std::strcmp(text,"ufo")) return Kind::UFO;
    return Kind::None;
}
inline const char *label(Kind kind) {
    return kind==Kind::NyanCat?"NYAN CAT":kind==Kind::Santa?"HO HO HO":"UNIDENTIFIED";
}
// Match whole watchlist items; multiword items require whitespace, not commas.
inline const char *afterPhrase(const char *p,const char *phrase) {
    const char *q=p;
    for(;*phrase;++phrase) {
        if(*phrase==' ') {
            if(!std::isspace(static_cast<unsigned char>(*q))) return p;
            while(std::isspace(static_cast<unsigned char>(*q))) ++q;
        } else {
            if(std::tolower(static_cast<unsigned char>(*q))!=*phrase) return p;
            ++q;
        }
    }
    return separator(*q)?q:p;
}
inline const char *afterMarker(const char *p,Kind *kind=nullptr) {
    const char *phrases[]={"dr evil","nyan cat","santa","ufo"};
    for(unsigned i=0;i<4;++i) {
        const char *end=afterPhrase(p,phrases[i]);
        if(end!=p) { if(kind) *kind=Kind(i+1); return end; }
    }
    if(kind) *kind=Kind::None;
    return p;
}
inline uint8_t enabledBy(const char *p) {
    uint8_t mask=0;
    while(*p) {
        while(*p && separator(*p)) ++p;
        if(!*p) break;
        Kind kind; const char *end=afterMarker(p,&kind);
        if(end!=p) { mask|=kindMask(kind); p=end; }
        else while(*p && !separator(*p)) ++p;
    }
    return mask;
}
inline Kind choose(uint8_t mask,uint32_t random) {
    unsigned count=0; for(unsigned i=0;i<4;++i) if(mask&(1<<i)) ++count;
    if(!count) return Kind::None;
    unsigned target=random%count;
    for(unsigned i=0;i<4;++i) if(mask&(1<<i)) { if(!target--) return Kind(i+1); }
    return Kind::None;
}
struct Flyby {
    std::atomic<uint8_t> automaticMask{0},pending{0};
    std::atomic<bool> visible{false};
    Kind kind=Kind::DrEvil;
    // Animation/schedule fields belong solely to the UI thread.
    bool armed=false,active=false,manual=false;
    uint32_t next=0,started=0;
    float lane=0; int direction=1;
    static constexpr uint32_t duration=20000;
    static uint32_t interval(uint32_t random) { return 3600000u+random%7200001u; }
    void tick(uint32_t now,bool onRadar,uint32_t random) {
        const uint8_t mask=automaticMask.load()&15; const bool automatic=mask!=0; visible=onRadar;
        const uint8_t request=pending.exchange(0); const bool requested=request>=1 && request<=4;
        if(!manual && !(mask&kindMask(kind))) active=false;
        if(!automatic) { armed=false; if(!manual) active=false; }
        else if(!armed) { armed=true; next=now+interval(random); }
        if(!onRadar) { active=manual=false; if(automatic && int32_t(now-next)>=0) next=now+interval(random); return; }
        if(active && uint32_t(now-started)>=duration) active=false;
        if(requested || (automatic && !active && int32_t(now-next)>=0)) {
            started=now; active=true; manual=requested; kind=requested?Kind(request):choose(mask,random>>8); next=now+duration+interval(random);
            direction=(random&1)?1:-1; lane=(int((random>>1)%61)-30)/100.0f;
        }
    }
    // No aircraft objects, alerts, selections or telemetry are created.
    template<class Segment> void draw(uint32_t now,Segment segment) const {
        if(!active) return;
        const float t=float(uint32_t(now-started))/duration;
        if(t>=1) return;
        float travel=-1.35f+2.7f*t;
        if(kind==Kind::UFO) {
            // Cruise to centre, hover with a gentle wobble, then accelerate away.
            if(t<.35f) travel=-1.35f+1.35f*t/.35f;
            else if(t<.68f) travel=.018f*std::sin(t*90);
            else { const float exit=(t-.68f)/.32f; travel=1.35f*exit*exit; }
        }
        const float cx=travel*direction;
        const float bob=kind==Kind::UFO?.022f*std::sin(t*70):kind==Kind::NyanCat?.012f*std::sin(t*100):0;
        const float cy=lane-.22f*(t-.5f)+bob;
        auto stroke=[&](float x,float y,float xx,float yy,uint32_t color,unsigned level=100) {
            x=cx+x*direction; xx=cx+xx*direction; y+=cy; yy+=cy;
            // Clip every stroke to the radar circle, including trail and boosters.
            if(x*x+y*y<=1 && xx*xx+yy*yy<=1) { segment(x,y,xx,yy,color,level); return; }
            const float dx=xx-x,dy=yy-y,a=dx*dx+dy*dy;
            if(a<1e-10f) return;
            const float b=2*(x*dx+y*dy),c=x*x+y*y-1,disc=b*b-4*a*c;
            if(disc<0) return;
            float lo=std::fmax(0.f,(-b-std::sqrt(disc))/(2*a)),hi=std::fmin(1.f,(-b+std::sqrt(disc))/(2*a));
            if(lo<=hi) segment(x+lo*dx,y+lo*dy,x+hi*dx,y+hi*dy,color,level);
        };
        auto edge=[&](float x,float y,float xx,float yy,unsigned level) { stroke(x,y,xx,yy,0x68F3AE,level); };
        auto box=[&](float x,float y,float w,float h,uint32_t color,unsigned level=100) {
            for(float row=y;row<y+h;row+=.008f) stroke(x,row,x+w,row,color,level);
        };
        auto ellipse=[&](float x,float y,float rx,float ry,uint32_t color,unsigned level=100) {
            float px=x+rx,py=y;
            for(int i=1;i<=24;++i) { const float a=i*6.2831853f/24; float xx=x+rx*std::cos(a),yy=y+ry*std::sin(a); stroke(px,py,xx,yy,color,level); px=xx; py=yy; }
        };
        if(kind==Kind::NyanCat) {
            // Six trailing rainbow bands, animated blocky grey cat and pink toast.
            constexpr uint32_t rainbow[]={0xFF5454,0xFFAD45,0xFFE960,0x79E85F,0x5CCBFF,0xBB78F0};
            for(unsigned band=0;band<6;++band) for(unsigned j=0;j<8;++j) {
                const float wave=((j+unsigned(now/180))%2)*.008f;
                box(-.16f-(j+1)*.047f,-.065f+band*.022f+wave,.05f,.021f,rainbow[band],80-j*8);
            }
            constexpr uint32_t grey=0xADBAC8,dark=0x192630,pink=0xEF92BC,toast=0xEAC98E;
            const float feet=(now/160)%2?.012f:-.012f;
            box(-.15f-feet,.068f,.045f,.026f,grey); box(.025f+feet,.068f,.043f,.026f,grey);
            box(-.19f,-.01f,.05f,.024f,grey); box(-.207f,-.038f,.025f,.04f,grey);
            box(-.155f,-.071f,.255f,.142f,toast); box(-.132f,-.053f,.21f,.105f,pink);
            for(unsigned i=0;i<5;++i) box(-.11f+(i%3)*.056f,-.032f+(i/3)*.056f,.012f,.015f,0xCA4B87);
            box(.045f,-.045f,.16f,.106f,grey);
            box(.045f,-.082f,.034f,.052f,grey); box(.162f,-.082f,.043f,.052f,grey);
            box(.056f,-.068f,.012f,.024f,pink); box(.177f,-.068f,.013f,.024f,pink);
            box(.085f,-.011f,.014f,.016f,dark); box(.166f,-.011f,.014f,.016f,dark);
            stroke(.108f,.028f,.151f,.028f,dark); box(.058f,.016f,.021f,.015f,pink); box(.179f,.016f,.02f,.015f,pink);
            return;
        }
        if(kind==Kind::Santa) {
            constexpr uint32_t red=0xEE5454,gold=0xEACA72,white=0xF2F6DF,deer=0xB6BC89;
            const float gait=.02f*std::sin(now*.016f);
            // Sleigh, curved runners, Santa's hat/beard and gift sack.
            box(-.24f,.012f,.19f,.07f,red); stroke(-.24f,.012f,-.26f,-.04f,gold);
            stroke(-.27f,.107f,-.045f,.107f,gold); stroke(-.045f,.107f,-.018f,.074f,gold);
            stroke(-.2f,.077f,-.19f,.107f,gold); stroke(-.08f,.077f,-.075f,.107f,gold);
            ellipse(-.21f,-.015f,.049f,.054f,0xA5AF71);
            box(-.14f,-.052f,.056f,.064f,red); box(-.125f,-.09f,.041f,.04f,0xF4C69C);
            box(-.127f,-.068f,.04f,.025f,white); stroke(-.14f,-.096f,-.077f,-.096f,white);
            stroke(-.133f,-.105f,-.1f,-.137f,red); stroke(-.1f,-.137f,-.077f,-.11f,red);
            ellipse(-.075f,-.106f,.01f,.01f,white);
            stroke(-.05f,.03f,.23f,.015f,gold,55);
            for(float x:{.035f,.225f}) {
                ellipse(x,0,.052f,.026f,deer); stroke(x+.041f,-.012f,x+.058f,-.06f,deer);
                stroke(x+.058f,-.06f,x+.09f,-.055f,deer); // head
                stroke(x+.061f,-.062f,x+.05f,-.105f,deer); stroke(x+.053f,-.094f,x+.028f,-.103f,deer);
                stroke(x+.057f,-.079f,x+.083f,-.099f,deer);
                stroke(x-.028f,.016f,x-.039f-gait,.062f,deer); stroke(x+.026f,.016f,x+.04f+gait,.062f,deer);
            }
            ellipse(.32f,-.052f,.009f,.009f,red); // leading red nose
            return;
        }
        if(kind==Kind::UFO) {
            constexpr uint32_t silver=0xB6D7DB,cyan=0x63EAD1;
            if(t>.38f && t<.65f) {
                stroke(-.065f,.046f,-.14f,.23f,cyan,25); stroke(.065f,.046f,.14f,.23f,cyan,25);
                stroke(-.14f,.23f,.14f,.23f,cyan,16);
            }
            ellipse(0,0,.18f,.05f,silver); ellipse(0,-.033f,.084f,.061f,cyan);
            stroke(-.17f,0,.17f,0,silver);
            for(int i=-2;i<=2;++i) ellipse(i*.061f,.022f,.01f,.008f,((unsigned(now/140)+i+2)%3)==0?0xFFF29C:cyan);
            return;
        }
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
