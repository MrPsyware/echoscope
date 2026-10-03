#pragma once
// Shared radar visuals, included inside ui after canvas drawing helpers.
constexpr uint32_t attributionColor=0x5B8C73;
inline void drawMapAttribution(int y=383) { text(y,mapCredit,&lv_font_montserrat_12,attributionColor,58,350); }
inline void drawAircraft(const sky::Aircraft &a,lv_point_t p,bool selected,uint32_t now) {
        const uint32_t color=selected?amber:sky::age(a,now)>20?muted:sky::altitudeColor(a.altitude);
        // Heading-oriented line silhouettes; a diamond means unknown class.
        const float heading=std::isfinite(a.track)?a.track*sky::pi/180:0;
        auto segment=[&](float x,float y,float x2,float y2) {
            auto px=[&](float xx,float yy){return p.x+int(xx*std::cos(heading)-yy*std::sin(heading));};
            auto py=[&](float xx,float yy){return p.y+int(xx*std::sin(heading)+yy*std::cos(heading));};
            line(px(x,y),py(x,y),px(x2,y2),py(x2,y2),color,2);
        };
        if(a.kind==sky::AircraftKind::Rotorcraft) {
            segment(0,-6,0,10); segment(-9,-3,9,-3); segment(-4,9,4,9);
            circle(p.x,p.y,3,color);
        } else if(a.kind!=sky::AircraftKind::Unknown) {
            const int span=a.kind==sky::AircraftKind::Large?11:7;
            segment(0,-10,0,10); segment(-span,3,0,-2); segment(0,-2,span,3);
            segment(-4,8,4,8);
        } else {
            segment(0,-7,5,0); segment(5,0,0,7); segment(0,7,-5,0); segment(-5,0,0,-7);
        }
        if(sky::watched(a,model.watches)) circle(p.x,p.y,11,alertStyle.color(sky::alertKind(a,model.watches)),2);
        if(a.military) text(p.y-19,"M",&lv_font_montserrat_14,color,p.x+9,16);
        if(selected) {
            circle(p.x,p.y,15,amber);
            const int tx=std::max(20,std::min(310,int(p.x)-65));
            const int ty=p.y>centre?p.y-34:p.y+18;
            text(ty,a.callsign[0]?a.callsign:a.hex,&lv_font_montserrat_14,amber,tx,136);
        }
}
