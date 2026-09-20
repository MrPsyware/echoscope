#pragma once
#include <lvgl.h>
#include <cstdio>
#include "radar_model.h"
#include "input_gate.h"
namespace ui {
constexpr int size=466, centre=233, radius=182;
constexpr uint32_t green=0x68F3AE, muted=0x63958E, grid=0x143B36, white=0xE8F8F4, amber=0xF2BB70;
inline lv_obj_t *canvas;
inline sky::Model model;
inline sky::InputGate input;
inline int footerSplitX=centre;
inline bool settings=false;
inline char status[80]="Starting", setupPassword[20]="", setupAddress[24]="192.168.4.1";
inline void line(int x,int y,int x2,int y2,uint32_t color,int width=1,lv_opa_t opacity=LV_OPA_COVER) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d); d.color=lv_color_hex(color); d.width=width; d.opa=opacity;
    lv_point_t p[]={{lv_coord_t(x),lv_coord_t(y)},{lv_coord_t(x2),lv_coord_t(y2)}};
    lv_canvas_draw_line(canvas,p,2,&d);
}
inline void circle(int x,int y,int r,uint32_t color,int width=1,bool fill=false) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d); d.radius=LV_RADIUS_CIRCLE;
    d.bg_opa=fill?LV_OPA_COVER:LV_OPA_TRANSP; d.bg_color=lv_color_hex(color);
    d.border_color=lv_color_hex(color); d.border_width=width;
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
    std::snprintf(zoom,sizeof(zoom),"%.0f km",model.range());
    std::snprintf(flights,sizeof(flights),"%u aircraft",unsigned(visible));
    auto width=[](const char *s) { lv_point_t p; lv_txt_get_size(&p,s,&lv_font_montserrat_16,0,0,400,LV_TEXT_FLAG_NONE); return int(p.x); };
    const int z=width(zoom)+2,separator=width(" / "),f=width(flights)+2;
    const int start=(size-z-separator-f)/2;
    footerSplitX=start+z+separator/2;
    text(420,zoom,&lv_font_montserrat_16,model.selectMode?muted:green,start,z);
    text(420," / ",&lv_font_montserrat_16,muted,start+z,separator);
    text(420,flights,&lv_font_montserrat_16,model.selectMode?green:muted,start+z+separator,f);
    // One-pixel emboldening retains the compact built-in font and baseline.
    if(model.selectMode) text(420,flights,&lv_font_montserrat_16,green,start+z+separator+1,f);
    else text(420,zoom,&lv_font_montserrat_16,green,start+1,z);
}
inline void render(uint32_t now) {
    model.refresh(now);
    lv_canvas_fill_bg(canvas,lv_color_hex(0x030D10),LV_OPA_COVER);
    if(settings) {
        text(70,"SETUP",&lv_font_montserrat_28,green);
        text(126,"Join Wi-Fi network",&lv_font_montserrat_18,muted);
        text(155,"EchoScope-Setup",&lv_font_montserrat_24);
        text(204,"Wi-Fi password",&lv_font_montserrat_16,muted);
        text(229,setupPassword,&lv_font_montserrat_24);
        text(279,"Then open in your browser",&lv_font_montserrat_16,muted);
        text(306,setupAddress,&lv_font_montserrat_22,green);
        text(370,"Press or tap to return",&lv_font_montserrat_16,muted);
        return;
    }
    const bool stale=!model.demo && (!model.hasUpdate || uint32_t(now-model.lastUpdate)>20000);
    text(22,model.demo?"DEMO / SAMPLE AIRCRAFT":stale?"WAITING / DATA STALE":"LIVE / ADSB.FI",&lv_font_montserrat_14,stale?amber:green);
    if(model.details) {
        auto *a=model.selection();
        if(!a) {
            text(145,"Aircraft left coverage",&lv_font_montserrat_22,amber);
            text(193,model.selected,&lv_font_montserrat_28);
            text(250,"Turn to select another flight",&lv_font_montserrat_18,muted);
        } else {
            char s[80];
            text(70,a->callsign[0]?a->callsign:a->hex,&lv_font_montserrat_36);
            std::snprintf(s,sizeof(s),"%s  /  %s",a->registration[0]?a->registration:"--",a->type[0]?a->type:"--");
            text(121,s,&lv_font_montserrat_20,muted);
            std::snprintf(s,sizeof(s),"%.1f km  /  %.0f deg",sky::distance(a->position),sky::bearing(a->position));
            text(162,s,&lv_font_montserrat_24,green);
            metric(214,"ALT",a->altitude,"ft"); metric(247,"SPEED",a->speed,"kt"); metric(280,"TRACK",a->track,"deg");
            std::snprintf(s,sizeof(s),"Position %.0fs old",sky::age(*a,now));
            text(330,s,&lv_font_montserrat_16,sky::age(*a,now)>20?amber:muted);
        }
        text(380,"TURN: FLIGHTS",&lv_font_montserrat_14,muted);
        text(402,"PRESS / TAP: RADAR",&lv_font_montserrat_14,green);
        return;
    }
    for(int i=1;i<=4;++i) circle(centre,centre,radius*i/4,grid);
    line(centre-radius,centre,centre+radius,centre,grid);
    line(centre,centre-radius,centre,centre+radius,grid);
    text(48,"N",&lv_font_montserrat_16,muted);
    text(398,"S",&lv_font_montserrat_14,muted);
    text(225,"W",&lv_font_montserrat_14,muted,17,26);
    text(225,"E",&lv_font_montserrat_14,muted,423,26);
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
            line(p.x,p.y,q.x,q.y,selected?amber:green,selected?2:1,selected?140:45);
        }
    }
    size_t visible=0;
    for(size_t i=0;i<model.data.count;++i) {
        auto &a=model.data.aircraft[i];
        if(sky::distance(a.position)>model.range() || sky::age(a,now)>60) continue;
        ++visible;
        const bool selected=!std::strcmp(a.hex,model.selected);
        const uint32_t color=sky::age(a,now)>20?muted:selected?amber:green;
        auto p=screen(a.position);
        if(std::isfinite(a.track)) {
            const float t=a.track*sky::pi/180;
            int nx=p.x+std::sin(t)*10,ny=p.y-std::cos(t)*10;
            int lx=p.x+std::sin(t+2.5f)*7,ly=p.y-std::cos(t+2.5f)*7;
            int rx=p.x+std::sin(t-2.5f)*7,ry=p.y-std::cos(t-2.5f)*7;
            line(nx,ny,lx,ly,color,2); line(nx,ny,rx,ry,color,2); line(lx,ly,rx,ry,color,2);
        } else circle(p.x,p.y,4,color,1,true);
        if(selected) {
            circle(p.x,p.y,15,amber);
            const int tx=std::max(20,std::min(310,int(p.x)-65));
            const int ty=p.y>centre?p.y-34:p.y+18;
            text(ty,a.callsign[0]?a.callsign:a.hex,&lv_font_montserrat_14,amber,tx,136);
        }
    }
    footer(visible);
    if(!visible) text(310,stale?"Waiting for fresh positions":"No aircraft in this range",&lv_font_montserrat_16,muted);
    if(stale) text(365,status,&lv_font_montserrat_14,amber);
}
inline void tap(int x,int y,uint32_t now) {
    if(settings) { settings=false; return; }
    if(model.details) { model.details=false; model.refresh(now); return; }
    float east=(x-centre)*model.range()/radius, north=(centre-y)*model.range()/radius;
    int i=model.hit(east,north,25*model.range()/radius,now);
    if(i>=0) { model.select(i); model.details=true; }
    else if(y>395) { model.selectMode=x>=footerSplitX; model.refresh(now); }
    else model.openSelected(now);
}
}
