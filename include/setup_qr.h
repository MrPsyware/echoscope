#pragma once
#include <cstring>
#include <cstdio>
#include "vendor/qrcodegen.h"
namespace setupui {
class QR {
    uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(6)]{},temp[qrcodegen_BUFFER_LEN_FOR_VERSION(6)]{};
    char previous[192]{}; bool valid=false;
public:
    bool prepare(const char *value) {
        if(!std::strcmp(previous,value)) return valid;
        if(std::strlen(value)>=sizeof(previous)) { valid=false; return false; }
        std::snprintf(previous,sizeof(previous),"%s",value);
        valid=qrcodegen_encodeText(value,temp,code,qrcodegen_Ecc_MEDIUM,1,6,qrcodegen_Mask_AUTO,true);
        return valid;
    }
    template<class Rect> void draw(int cx,int top,int maximum,Rect rect) const {
        if(!valid) return;
        const int n=qrcodegen_getSize(code),scale=maximum/(n+8),size=(n+8)*scale,left=cx-size/2;
        if(!scale) return;
        rect(left,top,size,size,true);
        for(int y=0;y<n;++y) for(int x=0;x<n;) {
            if(!qrcodegen_getModule(code,x,y)) { ++x; continue; }
            int start=x++; while(x<n && qrcodegen_getModule(code,x,y)) ++x;
            rect(left+(start+4)*scale,top+(y+4)*scale,(x-start)*scale,scale,false);
        }
    }
};
}
