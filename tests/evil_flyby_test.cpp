#include "evil_flyby.h"
#include "watchlist.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    for(auto s:{"dr evil","DR EVIL","EZY123, dr evil, BAW*","ezy123 Dr   Evil baw123"}) assert(fun::enabledBy(s));
    for(auto s:{"","dr","d","evil","dr,evil","dr evilish","xdr evil","drevil"}) assert(!fun::enabledBy(s));
    sky::WatchList calls; assert(calls.set("EZY123, dr evil, BAW*",true)); assert(calls.count==2);
    assert(calls.matches("EZY123") && calls.matches("BAW123")); assert(!calls.matches("DR") && !calls.matches("EVIL"));
    fun::Flyby manual;
    manual.pending=true; manual.tick(0,true,1);
    assert(manual.active && manual.visible && !manual.armed && !manual.pending);
    unsigned manualStrokes=0;
    manual.draw(10000,[&](float,float,float,float,uint32_t,unsigned){++manualStrokes;}); assert(manualStrokes>50);
    manual.tick(20000,true,1); assert(!manual.active);
    manual.tick(20000000,true,1); assert(!manual.active && !manual.armed);
    manual.pending=true; manual.tick(20000001,false,1); assert(!manual.active && !manual.pending);
    manual.pending=true; manual.tick(20000002,true,1); assert(manual.active);
    manual.tick(20000003,false,1); assert(!manual.active);
    fun::Flyby f; f.tick(0,true,1); assert(!f.active && f.visible);
    f.automaticMask=true; f.tick(10,true,1); assert(!f.active && f.visible);
    auto due=f.next; assert(due>=3600010 && due<=10800010);
    f.tick(due,true,15); assert(f.active);
    unsigned strokes=0;
    f.draw(due+10000,[&](float x,float y,float xx,float yy,uint32_t,unsigned level){
        assert(x*x+y*y<1.00001f && xx*xx+yy*yy<1.00001f); assert(level<=100); ++strokes;
    }); assert(strokes>50);
    f.tick(due+20000,true,15); assert(!f.active);
    f.pending=true; f.tick(due+20001,true,2); assert(f.active);
    f.tick(due+20002,false,2); assert(!f.active && !f.visible);
    f.pending=true; f.tick(due+20003,false,2); assert(!f.active && !f.pending);
    f.automaticMask=false; f.tick(due+20004,true,2); assert(!f.active && !f.armed);
    f.automaticMask=true; f.tick(due+20005,true,2); f.tick(f.next,true,2); assert(f.active);
    f.automaticMask=false; f.tick(f.started+1,true,2); assert(!f.active); // removing marker cancels automatic only
    fun::Flyby wrap; wrap.automaticMask=true; wrap.tick(UINT32_MAX-1000,true,5);
    wrap.tick(wrap.next-1,true,5); assert(!wrap.active); wrap.tick(wrap.next,true,5); assert(wrap.active);
    assert(fun::enabledBy("dr evil, NYAN CAT, santa, ufo")==15);
    assert(fun::enabledBy("nyan,cat, santa123, ufox")==0);
    assert(fun::enabledBy("nyan cat, nyan cat")==fun::kindMask(fun::Kind::NyanCat));
    assert(fun::fromHeader("dr-evil")==fun::Kind::DrEvil);
    assert(fun::fromHeader("nyan-cat")==fun::Kind::NyanCat);
    assert(fun::fromHeader("santa")==fun::Kind::Santa);
    assert(fun::fromHeader("ufo")==fun::Kind::UFO);
    assert(fun::fromHeader("garbage")==fun::Kind::None);
    assert(calls.set("nyan cat, santa, ufo, dr evil, EZY123",true));
    assert(calls.count==1 && calls.matches("EZY123") && !calls.matches("SANTA"));
    unsigned picked=0;
    for(unsigned r=0;r<12;++r) picked|=fun::kindMask(fun::choose(15,r));
    assert(picked==15);
    assert(fun::choose(0,12)==fun::Kind::None);
    assert(fun::choose(4,999)==fun::Kind::Santa);
    for(unsigned effect=1;effect<=4;++effect) {
        fun::Flyby test; test.pending=effect; test.tick(100,true,123);
        assert(test.active && test.manual && test.kind==fun::Kind(effect) && !test.armed);
        for(int direction:{-1,1}) for(float lane:{-.3f,.3f}) {
            test.direction=direction; test.lane=lane; unsigned count=0,colors=0;
            for(unsigned age=0;age<20000;age+=500) test.draw(100+age,[&](float x,float y,float xx,float yy,uint32_t color,unsigned level){
                assert(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(xx)&&std::isfinite(yy));
                assert(x*x+y*y<1.00002f && xx*xx+yy*yy<1.00002f);
                assert(level<=100 && color<=0xFFFFFF); if(color!=0x68F3AE) ++colors; ++count;
            });
            assert(count>50); if(effect>1) assert(colors>0);
        }
        test.tick(20100,true,123); assert(!test.active);
        test.tick(50000000,true,123); assert(!test.active); // manual never enables random appearances
        test.automaticMask=fun::kindMask(fun::Kind(effect)); test.tick(50000001,true,123);
        test.tick(test.next,true,123); assert(test.active && test.kind==fun::Kind(effect) && !test.manual);
        test.automaticMask=0; test.tick(test.started+1,true,123); assert(!test.active);
    }
    fun::Flyby invalid; invalid.pending=255; invalid.tick(1,true,1); assert(!invalid.active);
    std::cout<<"All four flybys: markers, HTTP names, scheduling, clipping, colour and sleep checks passed\n";
}
