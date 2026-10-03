#pragma once
// Included inside ui after InfoPage and navigation helpers.
inline bool approachMapReady=false;
inline char approachMapAirport[48]{};
inline lv_img_dsc_t approachMapImage{};
inline void renderApproach(const InfoPage &page,uint32_t now) {
    constexpr float km=20;
    constexpr int cx=centre,cy=centre,r=radius;
    const auto &airport=page.airport;
    auto local=[&](sky::Point p) { return sky::Point{p.east-airport.position.east,p.north-airport.position.north}; };
    auto pixel=[](sky::Point p) { return lv_point_t{lv_coord_t(cx+p.east/km*r),lv_coord_t(cy-p.north/km*r)}; };
    const bool drawMap=mapsEnabled && mapWanted && approachMapReady && !std::strcmp(approachMapAirport,page.item);
    if(drawMap) { lv_draw_img_dsc_t image; lv_draw_img_dsc_init(&image); lv_canvas_draw_img(canvas,23,23,&approachMapImage,&image); }
    for(int n=1;n<=4;++n) circle(cx,cy,r*n/4,grid);
    line(cx-r,cy,cx+r,cy,grid); line(cx,cy-r,cx,cy+r,grid);
    const float sweep=(now%8000)*2*sky::pi/8000;
    for(int n=0;n<9;++n) { const float a=sweep-n*.032f; line(cx,cy,cx+std::sin(a)*r,cy-std::cos(a)*r,green,1,20+n*3); }
    text(225,"W",&lv_font_montserrat_14,muted,70,26); text(225,"E",&lv_font_montserrat_14,muted,370,26);
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
            if(sky::clipToCircle(from,to,km)) { auto p=pixel(from),q=pixel(to);const bool selected=!std::strcmp(a.hex,model.selected); line(p.x,p.y,q.x,q.y,selected?sky::altitudeColor(trail->altitudes[j]):color,selected?2:1,selected?180:45); }
        }
    }
    // Symbols above every trail, matching the main radar's draw order.
    if(fresh) for(size_t i=0;i<model.data.count;++i) {
        const auto &a=model.data.aircraft[i]; const auto position=local(a.position);
        if(sky::age(a,now)>60 || sky::distance(position)>km || !sky::matches(a,model.filter,model.watches) || (model.altitudeFilter && sky::altitudeBand(a.altitude)!=model.altitudeFilter)) continue;
        drawAircraft(a,pixel(position),!std::strcmp(a.hex,model.selected),now);
    }
    char statusLine[64]; snprintf(statusLine,sizeof(statusLine),fresh?"%u aircraft / updated %lus ago":"Waiting for fresh aircraft",count,(unsigned long)(uint32_t(now-model.lastUpdate)/1000));
    text(40,page.title,&lv_font_montserrat_24,green);
    text(72,"APPROACH / 20 km / N UP",&lv_font_montserrat_14,muted);
    text(379,statusLine,&lv_font_montserrat_14,muted);
    const float homeDistance=sky::distance(airport.position);
    if(drawMap) drawMapAttribution(401);
    text(422,homeDistance>120?"Outside home feed coverage":homeDistance>80?"Partial home feed coverage":!airport.count?"Home feed / runways unavailable":"Home feed / filters apply",&lv_font_montserrat_12,muted);
    itemRing(itemIndex(),itemCount());pageArrows(true);
}
