#include "radar_model.h"
#include "input_gate.h"
#include "activity.h"
#include "button_debounce.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace sky;
    auto n=project(51.6,0,51.5,0); assert(n.north>11 && n.north<11.2 && std::abs(n.east)<0.01);
    auto e=project(51.5,0.1,51.5,0); assert(e.east>6.9 && e.east<7 && std::abs(e.north)<0.01);
    auto wrap=project(0,-179.9,0,179.9); assert(wrap.east>22 && wrap.east<22.3);
    Model m; std::array<Trail,maxAircraft> trails; m.trails=trails.data();
    m.rotate(-100,0); assert(m.range()==5); m.rotate(100,0); assert(m.range()==100);
    Snapshot s; s.count=3;
    for(size_t i=0;i<s.count;++i) { snprintf(s.aircraft[i].hex,12,"abc00%u",unsigned(i)); s.aircraft[i].position={float(i+1),0}; }
    s.aircraft[2].position={30,0}; m.rangeIndex=2;
    m.ingest(s,0); assert(!strcmp(m.selected,"abc000"));
    m.press(0); assert(m.selectMode && !m.details);
    m.rotate(1,0); assert(!strcmp(m.selected,"abc001") && m.range()==25);
    m.rotate(1,0); assert(!strcmp(m.selected,"abc000")); // skip out-of-range
    m.rotate(-1,0); assert(!strcmp(m.selected,"abc001"));
    m.openSelected(0); assert(m.details);
    m.press(0); assert(!m.details && m.selectMode); // back preserves mode
    std::swap(s.aircraft[0],s.aircraft[1]); s.aircraft[0].position={3,0};
    m.ingest(s,5000); assert(!strcmp(m.selected,"abc001"));
    assert(m.trailFor("abc001")->count==2 && m.trailFor("abc001")->points[0].east==2);
    m.openSelected(5000); s.count=0; m.ingest(s,10000); assert(m.selection()==nullptr && m.details);
    assert(!m.trailFor("abc001")); m.press(10000); assert(!m.details);
    m.press(10000); assert(!m.selectMode); m.openSelected(10000); assert(!m.details);
    s.count=1; strcpy(s.aircraft[0].hex,"test"); s.aircraft[0].position={20,0}; s.aircraft[0].received=11000;
    m.ingest(s,11000); assert(m.trailFor("test"));
    m.rotate(-1,11000); assert(m.range()==10 && !m.trailFor("test") && !m.selected[0]);
    m.rotate(1,11000); assert(m.trailFor("test")->count==1); // fresh history on re-entry
    m.refresh(72000); assert(!m.trailFor("test") && !m.selected[0]);
    assert(m.hit(20,0,0.5,72000)==-1);
    s.aircraft[0].received=UINT32_MAX-999; assert(age(s.aircraft[0],1000)==2);
    Trail t; for(int i=0;i<1000;++i) t.append({float(i),float(i%2)});
    assert(t.count<=trailLength && t.points[0].east==0 && t.points[t.count-1].east==999);
    Trail straight; for(int i=0;i<100;++i) straight.append({i*0.01f,0});
    assert(straight.count==2 && straight.points[0].east==0);
    Point a={-20,0},b={20,0}; assert(clipToCircle(a,b,10) && a.east==-10 && b.east==10);
    a={20,20}; b={30,30}; assert(!clipToCircle(a,b,10));
    m.reset(); assert(m.trails==trails.data() && !m.selectMode && !m.data.count);
    assert(!setupExpired(1000,1001)); // stale loop timestamp before portal opened
    assert(!setupExpired(1001,1001));
    assert(!setupExpired(301000,1001));
    assert(setupExpired(301001,1001));
    assert(!setupExpired(50,UINT32_MAX-100));
    assert(setupExpired(300000,UINT32_MAX-100));
    Activity idle;
    assert(!idle.tick(9999) && idle.filterVisible);
    assert(!idle.tick(10000) && !idle.filterVisible);
    assert(!idle.tapFilter(10001) && idle.filterVisible);
    assert(idle.tapFilter(10002));
    idle.interact(15000,true); idle.tick(24999); assert(idle.filterVisible);
    idle.tick(25000); assert(!idle.filterVisible);
    assert(!idle.tick(3614999)); assert(idle.tick(3615000) && idle.sleeping);
    assert(!idle.tick(3615001)); assert(idle.interact(3615100) && !idle.sleeping);
    assert(!idle.interact(3615200));
    idle.lastActivity=UINT32_MAX-100; assert(!idle.tick(100));
    idle.interact(0); assert(!idle.tick(Activity::sleepTimeout,true));
    Model filtered; filtered.data.count=2;
    strcpy(filtered.data.aircraft[0].hex,"civil"); filtered.data.aircraft[0].position={1,0};
    strcpy(filtered.data.aircraft[1].hex,"mil"); filtered.data.aircraft[1].position={2,0};
    filtered.data.aircraft[1].military=true;
    filtered.cycleFilter(0); assert(filtered.filter==Filter::Military);
    assert(!strcmp(filtered.selected,"mil")); assert(filtered.hit(1,0,0.2f,0)==-1);
    filtered.selectMode=true; filtered.rotate(1,0); assert(!strcmp(filtered.selected,"mil"));
    filtered.cycleFilter(0); assert(!filtered.selected[0]);
    filtered.cycleFilter(0); assert(!strcmp(filtered.selected,"civil"));
    // Capture a complete short press while the UI is blocked for a 140 ms render.
    ButtonDebounce button;
    assert(button.sample(true,100)==ButtonDebounce::None);
    assert(button.sample(false,105)==ButtonDebounce::None); // contact bounce
    assert(button.sample(true,110)==ButtonDebounce::None);
    assert(button.sample(true,140)==ButtonDebounce::Down);
    assert(button.sample(false,180)==ButtonDebounce::None);
    assert(button.sample(false,210)==ButtonDebounce::Up);
    InputGate delayed;
    delayed.buttonBegin(140); delayed.buttonEnd(210,button.longSent);
    assert(delayed.takeClick(390) && !delayed.takeClick(400));
    assert(button.sample(true,1000)==ButtonDebounce::None);
    assert(button.sample(true,1030)==ButtonDebounce::Down);
    assert(button.sample(true,2530)==ButtonDebounce::None); // old threshold no longer opens setup
    assert(button.sample(true,6029)==ButtonDebounce::None);
    assert(button.sample(true,6030)==ButtonDebounce::Hold);
    assert(button.sample(true,6100)==ButtonDebounce::None);
    assert(button.sample(false,6200)==ButtonDebounce::None);
    assert(button.sample(false,6230)==ButtonDebounce::Up && button.longSent);
    ButtonDebounce rollover;
    assert(rollover.sample(true,UINT32_MAX-10)==ButtonDebounce::None);
    assert(rollover.sample(true,20)==ButtonDebounce::Down);
    InputGate touchFirst;
    touchFirst.touchBegin(220); touchFirst.touchEnd(230);
    touchFirst.buttonBegin(140); touchFirst.buttonEnd(210,false);
    assert(!touchFirst.takeClick(500)); // touch callbacks before delayed button queue
    InputGate gate; gate.buttonBegin(1000); gate.buttonEnd(1100,false);
    assert(!gate.takeClick(1200) && gate.takeClick(1280) && !gate.takeClick(1300));
    gate.touchBegin(2000); gate.buttonBegin(2010); gate.buttonEnd(2100,false); gate.touchEnd(2110);
    assert(!gate.takeClick(2400));
    gate.buttonBegin(3000); gate.touchBegin(3010); gate.touchEnd(3100); gate.buttonEnd(3110,false);
    assert(!gate.takeClick(3400));
    gate.buttonBegin(4000); gate.buttonEnd(4100,false); gate.touchBegin(4150); gate.touchEnd(4200);
    assert(!gate.takeClick(4500));
    gate.buttonBegin(5000); gate.buttonEnd(7000,true); assert(!gate.takeClick(7300));
    std::cout << "Passed: geometry, modes, visible selection, touch/button arbitration, persistent bounded trails, range/stale cleanup and clipping.\n";
}
