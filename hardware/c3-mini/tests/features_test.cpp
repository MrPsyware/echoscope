#include "../src/mini_features.h"
#include <cassert>
#include <cstdio>
#include <string>
int main(){
    assert(fun::enabledBy("EZY123, dr evil"));
    assert(!mini::matches("dr evil", "DR",true));
    assert(!mini::matches("dr evil", "EVIL",true));
    assert(mini::matches("dr evil, EZY123", "EZY123",true));
    using namespace mini;
    assert(backlightDuty(0)==100 && backlightDuty(100)==0 && backlightDuty(25)==75 && backlightDuty(200)==0);
    assert(matches("a388, B744\nA320","A388"));assert(!matches("A388","A38"));assert(!matches(" , ",""));assert(!matches("B744","A388"));
    assert(validWatch("G-XLEA, EZY123"));assert(!validWatch("<script>"));assert(!validWatch(std::string(96,'A').c_str()));
    Watchlist w;strcpy(w.types,"A388");Aircraft a;strcpy(a.type,"A388");assert(w.contains(a));strcpy(a.type,"A320");assert(!w.contains(a));
    AirportList list;assert(parseAirports("lgw: -0.190278/51.148102\r\nLHR: -0.4543/51.47\n",list));assert(list.count==2&&!strcmp(list.items[0].code,"LGW"));assert(list.items[0].lon<0&&list.items[0].lat>51);
    for(auto invalid:{"LGW: 181/51","LGW: 0/91","LGW: nan/51","LGW: 0/inf","LGW: 0/51 rubbish","LGW 0/51","LGW: /51","LGW: 0/","TOOLONGCODE: 0/51","LGW: 0/51\nlgw: 1/52"}){assert(!parseAirports(invalid,list));assert(list.count==2);}
    std::string lots;for(int i=0;i<33;++i)lots+="AP"+std::to_string(i)+": 0/51\n";assert(!parseAirports(lots.c_str(),list));assert(parseAirports(" \n",list)&&list.count==0);
    bool lhr=false,lgw=false;for(auto &p:ukAirports){assert(p.lat>49&&p.lat<61&&p.lon>-9&&p.lon<3);lhr|=!strcmp(p.code,"LHR");lgw|=!strcmp(p.code,"LGW");}assert(lhr&&lgw);
    Trails trails;Snapshot s;s.count=1;s.received=1000;strcpy(s.aircraft[0].hex,"abcdef");s.aircraft[0].alt=1000;
    trails.update(s);assert(trails.tracks[0].count==1);trails.update(s);assert(trails.tracks[0].count==1);
    for(unsigned i=1;i<40;++i){s.received+=5000;s.aircraft[0].east=i*.1;s.aircraft[0].alt=1000+i*1000;trails.update(s);}
    auto &t=trails.tracks[0];assert(t.count==32&&t.points[0].alt==9000&&t.points[31].alt==40000);
    s.received+=5000;trails.update(s);assert(t.count==32); // stationary observation adds nothing
    s.received+=31000;s.aircraft[0].east+=1;trails.update(s);assert(t.count==1); // no line across feed outage
    s.received+=5000;s.count=0;trails.update(s);assert(t.count==0); // departed aircraft released
    s.count=32;for(unsigned i=0;i<32;++i)snprintf(s.aircraft[i].hex,9,"%06x",i);s.received+=5000;trails.update(s);
    unsigned count=0;for(auto &track:trails.tracks)count+=track.count>0;assert(count==32);
    s=Snapshot{};trails.update(s);for(auto &track:trails.tracks)assert(!track.count);
    printf("Mini airport, watchlist and trail tests passed; history storage=%zu bytes\n",sizeof(Trails));
}
