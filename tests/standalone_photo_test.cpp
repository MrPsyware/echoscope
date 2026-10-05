#include "standalone_photo.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
std::vector<uint8_t> read(const char *path) {
    std::ifstream f(path,std::ios::binary); assert(f);
    return {std::istreambuf_iterator<char>(f),{}};
}
int main(int argc,char **argv) {
    using namespace standalone;
    for(const auto *s:{"G-UZHO","N123AB","C-GFMQ"}) assert(photoRegistration(s));
    for(const auto *s:{"","-ABC","G/ABC","G ABC","ABC?x","1234567890123456"}) assert(!photoRegistration(s));
    for(const auto *s:{"https://t.plnspttrs.net/123/test.jpg","https://cdn.planespotters.net/x.jpg"}) assert(photoURL(s,true));
    for(const auto *s:{"http://t.plnspttrs.net/x","https://t.plnspttrs.net.evil/x","https://t.plnspttrs.net@evil/x","https://t.plnspttrs.net:444/x","https://t.plnspttrs.net/","https://t.plnspttrs.net/x\n","https://t.plnspttrs.net/x#foo"}) assert(!photoURL(s,true));
    JsonDocument doc;
    doc["thumbnail"]["src"]="https://t.plnspttrs.net/123/test.jpg";
    doc["photographer"]="Test photographer"; doc["link"]="https://www.planespotters.net/photo/123/test";
    PhotoMetadata meta; assert(photoMetadata(doc.as<JsonVariantConst>(),meta));
    doc["photographer"]="  "; assert(!photoMetadata(doc.as<JsonVariantConst>(),meta));
    doc["photographer"]="Test\nname"; assert(!photoMetadata(doc.as<JsonVariantConst>(),meta));
    doc["photographer"]=std::string(128,'x'); assert(!photoMetadata(doc.as<JsonVariantConst>(),meta));
    doc["photographer"]="Test"; doc["thumbnail"]["src"]="https://localhost/x"; assert(!photoMetadata(doc.as<JsonVariantConst>(),meta));
    PhotoCache cache;
    auto *e=cache.select("G-UZHO",100); assert(e && e->state==PhotoState::Lookup);
    e->finish(PhotoState::Missing,100);
    assert(cache.select("G-UZHO",899999)->state==PhotoState::Missing);
    assert(cache.select("G-UZHO",900100)->state==PhotoState::Lookup);
    e->finish(PhotoState::Failed,UINT32_MAX-1000);
    assert(cache.select("G-UZHO",1000)->state==PhotoState::Failed);
    assert(cache.select("G-UZHO",60000)->state==PhotoState::Lookup);
    cache.clear();
    for(int n=0;n<8;++n) { char reg[16]; snprintf(reg,sizeof(reg),"N%d",n); cache.select(reg,100+n)->finish(PhotoState::Missing,100+n); }
    cache.select("N0",200); // Keep the oldest inserted entry recently used.
    cache.select("N8",201);
    assert(cache.select("N0",202)->state==PhotoState::Missing);
    assert(cache.select("N1",203)->state==PhotoState::Lookup); // LRU evicted N1.
    assert(!cache.select("../x",1));
    std::vector<uint8_t> packet(sky::photoMaxBytes); size_t written=0;
    for(const char *path:{"tests/fixtures/photos/baseline.jpg","tests/fixtures/photos/progressive.jpg"}) {
        auto jpg=read(path); assert(decodePhoto(jpg.data(),jpg.size(),meta,packet.data(),packet.size(),written));
        unsigned w=0,h=0; assert(sky::photoPacket(reinterpret_cast<const char*>(packet.data()),written,w,h) && w==32 && h==24);
        auto pixel=[&](unsigned x) { const auto at=sky::photoHeaderSize+(12*w+x)*2; return unsigned(packet[at])<<8|packet[at+1]; };
        assert((pixel(4)>>11)>25 && (pixel(4)&31)<4); // Red, big-endian RGB565.
        assert((pixel(28)>>11)<4 && (pixel(28)&31)>25); // Blue.
        assert(!strcmp(reinterpret_cast<char*>(packet.data()+8),meta.credit));
        assert(!strcmp(reinterpret_cast<char*>(packet.data()+136),meta.link));
        assert(!decodePhoto(jpg.data(),jpg.size()-2,meta,packet.data(),packet.size(),written));
        assert(!decodePhoto(jpg.data(),jpg.size(),meta,packet.data(),16,written));
        auto broken=jpg; std::fill(broken.begin()+2,broken.end()-2,0);
        assert(!decodePhoto(broken.data(),broken.size(),meta,packet.data(),packet.size(),written));
    }
    auto portrait=read("tests/fixtures/photos/portrait.jpg");
    assert(decodePhoto(portrait.data(),portrait.size(),meta,packet.data(),packet.size(),written));
    unsigned w=0,h=0; assert(sky::photoPacket(reinterpret_cast<const char*>(packet.data()),written,w,h) && w==100 && h==150);
    auto huge=read("tests/fixtures/photos/oversize.jpg");
    assert(!decodePhoto(huge.data(),huge.size(),meta,packet.data(),packet.size(),written));
    const uint8_t html[]="<html>failure</html>"; assert(!decodePhoto(html,sizeof(html),meta,packet.data(),packet.size(),written));
    if(argc>1) {
        auto real=read(argv[1]); assert(decodePhoto(real.data(),real.size(),meta,packet.data(),packet.size(),written));
        assert(sky::photoPacket(reinterpret_cast<const char*>(packet.data()),written,w,h));
        std::cout<<"External sample decoded: "<<w<<"x"<<h<<"\n";
        if(argc>2) { std::ofstream f(argv[2],std::ios::binary); f.write(reinterpret_cast<char*>(packet.data()),written); }
    }
    std::cout<<"Standalone photos passed: provider metadata, bounded baseline/progressive JPEG decoding, RGB565, resizing, corruption, cache expiry and eviction.\n";
}
