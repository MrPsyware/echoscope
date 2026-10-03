// Standalone diagnostic only: no Wi-Fi, credentials, NVS writes or EchoScope feed.
#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <esp_heap_caps.h>
#include <cmath>
#include "input.h"
using esp_panel::board::Board;
constexpr int pinA=7,pinB=6,pinButton=9,W=240,H=240,stripHeight=16;
Board *board;
uint16_t *pixels;
int stripY;
volatile int32_t ticks=0;
volatile uint32_t invalid=0;
volatile uint8_t previousAB=0;
portMUX_TYPE inputMux=portMUX_INITIALIZER_UNLOCKED;
DRAM_ATTR const int8_t transitions[16]={0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
void ARDUINO_ISR_ATTR encoder() {
    portENTER_CRITICAL_ISR(&inputMux);
    uint8_t ab=(digitalRead(pinA)<<1)|digitalRead(pinB);
    if((previousAB^ab)==3) ++invalid;
    ticks+=transitions[(previousAB<<2)|ab]; previousAB=ab;
    portEXIT_CRITICAL_ISR(&inputMux);
}
// Five-column diagnostic glyphs, digits then A-Z; each bit is one pixel row.
const uint8_t glyphs[][5]={
{62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},
{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},
{126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},
{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},
{127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},
{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},
{63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67}};
uint16_t rgb(uint8_t r,uint8_t g,uint8_t b) {uint16_t c=((r>>3)<<11)|((g>>2)<<5)|(b>>3);return (c>>8)|(c<<8);}
void dot(int x,int y,uint16_t color) {if(x>=0&&x<W&&y>=stripY&&y<stripY+stripHeight&&y<H)pixels[(y-stripY)*W+x]=color;}
void rect(int x,int y,int w,int h,uint16_t c){for(int yy=max(y,stripY);yy<min(y+h,stripY+stripHeight);++yy)for(int xx=max(0,x);xx<min(W,x+w);++xx)dot(xx,yy,c);}
void text(int y,const char *s,uint16_t color,int scale=1) {
    int x=(W-int(strlen(s))*6*scale)/2;
    for(;*s;++s,x+=6*scale){
        int n=*s>='0'&&*s<='9'?*s-'0':*s>='A'&&*s<='Z'?*s-'A'+10:-1;
        for(int col=0;col<5;++col){uint8_t bits=n>=0?glyphs[n][col]:*s=='-'?8:*s=='+'?(col==2?62:8):0;
            for(int row=0;row<7;++row)if(bits&(1<<row))rect(x+col*scale,y+row*scale,scale,scale,color);
        }
    }
}
KnobInput input;
void sampleInputs(void *) {
    TickType_t wake=xTaskGetTickCount();
    for(;;){
        const uint32_t now=millis();const bool pressed=digitalRead(pinButton)==LOW;
        portENTER_CRITICAL(&inputMux);input.sample(now,pressed,ticks);portEXIT_CRITICAL(&inputMux);
        vTaskDelayUntil(&wake,max(TickType_t(1),pdMS_TO_TICKS(2)));
    }
}
void render(const KnobInput &state,uint32_t bad) {
    const int32_t count=state.steps;const bool pressed=state.down;const char *eventName=state.event;
    uint16_t green=rgb(104,243,174),muted=rgb(140,177,170),white=rgb(235,250,245);
    char value[32],counts[40],heap[24];snprintf(value,sizeof(value),"STEP %+ld",(long)count);
    snprintf(counts,sizeof(counts),"S %lu D %lu H %lu",(unsigned long)state.singles,(unsigned long)state.doubles,(unsigned long)state.holds);
    snprintf(heap,sizeof(heap),"HEAP %luK",(unsigned long)(ESP.getFreeHeap()/1024));
    for(stripY=0;stripY<H;stripY+=stripHeight){
        for(int n=0;n<W*stripHeight;++n)pixels[n]=rgb(3,13,16);
        for(int d=0;d<720;++d){float a=d*3.14159265f/360;dot(120+int(sin(a)*114),120+int(cos(a)*114),pressed?rgb(255,181,70):green);}
        float angle=count*3.14159265f/40;rect(118+int(sin(angle)*107),118-int(cos(angle)*107),5,5,white);
        text(25,"ECHOSCOPE",green,2);text(47,"C3 HARDWARE TEST",muted);
        text(70,value,white,2);text(96,pressed?"BUTTON DOWN":"BUTTON UP",pressed?rgb(255,181,70):muted);
        text(119,eventName,green,strlen(eventName)>16?1:2);
        text(145,counts,white);text(160,heap,muted);
        rect(75,181,26,10,rgb(255,0,0));rect(107,181,26,10,rgb(0,255,0));rect(139,181,26,10,rgb(0,0,255));
        text(199,"TURN CLICK HOLD",muted);text(213,bad?"ENCODER SKIPS":"240 X 240",muted);
        if(!board->getLCD()->drawBitmap(0,stripY,W,stripHeight,reinterpret_cast<uint8_t*>(pixels),1000)){Serial.println("[display] transfer failed");return;}
    }
}
void setup(){
    Serial.begin(115200);delay(1000);Serial.println("EchoScope C3 hardware test - no network or saved settings");
    Serial.printf("[chip] %s revision=%d flash=%lu heap=%lu\n",ESP.getChipModel(),ESP.getChipRevision(),(unsigned long)ESP.getFlashChipSize(),(unsigned long)ESP.getFreeHeap());
    pinMode(pinA,INPUT_PULLUP);pinMode(pinB,INPUT_PULLUP);pinMode(pinButton,INPUT_PULLUP);
    previousAB=(digitalRead(pinA)<<1)|digitalRead(pinB);
    attachInterrupt(pinA,encoder,CHANGE);attachInterrupt(pinB,encoder,CHANGE);
    board=new Board();assert(board->init());assert(board->begin());
    pixels=static_cast<uint16_t*>(heap_caps_malloc(W*stripHeight*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));assert(pixels);
    board->getBacklight()->setBrightness(70);
    assert(xTaskCreate(sampleInputs,"knob-input",2048,nullptr,3,nullptr)==pdPASS);
    Serial.println("[ready] test v2; 2 transitions/notch; direction reversed; independent input task; debounce 15ms; double window 400ms");
}
void loop(){
    static uint32_t lastDraw=0,lastLog=0,previousEvents=0;
    static int32_t previousRaw=0;
    KnobInput state;uint32_t bad;
    portENTER_CRITICAL(&inputMux);state=input;bad=invalid;portEXIT_CRITICAL(&inputMux);
    uint32_t now=millis();
    if(state.events!=previousEvents||state.rawTicks!=previousRaw){
        Serial.printf("[input] %s raw=%ld steps=%ld down=%d S=%lu D=%lu H=%lu skips=%lu\n",state.event,(long)state.rawTicks,(long)state.steps,state.down,(unsigned long)state.singles,(unsigned long)state.doubles,(unsigned long)state.holds,(unsigned long)bad);
        previousEvents=state.events;previousRaw=state.rawTicks;
    }
    if(now-lastDraw>=100){lastDraw=now;render(state,bad);}
    if(now-lastLog>=10000){lastLog=now;Serial.printf("[alive] test=v2 heap=%lu\n",(unsigned long)ESP.getFreeHeap());}
    delay(1);
}
