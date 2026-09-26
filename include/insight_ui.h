#pragma once
// Included inside namespace ui after drawing helpers.
inline bool weatherEnabled=false,flightsEnabled=false,airportsEnabled=false;
inline bool infoMenu=false,infoNeedsFetch=false;
inline char routeNumber[16]{};
inline int infoSelection=0,infoView=0,infoPage=0; // 1 weather, 2 family, 3 airports, 4 route
inline char familyNumber[11]{};
struct InfoPage { char title[33]{}; char lines[7][61]{}; };
inline InfoPage infoPages[9]{};
inline unsigned infoCount=0;
inline uint32_t infoReceived=0,infoGenerated=0;
inline char infoSource[64]{},infoMessage[64]="Loading...";
inline bool hasRouteSelection() { const auto *a=model.selection(); return a && a->callsign[0]; }
inline bool infoAvailable() { return satellitesEnabled || weatherEnabled || (flightsEnabled && (familyNumber[0] || hasRouteSelection())) || airportsEnabled; }
inline bool infoOption(int option) {
    return option==0?satellitesEnabled:option==1?weatherEnabled:option==2?flightsEnabled && familyNumber[0]:option==3?airportsEnabled:flightsEnabled && hasRouteSelection();
}
inline void rotateInfo(int delta) {
    if(infoView) { if(infoCount) infoPage=(infoPage+delta%int(infoCount)+int(infoCount))%int(infoCount); return; }
    if(!infoAvailable()) return;
    int step=delta<0?-1:1;
    for(int n=0;n<std::abs(delta);++n) for(int i=0;i<5;++i) {
        infoSelection=(infoSelection+step+5)%5;
        if(infoOption(infoSelection)) break;
    }
}
inline void openInfo() {
    infoMenu=true; infoSelection=0;
    if(!infoOption(infoSelection)) rotateInfo(1);
}
inline void pressInfo() {
    if(infoView) { infoView=0; infoMenu=true; return; }
    if(!infoOption(infoSelection)) return;
    infoMenu=false;
    if(infoSelection==0) { satelliteView=true; return; }
    infoNeedsFetch=true;
    if(infoSelection==4) snprintf(routeNumber,sizeof(routeNumber),"%s",model.selection()->callsign);
    infoView=infoSelection; infoCount=0; infoPage=0; infoReceived=0;
    snprintf(infoMessage,sizeof(infoMessage),"Loading...");
}
inline void renderInfo(uint32_t now) {
    if(infoMenu) {
        text(65,"INFORMATION",&lv_font_montserrat_24,green);
        const char *labels[]={"Space stations","Weather / clouds","Family flight","Nearby airports","Selected flight route"};
        int row=0;
        for(int i=0;i<5;++i) if(infoOption(i)) {
            const int y=130+row++*44;
            text(y,labels[i],&lv_font_montserrat_20,i==infoSelection?green:muted);
            if(i==infoSelection) line(110,y+28,356,y+28,green,2);
        }
        text(381,"Turn + press to open",&lv_font_montserrat_14,muted);
        text(410,"Tap bottom to return",&lv_font_montserrat_14,muted);
        return;
    }
    const bool stale=infoCount && uint32_t(now-infoReceived)>(infoView==2?60000u:1800000u);
    if(!infoCount || stale) {
        text(80,"INFORMATION",&lv_font_montserrat_24,green);
        text(201,stale?"Data stale / updating":infoMessage,&lv_font_montserrat_18,muted);
    } else {
        if(infoPage>=int(infoCount)) infoPage=0;
        const auto &p=infoPages[infoPage];
        lv_point_t bounds;
        lv_txt_get_size(&bounds,p.title,&lv_font_montserrat_22,0,0,320,LV_TEXT_FLAG_NONE);
        text(65,p.title,bounds.y>48?&lv_font_montserrat_18:&lv_font_montserrat_22,green,73,320);
        for(int i=0;i<7;++i) {
            const lv_font_t *font=&lv_font_montserrat_16;
            lv_txt_get_size(&bounds,p.lines[i],font,0,0,360,LV_TEXT_FLAG_NONE);
            if(bounds.y>32) font=&lv_font_montserrat_14;
            lv_txt_get_size(&bounds,p.lines[i],font,0,0,360,LV_TEXT_FLAG_NONE);
            if(bounds.y>32) font=&lv_font_montserrat_12;
            text(118+i*35,p.lines[i],font,i==0?white:muted,53,360);
        }
        text(378,infoSource,&lv_font_montserrat_12,muted);
        char age[80];
        snprintf(age,sizeof(age),"Page %d/%u / data %lus old",infoPage+1,infoCount,(unsigned long)((now-infoReceived)/1000));
        text(400,age,&lv_font_montserrat_14,muted);
    }
    text(427,"Turn pages / press back",&lv_font_montserrat_12,muted);
}
inline void tapInfo(int y) {
    if(infoView) { pressInfo(); return; }
    if(y>=365) { infoMenu=false; return; }
    int row=0;
    for(int i=0;i<5;++i) if(infoOption(i)) {
        const int top=122+row++*44;
        if(y>=top && y<top+44) { infoSelection=i; pressInfo(); return; }
    }
}
