#pragma once
#include <algorithm>
#include <cmath>
namespace circle::fg {
// Integer Auto is deliberately independent of the real-frame-first planner.
// Decide at complete input boundaries; output evidence includes repeated and
// skipped XR slots, not just the render rate (which fixed FG itself throttles).
struct StableAutoFramegen {
 unsigned selected=2,trialFrom{},events{},windows{};
 double refresh{},last{},changed{},overload{},reserve{},cooldown{},trialStart{};
 double stableOutput{},trialHealthy{};
 double windowStart{},lastDisplay{},outputAt{},delivery{};
 unsigned delivered{};
 const char* reason="holding 2x";
 void clearOutput(){windowStart=lastDisplay=outputAt=delivery=0;delivered=windows=0;}
 void suspend(double now){last=now;overload=reserve=stableOutput=trialHealthy=0;clearOutput();if(trialFrom)trialStart=now;}
 void output(double now,double period,bool fresh,double displayTime){
  if(!std::isfinite(now)||!std::isfinite(period)||period<4||period>40)return;
  const double stamp=std::isfinite(displayTime)&&displayTime>0?displayTime:now;
  if(!lastDisplay||stamp<=lastDisplay||stamp-lastDisplay>2000){
   clearOutput();windowStart=lastDisplay=stamp;return;
  }
  lastDisplay=stamp;if(fresh)++delivered;
  const double elapsed=stamp-windowStart;
  if(elapsed>=1000){delivery=std::min(1.,delivered*period/elapsed);outputAt=now;++windows;windowStart=stamp;delivered=0;}
 }
 unsigned choose(double now,double period,double render,double perGenerated,unsigned maximum){
  if(!std::isfinite(now))return selected;
  if(!std::isfinite(period)||period<4||period>40||!std::isfinite(render)||render<=0||
     !std::isfinite(perGenerated)||perGenerated<0){suspend(now);reason="waiting for valid timing";return selected;}
  maximum=std::clamp(maximum,2u,4u);
  if(selected>maximum){selected=maximum;trialFrom=0;changed=now;suspend(now);reason="backend ceiling";++events;}
  if(std::abs(refresh-period)>.05||now<last){refresh=period;suspend(now);reason="measuring output";}
  // No elapsed time from a pause, missing telemetry, or scene transition counts
  // toward an upgrade/recovery. Ordinary input intervals are well below 250 ms.
  if(last&&now-last>250)suspend(now);
  const double dt=last?std::clamp(now-last,0.,100.):0;last=now;
  const bool evidence=outputAt&&now>=outputAt&&now-outputAt<=1500;
  if(trialFrom){
   const double age=now-trialStart;
   trialHealthy=evidence&&delivery>=.97?trialHealthy+dt:0;
   if((age>=1500&&evidence&&delivery<.90)||age>=6000){
    selected=std::min(trialFrom,maximum);trialFrom=0;changed=now;cooldown=now+20000;
    overload=reserve=stableOutput=trialHealthy=0;clearOutput();reason="rollback: lower stage did not sustain output";++events;return selected;
   }
   if(age>=3000&&windows>=2&&trialHealthy>=1500&&evidence&&delivery>=.97){
    trialFrom=0;reserve=stableOutput=trialHealthy=0;changed=now;reason="lower stage confirmed";++events;
   }
   return selected;
  }
  const double cost=render+perGenerated*(selected-1);
  const double nextCost=cost+perGenerated;
  const bool useful=double(selected+1)/nextCost>=1.08*selected/cost;
  const bool severe=evidence&&delivery<.80&&cost>period*selected*1.20&&useful&&selected<maximum;
  overload=severe?overload+dt:0;
  if(overload>=3000&&(!changed||now-changed>=10000)){
   ++selected;changed=now;overload=reserve=stableOutput=0;clearOutput();
   reason="upshift: sustained major output and workload deficit";++events;return selected;
  }
  const unsigned lower=selected>2?selected-1:2;
  const bool fits=selected>2&&evidence&&delivery>=.97&&
   render+perGenerated*(lower-1)<=period*lower*.85&&now>=cooldown;
  reserve=fits?reserve+dt:0;
  // Fixed FG itself changes observed producer timing. Do not require the
  // higher-stage estimate to prove a lower stage can work before testing it.
  // A long healthy output run earns one bounded experiment, never a blind
  // permanent decrease. Failed trials still enforce the full cooldown.
  const bool eligible=selected>2&&evidence&&now>=cooldown;
  const bool healthy=eligible&&delivery>=.95;
  // Brief 90-95% output dips must not erase a nearly complete observation.
  // Sustained deficits still drain credit, and severe/stale output resets it.
  if(healthy)stableOutput+=dt;
  else if(eligible&&delivery>=.90)stableOutput=std::max(0.,stableOutput-dt*.5);
  else stableOutput=0;
  const bool measuredProbe=reserve>=20000;
  if((measuredProbe||(healthy&&stableOutput>=3000))&&now-changed>=3000){
   trialFrom=selected;selected=lower;trialStart=changed=now;overload=reserve=stableOutput=trialHealthy=0;clearOutput();
   reason=measuredProbe?"probing lower integer stage after sustained headroom":"probing lower integer stage after 3s stable output";++events;return selected;
  }
  reason=!evidence?"waiting for output evidence":severe?"measuring sustained major deficit":
   now<cooldown?"holding after failed lower-stage probe":fits?"measuring sustained recovery":healthy?"measuring stable output for bounded probe":"holding integer stage";
  return selected;
 }
};
}
