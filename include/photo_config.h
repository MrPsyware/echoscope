#pragma once
#include <cctype>
#include <cstring>
namespace sky {
inline bool validPhotoBase(const char *url) {
    if(!*url) return true;
    if(std::strlen(url)>160 || std::strncmp(url,"http://",7)) return false;
    const char *host=url+7,*p=host;
    if(!std::isalnum(static_cast<unsigned char>(*p))) return false;
    while(*p && *p!=':') {
        if(!std::isalnum(static_cast<unsigned char>(*p)) && *p!='.' && *p!='-') return false;
        ++p;
    }
    if(!std::isalnum(static_cast<unsigned char>(p[-1]))) return false;
    if(!*p) return true;
    unsigned port=0; ++p;
    if(!*p) return false;
    while(*p) {
        if(*p<'0' || *p>'9') return false;
        port=port*10+unsigned(*p++-'0'); if(port>65535) return false;
    }
    return port!=0;
}
}
