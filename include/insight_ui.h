#pragma once
// Included inside namespace ui after drawing helpers.
inline bool stargazingEnabled=false,highlightsEnabled=false;
inline bool pickupArmed=false; inline uint32_t pickupStarted=0;
inline char notification[121]{}; inline uint32_t notificationUntil=0,notificationCountdown=0;
inline bool familyEnabled=false;
inline bool weatherEnabled=false,flightsEnabled=false,airportsEnabled=false;
inline bool infoMenu=false,infoNeedsFetch=false;
inline int infoSelection=0,infoView=0,infoPage=0; // 1 weather, 2 family, 3 airports, 4 stargazing, 5 highlights
inline char familyNumber[11]{};
struct WeatherCard { char label[20]{},condition[24]{},temperature[24]{},cloud[24]{},rain[24]{},wind[24]{}; int icon=7; bool night=false; };
struct AirportRunway { sky::Point from,to; char name[12]{}; };
struct AirportDetail { bool valid=false; double lat=0,lon=0; sky::Point position; AirportRunway runways[3]{}; unsigned count=0; };
inline int airportPage=0;
inline char airportTarget[9]{};
struct InfoPage { AirportDetail airport{}; char item[48]{}; uint32_t entryId=0; char registration[16]{}; char title[33]{}; char lines[7][61]{}; char subtitle[48]{},note[64]{}; WeatherCard cards[3]{}; unsigned cardCount=0; };
inline InfoPage infoPages[9]{};
inline unsigned infoCount=0;
inline uint32_t infoReceived=0,infoGenerated=0;
inline char infoSource[64]{},infoMessage[64]="Loading...";
inline bool infoAvailable() { return stargazingEnabled || highlightsEnabled || satellitesEnabled || weatherEnabled || (familyEnabled && (familyNumber[0])) || airportsEnabled; }
inline bool infoOption(int option) {
    return option==0?satellitesEnabled:option==1?weatherEnabled:option==2?familyEnabled && familyNumber[0]:option==3?airportsEnabled:option==4?stargazingEnabled:option==5?highlightsEnabled:false;
}
inline void resetLog(bool preservePage=false);
inline constexpr int infoOrder[]={5,2,3,1,4,0};
inline void changeLogPage(int direction);
inline void renderLog(uint32_t now);
inline bool sameItem(int a,int b) { return a==b || (infoPages[a].item[0] && !std::strcmp(infoPages[a].item,infoPages[b].item)); }
inline int itemFirst(int page) { for(int i=0;i<page;++i) if(sameItem(i,page)) return i; return page; }
inline int itemIndex() { int n=0; const int first=itemFirst(infoPage); for(int i=0;i<first;++i) if(itemFirst(i)==i) ++n; return n; }
inline int itemCount() { int n=0; for(int i=0;i<int(infoCount);++i) if(itemFirst(i)==i) ++n; return n; }
inline int subpageCount() { int n=0; for(int i=0;i<int(infoCount);++i) if(sameItem(i,infoPage)) ++n; return n; }
inline void changeInfoPage(int direction) {
    if(!infoCount) return;
    for(int step=1;step<=int(infoCount);++step) {
        const int candidate=(infoPage+direction*step+int(infoCount)*2)%int(infoCount);
        if(sameItem(candidate,infoPage)) { infoPage=candidate; return; }
    }
}
inline void turnInfoPage(int direction) {
    if(infoView==3 && infoCount && infoPages[infoPage].airport.valid) airportPage=(airportPage+direction%2+2)%2;
    else if(infoView==5) changeLogPage(direction<0?-1:1);
    else changeInfoPage(direction<0?-1:1);
}
inline void rotateInfo(int delta) {
    if(infoView) {
        if(!infoCount) return;
        int starts[9]{},count=0; for(int i=0;i<int(infoCount);++i) if(itemFirst(i)==i) starts[count++]=i;
        const int target=(itemIndex()-delta%count+count)%count;
        if(starts[target]!=itemFirst(infoPage)) {
            int sub=0; for(int i=0;i<infoPage;++i) if(sameItem(i,infoPage)) ++sub;
            const int first=starts[target]; infoPage=first;
            for(int i=first+1;i<int(infoCount) && sub>0;++i) if(sameItem(first,i)) { infoPage=i; --sub; }
            resetLog(true);
        }
        return;
    }
    if(!infoAvailable()) return;
    int step=delta<0?1:-1;
    for(int n=0;n<std::abs(delta);++n) for(int i=0;i<6;++i) {
        int index=0; while(infoOrder[index]!=infoSelection) ++index;
        infoSelection=infoOrder[(index+step+6)%6];
        if(infoOption(infoSelection)) break;
    }
}
inline void ensureInfoSelection() {
    if(infoOption(infoSelection)) return;
    for(int i:infoOrder) if(infoOption(i)) { infoSelection=i; return; }
}
inline void openInfo() {
    infoView=0; satelliteView=false; model.details=false; infoMenu=true; infoSelection=5; ensureInfoSelection();
}
inline void pressInfo() {
    if(infoView) { infoView=0; infoMenu=false; model.details=false; return; }
    if(!infoOption(infoSelection)) return;
    infoMenu=false;
    if(infoSelection==0) { satelliteView=true; return; }
    infoNeedsFetch=true;
    airportTarget[0]=0; airportPage=0; resetLog(); infoView=infoSelection; infoCount=0; infoPage=0; infoReceived=0;
    snprintf(infoMessage,sizeof(infoMessage),"Loading...");
}
#include "weather_ui.h"
#include "approach_ui.h"
inline void renderInfo(uint32_t now) {
    if(infoMenu) {
        text(65,"INFORMATION",&lv_font_montserrat_24,green);
        const char *labels[]={"Space stations","Weather / clouds","Family flight","Nearby airports","Stargazing tonight","Logbook"};
        int selected=0,total=0; for(int i:infoOrder) if(infoOption(i)) { if(i==infoSelection) selected=total; ++total; } itemRing(selected,total);
        int row=0;
        for(int i:infoOrder) if(infoOption(i)) {
            const int y=118+row++*41;
            text(y,labels[i],&lv_font_montserrat_20,i==infoSelection?green:muted);
            if(i==infoSelection) line(110,y+28,356,y+28,green,2);
        }
        text(381,"Turn + press to open",&lv_font_montserrat_14,muted);
        text(410,"Tap bottom: radar",&lv_font_montserrat_14,muted);
        return;
    }
    if(infoView==5) { renderLog(now); return; }
    const bool stale=infoCount && uint32_t(now-infoReceived)>(infoView==2?60000u:1800000u);
    if(!infoCount || stale) {
        text(80,"INFORMATION",&lv_font_montserrat_24,green);
        text(201,stale?"Data stale / updating":infoMessage,&lv_font_montserrat_18,muted);
    } else {
        if(infoPage>=int(infoCount)) infoPage=0;
        const auto &p=infoPages[infoPage];
        if(infoView==3 && p.airport.valid && airportPage==1) { renderApproach(p,now); return; }
        itemRing(itemIndex(),itemCount()); pageArrows(subpageCount()>1 || (infoView==3 && p.airport.valid));
        if(p.cardCount) { renderWeather(p); return; }
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
        snprintf(age,sizeof(age),"Item %d/%d / data %lus old",itemIndex()+1,itemCount(),(unsigned long)((now-infoReceived)/1000));
        text(400,age,&lv_font_montserrat_14,muted);
    }
    text(427,"Turn: items / centre: radar",&lv_font_montserrat_12,muted);
}
inline void tapInfo(int x,int y) {
    if(infoView) {
        if(pageSide(x,y)) {
            if(infoView==3 && infoCount && infoPages[infoPage].airport.valid) airportPage=1-airportPage;
            else if(infoView==5) changeLogPage(pageSide(x,y)); else changeInfoPage(pageSide(x,y));
            return;
        }
        pressInfo(); return;
    }
    if(y>=365) { infoMenu=false; return; }
    int row=0;
    for(int i:infoOrder) if(infoOption(i)) {
        const int top=110+row++*41;
        if(y>=top && y<top+41) { infoSelection=i; pressInfo(); return; }
    }
}
