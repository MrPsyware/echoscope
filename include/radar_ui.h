#pragma once
#include <lvgl.h>
#include <cstdio>
#include <atomic>
#include <ctime>
#include "info_protocol.h"
#include "radar_model.h"
#include "input_gate.h"
#include "activity.h"
namespace ui {
constexpr int size=466, centre=233, radius=210;
constexpr uint32_t green=0x68F3AE, muted=0x63958E, grid=0x143B36, white=0xE8F8F4, amber=0xF2BB70;
inline lv_obj_t *canvas;
inline sky::Model model;
inline sky::InputGate input;
inline sky::Activity activity;
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
inline void rotateStations(int delta) { if(stationCount) stationIndex=((stationIndex+delta)%int(stationCount)+int(stationCount))%int(stationCount); }

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
    const char *labels[]={zoom,flights,"ALT"};
    const int active=model.altitudeMode?2:model.selectMode?1:0;
    for(int i=0;i<3;++i) {
        text(402,labels[i],&lv_font_montserrat_16,i==active?green:muted,83+i*100,100);
        if(i==active) text(402,labels[i],&lv_font_montserrat_16,green,84+i*100,100);
    }
    text(424,sky::altitudeLabel(model.altitudeFilter),&lv_font_montserrat_14,
         model.altitudeFilter?sky::altitudeColor(model.altitudeFilter==1?0:model.altitudeFilter==2?5000:model.altitudeFilter==3?15000:model.altitudeFilter==4?30000:NAN):muted);
}
inline void renderStations(uint32_t now) {
    text(35,"SPACE STATIONS",&lv_font_montserrat_22,green);
    if(!stationCount || uint32_t(now-stationReceived)>30000) {
        text(193,"Updating predictions...",&lv_font_montserrat_20,muted);
        text(380,"Tap / press to return",&lv_font_montserrat_14,muted); return;
    }
    if(stationIndex>=int(stationCount)) stationIndex=0;
    const auto &selected=stations[stationIndex];
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
inline void render(uint32_t now) {
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
    if(satelliteView && satellitesEnabled) { renderStations(now); return; }
    const bool stale=!model.demo && (!model.hasUpdate || uint32_t(now-model.lastUpdate)>20000);
    // Attribution remains in setup/details; normal live operation needs no banner.
    if(model.details && (stale || model.demo)) text(22,model.demo?"DEMO":"DATA STALE",&lv_font_montserrat_14,amber);
    if(model.details) {
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
        const bool selected=!std::strcmp(a.hex,model.selected);
        if(selected!=(pass==1)) continue;
        const auto *trail=model.trailFor(a.hex); if(!trail) continue;
        for(size_t j=1;j<trail->count;++j) {
            auto from=trail->points[j-1],to=trail->points[j];
            if(!sky::clipToCircle(from,to,model.range())) continue;
            auto p=screen(from),q=screen(to);
            line(p.x,p.y,q.x,q.y,selected?amber:sky::altitudeColor(a.altitude),selected?2:1,selected?140:45);
        }
    }
    size_t visible=0;
    for(size_t i=0;i<model.data.count;++i) {
        auto &a=model.data.aircraft[i];
        if(!model.visible(a,now)) continue;
        ++visible;
        const bool selected=!std::strcmp(a.hex,model.selected);
        const uint32_t color=selected?amber:sky::age(a,now)>20?muted:sky::altitudeColor(a.altitude);
        auto p=screen(a.position);
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
    if(activity.filterVisible) {
    // Top touch target cycles filters; reserve its background above the plot.
    lv_draw_rect_dsc_t chip; lv_draw_rect_dsc_init(&chip);
    chip.bg_color=lv_color_hex(0x030D10); chip.bg_opa=LV_OPA_COVER; chip.border_width=0;
    lv_canvas_draw_rect(canvas,133,25,200,37,&chip);
    const char *filter=model.filter==sky::Filter::All?"ALL":model.filter==sky::Filter::Military?"MILITARY":"ROTORCRAFT";
    char filterText[40]; snprintf(filterText,sizeof(filterText),"%s  >%s",filter,model.demo?"  DEMO":"");
    text(36,filterText,&lv_font_montserrat_14,green,128,210);
    }
    if(model.demo && !activity.filterVisible) text(84,"DEMO",&lv_font_montserrat_14,muted);
    if(drawMap) text(383,mapCredit,&lv_font_montserrat_12,white,58,350);
    if(satellitesEnabled) text(82,"SAT >",&lv_font_montserrat_14,green,315,70);
    footer(visible);
    const auto alert=model.activeAlert(now);
    const uint8_t opacity=alertStyle.opacity(now);
    if(alert!=sky::AlertKind::None && opacity) circle(centre,centre,229,alertStyle.color(alert),alertStyle.width,false,opacity);
    if(!visible) text(310,stale?"Waiting for fresh positions":model.filter==sky::Filter::All?"No aircraft in this range":"No matching aircraft in range",&lv_font_montserrat_16,muted);
    if(stale) text(365,status,&lv_font_montserrat_14,amber);
}
inline void tap(int x,int y,uint32_t now) {
    if(settings) { settings=false; return; }
    if(satelliteView) { satelliteView=false; return; }
    if(model.details) {
        model.details=false; model.refresh(now); return;
    }
    if(satellitesEnabled && y>=70 && y<=108 && x>=305 && x<=395) { satelliteView=true; return; }
    if(y>=20 && y<67 && x>=128 && x<=338) { if(activity.tapFilter(now)) { model.cycleFilter(now); requestFeed=true; } return; }
    if(y>=400) { model.selectMode=x>=183 && x<283; model.altitudeMode=x>=283; model.refresh(now); return; }
    float east=(x-centre)*model.range()/radius, north=(centre-y)*model.range()/radius;
    int i=model.hit(east,north,25*model.range()/radius,now);
    if(i>=0) { model.select(i); model.details=true; }
    else model.openSelected(now);
}
}
