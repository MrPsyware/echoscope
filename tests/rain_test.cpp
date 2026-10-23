#include "rain_model.h"
#include <cassert>
#include <set>
#include <iostream>
int main() {
    constexpr uint32_t now=1792789800;
    rain::Timeline t; JsonDocument doc;
    doc["host"]="https://tilecache.rainviewer.com";
    auto add=[&](uint32_t stamp,const char *path) { auto e=doc["radar"]["past"].add<JsonObject>(); e["time"]=stamp; e["path"]=path; };
    add(now-7200,"/v2/radar/abc123"); add(now-600,"/v2/radar/def456"); add(now,"/v2/radar/latest");
    assert(t.parse(doc.as<JsonVariantConst>(),now) && t.count==3 && t.frames[2].time==now);
    assert(!rain::stale(now,now+600)); assert(rain::stale(now,now+2701));
    doc["host"]="http://tilecache.rainviewer.com"; assert(!t.parse(doc.as<JsonVariantConst>(),now));
    doc["host"]="https://tilecache.rainviewer.com"; doc["radar"]["past"].clear();
    add(now+1000,"/v2/radar/future"); add(now-10801,"/v2/radar/old");
    add(now,"/v2/radar/../evil"); add(now,"/v2/radar/abc?query"); add(now,"/v2/radar/");
    assert(!t.parse(doc.as<JsonVariantConst>(),now));
    doc["radar"]["past"].clear();
    for(unsigned i=0;i<20;++i) add(now-6000+i*300,"/v2/radar/hash");
    assert(t.parse(doc.as<JsonVariantConst>(),now) && t.count==13 && t.frames[0].time==now-3900);
    const uint8_t wet[]={0xff,0x44,0,255},dry[]={0,0,0,0};
    auto wetPixel=rain::overlay(wet,0,10,10); assert(wetPixel.r==255 && wetPixel.g==0x44 && wetPixel.a==210);
    assert(rain::overlay(dry,0,10,10).a==0); // clear covered pixel preserves the map
    assert(rain::overlay(dry,255,0,0).a==150); // uncovered is never reported as clear
    assert(rain::overlay(wet,255,5,0).a==30); // coverage masks weather, including hatch gaps
    // Every supported viewport fits the bounded tile table, including the date line.
    for(double lat:{-85.,-60.,0.,51.5,85.}) for(double lon:{-179.99,0.,179.99}) for(double km:sky::ranges) {
        int z=rain::zoom(lat,km); assert(z>=0 && z<=7); std::set<std::pair<int,int>> tiles;
        for(int y=0;y<420;++y) for(int x=0;x<420;++x) if(standalone::inMap(x,y)) {
            auto p=standalone::mapPixel(lat,lon,km,z,x,y);
            assert(p.x>=0 && p.x<256*(1<<z) && p.y>=0 && p.y<256*(1<<z));
            tiles.emplace(p.x/256,p.y/256);
        }
        assert(tiles.size()<=16);
    }
    std::cout<<"Rain manifest validation, staleness and viewport bounds passed\n";
}
