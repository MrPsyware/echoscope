#pragma once
#include <LittleFS.h>
#include <esp_partition.h>
extern "C" {
#include <src/extra/libs/png/lodepng.h>
}
#include "standalone_map_math.h"
namespace standalone {
// A single job, advanced in small steps between live-feed polls. Raw PNG tiles
// persist across reboot; derived maps are recreated in PSRAM without flash wear.
class Map {
    struct Tile { int x,y; } tiles[16]{};
    uint32_t *lookup=nullptr;
    uint16_t *pixels=nullptr;
    unsigned row=0,tileCount=0,tileIndex=0;
    double lat=0,lon=0,radius=0;
    int zoom=0;
    bool mounted=false,tried=false,failed=false,ready=false;
    static constexpr uint64_t ttl=7*86400;
    static constexpr size_t budget=2700*1024;
    String key(int x,int y) const { return "/t"+String(zoom)+"_"+String(x)+"_"+String(y)+".png"; }
    bool storage() {
        if(tried) return mounted;
        tried=true; mounted=LittleFS.begin(false);
        if(mounted) return true;
        // Format only an erased partition, never a filesystem/data belonging to
        // another firmware. OTA leaves this partition untouched.
        const auto *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,nullptr);
        if(!part) return false;
        uint8_t block[512]; bool empty=true;
        for(size_t at=0;empty && at<part->size;at+=sizeof(block)) {
            if(esp_partition_read(part,at,block,sizeof(block))!=ESP_OK) { empty=false; break; }
            for(auto b:block) if(b!=0xff) { empty=false; break; }
            if(at%16384==0) delay(1);
        }
        if(empty && LittleFS.format()) mounted=LittleFS.begin(false);
        return mounted;
    }
    void prune(uint64_t now) {
        auto root=LittleFS.open("/");
        for(auto file=root.openNextFile();file;file=root.openNextFile()) {
            const String path=file.path(); uint64_t stamp=0;
            const bool tile=path.startsWith("/t") && path.endsWith(".png");
            const bool read=tile && file.read(reinterpret_cast<uint8_t*>(&stamp),sizeof(stamp))==sizeof(stamp);
            file.close();
            if(path=="/tile.tmp" || (tile && (!read || stamp>now || now-stamp>=ttl))) LittleFS.remove(path);
        }
    }
public:
    String message;
    void cancel() { free(lookup); free(pixels); lookup=nullptr; pixels=nullptr; ready=false; failed=false; row=tileCount=tileIndex=0; }
    ~Map() { cancel(); }
    bool start(double latitude,double longitude,double km) {
        cancel(); lat=latitude; lon=longitude; radius=km; zoom=mapZoom(lat,km); message="Preparing map...";
        if(std::abs(lat)>85 || !storage()) { message="Map cache unavailable"; failed=true; return false; }
        lookup=static_cast<uint32_t*>(heap_caps_malloc(420*420*4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        pixels=static_cast<uint16_t*>(heap_caps_calloc(420*420,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!lookup||!pixels) { cancel(); failed=true; message="Map memory unavailable"; return false; }
        return true;
    }
    bool active() const { return pixels && !ready && !failed; }
    bool complete() const { return ready; }
    const uint16_t *data() const { return pixels; }
    // download(url, destination) writes only a bounded complete PNG response.
    template<class Download> void step(Download download) {
        if(!active()) return;
        if(row<420) {
            const unsigned end=std::min(420u,row+8);
            for(;row<end;++row) for(int x=0;x<420;++x) {
                auto &entry=lookup[row*420+x]; entry=UINT32_MAX;
                if(!inMap(x,row)) continue;
                auto p=mapPixel(lat,lon,radius,zoom,x,row);
                const int tx=p.x/256,ty=p.y/256;
                unsigned i=0; while(i<tileCount && (tiles[i].x!=tx||tiles[i].y!=ty)) ++i;
                if(i==tileCount) { if(tileCount==16) { failed=true; message="Map area too large"; return; } tiles[tileCount++]={tx,ty}; }
                entry=(i<<16)|((p.y%256)*256+p.x%256);
            }
            return;
        }
        if(tileIndex==tileCount) { ready=true; message=""; free(lookup); lookup=nullptr; return; }
        const uint64_t now=time(nullptr);
        if(now<1700000000) { message="Map waiting for clock"; return; }
        const auto t=tiles[tileIndex]; const String path=key(t.x,t.y);
        auto file=LittleFS.open(path,"r"); uint64_t stamp=0;
        bool cached=file && file.read(reinterpret_cast<uint8_t*>(&stamp),sizeof(stamp))==sizeof(stamp) && stamp<=now && now-stamp<ttl && file.size()>sizeof(stamp) && file.size()<=512*1024+sizeof(stamp);
        if(!cached) {
            file.close(); prune(now);
            // Never evict fresh tiles to make space. That would cause repeated
            // downloads within OSM's minimum cache lifetime.
            if(LittleFS.usedBytes()+512*1024>budget) { failed=true; message="Map cache full / retry later"; return; }
            const String url="https://tile.openstreetmap.org/"+String(zoom)+"/"+String(t.x)+"/"+String(t.y)+".png";
            auto temp=LittleFS.open("/tile.tmp","w");
            bool ok=temp && temp.write(reinterpret_cast<const uint8_t*>(&now),sizeof(now))==sizeof(now);
            if(ok) ok=download(url,temp);
            temp.flush(); temp.close();
            if(!ok || !LittleFS.rename("/tile.tmp",path)) { LittleFS.remove("/tile.tmp"); failed=true; message="Map download unavailable"; return; }
            file=LittleFS.open(path,"r"); if(file) file.seek(sizeof(stamp));
        }
        if(!file || file.size()<=sizeof(stamp)) { failed=true; message="Map cache read error"; return; }
        const size_t size=file.size()-sizeof(stamp);
        auto *png=static_cast<uint8_t*>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        bool read=png && file.read(png,size)==size; file.close();
        unsigned width=0,height=0; LodePNGState state; lodepng_state_init(&state);
        unsigned error=read?lodepng_inspect(&width,&height,&state,png,size):1;
        lodepng_state_cleanup(&state);
        uint8_t *rgb=nullptr;
        if(!error && width==256 && height==256) error=lodepng_decode24(&rgb,&width,&height,png,size); else error=1;
        free(png);
        if(error || width!=256 || height!=256) { free(rgb); failed=true; message="Map PNG invalid"; return; }
        for(unsigned i=0;i<420*420;++i) if(lookup[i]!=UINT32_MAX && (lookup[i]>>16)==tileIndex) {
            const auto p=(lookup[i]&65535)*3; pixels[i]=mapColor(rgb[p],rgb[p+1],rgb[p+2]);
        }
        free(rgb); ++tileIndex; message="Preparing map "+String(tileIndex)+"/"+String(tileCount);
    }
};
}
