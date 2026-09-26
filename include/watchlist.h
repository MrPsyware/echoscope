#pragma once
#include <array>
#include <cctype>
#include <cstring>
namespace sky {
struct WatchList {
    std::array<std::array<char,16>,16> entries{};
    unsigned count=0;
    bool set(const char *text) {
        WatchList next;
        while(*text) {
            while(*text==',' || std::isspace(static_cast<unsigned char>(*text))) ++text;
            if(!*text) break;
            if(next.count==next.entries.size()) return false;
            unsigned n=0;
            while(*text && *text!=',' && !std::isspace(static_cast<unsigned char>(*text))) {
                const unsigned char c=*text++;
                if(n>=15 || !(std::isalnum(c) || c=='-' || c=='*')) return false;
                next.entries[next.count][n++]=std::toupper(c);
            }
            const char *star=std::strchr(next.entries[next.count].data(),'*');
            if(star && (star==next.entries[next.count].data() || star[1])) return false;
            ++next.count;
        }
        *this=next; return true;
    }
    bool matches(const char *value,bool type=false) const {
        char normal[16]{}; size_t n=std::strlen(value);
        if(n>=sizeof(normal)) return false;
        for(size_t i=0;i<n;++i) normal[i]=std::toupper(static_cast<unsigned char>(value[i]));
        for(unsigned i=0;i<count;++i) {
            const char *pattern=entries[i].data(); const size_t length=std::strlen(pattern);
            if(type && !std::strcmp(pattern,"A380") && !std::strcmp(normal,"A388")) return true;
            if(length && pattern[length-1]=='*') { if(!std::strncmp(normal,pattern,length-1)) return true; }
            else if(!std::strcmp(normal,pattern)) return true;
        }
        return false;
    }
};
struct Watches {
    WatchList types,registrations,callsigns;
    bool military=false,rotorcraft=false;
};
}
