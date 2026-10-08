#include "watch_editor.h"
#include "setup_qr.h"
#include <cassert>
#include <iostream>
int main(){
 const char *ok=R"([{"kind":"type","value":"A388","label":"Airbus A380","enabled":true},{"kind":"callsign","value":"EZY123","label":"Sue coming home","enabled":false}])";
 assert(setupui::watchMetadata(ok,"A388","","",false));
 assert(!setupui::watchMetadata(ok,"A388","","EZY123",false));
 assert(!setupui::watchMetadata("{}","","","",true));
 assert(!setupui::watchMetadata("[null]","","","",true));
 assert(!setupui::watchMetadata(R"([{"kind":"type","value":"B74*","label":"Jumbo","enabled":true}])","B74*","","",false));
 assert(setupui::watchMetadata(R"([{"kind":"type","value":"B74*","label":"Jumbo","enabled":true}])","B74*","","",true));
 assert(setupui::watchMetadata(R"([{"kind":"callsign","value":"DR EVIL","label":"Fun","enabled":true}])","","","DR EVIL",false));
 assert(!setupui::watchMetadata(R"([{"kind":"callsign","value":"DR EVIL, BA1","label":"Fun","enabled":true}])","","","DR EVIL, BA1",true));
 setupui::QR qr;assert(qr.prepare("http://192.168.2.151/"));int white=0,black=0;
 qr.draw(120,65,126,[&](int x,int y,int w,int h,bool light){assert(x>=0&&y>=0&&x+w<=240&&y+h<=192);if(light)++white;else ++black;});
 assert(white==1&&black>20);assert(qr.prepare("WIFI:T:WPA;S:EchoScope-Setup;P:echo-12345678;;"));
 std::cout<<"Watch metadata, legacy compatibility, wildcard limits and bounded setup QR checks passed.\n";
}
