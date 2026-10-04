#pragma once
#include <cmath>
namespace sky {
// Bounds recorded only for icons/labels actually drawn in the current radar frame.
struct AirportHit {
    int index=-1,x=0,y=0,labelX=0,labelY=0,labelW=0,labelH=0;
    bool contains(int px,int py)const {
        return std::hypot(float(px-x),float(py-y))<=18 ||
            (labelW>0 && px>=labelX-5 && px<=labelX+labelW+5 && py>=labelY-5 && py<=labelY+labelH+5);
    }
};
}
