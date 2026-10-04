#pragma once
#include "airport_hit.h"
#include "radar_selection.h"
#include <lvgl.h>
#include <cstdio>
#include <atomic>
#include <ctime>
#include "info_protocol.h"
#include "radar_model.h"
#include "input_gate.h"
#include "activity.h"
#include "detail_gesture.h"
namespace ui {
constexpr int size=466, centre=233, radius=210;
constexpr uint32_t green=0x68F3AE, muted=0x63958E, grid=0x143B36, white=0xE8F8F4, amber=0xF2BB70;
inline lv_obj_t *canvas;
inline sky::Model model;
inline uint32_t navigationNow=0;
inline sky::InputGate input;
inline sky::Activity activity;
inline sky::RadarControls radarControls;
inline sky::AlertStyle alertStyle;
inline bool photosEnabled=false,photoReady=false;
inline char photoReg[16]{},photoCredit[128]{},photoLink[256]{},photoStatus[48]="Loading photo...";
inline lv_img_dsc_t photoImage{},mapImage{};
inline bool mapsEnabled=false,mapReady=false,mapWanted=true,satellitesEnabled=false,satelliteView=false;
inline int mapRange=-1,stationIndex=0;
inline char mapCredit[96]="Copyright OpenStreetMap contributors";
inline sky::Station stations[8]{};
inline unsigned stationCount=0;
inline uint32_t stationReceived=0;
inline void rotateStations(int delta) { if(stationCount) stationIndex=((stationIndex-delta)%int(stationCount)+int(stationCount))%int(stationCount); }

inline std::atomic<bool> asleep{false};
inline bool settings=false,setupOpeningTouch=false,setupConnected=false;
inline char setupSSID[33]{};
inline std::atomic<bool> requestFeed{false};
inline char status[80]="Starting", setupPassword[20]="", setupAddress[24]="192.168.4.1";
inline void line(int x,int y,int x2,int y2,uint32_t color,int width=1,lv_opa_t opacity=LV_OPA_COVER) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d); d.color=lv_color_hex(color); d.width=width; d.opa=opacity;
    lv_point_t p[]={{lv_coord_t(x),lv_coord_t(y)},{lv_coord_t(x2),lv_coord_t(y2)}};
    lv_canvas_draw_line(canvas,p,2,&d);
}
inline void circle(int x,int y,int r,uint32_t color,int width=1,bool fill=false,lv_opa_t opacity=LV_OPA_COVER) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d); d.radius=LV_RADIUS_CIRCLE;
    d.bg_opa=fill?LV_OPA_COVER:LV_OPA_TRANSP; d.bg_color=lv_color_hex(color);
    d.border_color=lv_color_hex(color); d.border_width=width; d.border_opa=opacity;
    lv_canvas_draw_rect(canvas,x-r,y-r,r*2+1,r*2+1,&d);
}
inline void text(int y,const char *s,const lv_font_t *font=&lv_font_montserrat_18,uint32_t color=white,int x=48,int width=370) {
    lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d); d.color=lv_color_hex(color); d.font=font; d.align=LV_TEXT_ALIGN_CENTER;
    lv_canvas_draw_text(canvas,x,y,width,&d,s);
}
inline lv_point_t screen(sky::Point p) {
    return {lv_coord_t(centre+p.east/model.range()*radius),lv_coord_t(centre-p.north/model.range()*radius)};
}
#include "radar_visuals.h"
#include "airport_ui.h"
#include "navigation.h"
#include "insight_ui.h"
#include "logbook_ui.h"
inline int selectedAirportIndex() {
    if(!airportsEnabled || !airportOverlayEnabled || !airportMode || !airportBrightness || airportRange!=model.rangeIndex || !selectedAirport[0])return -1;
    for(unsigned i=0;i<airportHitCount;++i){int n=airportHits[i].index;if(n>=0 && unsigned(n)<airportCount && !std::strcmp(airportMarkers[n].code,selectedAirport))return n;}
    return -1;
}
inline void openAirport(int index) {
    if(index<0 || unsigned(index)>=airportCount || !airportsEnabled)return;
    infoSelection=3;pressInfo();
    snprintf(airportTarget,sizeof(airportTarget),"%s",airportMarkers[index].code);
    snprintf(selectedAirport,sizeof(selectedAirport),"%s",airportMarkers[index].code);
}
inline void openRadarSelection(uint32_t now) {
    const int airport=selectedAirportIndex();
    if(airport>=0)openAirport(airport);
    else { selectedAirport[0]=0;model.openSelected(now); }
}
inline void rotateRadarSelection(int delta,uint32_t now) {
    sky::RadarChoice choices[sky::maxAircraft+32]{};int count=0;
    for(unsigned i=0;i<model.data.count;++i)if(model.visible(model.data.aircraft[i],now))choices[count++]={model.data.aircraft[i].hex,i,false};
    if(airportsEnabled && airportOverlayEnabled && airportMode && airportBrightness && airportRange==model.rangeIndex)
        for(unsigned i=0;i<airportHitCount;++i){unsigned n=airportHits[i].index;if(n<airportCount)choices[count++]={airportMarkers[n].code,n,true};}
    const int next=sky::nextRadarChoice(choices,count,selectedAirport[0]?selectedAirport:model.selected,selectedAirport[0]!=0,delta);
    selectedAirport[0]=0;
    if(next<0)return;
    if(choices[next].airport)snprintf(selectedAirport,sizeof(selectedAirport),"%s",choices[next].key);
    else model.select(choices[next].index);
}

inline void metric(int y,const char *name,float value,const char *unit) {
    char s[80]; if(std::isfinite(value)) std::snprintf(s,sizeof(s),"%s   %.0f %s",name,value,unit);
    else std::snprintf(s,sizeof(s),"%s   --",name);
    text(y,s,&lv_font_montserrat_20);
}
inline void footer(size_t visible) {
    char zoom[24],flights[32];
    snprintf(zoom,sizeof(zoom),"%.0f km",model.range());
    snprintf(flights,sizeof(flights),"%u planes",unsigned(visible));
    lv_draw_rect_dsc_t bg; lv_draw_rect_dsc_init(&bg); bg.bg_color=lv_color_hex(0x030D10);
    bg.bg_opa=LV_OPA_COVER; bg.border_width=0;
    lv_canvas_draw_rect(canvas,76,400,314,42,&bg);
    const char *labels[]={zoom,flights,"Alt","Type"};
    const int active=model.rotationMode();
    for(int i=0;i<4;++i) {
        text(402,labels[i],&lv_font_montserrat_14,i==active?green:muted,73+i*80,80);
        if(i==active) line(89+i*80,420,137+i*80,420,green,1);
    }
    char values[64]; snprintf(values,sizeof(values),"%s / %s",sky::altitudeLabel(model.altitudeFilter),sky::filterLabel(model.filter));
    text(424,values,&lv_font_montserrat_14,muted);

}
inline void renderStations(uint32_t now) {
    text(35,"SPACE STATIONS",&lv_font_montserrat_22,green);
    if(!stationCount || uint32_t(now-stationReceived)>30000) {
        text(193,"Updating predictions...",&lv_font_montserrat_20,muted);
        text(380,"Tap / press to return",&lv_font_montserrat_14,muted); return;
    }
    if(stationIndex>=int(stationCount)) stationIndex=0;
    const auto &selected=stations[stationIndex];
    itemRing(stationIndex,stationCount);
    text(70,selected.name,&lv_font_montserrat_18);
    for(int i=1;i<=3;++i) circle(233,230,130*i/3,grid);
    line(103,230,363,230,grid); line(233,100,233,360,grid);
    text(103,"N",&lv_font_montserrat_14,muted);
    text(224,"W",&lv_font_montserrat_14,muted,88,24); text(224,"E",&lv_font_montserrat_14,muted,354,24);
    text(341,"S",&lv_font_montserrat_14,muted);
    bool above=false;
    for(unsigned i=0;i<stationCount;++i) if(stations[i].el>=0) {
        above=true; const auto &a=stations[i]; const float r=130*(90-a.el)/90,angle=a.az*sky::pi/180;
        const int x=233+std::sin(angle)*r,y=230-std::cos(angle)*r;
        circle(x,y,i==unsigned(stationIndex)?7:4,i==unsigned(stationIndex)?amber:green,2,true);
    }
    if(!above) text(223,"Below the horizon",&lv_font_montserrat_16,muted);
    char label[96]; snprintf(label,sizeof(label),"Az %.0f°  /  El %.0f°  /  %.0f km",selected.az,selected.el,selected.km);
    text(365,label,&lv_font_montserrat_14,green);
    const time_t rise=selected.nextRise;
    if(rise) { struct tm utc; gmtime_r(&rise,&utc); snprintf(label,sizeof(label),"Next 10° rise: %02d:%02d UTC",utc.tm_hour,utc.tm_min); }
    else snprintf(label,sizeof(label),"No 10° rise in next 24h");
    text(391,label,&lv_font_montserrat_14,muted);
    text(420,"Predicted / CelesTrak",&lv_font_montserrat_14,muted);
}
inline bool routePage=false,routeReady=false;
inline char detailHex[12]{},detailCall[16]{},routeCall[16]{};
inline InfoPage routeInfo{};
inline bool pageAnimating=false;
inline lv_color_t *slidePixels[2]{};
inline lv_img_dsc_t slideImages[2]{};
inline lv_obj_t *slideObjects[2]{};
inline int slideDirection=1;
inline void render(uint32_t now);
inline bool routeAvailable() { return flightsEnabled; }
inline void aircraftNavigation() {
    int count=0,index=-1;
    for(size_t i=0;i<model.data.count;++i) if(model.visible(model.data.aircraft[i],navigationNow)) { if(!std::strcmp(model.data.aircraft[i].hex,model.selected)) index=count; ++count; }
    itemRing(index,count); pageArrows(routeAvailable());
}
inline void finishSlide() {
    if(!pageAnimating) return;
    for(auto *&obj:slideObjects) { if(obj) lv_obj_del(obj); obj=nullptr; }
    pageAnimating=false;
}
inline bool swipeDetails(int direction,uint32_t now) {
    if(settings || infoMenu || infoView || satelliteView || !model.details || pageAnimating) return false;
    // A rapid swipe can arrive before the regular frame catches a new selection.
    if(const auto *a=model.selection()) if(std::strcmp(detailHex,a->hex) || std::strcmp(detailCall,a->callsign)) render(now);
    if(!routeAvailable()) return false;
    if(!direction) return false;
    if(slidePixels[0] && slidePixels[1]) memcpy(slidePixels[0],lv_canvas_get_img(canvas)->data,size*size*sizeof(lv_color_t));
    routePage=!routePage;
    render(now);
    if(!slidePixels[0] || !slidePixels[1]) return true; // Low-memory fallback preserves navigation.
    memcpy(slidePixels[1],lv_canvas_get_img(canvas)->data,size*size*sizeof(lv_color_t));
    slideDirection=direction;
    for(int i=0;i<2;++i) {
        lv_img_cache_invalidate_src(&slideImages[i]);
        slideImages[i].header.cf=LV_IMG_CF_TRUE_COLOR; slideImages[i].header.w=size; slideImages[i].header.h=size;
        slideImages[i].data_size=size*size*sizeof(lv_color_t); slideImages[i].data=reinterpret_cast<uint8_t*>(slidePixels[i]);
        slideObjects[i]=lv_img_create(lv_scr_act()); lv_img_set_src(slideObjects[i],&slideImages[i]);
        lv_obj_clear_flag(slideObjects[i],LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(slideObjects[i],i*direction*size,0);
    }
    pageAnimating=true;
    lv_anim_t animation; lv_anim_init(&animation); lv_anim_set_var(&animation,canvas);
    lv_anim_set_values(&animation,0,-direction*size); lv_anim_set_time(&animation,380);
    lv_anim_set_path_cb(&animation,lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation,[](void*,int32_t x) { if(pageAnimating) { lv_obj_set_x(slideObjects[0],x); lv_obj_set_x(slideObjects[1],x+slideDirection*size); } });
    lv_anim_set_ready_cb(&animation,[](lv_anim_t*) { finishSlide(); }); lv_anim_start(&animation);
    return true;
}
inline void renderRoute() {
    aircraftNavigation();
    text(54,detailCall[0]?detailCall:(model.selection()?model.selection()->registration:""),&lv_font_montserrat_28,white);
    text(97,"FLIGHT ROUTE",&lv_font_montserrat_18,green);
    if(!detailCall[0]) text(215,"Unavailable",&lv_font_montserrat_20,muted);
    else if(!routeReady || std::strcmp(routeCall,detailCall)) text(215,"Loading route...",&lv_font_montserrat_20,muted);
    else for(int i=0;i<7;++i) {
        lv_point_t bounds; lv_txt_get_size(&bounds,routeInfo.lines[i],&lv_font_montserrat_16,0,0,350,LV_TEXT_FLAG_NONE);
        text(145+i*32,routeInfo.lines[i],bounds.y>30?&lv_font_montserrat_12:&lv_font_montserrat_16,i==1?green:muted,58,350);
    }
    text(395,"Route database: adsbdb",&lv_font_montserrat_14,muted);
    text(422,"Turn: aircraft / centre: radar",&lv_font_montserrat_12,muted);
}
inline void render(uint32_t now) {
    navigationNow=now;
    if(pageAnimating) return;
    if(!model.details) { routePage=false; detailHex[0]=0; }
    else if(const auto *a=model.selection()) {
        if(std::strcmp(detailHex,a->hex) || std::strcmp(detailCall,a->callsign)) {
            snprintf(detailHex,sizeof(detailHex),"%s",a->hex); snprintf(detailCall,sizeof(detailCall),"%s",a->callsign);
        }
    }
    if(!routeAvailable()) routePage=false;
    model.refresh(now);
    lv_canvas_fill_bg(canvas,lv_color_hex(0x030D10),LV_OPA_COVER);
    if(settings) {
        text(70,"SETUP",&lv_font_montserrat_28,green);
        text(126,setupConnected?"Connected to Wi-Fi":"Join Wi-Fi network",&lv_font_montserrat_18,muted);
        text(155,setupConnected?setupSSID:"EchoScope-Setup",&lv_font_montserrat_20,white,63,340);
        if(setupConnected) {
            text(229,"Setup access point is off",&lv_font_montserrat_16,muted);
        } else {
            text(204,"Wi-Fi password",&lv_font_montserrat_16,muted);
            text(229,setupPassword,&lv_font_montserrat_24);
        }
        text(279,"Open in your browser",&lv_font_montserrat_16,muted);
        text(306,setupAddress,&lv_font_montserrat_22,green);
        text(370,"Press or tap to return",&lv_font_montserrat_16,muted);
        return;
    }
    if(notificationUntil && int32_t(notificationUntil-now)>0) {
        circle(centre,centre,222,green,3);
        text(105,"ECHOSCOPE ALERT",&lv_font_montserrat_22,green);
        text(174,notification,&lv_font_montserrat_20,white,73,320);
        if(notificationCountdown) {
            const int seconds=std::max<int32_t>(0,int32_t(notificationCountdown-now)/1000);
            char countdown[40]; snprintf(countdown,sizeof(countdown),seconds?"Visible in %d:%02d":"Look up now",seconds/60,seconds%60);
            text(300,countdown,&lv_font_montserrat_22,green);
        }
        text(355,"Tap or press to dismiss",&lv_font_montserrat_14,muted); return;
    }
    notificationUntil=0;
    if(infoMenu || infoView) { renderInfo(now); return; }
    if(satelliteView && satellitesEnabled) { renderStations(now); return; }
    const bool stale=!model.demo && (!model.hasUpdate || uint32_t(now-model.lastUpdate)>20000);
    // Attribution remains in setup/details; normal live operation needs no banner.
    if(model.details && (stale || model.demo)) text(22,model.demo?"DEMO":"DATA STALE",&lv_font_montserrat_14,amber);
    if(model.details && routePage) { renderRoute(); return; }
    if(model.details) {
        aircraftNavigation();
        auto *a=model.selection();
        if(!a) {
            text(145,"Aircraft left coverage",&lv_font_montserrat_22,amber);
            text(193,model.selected,&lv_font_montserrat_28);
            text(250,"Turn to select another flight",&lv_font_montserrat_18,muted);
        } else {
            char s[80];
            const bool showPhoto=photosEnabled && photoReady && !std::strcmp(photoReg,a->registration);
            text(showPhoto?44:64,a->callsign[0]?a->callsign:a->hex,&lv_font_montserrat_36);
            std::snprintf(s,sizeof(s),"%s  /  %s",a->registration[0]?a->registration:"--",a->type[0]?a->type:"--");
            text(showPhoto?88:109,s,&lv_font_montserrat_20,muted);
            if(showPhoto) {
                // Photo and live telemetry share one page; retain attribution and age.
                text(116,a->description[0]?a->description:"Aircraft model unavailable",&lv_font_montserrat_14,white,63,340);
                if(photoReady && !std::strcmp(photoReg,a->registration)) {
                    lv_draw_img_dsc_t d; lv_draw_img_dsc_init(&d);
                    lv_canvas_draw_img(canvas,(size-photoImage.header.w)/2,151,&photoImage,&d);
                    char credit[170]; snprintf(credit,sizeof(credit),"Copyright %s / Planespotters.net",photoCredit);
                    lv_draw_label_dsc_t label; lv_draw_label_dsc_init(&label);
                    label.color=lv_color_hex(muted); label.font=&lv_font_montserrat_14; label.align=LV_TEXT_ALIGN_CENTER;
                    // Shrink unusually long credits to fit the reserved two lines.
                    lv_point_t bounds; lv_txt_get_size(&bounds,credit,label.font,0,0,360,LV_TEXT_FLAG_NONE);
                    if(bounds.y>34) label.font=&lv_font_montserrat_12;
                    lv_canvas_draw_text(canvas,53,305,360,&label,credit);
                } else text(210,!a->registration[0]?"No registration available":photoStatus,&lv_font_montserrat_18,muted);
                char alt[16],speed[16],track[16];
                auto value=[](char *out,size_t n,float v) { if(std::isfinite(v)) snprintf(out,n,"%.0f",v); else snprintf(out,n,"--"); };
                value(alt,sizeof(alt),a->altitude); value(speed,sizeof(speed),a->speed); value(track,sizeof(track),a->track);
                snprintf(s,sizeof(s),"%s ft   /   %s kt",alt,speed);
                text(347,s,&lv_font_montserrat_20);
                snprintf(s,sizeof(s),"%.1f km / %.0f deg   Track %s",sky::distance(a->position),sky::bearing(a->position),track);
                text(375,s,&lv_font_montserrat_16,green);
                snprintf(s,sizeof(s),"%sPosition %.0fs old",a->military?"MILITARY / ":"",sky::age(*a,now));
                text(401,s,&lv_font_montserrat_14,sky::age(*a,now)>20?amber:muted);
                text(426,"Data: adsb.fi",&lv_font_montserrat_14,muted);
                return;
            }
            text(144,a->description[0]?a->description:"Aircraft model unavailable",&lv_font_montserrat_16,white,63,340);
            if(a->military) text(185,"MILITARY",&lv_font_montserrat_14,amber);
            std::snprintf(s,sizeof(s),"%.1f km  /  %.0f deg",sky::distance(a->position),sky::bearing(a->position));
            text(209,s,&lv_font_montserrat_24,green);
            metric(248,"ALT",a->altitude,"ft"); metric(279,"SPEED",a->speed,"kt"); metric(310,"TRACK",a->track,"deg");
            std::snprintf(s,sizeof(s),"Position %.0fs old",sky::age(*a,now));
            text(350,s,&lv_font_montserrat_16,sky::age(*a,now)>20?amber:muted);
        }
        text(426,"Data: adsb.fi",&lv_font_montserrat_14,muted);
        return;
    }
    const bool drawMap=mapsEnabled && mapWanted && mapReady && mapRange==model.rangeIndex;
    if(drawMap) { lv_draw_img_dsc_t image; lv_draw_img_dsc_init(&image); lv_canvas_draw_img(canvas,23,23,&mapImage,&image); }
    for(int i=1;i<=4;++i) circle(centre,centre,radius*i/4,grid);
    line(centre-radius,centre,centre+radius,centre,grid);
    line(centre,centre-radius,centre,centre+radius,grid);
    text(48,"N",&lv_font_montserrat_16,muted);
    text(drawMap?357:378,"S",&lv_font_montserrat_14,muted);
    text(225,"W",&lv_font_montserrat_14,muted,38,26);
    text(225,"E",&lv_font_montserrat_14,muted,402,26);
    if(!airportsEnabled)selectedAirport[0]=0;
    renderAirports();
    if(selectedAirportIndex()<0)selectedAirport[0]=0;
    // Sweep is decorative. Aircraft are always drawn from their last reported position.
    const float angle=(now%8000)*2*sky::pi/8000;
    for(int i=0;i<9;++i) {
        const float a=angle-i*0.032f;
        line(centre,centre,centre+std::sin(a)*radius,centre-std::cos(a)*radius,green,1,20+i*3);
    }
    circle(centre,centre,4,white,1,true);
    // Other paths first; selected path on top, with aircraft/labels above both.
    for(int pass=0;pass<2;++pass) for(size_t i=0;i<model.data.count;++i) {
        const auto &a=model.data.aircraft[i];
        if(!model.visible(a,now)) continue;
        const bool selected=!selectedAirport[0] && !std::strcmp(a.hex,model.selected);
        if(selected!=(pass==1)) continue;
        const auto *trail=model.trailFor(a.hex); if(!trail) continue;
        for(size_t j=1;j<trail->count;++j) {
            auto from=trail->points[j-1],to=trail->points[j];
            if(!sky::clipToCircle(from,to,model.range())) continue;
            auto p=screen(from),q=screen(to);
            line(p.x,p.y,q.x,q.y,sky::altitudeColor(selected?trail->altitudes[j]:a.altitude),selected?2:1,selected?180:45);
        }
    }
    size_t visible=0;
    for(size_t i=0;i<model.data.count;++i) {
        auto &a=model.data.aircraft[i];
        if(!model.visible(a,now)) continue;
        ++visible;
        const bool selected=!selectedAirport[0] && !std::strcmp(a.hex,model.selected);
        drawAircraft(a,screen(a.position),selected,now);
    }
    if(selectedAirport[0]) { char caption[32];snprintf(caption,sizeof(caption),"AIRPORT %s",selectedAirport);text(80,caption,&lv_font_montserrat_18,amber); }
    else if(model.demo) text(84,"DEMO",&lv_font_montserrat_14,muted);
    if(drawMap) drawMapAttribution(radarControls.visible(now)?383:401);
    if(radarControls.visible(now)) footer(visible);
    const auto alert=model.activeAlert(now);
    const uint8_t opacity=alertStyle.opacity(now);
    if(alert!=sky::AlertKind::None && opacity) circle(centre,centre,229,alertStyle.color(alert),alertStyle.width,false,opacity);
    if(!visible) text(310,stale?"Waiting for fresh positions":model.filter==sky::Filter::All?"No aircraft in this range":"No matching aircraft in range",&lv_font_montserrat_16,muted);
    if(stale) text(365,status,&lv_font_montserrat_14,amber);
}
inline void tap(int x,int y,uint32_t now) {
    if(pageAnimating) return;
    if(notificationUntil) { notificationUntil=0; return; }
    if(settings) { settings=false; return; }
    if(infoMenu || infoView) { tapInfo(x,y); return; }
    if(satelliteView) { satelliteView=false; return; }
    if(model.details) {
        if(routeAvailable() && pageSide(x,y)) { swipeDetails(pageSide(x,y),now); return; }
        routePage=false; model.details=false; model.refresh(now); return;
    }
    if(y>=395) {
        radarControls.show(now);
        const auto previous=model.filter; const int band=model.altitudeFilter;
        const int mode=std::max(0,std::min(3,(x-73)/80));
        if(mode==1 && model.rotationMode()==1)rotateRadarSelection(1,now);else model.tapMode(mode,now);
        if(previous!=model.filter || band!=model.altitudeFilter) requestFeed=true;
        return;
    }
    float east=(x-centre)*model.range()/radius, north=(centre-y)*model.range()/radius;
    int i=model.hit(east,north,25*model.range()/radius,now);
    if(i>=0) { selectedAirport[0]=0; model.select(i); model.details=true; }
    else {
        const int airport=hitAirport(x,y);
        if(airport>=0 && airportsEnabled) {
            openAirport(airport);
        } else openRadarSelection(now);
    }
}
}
