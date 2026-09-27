#pragma once
#include <cstdlib>
namespace sky {
struct DetailGesture {
    int x=0,y=0,lastX=0,lastY=0;
    bool active=false,moved=false;
    void begin(int px,int py) { x=lastX=px; y=lastY=py; active=true; moved=false; }
    void move(int px,int py) { if(!active) return; lastX=px; lastY=py; if(std::abs(px-x)>18 || std::abs(py-y)>18) moved=true; }
    int direction() const { const int dx=lastX-x,dy=lastY-y; return active && std::abs(dx)>=54 && std::abs(dx)*10>std::abs(dy)*13?(dx<0?1:-1):0; }
    void end() { active=false; }
};
}
