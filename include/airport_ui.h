#pragma once
// Included inside ui, after canvas drawing helpers.
struct AirportMarker { sky::Point position; char code[9]{}; unsigned size=2; };
inline AirportMarker airportMarkers[32]{};
inline unsigned airportCount=0,airportMode=1,airportSize=0,airportBrightness=35;
inline uint32_t airportColor=0x9BB8CD;
inline bool airportLabels=true,airportOverlayEnabled=false;
inline int airportRange=-1;
inline void renderAirports() {
    if(!airportOverlayEnabled || !airportMode || !airportBrightness || airportRange!=model.rangeIndex) return;
    const auto dim=[](uint32_t c,unsigned b) { return (((c>>16)*b/100)<<16) | ((((c>>8)&255)*b/100)<<8) | ((c&255)*b/100); };
    const uint32_t color=dim(airportColor,airportBrightness);
    lv_point_t icons[32]{}; unsigned iconCount=0;
    struct Box { int x,y,w,h; }; Box labels[32]{}; unsigned labelCount=0;
    for(unsigned i=0;i<airportCount;++i) {
        const auto &a=airportMarkers[i]; const auto p=screen(a.position);
        if(sky::distance(a.position)>model.range() || p.y<64 || p.y>388 || std::hypot(float(p.x-centre),float(p.y-centre))>radius-9) continue;
        bool crowded=false;
        for(unsigned j=0;j<iconCount;++j) if(std::abs(p.x-icons[j].x)<14 && std::abs(p.y-icons[j].y)<14) crowded=true;
        for(unsigned j=0;j<labelCount;++j) { const auto b=labels[j]; if(p.x>=b.x-7 && p.x<=b.x+b.w+7 && p.y>=b.y-7 && p.y<=b.y+b.h+7) crowded=true; }
        if(crowded) continue;
        icons[iconCount++]=p;
        const int r=a.size==0?4:3;
        line(p.x-r,p.y-r,p.x+r,p.y-r,color); line(p.x+r,p.y-r,p.x+r,p.y+r,color);
        line(p.x+r,p.y+r,p.x-r,p.y+r,color); line(p.x-r,p.y+r,p.x-r,p.y-r,color);
        line(p.x-1,p.y+2,p.x+1,p.y-2,color);
        if(!airportLabels || !a.code[0]) continue;
        lv_point_t bounds; lv_txt_get_size(&bounds,a.code,&lv_font_montserrat_12,0,0,100,LV_TEXT_FLAG_NONE);
        const Box box{p.x+7,p.y-6,bounds.x+2,14};
        if(box.x+box.w>443 || std::hypot(float(box.x+box.w-centre),float(box.y-centre))>radius-5) continue;
        for(unsigned j=0;j<labelCount;++j) { const auto b=labels[j]; if(box.x<b.x+b.w+4 && box.x+box.w+4>b.x && box.y<b.y+b.h+3 && box.y+box.h+3>b.y) crowded=true; }
        for(unsigned j=0;j+1<iconCount;++j) if(icons[j].x>=box.x-7 && icons[j].x<=box.x+box.w+7 && icons[j].y>=box.y-7 && icons[j].y<=box.y+box.h+7) crowded=true;
        // Do not put text over live aircraft, the home marker or cardinal labels.
        if(box.x<243 && box.x+box.w>223 && box.y<243 && box.y+box.h>223) crowded=true;
        if(box.y<249 && box.y+box.h>220 && (box.x<68 || box.x+box.w>399)) crowded=true;
        for(size_t j=0;j<model.data.count;++j) if(model.visible(model.data.aircraft[j],navigationNow)) {
            const auto plane=screen(model.data.aircraft[j].position);
            if(plane.x>=box.x-35 && plane.x<=box.x+box.w+35 && plane.y>=box.y-35 && plane.y<=box.y+box.h+12) crowded=true;
        }
        if(crowded) continue;
        labels[labelCount++]=box;
        text(box.y,a.code,&lv_font_montserrat_12,color,box.x,box.w);
    }
}
