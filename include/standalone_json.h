#pragma once
#include <ArduinoJson.h>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
namespace standalone {
inline int weatherIcon(int code) {
    if(code==0 || code==1) return 0;
    if(code==2) return 1;
    if(code==3) return 2;
    if(code==45||code==48) return 6;
    if(code==71||code==73||code==75||code==77||code==85||code==86) return 4;
    if(code==95||code==96||code==99) return 5;
    if((code>=51&&code<=67)||code==80||code==81||code==82) return 3;
    return 7;
}
inline std::string number(JsonVariantConst v,const char *suffix="") {
    if(!v.is<double>() || !std::isfinite(v.as<double>())) return "--";
    char text[48]; snprintf(text,sizeof(text),"%.0f%s",v.as<double>(),suffix); return text;
}
inline std::string dateLabel(int64_t stamp,const char *format) {
    time_t t=stamp; tm value{}; gmtime_r(&t,&value); char s[32]; strftime(s,sizeof(s),format,&value); return s;
}
inline void weatherCard(JsonObject c,JsonVariantConst code) {
    const int icon=code.is<int>()?weatherIcon(code.as<int>()):7;
    const char *names[]={"Clear","Partly cloudy","Overcast","Rain","Snow","Thunderstorms","Fog","Unavailable"};
    c["icon"]=icon; c["condition"]=names[icon];
}
// API uses Unix timestamps with timezone=GMT, so all labels are explicitly UTC.
inline bool weatherPages(JsonVariantConst data,JsonDocument &out,int64_t now) {
    auto hourly=data["hourly"],daily=data["daily"];
    const auto times=hourly["time"].as<JsonArrayConst>();
    if(times.size()<24 || daily["time"].size()<4) return false;
    size_t next=0; while(next<times.size() && times[next].as<int64_t>()<=now) ++next;
    if(next==times.size()) return false;
    out.clear(); out["generated"]=now; out["source"]="Open-Meteo / CC BY 4.0";
    auto pages=out["pages"].to<JsonArray>();
    auto hour=pages.add<JsonObject>(); hour["title"]="NEXT HOUR"; hour["layout"]="weather";
    hour["subtitle"]=dateLabel(times[next],"%d %b / UTC"); hour["note"]="Forecast / UTC";
    auto c=hour["cards"].to<JsonArray>().add<JsonObject>();
    c["label"]=dateLabel(times[next],"%H:%M"); weatherCard(c,hourly["weather_code"][next]);
    c["temperature"]=number(hourly["temperature_2m"][next]," C");
    c["cloud"]="Cloud "+number(hourly["cloud_cover"][next],"%");
    c["rain"]="Rain "+number(hourly["precipitation_probability"][next],"%");
    c["wind"]="Wind "+number(hourly["wind_speed_10m"][next]," km/h");
    c["night"]=hourly["is_day"][next]==0;
    JsonObject today=pages.add<JsonObject>(),later=pages.add<JsonObject>();
    today["title"]="TODAY"; later["title"]="NEXT 3 DAYS";
    for(auto p:{today,later}) { p["layout"]="weather"; p["subtitle"]="UTC"; }
    later["note"]="Cloud: daily mean / rain: peak chance";
    double night=0; unsigned nightCount=0;
    // Next future night only: do not mix tomorrow's night into tonight's mean.
    bool started=false;
    for(size_t j=next;j<times.size();++j) {
        if(hourly["is_day"][j]==0) {
            started=true;
            if(hourly["cloud_cover"][j].is<double>()) { night+=hourly["cloud_cover"][j].as<double>(); ++nightCount; }
        } else if(started) break;
    }
    char note[64]; if(nightCount) snprintf(note,sizeof(note),"Next night cloud %.0f%%",night/nightCount); else snprintf(note,sizeof(note),"Night cloud unavailable");
    today["note"]=note;
    for(unsigned n=0;n<4;++n) {
        auto p=n==0?today:later; auto card=p["cards"].as<JsonArray>(); if(card.isNull()) card=p["cards"].to<JsonArray>();
        auto d=card.add<JsonObject>(); const int64_t start=daily["time"][n].as<int64_t>();
        d["label"]=dateLabel(start,"%a %d"); weatherCard(d,daily["weather_code"][n]);
        d["temperature"]=number(daily["temperature_2m_min"][n])+" / "+number(daily["temperature_2m_max"][n]," C");
        d["rain"]="Rain "+number(daily["precipitation_probability_max"][n],"%");
        d["wind"]="Wind "+number(daily["wind_speed_10m_max"][n]," km/h");
        double sum=0; unsigned count=0;
        for(size_t j=0;j<times.size();++j) if(times[j].as<int64_t>()>=start && times[j].as<int64_t>()<start+86400 && hourly["cloud_cover"][j].is<double>()) { sum+=hourly["cloud_cover"][j].as<double>(); ++count; }
        char cloud[32]; if(count) snprintf(cloud,sizeof(cloud),"Avg cloud %.0f%%",sum/count); else snprintf(cloud,sizeof(cloud),"Avg cloud --"); d["cloud"]=cloud;
    }
    return !out.overflowed();
}
inline bool routePages(JsonVariantConst data,const char *call,JsonDocument &out,int64_t now) {
    auto route=data["response"]["flightroute"];
    out.clear(); out["generated"]=now; out["source"]="adsbdb";
    auto p=out["pages"].to<JsonArray>().add<JsonObject>(); p["title"]=call;
    auto lines=p["lines"].to<JsonArray>();
    if(!route.is<JsonObjectConst>()) { lines.add("Unavailable"); return false; }
    const auto origin=route["origin"],dest=route["destination"];
    lines.add(route["airline"]["name"] | "");
    std::string from=origin["iata_code"] | (origin["icao_code"] | "?");
    std::string to=dest["iata_code"] | (dest["icao_code"] | "?");
    lines.add(from+" > "+to); lines.add(origin["name"] | ""); lines.add(dest["name"] | "");
    lines.add("Database route; verify with airline"); lines.add("No schedule or arrival estimate"); return true;
}
}
