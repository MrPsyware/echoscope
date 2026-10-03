#pragma once
#include <ArduinoJson.h>
#include "model.h"
#include "feed_stream.h"
namespace mini {
struct Decoder {
    AircraftStream stream;
    Snapshot snapshot;
    double lat,lon;
    JsonDocument filter,doc;
    char error[64]{};
    Decoder(double homeLat,double homeLon,int range):lat(homeLat),lon(homeLon){
        snapshot.range=range;
        for(auto key:{"hex","flight","r","t","lat","lon","alt_baro","alt_geom","gs","track","seen_pos"})filter[key]=true;
    }
    static float number(JsonVariantConst v){return v.is<double>()?v.as<float>():NAN;}
    static void clean(char *out,size_t n,const char *value){
        size_t i=0;if(value)for(;*value&&i+1<n;++value)if(*value>=32&&*value<127)out[i++]=*value;
        while(i&&out[i-1]==' ')--i;out[i]=0;
    }
    bool aircraft(const char *bytes,size_t size){
        auto e=deserializeJson(doc,bytes,size,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(24));
        if(e||!doc.is<JsonObject>()){snprintf(error,sizeof(error),"JSON %s",e?e.c_str():"object required");return false;}
        Aircraft a;
        double y=number(doc["lat"]),x=number(doc["lon"]);
        if(!project(y,x,lat,lon,a.east,a.north))return true;
        a.seen=number(doc["seen_pos"]);if(!std::isfinite(a.seen)||a.seen<0||a.seen>30)return true;
        a.km=std::hypot(a.east,a.north);if(a.km>ranges[snapshot.range])return true;
        clean(a.hex,sizeof(a.hex),doc["hex"] | "");if(!a.hex[0])return true;
        clean(a.call,sizeof(a.call),doc["flight"] | "");clean(a.reg,sizeof(a.reg),doc["r"] | "");clean(a.type,sizeof(a.type),doc["t"] | "");
        a.alt=number(doc["alt_baro"]);if(!std::isfinite(a.alt))a.alt=number(doc["alt_geom"]);
        if(doc["alt_baro"].is<const char*>()&&!strcmp(doc["alt_baro"],"ground"))a.alt=0;
        a.speed=number(doc["gs"]);a.heading=number(doc["track"]);
        keep(snapshot,a);return true;
    }
    bool put(char c){return stream.put(c,[this](const char *p,size_t n){return aircraft(p,n);});}
    bool finish(){
        if(!stream.complete()){if(!error[0])snprintf(error,sizeof(error),"%s",stream.error?stream.error:"Incomplete JSON");return false;}
        auto e=deserializeJson(doc,static_cast<const char*>(stream.envelope));
        if(e||!doc["ac"].is<JsonArray>()){snprintf(error,sizeof(error),"Invalid feed envelope");return false;}
        const char *message=doc["msg"] | "No error";
        if(strcmp(message,"No error")){snprintf(error,sizeof(error),"Feed reports error");return false;}
        return true;
    }
};
}
