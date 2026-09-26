#include "network_policy.h"
#include <Arduino.h>
#include "network_log.h"
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_lcd_panel_io.h>
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
String watchTypes,watchRegs,watchCalls;
unsigned brightness=100,sleepMinutes=60,startRange=2;
bool watchMilitary=false,watchRotor=false;
sky::AlertStyle savedAlertStyle;
uint32_t photoRetryAt=0;
char photoAttempt[16]{};
lv_color_t *photoPixels=nullptr,*mapPixels=nullptr;
bool capsPhotos=false,capsMaps=false,capsSatellites=false,mapWanted=true;
String familyFlight,familyCallsign,familyArrival;
uint32_t nextInsight=0; int lastInsight=0; String lastRoute;
bool capsWeather=false,capsFlights=false,capsAirports=false;
uint32_t nextCapabilities=0,nextMap=0,nextStations=0;
int requestedMapRange=-1;

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
    if(std::strcmp(previous,message)) { deviceLog.printf("[network] %s\n",message); snprintf(previous,sizeof(previous),"%s",message); }
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
// This AMOLED uses the board's SH8601-compatible QSPI command framing.
// Serialize with LVGL transfers; the board init sequence uses 0x51 for luminance.
void applyBrightness() {
    const uint8_t value=uint8_t((brightness*255+50)/100);
    const auto result=esp_lcd_panel_io_tx_param(board->getLCD()->getBus()->getControlPanelHandle(),0x02005100,&value,1);
    if(result!=ESP_OK) deviceLog.printf("[power] Brightness command failed: %d\n",result);
}
bool wakeForInput(uint32_t now,bool touch=false) {
    if(!ui::activity.interact(now,touch)) return false;
    ui::asleep=false; ui::input=sky::InputGate{};
    if(!board->getLCD()->setDisplayOnOff(true)) deviceLog.println("[power] Display wake command failed");
    applyBrightness();
    ui::requestFeed=true;
    deviceLog.println("[power] Awake; refreshing aircraft");
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
    if(std::abs(remainder)>=transitionsPerDetent) {
        const int previousBand=ui::model.altitudeFilter;
        if(!ui::settings) { if(ui::infoMenu || ui::infoView) ui::rotateInfo(remainder/transitionsPerDetent); else if(ui::satelliteView) ui::rotateStations(remainder/transitionsPerDetent); else ui::model.rotate(remainder/transitionsPerDetent,now); }
        if(previousBand!=ui::model.altitudeFilter) ui::requestFeed=true;
        remainder%=transitionsPerDetent;
    }
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
            deviceLog.printf("[input] Button released; long=%s, touch overlap=%s\n",
                          event.longPress?"yes":"no",(ui::input.touchedDuringPress || ui::input.touching)?"yes":"no");
        } else if(event.kind==sky::ButtonDebounce::Hold && !wakeButton) requestPortal=true;
    }
    if(ui::input.takeClick(millis())) {
        if(ui::settings) ui::settings=false;
        else if(ui::infoMenu || ui::infoView) ui::pressInfo();
        else if(ui::satelliteView) ui::satelliteView=false;
        else ui::model.press(now);
        deviceLog.printf("[input] Click accepted; rotation=%s\n",ui::model.altitudeMode?"altitude":ui::model.selectMode?"aircraft":"range");
    }
    if(ui::activity.tick(millis(),ui::input.touching || ui::input.buttonHeld || wakeButton)) {
        ui::asleep=true;
        if(!board->getLCD()->setDisplayOnOff(false)) {
            lv_canvas_fill_bg(ui::canvas,lv_color_black(),LV_OPA_COVER);
            deviceLog.println("[power] Display off command failed; using black screen");
        }
        deviceLog.println("[power] Idle sleep; feed paused");
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
    http.end();
    String result="Cannot reach a compatible information server. Check its LAN IP and port.";
    if(ok) {
        const bool legacy=doc["capabilities"].isNull();
        result="Info server connected. Available:";
        if(legacy || doc["capabilities"]["photos"]==true) result+=" photos";
        if(doc["capabilities"]["maps"]==true) result+=" maps";
        if(doc["capabilities"]["satellites"]==true) result+=" space stations";
        if(doc["capabilities"]["weather"]==true) result+=" weather";
        if(doc["capabilities"]["flights"]==true) result+=" flights";
        if(doc["capabilities"]["airports"]==true) result+=" airports";
        if(!legacy && doc["capabilities"]["weather"]!=true && doc["capabilities"]["flights"]!=true && doc["capabilities"]["airports"]!=true && doc["capabilities"]["photos"]!=true && doc["capabilities"]["maps"]!=true && doc["capabilities"]["satellites"]!=true) result+=" none currently";
        result+=".";
    }
    server.send(ok?200:502,"text/plain",result);
}
bool updateAccepted=false,updateComplete=false;
size_t updateExpected=0,updateWritten=0,updateHeaderSize=0;
uint8_t updateHeader[36]{};
String updateError;
void maintenanceInfo() {
    if(!portal) { server.send(403,"text/plain","Hold the knob for 5 seconds to unlock wireless upload, then retry."); return; }
    const auto *partition=esp_ota_get_next_update_partition(nullptr);
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json","{\"device\":\"echoscope\",\"protocol\":1,\"token\":\""+csrf+"\",\"capacity\":"+String(partition?partition->size:0)+"}");
}
void uploadFirmwareChunk() {
    auto &upload=server.upload();
    if(upload.status==UPLOAD_FILE_START) {
        updateAccepted=false; updateComplete=false; updateError=""; updateWritten=0; updateHeaderSize=0;
        if(!portal || server.header("X-EchoScope-Token")!=csrf) { updateError="Unlock setup before uploading"; return; }
        const String size=server.header("X-Firmware-Size"),md5=server.header("X-Firmware-MD5");
        char *end=nullptr; updateExpected=strtoul(size.c_str(),&end,10);
        const auto *partition=esp_ota_get_next_update_partition(nullptr);
        bool validMD5=md5.length()==32;
        for(char c:md5) if(!isxdigit(static_cast<unsigned char>(c))) validMD5=false;
        if(size.isEmpty() || !end || *end || !partition || updateExpected<36 || updateExpected>partition->size || !validMD5) {
            updateError="Invalid image size/checksum or no OTA partition"; return;
        }
        if(!Update.begin(updateExpected,U_FLASH) || !Update.setMD5(md5.c_str())) {
            updateError=Update.errorString(); Update.abort(); return;
        }
        updateAccepted=true;
        status("Uploading firmware..."); deviceLog.println("[update] Upload started; aircraft requests paused");
    } else if(upload.status==UPLOAD_FILE_WRITE && updateAccepted) {
        size_t offset=0;
        if(upload.currentSize>updateExpected-updateWritten) {
            updateError="Image exceeds declared size"; Update.abort(); updateAccepted=false; return;
        }
        if(updateHeaderSize<sizeof(updateHeader)) {
            const size_t n=std::min(upload.currentSize,sizeof(updateHeader)-updateHeaderSize);
            memcpy(updateHeader+updateHeaderSize,upload.buf,n); updateHeaderSize+=n; offset=n;
            if(updateHeaderSize==sizeof(updateHeader)) {
                // Reject merged images/bootloaders and non-S3 firmware before committing.
                if(updateHeader[0]!=0xE9 || updateHeader[12]!=9 || updateHeader[13]!=0 ||
                   updateHeader[32]!=0x32 || updateHeader[33]!=0x54 || updateHeader[34]!=0xCD || updateHeader[35]!=0xAB) {
                    updateError="Expected ESP32-S3 application image (not merged/bootloader)";
                    Update.abort(); updateAccepted=false; return;
                }
                if(Update.write(updateHeader,sizeof(updateHeader))!=sizeof(updateHeader)) {
                    updateError=Update.errorString(); Update.abort(); updateAccepted=false; return;
                }
            }
        }
        const size_t remaining=upload.currentSize-offset;
        if(remaining && Update.write(upload.buf+offset,remaining)!=remaining) {
            updateError=Update.errorString(); Update.abort(); updateAccepted=false;
        } else updateWritten+=upload.currentSize;
    } else if(upload.status==UPLOAD_FILE_END && updateAccepted) {
        updateComplete=updateWritten==updateExpected && Update.end();
        if(!updateComplete) { updateError=updateWritten!=updateExpected?"Incomplete firmware image":Update.errorString(); Update.abort(); }
        updateAccepted=false;
    } else if(upload.status==UPLOAD_FILE_ABORTED) {
        if(updateAccepted) Update.abort();
        updateAccepted=false; updateComplete=false; updateError="Upload interrupted";
        deviceLog.println("[update] Upload interrupted; current firmware retained");
    }
}
void finishFirmwareUpload() {
    if(!updateComplete) {
        server.send(400,"text/plain",updateError.isEmpty()?"No complete firmware received":updateError);
        status("Firmware upload failed"); return;
    }
    server.send(200,"text/plain","Firmware verified; rebooting EchoScope.");
    deviceLog.println("[update] Image verified; rebooting");
    delay(500); ESP.restart();
}
void networkLogs() {
    uint64_t cursor=strtoull(server.arg("since").c_str(),nullptr,10);
    char chunk[1025]; bool lost=false;
    const size_t n=deviceLog.read(cursor,chunk,1024,lost); chunk[n]=0;
    char next[24]; snprintf(next,sizeof(next),"%llu",(unsigned long long)cursor);
    server.sendHeader("Cache-Control","no-store");
    static const String bootId=String(esp_random(),HEX);
    server.sendHeader("X-Log-Boot",bootId);
    server.sendHeader("X-Log-Cursor",next); server.sendHeader("X-Log-Lost",lost?"1":"0");
    server.send(200,"text/plain",String(chunk,n));
}
String alertColorInput(const char *name,const char *label,uint32_t color) {
    char hex[8]; snprintf(hex,sizeof(hex),"#%06lx",(unsigned long)color);
    return String("<label>")+label+"</label><input type='color' style='height:52px' name='"+name+"' value='"+hex+"'>";
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
    page+="<label>Startup range</label><select name='start_range' style='width:100%;padding:13px;font:inherit'>";
    for(int i=0;i<5;++i) page+="<option value='"+String(i)+"' "+String(unsigned(i)==startRange?"selected":"")+">"+String(int(sky::ranges[i]))+" km</option>";
    page+="</select>";
    page+="<h2>Display</h2><label>Brightness (%)</label><input name='brightness' type='number' min='5' max='100' required value='"+String(brightness)+"'>";
    page+="<label>Sleep after idle minutes (0 = never)</label><input name='sleep' type='number' min='0' max='1440' required value='"+String(sleepMinutes)+"'>";
    page+="<h2>Watchlist</h2><p>Comma-separated types, registrations or callsigns. A trailing * matches a prefix. Up to 16 entries per field, 15 characters each. A380 also matches A388.</p>";
    page+="<label>Aircraft types</label><input name='watch_types' maxlength='255' placeholder='A380, B74*' value='"+escape(watchTypes)+"'>";
    page+="<label>Registrations</label><input name='watch_regs' maxlength='255' placeholder='G-UZHO' value='"+escape(watchRegs)+"'>";
    page+="<label>Callsigns</label><input name='watch_calls' maxlength='255' placeholder='RCH*' value='"+escape(watchCalls)+"'>";
    page+="<label><input style='width:auto' type='checkbox' name='watch_military' "+String(watchMilitary?"checked":"")+"> Watch military aircraft</label>";
    page+="<label><input style='width:auto' type='checkbox' name='watch_rotor' "+String(watchRotor?"checked":"")+"> Watch helicopters</label><p>Fresh visible matches activate the outer alert ring. Range, aircraft and altitude filters apply. Sleep pauses monitoring.</p>";
    page+="<h2>Alert appearance</h2>";
    page+=alertColorInput("alert_watch","Watchlist colour",savedAlertStyle.watch);
    page+=alertColorInput("alert_heli","Helicopter colour",savedAlertStyle.helicopter);
    page+=alertColorInput("alert_mil","Military colour",savedAlertStyle.military);
    page+="<label>Ring peak brightness (%)</label><input name='alert_bright' type='number' min='0' max='100' required value='"+String(savedAlertStyle.brightness)+"'>";
    page+="<label>Ring width (pixels)</label><input name='alert_width' type='number' min='1' max='8' required value='"+String(savedAlertStyle.width)+"'>";
    page+="<label>Pulse / flash period (seconds)</label><input name='alert_period' type='number' min='2' max='12' required value='"+String(savedAlertStyle.periodSeconds)+"'>";
    page+="<label>Ring effect</label><select name='alert_effect' style='width:100%;padding:13px;font:inherit'>";
    const char *effects[]={"Off","Steady","Gentle pulse","Flash"};
    for(int i=0;i<4;++i) page+="<option value='"+String(i)+"' "+String(i==int(savedAlertStyle.effect)?"selected":"")+">"+effects[i]+"</option>";
    page+="</select><p>Brightness is relative to the display brightness. Colours also identify watch markers. With several categories present, the outer ring prioritises military, then helicopters, then other watch matches. Off hides the outer ring; markers remain.</p>";
    page+="<label>Info server URL (optional)</label><input name='photo_url' maxlength='160' placeholder='http://192.168.1.10:8086' value='"+escape(photoBase)+"'>";
    page+="<p>Leave blank to disable external features. Capabilities are discovered automatically. Use your Docker server's LAN address.</p><button type='button' onclick=\"const b=this;b.disabled=true;fetch('/test-photo',{method:'POST',body:new URLSearchParams(new FormData(b.form))}).then(async r=>{document.getElementById('test-result').textContent=await r.text()}).catch(()=>{document.getElementById('test-result').textContent='Connection test failed'}).finally(()=>b.disabled=false)\">Test connection</button><p id='test-result' role='status'></p>";
    if(capsFlights) {
        page+="<h2>Family flight</h2><input type='hidden' name='family_setting' value='1'><p>Free live tracking, including beyond radar range. Booking numbers and broadcast callsigns can differ. Confirm the flight/date with the airline; no arrival or delay estimates.</p>";
        page+="<label>Flight number (blank disables)</label><input name='family_flight' maxlength='10' placeholder='U2123' value='"+escape(familyFlight)+"'>";
        page+="<label>Actual callsign override (optional)</label><input name='family_callsign' maxlength='10' placeholder='EZY123' value='"+escape(familyCallsign)+"'>";
        page+="<label>Arrival airport IATA / ICAO (optional)</label><input name='family_arrival' maxlength='10' placeholder='LGW' value='"+escape(familyArrival)+"'>";
    }
    if(capsMaps) page+="<input type='hidden' name='map_setting' value='1'><label><input style='width:auto' type='checkbox' name='map_enabled' "+String(mapWanted?"checked":"")+"> Faint map background</label>";
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
    if(!photoURL(newPhoto)) { server.send(400,"text/plain","Info server URL must be http://SERVER-IP:PORT with no path or credentials"); return; }
    double lat,lon;
    if(!newSSID.length() || newSSID.length()>32 || newPassword.length()>63 || !coordinate(server.arg("lat"),-90,90,lat) || !coordinate(server.arg("lon"),-180,180,lon)) {
        server.send(400,"text/plain","Check Wi-Fi name and decimal latitude/longitude."); return;
    }
    if(!newPassword.length() && newSSID==ssid) newPassword=password;
    if(newPassword.length() && newPassword.length()<8) { server.send(400,"text/plain","Wi-Fi password must be at least 8 characters."); return; }
    double newBrightness,newSleep,newRange;
    sky::Watches watches;
    const String types=server.arg("watch_types"),regs=server.arg("watch_regs"),calls=server.arg("watch_calls");
    if(!coordinate(server.arg("start_range"),0,4,newRange) || floor(newRange)!=newRange ||
       !coordinate(server.arg("brightness"),5,100,newBrightness) || std::floor(newBrightness)!=newBrightness ||
       !coordinate(server.arg("sleep"),0,1440,newSleep) || std::floor(newSleep)!=newSleep ||
       types.length()>255 || regs.length()>255 || calls.length()>255 ||
       !watches.types.set(types.c_str()) || !watches.registrations.set(regs.c_str()) || !watches.callsigns.set(calls.c_str())) {
        server.send(400,"text/plain","Check brightness (5-100), sleep (0-1440 whole minutes), and watchlists (16 entries, 15 characters each; letters, numbers, hyphens, optional trailing *)."); return;
    }
    String newFamily=familyFlight,newCallsign=familyCallsign,newArrival=familyArrival;
    if(server.hasArg("family_setting")) {
        newFamily=server.arg("family_flight"); newCallsign=server.arg("family_callsign"); newArrival=server.arg("family_arrival");
        auto valid=[](String &s) { s.trim(); s.toUpperCase(); if(s.length() && (s.length()<2 || s.length()>10)) return false; for(unsigned i=0;i<s.length();++i) if(!((s[i]>='A' && s[i]<='Z') || (s[i]>='0' && s[i]<='9'))) return false; return true; };
        if(!valid(newFamily) || !valid(newCallsign) || !valid(newArrival)) { server.send(400,"text/plain","Flight and airport fields need 2-10 letters or digits, or leave blank."); return; }
    }
    sky::AlertStyle newAlert;
    double ringBrightness,ringWidth,ringPeriod,ringEffect;
    if(!sky::parseAlertColor(server.arg("alert_watch").c_str(),newAlert.watch) ||
       !sky::parseAlertColor(server.arg("alert_heli").c_str(),newAlert.helicopter) ||
       !sky::parseAlertColor(server.arg("alert_mil").c_str(),newAlert.military) ||
       !coordinate(server.arg("alert_bright"),0,100,ringBrightness) || floor(ringBrightness)!=ringBrightness ||
       !coordinate(server.arg("alert_width"),1,8,ringWidth) || floor(ringWidth)!=ringWidth ||
       !coordinate(server.arg("alert_period"),2,12,ringPeriod) || floor(ringPeriod)!=ringPeriod ||
       !coordinate(server.arg("alert_effect"),0,3,ringEffect) || floor(ringEffect)!=ringEffect) {
        server.send(400,"text/plain","Check alert colours, brightness (0-100), width (1-8), period (2-12) and effect."); return;
    }
    newAlert.brightness=unsigned(ringBrightness); newAlert.width=unsigned(ringWidth);
    newAlert.periodSeconds=unsigned(ringPeriod); newAlert.effect=sky::AlertEffect(unsigned(ringEffect));
    familyFlight=newFamily; familyCallsign=newCallsign; familyArrival=newArrival;
    prefs.putString("family_flight",familyFlight); prefs.putString("family_call",familyCallsign); prefs.putString("family_arr",familyArrival);
    nextInsight=0; lastInsight=0; lastRoute="";
    lvgl_port_lock(-1); snprintf(ui::familyNumber,sizeof(ui::familyNumber),"%s",familyFlight.c_str()); ui::infoMenu=false; ui::infoView=0; ui::infoCount=0; ui::weatherEnabled=ui::flightsEnabled=ui::airportsEnabled=false; lvgl_port_unlock();
    capsWeather=capsFlights=capsAirports=false;
    savedAlertStyle=newAlert;
    prefs.putUInt("alert_watch",newAlert.watch); prefs.putUInt("alert_heli",newAlert.helicopter); prefs.putUInt("alert_mil",newAlert.military);
    prefs.putUInt("alert_bright",newAlert.brightness); prefs.putUInt("alert_width",newAlert.width);
    prefs.putUInt("alert_period",newAlert.periodSeconds); prefs.putUInt("alert_effect",unsigned(newAlert.effect));
    watches.military=server.hasArg("watch_military"); watches.rotorcraft=server.hasArg("watch_rotor");
    watchTypes=types; watchRegs=regs; watchCalls=calls;
    watchMilitary=watches.military; watchRotor=watches.rotorcraft;
    startRange=unsigned(newRange); prefs.putUInt("start_range",startRange);
    sleepMinutes=unsigned(newSleep);
    prefs.putUInt("brightness",unsigned(newBrightness)); prefs.putUInt("sleep_min",sleepMinutes);
    prefs.putString("watch_types",types); prefs.putString("watch_regs",regs); prefs.putString("watch_calls",calls);
    prefs.putBool("watch_mil",watchMilitary); prefs.putBool("watch_rotor",watchRotor);
    lvgl_port_lock(-1);
    brightness=unsigned(newBrightness);
    ui::model.defaultRangeIndex=startRange;
    ui::alertStyle=newAlert;
    ui::model.watches=watches; ui::activity.sleepAfterMs=sleepMinutes*60000;
    ui::activity.lastActivity=millis(); applyBrightness();
    lvgl_port_unlock();
    photoBase=newPhoto; prefs.putString("photo_url",photoBase); photoAttempt[0]=0; photoRetryAt=0; nextCapabilities=0; nextMap=0; nextStations=0; requestedMapRange=-1;
    capsPhotos=capsMaps=capsSatellites=false;
    if(server.hasArg("map_setting")) mapWanted=server.hasArg("map_enabled");
    prefs.putBool("map_enabled",mapWanted);
    lvgl_port_lock(-1); ui::mapWanted=mapWanted; ui::photosEnabled=false; ui::mapsEnabled=false; ui::satellitesEnabled=false; ui::satelliteView=false; ui::mapReady=false; ui::photoReady=false; lvgl_port_unlock();
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
    deviceLog.println("[network] Fallback AP enabled");
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
    deviceLog.printf("[feed] %s: %s%s\n",label,preview,length>n?" ...":"");
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
    if(photoBase.isEmpty() || !capsPhotos || !photoPixels || ui::asleep.load()) return;
    char reg[16]{};
    lvgl_port_lock(-1);
    auto *selected=ui::model.selection();
    if(ui::model.details && !ui::settings && selected) snprintf(reg,sizeof(reg),"%s",selected->registration);
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
    deviceLog.printf("[photo] %s HTTP=%d %s\n",reg,code,ok?"ready":"unavailable/discarded");
}
bool infoJSON(const String &path,JsonDocument &doc) {
    NetworkClient client; HTTPClient http;
    http.setConnectTimeout(1500); http.setTimeout(4000); http.useHTTP10(true);
    http.begin(client,photoBase+path);
    const int code=http.GET(); bool ok=false;
    if(code==200) {
        auto body=readFeedBody(http,8192,4000);
        const auto error=deserializeJson(doc,body.text.c_str(),body.text.length());
        ok=body.complete && !error;
        if(!ok) deviceLog.printf("[info] JSON=%s complete=%d bytes=%u\n",error.c_str(),body.complete,unsigned(body.text.length()));
    }
    if(code!=200) deviceLog.printf("[info] request HTTP=%d\n",code);
    http.end(); return ok;
}
void discoverInfo() {
    JsonDocument doc;
    const bool ok=infoJSON("/health",doc) && doc["service"]=="echoscope-photos" && doc["protocol"]==1;
    const bool legacy=doc["capabilities"].isNull();
    capsPhotos=ok && (legacy || doc["capabilities"]["photos"]==true);
    capsMaps=ok && doc["capabilities"]["maps"]==true && mapPixels;
    capsSatellites=ok && doc["capabilities"]["satellites"]==true;
    capsWeather=ok && doc["capabilities"]["weather"]==true;
    capsFlights=ok && doc["capabilities"]["flights"]==true;
    capsAirports=ok && doc["capabilities"]["airports"]==true;
    lvgl_port_lock(-1);
    ui::weatherEnabled=capsWeather; ui::flightsEnabled=capsFlights; ui::airportsEnabled=capsAirports;
    if((ui::infoView==1 && !capsWeather) || ((ui::infoView==2 || ui::infoView==4) && !capsFlights) || (ui::infoView==3 && !capsAirports)) { ui::infoView=0; ui::infoCount=0; }
    ui::photosEnabled=capsPhotos && photoPixels;
    ui::mapsEnabled=capsMaps;
    ui::satellitesEnabled=capsSatellites;
    if(!ui::infoAvailable()) ui::infoMenu=false;
    if(ui::infoMenu && !ui::infoOption(ui::infoSelection)) ui::rotateInfo(1);
    if(!capsPhotos) { ui::photoReady=false; photoAttempt[0]=0; }
    if(!capsMaps) { ui::mapReady=false; requestedMapRange=-1; }
    if(!capsSatellites) { ui::satelliteView=false; ui::stationCount=0; }
    const char *credit=doc["map_credit"] | "Copyright OpenStreetMap contributors";
    snprintf(ui::mapCredit,sizeof(ui::mapCredit),"%s",credit);
    lvgl_port_unlock();
    nextCapabilities=millis()+(ok?60000:30000);
    deviceLog.printf("[info] available=%d photos=%d maps=%d stations=%d weather=%d flights=%d airports=%d\n",ok,capsPhotos,capsMaps,capsSatellites,capsWeather,capsFlights,capsAirports);
}
void fetchMap(int rangeIndex) {
    requestedMapRange=rangeIndex; nextMap=millis()+10000;
    NetworkClient client; HTTPClient http; http.setConnectTimeout(1500); http.setTimeout(4000); http.useHTTP10(true);
    const String path="/v1/map?lat="+String(homeLat,6)+"&lon="+String(homeLon,6)+"&range="+String(int(sky::ranges[rangeIndex]));
    http.begin(client,photoBase+path); const int code=http.GET();
    if(code==200 && http.getSize()==int(sky::mapBytes)) {
        auto body=readFeedBody(http,sky::mapBytes,5000);
        if(body.complete && sky::validMap(body.text.c_str(),body.text.length())) {
            lvgl_port_lock(-1);
            if(ui::model.rangeIndex==rangeIndex && !ui::asleep.load()) {
                lv_img_cache_invalidate_src(&ui::mapImage);
                memcpy(mapPixels,body.text.c_str()+8,sky::mapBytes-8);
                ui::mapImage.header.cf=LV_IMG_CF_TRUE_COLOR; ui::mapImage.header.w=420; ui::mapImage.header.h=420;
                ui::mapImage.data_size=sky::mapBytes-8; ui::mapImage.data=reinterpret_cast<const uint8_t*>(mapPixels);
                ui::mapReady=true; ui::mapRange=rangeIndex; nextMap=millis()+604800000;
            }
            lvgl_port_unlock();
        }
    }
    http.end();
    deviceLog.printf("[info] map range=%d HTTP=%d\n",int(sky::ranges[rangeIndex]),code);
}
void fetchStations() {
    nextStations=millis()+10000;
    JsonDocument doc;
    const bool ok=infoJSON("/v1/satellites?lat="+String(homeLat,6)+"&lon="+String(homeLon,6),doc);
    sky::Station values[8]{}; unsigned count=0;
    const int64_t generated=doc["generated"] | int64_t(0);
    if(ok && std::abs(int64_t(time(nullptr))-generated)<=30) {
        for(JsonObjectConst item:doc["satellites"].as<JsonArrayConst>()) {
            if(count>=8) break;
            const float az=sky::number(item["az"]),el=sky::number(item["el"]),km=sky::number(item["km"]);
            if(!std::isfinite(az) || az<0 || az>=360 || !std::isfinite(el) || el< -90 || el>90 || !std::isfinite(km) || km<0) continue;
            auto &station=values[count++]; sky::copyText(station.name,item["name"]);
            station.az=az; station.el=el; station.km=km; station.nextRise=item["next_rise"] | uint32_t(0);
        }
    }
    lvgl_port_lock(-1);
    if(count) { memcpy(ui::stations,values,sizeof(values)); ui::stationCount=count; ui::stationReceived=millis(); }
    else { ui::satellitesEnabled=false; ui::satelliteView=false; ui::stationCount=0; }
    lvgl_port_unlock();
}
void fetchInsight(int view,const String &callsign) {
    nextInsight=millis()+30000;
    String path;
    if(view==1) path="/v1/weather?lat="+String(homeLat,4)+"&lon="+String(homeLon,4);
    else if(view==2) path="/v1/family?flight="+familyFlight+"&callsign="+familyCallsign+"&arrival="+familyArrival;
    else if(view==3) path="/v1/airports?lat="+String(homeLat,4)+"&lon="+String(homeLon,4);
    else { String call=callsign; call.trim(); path="/v1/route?flight="+call; }
    JsonDocument doc;
    const bool ok=infoJSON(path,doc);
    const int64_t generated=doc["generated"] | int64_t(0);
    const int64_t age=int64_t(time(nullptr))-generated;
    const bool fresh=age>=-30 && age<=(view==2?60:1800);
    lvgl_port_lock(-1);
    if(ui::infoView==view && (view!=4 || callsign==ui::routeNumber)) {
        ui::infoCount=0;
        if(ok && fresh) for(JsonObjectConst p:doc["pages"].as<JsonArrayConst>()) {
            if(ui::infoCount>=9) break;
            auto &out=ui::infoPages[ui::infoCount++]; out={};
            sky::copyText(out.title,p["title"]); unsigned n=0;
            for(JsonVariantConst line:p["lines"].as<JsonArrayConst>()) { if(n>=7) break; sky::copyText(out.lines[n++],line); }
        }
        if(ui::infoCount) { ui::infoReceived=millis()-uint32_t(std::max(int64_t(0),age))*1000; ui::infoGenerated=generated; sky::copyText(ui::infoSource,doc["source"]); nextInsight=millis()+(view==2?20000:900000); }
        else snprintf(ui::infoMessage,sizeof(ui::infoMessage),"Data unavailable / retrying");
    }
    lvgl_port_unlock();
    deviceLog.printf("[info] page=%d HTTP/JSON=%d fresh=%d\n",view,ok,fresh);
}
void fetchInfo() {
    if(photoBase.isEmpty() || ui::asleep.load()) return;
    const uint32_t now=millis();
    if(int32_t(now-nextCapabilities)>=0) { discoverInfo(); return; }
    lvgl_port_lock(-1);
    const bool skyView=ui::satelliteView,radar=!ui::settings && !ui::model.details && !skyView && !ui::infoMenu && !ui::infoView;
    const int insight=ui::settings?0:ui::infoView;
    const String routeCall=ui::routeNumber;
    const bool needsInfo=ui::infoNeedsFetch; ui::infoNeedsFetch=false;
    const int rangeIndex=ui::model.rangeIndex;
    lvgl_port_unlock();
    if(insight) {
        if(needsInfo || lastInsight!=insight || (insight==4 && routeCall!=lastRoute)) { nextInsight=0; lastInsight=insight; lastRoute=routeCall; }
        if(int32_t(now-nextInsight)>=0) { fetchInsight(insight,routeCall); return; }
    } else lastInsight=0;
    if(skyView && capsSatellites && int32_t(now-nextStations)>=0) { fetchStations(); return; }
    if(radar && capsMaps && mapWanted) {
        if(requestedMapRange!=rangeIndex) nextMap=now;
        if(int32_t(now-nextMap)>=0) { fetchMap(rangeIndex); return; }
    }
    fetchPhoto();
}
void fetch() {
    if(ui::asleep.load()) return;
    if(time(nullptr)<1700000000) { status("Waiting for clock sync"); nextFetch=millis()+5000; return; }
    IPAddress address;
    if(WiFi.hostByName("opendata.adsb.fi",address)!=1) {
        deviceLog.printf("[feed] DNS lookup failed; Wi-Fi status=%d, RSSI=%d dBm\n",int(WiFi.status()),WiFi.RSSI());
        status("DNS lookup failed");
        retryDelay=std::min(uint32_t(120000),retryDelay*2); nextFetch=millis()+retryDelay;
        deviceLog.printf("[feed] DNS failed; next attempt in %lu ms\n",(unsigned long)retryDelay); return;
    }
    deviceLog.printf("[feed] Connecting; UTC=%lld, free heap=%u, largest internal block=%u\n",
                  (long long)time(nullptr),ESP.getFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    deviceLog.printf("[feed] DNS IPv4=%s, RSSI=%d dBm\n",address.toString().c_str(),WiFi.RSSI());
    FeedTLSClient client; client.setCACert(apiRootCA); client.setHandshakeTimeout(15);
    HTTPClient http; http.setConnectTimeout(10000); http.setTimeout(5000); http.useHTTP10(true);
    String url="https://opendata.adsb.fi/api/v3/lat/"+String(homeLat,6)+"/lon/"+String(homeLon,6)+"/dist/54";
    if(!http.begin(client,url)) { deviceLog.println("[feed] HTTP begin failed; retry in 15000 ms"); status("Cannot open data feed"); nextFetch=millis()+15000; return; }
    const char *headers[]={"Content-Type","Content-Encoding","Transfer-Encoding","Retry-After","Server","CF-Ray"};
    http.collectHeaders(headers,sizeof(headers)/sizeof(headers[0]));
    lvgl_port_lock(-1); const auto fetchFilter=ui::model.filter; const int fetchAltitude=ui::model.altitudeFilter; const auto watches=ui::model.watches; lvgl_port_unlock();
    const uint32_t requestStart=millis();
    const int code=http.GET();
    deviceLog.printf("[feed] HTTP=%d, headers after=%lu ms, content length=%d\n",code,
                  (unsigned long)(millis()-requestStart),http.getSize());
    bool ok=false;
    if(code==200) {
        // Filter unused fields during decoding; retain raw body for failure diagnostics.
        JsonDocument doc;
        auto body=readFeedBody(http,512*1024,8000);
        deviceLog.printf("[feed] Body=%u/%d bytes, read=%lu ms, transport ended=%s, stop=%s\n",
                      unsigned(body.text.length()),http.getSize(),(unsigned long)body.elapsed,
                      body.complete?"yes":"no",body.reason);
        if(!body.complete) {
            deviceLog.printf("[feed] Response incomplete: %s (JSON decode skipped)\n",body.reason);
            status("Incomplete feed response");
        } else {
            sky::FeedJsonReader reader{body.text.c_str(),body.text.length()};
            const auto error=sky::decodeFeed(doc,reader);
            if(error) {
                const size_t offset=reader.position?reader.position-1:0;
                const size_t start=offset>80?offset-80:0;
                deviceLog.printf("[feed] JSON stopped near byte %u/%u (zero-based); context starts at %u\n",
                              unsigned(offset),unsigned(body.text.length()),unsigned(start));
                logFeedBytes("Decode context",body.text.c_str()+start,std::min(size_t(200),body.text.length()-start));
                deviceLog.print("[feed] Bytes around stop (hex):");
                for(size_t i=offset>16?offset-16:0;i<std::min(offset+size_t(17),body.text.length());++i)
                    deviceLog.printf(" %02X",static_cast<unsigned char>(body.text[i]));
                deviceLog.println();
                const size_t tail=body.text.length()>200?body.text.length()-200:0;
                logFeedBytes("Response tail",body.text.c_str()+tail,body.text.length()-tail);
                deviceLog.printf("[feed] JSON decode failed: %s; document overflow=%s, free heap=%u, largest internal block=%u\n",error.c_str(),doc.overflowed()?"yes":"no",ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
                char message[80]; snprintf(message,sizeof(message),"JSON: %s",error.c_str()); status(message);
            } else if(!sky::parseAircraft(doc,incoming,homeLat,homeLon,millis(),fetchFilter,fetchAltitude,watches)) {
                deviceLog.println("[feed] JSON schema error: expected top-level 'ac' array (missing or wrong type)");
                status("Feed missing aircraft array");
            } else {
                lvgl_port_lock(-1);
                if(ui::model.filter==fetchFilter && ui::model.altitudeFilter==fetchAltitude) ui::model.ingest(incoming,millis());
                lvgl_port_unlock(); ok=true; status("Live positions");
                deviceLog.printf("[feed] Parsed %u entries; kept %u aircraft\n",unsigned(doc["ac"].size()),unsigned(incoming.count));
            }
        }
        if(!ok) logFeedBytes("Response prefix",body.text.c_str(),body.text.length());
    } else {
        char msg[80];
        if(code<0) {
            char tlsMessage[192]{}; const int tlsError=client.lastError(tlsMessage,sizeof(tlsMessage));
            deviceLog.printf("[feed] HTTP client %d (%s); TLS %d (%s)\n",code,HTTPClient::errorToString(code).c_str(),tlsError,tlsMessage);
            if(tlsError==-80) snprintf(msg,sizeof(msg),"TLS connection reset");
            else if(tlsError==-0x2700) snprintf(msg,sizeof(msg),"TLS certificate rejected");
            else if(tlsError!=0 && tlsError!=-1) snprintf(msg,sizeof(msg),"TLS error %d",tlsError);
            else snprintf(msg,sizeof(msg),"Connection failed (%d)",code);
        } else {
            snprintf(msg,sizeof(msg),"Data feed HTTP %d",code);
            deviceLog.printf("[feed] Server returned HTTP %d\n",code);
            auto body=readFeedBody(http,1024,1000);
            logFeedBytes("Error response prefix",body.text.c_str(),body.text.length());
        }
        status(msg);
    }
    if(!ok) {
        for(auto header:headers) if(http.hasHeader(header)) logFeedText(header,http.header(header));
        deviceLog.printf("[feed] Failure after=%lu ms; Wi-Fi status=%d, RSSI=%d dBm, free heap=%u, largest internal block=%u, free PSRAM=%u\n",
                      (unsigned long)(millis()-requestStart),int(WiFi.status()),WiFi.RSSI(),ESP.getFreeHeap(),
                      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),ESP.getFreePsram());
    }
    http.end();
    static bool controlTestDone=false;
    if(code<0 && !controlTestDone && !ui::asleep.load()) {
        controlTestDone=true;
        // One handshake per boot, no HTTP request or location data to the control host.
        FeedTLSClient control; control.setCACert(apiRootCA); control.setHandshakeTimeout(15);
        deviceLog.println("[control] Testing verified TLS to pki.goog");
        if(control.connect("pki.goog",443,10000)) deviceLog.println("[control] HTTPS handshake works to other host");
        else { char error[160]; int n=control.lastError(error,sizeof(error)); deviceLog.printf("[control] Failed: %d %s\n",n,error); }
        control.stop();
    }
    retryDelay=ok?5000:std::min(uint32_t(120000),retryDelay*2);
    if(code==429) retryDelay=120000;
    deviceLog.printf("[feed] %s; next attempt in %lu ms\n",ok?"Success":"Failed",(unsigned long)retryDelay);
    nextFetch=millis()+retryDelay;
}
}

void setup() {
    Serial.begin(115200);
    deviceLog.println("EchoScope 0.7.0 / family flights and observing weather");
    deviceLog.printf("[tasks] Network core=%d, LVGL core=%d\n",xPortGetCoreID(),LVGL_PORT_TASK_CORE);
    // Keep the original NVS namespace so existing Wi-Fi/location survive updates.
    prefs.begin("sky-knob",false);
    familyFlight=prefs.getString("family_flight",""); familyCallsign=prefs.getString("family_call",""); familyArrival=prefs.getString("family_arr","");
    snprintf(ui::familyNumber,sizeof(ui::familyNumber),"%s",familyFlight.c_str());
    photoBase=prefs.getString("photo_url",""); ui::photosEnabled=false;
    mapWanted=prefs.getBool("map_enabled",true); ui::mapWanted=mapWanted;
    photoPixels=static_cast<lv_color_t*>(heap_caps_malloc(200*150*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!photoPixels) ui::photosEnabled=false;
    mapPixels=static_cast<lv_color_t*>(heap_caps_malloc(420*420*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    savedAlertStyle.watch=prefs.getUInt("alert_watch",0x68F3AE)&0xFFFFFF;
    savedAlertStyle.helicopter=prefs.getUInt("alert_heli",0x62D8F5)&0xFFFFFF;
    savedAlertStyle.military=prefs.getUInt("alert_mil",0xD59AF5)&0xFFFFFF;
    savedAlertStyle.brightness=std::min<uint32_t>(100,prefs.getUInt("alert_bright",30));
    savedAlertStyle.width=std::max<uint32_t>(1,std::min<uint32_t>(8,prefs.getUInt("alert_width",3)));
    savedAlertStyle.periodSeconds=std::max<uint32_t>(2,std::min<uint32_t>(12,prefs.getUInt("alert_period",4)));
    savedAlertStyle.effect=sky::AlertEffect(std::min<uint32_t>(3,prefs.getUInt("alert_effect",2)));
    ui::alertStyle=savedAlertStyle;
    startRange=std::min<uint32_t>(4,prefs.getUInt("start_range",2));
    ui::model.defaultRangeIndex=startRange; ui::model.rangeIndex=startRange;
    brightness=std::max<uint32_t>(5,std::min<uint32_t>(100,prefs.getUInt("brightness",100)));
    sleepMinutes=std::min<uint32_t>(1440,prefs.getUInt("sleep_min",60)); ui::activity.sleepAfterMs=sleepMinutes*60000;
    watchTypes=prefs.getString("watch_types",""); watchRegs=prefs.getString("watch_regs",""); watchCalls=prefs.getString("watch_calls","");
    ui::model.watches.types.set(watchTypes.c_str()); ui::model.watches.registrations.set(watchRegs.c_str()); ui::model.watches.callsigns.set(watchCalls.c_str());
    watchMilitary=prefs.getBool("watch_mil",false); watchRotor=prefs.getBool("watch_rotor",false);
    ui::model.watches.military=watchMilitary; ui::model.watches.rotorcraft=watchRotor;
    configured=prefs.getBool("set",false); ssid=prefs.getString("ssid"); password=prefs.getString("pass");
    homeLat=prefs.getDouble("lat",0); homeLon=prefs.getDouble("lon",0);
    snprintf(ui::setupPassword,sizeof(ui::setupPassword),"echo-%08lx",(unsigned long)esp_random());
    csrf=String(esp_random(),HEX)+String(esp_random(),HEX);
    board=new esp_panel::board::Board(); board->init(); assert(board->begin());
    applyBrightness();
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
            deviceLog.printf("[display] core=%d, render=%lu ms, period=%lu ms\n",xPortGetCoreID(),
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
    const char *maintenanceHeaders[]={"X-EchoScope-Token","X-Firmware-Size","X-Firmware-MD5"};
    server.collectHeaders(maintenanceHeaders,3);
    server.on("/maintenance",HTTP_GET,maintenanceInfo);
    server.on("/update",HTTP_POST,finishFirmwareUpload,uploadFirmwareChunk);
    server.on("/logs",HTTP_GET,networkLogs);
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
        deviceLog.println("[network] Connected to configured Wi-Fi; fallback AP disabled");
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
        else fetchInfo();
    } else {
        status("Wi-Fi disconnected");
        if(int32_t(now-nextReconnect)>=0) { WiFi.begin(ssid.c_str(),password.c_str()); nextReconnect=now+20000; }
    }
    delay(5);
}
