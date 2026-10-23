#pragma once
// Network task only; publish complete images under the LVGL lock.
namespace rainRuntime {
static_assert(sizeof(lv_color_t)==2,"Rain image packing requires RGB565");
rain::Timeline timeline;
uint32_t nextManifest=0,nextRequest=0,retryAt=0;
uint32_t *lookup=nullptr;
uint8_t *working=nullptr,*coverage=nullptr,*display=nullptr;
struct Tile { int x=0,y=0; } tiles[16];
unsigned row=0,tileCount=0,tileIndex=0;
int jobRange=-1,coverageRange=-1,jobFrame=-1,jobZoom=0;
uint32_t jobTime=0;
bool coverageStep=true,active=false;
void cancel() { free(lookup); free(working); lookup=nullptr; working=nullptr; active=false; }
void message(const char *value) { lvgl_port_lock(-1); snprintf(ui::rainMessage,sizeof(ui::rainMessage),"%s",value); lvgl_port_unlock(); }
void reset() {
    cancel(); free(coverage); coverage=nullptr; coverageRange=-1; nextManifest=nextRequest=retryAt=0; timeline.count=0;
    lvgl_port_lock(-1); ui::rainReady=false; ui::rainCount=0; lv_img_cache_invalidate_src(&ui::rainImage); free(display); display=nullptr; ui::rainImage={}; lvgl_port_unlock();
}
void fail(const char *why) { cancel(); retryAt=millis()+60000; message(why); deviceLog.printf("[rain] %s\n",why); }
bool tilePNG(const String &url,uint8_t **rgba) {
    if(int32_t(millis()-nextRequest)<0) return false;
    FeedTLSClient client; const String roots=String(apiRootCA)+standaloneRootCA;
    client.setCACert(roots.c_str()); client.setHandshakeTimeout(5);
    HTTPClient http; http.useHTTP10(true); http.setConnectTimeout(2500); http.setTimeout(4000);
    http.setUserAgent("EchoScope rain radar (+https://github.com/MrPsyware/echoscope)");
    bool ok=false; http.begin(client,url); const int code=http.GET();
    if(code==200) {
        auto body=readFeedBody(http,256*1024,5000);
        LodePNGState state; lodepng_state_init(&state); unsigned w=0,h=0;
        auto *bytes=reinterpret_cast<const uint8_t*>(body.text.c_str());
        unsigned error=body.complete?lodepng_inspect(&w,&h,&state,bytes,body.text.length()):1;
        lodepng_state_cleanup(&state);
        if(!error && w==256 && h==256) error=lodepng_decode32(rgba,&w,&h,bytes,body.text.length()); else error=1;
        ok=!error && w==256 && h==256;
        deviceLog.printf("[rain] Tile HTTP=%d bytes=%u PNG=%u\n",code,unsigned(body.text.length()),error);
    } else deviceLog.printf("[rain] Tile HTTP=%d\n",code);
    http.end(); nextRequest=millis()+(code==429?120000:1100);
    return ok;
}
void step(int range,int frame) {
    const uint32_t now=millis();
    if(time(nullptr)<1700000000) { message("Waiting for clock sync"); return; }
    if(int32_t(now-retryAt)<0 || int32_t(now-nextRequest)<0) return;
    if(!active && int32_t(now-nextManifest)>=0) {
        JsonDocument doc(&standaloneAllocator); rain::Timeline next;
        int code=0;
        const bool ok=publicJSON("https://api.rainviewer.com/public/weather-maps.json",doc,&code) && next.parse(doc.as<JsonVariantConst>(),time(nullptr));
        nextManifest=millis()+(ok?300000:60000); nextRequest=millis()+(code==429?120000:1100);
        if(!ok) { fail("Rain service unavailable / retrying"); return; }
        lvgl_port_lock(-1);
        const bool latest=!ui::rainCount || ui::rainFrame==ui::rainCount-1;
        const uint32_t selected=ui::rainCount?ui::rainTimes[ui::rainFrame]:0;
        timeline=next; ui::rainCount=next.count; ui::rainFrame=next.count-1;
        for(unsigned i=0;i<next.count;++i) { ui::rainTimes[i]=next.frames[i].time; if(!latest && next.frames[i].time==selected) ui::rainFrame=i; }
        lvgl_port_unlock(); return;
    }
    if(!timeline.count) return;
    frame=std::max(0,std::min(int(timeline.count)-1,frame));
    const auto &selected=timeline.frames[frame];
    if(active && (range!=jobRange || selected.time!=jobTime)) cancel();
    if(!active) {
        lvgl_port_lock(-1); const bool ready=ui::rainReady && ui::rainRange==range && ui::rainShown==selected.time; lvgl_port_unlock();
        if(ready) return;
        if(std::abs(homeLat)>85) { fail("Radar unavailable at this latitude"); return; }
        lookup=static_cast<uint32_t*>(heap_caps_malloc(420*420*4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        working=static_cast<uint8_t*>(heap_caps_calloc(420*420,3,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!coverage) coverage=static_cast<uint8_t*>(heap_caps_malloc(420*420,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!lookup || !working || !coverage) { fail("Rain radar memory unavailable"); return; }
        jobRange=range; jobFrame=frame; jobTime=selected.time; jobZoom=rain::zoom(homeLat,sky::ranges[range]);
        row=tileCount=tileIndex=0; coverageStep=coverageRange!=range;
        if(coverageStep) coverageRange=-1; // A cancelled partial mask must never be reused.
        active=true; message("Loading radar frame...");
    }
    if(row<420) {
        const unsigned end=std::min(row+12,420u);
        for(;row<end;++row) for(unsigned x=0;x<420;++x) {
            auto &entry=lookup[row*420+x]; entry=UINT32_MAX;
            if(!standalone::inMap(x,row)) continue;
            const auto p=standalone::mapPixel(homeLat,homeLon,sky::ranges[range],jobZoom,x,row);
            unsigned i=0; while(i<tileCount && (tiles[i].x!=p.x/256 || tiles[i].y!=p.y/256)) ++i;
            if(i==tileCount) { if(tileCount==16) { fail("Radar area too large"); return; } tiles[tileCount++]={p.x/256,p.y/256}; }
            entry=(i<<16)|((p.y%256)*256+p.x%256);
        }
        return;
    }
    if(tileIndex<tileCount) {
        const auto t=tiles[tileIndex]; uint8_t *rgba=nullptr;
        const String path=coverageStep?"/v2/coverage/0":String(timeline.frames[jobFrame].path);
        const String url="https://tilecache.rainviewer.com"+path+"/256/"+String(jobZoom)+"/"+String(t.x)+"/"+String(t.y)+(coverageStep?"/0/0_0.png":"/2/1_0.png");
        if(!tilePNG(url,&rgba)) { free(rgba); fail(coverageStep?"Radar coverage unavailable":"Radar image unavailable / retrying"); return; }
        for(unsigned i=0;i<420*420;++i) {
            if(lookup[i]==UINT32_MAX || (lookup[i]>>16)!=tileIndex) continue;
            const auto *p=rgba+(lookup[i]&65535)*4;
            if(coverageStep) { coverage[i]=p[3]; continue; }
            const auto sample=rain::overlay(p,coverage[i],i%420,i/420);
            const auto color=lv_color_make(sample.r,sample.g,sample.b);
            memcpy(working+i*3,&color,2); working[i*3+2]=sample.a;
        }
        free(rgba); ++tileIndex;
        if(tileIndex==tileCount && coverageStep) { coverageRange=range; coverageStep=false; tileIndex=0; }
        return;
    }
    lvgl_port_lock(-1);
    if(ui::infoView==6 && !ui::settings && ui::model.rangeIndex==range && ui::rainFrame==jobFrame && !ui::asleep.load()) {
        lv_img_cache_invalidate_src(&ui::rainImage); free(display); display=working; working=nullptr;
        ui::rainImage.header.cf=LV_IMG_CF_TRUE_COLOR_ALPHA; ui::rainImage.header.w=420; ui::rainImage.header.h=420;
        ui::rainImage.data_size=420*420*3; ui::rainImage.data=display;
        ui::rainReady=true; ui::rainRange=range; ui::rainShown=jobTime; ui::rainMessage[0]=0;
    }
    lvgl_port_unlock(); cancel();
}
}
