#include "src/input.h"
#include <cassert>
int main(){
    KnobInput k;
    k.sample(0,true,0);k.sample(15,true,0);k.sample(60,false,0);k.sample(75,false,0);
    k.sample(475,false,0);assert(k.singles==1&&k.doubles==0);
    // Second press begins within the window; its release need not fall inside it.
    k.sample(1000,true,0);k.sample(1015,true,0);k.sample(1060,false,0);k.sample(1075,false,0);
    k.sample(1400,true,0);k.sample(1415,true,0);k.sample(1700,false,0);k.sample(1715,false,0);
    assert(k.singles==1&&k.doubles==1);k.sample(2500,false,0);assert(k.singles==1);
    // Half-notch reversals don't advance the cursor or cancel a click.
    k.sample(2600,true,-1);k.sample(2615,true,0);k.sample(2650,false,0);k.sample(2665,false,0);k.sample(3065,false,0);
    assert(k.singles==2&&k.steps==0);
    k.sample(3200,false,-1);assert(k.steps==0);k.sample(3202,false,-2);assert(k.steps==1);
    k.sample(3204,false,-1);k.sample(3206,false,0);assert(k.steps==0);
    // Held rotation consumes the click/hold gesture.
    k.sample(4000,true,0);k.sample(4015,true,0);k.sample(4050,true,-2);k.sample(5200,true,-2);
    assert(k.steps==1&&k.holds==0);k.sample(5300,false,-2);k.sample(5315,false,-2);k.sample(5800,false,-2);assert(k.singles==2);
    k.sample(6000,true,-2);k.sample(6015,true,-2);k.sample(7015,true,-2);k.sample(8000,true,-2);assert(k.holds==1);
    k.sample(8100,false,-2);k.sample(8115,false,-2);k.sample(9000,false,-2);assert(k.singles==2);
    KnobInput rollover;rollover.sample(UINT32_MAX-30,true,0);rollover.sample(UINT32_MAX-15,true,0);
    rollover.sample(10,false,0);rollover.sample(25,false,0);rollover.sample(425,false,0);assert(rollover.singles==1);
    KnobInput setup;setup.holdMs=5000;setup.sample(0,true,0);setup.sample(15,true,0);setup.sample(1015,true,0);assert(setup.holds==0);setup.sample(5015,true,0);assert(setup.holds==1);
    KnobInput directions;directions.sample(0,false,-2);assert(directions.freeSteps==1&&directions.heldSteps==0);directions.sample(10,true,-4);assert(directions.freeSteps==1&&directions.heldSteps==1);
    KnobInput bounce;bounce.sample(0,true,0);bounce.sample(4,false,0);bounce.sample(8,true,0);bounce.sample(12,false,0);bounce.sample(500,false,0);assert(!bounce.down&&bounce.singles==0);
}
