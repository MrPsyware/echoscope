#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <esp_heap_caps.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <atomic>
#include <memory>
#include <cmath>
#include <cctype>
#include "../../c3-test/src/input.h"
#include "../../../include/feed_tls_client.h"
#include "../../../include/api_ca.h"
#include "decoder.h"
#include "mini_features.h"
using esp_panel::board::Board;
constexpr int pinA=7,pinB=6,pinButton=9,W=240,H=240,stripHeight=16;
Board *board;uint16_t *pixels;int stripY;
#include "drawing.h"
volatile int32_t ticks=0;volatile uint8_t previousAB=0;
portMUX_TYPE inputMux=portMUX_INITIALIZER_UNLOCKED;
DRAM_ATTR const int8_t transitions[16]={0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
KnobInput input;
void ARDUINO_ISR_ATTR encoder(){portENTER_CRITICAL_ISR(&inputMux);uint8_t ab=(digitalRead(pinA)<<1)|digitalRead(pinB);ticks+=transitions[(previousAB<<2)|ab];previousAB=ab;portEXIT_CRITICAL_ISR(&inputMux);}
void sampleInputs(void *){TickType_t wake=xTaskGetTickCount();for(;;){uint32_t now=millis();bool pressed=digitalRead(pinButton)==LOW;portENTER_CRITICAL(&inputMux);input.sample(now,pressed,ticks);portEXIT_CRITICAL(&inputMux);vTaskDelayUntil(&wake,max(TickType_t(1),pdMS_TO_TICKS(2)));}}
struct Settings {char ssid[33]{},password[65]{};double lat=0,lon=0;bool configured=false;unsigned brightness=70,sleepMinutes=60;int range=2;mini::Watchlist watch;mini::AirportList airports;unsigned airportMode=1,alertBrightness=25;bool trails=true;};
struct Shared {Settings settings;mini::Snapshot data;char status[32]="STARTING",ip[20]="",ssid[33]="";bool connected=false,ap=false;uint32_t generation=0;};
Shared shared;SemaphoreHandle_t stateMutex;
struct Lock{Lock(){xSemaphoreTake(stateMutex,portMAX_DELAY);}~Lock(){xSemaphoreGive(stateMutex);}};
std::atomic<bool> setupRequested{false},sleeping{false},refreshRequested{false},setupVisible{false};
std::atomic<int> requestedRange{2};
fun::Flyby evilFlyby;
Preferences prefs;WebServer server(80);DNSServer dns;
char apName[24]{},apPassword[13]{},csrf[33]{};
uint32_t restartAt=0;
bool apActive=false;uint32_t connectStarted=0,nextReconnect=0,nextFetch=0,backoff=5000;
void status(const char *s){Lock lock;snprintf(shared.status,sizeof(shared.status),"%s",s);}
void startAP(){if(apActive)return;WiFi.mode(WIFI_AP_STA);if(WiFi.softAP(apName,apPassword)){apActive=true;setupRequested=true;dns.start(53,"*",WiFi.softAPIP());deviceLog.println("[wifi] Setup AP started");}}
String html(String s){s.replace("&","&amp;");s.replace("<","&lt;");s.replace(">","&gt;");s.replace("\"","&quot;");s.replace("'","&#39;");return s;}
Settings settingsCopy(){Lock lock;return shared.settings;}
String airportText(const mini::AirportList &list){String s;for(unsigned i=0;i<list.count;++i){const auto &a=list.items[i];s+=String(a.code)+": "+String(a.lon,6)+"/"+String(a.lat,6)+"\n";}return s;}
void webRoot(){
    Settings c=settingsCopy();
    String body=F("<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><title>EchoScope Mini setup</title><style>body{font:17px system-ui;background:#030d10;color:#e8f8f4;max-width:580px;margin:30px auto;padding:20px}label{display:block;margin:16px 0}input,select,button,textarea{box-sizing:border-box;width:100%;padding:12px;font:inherit;background:#132b2d;color:#e8f8f4;border:1px solid #43665a;border-radius:8px}button{background:#68f3ae;color:#03140e}p{color:#afc6bd}h2{margin-top:32px}textarea{min-height:180px}input[type=checkbox]{width:auto}a{color:#68f3ae}</style><h1>EchoScope Mini</h1><p>A standalone radar for nearby aircraft. Connect to 2.4 GHz Wi-Fi and set the centre of your radar.</p><form method='post' action='/save'><input type='hidden' name='token' value='");
    body+=csrf;body+=F("'><h2>Wi-Fi and location</h2><label>Wi-Fi name<input name='ssid' maxlength='32' required value='");body+=html(c.ssid);
    body+=F("'></label><label>Wi-Fi password<input type='password' name='password' maxlength='63' autocomplete='new-password' placeholder='Leave blank to keep saved password'></label><label><input style='width:auto' type='checkbox' name='open'> This network has no password</label><label>Latitude<input name='lat' type='number' step='any' min='-85' max='85' required value='");
    if(c.configured)body+=String(c.lat,6);
    body+=F("'></label><label>Longitude<input name='lon' type='number' step='any' min='-180' max='180' required value='");if(c.configured)body+=String(c.lon,6);
    body+=F("'></label><p>Use decimal coordinates from your map app. Negative longitude means west. Example: 51.5, -0.1. Your radar location is sent to adsb.fi when requesting aircraft.</p><h2>Display</h2><label>Starting range<select name='range'>");
    for(int i=0;i<4;++i){body+="<option value='"+String(i)+"'"+(i==c.range?" selected":"")+">"+String(int(mini::ranges[i]))+" km</option>";}
    body+="</select></label><label>Brightness (%)<input name='brightness' type='number' min='10' max='100' value='"+String(c.brightness)+"'></label><label>Sleep after (minutes; 0 disables)<input name='sleep' type='number' min='0' max='240' value='"+String(c.sleepMinutes)+"'></label>";
    body+=String("<label><input type='checkbox' name='trails'")+(c.trails?" checked":"")+"> Recent altitude-coloured trails</label><h2>Watchlist alerts</h2><p>Exact identifiers, separated by commas or spaces. The A380 type code is A388; use the callsign shown on the knob for flights. Any match pulses the radar rim green while the aircraft has a fresh position. Alerts cover the nearest 32 aircraft; polling pauses during sleep/setup.</p>";
    body+="<label>Aircraft types (e.g. A388, B744)<input name='types' maxlength='95' value='"+html(c.watch.types)+"'></label>";
    body+="<label>Registrations (e.g. G-XLEA)<input name='registrations' maxlength='95' value='"+html(c.watch.registrations)+"'></label>";
    body+="<label>Flight callsigns (e.g. EZY123)<input name='calls' maxlength='95' value='"+html(c.watch.calls)+"'></label>";
    body+="<label>Alert ring brightness (%; 0 disables)<input name='alert' type='number' min='0' max='100' value='"+String(c.alertBrightness)+"'></label><h2>Airport markers</h2><p>Dim airport icons and codes are drawn beneath aircraft. No info server is needed.</p><label>Airport list<select name='airports'>";
    const char *modes[]={"Off","Built-in UK major airports","Custom list"};for(unsigned i=0;i<3;++i)body+="<option value='"+String(i)+"'"+(i==c.airportMode?" selected":"")+">"+modes[i]+"</option>";
    body+="</select></label><label>Custom airports (up to 32)<textarea name='airportList' maxlength='2048' placeholder='LGW: -0.190278/51.148102'>"+html(airportText(c.airports))+"</textarea></label><p>One per line: CODE: longitude/latitude. Negative longitude is west. Codes can contain 2–8 letters/digits. Custom replaces the UK list; select Built-in UK to restore it. Built-in coordinates: OurAirports, public domain.</p><button>Save and connect</button></form><p>Rotate: select aircraft. Click: details. Double-click: back/menu. Hold and turn: change mode/page. Hold 5 seconds: setup.</p>";
    server.sendHeader("Cache-Control","no-store");server.send(200,"text/html",body);
}
bool numeric(const String &s,double &out){if(!s.length())return false;char *end;out=strtod(s.c_str(),&end);return *end==0&&std::isfinite(out);}
void saveSettings(){
    if(server.arg("token")!=csrf){server.send(403,"text/plain","Reload setup and try again.");return;}
    Settings c=settingsCopy();String ssid=server.arg("ssid"),password=server.arg("password");double lat,lon,range,bright,sleep;
    if(!ssid.length()||ssid.length()>32||!numeric(server.arg("lat"),lat)||!numeric(server.arg("lon"),lon)||fabs(lat)>85||fabs(lon)>180||
        !numeric(server.arg("range"),range)||range<0||range>3||range!=int(range)||!numeric(server.arg("brightness"),bright)||bright<10||bright>100||
        !numeric(server.arg("sleep"),sleep)||sleep<0||sleep>240){server.send(400,"text/plain","Check Wi-Fi name, coordinates, range, brightness and sleep values.");return;}
    if(server.hasArg("open"))password="";
    else if(!password.length()&&ssid==c.ssid)password=c.password;
    else if(password.length()<8||password.length()>63){server.send(400,"text/plain","Enter an 8-63 character Wi-Fi password, or select the open-network option.");return;}
    double airportMode,alert;
    String types=server.arg("types"),registrations=server.arg("registrations"),calls=server.arg("calls"),airports=server.arg("airportList");
    if(!mini::validWatch(types.c_str())||!mini::validWatch(registrations.c_str())||!mini::validWatch(calls.c_str())||
       !numeric(server.arg("airports"),airportMode)||airportMode<0||airportMode>2||airportMode!=int(airportMode)||
       !numeric(server.arg("alert"),alert)||alert<0||alert>100||airports.length()>2048||!mini::parseAirports(airports.c_str(),c.airports)||
       (airportMode==2&&!c.airports.count)){server.send(400,"text/plain","Check watchlists, alert brightness and airports. Use CODE: longitude/latitude, one per line, up to 32 unique airports.");return;}
    snprintf(c.watch.types,sizeof(c.watch.types),"%s",types.c_str());snprintf(c.watch.registrations,sizeof(c.watch.registrations),"%s",registrations.c_str());snprintf(c.watch.calls,sizeof(c.watch.calls),"%s",calls.c_str());
    c.airportMode=unsigned(airportMode);c.alertBrightness=unsigned(alert);c.trails=server.hasArg("trails");
    prefs.putString("watchTypes",c.watch.types);prefs.putString("watchRegs",c.watch.registrations);prefs.putString("watchCalls",c.watch.calls);
    prefs.putUInt("airportMode",c.airportMode);prefs.putString("airports",airportText(c.airports));prefs.putUInt("alert",c.alertBrightness);prefs.putBool("trails",c.trails);
    snprintf(c.ssid,sizeof(c.ssid),"%s",ssid.c_str());snprintf(c.password,sizeof(c.password),"%s",password.c_str());c.lat=lat;c.lon=lon;c.range=int(range);c.brightness=unsigned(bright);c.sleepMinutes=unsigned(sleep);c.configured=true;
    prefs.putString("ssid",c.ssid);prefs.putString("pass",c.password);prefs.putDouble("lat",lat);prefs.putDouble("lon",lon);prefs.putUInt("range",c.range);prefs.putUInt("brightness",c.brightness);prefs.putUInt("sleep",c.sleepMinutes);prefs.putBool("set",true);
    {Lock lock;shared.settings=c;shared.data=mini::Snapshot{};++shared.generation;}
    requestedRange=c.range;refreshRequested=true;
    server.sendHeader("Connection","close");
    server.send(200,"text/html","<meta name='viewport' content='width=device-width'><h1>Saved</h1><p>EchoScope Mini is restarting with your settings. It will reconnect to Wi-Fi and return to radar automatically. Reopen its LAN address after it reconnects.</p>");
    // Give the HTTP response time to leave before restarting. Do not start another
    // feed or reconfigure Wi-Fi under an existing TLS/network state.
    restartAt=millis()+1000;status("SAVED / RESTARTING");
    deviceLog.println("[setup] Settings saved; restarting");
}
class FeedSink:public Stream {
public:
    mini::Decoder &decoder;uint32_t started=millis();
    explicit FeedSink(mini::Decoder &d):decoder(d){}
    int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
    size_t write(uint8_t c)override{return write(&c,1);}
    size_t write(const uint8_t *data,size_t n)override{
        if(uint32_t(millis()-started)>20000){decoder.stream.fail("Feed timeout");return 0;}
        for(size_t i=0;i<n;++i)if(!decoder.put(char(data[i])))return i;
        return n;
    }
};
void fetchFeed(){
    Settings c=settingsCopy();const int range=requestedRange.load();
    if(time(nullptr)<1700000000){status("SYNCING CLOCK");nextFetch=millis()+2000;return;}
    status("UPDATING");
    FeedTLSClient client;client.setCACert(apiRootCA);client.setHandshakeTimeout(12);
    HTTPClient http;http.setConnectTimeout(7000);http.setTimeout(5000);http.useHTTP10(true);
    String url="https://opendata.adsb.fi/api/v3/lat/"+String(c.lat,6)+"/lon/"+String(c.lon,6)+"/dist/"+String(int(ceil(mini::ranges[range]/1.852)));
    const char *headers[]={"Content-Encoding","Content-Type","Retry-After"};http.collectHeaders(headers,3);
    http.begin(client,url);http.setUserAgent("EchoScope-Mini/0.16.0-dev.3");
    deviceLog.printf("[feed] start range=%dkm heap=%u largest=%u\n",int(mini::ranges[range]),ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    int code=http.GET();bool ok=false;char reason[64]{};
    if(code==200&&(http.header("Content-Encoding").isEmpty()||http.header("Content-Encoding")=="identity")){
        std::unique_ptr<mini::Decoder> decoder(new(std::nothrow) mini::Decoder(c.lat,c.lon,range));
        if(decoder){
            FeedSink sink(*decoder);int copied=http.writeToStream(&sink);
            ok=copied>=0&&decoder->finish();
            if(ok){
                decoder->snapshot.received=millis();
                Lock lock;if(requestedRange==range){shared.data=decoder->snapshot;++shared.generation;}
            }else snprintf(reason,sizeof(reason),"%s",decoder->error[0]?decoder->error:decoder->stream.error?decoder->stream.error:HTTPClient::errorToString(copied).c_str());
            deviceLog.printf("[feed] HTTP=%d body=%u aircraft=%u kept=%u ground=%u result=%s heap=%u minimum=%u\n",code,unsigned(decoder->stream.bytes),unsigned(decoder->snapshot.total),unsigned(decoder->snapshot.count),decoder->groundSkipped,ok?"OK":reason,ESP.getFreeHeap(),ESP.getMinFreeHeap());
        }else snprintf(reason,sizeof(reason),"LOW MEMORY");
    }else{
        snprintf(reason,sizeof(reason),"FEED ERROR %d",code);
        char tls[120]{};int err=client.lastError(tls,sizeof(tls));deviceLog.printf("[feed] HTTP=%d TLS=%d %s\n",code,err,tls);
    }
    uint32_t retrySeconds=code==429?std::max(60L,std::min(900L,http.header("Retry-After").toInt())):0;
    http.end();
    if(ok){backoff=5000;nextFetch=millis()+5000;status("LIVE POSITIONS");}
    else{backoff=std::max(retrySeconds*1000,std::min(uint32_t(120000),backoff*2));nextFetch=millis()+backoff;status(reason[0]?reason:"FEED ERROR");}
}
void networkTask(void *){
    prefs.begin("echo-mini",false);Settings c;
    snprintf(c.ssid,sizeof(c.ssid),"%s",(prefs.isKey("ssid")?prefs.getString("ssid"):String()).c_str());snprintf(c.password,sizeof(c.password),"%s",(prefs.isKey("pass")?prefs.getString("pass"):String()).c_str());
    c.lat=prefs.isKey("lat")?prefs.getDouble("lat",0):0;c.lon=prefs.isKey("lon")?prefs.getDouble("lon",0):0;c.configured=prefs.getBool("set",false);c.range=std::min<uint32_t>(3u,prefs.getUInt("range",2));c.brightness=std::max<uint32_t>(10u,std::min<uint32_t>(100u,prefs.getUInt("brightness",70)));c.sleepMinutes=std::min<uint32_t>(240u,prefs.getUInt("sleep",60));
    if(prefs.isKey("watchTypes"))snprintf(c.watch.types,sizeof(c.watch.types),"%s",prefs.getString("watchTypes").c_str());
    if(prefs.isKey("watchRegs"))snprintf(c.watch.registrations,sizeof(c.watch.registrations),"%s",prefs.getString("watchRegs").c_str());
    if(prefs.isKey("watchCalls"))snprintf(c.watch.calls,sizeof(c.watch.calls),"%s",prefs.getString("watchCalls").c_str());
    c.airportMode=std::min<uint32_t>(2u,prefs.getUInt("airportMode",1));c.alertBrightness=std::min<uint32_t>(100u,prefs.getUInt("alert",25));c.trails=prefs.getBool("trails",true);
    if(prefs.isKey("airports"))mini::parseAirports(prefs.getString("airports").c_str(),c.airports);
    if(!std::isfinite(c.lat)||!std::isfinite(c.lon)||fabs(c.lat)>85||fabs(c.lon)>180)c.configured=false;
    {Lock lock;shared.settings=c;++shared.generation;}requestedRange=c.range;
    WiFi.mode(WIFI_STA);WiFi.setSleep(false);WiFi.setAutoReconnect(true);
    configTime(0,0,"pool.ntp.org","time.cloudflare.com");
    const char *funHeaders[]={"X-EchoScope-Fun"}; server.collectHeaders(funHeaders,1);
    server.on("/easter-egg",HTTP_POST,[](){
        static uint32_t last=0; static bool triggered=false;
        server.sendHeader("Cache-Control","no-store");
        if(server.header("X-EchoScope-Fun")!="dr-evil" || !fun::enabledBy(settingsCopy().watch.calls)) { server.send(403,"text/plain","Unavailable"); return; }
        if(!evilFlyby.visible.load() || sleeping.load() || setupVisible.load()) { server.send(409,"text/plain","Open the awake radar first"); return; }
        if(triggered && uint32_t(millis()-last)<30000) { server.send(429,"text/plain","Try again in 30 seconds"); return; }
        evilFlyby.pending=true; last=millis(); triggered=true; server.send(202,"text/plain","Unidentified contact incoming");
    });
    server.on("/",HTTP_GET,webRoot);server.on("/save",HTTP_POST,saveSettings);
    server.onNotFound([](){server.sendHeader("Location","/",true);server.send(302,"text/plain","");});server.begin();
    if(c.configured){WiFi.begin(c.ssid,c.password);status("CONNECTING WIFI");}else{startAP();status("SETUP REQUIRED");setupRequested=true;}
    connectStarted=millis();nextReconnect=connectStarted+20000;
    for(;;){
        server.handleClient();
        if(restartAt){if(int32_t(millis()-restartAt)>=0)ESP.restart();vTaskDelay(pdMS_TO_TICKS(10));continue;}
        if(apActive)dns.processNextRequest();c=settingsCopy();
        uint32_t now=millis();bool connected=WiFi.status()==WL_CONNECTED;
        if(connected&&apActive){dns.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);apActive=false;deviceLog.println("[wifi] Connected; setup AP stopped");}
        if(!connected){
            if(!c.configured||uint32_t(now-connectStarted)>=20000)startAP();
            if(c.configured&&int32_t(now-nextReconnect)>=0){WiFi.disconnect(false,false);WiFi.begin(c.ssid,c.password);nextReconnect=now+20000;}
            status(c.configured?"WIFI RECONNECTING":"SETUP REQUIRED");
        }else connectStarted=now;
        {Lock lock;shared.connected=connected;shared.ap=apActive;snprintf(shared.ip,sizeof(shared.ip),"%s",(connected?WiFi.localIP():WiFi.softAPIP()).toString().c_str());snprintf(shared.ssid,sizeof(shared.ssid),"%s",connected?c.ssid:apName);}
        if(refreshRequested.exchange(false)&&backoff==5000)nextFetch=0;
        if(connected&&c.configured&&!sleeping&&!setupVisible&&int32_t(now-nextFetch)>=0)fetchFeed();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
enum class View{Radar,Details,Menu,Setup};
View view=View::Radar;bool rangeMode=false,ignoreWake=false;unsigned selected=0,detailPage=0,menuItem=0;
char selectedHex[9]{};uint32_t seenGeneration=0,lastActivity=0;Shared frame;mini::Trails trails;
uint16_t green(){return rgb(104,243,174);}uint16_t muted(){return rgb(123,161,153);}uint16_t white(){return rgb(235,248,243);}uint16_t amber(){return rgb(255,185,82);}
uint16_t altitude(float h,unsigned intensity=100){
    unsigned r=123,g=161,b=153;
    if(std::isfinite(h)){if(h<5000){r=104;g=243;b=174;}else if(h<15000){r=91;g=211;b=232;}else if(h<30000){r=103;g=157;b=244;}else{r=210;g=144;b=239;}}
    return rgb(r*intensity/100,g*intensity/100,b*intensity/100);
}
void small(int y,const char *s,uint16_t c){char clipped[32];snprintf(clipped,sizeof(clipped),"%.30s",s);text(y,clipped,c);}
void title(int y,const char *s,uint16_t c){text(y,s,c,strlen(s)<=16?2:1);}
void drawAircraft(const mini::Aircraft &a,bool active,float range,bool stale){
    if(a.km>range)return;float x=120+a.east/range*104,y=120-a.north/range*104;
    float h=std::isfinite(a.heading)?a.heading*3.14159265f/180:0;
    auto seg=[&](float ax,float ay,float bx,float by){auto px=[&](float xx,float yy){return int(x+xx*cos(h)-yy*sin(h));};auto py=[&](float xx,float yy){return int(y+xx*sin(h)+yy*cos(h));};line(px(ax,ay),py(ax,ay),px(bx,by),py(bx,by),active?amber():stale?muted():altitude(a.alt));};
    seg(0,-5,0,5);seg(-5,1,0,-1);seg(0,-1,5,1);seg(-2,4,2,4);if(active)circle(int(x),int(y),8,amber());
}
void drawAirports(float range){
    if(!frame.settings.airportMode)return;
    const mini::Airport *items=frame.settings.airportMode==1?mini::ukAirports:frame.settings.airports.items;
    unsigned count=frame.settings.airportMode==1?sizeof(mini::ukAirports)/sizeof(mini::Airport):frame.settings.airports.count;
    for(unsigned i=0;i<count;++i){float east,north;const auto &a=items[i];
        if(!mini::project(a.lat,a.lon,frame.settings.lat,frame.settings.lon,east,north)||std::hypot(east,north)>range)continue;
        int x=120+int(east/range*104),y=120-int(north/range*104);auto color=rgb(54,99,103);
        // Keep the marker inside the scope, including airports exactly at its edge.
        for(int dx=-3;dx<=3;++dx)for(int dy=-3;dy<=3;++dy)if((abs(dx)==3||abs(dy)==3)&&(x+dx-120)*(x+dx-120)+(y+dy-120)*(y+dy-120)<=104*104)dot(x+dx,y+dy,color);
        int width=strlen(a.code)*6,left=x-width/2,top=y+6;
        if(top>=190)top=std::min(y-13,183);
        auto inside=[](int xx,int yy){return (xx-120)*(xx-120)+(yy-120)*(yy-120)<=104*104;};
        if(!inside(left,top+7)||!inside(left+width,top+7))top=y-13;
        if(inside(left,top)&&inside(left+width,top)&&inside(left,top+7)&&inside(left+width,top+7))textAt(left,top,a.code,color);
    }
}
void drawTrails(float range){
    if(!frame.settings.trails)return;
    const float scale=104/range,northMin=(120-stripY-stripHeight-1)/scale,northMax=(121-stripY)/scale;
    for(const auto &t:trails.tracks){
        bool active=frame.data.count&&selected<frame.data.count&&!strcmp(t.hex,frame.data.aircraft[selected].hex);
        for(unsigned i=1;i<t.count;++i){const auto &a=t.points[i-1],&b=t.points[i];
            if(std::max(a.north,b.north)<northMin||std::min(a.north,b.north)>northMax)continue;
            if(a.east*a.east+a.north*a.north>range*range||b.east*b.east+b.north*b.north>range*range)continue;
            // Each segment retains the altitude of the aircraft at that recorded point.
            line(120+int(a.east*scale),120-int(a.north*scale),120+int(b.east*scale),120-int(b.north*scale),altitude(b.alt,active?75:23));
        }
    }
}
void radar(uint32_t now){
    const float range=mini::ranges[requestedRange.load()];const auto &s=frame.data;
    const bool stale=!s.received||uint32_t(now-s.received)>30000;
    uint16_t grid=rgb(20,53,47);
    for(int r=26;r<=104;r+=26)circle(120,120,r,grid);
    line(16,120,224,120,grid);line(120,16,120,224,grid);circle(120,120,2,green());
    float sweep=(now%10000)*6.2831853f/10000;line(120,120,120+int(sin(sweep)*104),120-int(cos(sweep)*104),rgb(22,70,48));
    drawAirports(range);
    if(s.received&&uint32_t(now-s.received)<60000)drawTrails(range);
    bool watched=false;
    if(!stale)for(unsigned i=0;i<s.count;++i)if(s.aircraft[i].km<=range&&s.aircraft[i].seen+float(now-s.received)/1000<=30&&frame.settings.watch.contains(s.aircraft[i]))watched=true;
    if(watched&&frame.settings.alertBrightness){float pulse=.35f+.65f*(.5f-.5f*cos((now%3000)*6.2831853f/3000));unsigned level=frame.settings.alertBrightness*pulse;
        for(int r=116;r<=118;++r)circle(120,120,r,rgb(104*level/100,243*level/100,174*level/100));
    }
    if(s.received&&uint32_t(now-s.received)<60000)for(unsigned i=0;i<s.count;++i)drawAircraft(s.aircraft[i],i==selected,range,stale||s.aircraft[i].seen+float(now-s.received)/1000>30);
    text(8,"N",muted());text(224,"S",muted());
    char label[40];snprintf(label,sizeof(label),"%s  %d KM",rangeMode?"RANGE":"AIRCRAFT",int(range));text(196,label,green());
    snprintf(label,sizeof(label),"%u AIRCRAFT%s",s.count,s.total>mini::capacity?" NEAREST":"");text(211,label,muted());
    if(s.count&&selected<s.count){const auto &a=s.aircraft[selected];text(28,a.call[0]?a.call:a.reg[0]?a.reg:a.hex,amber());}
    if(!s.received){title(87,"ECHOSCOPE",green());small(145,frame.status,white());}
    else if(stale)text(169,"DATA STALE",amber());
    else if(s.range!=requestedRange)text(169,"UPDATING RANGE",amber());
    else if(!s.count)text(109,"NO AIRCRAFT IN RANGE",muted());
    else if(strcmp(frame.status,"LIVE POSITIONS")&&strcmp(frame.status,"UPDATING"))small(180,frame.status,amber());
}
void details(uint32_t now){
    if(!frame.data.count){title(70,"AIRCRAFT LEFT",amber());text(140,"DOUBLE CLICK TO GO BACK",muted());return;}
    const auto &a=frame.data.aircraft[selected];char lineText[40];
    title(30,a.call[0]?a.call:a.hex,amber());snprintf(lineText,sizeof(lineText),"%s / %s",a.reg[0]?a.reg:"UNKNOWN",a.type[0]?a.type:"TYPE N/A");small(55,lineText,muted());
    if(!detailPage){
        text(81,"ALTITUDE",muted());if(std::isfinite(a.alt))snprintf(lineText,sizeof(lineText),"%.0f FT",a.alt);else strcpy(lineText,"UNAVAILABLE");title(96,lineText,white());
        text(125,"GROUND SPEED",muted());if(std::isfinite(a.speed))snprintf(lineText,sizeof(lineText),"%.0f KT",a.speed);else strcpy(lineText,"UNAVAILABLE");title(140,lineText,white());
        snprintf(lineText,sizeof(lineText),"%.1f KM AWAY",a.km);text(171,lineText,green());
    }else{
        float bearing=atan2(a.east,a.north)*180/3.14159265f;if(bearing<0)bearing+=360;
        text(82,"DISTANCE / BEARING",muted());snprintf(lineText,sizeof(lineText),"%.1f KM / %.0f DEG",a.km,bearing);text(100,lineText,white());
        text(126,"AIRCRAFT HEADING",muted());if(std::isfinite(a.heading))snprintf(lineText,sizeof(lineText),"%.0f DEG",a.heading);else strcpy(lineText,"UNAVAILABLE");title(143,lineText,white());
        text(175,a.hex,muted());
    }
    uint32_t age=uint32_t(now-frame.data.received)/1000+unsigned(a.seen);
    snprintf(lineText,sizeof(lineText),age>30?"STALE / %lu SECONDS":"UPDATED %lu SECONDS",(unsigned long)age);text(196,lineText,age>30?amber():muted());
    if(frame.settings.watch.contains(a))text(13,"WATCH",green());
    circle(112,217,3,detailPage==0?green():muted());circle(128,217,3,detailPage==1?green():muted());
}
void setupScreen(){
    title(27,"SETUP",green());
    text(58,frame.connected?"CONNECTED TO":"JOIN WIFI",muted());
    char name[33];snprintf(name,sizeof(name),"%.30s",frame.ssid[0]?frame.ssid:apName);small(76,name,white());
    if(!frame.connected){text(100,"PASSWORD",muted());title(115,apPassword,white());}
    else {text(105,"OPEN IN YOUR BROWSER",muted());}
    title(146,frame.ip[0]?frame.ip:"192.168.4.1",green());
    text(181,frame.connected?"CLICK TO RETURN":"SAVE WIFI AND LOCATION",muted());
    text(204,"HOLD 5S FOR SETUP",muted());
}
void render(uint32_t now){
    for(stripY=0;stripY<H;stripY+=stripHeight){
        for(int i=0;i<W*stripHeight;++i)pixels[i]=rgb(3,13,16);
        if(view==View::Radar){
            radar(now);
            evilFlyby.draw(now,[](float x,float y,float xx,float yy,unsigned level){line(120+int(x*104),120+int(y*104),120+int(xx*104),120+int(yy*104),rgb(104*level/100,243*level/100,174*level/100));});
            if(evilFlyby.active)text(47,"UNIDENTIFIED",green());
        }
        else if(view==View::Details)details(now);
        else if(view==View::Setup)setupScreen();
        else {title(35,"ECHOSCOPE MINI",green());text(65,"MENU",muted());const char *items[]={"RADAR","SETUP","SLEEP"};for(unsigned i=0;i<3;++i){if(i==menuItem)rect(49,90+i*33,142,25,rgb(20,54,40));title(96+i*33,items[i],i==menuItem?green():muted());}text(205,"CLICK TO SELECT",muted());}
        if(!board->getLCD()->drawBitmap(0,stripY,W,stripHeight,reinterpret_cast<uint8_t*>(pixels),1000)){deviceLog.println("[display] transfer failed");return;}
    }
}
void setBacklight(unsigned percent){
    // MD50E schematic: GPIO8 drives Q1 (P-channel CJ3407), so LOW means ON.
    // The pinned vendor board profile has ON_LEVEL=1: invert its PWM percentage.
    if(!board->getBacklight()->setBrightness(mini::backlightDuty(percent)))deviceLog.println("[power] Backlight command failed");
}
void goSleep(){sleeping=true;setBacklight(0);board->getLCD()->setDisplayOnOff(false);deviceLog.println("[power] Sleeping; backlight off");}
unsigned availableAircraft(){unsigned n=0;while(n<frame.data.count&&frame.data.aircraft[n].km<=mini::ranges[requestedRange.load()])++n;return n;}
void controls(const KnobInput &state,uint32_t now){
    static KnobInput previous;
    bool action=state.freeSteps!=previous.freeSteps||state.heldSteps!=previous.heldSteps||state.down!=previous.down||state.singles!=previous.singles||state.doubles!=previous.doubles||state.holds!=previous.holds;
    if(action)lastActivity=now;
    if(action&&sleeping){sleeping=false;board->getLCD()->setDisplayOnOff(true);setBacklight(frame.settings.brightness);ignoreWake=true;refreshRequested=true;}
    if(ignoreWake){previous=state;if(!state.down&&!state.raw&&!state.pending)ignoreWake=false;return;}
    if(state.holds!=previous.holds){view=View::Setup;setupRequested=true;}
    int turns=state.freeSteps-previous.freeSteps,held=state.heldSteps-previous.heldSteps;
    if(held&&view==View::Radar)rangeMode=mini::wrap(int(rangeMode)+held,2);
    else if(held&&view==View::Details)detailPage=mini::wrap(int(detailPage)+held,2);
    if(turns){
        if(view==View::Menu)menuItem=mini::wrap(int(menuItem)+turns,3);
        else if(view==View::Radar&&rangeMode){requestedRange=std::max(0,std::min(3,requestedRange.load()+turns));refreshRequested=true;}
        else if(view==View::Radar||view==View::Details)selected=mini::wrap(int(selected)+turns,availableAircraft());
    }
    if(state.doubles!=previous.doubles){if(view==View::Radar){view=View::Menu;menuItem=0;}else view=View::Radar;}
    if(state.singles!=previous.singles){
        if(view==View::Radar&&availableAircraft()){view=View::Details;detailPage=0;}
        else if(view==View::Menu){if(menuItem==0)view=View::Radar;else if(menuItem==1)view=View::Setup;else goSleep();}
        else if(view==View::Setup)view=View::Radar;
    }
    if(frame.data.count&&selected<frame.data.count)snprintf(selectedHex,sizeof(selectedHex),"%s",frame.data.aircraft[selected].hex);
    previous=state;
}
void setup(){
    Serial.begin(115200);delay(500);deviceLog.println("EchoScope Mini 0.16.0-dev.3 / ESP32-C3 standalone");
    uint64_t mac=ESP.getEfuseMac();snprintf(apName,sizeof(apName),"EchoMini-%04X",unsigned((mac>>32)&0xffff));snprintf(apPassword,sizeof(apPassword),"%08lX",(unsigned long)esp_random());snprintf(csrf,sizeof(csrf),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
    stateMutex=xSemaphoreCreateMutex();assert(stateMutex);input.holdMs=5000;
    pinMode(pinA,INPUT_PULLUP);pinMode(pinB,INPUT_PULLUP);pinMode(pinButton,INPUT_PULLUP);previousAB=(digitalRead(pinA)<<1)|digitalRead(pinB);
    attachInterrupt(pinA,encoder,CHANGE);attachInterrupt(pinB,encoder,CHANGE);
    board=new Board();assert(board->init());assert(board->begin());pixels=static_cast<uint16_t*>(heap_caps_malloc(W*stripHeight*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));assert(pixels);setBacklight(70);
    assert(xTaskCreate(sampleInputs,"mini-input",2048,nullptr,3,nullptr)==pdPASS);
    assert(xTaskCreate(networkTask,"mini-network",12288,nullptr,1,nullptr)==pdPASS);
    lastActivity=millis();deviceLog.printf("[ready] Mini heap=%u largest=%u\n",ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
}
void loop(){
    static uint32_t lastDraw=0,lastLog=0,renderMs=0;static unsigned bright=70;
    uint32_t now=millis();{Lock lock;frame=shared;}
    if(setupRequested.exchange(false)){view=View::Setup;lastActivity=now;}
    trails.update(frame.data);
    if(seenGeneration!=frame.generation){seenGeneration=frame.generation;selected=0;for(unsigned i=0;i<frame.data.count;++i)if(!strcmp(frame.data.aircraft[i].hex,selectedHex))selected=i;}
    KnobInput state;portENTER_CRITICAL(&inputMux);state=input;portEXIT_CRITICAL(&inputMux);selected=std::min(selected,availableAircraft()?availableAircraft()-1:0);controls(state,now);setupVisible=view==View::Setup;
    if(!sleeping&&bright!=frame.settings.brightness){bright=frame.settings.brightness;setBacklight(bright);}
    if(!sleeping&&view!=View::Setup&&frame.settings.sleepMinutes&&uint32_t(now-lastActivity)>=frame.settings.sleepMinutes*60000u)goSleep();
    evilFlyby.enabled=fun::enabledBy(frame.settings.watch.calls);
    evilFlyby.tick(now,!sleeping.load() && view==View::Radar,esp_random());
    if(!sleeping&&now-lastDraw>=200){lastDraw=now;render(now);renderMs=millis()-now;}
    if(now-lastLog>=15000){lastLog=now;deviceLog.printf("[mini] wifi=%d aircraft=%u heap=%u minimum=%u largest=%u render=%lums\n",frame.connected,frame.data.count,ESP.getFreeHeap(),ESP.getMinFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned long)renderMs);}
    delay(2);
}
