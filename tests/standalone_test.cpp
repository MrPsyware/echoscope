#include "standalone_airports.h"
#include "standalone_json.h"
#include "standalone_map_math.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace standalone;
    CustomAirports custom;
    assert(parseAirports("LGW: -0.185739/51.148744\nJFK: -73.7781/40.6413",custom));
    assert(custom.count==2 && custom.items[0].lon<0);
    for(const char *bad:{"LGW", "L:", "LGW: 181/50", "LGW: 0/nan", "LGW: 0/0\nlgw: 1/1", "LGW: 0/0 rubbish"}) {
        assert(!parseAirports(bad,custom)); assert(custom.count==2);
    }
    NearbyAirport near[9]; CustomAirports empty;
    unsigned n=nearby(51.5,0,empty,true,near,9,100);
    bool lgw=false,lhr=false;
    for(unsigned i=0;i<n;++i) { if(!strcmp(near[i].airport.code,"LGW")) { lgw=true; assert(near[i].airport.count>0); } if(!strcmp(near[i].airport.code,"LHR")) lhr=true; }
    assert(lgw&&lhr);
    n=nearby(51.5,0,empty,true,near,9,100,"JFK"); assert(n && !strcmp(near[0].airport.code,"JFK"));
    n=nearby(51.5,0,custom,false,near,9,100); assert(n==1 && !strcmp(near[0].airport.code,"LGW"));
    assert(nearby(0,0,empty,false,near,9,100)==0);
    n=nearby(51.5,0,custom,true,near,9,100,"LGW"); assert(n && near[0].airport.count==0); // custom override
    for(int zoom=0;zoom<16;++zoom) {
        const auto p=mapPixel(0,0,25,zoom,210,210);
        assert(std::abs(p.x-128*(1<<zoom))<=1 && std::abs(p.y-128*(1<<zoom))<=1);
        const auto edge=mapPixel(0,179.999,100,zoom,400,210);
        assert(edge.x>=0 && edge.x<256*(1<<zoom));
    }
    assert(!inMap(0,0)); assert(inMap(210,210));
    assert(mapZoom(51.5,5)>mapZoom(51.5,100));
    // RGB565 reference values from the info server's NumPy map palette.
    assert(mapBackground==0x0062);
    assert(mapColor(255,255,255)==mapBackground);
    assert(mapColor(0,0,0)==0x1a88);
    assert(mapColor(128,128,128)==0x1165);
    assert(mapColor(170,211,223)==0x08c3); // OSM water
    assert(mapColor(242,239,233)==0x0082); // OSM land
    assert(mapColor(255,0,0)==0x11c6);
    assert(mapColor(0,255,0)==0x0944);
    assert(mapColor(0,0,255)==0x1a47);
    JsonDocument raw,out;
    assert(!weatherPages(raw.as<JsonVariantConst>(),out,1700000000));
    const int64_t day=1700006400;
    for(int i=0;i<96;++i) {
        raw["hourly"]["time"].add(day+i*3600);
        raw["hourly"]["cloud_cover"].add(i<24?20:80);
        raw["hourly"]["weather_code"].add(3);
        raw["hourly"]["is_day"].add(i%24>=7&&i%24<18?1:0);
        raw["hourly"]["temperature_2m"].add(10);
    }
    for(int i=0;i<4;++i) { raw["daily"]["time"].add(day+i*86400); raw["daily"]["weather_code"].add(0); }
    assert(weatherPages(raw.as<JsonVariantConst>(),out,day));
    assert(out["pages"].size()==3);
    assert(out["pages"][0]["cards"][0]["label"]=="01:00");
    assert(out["pages"][0]["cards"][0]["rain"]=="Rain --");
    assert(out["pages"][1]["cards"][0]["cloud"]=="Avg cloud 20%");
    assert(out["pages"][2]["cards"].size()==3);
    raw.clear(); assert(!routePages(raw.as<JsonVariantConst>(),"G-EHAT",out,day));
    assert(out["pages"][0]["lines"][0]=="Unavailable");
    raw["response"]["flightroute"]["origin"]["iata_code"]="LGW";
    raw["response"]["flightroute"]["destination"]["icao_code"]="KJFK";
    assert(routePages(raw.as<JsonVariantConst>(),"EZY123",out,day));
    assert(out["pages"][0]["lines"][1]=="LGW > KJFK");
    std::cout<<"Standalone airport, forecast, route and projection checks passed\n";
}
