#pragma once
#include "hand_pose.hpp"
#include "turret_aim.hpp"

namespace circle::motor::turret {
using hands::Pose;using hands::Quat;using hands::Vec;
// Camera and rendered player are children of the stationary gun mount, never
// children of its yaw/pitch joints or the native user-view angles.
struct SeatAnchor {
 uint64_t owner{},gun{},generation{};Pose localCamera{};Quat reference{};
 void reset(){owner=gun=0;}
 bool update(uint64_t who,uint64_t weapon,Pose mount,Pose nativeCamera,Quat head,bool recenter,float entryRadius=4.f){
  if(!who||!weapon||!hands::valid(mount)||!hands::valid(head))return false;
  if(owner!=who||gun!=weapon){
   if(!hands::valid(nativeCamera)||!std::isfinite(entryRadius)||entryRadius<=0||hands::length(nativeCamera.position-mount.position)>entryRadius)return false;
   owner=who;gun=weapon;++generation;
   localCamera=hands::compose(hands::inverse(mount),nativeCamera);reference=head;
  }else if(recenter)reference=head;
  return true;
 }
 Pose camera(Pose mount)const{return hands::compose(mount,localCamera);}
 // Moving driver seats finish their native placement AFTER mode entry.
 // Follow their authored eye origin without importing native look rotation
 // or changing the headset reference. Stationary gun seats do not use this.
 bool followDriverPosition(Pose mount,Vec eye,float radius){
  if(!owner||!hands::valid(mount)||!hands::finite(eye)||!std::isfinite(radius)||radius<=0||
     hands::length(eye-mount.position)>radius)return false;
  localCamera.position=hands::rotate(hands::inverse(mount).orientation,eye-mount.position);
  return true;
 }
 bool followDriverPose(Pose mount,Pose driver,float radius){
  if(!hands::valid(driver)||!followDriverPosition(mount,driver.position,radius))return false;
  localCamera.orientation=hands::multiply(hands::inverse(mount).orientation,driver.orientation);
  return true;
 }
};
// Pose deltas go only to a private copy consumed by the gun joint calculation.
// Native mount angle changes remain additive. Neither aiming input uses XInput
// motion injection and no write to player/camera view-angle components.
struct Aim {
 uint64_t owner{},gun{},session{},time{};
 Vec previousNative{},command{};float yaw{},pitch{};bool tracked{};
 void reset(){*this={};}
 Vec update(uint64_t who,uint64_t weapon,uint64_t xrSession,uint64_t ns,Quat q,
            Vec native,Vec minimum,Vec maximum,bool allowed,bool manual,Vec stick={},float dt=0){
  if(owner!=who||gun!=weapon){reset();owner=who;gun=weapon;command=previousNative=native;}
  const float dp=std::remainder(native.x-previousNative.x,360.f);
  const float dy=std::remainder(native.y-previousNative.y,360.f);
  // Native forced-angle/scene discontinuity should not become an accumulated turn.
  if(std::hypot(dp,dy)>90){command=native;tracked=false;}
  else {command.x+=dp;command.y+=dy;}
  previousNative=native;command.z=native.z;
  if(allowed&&manual&&hands::finite(stick)&&dt>0&&dt<=.1f){
   const float magnitude=std::hypot(stick.x,stick.y);
   constexpr float deadzone=8689.f/32767.f;
   if(magnitude>deadzone){
    const float scale=std::clamp((magnitude-deadzone)/(1-deadzone),0.f,1.f)/magnitude;
    command.y-=stick.x*scale*90.f*dt;
    command.x-=stick.y*scale*90.f*dt;
   }
  }
  const auto f=hands::rotate(q,{0,0,-1});
  const bool valid=allowed&&!manual&&ns&&hands::valid(q)&&std::hypot(f.x,f.z)>.15f;
  if(valid){
   const float y=std::atan2(f.x,-f.z)*57.295779513f;
   const float p=std::asin(std::clamp(f.y,-1.f,1.f))*57.295779513f;
   if(tracked&&session==xrSession&&ns>time&&ns-time<=100000000){
    const float x=std::remainder(y-yaw,360.f),v=p-pitch;
    // Mounted gun joints invert native view-angle motion relative to the XR
    // pointing basis. Headset verification: positive right/up needs addition.
    if(std::hypot(x,v)<=30){command.y+=x;command.x+=v;}
   }
   // Duplicate polls retain output and reference, without a second delta.
   if(ns!=time||!tracked){yaw=y;pitch=p;time=ns;}
   session=xrSession;tracked=true;
  }else {tracked=false;time=0;}
  // Native limits are world angles and may straddle +/-180. Preserve the
  // equivalent interval rather than clamping against a wrapped scalar.
  const auto limit=[](float angle,float low,float high){
   if(!std::isfinite(low+high)||std::abs(high-low)>=359.f)return angle;
   if(high<low)high+=360.f;
   const float center=(low+high)*.5f;
   return std::clamp(center+std::remainder(angle-center,360.f),low,high);
  };
  command.x=limit(command.x,minimum.x,maximum.x);
  command.y=limit(command.y,minimum.y,maximum.y);
  return command;
 }
};
inline std::array<float,12> matrix(Pose p){
 const auto x=hands::rotate(p.orientation,{1,0,0});
 const auto y=hands::rotate(p.orientation,{0,1,0});
 const auto z=hands::rotate(p.orientation,{0,0,1});
 return {p.position.x,p.position.y,p.position.z,x.x,x.y,x.z,y.x,y.y,y.z,z.x,z.y,z.z};
}
inline std::optional<Pose> pose(const std::array<float,12>& m){
 auto q=hands::rotationBasis({m[3],m[4],m[5]},{m[6],m[7],m[8]},{m[9],m[10],m[11]});
 if(!q)return {};Pose p{{m[0],m[1],m[2]},*q};return hands::valid(p)?std::optional<Pose>(p):std::nullopt;
}
}
