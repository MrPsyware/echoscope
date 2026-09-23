#include "network_policy.h"
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
#include "feed_json.h"
#include "feed_buffer.h"
#include "button_debounce.h"
#include "photo_protocol.h"
#include "photo_config.h"
#include "radar_ui.h"
#include "api_ca.h"
#include "feed_tls_client.h"

SET_LOOP_TASK_STACK_SIZE(32 * 1024);
namespace {
Preferences prefs;
WebServer server(80);
DNSServer dns;
esp_panel::board::Board *board;
String ssid,password,csrf,photoBase;
uint32_t photoRetryAt=0;
char photoAttempt[16]{};
lv_color_t *photoPixels=nullptr;
double homeLat=0,homeLon=0;
bool configured=false,portal=false,apActive=false;
sky::NetworkPolicy networkPolicy;
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
struct ButtonEvent { sky::ButtonDebounce::Event kind; uint32_t at; bool longPress; };
QueueHandle_t buttonQueue=nullptr;
void sampleButton(void *) {
    sky::ButtonDebounce debounce;
    TickType_t next=xTaskGetTickCount();
    for(;;) {
        const uint32_t now=millis();
        const auto kind=debounce.sample(digitalRead(0)==LOW,now);
        if(kind!=sky::ButtonDebounce::None) {
            const ButtonEvent event{kind,now,debounce.longSent};
            // Only three events per gesture; the UI drains these between frames.
            xQueueSend(buttonQueue,&event,portMAX_DELAY);
        }
        vTaskDelayUntil(&next,std::max(TickType_t(1),pdMS_TO_TICKS(5)));
    }
}
bool wakeForInput(uint32_t now,bool touch=false) {
    if(!ui::activity.interact(now,touch)) return false;
    ui::asleep=false; ui::input=sky::InputGate{};
    if(!board->getLCD()->setDisplayOnOff(true)) Serial.println("[power] Display wake command failed");
    ui::requestFeed=true;
    Serial.println("[power] Awake; refreshing aircraft");
    return true;
}
void controls(lv_timer_t *) {
    constexpr int transitionsPerDetent=2; // This knob has two quadrature edges per physical click.
    static int remainder=0;
    static bool wakeButton=false;
    const uint32_t now=millis();
    int steps;
    portENTER_CRITICAL(&encoderMux); steps=encoderSteps; encoderSteps=0; portEXIT_CRITICAL(&encoderMux);
    if(steps && wakeForInput(now)) { steps=0; remainder=0; }
    remainder+=steps;
    if(std::abs(remainder)>=transitionsPerDetent) { if(!ui::settings) ui::model.rotate(remainder/transitionsPerDetent,now); remainder%=transitionsPerDetent; }
    ButtonEvent event;
    while(xQueueReceive(buttonQueue,&event,0)==pdTRUE) {
        if(event.kind==sky::ButtonDebounce::Down) {
            wakeButton=wakeForInput(millis());
            if(!wakeButton) ui::input.buttonBegin(event.at);
        }
        else if(event.kind==sky::ButtonDebounce::Up) {
            wakeForInput(millis());
            if(wakeButton) { wakeButton=false; continue; }
            ui::input.buttonEnd(event.at,event.longPress);
            Serial.printf("[input] Button released; long=%s, touch overlap=%s\n",
                          event.longPress?"yes":"no",(ui::input.touchedDuringPress || ui::input.touching)?"yes":"no");
        } else if(event.kind==sky::ButtonDebounce::Hold && !wakeButton) requestPortal=true;
    }
    if(ui::input.takeClick(millis())) {
        if(ui::settings) ui::settings=false;
        else ui::model.press(now);
        Serial.printf("[input] Click accepted; rotation=%s\n",ui::model.selectMode?"aircraft":"range");
    }
    if(ui::activity.tick(millis(),ui::input.touching || ui::input.buttonHeld || wakeButton)) {
        ui::asleep=true;
        if(!board->getLCD()->setDisplayOnOff(false)) {
            lv_canvas_fill_bg(ui::canvas,lv_color_black(),LV_OPA_COVER);
            Serial.println("[power] Display off command failed; using black screen");
        }
        Serial.println("[power] Sleeping after one hour idle; feed paused");
    }
}
void touch(lv_event_t *event) {
    const auto code=lv_event_get_code(event); const uint32_t now=millis();
    static bool wakeTouch=false;
    if(code==LV_EVENT_PRESSED) { ui::setupOpeningTouch=false; wakeTouch=wakeForInput(now,true); ui::input.touchBegin(now); return; }
    if(code==LV_EVENT_PRESSING) { wakeForInput(now,true); return; }
    if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) { wakeForInput(now,true); ui::input.touchEnd(now); return; }
    if(code!=LV_EVENT_SHORT_CLICKED) return;
    if(wakeTouch) { wakeTouch=false; return; }
    if(ui::setupOpeningTouch) { ui::setupOpeningTouch=false; return; }
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
        snprintf(a.type,sizeof(a.type),"%s",i==0?"H47":i%2?"B738":"C172");
        snprintf(a.description,sizeof(a.description),"%s",i==0?"BOEING CH-47 Chinook":i%2?"BOEING 737-800":"CESSNA 172 Skyhawk");
        a.kind=i==0?sky::AircraftKind::Rotorcraft:i%2?sky::AircraftKind::Large:sky::AircraftKind::Light;
        a.military=i==0;
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
bool photoURL(String &url) {
    url.trim(); while(url.endsWith("/")) url.remove(url.length()-1);
    return sky::validPhotoBase(url.c_str());
}
void photoSource() {
    lvgl_port_lock(-1);
    String credit=ui::photoCredit,link=ui::photoLink,reg=ui::photoReg;
    const bool ready=ui::photoReady;
    lvgl_port_unlock();
    String page="<meta name='viewport' content='width=device-width'><h1>EchoScope photo</h1>";
    if(ready && link.startsWith("https://www.planespotters.net/"))
        page+="<p>"+escape(reg)+" · © "+escape(credit)+" / Planespotters.net</p><p><a href='"+escape(link)+"'>View original photo</a></p>";
    else page+="<p>Open a flight's PHOTO view on the knob first.</p>";
    server.sendHeader("Cache-Control","no-store"); server.send(200,"text/html; charset=utf-8",page);
}
void testPhotoService() {
    if(!portal || server.arg("token")!=csrf) { server.send(403,"text/plain","Reopen setup first"); return; }
    String url=server.arg("photo_url");
    if(!photoURL(url) || url.isEmpty()) { server.send(400,"text/plain","Enter http://SERVER-IP:8086"); return; }
    NetworkClient client; HTTPClient http; http.setConnectTimeout(2000); http.setTimeout(2000);
    http.begin(client,url+"/health"); const int code=http.GET();
    JsonDocument doc; const String body=code==200 && http.getSize()>0 && http.getSize()<512?http.getString():String("");
    const bool ok=!deserializeJson(doc,body) && doc["service"]=="echoscope-photos" && doc["protocol"]==1;
    http.end(); server.send(ok?200:502,"text/plain",ok?"Photo service connected (protocol 1).":"Cannot reach a compatible photo service. Check its LAN IP and port.");
}
void setupPage() {
    if(!portal) { server.send(403,"text/plain","Hold the knob for 5 seconds to enable setup."); return; }
    String page=R"HTML(<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><title>EchoScope setup</title>
<style>body{background:#071918;color:#e8f8f4;font:17px system-ui;max-width:440px;margin:40px auto;padding:24px}h1{color:#68f3ae}label{display:block;margin:22px 0 6px}input,button{box-sizing:border-box;width:100%;padding:13px;border-radius:8px;border:1px solid #52716a;font:inherit}button{background:#68f3ae;margin-top:26px}p{line-height:1.5}</style>
<h1>EchoScope</h1><p>Choose your home Wi-Fi and the centre of your radar. Coordinates are decimal degrees; west and south are negative.</p><form method="post" action="/save">
)HTML";
    page+="<input type='hidden' name='token' value='"+csrf+"'>";
    page+="<label>Wi-Fi name (2.4 GHz)</label><input name='ssid' maxlength='32' required value='"+escape(ssid)+"'>";
    page+="<label>Wi-Fi password</label><input name='password' type='password' maxlength='63' autocomplete='new-password' placeholder='Leave blank to keep saved password'>";
    page+="<label>Latitude</label><input name='lat' type='number' step='any' min='-90' max='90' required value='"+(configured?String(homeLat,6):String(""))+"'>";
    page+="<label>Longitude</label><input name='lon' type='number' step='any' min='-180' max='180' required value='"+(configured?String(homeLon,6):String(""))+"'>";
    page+="<label>Photo service URL (optional)</label><input name='photo_url' maxlength='160' placeholder='http://192.168.1.10:8086' value='"+escape(photoBase)+"'>";
    page+="<p>Leave blank to disable photos. Use your Docker server's LAN address.</p><button type='button' onclick=\"const b=this;b.disabled=true;fetch('/test-photo',{method:'POST',body:new URLSearchParams(new FormData(b.form))}).then(async r=>{document.getElementById('test-result').textContent=await r.text()}).catch(()=>{document.getElementById('test-result').textContent='Connection test failed'}).finally(()=>b.disabled=false)\">Test connection</button><p id='test-result' role='status'></p>";
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
    String newSSID=server.arg("ssid"), newPassword=server.arg("password"),newPhoto=server.arg("photo_url");
    if(!photoURL(newPhoto)) { server.send(400,"text/plain","Photo URL must be http://SERVER-IP:PORT with no path or credentials"); return; }
    double lat,lon;
    if(!newSSID.length() || newSSID.length()>32 || newPassword.length()>63 || !coordinate(server.arg("lat"),-90,90,lat) || !coordinate(server.arg("lon"),-180,180,lon)) {
        server.send(400,"text/plain","Check Wi-Fi name and decimal latitude/longitude."); return;
    }
    if(!newPassword.length() && newSSID==ssid) newPassword=password;
    if(newPassword.length() && newPassword.length()<8) { server.send(400,"text/plain","Wi-Fi password must be at least 8 characters."); return; }
    photoBase=newPhoto; prefs.putString("photo_url",photoBase); photoAttempt[0]=0; photoRetryAt=0;
    lvgl_port_lock(-1); ui::photosEnabled=!photoBase.isEmpty() && photoPixels; ui::photoReady=false; lvgl_port_unlock();
    ssid=newSSID; password=newPassword; homeLat=lat; homeLon=lon; configured=true;
    prefs.putString("ssid",ssid); prefs.putString("pass",password); prefs.putDouble("lat",lat); prefs.putDouble("lon",lon); prefs.putBool("set",true);
    server.send(200,"text/html","<meta name='viewport' content='width=device-width'><h1>Settings saved</h1><p>The knob is connecting. If it cannot connect, setup remains available. Press the knob to view the radar.</p>");
    WiFi.begin(ssid.c_str(),password.c_str()); nextReconnect=millis()+20000; nextFetch=millis(); retryDelay=5000;
    lvgl_port_lock(-1); ui::model.reset(); ui::model.demo=false; ui::settings=false; lvgl_port_unlock();
    status("Connecting to Wi-Fi");
}
void updateSetupNetwork() {
    const bool connected=WiFi.status()==WL_CONNECTED;
    const String address=connected?WiFi.localIP().toString():WiFi.softAPIP().toString();
    static String previousAddress,previousSSID;
    static bool previousConnected=false;
    if(address==previousAddress && ssid==previousSSID && connected==previousConnected) return;
    previousAddress=address; previousSSID=ssid; previousConnected=connected;
    lvgl_port_lock(-1);
    ui::setupConnected=connected;
    snprintf(ui::setupSSID,sizeof(ui::setupSSID),"%s",ssid.c_str());
    snprintf(ui::setupAddress,sizeof(ui::setupAddress),"%s",address.c_str());
    lvgl_port_unlock();
}
void startFallbackAP() {
    if(apActive || WiFi.status()==WL_CONNECTED) return;
    WiFi.mode(WIFI_AP_STA);
    if(!WiFi.softAP("EchoScope-Setup",ui::setupPassword)) { status("Setup Wi-Fi failed"); return; }
    dns.start(53,"*",WiFi.softAPIP()); apActive=true;
    Serial.println("[network] Fallback AP enabled");
}
void openPortal() {
    startFallbackAP();
    portal=true; portalStarted=millis();
    updateSetupNetwork();
    lvgl_port_lock(-1); ui::settings=true; ui::setupOpeningTouch=ui::input.touching; ui::input.pending=false; lvgl_port_unlock();
}
// Keep remote text single-line and bounded, including HTML error pages.
void logFeedBytes(const char *label,const char *value,size_t length) {
    char preview[241]; const size_t n=std::min(length,sizeof(preview)-1);
    for(size_t i=0;i<n;++i) {
        const unsigned char c=value[i]; preview[i]=(c>=32 && c<127)?char(c):' ';
    }
    preview[n]=0;
    Serial.printf("[feed] %s: %s%s\n",label,preview,length>n?" ...":"");
}
void logFeedText(const char *label,const String &value) {
    logFeedBytes(label,value.c_str(),value.length());
}
void *allocateFeedBytes(size_t size) {
    return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
struct FeedBody {
    sky::FeedBuffer text;
    explicit FeedBody(size_t limit):text(limit,allocateFeedBytes) {}
    bool complete=false;
    const char *reason="connection closed";
    uint32_t elapsed=0;
};
FeedBody readFeedBody(HTTPClient &http,size_t limit,uint32_t timeout) {
    FeedBody result(limit);
    if(!result.text.allocated()) { result.reason="body allocation failed"; return result; }
    auto *stream=http.getStreamPtr();
    const int expected=http.getSize(); const uint32_t start=millis();
    while(true) {
        if(expected>=0 && result.text.length()==size_t(expected)) {
            result.complete=true; result.reason="content length reached"; break;
        }
        if(!stream) { result.reason="response stream unavailable"; break; }
        const int available=stream->available();
        if(available<=0 && !http.connected()) {
            result.complete=expected<0;
            result.reason=result.complete?"connection closed (unknown length)":"connection closed early"; break;
        }
        if(result.text.length()>=limit) { result.reason="body size limit"; break; }
        if(uint32_t(millis()-start)>=timeout) { result.reason="body read timeout"; break; }
        if(available>0) {
            uint8_t buffer[512];
            size_t wanted=std::min(size_t(available),std::min(sizeof(buffer),limit-result.text.length()));
            if(expected>=0) wanted=std::min(wanted,size_t(expected)-result.text.length());
            const int n=stream->read(buffer,wanted);
            if(n>0 && !result.text.append(reinterpret_cast<const char*>(buffer),n)) {
                result.reason="body allocation failed"; break;
            }
            if(n<=0) delay(1);
        } else delay(5);
    }
    result.elapsed=millis()-start;
    return result;
}
void fetchPhoto() {
    if(photoBase.isEmpty() || !photoPixels || ui::asleep.load()) return;
    char reg[16]{};
    lvgl_port_lock(-1);
    auto *selected=ui::model.selection();
    if(ui::model.details && selected) snprintf(reg,sizeof(reg),"%s",selected->registration);
    lvgl_port_unlock();
    if(!reg[0]) return;
    for(char c:reg) { if(!c) break; if(!isalnum(static_cast<unsigned char>(c)) && c!='-') return; }
    if(!strcmp(photoAttempt,reg) && int32_t(millis()-photoRetryAt)<0) return;
    snprintf(photoAttempt,sizeof(photoAttempt),"%s",reg); photoRetryAt=millis()+300000;
    lvgl_port_lock(-1); ui::photoReady=false; snprintf(ui::photoStatus,sizeof(ui::photoStatus),"Loading photo..."); lvgl_port_unlock();
    NetworkClient client; HTTPClient http; http.setConnectTimeout(2000); http.setTimeout(12000); http.useHTTP10(true);
    http.begin(client,photoBase+"/v1/photo/"+reg);
    const int code=http.GET();
    bool ok=false,discarded=false;
    if(code==200 && http.getSize()>=int(sky::photoHeaderSize) && http.getSize()<=int(sky::photoMaxBytes)) {
        auto body=readFeedBody(http,sky::photoMaxBytes,5000);
        unsigned width=0,height=0;
        if(body.complete && sky::photoPacket(body.text.c_str(),body.text.length(),width,height)) {
            lvgl_port_lock(-1);
            auto *current=ui::model.selection();
            if(!ui::asleep.load() && ui::model.details && current && !strcmp(current->registration,reg)) {
                lv_img_cache_invalidate_src(&ui::photoImage);
                memcpy(photoPixels,body.text.c_str()+sky::photoHeaderSize,width*height*2);
                // Wire pixels are RGB565 big-endian, matching LV_COLOR_16_SWAP=1.
                ui::photoImage.header.cf=LV_IMG_CF_TRUE_COLOR; ui::photoImage.header.w=width; ui::photoImage.header.h=height;
                ui::photoImage.data=reinterpret_cast<const uint8_t*>(photoPixels); ui::photoImage.data_size=width*height*2;
                snprintf(ui::photoReg,sizeof(ui::photoReg),"%s",reg);
                memcpy(ui::photoCredit,body.text.c_str()+8,128); memcpy(ui::photoLink,body.text.c_str()+136,256);
                ui::photoReady=true; ok=true;
            } else discarded=true;
            lvgl_port_unlock();
        }
    }
    http.end();
    if(discarded) { photoAttempt[0]=0; return; }
    if(!ok) {
        photoRetryAt=millis()+60000;
        lvgl_port_lock(-1); snprintf(ui::photoStatus,sizeof(ui::photoStatus),"%s",code==404?"No photo available":"Photo service unavailable"); lvgl_port_unlock();
    }
    Serial.printf("[photo] %s HTTP=%d %s\n",reg,code,ok?"ready":"unavailable/discarded");
}
void fetch() {
    if(ui::asleep.load()) return;
    if(time(nullptr)<1700000000) { status("Waiting for clock sync"); nextFetch=millis()+5000; return; }
    IPAddress address;
    if(WiFi.hostByName("opendata.adsb.fi",address)!=1) {
        Serial.printf("[feed] DNS lookup failed; Wi-Fi status=%d, RSSI=%d dBm\n",int(WiFi.status()),WiFi.RSSI());
        status("DNS lookup failed");
        retryDelay=std::min(uint32_t(120000),retryDelay*2); nextFetch=millis()+retryDelay;
        Serial.printf("[feed] DNS failed; next attempt in %lu ms\n",(unsigned long)retryDelay); return;
    }
    Serial.printf("[feed] Connecting; UTC=%lld, free heap=%u, largest internal block=%u\n",
                  (long long)time(nullptr),ESP.getFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    Serial.printf("[feed] DNS IPv4=%s, RSSI=%d dBm\n",address.toString().c_str(),WiFi.RSSI());
    FeedTLSClient client; client.setCACert(apiRootCA); client.setHandshakeTimeout(15);
    HTTPClient http; http.setConnectTimeout(10000); http.setTimeout(5000); http.useHTTP10(true);
    String url="https://opendata.adsb.fi/api/v3/lat/"+String(homeLat,6)+"/lon/"+String(homeLon,6)+"/dist/54";
    if(!http.begin(client,url)) { Serial.println("[feed] HTTP begin failed; retry in 15000 ms"); status("Cannot open data feed"); nextFetch=millis()+15000; return; }
    const char *headers[]={"Content-Type","Content-Encoding","Transfer-Encoding","Retry-After","Server","CF-Ray"};
    http.collectHeaders(headers,sizeof(headers)/sizeof(headers[0]));
    lvgl_port_lock(-1); const auto fetchFilter=ui::model.filter; lvgl_port_unlock();
    const uint32_t requestStart=millis();
    const int code=http.GET();
    Serial.printf("[feed] HTTP=%d, headers after=%lu ms, content length=%d\n",code,
                  (unsigned long)(millis()-requestStart),http.getSize());
    bool ok=false;
    if(code==200) {
        // Filter unused fields during decoding; retain raw body for failure diagnostics.
        JsonDocument doc;
        auto body=readFeedBody(http,512*1024,8000);
        Serial.printf("[feed] Body=%u/%d bytes, read=%lu ms, transport ended=%s, stop=%s\n",
                      unsigned(body.text.length()),http.getSize(),(unsigned long)body.elapsed,
                      body.complete?"yes":"no",body.reason);
        if(!body.complete) {
            Serial.printf("[feed] Response incomplete: %s (JSON decode skipped)\n",body.reason);
            status("Incomplete feed response");
        } else {
            sky::FeedJsonReader reader{body.text.c_str(),body.text.length()};
            const auto error=sky::decodeFeed(doc,reader);
            if(error) {
                const size_t offset=reader.position?reader.position-1:0;
                const size_t start=offset>80?offset-80:0;
                Serial.printf("[feed] JSON stopped near byte %u/%u (zero-based); context starts at %u\n",
                              unsigned(offset),unsigned(body.text.length()),unsigned(start));
                logFeedBytes("Decode context",body.text.c_str()+start,std::min(size_t(200),body.text.length()-start));
                Serial.print("[feed] Bytes around stop (hex):");
                for(size_t i=offset>16?offset-16:0;i<std::min(offset+size_t(17),body.text.length());++i)
                    Serial.printf(" %02X",static_cast<unsigned char>(body.text[i]));
                Serial.println();
                const size_t tail=body.text.length()>200?body.text.length()-200:0;
                logFeedBytes("Response tail",body.text.c_str()+tail,body.text.length()-tail);
                Serial.printf("[feed] JSON decode failed: %s; document overflow=%s, free heap=%u, largest internal block=%u\n",error.c_str(),doc.overflowed()?"yes":"no",ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
                char message[80]; snprintf(message,sizeof(message),"JSON: %s",error.c_str()); status(message);
            } else if(!sky::parseAircraft(doc,incoming,homeLat,homeLon,millis(),fetchFilter)) {
                Serial.println("[feed] JSON schema error: expected top-level 'ac' array (missing or wrong type)");
                status("Feed missing aircraft array");
            } else {
                lvgl_port_lock(-1);
                if(ui::model.filter==fetchFilter) ui::model.ingest(incoming,millis());
                lvgl_port_unlock(); ok=true; status("Live positions");
                Serial.printf("[feed] Parsed %u entries; kept %u aircraft\n",unsigned(doc["ac"].size()),unsigned(incoming.count));
            }
        }
        if(!ok) logFeedBytes("Response prefix",body.text.c_str(),body.text.length());
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
            auto body=readFeedBody(http,1024,1000);
            logFeedBytes("Error response prefix",body.text.c_str(),body.text.length());
        }
        status(msg);
    }
    if(!ok) {
        for(auto header:headers) if(http.hasHeader(header)) logFeedText(header,http.header(header));
        Serial.printf("[feed] Failure after=%lu ms; Wi-Fi status=%d, RSSI=%d dBm, free heap=%u, largest internal block=%u, free PSRAM=%u\n",
                      (unsigned long)(millis()-requestStart),int(WiFi.status()),WiFi.RSSI(),ESP.getFreeHeap(),
                      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),ESP.getFreePsram());
    }
    http.end();
    static bool controlTestDone=false;
    if(code<0 && !controlTestDone && !ui::asleep.load()) {
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
    Serial.printf("[feed] %s; next attempt in %lu ms\n",ok?"Success":"Failed",(unsigned long)retryDelay);
    nextFetch=millis()+retryDelay;
}
}

void setup() {
    Serial.begin(115200);
    Serial.println("EchoScope 0.3.2 / LAN setup and combined photo details");
    Serial.printf("[tasks] Network core=%d, LVGL core=%d\n",xPortGetCoreID(),LVGL_PORT_TASK_CORE);
    // Keep the original NVS namespace so existing Wi-Fi/location survive updates.
    prefs.begin("sky-knob",false);
    photoBase=prefs.getString("photo_url",""); ui::photosEnabled=!photoBase.isEmpty();
    photoPixels=static_cast<lv_color_t*>(heap_caps_malloc(200*150*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!photoPixels) ui::photosEnabled=false;
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
        if(ui::asleep.load()) return;
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
    pinMode(0,INPUT_PULLUP);
    buttonQueue=xQueueCreate(16,sizeof(ButtonEvent)); assert(buttonQueue);
    const BaseType_t buttonStarted=xTaskCreatePinnedToCore(sampleButton,"knob-button",2048,nullptr,2,nullptr,0);
    assert(buttonStarted==pdPASS);
    lv_timer_create(controls,10,nullptr);
    lvgl_port_unlock();
    pinMode(6,INPUT_PULLUP); pinMode(5,INPUT_PULLUP); pinMode(0,INPUT_PULLUP);
    encoderPrevious=(digitalRead(6)<<1)|digitalRead(5);
    attachInterrupt(digitalPinToInterrupt(6),encoderISR,CHANGE); attachInterrupt(digitalPinToInterrupt(5),encoderISR,CHANGE);
    WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
    configTime(0,0,"pool.ntp.org","time.google.com");
    server.on("/photo",HTTP_GET,photoSource); server.on("/test-photo",HTTP_POST,testPhotoService);
    server.on("/",HTTP_GET,setupPage); server.on("/save",HTTP_POST,saveSetup); server.onNotFound(setupPage); server.begin();
    if(configured) { WiFi.begin(ssid.c_str(),password.c_str()); status("Connecting to Wi-Fi"); }
    else { demoFrame(millis()); openPortal(); }
}
void loop() {
    if(requestPortal.exchange(false)) openPortal();
    const uint32_t now=millis();
    if(ui::requestFeed.exchange(false)) nextFetch=now;
    const bool connected=WiFi.status()==WL_CONNECTED;
    if(connected && apActive) {
        dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); apActive=false;
        Serial.println("[network] Connected to configured Wi-Fi; fallback AP disabled");
    }
    if(networkPolicy.fallback(configured,connected,now) && !apActive) openPortal();
    updateSetupNetwork();
    if(connected && portal && sky::setupExpired(millis(),portalStarted)) {
        portal=false; lvgl_port_lock(-1); ui::settings=false; lvgl_port_unlock();
    }
    server.handleClient(); if(apActive) dns.processNextRequest();
    if(ui::asleep.load()) { delay(20); return; }
    if(!configured) {
        static uint32_t lastDemo=0;
        if(uint32_t(now-lastDemo)>1000) { demoFrame(now); lastDemo=now; }
    } else if(WiFi.status()==WL_CONNECTED) {
        if(int32_t(now-nextFetch)>=0) fetch();
        else fetchPhoto();
    } else {
        status("Wi-Fi disconnected");
        if(int32_t(now-nextReconnect)>=0) { WiFi.begin(ssid.c_str(),password.c_str()); nextReconnect=now+20000; }
    }
    delay(5);
}
