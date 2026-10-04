#pragma once
// Included by main.cpp after network helpers. All model/UI access holds LVGL lock.
bool apiAuthorized() {
    if(!integrationEnabled || apiToken.isEmpty() || server.header("X-EchoScope-Token")!=apiToken) {
        server.send(403,"text/plain","Integration disabled or invalid token"); return false;
    }
    server.sendHeader("Cache-Control","no-store"); return true;
}
const char *devicePage() {
    if(ui::settings) return "setup";
    if(ui::infoMenu) return "information";
    const char *pages[]={"radar","weather","family","airports","stargazing","logbook"};
    if(ui::infoView>0 && ui::infoView<6) return pages[ui::infoView];
    if(ui::satelliteView) return "stations";
    return ui::model.details?(ui::routePage?"route":"aircraft"):"radar";
}
void apiState() {
    if(!apiAuthorized()) return;
    JsonDocument doc;
    doc["protocol"]=1; doc["id"]=WiFi.macAddress(); doc["version"]="0.16.0-dev.1";
    doc["lat"]=homeLat; doc["lon"]=homeLon;
    doc["family_flight"]=familyFlight; doc["family_callsign"]=familyCallsign; doc["family_arrival"]=familyArrival;
    doc["pickup_km"]=pickupKm; doc["satellite_alerts"]=satelliteAlerts;
    lvgl_port_lock(-1);
    const uint32_t now=millis();
    doc["page"]=devicePage(); doc["screen"]=!ui::asleep.load(); doc["brightness"]=brightness;
    doc["pickup"]=ui::pickupArmed; doc["selected"]=ui::model.selected;
    doc["demo"]=ui::model.demo; doc["range_km"]=ui::model.range();
    auto pages=doc["pages"].to<JsonArray>(); pages.add("radar");
    if(ui::model.selection()) { pages.add("aircraft"); if(ui::routeAvailable()) pages.add("route"); }
    if(ui::infoAvailable()) pages.add("information");
    if(ui::satellitesEnabled) pages.add("stations");
    const char *names[]={"","weather","family","airports","stargazing","logbook"};
    for(int i=1;i<6;++i) if(ui::infoOption(i)) pages.add(names[i]);
    auto list=doc["aircraft"].to<JsonArray>();
    if(!ui::model.demo) for(size_t i=0;i<ui::model.data.count;++i) {
        const auto &a=ui::model.data.aircraft[i]; const float age=sky::age(a,now);
        if(age>60) continue;
        auto p=list.add<JsonObject>();
        p["hex"]=a.hex; p["callsign"]=a.callsign; p["registration"]=a.registration; p["type"]=a.type;
        p["description"]=a.description; p["age_s"]=age; p["distance_km"]=sky::distance(a.position);
        p["east_km"]=a.position.east; p["north_km"]=a.position.north;
        p["bearing"]=sky::bearing(a.position); p["military"]=a.military; p["helicopter"]=a.kind==sky::AircraftKind::Rotorcraft;
        p["watched"]=sky::watched(a,ui::model.watches); p["visible"]=ui::model.visible(a,now);
        if(std::isfinite(a.altitude)) p["altitude_ft"]=a.altitude;
        if(std::isfinite(a.speed)) p["speed_kt"]=a.speed;
    }
    lvgl_port_unlock();
    String body; serializeJson(doc,body); server.send(200,"application/json",body);
}
void apiControl() {
    if(!apiAuthorized()) return;
    if(server.arg("plain").length()>512) { server.send(413,"text/plain","Command too large"); return; }
    JsonDocument doc;
    if(deserializeJson(doc,server.arg("plain")) || !doc.is<JsonObject>() || doc.size()!=1) { server.send(400,"text/plain","One JSON command required"); return; }
    int code=200; const char *message="OK";
    lvgl_port_lock(-1);
    const uint32_t now=millis();
    if(ui::settings || ui::pageAnimating || updateAccepted) { code=409; message="Device busy; retry after setup/transition"; }
    else if(doc["brightness"].is<int>() && doc["brightness"].as<int>()>=5 && doc["brightness"].as<int>()<=100) {
        brightness=doc["brightness"].as<unsigned>(); applyBrightness(); // Runtime only: avoid NVS wear.
    } else if(doc["screen"].is<bool>()) {
        if(doc["screen"].as<bool>()) wakeForInput(now);
        else { ui::pickupArmed=false; ui::notificationUntil=0; ui::activity.sleepExplicitly(); ui::asleep=true; board->getLCD()->setDisplayOnOff(false); }
    } else if(doc["pickup"].is<bool>()) {
        if(doc["pickup"].as<bool>() && (familyFlight.isEmpty() || familyArrival.isEmpty() || !remoteFlights)) { code=409; message="Configure family flight and arrival airport first"; }
        else { ui::pickupArmed=doc["pickup"].as<bool>(); ui::pickupStarted=now; if(ui::pickupArmed) { wakeForInput(now); ui::infoMenu=false; ui::satelliteView=false; ui::infoView=2; ui::infoCount=0; ui::infoNeedsFetch=true; } }
    } else if(doc["notify"].is<JsonObjectConst>()) {
        const char *value=doc["notify"]["message"] | "";
        const int64_t countdown=doc["notify"]["countdown"] | int64_t(0);
        const int64_t seconds=countdown-int64_t(time(nullptr));
        if(strlen(value)>120 || !value[0] || (countdown && (seconds < -60 || seconds>180))) { code=400; message="Invalid notification"; }
        else { wakeForInput(now); snprintf(ui::notification,sizeof(ui::notification),"%s",value); ui::notificationUntil=now+(countdown?uint32_t(std::max(int64_t(0),seconds)+30)*1000:30000); ui::notificationCountdown=countdown?now+uint32_t(std::max(int64_t(0),seconds))*1000:0; }
    } else if(doc["aircraft"].is<const char*>()) {
        const char *hex=doc["aircraft"]; int found=-1;
        for(size_t i=0;i<ui::model.data.count;++i) if(!strcmp(hex,ui::model.data.aircraft[i].hex) && sky::age(ui::model.data.aircraft[i],now)<=60) found=int(i);
        if(found<0 || ui::model.demo) { code=404; message="Aircraft no longer has a fresh position"; }
        else { wakeForInput(now); ui::notificationUntil=0; ui::model.select(found); ui::model.details=true; ui::routePage=false; ui::infoView=0; ui::infoMenu=false; ui::satelliteView=false; }
    } else if(doc["page"].is<const char*>()) {
        String page=doc["page"].as<const char*>(); if(page=="highlights") page="logbook"; int view=0;
        const char *names[]={"","weather","family","airports","stargazing","logbook"};
        for(int i=1;i<6;++i) if(page==names[i] && ui::infoOption(i)) view=i;
        const bool valid=view || page=="radar" || (page=="information" && ui::infoAvailable()) || (page=="stations" && ui::satellitesEnabled) || (page=="aircraft" && ui::model.selection()) || (page=="route" && ui::routeAvailable());
        if(!valid) { code=404; message="Page unavailable"; }
        else {
            wakeForInput(now); ui::notificationUntil=0; ui::infoMenu=page=="information"; ui::satelliteView=page=="stations";
            ui::infoView=view; ui::infoCount=0; ui::infoPage=0; ui::infoNeedsFetch=view!=0;
            ui::model.details=page=="aircraft" || page=="route"; ui::routePage=page=="route";
            if(auto *a=ui::model.selection()) { snprintf(ui::detailHex,sizeof(ui::detailHex),"%s",a->hex); snprintf(ui::detailCall,sizeof(ui::detailCall),"%s",a->callsign); }
        }
    } else { code=400; message="Invalid command or value"; }
    lvgl_port_unlock(); server.send(code,"text/plain",message);
}
