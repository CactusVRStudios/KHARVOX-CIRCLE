#pragma once
#include "locomotion_policy.hpp"
#include <array>
#include <optional>
namespace circle::motor::hands {
// WADE20 retains usable hands/equipment, including Himalaya terrain. Keep
// surface/subsurface swimming restricted; callers still check native ownership.
inline bool trackedArmMovement(unsigned mode){return locomotion::nativeControlledPosture(mode)||mode==20;}
// Water owns its movement rules, but the visible hands still belong to the
// controllers. Keep this separate from combat/whip eligibility.
inline bool trackedVisualArmMovement(unsigned mode){return trackedArmMovement(mode)||locomotion::waterMode(mode);}
using locomotion::Vec;using locomotion::dot;using locomotion::length;using locomotion::finite;
inline Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec unit(Vec v,Vec fallback={1,0,0}){const float n=length(v);return n>1e-6f&&std::isfinite(n)?v*(1/n):fallback;}
struct ArmSolution {Vec elbow{},wrist{};bool valid{},clamped{};};
// Model-space lengths and shoulder are owned by the native skeleton. The
// solver changes neither shoulder/root nor segment lengths.
inline ArmSolution solveArm(Vec shoulder,Vec nativeElbow,Vec target,float upper,float lower,Vec bendHint,float reach=1){
 ArmSolution out;
 if(!finite(shoulder)||!finite(nativeElbow)||!finite(target)||!finite(bendHint)||!std::isfinite(upper)||!std::isfinite(lower)||upper<.01f||lower<.01f||!std::isfinite(reach)||reach<=0)return out;
 Vec delta=target-shoulder;float requested=length(delta);
 const float minimum=std::abs(upper-lower)+.0001f,maximum=std::max(minimum,(upper+lower)*std::clamp(reach,.1f,1.f)-.0001f);
 float d=std::clamp(requested,minimum,maximum);Vec forward=unit(delta,unit(nativeElbow-shoulder));
 Vec pole=bendHint-forward*dot(bendHint,forward);
 if(length(pole)<.001f){pole=nativeElbow-shoulder;pole=pole-forward*dot(pole,forward);}
 if(length(pole)<.001f)pole=cross(forward,std::abs(forward.z)<.9f?Vec{0,0,1}:Vec{0,1,0});
 pole=unit(pole);float x=(upper*upper-lower*lower+d*d)/(2*d);float y=std::sqrt(std::max(0.f,upper*upper-x*x));
 out.elbow=shoulder+forward*x+pole*y;out.wrist=shoulder+forward*d;out.valid=finite(out.elbow)&&finite(out.wrist);out.clamped=std::abs(d-requested)>.00001f;return out;
}
struct HandFrame {
 Vec nativeCamera{},trackingHead{};locomotion::GroundFrame basis{};float referenceYaw{};
 std::optional<Vec> toWorld(Vec trackingController)const{
  if(!finite(nativeCamera)||!finite(trackingHead)||!finite(trackingController)||!basis.valid()||!std::isfinite(referenceYaw))return {};
  Vec relative=trackingController-trackingHead;
  // Horizontal motion already accepted by physics is in nativeCamera. HMD
  // absolute height is never added; only controller height relative to head.
  return nativeCamera+basis.trackingToWorld(relative,referenceYaw)+basis.up*(relative.y*basis.unitsPerMeter);
 }
};
enum class Context {Unknown,Menu,Exploration,Unarmed,MeleeItem,Firearm,Whip,Carrying,Traversal,Cinematic};
enum class Intent {None,Pause,Interact,Grab,Release,ContextUse,Fire,Whip,Punch,Block};
struct Request {bool pause{},interact{},grip{},release{},trigger{},punch{},block{};};
// Semantic requests only. No native IDs are assigned until measured in Motor.
inline Intent route(Context c,Request r,bool tracked){
 if(r.pause)return Intent::Pause;
 if(!tracked||c==Context::Unknown||c==Context::Menu||c==Context::Traversal||c==Context::Cinematic)return Intent::None;
 if(r.release)return Intent::Release;
 if(r.grip)return Intent::Grab;
 if(r.interact)return Intent::Interact;
 if(r.trigger)return c==Context::Firearm?Intent::Fire:c==Context::Whip?Intent::Whip:Intent::ContextUse;
 if(c==Context::Unarmed){if(r.block)return Intent::Block;if(r.punch)return Intent::Punch;}
 return Intent::None;
}
struct PunchSettings {float minSpeed=1.5f;uint64_t cooldownMs=350;};
struct GripSwipe {
 Vec anchor{};uint64_t since{};bool armed{};
 bool update(Vec handRelative,Vec right,uint64_t now,bool eligible,bool held){
  if(!eligible||!held||!finite(handRelative)||!finite(right)){armed=false;since=0;return false;}
  if(!since){anchor=handRelative;since=now;armed=true;return false;}
  if(!armed)return false; // Release grip before another native action.
  if(now==since)return false;
  if(now<since||now-since>650){anchor=handRelative;since=now;return false;}
  const float left=-dot(handRelative-anchor,unit(right));
  if(left>=.16f&&left*1000.f/float(now-since)>=.4f){armed=false;return true;}
  return false;
 }
};
struct PunchDetector {
 Vec previous{},velocity{};uint64_t previousTick{},lastPunch{};bool valid{},armed=true;
 void reset(){previous={};velocity={};previousTick=0;valid=false;armed=false;}
 bool update(Vec handRelativeToBody,Vec forward,uint64_t tick,Context context,bool tracked,bool occupied,bool gripping,const PunchSettings& config={}){
  if(!tracked||occupied||gripping||context!=Context::Unarmed||!finite(handRelativeToBody)||!finite(forward)||!std::isfinite(config.minSpeed)||config.minSpeed<=0){reset();return false;}
  if(!valid||tick<=previousTick||tick-previousTick>100){previous=handRelativeToBody;previousTick=tick;velocity={};valid=true;armed=false;return false;}
  float dt=float(tick-previousTick)*.001f;Vec displacement=handRelativeToBody-previous;
  previous=handRelativeToBody;previousTick=tick;
  if(length(displacement)>.35f){reset();return false;}
  Vec raw=displacement*(1/dt);float weight=1-std::exp(-dt/.025f);Vec old=velocity;velocity=velocity*(1-weight)+raw*weight;
  float speed=length(velocity),along=dot(velocity,unit(forward));
  if(speed<.35f)armed=true;
  bool cooldown=!lastPunch||(tick>=lastPunch&&tick-lastPunch>=config.cooldownMs);
  bool accelerating=dot(velocity-old,unit(forward))>0;
  if(armed&&cooldown&&speed>=config.minSpeed&&along>=config.minSpeed&&along>=speed*.75f&&accelerating){armed=false;lastPunch=tick;return true;}
  return false;
 }
};
// ARGENT's swing-speed variant: magnitude, 45% rearm and 350ms cooldown.
// Direction and a high-speed ceiling must not discard deliberate fast strokes.
// Analog squeeze belongs to the same hand as the strike. Invalid input fails closed.
inline float fistAmount(float squeeze) { return std::isfinite(squeeze)?std::clamp(squeeze,0.f,1.f):0.f; }
inline bool fistReady(float squeeze) { return std::isfinite(squeeze)&&squeeze>=.9f&&squeeze<=1.f; }
struct VelocityPunchDetector {
 bool armed{};uint64_t lastPunch{};Vec velocity{};
 bool update(Vec measured,Vec /*forward*/,uint64_t tick,bool eligible,float threshold=2.f){
  velocity=measured;
  const float speed=length(measured);
  if(!eligible||!finite(measured)||!std::isfinite(speed)||!std::isfinite(threshold)||threshold<1.f||threshold>4.f){armed=false;return false;}
  if(speed<=threshold*.45f){armed=true;return false;}
  if(!armed||speed<threshold||(lastPunch&&(tick<lastPunch||tick-lastPunch<350)))return false;
  armed=false;lastPunch=tick;return true;
 }
};
struct BlockDetector {
 uint64_t since{};bool held{};
 bool update(Vec left,Vec right,Context c,bool tracked,bool occupied,uint64_t now){
  if(c!=Context::Unarmed||!tracked||occupied||!finite(left)||!finite(right)){since=0;held=false;return false;}
  // Input coordinates: body-local metres, +X right, +Y forward, +Z up,
  // origin at the native head. A candidate only, not an automatic parry.
  float minZ=held?-.45f:-.30f;
  bool guard=left.z>minZ&&right.z>minZ&&left.z<.25f&&right.z<.25f&&left.y>.05f&&right.y>.05f&&left.y<.65f&&right.y<.65f&&std::abs(left.x)<.5f&&std::abs(right.x)<.5f;
  if(!guard){since=0;held=false;return false;}if(!since||now<since)since=now;
  held=now-since>=120;return held;
 }
};
}
