#pragma once
// Included inside the network-task namespace after infoJSON/readFeedBody.
standalone::Map localMap;
String localMapKey;
struct StandaloneAllocator : ArduinoJson::Allocator {
    void *allocate(size_t n) override { return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
    void deallocate(void *p) override { free(p); }
    void *reallocate(void *p,size_t n) override { return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
} standaloneAllocator;
JsonDocument weatherCache(&standaloneAllocator);
double weatherLat=NAN,weatherLon=NAN;
uint32_t weatherAttempt=0;
struct RouteCache { String call; JsonDocument data{&standaloneAllocator}; uint32_t until=0; bool ok=false; } routeCache[8];
unsigned routeSlot=0;
standalone::PhotoCache localPhotos;
uint32_t nextLocalPhoto=0;


void resetStandalone() {
    localPhotos.clear(); nextLocalPhoto=0;
    localMap.cancel(); localMapKey=""; weatherCache.clear(); weatherLat=weatherLon=NAN; weatherAttempt=0;
    for(auto &cached:routeCache) { cached.call=""; cached.data.clear(); cached.until=0; }
}
bool publicJSON(const String &url,JsonDocument &doc,int *httpStatus=nullptr) {
    if(httpStatus) *httpStatus=0;
    if(time(nullptr)<1700000000) return false;
    FeedTLSClient client; const String roots=String(apiRootCA)+standaloneRootCA;
    client.setCACert(roots.c_str()); client.setHandshakeTimeout(5);
    HTTPClient http; http.setConnectTimeout(2500); http.setTimeout(4000); http.useHTTP10(true);
    http.setUserAgent("EchoScope-standalone-dev (+https://github.com/MrPsyware/echoscope)");
    http.begin(client,url); const int code=http.GET(); bool ok=false;
    if(httpStatus) *httpStatus=code;
    if(code==200) {
        auto body=readFeedBody(http,64*1024,5000);
        const auto error=deserializeJson(doc,body.text.c_str(),body.text.length());
        ok=body.complete && !error && !doc.overflowed();
        if(!ok) deviceLog.printf("[standalone] JSON=%s complete=%d bytes=%u\n",error.c_str(),body.complete,unsigned(body.text.length()));
    }
    deviceLog.printf("[standalone] %s HTTP=%d\n",url.substring(0,url.indexOf('/',8)).c_str(),code);
    http.end(); return ok;
}
standalone::PhotoEntry *localPhoto(const char *reg) {
    using standalone::PhotoState;
    auto *entry=localPhotos.select(reg,millis());
    if(!entry || (entry->state!=PhotoState::Lookup && entry->state!=PhotoState::Image)) return entry;
    if(time(nullptr)<1700000000 || int32_t(millis()-nextLocalPhoto)<0 || ui::asleep.load()) return entry;
    // At most one bounded network operation per loop, with a global throttle.
    // A due live feed runs before the next metadata/image stage.
    int code=0;
    if(entry->state==PhotoState::Lookup) {
        JsonDocument doc(&standaloneAllocator);
        if(publicJSON(String("https://api.planespotters.net/pub/photos/reg/")+reg,doc,&code)) {
            auto photos=doc["photos"].as<JsonArrayConst>();
            if(!photos.isNull() && !photos.size()) entry->finish(PhotoState::Missing,millis());
            else if(standalone::photoMetadata(photos[0],entry->meta)) entry->state=PhotoState::Image;
            else entry->finish(PhotoState::Failed,millis());
        } else entry->finish(code==404?PhotoState::Missing:PhotoState::Failed,millis());
        deviceLog.printf("[photo] standalone metadata %s HTTP=%d state=%d\n",reg,code,int(entry->state));
    } else {
        FeedTLSClient client; const String roots=String(apiRootCA)+standaloneRootCA;
        client.setCACert(roots.c_str()); client.setHandshakeTimeout(5);
        HTTPClient http; http.useHTTP10(true); http.setConnectTimeout(2500); http.setTimeout(4000);
        http.setUserAgent("EchoScope-standalone-dev (+https://github.com/MrPsyware/echoscope)");
        http.begin(client,entry->meta.url); code=http.GET(); bool ok=false;
        if(code==200) {
            auto body=readFeedBody(http,standalone::photoDownloadLimit,4000);
            if(body.complete) {
                entry->packet.reset(static_cast<uint8_t*>(allocateFeedBytes(sky::photoMaxBytes)));
                ok=standalone::decodePhoto(reinterpret_cast<const uint8_t*>(body.text.c_str()),body.text.length(),entry->meta,
                    entry->packet.get(),sky::photoMaxBytes,entry->size);
            }
            deviceLog.printf("[photo] JPEG bytes=%u complete=%d decoded=%d\n",unsigned(body.text.length()),body.complete,ok);
        }
        http.end(); entry->finish(ok?PhotoState::Ready:PhotoState::Failed,millis());
        deviceLog.printf("[photo] standalone image %s HTTP=%d %s\n",reg,code,ok?"ready":"unavailable");
    }
    nextLocalPhoto=millis()+(code==429?900000:1500);
    return entry;
}
bool localWeather(JsonDocument &out) {
    const bool same=homeLat==weatherLat && homeLon==weatherLon;
    if(same && int32_t(millis()-weatherAttempt)<0) {
        if(weatherCache.isNull()) return false;
        out.set(weatherCache); return true;
    }
    weatherLat=homeLat; weatherLon=homeLon; weatherAttempt=millis()+60000;
    weatherCache.clear();
    JsonDocument data(&standaloneAllocator);
    const String url="https://api.open-meteo.com/v1/forecast?latitude="+String(homeLat,4)+"&longitude="+String(homeLon,4)+
        "&timezone=GMT&timeformat=unixtime&forecast_days=4&hourly=temperature_2m,weather_code,cloud_cover,precipitation_probability,wind_speed_10m,is_day&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,wind_speed_10m_max";
    const bool ok=publicJSON(url,data) && standalone::weatherPages(data.as<JsonVariantConst>(),weatherCache,time(nullptr));
    if(ok) { weatherAttempt=millis()+900000; out.set(weatherCache); }
    return ok;
}
bool localRoute(const String &call,JsonDocument &out) {
    // Callsigns come from an external feed; never interpolate arbitrary paths.
    if(call.length()<2 || call.length()>10) return false;
    for(unsigned i=0;i<call.length();++i) if(!isalnum(static_cast<unsigned char>(call[i]))) return false;
    for(auto &cached:routeCache) if(cached.call==call && int32_t(millis()-cached.until)<0) { out.set(cached.data); return cached.ok; }
    JsonDocument data(&standaloneAllocator);
    auto &cached=routeCache[routeSlot++%8]; cached.call=call; cached.data.clear();
    cached.ok=publicJSON("https://api.adsbdb.com/v0/callsign/"+call,data) && standalone::routePages(data.as<JsonVariantConst>(),call.c_str(),cached.data,time(nullptr));
    cached.until=millis()+(cached.ok?21600000:60000);
    out.set(cached.data); return cached.ok;
}
bool localAirports(JsonDocument &out,bool overlay,int rangeIndex,const String &target="") {
    standalone::NearbyAirport airports[32];
    const unsigned count=standalone::nearby(homeLat,homeLon,customAirports,builtInAirports,airports,overlay?32:9,overlay?sky::ranges[rangeIndex]:300,target.c_str());
    out.clear(); out["generated"]=int64_t(time(nullptr)); out["source"]="OurAirports / built-in + custom";
    auto list=out[overlay?"airports":"pages"].to<JsonArray>();
    for(unsigned i=0;i<count;++i) {
        const auto &a=airports[i].airport; auto p=list.add<JsonObject>();
        if(overlay) { p["code"]=a.code; p["lat"]=a.lat; p["lon"]=a.lon; p["size"]=0; continue; }
        p["item"]=a.code; p["title"]=a.code; auto lines=p["lines"].to<JsonArray>(); lines.add(a.name);
        char distance[64]; snprintf(distance,sizeof(distance),"%.0f km from home",airports[i].km); lines.add(distance);
        lines.add(a.icao); lines.add("Turn page for approach radar");
        if(!a.count) lines.add("Runways unavailable");
        auto detail=p["airport"].to<JsonObject>(); detail["lat"]=a.lat; detail["lon"]=a.lon;
        auto runways=detail["runways"].to<JsonArray>();
        for(unsigned j=0;j<a.count;++j) {
            const auto &r=a.runways[j]; auto runway=runways.add<JsonObject>(); runway["name"]=r.name;
            auto ends=runway["ends"].to<JsonArray>(); ends.add(r.lat1); ends.add(r.lon1); ends.add(r.lat2); ends.add(r.lon2);
        }
    }
    return true;
}
bool downloadTile(const String &url,File &file) {
    FeedTLSClient client; const String roots=String(apiRootCA)+standaloneRootCA;
    client.setCACert(roots.c_str()); client.setHandshakeTimeout(5);
    HTTPClient http; http.useHTTP10(true); http.setConnectTimeout(2500); http.setTimeout(4000);
    http.setUserAgent("EchoScope-standalone-dev (+https://github.com/MrPsyware/echoscope)");
    http.begin(client,url); const int code=http.GET(); bool ok=false;
    if(code==200) {
        auto body=readFeedBody(http,512*1024,5000);
        const auto *bytes=reinterpret_cast<const uint8_t*>(body.text.c_str());
        // Validate size before persistence/decoder allocation.
        LodePNGState state; lodepng_state_init(&state); unsigned w=0,h=0;
        const unsigned error=lodepng_inspect(&w,&h,&state,bytes,body.text.length()); lodepng_state_cleanup(&state);
        ok=body.complete && !error && w==256 && h==256 && file.write(bytes,body.text.length())==body.text.length();
    }
    http.end(); deviceLog.printf("[map] Tile HTTP=%d cached=%d\n",code,ok); return ok;
}
void localMapStep(const String &key,double lat,double lon,double km,int rangeIndex,const String &airport="") {
    if(key!=localMapKey) {
        localMapKey=key; localMap.start(lat,lon,km);
    }
    localMap.step(downloadTile);
    lvgl_port_lock(-1);
    snprintf(ui::mapMessage,sizeof(ui::mapMessage),"%s",localMap.message.c_str());
    const bool matches=airport.isEmpty()?ui::model.rangeIndex==rangeIndex:ui::infoView==3 && ui::airportPage==1 && ui::infoCount && airport==ui::infoPages[ui::infoPage].item;
    if(localMap.complete() && matches && !ui::asleep.load()) {
        lv_img_cache_invalidate_src(&ui::mapImage); lv_img_cache_invalidate_src(&ui::approachMapImage); lv_img_cache_invalidate_src(&ui::logMapImage);
        // LV_COLOR_16_SWAP is handled by lv_color_make; no byte-order assumption.
        for(unsigned i=0;i<420*420;++i) {
            const uint16_t c=localMap.data()[i]; mapPixels[i]=lv_color_make(((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31);
        }
        auto &image=airport.isEmpty()?ui::mapImage:ui::approachMapImage;
        image.header.cf=LV_IMG_CF_TRUE_COLOR; image.header.w=420; image.header.h=420;
        image.data_size=420*420*2; image.data=reinterpret_cast<const uint8_t*>(mapPixels);
        ui::mapReady=airport.isEmpty(); ui::approachMapReady=!airport.isEmpty(); ui::logMapReady=false;
        snprintf(ui::mapCredit,sizeof(ui::mapCredit),"Copyright OpenStreetMap contributors");
        if(airport.isEmpty()) { ui::mapRange=rangeIndex; nextMap=millis()+604800000; requestedApproach=""; }
        else { snprintf(ui::approachMapAirport,sizeof(ui::approachMapAirport),"%s",airport.c_str()); nextApproachMap=millis()+604800000; requestedMapRange=-1; }
    }
    lvgl_port_unlock();
    if(!localMap.active()) {
        if(!localMap.complete()) { if(airport.isEmpty()) nextMap=millis()+300000; else nextApproachMap=millis()+300000; deviceLog.printf("[map] %s\n",localMap.message.c_str()); }
        localMap.cancel(); localMapKey="";
    }
}
