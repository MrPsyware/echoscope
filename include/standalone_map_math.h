#pragma once
#include "radar_model.h"
namespace standalone {
struct TilePixel { int x,y; };
inline int mapZoom(double latitude,double radius) {
    return std::max(0,std::min(16,int(std::round(std::log2(40075.0167*std::max(0.001,std::cos(latitude*sky::pi/180))*210/(256*radius))))));
}
// Inverse of sky::project (azimuthal equidistant), then Web Mercator.
inline TilePixel mapPixel(double latitude,double longitude,double radius,int zoom,int x,int y) {
    const double east=(x-210.0)*radius/210,north=(210.0-y)*radius/210;
    const double rho=std::hypot(east,north),c=rho/6371.0088,h=latitude*sky::pi/180;
    const double lat=rho?std::asin(std::cos(c)*std::sin(h)+north*std::sin(c)*std::cos(h)/rho):h;
    const double lon=longitude*sky::pi/180+(rho?std::atan2(east*std::sin(c),rho*std::cos(h)*std::cos(c)-north*std::sin(h)*std::sin(c)):0);
    const double bounded=std::max(-85.05112878,std::min(85.05112878,lat*180/sky::pi))*sky::pi/180;
    const int world=256*(1<<zoom);
    int px=int(std::floor((lon/(2*sky::pi)+.5)*world)); px=(px%world+world)%world;
    const int py=std::max(0,std::min(world-1,int(std::floor((1-std::asinh(std::tan(bounded))/sky::pi)*.5*world))));
    return {px,py};
}
inline bool inMap(int x,int y) { return std::hypot(x-210.0,y-210.0)<=210; }
// Same background and palette as info-service/extras.py::make_map.
inline constexpr uint16_t mapBackground=uint16_t(((3>>3)<<11)|((13>>2)<<5)|(16>>3));
inline uint16_t mapColor(unsigned r,unsigned g,unsigned b) {
    const double luminance=r*0.299+g*0.587+b*0.114;
    const double ink=(255-luminance)/255;
    r=unsigned(3+ink*27); g=unsigned(13+ink*67); b=unsigned(16+ink*52);
    return uint16_t(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}
}
