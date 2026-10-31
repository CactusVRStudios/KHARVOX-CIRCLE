#pragma once
#include "hand_policy.hpp"

namespace circle::motor::hands {
// Charge is local until a physical punch. Cancelling a charge therefore has
// no native button release and cannot run an attack or charge animation.
class PunchCharge {
 uint64_t owner{},epoch{},last{},start{};
 bool ready{},down{};
public:
 unsigned update(uint64_t entity,uint64_t generation,uint64_t tick,bool eligible,float grip,float trigger,bool strike){
  const bool reset=entity!=owner||generation!=epoch||!last||tick<last||tick-last>100;
  if(reset){start=0;ready=down=false;owner=entity;epoch=generation;}
  last=tick;
  if(!entity||!std::isfinite(trigger)){start=0;ready=down=false;return 0;}
  // Native attack/hit recovery can briefly inhibit combat. Cancel accumulated
  // charge, but retain this hand's previously armed trigger across that gap.
  // No request is emitted until a new, eligible physical stroke.
  if(!eligible){start=0;down=false;if(trigger<=.1f)ready=false;return 0;}
  if(!fistReady(grip)){start=0;down=false;ready=trigger<=.1f;return 0;}
  if(trigger<=.1f){ready=true;start=0;down=false;return 0;}
  if(ready&&trigger>=.15f){if(!down)start=tick;down=true;}
  if(!strike||!down)return 0;
  const auto duration=unsigned(std::min<uint64_t>(tick-start,2000));
  start=tick; // Each subsequent physical stroke earns its own charge time.
  return duration;
 }
};
inline int64_t creditedPunchStart(int64_t now,unsigned chargeMs){
 // Motor reflected gameTimeUnique_t uses 999960 ticks/second.
 const auto elapsed=int64_t(std::min(chargeMs,2000u))*999960/1000;
 return now>elapsed?now-elapsed:1;
}
inline bool physicalChargeReleaseReady(unsigned chargeMs,unsigned state,unsigned hand,unsigned creditedStages){
 // Native CHARGING=2 and its 350 ms charge threshold. A timestamp alone
 // does not dispatch the native charged attack; require an accepted VR charge.
 // Credit may originate at precharge; current state2 proves it has now reached
 // CHARGING even when the transition timestamp hook was not visited.
 return chargeMs>=350 && state==2 && hand<2 && (creditedStages&7)!=0 && !(creditedStages&8);
}
inline bool physicalItemReleaseReady(unsigned chargeMs,unsigned state,unsigned hand,unsigned stages){
 return chargeMs<350&&state==2&&hand<2&&!(stages&8);
}
inline bool physicalStrikeHandValid(bool unarmed,unsigned nativeHand){
 return nativeHand<2||(!unarmed&&nativeHand==~0u);
}
}
