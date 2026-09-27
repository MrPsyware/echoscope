#pragma once
// Small vector icons; no downloaded bitmap or font glyph dependency.
inline void weatherIcon(int x,int y,int icon,bool night,int scale=1) {
    auto stroke=[&](int a,int b,int c,int d,uint32_t color) { line(x+a*scale,y+b*scale,x+c*scale,y+d*scale,color,2*scale); };
    if(icon==7) { text(y-12,"?",&lv_font_montserrat_24,muted,x-20,40); return; }
    if(icon<=1) {
        circle(x-7*scale,y-8*scale,12*scale,amber,2*scale,true);
        if(night) circle(x-2*scale,y-12*scale,11*scale,0x030D10,0,true);
        else for(int i=0;i<8;++i) {
            const float a=i*sky::pi/4;
            line(x+(-7+16*std::cos(a))*scale,y+(-8+16*std::sin(a))*scale,x+(-7+20*std::cos(a))*scale,y+(-8+20*std::sin(a))*scale,amber,2*scale);
        }
        if(icon==0) return;
    }
    circle(x-13*scale,y+4*scale,10*scale,muted,0,true);
    circle(x,y-2*scale,15*scale,muted,0,true);
    circle(x+15*scale,y+5*scale,10*scale,muted,0,true);
    stroke(-13,13,15,13,muted);
    if(icon==3) for(int i=-12;i<=12;i+=12) stroke(i,21,i-4,29,0x6CCCED);
    if(icon==4) for(int i=-14;i<=14;i+=14) { stroke(i-3,24,i+3,24,white); stroke(i,21,i,27,white); }
    if(icon==5) { stroke(2,17,-6,28,amber); stroke(-6,28,4,28,amber); stroke(4,28,-4,38,amber); }
    if(icon==6) { stroke(-22,21,22,21,muted); stroke(-16,29,16,29,muted); }
}
inline void renderWeather(const InfoPage &p) {
    text(47,p.title,&lv_font_montserrat_24,green);
    text(81,p.subtitle,&lv_font_montserrat_14,muted);
    if(p.cardCount==1) {
        const auto &c=p.cards[0];
        weatherIcon(233,c.icon==5?132:154,c.icon,c.night,2);
        text(214,c.temperature,&lv_font_montserrat_36);
        char description[80]; snprintf(description,sizeof(description),"%s / %s",c.label,c.condition);
        text(260,description,&lv_font_montserrat_18,muted);
        text(295,c.cloud,&lv_font_montserrat_22,green);
        char detail[80]; snprintf(detail,sizeof(detail),"%s  /  %s",c.rain,c.wind);
        text(330,detail,&lv_font_montserrat_16,muted);
    } else for(unsigned i=0;i<p.cardCount && i<3;++i) {
        const auto &c=p.cards[i]; const int y=122+i*78;
        weatherIcon(100,y+27,c.icon,c.night);
        text(y,c.label,&lv_font_montserrat_18,green,142,110);
        text(y,c.temperature,&lv_font_montserrat_18,white,253,135);
        char detail[80]; snprintf(detail,sizeof(detail),"%s / %s",c.cloud,c.rain);
        text(y+31,detail,&lv_font_montserrat_14,muted,142,248);
    }
    text(365,p.note,&lv_font_montserrat_14,green,63,340);
    text(395,"Open-Meteo / CC BY 4.0",&lv_font_montserrat_14,muted);
    char nav[64]; snprintf(nav,sizeof(nav),"%d / %u   Turn to change view",infoPage+1,infoCount);
    text(417,nav,&lv_font_montserrat_14,muted);
}
