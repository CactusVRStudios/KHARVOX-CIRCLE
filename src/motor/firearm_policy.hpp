#pragma once
#include "hand_pose.hpp"
#include "weapon_slots.hpp"
#include <string_view>
namespace circle::motor::hands {
inline bool muzzleParticleAsset(std::string_view asset){
 return asset.starts_with("weapon/particle_weapon_")&&asset.find("_muzzle_")!=std::string_view::npos;
}
// Native weaponPoseType_t: legacy firearm poses 1..13 and Great Circle
// revolver/pistols/SMGs/rifles/shotgun/turret 23..31. Camera/whip/items follow.
constexpr bool pickupFirearmPose(unsigned slot,int pose){
 return slot==10&&((pose>=1&&pose<=13)||(pose>=23&&pose<=31));
}
// The asset muzzle is relative to the weapon MODEL, not the animated hand
// anchor or the camera. Keep its rotation as well as its translation.
inline std::optional<Pose> modelMuzzle(Pose model,Pose local){
 if(!valid(model)||!valid(local)||length(local.position)>2.f)return {};
 const auto result=compose(model,local);
 return valid(result)?std::optional<Pose>{result}:std::nullopt;
}
// Convert OpenXR aim (-Z forward, +Y up) to Motor (+X forward, +Z up).
inline std::optional<Pose> motorFirePose(Pose controller){
 if(!valid(controller))return {};
 const auto q=normalized(controller.orientation);
 auto basis=rotationBasis(rotate(q,{0,0,-1}),rotate(q,{-1,0,0}),rotate(q,{0,1,0}));
 if(!basis)return {};
 return Pose{controller.position,*basis};
}
inline bool itemStrikeEligible(unsigned slot,unsigned hand,float grip,float trigger,Vec /*forward*/,bool meleeMode=false){
 return itemMeleeSlot(slot)&&hand<2&&fistReady(grip)&&std::isfinite(trigger)&&
  (!firearmSlot(slot)||hand==0||meleeMode||trigger<=.1f);
}
// Native Great Circle long-gun poses: SMG01/02, RIFLE01/02, SHOTGUN01.
constexpr bool twoHandFirearmPose(int pose){return pose>=26&&pose<=30;}
struct NativeSupportPose {
 Pose candidate{},value{};uint64_t since{},last{};bool ready{};
 bool observe(Pose pose,uint64_t now){
  const float distance=length(pose.position);
  if(!valid(pose)||distance<.08f||distance>1.2f){since=0;return false;}
  const auto a=candidate.orientation,b=pose.orientation;
  if(!since||now<last||now-last>100||length(pose.position-candidate.position)>.01f||std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)<.998f){candidate=pose;since=now;}
  last=now;if(now-since<(ready?400u:200u))return false;
  const auto x=value.orientation,y=candidate.orientation;
  if(ready&&length(value.position-candidate.position)<=.002f&&std::abs(x.x*y.x+x.y*y.y+x.z*y.z+x.w*y.w)>=.9998f)return false;
  value=candidate;ready=true;return true;
 }
};
// ARGENT TwoHandSupport contract: calibrated support vector, shortest-arc
// correction preserves primary roll. No temporal filter or universal barrel axis.
inline Pose supportedWrist(Pose primary,Vec supportLocal,Vec offhand){
 const Vec actual=offhand-primary.position;
 if(length(actual)<.08f||length(actual)>1.2f)return primary;
 primary.orientation=normalized(multiply(between(rotate(primary.orientation,supportLocal),actual),primary.orientation));
 return primary;
}
// A virtual buttstock near the right shoulder reduces primary-hand angular
// wobble. Head yaw transports the shoulder; pitch/roll do not lift it.
inline Pose gunstockWrist(Pose primary,Vec supportLocal,Vec offhand,Pose head){
 const Vec f=rotate(head.orientation,{0,0,-1});
 const Vec forward=unit({f.x,f.y,0},{1,0,0});
 const Vec right=unit(cross(forward,{0,0,1}),{0,-1,0});
 const Vec shoulder=head.position+right*.18f-forward*.10f-Vec{0,0,.22f};
 const float weight=std::clamp((.55f-length(primary.position-shoulder))/.30f,0.f,1.f)*.75f;
 const Vec direct=offhand-primary.position,stock=offhand-shoulder;
 if(length(direct)<.08f||length(direct)>1.2f||length(stock)<.08f)return primary;
 const Vec direction=unit(unit(direct)*(1-weight)+unit(stock)*weight);
 return supportedWrist(primary,supportLocal,primary.position+direction*length(direct));
}
inline bool muzzleFlashMatches(Pose shot,Pose effect,uint64_t fired,uint64_t now){
 return valid(shot)&&valid(effect)&&fired&&now>=fired&&now-fired<=250&&
  length(shot.position-effect.position)<=.005f&&dot(rotate(shot.orientation,{1,0,0}),rotate(effect.orientation,{1,0,0}))>.999f;
}
// Preserve the effect's authored offset/roll in the shot muzzle frame, then
// transport it with the CURRENT rendered muzzle (including controller motion).
inline std::optional<Pose> renderMuzzleParticle(Pose shot,Pose currentMuzzle,Pose effect,uint64_t fired,uint64_t now){
 if(!valid(currentMuzzle)||!muzzleFlashMatches(shot,effect,fired,now))return {};
 const auto wanted=compose(currentMuzzle,compose(inverse(shot),effect));
 return valid(wanted)?std::optional<Pose>{wanted}:std::nullopt;
}
struct SupportGrab {
 bool armed{},latched{},consumed{};
 bool update(bool valid,float grip,bool withinReach){
  if(grip<=.65f){latched=consumed=false;armed=valid;return false;}
  if(!valid){latched=false;armed=false;return consumed;}
  if(grip>=.9f&&armed){armed=false;if(withinReach)latched=consumed=true;}
  return consumed;
 }
};
struct StableWeaponGrip {
 Pose candidate{},value{};uint64_t since{},last{};bool ready{};
 bool observe(Pose local,uint64_t now){
  if(ready)return true;
  if(!valid(local)||length(local.position)>.18f){since=last=0;return false;}
  const auto a=candidate.orientation,b=local.orientation;
  const float agreement=std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);
  if(!since||now<last||now-last>100||length(local.position-candidate.position)>.002f||agreement<.9998f){candidate=local;since=now;}
  last=now;
  if(now-since>=200){value=candidate;ready=true;}
  return ready;
 }
};
// Capture both native transforms in the same settled interval. Keep the last
// good pair through animation, but allow a later settled pose to repair it.
struct StableWeaponBinding {
 Pose candidate{},referenceCandidate{},value{},referenceValue{};
 uint64_t since{},last{};bool ready{};
 static bool close(Pose a,Pose b){
  const auto x=a.orientation,y=b.orientation;
  return length(a.position-b.position)<=.002f&&std::abs(x.x*y.x+x.y*y.y+x.z*y.z+x.w*y.w)>=.9998f;
 }
 bool observe(Pose grip,Pose reference,uint64_t now,bool eligible){
  if(!eligible||!valid(grip)||!valid(reference)||length(grip.position)>.18f||length(reference.position)>.18f){since=last=0;return false;}
  if(!since||now<last||now-last>100||!close(candidate,grip)||!close(referenceCandidate,reference)){
   candidate=grip;referenceCandidate=reference;since=now;
  }
  last=now;
  if(now-since<(ready?400u:200u))return false;
  if(ready&&close(value,candidate)&&close(referenceValue,referenceCandidate))return false;
  value=candidate;referenceValue=referenceCandidate;ready=true;return true;
 }
};
// Bind the weapon to the solved wrist. The native weapon anchor is camera-
// parented, so changing the arm alone does not move that independent joint.
inline std::optional<Pose> firearmWristTarget(Pose controller,Pose wristToWeapon){
 auto aim=motorFirePose(controller);
 if(!aim||!valid(wristToWeapon)||length(wristToWeapon.position)>.4f)return {};
 const auto inv=inverse(wristToWeapon);
 return Pose{controller.position,normalized(multiply(aim->orientation,inv.orientation))};
}

}
