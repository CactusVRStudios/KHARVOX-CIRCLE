#include "whip_policy.hpp"
#include "default_input.hpp"
#include <cstdio>
#include <limits>
using namespace circle::motor::hands;
int main(){
 int errors=0;auto check=[&](bool ok,const char* why){if(!ok){++errors;std::printf("FAIL %s\n",why);}};
 // Native state dispatch: ownership applies only to idle and completed normal
 // strokes. Traversal, attachments, explicit weapon changes and QTEs retain
 // their original native dispatch, including when grip remains pressed.
 for(unsigned state=0;state<=255;++state){
  const auto expected=state==31?WhipIdleRoute::Hold:state==8?WhipIdleRoute::FinishStroke:WhipIdleRoute::Native;
  check(whipIdleRoute(true,false,18,0,state,false)==expected,"only IDLE and CRACK_END receive owned hold routing");
  for(unsigned reason=0;reason<5;++reason)
   check(whipIdleRoute(reason!=0,reason==1,reason==2?1:18,reason==3?3:0,state,reason==4)==WhipIdleRoute::Native,
         "release/inhibit/slot change/pending weapon/attachment returns native ownership");
 }
 for(unsigned frame=0;frame<600;++frame)
  check(whipIdleRoute(true,false,18,18,31,false)==WhipIdleRoute::Hold,"stationary ten-second grip cannot enter stock auto-stow branch");
 check(whipIdleRoute(true,false,18,0,8,false)==WhipIdleRoute::FinishStroke&&
       whipIdleRoute(true,false,18,0,31,false)==WhipIdleRoute::Hold,"completed stroke returns to owned idle for another swing");
 for(unsigned state=0;state<=255;++state)
  check(whipIdleRoute(true,false,18,18,state,false,2)==WhipIdleRoute::Native,"accepted native staged action must not be replaced by owned idle");
 check(whipObjectStroke(true,false,123,-1,true),"visible native object plus physical edge uses staged object action");
 for(unsigned reject=0;reject<5;++reject)
  check(!whipObjectStroke(reject!=0,reject==1,reject==2?UINT64_MAX:123,reject==3?0:-1,reject!=4),
        "no edge, inhibition, no target, actor hit group or invisible target cannot request object stroke");
 check(!whipObjectStroke(false,false,123,-1,true),"shoulder draw and stationary grip do not attach automatically");
 check(whipIdleRoute(true,false,18,0,47,false)==WhipIdleRoute::Native&&
       whipIdleRoute(true,false,18,0,23,false)==WhipIdleRoute::Native&&
       whipIdleRoute(true,false,18,0,18,true)==WhipIdleRoute::Native,"staged stroke, attachment entry and attached idle retain native animation and timing");
 WhipSample s{1,1,1000,1,true,true,true,0,{.25f,-.10f,-.10f},{}};
 ShoulderWhip w;w.update(s);s.time+=10;s.grip=1;
 auto out=w.update(s);check(out.held&&!out.equipped&&out.actions==whipSelect,"shoulder grip selects whip without attacking");
 s.time+=20;s.relative.z=.3f;s.velocity.z=3;out=w.update(s);
 check(!(out.actions&whipCrack),"no strike before native slot acknowledgement");
 s.slot=18;s.time+=20;out=w.update(s);check(out.equipped&&!(out.actions&whipCrack),"fast draw cannot itself crack whip");
 s.time+=20;s.velocity={};w.update(s);
 s.time+=20;s.relative.z=.4f;s.velocity.z=2.5f;out=w.update(s);check(out.actions&whipCrack,"settle then forward swing cracks native whip");
 s.time+=20;s.relative.z=.5f;out=w.update(s);check(out.actions&whipCrack,"native pulse persists across command polls");
 s.time+=100;w.update(s);s.time+=100;out=w.update(s);check(!(out.actions&whipCrack),"continuous fast movement cannot repeat strike");
 s.time+=20;s.grip=0;out=w.update(s);check(!out.held&&out.actions==whipStow,"grip release stows and releases crack");
 s.time+=130;check(w.update(s).actions==0,"stow pulse releases");
 for(unsigned cancel=0;cancel<7;++cancel){
  ShoulderWhip p;auto a=WhipSample{1,1,1000,1,true,true,true,0,{.25f,-.1f,-.1f},{}};
  p.update(a);a.time+=10;a.grip=1;p.update(a);a.slot=18;a.time+=10;p.update(a);
  if(cancel==0)a.eligible=false; // pause / QTE / cutscene / modifier / focus
  if(cancel==1)a.tracked=false;
  if(cancel==2)++a.owner;
  if(cancel==3)++a.epoch;
  if(cancel==4)a.time=900;
  if(cancel==5)a.relative.x=std::numeric_limits<float>::quiet_NaN();
  if(cancel==6)a.slot=10;
  a.time+=10;out=p.update(a);check(!out.held&&!out.actions,"context, ownership, tracking and held-item cancellation release actions");
  a.eligible=a.tracked=true;a.relative={.25f,-.1f,-.1f};a.slot=1;a.time+=10;
  check(!p.update(a).held,"held grip cannot draw on resume");
  a.grip=0;a.time+=10;p.update(a);a.grip=1;a.time+=10;
  check(p.update(a).held,"fresh grip after resume restores shoulder draw");
 }
 for(auto pos:{Vec{-.25f,-.1f,-.1f},Vec{.25f,-.7f,-.1f},Vec{.25f,-.1f,.5f}}){
  ShoulderWhip p;auto a=s;a.slot=1;a.grip=0;a.relative=pos;a.time=1000;p.update(a);a.time+=10;a.grip=1;
  check(!p.update(a).held,"left shoulder, waist and forward fist cannot draw");
 }
 {
  ShoulderWhip p;auto a=s;a.slot=1;a.time=1000;a.grip=1;a.relative={.25f,-.1f,-.1f};
  check(!p.update(a).held,"startup with held grip requires release");
  a.grip=0;a.time+=10;p.update(a);a.grip=1;a.time+=10;p.update(a);
  a.time+=1600;out=p.update(a);check(!out.held&&out.actions==whipStow,"unavailable whip times out and returns to fists");
 }
 {
  ShoulderWhip p;auto a=s;a.slot=2;a.grip=0;a.time=1000;p.update(a);a.grip=1;a.time+=10;
  check(!p.update(a).held,"firearms are not discarded by shoulder gesture");
 }
 {
  ShoulderWhip p;WhipSample a{1,1,1000,1,true,true,true,0,{.25f,-.1f,-.1f},{}};
  p.update(a);a.time+=10;a.grip=1;p.update(a);a.slot=18;a.relative.z=.2f;a.time+=20;p.update(a);
  a.time+=20;a.relative.z=.3f;a.velocity.z=-3;check(p.update(a).actions&whipCrack,"backward swing uses magnitude like every direction");
  a.time+=20;a.velocity={};p.update(a);a.time+=20;a.relative.z=.4f;a.velocity.z=3;p.update(a);
  a.time+=20;a.velocity={};p.update(a);a.time+=20;a.relative.z=.5f;a.velocity.z=3;p.update(a);
  a.time+=60;p.update(a);a.time+=60;check(!(p.update(a).actions&whipCrack),"cooldown prevents a second pulse extending the first");
  a.time+=400;a.velocity={};p.update(a);a.time+=20;p.update(a);a.time+=20;a.relative.z=.6f;a.velocity.z=3;
  check(p.update(a).actions&whipCrack,"new deliberate swing after cooldown works without re-drawing");
  a.time+=10;a.eligible=false;a.grip=0;check(p.update(a).actions==0,"release inside QTE does not force weapon switch");
  a.time+=20;a.eligible=true;check(p.update(a).actions==whipStow,"deferred stow only on normal gameplay return");
 }
 using namespace circle::motor::input;
 Profile profile;Physical v;v.focused=true;v.session=1;v.hand[0].tracked=v.hand[1].tracked=true;
 profile.update(v,circle::motor::input::Context::Unarmed,1000);
 v.rightWhip=true;v.hand[1].grip=1;v.hand[1].trigger=1;
 auto mapped=profile.update(v,circle::motor::input::Context::Unarmed,1010,2);
 check(mapped.pad.rt==0&&mapped.fingerGesture[1]==0,"draw reserves right attack and punch while native slot changes");
 mapped=profile.update(v,circle::motor::input::Context::Whip,1020);
 check(!(mapped.pad.buttons&X)&&mapped.pad.rt==0,"held whip grip does not spam native use or trigger attack");
 v.hand[0].lower=true;mapped=profile.update(v,circle::motor::input::Context::Whip,1030);
 check(mapped.pad.buttons&X,"explicit interact button remains accessible");
 v.hand[0].upper=true;mapped=profile.update(v,circle::motor::input::Context::Whip,1040);
 check(mapped.pad.buttons&RB,"native Y+RT fallback wins over whip gesture");
 // The exact punch detector drives whip recognition. Exercise repeated
 // strokes and direction discrimination at several headset pitches; native
 // attack pulses must fall between strokes while equipment ownership stays.
 for(const auto forward:{Vec{0,0,1},unit(Vec{0,.7f,.7f}),unit(Vec{0,-.7f,.7f})}){
  ShoulderWhip p;WhipSample a{1,1,1000,1,true,true,true,0,{.25f,-.1f,-.1f},{}};
  p.update(a);a.time+=10;a.grip=1;p.update(a);a.slot=18;
  a.strikeForward=forward;a.relative=forward*.3f;
  a.time+=20;a.velocity=forward*3.f;
  check(!(p.update(a).actions&whipCrack),"draw motion remains disarmed at all head pitches");
  a.time+=20;a.velocity={};p.update(a);
  for(const auto nonStrike:{forward*1.9f}){
   a.time+=20;a.velocity=nonStrike;
   check(!(p.update(a).actions&whipCrack),"subthreshold movement cannot crack");
  }
  for(unsigned swing=0;swing<3;++swing){
   a.time+=20;a.velocity={};p.update(a);
   a.time+=20;a.velocity=forward*2.5f;
   out=p.update(a);
   check(out.held&&out.equipped&&(out.actions&whipCrack),"each forward punch creates a new crack while grip stays held");
   for(unsigned frame=0;frame<20;++frame){
    a.time+=20;out=p.update(a);
    check(out.held&&out.equipped,"equipment remains owned after attack pulse releases");
    if(frame>=9)check(!(out.actions&whipCrack),"constant speed produces no repeat and attack releases after 200ms");
   }
  }
  a.time+=20;a.velocity={};a.grip=.6f;p.update(a);
  a.time+=20;a.velocity=forward*3.f;out=p.update(a);
  check(out.held&&!(out.actions&whipCrack),"loose grip retains equipment but cannot strike");
  a.time+=20;a.grip=1;
  check(!(p.update(a).actions&whipCrack),"tightening grip during fast movement requires rearm");
  a.time+=20;a.velocity={};p.update(a);
  a.time+=200;a.velocity=forward*3.f;
  check(!(p.update(a).actions&whipCrack),"tracking gap disarms rather than firing stale motion");
  a.time+=20;a.velocity={};p.update(a);
  a.time+=20;a.velocityValid=false;a.velocity=forward*3.f;
  check(!(p.update(a).actions&whipCrack),"invalid velocity does not strike");
  a.time+=20;a.velocityValid=true;
  check(!(p.update(a).actions&whipCrack),"tracking resume requires settle before another strike");
  a.time+=20;a.grip=0;
  check(p.update(a).actions==whipStow,"grip release stows after repeated strikes");
 }
 {
  VelocityWhipPull p;uint64_t t=1000;
  auto step=[&](Vec v,bool safe=true){t+=20;return p.update(1,1,t,v,{0,0,-1},safe);};
  check(!step({})&&!step({}),"pull settles before arming");
  check(!step({0,0,-2})&&!step({2,0,0}),"forward and sideways movement never yank");
  check(!step({0,0,.5f}),"slow movement never yanks");
  check(step({0,0,2}),"fast backward pull emits yank");
  for(int i=0;i<30;++i)step({0,0,2});
  check(!step({0,0,2}),"continuous motion cannot repeat yank");
  step({});check(step({0,0,2}),"settle after cooldown rearms pull");
  check(!step({},false),"lost attachment or focus cancels pulse immediately");
  check(!step({0,0,2})&&!step({0,0,2}),"recovery in motion never yanks");
  step({});t+=1000;check(!step({0,0,2}),"tracking gap cannot create pull");
  step({});check(!p.update(2,1,t+20,{0,0,2},{0,0,-1},true),"owner change resets pull");
  VelocityWhipPull rotated;rotated.update(1,1,1000,{}, {1,0,0},true);
  rotated.update(1,1,1020,{}, {1,0,0},true);
  check(rotated.update(1,1,1040,{-2,0,0},{1,0,0},true),"pull follows settled controller orientation");
 }
 return errors?1:0;
}
