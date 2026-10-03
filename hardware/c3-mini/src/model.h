#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
namespace mini {
constexpr unsigned capacity=32;
constexpr float ranges[]={5,10,25,50};
struct Aircraft {
    char hex[9]{},call[13]{},reg[17]{},type[9]{};
    float east=0,north=0,km=0,alt=NAN,speed=NAN,heading=NAN,seen=0;
};
struct Snapshot {Aircraft aircraft[capacity]{};unsigned count=0,total=0;uint32_t received=0;int range=2;};
inline void keep(Snapshot &s,const Aircraft &a){
    ++s.total;
    unsigned pos=0;while(pos<s.count&&s.aircraft[pos].km<=a.km)++pos;
    if(pos>=capacity)return;
    unsigned end=std::min(s.count,capacity-1);
    for(unsigned i=end;i>pos;--i)s.aircraft[i]=s.aircraft[i-1];
    s.aircraft[pos]=a;if(s.count<capacity)++s.count;
}
inline bool project(double lat,double lon,double homeLat,double homeLon,float &east,float &north){
    if(!std::isfinite(lat)||!std::isfinite(lon)||std::abs(lat)>90||std::abs(lon)>180)return false;
    double dl=std::remainder(lon-homeLon,360.0);
    east=dl*111.195*std::cos(homeLat*3.141592653589793/180);north=(lat-homeLat)*111.195;return true;
}
inline unsigned wrap(int value,unsigned size){return size?unsigned((value%int(size)+int(size))%int(size)):0;}
}
