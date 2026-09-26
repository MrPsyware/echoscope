#include "photo_config.h"
#include "photo_protocol.h"
#include "info_protocol.h"
#include <cassert>
#include <vector>
#include <iostream>
int main() {
    std::string map(sky::mapBytes,'\0'); memcpy(map.data(),"ECM1\xa4\x01\xa4\x01",8);
    assert(sky::validMap(map.data(),map.size())); assert(!sky::validMap(map.data(),map.size()-1));
    map[4]=0; assert(!sky::validMap(map.data(),map.size()));

    for(auto url:{"","http://192.168.1.2:8086","http://photos.local","http://server:65535"}) assert(sky::validPhotoBase(url));
    for(auto url:{"https://server","http://","http://host:0","http://host:65536","http://host:abc","http://user@host","http://host/path","http://host?x","http://host:80:81","http://-host"}) assert(!sky::validPhotoBase(url));
    std::vector<char> bytes(sky::photoHeaderSize+200*135*2,0);
    memcpy(bytes.data(),"ECP1",4); bytes[4]=char(200); bytes[6]=char(135);
    strcpy(bytes.data()+8,"Photographer"); strcpy(bytes.data()+136,"https://www.planespotters.net/photo/1");
    unsigned w=0,h=0;
    assert(sky::photoPacket(bytes.data(),bytes.size(),w,h) && w==200 && h==135);
    assert(!sky::photoPacket(bytes.data(),bytes.size()-1,w,h));
    assert(!sky::photoPacket(bytes.data(),100,w,h));
    bytes[4]=char(201); assert(!sky::photoPacket(bytes.data(),bytes.size(),w,h)); bytes[4]=char(200);
    memset(bytes.data()+8,'x',128); assert(!sky::photoPacket(bytes.data(),bytes.size(),w,h));
    strcpy(bytes.data()+8,"Name"); strcpy(bytes.data()+136,"http://malicious.invalid/");
    assert(!sky::photoPacket(bytes.data(),bytes.size(),w,h));
    strcpy(bytes.data()+136,"https://www.planespotters.net.evil/photo/1");
    assert(!sky::photoPacket(bytes.data(),bytes.size(),w,h));
    std::cout<<"Photo checks passed: URLs, dimensions, byte counts, credits and source links.\n";
}
