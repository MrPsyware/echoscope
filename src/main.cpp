#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_display_panel.hpp>
#include <esp_heap_caps.h>
#include <atomic>
#include <ctime>
#include "lvgl_v8_port.h"
#include "aircraft_json.h"
#include "radar_ui.h"
#include "api_ca.h"
#include "feed_tls_client.h"

SET_LOOP_TASK_STACK_SIZE(32 * 1024);
namespace {
Preferences prefs;
WebServer server(80);
DNSServer dns;
esp_panel::board::Board *board;
String ssid,password,csrf;
double homeLat=0,homeLon=0;
bool configured=false,portal=false;
uint32_t portalStarted=0,nextFetch=0,nextReconnect=0;
uint32_t retryDelay=5000;
std::atomic<bool> requestPortal{false};
portMUX_TYPE encoderMux=portMUX_INITIALIZER_UNLOCKED;
volatile int encoderSteps=0;
volatile uint8_t encoderPrevious=0;
sky::Snapshot incoming;

void IRAM_ATTR encoderISR() {
    const uint8_t current=(digitalRead(6)<<1)|digitalRead(5);
    static const int8_t table[16]={0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
    portENTER_CRITICAL_ISR(&encoderMux);
    encoderSteps+=table[(encoderPrevious<<2)|current]; encoderPrevious=current;
    portEXIT_CRITICAL_ISR(&encoderMux);
}
void status(const char *message) {
    static char previous[80]{};
    if(std::strcmp(previous,message)) { Serial.printf("[network] %s\n",message); snprintf(previous,sizeof(previous),"%s",message); }
    lvgl_port_lock(-1); snprintf(ui::status,sizeof(ui::status),"%s",message); lvgl_port_unlock();
}
void controls(lv_timer_t *) {
    constexpr int transitionsPerDetent=2; // This knob has two quadrature edges per physical click.
    static int remainder=0;
    static bool lastRaw=false,pressed=false,longSent=false;
    static uint32_t changed=0,downAt=0;
    const uint32_t now=millis();
    portENTER_CRITICAL(&encoderMux); remainder+=encoderSteps; encoderSteps=0; portEXIT_CRITICAL(&encoderMux);
    if(std::abs(remainder)>=transitionsPerDetent) { if(!ui::settings) ui::model.rotate(remainder/transitionsPerDetent,now); remainder%=transitionsPerDetent; }
    bool raw=digitalRead(0)==LOW;
    if(raw!=lastRaw) { changed=now; lastRaw=raw; }
    if(raw!=pressed && uint32_t(now-changed)>30) {
        pressed=raw;
        if(pressed) { downAt=now; longSent=false; ui::input.buttonBegin(now); }
        else ui::input.buttonEnd(now,longSent);
    }
    if(ui::input.takeClick(now)) { if(ui::settings) ui::settings=false; else ui::model.press(now); }
    if(pressed && !longSent && uint32_t(now-downAt)>=1500) { longSent=true; requestPortal=true; }
}
void touch(lv_event_t *event) {
    const auto code=lv_event_get_code(event); const uint32_t now=millis();
    if(code==LV_EVENT_PRESSED) { ui::input.touchBegin(now); return; }
    if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) { ui::input.touchEnd(now); return; }
    if(code!=LV_EVENT_SHORT_CLICKED) return;
    lv_indev_t *input=lv_indev_get_act(); if(!input) return;
    lv_point_t p; lv_indev_get_point(input,&p);
    lv_area_t area; lv_obj_get_coords(ui::canvas,&area);
    ui::tap(p.x-area.x1,p.y-area.y1,millis());
}
void demoFrame(uint32_t now) {
    incoming.count=6;
    const char *calls[]={"DEMO101","DEMO202","DEMO303","DEMO404","DEMO505","DEMO606"};
    for(size_t i=0;i<incoming.count;++i) {
        sky::Aircraft a;
        snprintf(a.hex,sizeof(a.hex),"demo%u",unsigned(i));
        snprintf(a.callsign,sizeof(a.callsign),"%s",calls[i]);
        snprintf(a.type,sizeof(a.type),"%s",i%2?"B738":"A320");
        const float angle=float(i)*1.0472f+now/180000.0f;
        const float r=4+i*3;
        a.position={std::sin(angle)*r,std::cos(angle)*r}; a.track=std::fmod(angle*180/sky::pi+90,360);
        a.altitude=7500+i*4000; a.speed=240+i*30; a.received=now;
        incoming.aircraft[i]=a;
    }
    lvgl_port_lock(-1); ui::model.ingest(incoming,now); lvgl_port_unlock();
}
String escape(const String &input) {
    String s=input; s.replace("&","&amp;"); s.replace("<","&lt;"); s.replace(">","&gt;"); s.replace("\"","&quot;"); s.replace("'","&#39;"); return s;
}
void setupPage() {
    if(!portal) { server.send(403,"text/plain","Hold the knob for 1.5 seconds to enable setup."); return; }
    String page=R"HTML(<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><title>EchoScope setup</title>
<style>body{background:#071918;color:#e8f8f4;font:17px system-ui;max-width:440px;margin:40px auto;padding:24px}h1{color:#68f3ae}label{display:block;margin:22px 0 6px}input,button{box-sizing:border-box;width:100%;padding:13px;border-radius:8px;border:1px solid #52716a;font:inherit}button{background:#68f3ae;margin-top:26px}p{line-height:1.5}</style>
<h1>EchoScope</h1><p>Choose your home Wi-Fi and the centre of your radar. Coordinates are decimal degrees; west and south are negative.</p><form method="post" action="/save">
)HTML";
    page+="<input type='hidden' name='token' value='"+csrf+"'>";
    page+="<label>Wi-Fi name (2.4 GHz)</label><input name='ssid' maxlength='32' required value='"+escape(ssid)+"'>";
    page+="<label>Wi-Fi password</label><input name='password' type='password' maxlength='63' autocomplete='new-password' placeholder='Leave blank to keep saved password'>";
    page+="<label>Latitude</label><input name='lat' type='number' step='any' min='-90' max='90' required value='"+(configured?String(homeLat,6):String(""))+"'>";
    page+="<label>Longitude</label><input name='lon' type='number' step='any' min='-180' max='180' required value='"+(configured?String(homeLon,6):String(""))+"'>";
    page+="<button>Save and start radar</button></form><p>Live aircraft data: adsb.fi. Hold the knob to reopen setup. Settings stay on this device.</p>";
    server.sendHeader("Cache-Control","no-store"); server.send(200,"text/html",page);
}
bool coordinate(const String &s,double min,double max,double &value) {
    if(!s.length()) return false;
    char *end; value=strtod(s.c_str(),&end);
    return end!=s.c_str() && *end==0 && std::isfinite(value) && value>=min && value<=max;
}
void saveSetup() {
    if(!portal || server.arg("token")!=csrf) { server.send(403,"text/plain","Reopen the setup page and try again."); return; }
    String newSSID=server.arg("ssid"), newPassword=server.arg("password");
    double lat,lon;
    if(!newSSID.length() || newSSID.length()>32 || newPassword.length()>63 || !coordinate(server.arg("lat"),-90,90,lat) || !coordinate(server.arg("lon"),-180,180,lon)) {
        server.send(400,"text/plain","Check Wi-Fi name and decimal latitude/longitude."); return;
    }
    if(!newPassword.length() && newSSID==ssid) newPassword=password;
    if(newPassword.length() && newPassword.length()<8) { server.send(400,"text/plain","Wi-Fi password must be at least 8 characters."); return; }
    ssid=newSSID; password=newPassword; homeLat=lat; homeLon=lon; configured=true;
    prefs.putString("ssid",ssid); prefs.putString("pass",password); prefs.putDouble("lat",lat); prefs.putDouble("lon",lon); prefs.putBool("set",true);
    server.send(200,"text/html","<meta name='viewport' content='width=device-width'><h1>Settings saved</h1><p>The knob is connecting. If it cannot connect, setup remains available. Press the knob to view the radar.</p>");
    WiFi.begin(ssid.c_str(),password.c_str()); nextReconnect=millis()+20000; nextFetch=millis(); retryDelay=5000;
    lvgl_port_lock(-1); ui::model.reset(); ui::model.demo=false; ui::settings=false; lvgl_port_unlock();
    status("Connecting to Wi-Fi");
}
void openPortal() {
    if(!portal) {
        WiFi.mode(WIFI_AP_STA);
        if(!WiFi.softAP("EchoScope-Setup",ui::setupPassword)) { status("Setup Wi-Fi failed"); return; }
        dns.start(53,"*",WiFi.softAPIP()); portal=true;
    }
    portalStarted=millis();
    lvgl_port_lock(-1); ui::settings=true; lvgl_port_unlock();
}
void fetch() {
    if(time(nullptr)<1700000000) { status("Waiting for clock sync"); nextFetch=millis()+5000; return; }
    IPAddress address;
    if(WiFi.hostByName("opendata.adsb.fi",address)!=1) {
        status("DNS lookup failed");
        retryDelay=std::min(uint32_t(120000),retryDelay*2); nextFetch=millis()+retryDelay; return;
    }
    Serial.printf("[feed] Connecting; UTC=%lld, free heap=%u, largest internal block=%u\n",
                  (long long)time(nullptr),ESP.getFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    Serial.printf("[feed] DNS IPv4=%s, RSSI=%d dBm\n",address.toString().c_str(),WiFi.RSSI());
    FeedTLSClient client; client.setCACert(apiRootCA); client.setHandshakeTimeout(15);
    HTTPClient http; http.setConnectTimeout(10000); http.setTimeout(5000); http.useHTTP10(true);
    String url="https://opendata.adsb.fi/api/v3/lat/"+String(homeLat,6)+"/lon/"+String(homeLon,6)+"/dist/54";
    if(!http.begin(client,url)) { status("Cannot open data feed"); nextFetch=millis()+15000; return; }
    const int code=http.GET();
    bool ok=false;
    if(code==200) {
        // Filter while streaming to keep JSON memory independent of unused API fields.
        JsonDocument filter; const char *fields[]={"hex","flight","r","t","lat","lon","alt_baro","alt_geom","gs","track","baro_rate","seen_pos"};
        for(auto field:fields) filter["ac"][0][field]=true;
        JsonDocument doc;
        // Read at most 512 KiB and within a fixed deadline, including interrupted responses.
        String body; body.reserve(32768);
        auto *stream=http.getStreamPtr(); const uint32_t start=millis(); const int length=http.getSize();
        while(uint32_t(millis()-start)<8000 && body.length()<512*1024 && (http.connected() || stream->available())) {
            uint8_t buffer[512]; const int available=stream->available();
            if(available>0) {
                int n=stream->read(buffer,std::min(available,int(sizeof(buffer))));
                if(n>0) body.concat(reinterpret_cast<const char*>(buffer),n);
            } else delay(5);
            if(length>=0 && body.length()>=unsigned(length)) break;
        }
        const bool complete=body.length()<512*1024 && (length>=0 ? body.length()==unsigned(length) : !http.connected());
        const auto error=complete?deserializeJson(doc,body,DeserializationOption::Filter(filter)):DeserializationError(DeserializationError::IncompleteInput);
        if(!error && sky::parseAircraft(doc,incoming,homeLat,homeLon,millis())) {
            lvgl_port_lock(-1); ui::model.ingest(incoming,millis()); lvgl_port_unlock(); ok=true; status("Live positions");
        } else status("Invalid aircraft response");
    } else {
        char msg[80];
        if(code<0) {
            char tlsMessage[192]{}; const int tlsError=client.lastError(tlsMessage,sizeof(tlsMessage));
            Serial.printf("[feed] HTTP client %d (%s); TLS %d (%s)\n",code,HTTPClient::errorToString(code).c_str(),tlsError,tlsMessage);
            if(tlsError==-80) snprintf(msg,sizeof(msg),"TLS connection reset");
            else if(tlsError==-0x2700) snprintf(msg,sizeof(msg),"TLS certificate rejected");
            else if(tlsError!=0 && tlsError!=-1) snprintf(msg,sizeof(msg),"TLS error %d",tlsError);
            else snprintf(msg,sizeof(msg),"Connection failed (%d)",code);
        } else {
            snprintf(msg,sizeof(msg),"Data feed HTTP %d",code);
            Serial.printf("[feed] Server returned HTTP %d\n",code);
        }
        status(msg);
    }
    http.end();
    static bool controlTestDone=false;
    if(code<0 && !controlTestDone) {
        controlTestDone=true;
        // One handshake per boot, no HTTP request or location data to the control host.
        FeedTLSClient control; control.setCACert(apiRootCA); control.setHandshakeTimeout(15);
        Serial.println("[control] Testing verified TLS to pki.goog");
        if(control.connect("pki.goog",443,10000)) Serial.println("[control] HTTPS handshake works to other host");
        else { char error[160]; int n=control.lastError(error,sizeof(error)); Serial.printf("[control] Failed: %d %s\n",n,error); }
        control.stop();
    }
    retryDelay=ok?5000:std::min(uint32_t(120000),retryDelay*2);
    if(code==429) retryDelay=120000;
    nextFetch=millis()+retryDelay;
}
}

void setup() {
    Serial.begin(115200);
    Serial.println("EchoScope 0.2.3 / build tooling and project rename");
    Serial.printf("[tasks] Network core=%d, LVGL core=%d\n",xPortGetCoreID(),LVGL_PORT_TASK_CORE);
    // Keep the original NVS namespace so existing Wi-Fi/location survive updates.
    prefs.begin("sky-knob",false);
    configured=prefs.getBool("set",false); ssid=prefs.getString("ssid"); password=prefs.getString("pass");
    homeLat=prefs.getDouble("lat",0); homeLon=prefs.getDouble("lon",0);
    snprintf(ui::setupPassword,sizeof(ui::setupPassword),"echo-%08lx",(unsigned long)esp_random());
    csrf=String(esp_random(),HEX)+String(esp_random(),HEX);
    board=new esp_panel::board::Board(); board->init(); assert(board->begin());
    assert(lvgl_port_init(board->getLCD(),board->getTouch()));
    auto *pixels=(lv_color_t*)heap_caps_malloc(ui::size*ui::size*sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    assert(pixels);
    auto *histories=static_cast<sky::Trail*>(heap_caps_malloc(sizeof(sky::Trail)*sky::maxAircraft,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    assert(histories);
    for(size_t i=0;i<sky::maxAircraft;++i) new (histories+i) sky::Trail{};
    lvgl_port_lock(-1);
    lv_obj_set_style_bg_color(lv_scr_act(),lv_color_black(),0);
    ui::model.trails=histories;
    ui::canvas=lv_canvas_create(lv_scr_act()); lv_canvas_set_buffer(ui::canvas,pixels,ui::size,ui::size,LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(ui::canvas); lv_obj_add_flag(ui::canvas,LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(ui::canvas,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ui::canvas,touch,LV_EVENT_ALL,nullptr);
    ui::model.demo=!configured;
    lv_timer_create([](lv_timer_t *timer){
        const uint32_t started=millis();
        ui::render(started);
        const uint32_t renderMs=millis()-started;
        // LVGL timestamps a timer before its callback. Account for actual draw
        // cost so a slow frame cannot make the next one immediately overdue.
        lv_timer_set_period(timer,std::max(uint32_t(200),renderMs+50));
        static uint32_t lastLog=0;
        if(uint32_t(started-lastLog)>30000) {
            Serial.printf("[display] core=%d, render=%lu ms, period=%lu ms\n",xPortGetCoreID(),
                          (unsigned long)renderMs,(unsigned long)std::max(uint32_t(200),renderMs+50));
            lastLog=started;
        }
    },200,nullptr);
    lv_timer_create(controls,10,nullptr);
    lvgl_port_unlock();
    pinMode(6,INPUT_PULLUP); pinMode(5,INPUT_PULLUP); pinMode(0,INPUT_PULLUP);
    encoderPrevious=(digitalRead(6)<<1)|digitalRead(5);
    attachInterrupt(digitalPinToInterrupt(6),encoderISR,CHANGE); attachInterrupt(digitalPinToInterrupt(5),encoderISR,CHANGE);
    WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
    configTime(0,0,"pool.ntp.org","time.google.com");
    server.on("/",HTTP_GET,setupPage); server.on("/save",HTTP_POST,saveSetup); server.onNotFound(setupPage); server.begin();
    if(configured) { WiFi.begin(ssid.c_str(),password.c_str()); status("Connecting to Wi-Fi"); }
    else { demoFrame(millis()); openPortal(); }
}
void loop() {
    const uint32_t now=millis();
    if(requestPortal.exchange(false)) openPortal();
    server.handleClient(); if(portal) dns.processNextRequest();
    if(!configured) {
        static uint32_t lastDemo=0;
        if(uint32_t(now-lastDemo)>1000) { demoFrame(now); lastDemo=now; }
    } else if(WiFi.status()==WL_CONNECTED) {
        if(portal && uint32_t(now-portalStarted)>300000) { dns.stop(); WiFi.softAPdisconnect(true); portal=false; lvgl_port_lock(-1); ui::settings=false; lvgl_port_unlock(); }
        if(int32_t(now-nextFetch)>=0) fetch();
    } else {
        status("Wi-Fi disconnected");
        if(int32_t(now-nextReconnect)>=0) { WiFi.begin(ssid.c_str(),password.c_str()); nextReconnect=now+20000; }
        // Keep credentials; automatically make recovery available after a failed connection.
        if(!portal && now>30000) openPortal();
    }
    delay(5);
}
