#pragma once
#include <ArduinoJson.h>
#include <cstring>
#include <string>
#include <cctype>
#include "evil_flyby.h"
namespace setupui {
// Labels and disabled rows are metadata only. Active values must exactly match
// the firmware watch fields so a stale editor cannot silently alter behaviour.
inline bool watchMetadata(const char *json,const char *types,const char *regs,const char *calls,bool wildcards) {
    if(!json || !*json) return true; // Legacy clients retain raw-field editing.
    if(std::strlen(json)>8192) return false;
    JsonDocument doc;
    if(deserializeJson(doc,json) || !doc.is<JsonArray>() || doc.size()>48) return false;
    std::string values[3];
    for(JsonObjectConst row:doc.as<JsonArrayConst>()) {
        const char *kind=row["kind"],*value=row["value"],*label=row["label"];
        if(!kind||!value||!label||!row["enabled"].is<bool>()||std::strlen(label)>80||!value[0]||std::strlen(value)>15) return false;
        const int k=!std::strcmp(kind,"type")?0:!std::strcmp(kind,"registration")?1:!std::strcmp(kind,"callsign")?2:-1;
        if(k<0) return false;
        for(const char *p=label;*p;++p) if(static_cast<unsigned char>(*p)<32) return false;
        fun::Kind egg; const char *end=fun::afterMarker(value,&egg);
        const bool visitor=k==2 && end!=value && !*end;
        if(!visitor) for(const char *p=value;*p;++p) {
            const bool star=wildcards && *p=='*' && p!=value && !p[1];
            if(!std::isalnum(static_cast<unsigned char>(*p)) && *p!='-' && !star) return false;
        }
        if(row["enabled"].as<bool>()) { if(!values[k].empty()) values[k]+=", "; values[k]+=value; }
    }
    return values[0]==types && values[1]==regs && values[2]==calls;
}
}
