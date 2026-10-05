#include "standalone_photo.h"
#ifdef ARDUINO
#include <esp_heap_caps.h>
#define STBI_MALLOC(n) heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)
#define STBI_REALLOC(p,n) heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)
#define STBI_FREE(p) free(p)
#endif
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 512
#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "vendor/stb_image.h"
#pragma GCC diagnostic pop

namespace standalone {
bool decodePhoto(const uint8_t *jpeg,size_t size,const PhotoMetadata &meta,uint8_t *packet,size_t capacity,size_t &written) {
    written=0;
    if(!jpeg || size<4 || size>photoDownloadLimit || !packet || capacity<sky::photoMaxBytes || !meta.credit[0] || !photoURL(meta.link,false)) return false;
    // Reject incomplete/HTML bodies even where a decoder might accept a partial JPEG.
    if(jpeg[0]!=0xff || jpeg[1]!=0xd8 || jpeg[size-2]!=0xff || jpeg[size-1]!=0xd9) return false;
    int w=0,h=0,channels=0;
    if(!stbi_info_from_memory(jpeg,int(size),&w,&h,&channels) || w<1 || h<1 || w>512 || h>512) return false;
    auto *rgb=stbi_load_from_memory(jpeg,int(size),&w,&h,&channels,3);
    if(!rgb) return false;
    unsigned width=unsigned(w),height=unsigned(h);
    if(width>200) { height=std::max(1u,height*200/width); width=200; }
    if(height>150) { width=std::max(1u,width*150/height); height=150; }
    std::memset(packet,0,sky::photoHeaderSize); std::memcpy(packet,"ECP1",4);
    packet[4]=uint8_t(width); packet[6]=uint8_t(height);
    std::memcpy(packet+8,meta.credit,sizeof(meta.credit)); std::memcpy(packet+136,meta.link,sizeof(meta.link));
    // Area averaging for larger/portrait thumbnails; ordinary 200x135 images
    // are converted pixel-for-pixel without enlargement or cropping.
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        const unsigned x0=x*w/width,x1=(x+1)*w/width,y0=y*h/height,y1=(y+1)*h/height;
        unsigned r=0,g=0,b=0,n=0;
        for(unsigned sy=y0;sy<y1;++sy) for(unsigned sx=x0;sx<x1;++sx) {
            const auto *p=rgb+(sy*w+sx)*3; r+=p[0]; g+=p[1]; b+=p[2]; ++n;
        }
        const uint16_t color=((r/n>>3)<<11)|((g/n>>2)<<5)|(b/n>>3);
        const size_t at=sky::photoHeaderSize+(y*width+x)*2;
        packet[at]=uint8_t(color>>8); packet[at+1]=uint8_t(color);
    }
    stbi_image_free(rgb);
    written=sky::photoHeaderSize+width*height*2;
    return true;
}
}
