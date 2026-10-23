#pragma once
// Inside namespace ui, after the common drawing/navigation helpers.
inline lv_img_dsc_t rainImage{};
inline bool rainReady=false;
inline int rainRange=-1,rainFrame=0,rainCount=0;
inline uint32_t rainTimes[13]{},rainShown=0;
inline char rainMessage[64]="Loading rain radar...";
inline void rainPage(int direction) { if(rainCount) rainFrame=std::max(0,std::min(rainCount-1,rainFrame+direction)); }
inline void renderRain() {
    const bool base=mapsEnabled && mapWanted && mapReady && mapRange==model.rangeIndex;
    if(base) { lv_draw_img_dsc_t d; lv_draw_img_dsc_init(&d); lv_canvas_draw_img(canvas,23,23,&mapImage,&d); }
    const bool shown=rainReady && rainRange==model.rangeIndex;
    if(shown) { lv_draw_img_dsc_t d; lv_draw_img_dsc_init(&d); lv_canvas_draw_img(canvas,23,23,&rainImage,&d); }
    itemRing(model.rangeIndex,sizeof(sky::ranges)/sizeof(sky::ranges[0]));
    circle(centre,centre,radius,grid); circle(centre,centre,105,grid);
    circle(centre,centre,4,white,1,true);
    text(25,"N",&lv_font_montserrat_14,muted);
    text(50,"RAIN RADAR",&lv_font_montserrat_22,white);
    char caption[96];
    if(shown) {
        const time_t stamp=rainShown; struct tm utc{}; gmtime_r(&stamp,&utc);
        const uint32_t now=time(nullptr);
        snprintf(caption,sizeof(caption),"%02d:%02d UTC / %s%s",utc.tm_hour,utc.tm_min,
                 rainCount && rainShown==rainTimes[rainCount-1]?"latest":"history",
                 rain::stale(rainCount?rainTimes[rainCount-1]:rainShown,now)?" / STALE":"");
        text(79,caption,&lv_font_montserrat_14,white);
    }
    if(shown && rainCount) {
        for(int i=0;i<rainCount;++i) if(rainTimes[i]==rainShown) {
            snprintf(caption,sizeof(caption),"Frame %d / %d",i+1,rainCount);
            text(300,caption,&lv_font_montserrat_12,muted);
        }
    }
    if(rainMessage[0]) text(110,rainMessage,&lv_font_montserrat_14,amber);
    pageArrows(rainCount>1);
    snprintf(caption,sizeof(caption),"%g km / turn: range",model.range());
    text(325,caption,&lv_font_montserrat_14,white);
    const uint32_t colors[]={0x88ddee,0x00a3e0,0x005588,0xffee00,0xffaa00,0xff4400};
    for(int i=0;i<6;++i) line(137+i*32,354,165+i*32,354,colors[i],8);
    text(364,"Light         Rain         Heavy",&lv_font_montserrat_12,white);
    text(383,"Hatched: no radar coverage",&lv_font_montserrat_12,muted);
    if(base) drawMapAttribution(400);
    text(416,"Weather data by RainViewer",&lv_font_montserrat_12,muted);
}
