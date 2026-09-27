#pragma once
inline bool logReady=false,logMapReady=false;
inline uint32_t logEntry=0;
inline int logPage=0,logRange=25;
inline InfoPage logRoute{};
inline sky::Point logPoints[192]{};
inline bool logGaps[192]{};
inline unsigned logPointCount=0;
inline lv_img_dsc_t logMapImage{};
inline void resetLog(bool preservePage) { logReady=false; logMapReady=false; logEntry=0; if(!preservePage) logPage=0; logPointCount=0; }
inline void changeLogPage(int direction) { do { logPage=(logPage+direction+3)%3; } while(logPage==1 && !flightsEnabled); }
inline void renderLog(uint32_t now) {
    if(!infoCount) { text(75,"LOGBOOK",&lv_font_montserrat_24,green); text(210,infoMessage,&lv_font_montserrat_18,muted); return; }
    if(infoPage>=int(infoCount)) infoPage=0;
    const auto &p=infoPages[infoPage];
    const bool ready=logReady && logEntry==p.entryId;
    if(logPage==2 && ready) {
        if(logMapReady) { lv_draw_img_dsc_t image; lv_draw_img_dsc_init(&image); lv_canvas_draw_img(canvas,23,23,&logMapImage,&image); }
        for(int n=1;n<=4;++n) circle(233,233,210*n/4,grid);
        line(23,233,443,233,grid); line(233,23,233,443,grid);
        auto position=[](sky::Point p) { return lv_point_t{lv_coord_t(233+p.east/logRange*210),lv_coord_t(233-p.north/logRange*210)}; };
        for(unsigned i=1;i<logPointCount;++i) if(!logGaps[i]) { auto a=logPoints[i-1],b=logPoints[i]; if(sky::clipToCircle(a,b,logRange)) { const auto x=position(a),y=position(b); line(x.x,x.y,y.x,y.y,amber,2); } }
        if(logPointCount) { auto a=position(logPoints[0]),b=position(logPoints[logPointCount-1]); circle(a.x,a.y,4,green,1,true); circle(b.x,b.y,5,amber,2); }
        else text(220,"No saved track for this entry",&lv_font_montserrat_16,muted);
        text(58,p.title,&lv_font_montserrat_20,white); text(89,"OBSERVED TRACK / N UP",&lv_font_montserrat_14,green);
        text(357,"Green: start / orange: last seen",&lv_font_montserrat_12,white);
        text(382,logMapReady?mapCredit:"Radar grid / map unavailable",&lv_font_montserrat_12,white,65,336);
        char label[64]; snprintf(label,sizeof(label),"%d km / %u recorded points",logRange,logPointCount); text(407,label,&lv_font_montserrat_14,muted);
    } else if(logPage==1) {
        text(58,p.title,&lv_font_montserrat_24,white); text(98,"FLIGHT ROUTE",&lv_font_montserrat_18,green);
        if(!ready) text(214,"Loading saved route...",&lv_font_montserrat_18,muted);
        else for(int i=0;i<7;++i) text(142+i*33,logRoute.lines[i],&lv_font_montserrat_14,i==1?green:muted,73,320);
    } else if(logPage==2) {
        text(85,p.title,&lv_font_montserrat_24,white); text(215,"Loading saved track...",&lv_font_montserrat_18,muted);
    } else {
        text(51,p.title,&lv_font_montserrat_28,white); text(91,p.lines[0],&lv_font_montserrat_18,green);
        const bool photo=photosEnabled && photoReady && p.registration[0] && !strcmp(photoReg,p.registration);
        if(photo) {
            lv_draw_img_dsc_t image; lv_draw_img_dsc_init(&image); lv_canvas_draw_img(canvas,(size-photoImage.header.w)/2,125,&photoImage,&image);
            char credit[170]; snprintf(credit,sizeof(credit),"Copyright %s / Planespotters.net",photoCredit);
            const lv_font_t *font=&lv_font_montserrat_12; lv_point_t bounds;
            lv_txt_get_size(&bounds,credit,font,0,0,336,LV_TEXT_FLAG_NONE); if(bounds.y>36) font=&lv_font_montserrat_10;
            lv_txt_get_size(&bounds,credit,font,0,0,336,LV_TEXT_FLAG_NONE); if(bounds.y>36) font=&lv_font_montserrat_8;
            text(283,credit,font,muted,65,336);
            text(326,p.lines[1],&lv_font_montserrat_14,green);
            for(int i=2;i<5;++i) text(350+(i-2)*25,p.lines[i],&lv_font_montserrat_14,muted);
        } else for(int i=1;i<6;++i) text(159+(i-1)*39,p.lines[i],&lv_font_montserrat_16,muted,73,320);
        text(426,"Recorded sighting / UTC",&lv_font_montserrat_12,muted);
    }
    itemRing(itemIndex(),itemCount()); pageArrows(p.entryId!=0);
}
