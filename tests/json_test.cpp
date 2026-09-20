#include "aircraft_json.h"
#include <cassert>
#include <iostream>
int main() {
    JsonDocument doc;
    auto err=deserializeJson(doc,R"({"ac":[
      {"hex":"abc001","flight":"TEST123  ","lat":51.5,"lon":0,"seen_pos":0,"alt_baro":12000,"gs":180,"track":90},
      {"hex":"ground","lat":51.5,"lon":0,"seen_pos":0,"alt_baro":"ground"},
      {"hex":"missing","lat":null,"lon":0,"seen_pos":0},
      {"hex":"stale","lat":51.5,"lon":0,"seen_pos":61},
      {"hex":"unknown","lat":51.5,"lon":0,"seen_pos":2}
    ]})");
    assert(!err); sky::Snapshot out; assert(sky::parseAircraft(doc,out,51.5,0,1234));
    assert(out.count==2 && !strcmp(out.aircraft[0].callsign,"TEST123"));
    assert(out.aircraft[0].altitude==12000 && out.aircraft[0].speed==180);
    assert(std::isnan(out.aircraft[1].altitude) && std::isnan(out.aircraft[1].track));
    doc.clear(); doc["error"]="unavailable"; assert(!sky::parseAircraft(doc,out,51.5,0,0));
    doc.clear(); doc["ac"].to<JsonArray>(); assert(sky::parseAircraft(doc,out,51.5,0,0) && out.count==0);
    for(int i=0;i<80;++i) { auto a=doc["ac"].add<JsonObject>(); a["hex"]=std::to_string(i); a["lat"]=51.5; a["lon"]=i/1000.0; a["seen_pos"]=0; }
    assert(sky::parseAircraft(doc,out,51.5,0,0) && out.count==sky::maxAircraft);
    std::cout << "JSON checks passed: valid/empty/error feeds, ground/stale/missing positions, unknown metrics and capacity.\n";
}
