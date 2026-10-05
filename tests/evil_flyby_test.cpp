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
    manual.draw(10000,[&](float,float,float,float,unsigned){++manualStrokes;}); assert(manualStrokes>50);
    manual.tick(20000,true,1); assert(!manual.active);
    manual.tick(20000000,true,1); assert(!manual.active && !manual.armed);
    manual.pending=true; manual.tick(20000001,false,1); assert(!manual.active && !manual.pending);
    manual.pending=true; manual.tick(20000002,true,1); assert(manual.active);
    manual.tick(20000003,false,1); assert(!manual.active);
    fun::Flyby f; f.tick(0,true,1); assert(!f.active && f.visible);
    f.automaticEnabled=true; f.tick(10,true,1); assert(!f.active && f.visible);
    auto due=f.next; assert(due>=3600010 && due<=10800010);
    f.tick(due,true,15); assert(f.active);
    unsigned strokes=0;
    f.draw(due+10000,[&](float x,float y,float xx,float yy,unsigned level){
        assert(x*x+y*y<1.00001f && xx*xx+yy*yy<1.00001f); assert(level<=100); ++strokes;
    }); assert(strokes>50);
    f.tick(due+20000,true,15); assert(!f.active);
    f.pending=true; f.tick(due+20001,true,2); assert(f.active);
    f.tick(due+20002,false,2); assert(!f.active && !f.visible);
    f.pending=true; f.tick(due+20003,false,2); assert(!f.active && !f.pending);
    f.automaticEnabled=false; f.tick(due+20004,true,2); assert(!f.active && !f.armed);
    f.automaticEnabled=true; f.tick(due+20005,true,2); f.tick(f.next,true,2); assert(f.active);
    f.automaticEnabled=false; f.tick(f.started+1,true,2); assert(!f.active); // removing marker cancels automatic only
    fun::Flyby wrap; wrap.automaticEnabled=true; wrap.tick(UINT32_MAX-1000,true,5);
    wrap.tick(wrap.next-1,true,5); assert(!wrap.active); wrap.tick(wrap.next,true,5); assert(wrap.active);
    std::cout<<"Flyby opt-in, reserved marker, scheduling, clipping and sleep checks passed\n";
}
