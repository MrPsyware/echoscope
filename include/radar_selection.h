#pragma once
#include <cstring>
namespace sky {
struct RadarChoice { const char *key; unsigned index; bool airport; };
inline int nextRadarChoice(const RadarChoice *choices,int count,const char *key,bool airport,int delta){
    if(!count)return -1;
    int current=-1;
    for(int i=0;i<count;++i)if(choices[i].airport==airport && !std::strcmp(choices[i].key,key)){current=i;break;}
    if(current<0)return delta<0?count-1:0;
    return (current+delta%count+count)%count;
}
}
