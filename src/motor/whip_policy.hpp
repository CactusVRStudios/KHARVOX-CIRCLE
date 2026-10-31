#pragma once
#include "hand_policy.hpp"

namespace circle::motor::hands {
// Bit25 is a private draw request consumed by our Indiana selector adapter:
// retail Indiana ignores _weapwhip in its normal loadout branch. It must never
// be substituted with bit13 here: that would also increment the crack sequence.
inline constexpr uint64_t whipSelect=uint64_t(1)<<25, whipCrack=uint64_t(1)<<13,
                          whipStow=uint64_t(1)<<27;
struct WhipSample {
 uint64_t owner{},epoch{},time{};
 unsigned slot{};
 bool eligible{},tracked{},velocityValid{};
 float grip{};
 // Yaw-only head frame: x right, y up, z FORWARD, in metres.
 Vec relative{},velocity{};
 // Full headset forward (including pitch), expressed in the same yaw frame.
 Vec strikeForward{0,0,1};
};
struct WhipOutput {uint64_t actions{};bool held{},equipped{};};
// Native IDLE (31) unconditionally queues UNEQUIP after an ordinary draw;
// CRACK_END (8) does the same after a stroke. Only our owned, unattached whip
// may remain idle. Do not interfere with native attached/traversal states.
enum class WhipIdleRoute { Native, Hold, FinishStroke };
inline WhipIdleRoute whipIdleRoute(bool owned,bool inhibited,unsigned slot,unsigned pendingSlot,
                                  unsigned state,bool attached,unsigned switchState=0){
 // An accepted native STAGE equip owns its initial action, including object
 // attachments. Never replace that action with our equipment-only idle hold.
 if(!owned||inhibited||slot!=18||(pendingSlot&&pendingSlot!=18)||attached||switchState==2)return WhipIdleRoute::Native;
 if(state==31)return WhipIdleRoute::Hold;
 if(state==8)return WhipIdleRoute::FinishStroke;
 return WhipIdleRoute::Native;
}
inline bool whipObjectStroke(bool edge,bool inhibited,uint64_t lookAt,int hitGroup,bool visible){
 // Native lookAtGroup==-1 distinguishes non-actor whippables. Use only the
 // native visible target; Motor still validates range, traces and attachment.
 return edge&&!inhibited&&lookAt!=UINT64_MAX&&hitGroup==-1&&visible;
}
// Pull opposite the controller's settled pointing direction. Tracking-space
// velocity excludes stick locomotion; require rest before every deliberate pull.
class VelocityWhipPull {
 uint64_t owner{},session{},last{},pulseUntil{},cooldownUntil{};
 Vec direction{};float distance{};bool armed{};
public:
 bool update(uint64_t actor,uint64_t epoch,uint64_t time,Vec velocity,Vec forward,bool eligible){
  if(!eligible||!actor||!time||!finite(velocity)||!finite(forward)||length(forward)<.9f){*this={};return false;}
  if(owner!=actor||session!=epoch||time<last||time-last>100){
   *this={};owner=actor;session=epoch;last=time;return false;
  }
  if(time==last)return time<pulseUntil;
  const auto dt=time-last;last=time;
  const float speed=length(velocity);
  if(speed<.25f&&time>=cooldownUntil){armed=true;direction=unit(forward,{0,0,-1})*-1.f;distance=0;}
  const float pull=dot(velocity,direction);
  if(armed&&pull>.8f&&pull>speed*.65f){
   distance+=pull*float(dt)*.001f;
   if(distance>=.04f){armed=false;distance=0;pulseUntil=time+120;cooldownUntil=time+350;}
  }else distance=0;
  return time<pulseUntil;
 }
};
class ShoulderWhip {
 uint64_t owner{},epoch{},lastTime{},drawAt{},selectUntil{},crackUntil{},stowUntil{};
 bool initialized{},gripArmed{},holding{},equipped{},pendingStow{};
 VelocityPunchDetector strike;
 static bool shoulder(Vec p){return finite(p)&&p.x>=.12f&&p.x<=.60f&&p.y>=-.30f&&p.y<=.30f&&p.z>=-.45f&&p.z<=.08f;}
public:
 WhipOutput update(const WhipSample& s){
  const bool reset=!initialized||s.owner!=owner||s.epoch!=epoch||s.time<lastTime;
  if(reset){*this={};initialized=true;owner=s.owner;epoch=s.epoch;}
  const bool continuous=lastTime&&s.time>lastTime&&s.time-lastTime<=100;
  const bool duplicate=lastTime&&s.time==lastTime;
  lastTime=s.time;
  const bool safe=s.eligible&&s.owner&&s.tracked&&finite(s.relative)&&std::isfinite(s.grip);
  if(!safe){
   pendingStow|=holding;holding=equipped=gripArmed=false;strike={};
   selectUntil=crackUntil=stowUntil=0;return {};
  }
  if(s.grip<=.35f){
   gripArmed=true;
   if(holding){pendingStow=true;holding=equipped=false;strike={};selectUntil=crackUntil=0;}
   // Never change weapons inside a scripted context. Stow our whip only on
   // returning to ordinary play with released grip; do not replace a new item.
   if(pendingStow){if(s.slot==18||s.slot==1)stowUntil=s.time+120;pendingStow=false;}
  }
  if(holding&&s.slot!=1&&s.slot!=18){holding=equipped=false;strike={};selectUntil=crackUntil=0;}
  if(!holding&&gripArmed&&s.grip>=.9f){
   gripArmed=false;
   // First implementation draws only from free hands. Never discard an item.
   if(s.slot==1&&shoulder(s.relative)){
    holding=true;drawAt=s.time;selectUntil=s.time+120;stowUntil=0;pendingStow=false;strike={};
   }
  }
  if(holding){
   equipped|=s.slot==18;
   if(!equipped&&s.time-drawAt>1500){holding=false;selectUntil=crackUntil=0;stowUntil=s.time+120;}
   // A native weapon change after acknowledgement cancels gesture ownership.
   if(equipped&&s.slot!=18){holding=equipped=false;strike={};selectUntil=crackUntil=0;}
  }
  if(!duplicate){
   const bool strikeReady=holding&&equipped&&continuous&&s.velocityValid&&fistReady(s.grip);
   // Same speed magnitude, rearm and cooldown as physical punches. A fast
   // shoulder draw starts disarmed and cannot itself become an attack.
   if(strike.update(s.velocity,{},s.time,strikeReady))crackUntil=s.time+200;
  }
  return {(s.time<selectUntil?whipSelect:0)|(s.time<crackUntil?whipCrack:0)|(s.time<stowUntil?whipStow:0),holding,equipped};
 }
};
}
