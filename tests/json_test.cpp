#include "aircraft_json.h"
#include "feed_json.h"
#include "feed_buffer.h"
#include <cassert>
#include <iostream>
int main() {
    JsonDocument doc;
    const std::string classified=R"({"ac":[{"hex":"one","t":"H47","desc":"BOEING CH-47 Chinook","dbFlags":9,"lat":51.5,"lon":0,"seen_pos":0},{"hex":"two","category":"A7","lat":51.5,"lon":0,"seen_pos":0},{"hex":"three","category":"A3","dbFlags":8,"lat":51.5,"lon":0,"seen_pos":0}]})";
    sky::FeedJsonReader classes{classified.data(),classified.size()};
    assert(!sky::decodeFeed(doc,classes)); sky::Snapshot classifiedOut;
    assert(sky::parseAircraft(doc,classifiedOut,51.5,0,0,sky::Filter::Military));
    assert(classifiedOut.count==1 && classifiedOut.aircraft[0].military);
    assert(classifiedOut.aircraft[0].kind==sky::AircraftKind::Rotorcraft);
    assert(!strcmp(classifiedOut.aircraft[0].description,"BOEING CH-47 Chinook"));
    assert(sky::parseAircraft(doc,classifiedOut,51.5,0,0,sky::Filter::Rotorcraft) && classifiedOut.count==2);
    doc.clear();
    // Non-terminated network chunks spanning 64 KiB must survive byte-for-byte.
    std::string large="{\"ignored\":\""+std::string(140000,'x')+"\",\"ac\":[]}";
    sky::FeedBuffer buffer(large.size());
    for(size_t at=0;at<large.size();) {
        const size_t n=std::min(size_t(371),large.size()-at);
        auto chunk=std::make_unique<char[]>(n);
        std::memcpy(chunk.get(),large.data()+at,n);
        assert(buffer.append(chunk.get(),n)); at+=n;
    }
    assert(buffer.length()==large.size());
    assert(!std::memcmp(buffer.c_str(),large.data(),large.size()));
    assert(buffer.c_str()[large.size()]==0);
    assert(!buffer.append("x",1));
    sky::FeedJsonReader largeReader{buffer.c_str(),buffer.length()};
    assert(!sky::decodeFeed(doc,largeReader) && doc["ac"].is<JsonArray>());
    sky::FeedBuffer failed(10,[](size_t)->void*{return nullptr;});
    assert(!failed.allocated() && !failed.append("x",1));
    doc.clear();
    // Exercise the exact filtered decoder used by the device, including failure offsets.
    const std::string valid=R"({"ac":[{"hex":"abc","lat":51.5,"lon":0,"seen_pos":0}],"ignored":[1,2]})";
    sky::FeedJsonReader reader{valid.data(),valid.size()};
    assert(!sky::decodeFeed(doc,reader));
    assert(doc["ac"].size()==1 && !doc["ignored"].is<JsonArray>());
    const std::string broken=R"({"ac":[{"lat":@}]})";
    reader={broken.data(),broken.size()};
    assert(sky::decodeFeed(doc,reader)==DeserializationError::InvalidInput);
    assert(reader.position-1==broken.find('@'));
    const std::string truncated=R"({"ac":[{"lat":51)";
    reader={truncated.data(),truncated.size()};
    assert(sky::decodeFeed(doc,reader)==DeserializationError::IncompleteInput);
    assert(reader.position==truncated.size());
    reader={valid.data(),0};
    assert(sky::decodeFeed(doc,reader)==DeserializationError::EmptyInput);
    doc.clear();
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
    auto military=doc["ac"].add<JsonObject>(); military["hex"]="military"; military["dbFlags"]=1;
    military["lat"]=51.5; military["lon"]=0.5; military["seen_pos"]=0;
    assert(sky::parseAircraft(doc,out,51.5,0,0,sky::Filter::Military) && out.count==1);
    assert(!strcmp(out.aircraft[0].hex,"military"));
    std::cout << "JSON checks passed: valid/empty/error feeds, ground/stale/missing positions, unknown metrics and capacity.\n";
}
