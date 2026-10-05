#pragma once
#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include "photo_protocol.h"

namespace standalone {
constexpr size_t photoDownloadLimit=128*1024;
inline bool photoRegistration(const char *s) {
    if(!s || !*s || std::strlen(s)>15 || !std::isalnum(static_cast<unsigned char>(*s))) return false;
    for(;*s;++s) if(!std::isalnum(static_cast<unsigned char>(*s)) && *s!='-') return false;
    return true;
}
inline bool photoURL(const char *s,bool image) {
    if(!s || std::strlen(s)>383) return false;
    const char *prefix=image?"https://t.plnspttrs.net/":"https://www.planespotters.net/photo/";
    if(std::strncmp(s,prefix,std::strlen(prefix))) {
        if(!image) return false;
        prefix="https://cdn.planespotters.net/";
        if(std::strncmp(s,prefix,std::strlen(prefix))) return false;
    }
    if(!s[std::strlen(prefix)]) return false;
    for(const auto *p=s;*p;++p) if(static_cast<unsigned char>(*p)<=32 || static_cast<unsigned char>(*p)>=127 || *p=='\\' || *p=='#') return false;
    return true;
}
struct PhotoMetadata { char url[384]{},credit[128]{},link[256]{}; };
inline bool photoMetadata(JsonVariantConst item,PhotoMetadata &out) {
    const char *url=item["thumbnail"]["src"],*credit=item["photographer"],*link=item["link"];
    if(!photoURL(url,true) || !photoURL(link,false) || !credit || !*credit || std::strlen(credit)>=sizeof(out.credit) || std::strlen(link)>=sizeof(out.link)) return false;
    bool printable=false;
    for(const auto *p=credit;*p;++p) {
        if(static_cast<unsigned char>(*p)<32 || *p==127) return false;
        printable|=*p!=' ';
    }
    if(!printable) return false;
    std::snprintf(out.url,sizeof(out.url),"%s",url);
    std::snprintf(out.credit,sizeof(out.credit),"%s",credit);
    std::snprintf(out.link,sizeof(out.link),"%s",link);
    return true;
}

// RGB565 big-endian ECP1 packet, identical to the info server's wire format.
// JPEG decoding uses PSRAM on the device and ordinary malloc in host tests.
bool decodePhoto(const uint8_t *jpeg,size_t size,const PhotoMetadata &meta,
                 uint8_t *packet,size_t capacity,size_t &written);

enum class PhotoState { Lookup, Image, Ready, Missing, Failed };
struct PhotoEntry {
    char reg[16]{};
    PhotoMetadata meta;
    PhotoState state=PhotoState::Lookup;
    uint32_t until=0,touched=0;
    size_t size=0;
    std::unique_ptr<uint8_t,decltype(&std::free)> packet{nullptr,&std::free};
    void finish(PhotoState result,uint32_t now) {
        state=result;
        until=now+(result==PhotoState::Ready?3600000u:result==PhotoState::Missing?900000u:60000u);
        if(result!=PhotoState::Ready) { packet.reset(); size=0; }
    }
};
class PhotoCache {
    PhotoEntry entries[8];
public:
    void clear() { for(auto &e:entries) e=PhotoEntry{}; }
    PhotoEntry *select(const char *reg,uint32_t now) {
        if(!photoRegistration(reg)) return nullptr;
        PhotoEntry *chosen=nullptr;
        for(auto &e:entries) if(!std::strcmp(e.reg,reg)) { chosen=&e; break; }
        if(!chosen) {
            for(auto &e:entries) if(!e.reg[0]) { chosen=&e; break; }
            if(!chosen) {
                chosen=&entries[0];
                for(auto &e:entries) if(uint32_t(now-e.touched)>uint32_t(now-chosen->touched)) chosen=&e;
            }
            *chosen=PhotoEntry{};
        } else if(chosen->state!=PhotoState::Lookup && chosen->state!=PhotoState::Image && int32_t(now-chosen->until)>=0) *chosen=PhotoEntry{};
        std::snprintf(chosen->reg,sizeof(chosen->reg),"%s",reg); chosen->touched=now;
        return chosen;
    }
};
}
