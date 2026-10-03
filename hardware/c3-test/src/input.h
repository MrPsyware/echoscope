#pragma once
#include <cstdint>
// Pure gesture state; sampled independently of display/USB work.
struct KnobInput {
    bool raw=false,down=false,turned=false,longFired=false,pending=false,second=false;
    uint32_t changed=0,pressedAt=0,releasedAt=0,singles=0,doubles=0,holds=0,events=0;
    int32_t rawTicks=0,steps=0,remainder=0,freeSteps=0,heldSteps=0;
    uint32_t holdMs=1000;
    const char *event="READY";
    void emit(const char *name){event=name;++events;}
    void single(){++singles;pending=false;emit("SINGLE CLICK");}
    void sample(uint32_t now,bool level,int32_t ticks){
        if(level!=raw){raw=level;changed=now;if(raw&&!down)turned=false;}
        // User calibration: two transitions per notch; negate electrical direction.
        remainder-=ticks-rawTicks;rawTicks=ticks;
        int delta=remainder/2;remainder-=delta*2;
        if(delta){steps+=delta;
            if(raw||down){heldSteps+=delta;turned=true;pending=false;emit("HOLD AND TURN");}
            else {freeSteps+=delta;emit(delta>0?"CLOCKWISE":"ANTICLOCKWISE");}
        }
        if(raw!=down&&uint32_t(now-changed)>=15){down=raw;
            if(down){
                if(pending&&uint32_t(now-releasedAt)>400)single();
                second=pending;pending=false;pressedAt=now;longFired=false;emit("PRESS");
            }else if(!turned&&!longFired){
                if(second){++doubles;second=false;emit("DOUBLE CLICK");}
                else{pending=true;releasedAt=now;}
            }else{second=false;emit("RELEASE");}
        }
        if(down&&!turned&&!longFired&&uint32_t(now-pressedAt)>=holdMs){longFired=true;second=false;pending=false;++holds;emit("LONG PRESS");}
        if(pending&&!down&&!raw&&uint32_t(now-releasedAt)>=400)single();
    }
};
