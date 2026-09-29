#pragma once
// Included inside ui after InfoPage and navigation helpers.
inline void renderApproach(const InfoPage &page,uint32_t now) {
    constexpr float km=10;
    constexpr int cx=233,cy=241,r=151;
    const auto &airport=page.airport;
    auto local=[&](sky::Point p) { return sky::Point{p.east-airport.position.east,p.north-airport.position.north}; };
    auto pixel=[](sky::Point p) { return lv_point_t{lv_coord_t(cx+p.east/km*r),lv_coord_t(cy-p.north/km*r)}; };
    text(40,page.title,&lv_font_montserrat_24,green);
    text(72,"APPROACH / 10 km / N UP",&lv_font_montserrat_14,muted);
    for(int n=1;n<=2;++n) circle(cx,cy,r*n/2,grid);
    line(cx-r,cy,cx+r,cy,grid); line(cx,cy-r,cx,cy+r,grid);
    // Runway endpoints are geographic data, not decorative runway headings.
    for(unsigned i=0;i<airport.count;++i) {
        auto from=airport.runways[i].from,to=airport.runways[i].to;
        if(!sky::clipToCircle(from,to,km)) continue;
        const auto a=pixel(from),b=pixel(to);line(a.x,a.y,b.x,b.y,muted,4);
        if(i==0) text(std::max(108,int(std::min(a.y,b.y))-20),airport.runways[i].name,&lv_font_montserrat_12,muted,std::min(a.x,b.x)-35,90);
    }
    if(!airport.count) { circle(cx,cy,4,muted); }
    const bool fresh=!model.demo && model.hasUpdate && uint32_t(now-model.lastUpdate)<=60000;
    unsigned count=0;
    if(fresh) for(size_t i=0;i<model.data.count;++i) {
        const auto &a=model.data.aircraft[i];
        if(sky::age(a,now)>60 || !sky::matches(a,model.filter,model.watches) || (model.altitudeFilter && sky::altitudeBand(a.altitude)!=model.altitudeFilter)) continue;
        const auto position=local(a.position);
        if(sky::distance(position)>km) continue;
        ++count;
        const uint32_t color=sky::age(a,now)>20?muted:sky::altitudeColor(a.altitude);
        if(const auto *trail=model.trailFor(a.hex)) for(size_t j=1;j<trail->count;++j) {
            auto from=local(trail->points[j-1]),to=local(trail->points[j]);
            if(sky::clipToCircle(from,to,km)) { auto p=pixel(from),q=pixel(to);line(p.x,p.y,q.x,q.y,color,1,55); }
        }
        const auto p=pixel(position);
        const float angle=std::isfinite(a.track)?a.track*sky::pi/180:0;
        auto wing=[&](float x,float y) { return lv_point_t{lv_coord_t(p.x+x*std::cos(angle)-y*std::sin(angle)),lv_coord_t(p.y+x*std::sin(angle)+y*std::cos(angle))}; };
        const auto tip=wing(0,-6),left=wing(-4,4),right=wing(4,4);
        line(tip.x,tip.y,left.x,left.y,color,2);line(tip.x,tip.y,right.x,right.y,color,2);
        if(sky::watched(a,model.watches)) circle(p.x,p.y,9,alertStyle.color(sky::alertKind(a,model.watches)));
    }
    char statusLine[64]; snprintf(statusLine,sizeof(statusLine),fresh?"%u aircraft / updated %lus ago":"Waiting for fresh aircraft",count,(unsigned long)(uint32_t(now-model.lastUpdate)/1000));
    text(397,statusLine,&lv_font_montserrat_14,muted);
    const float homeDistance=sky::distance(airport.position);
    text(417,homeDistance>110?"Outside home feed coverage":homeDistance>90?"Partial home feed coverage":!airport.count?"Home feed / runways unavailable":"Home feed / filters apply",&lv_font_montserrat_12,muted);
    itemRing(itemIndex(),itemCount());pageArrows(true);
}
