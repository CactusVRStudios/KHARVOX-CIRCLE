#pragma once
#include "hand_pose.hpp"
namespace circle::motor::interaction {
using namespace hands;
constexpr bool passengerTargeting(unsigned mode,int type,int reason,bool animated,bool local,unsigned /*slot*/){
 // Equipment can change after boarding (observed slot1 -> slot14). It does
 // not transfer ownership of the scripted seat back to ordinary movement.
 return (mode==1||mode==24)&&type==5&&reason==0&&!animated&&!local;
}
constexpr bool ledgeTargeting(unsigned mode,int type,int reason,bool animated,bool local){
 return mode==14&&type==5&&reason==0&&!animated&&!local;
}
inline std::optional<Pose> queryView(const Pose& vr,Vec base,Vec nativeOrigin,bool ownedLedge=false){
 if(!valid(vr)||!finite(base)||!finite(nativeOrigin))return {};
 // Owned ledge queries can still read the native third-person camera. The
 // rendered stereo center is already the complete first-person target ray.
 if(ownedLedge)return vr;
 if(length(nativeOrigin-base)>.5f)return {};
 return Pose{nativeOrigin+(vr.position-base),vr.orientation};
}
// Script EntityGetViewTransform exposes position, angles and three basis axes
// independently. All must describe the same head-directed ray; replacing only
// the focus tracker leaves script-created traces in the flat camera direction.
inline std::optional<std::array<float,12>> scriptViewTransform(uint64_t owner,uint64_t localOwner,
 const std::array<float,12>& native,const Pose& vr,Vec base,bool ledge=false){
 if(!localOwner||owner!=localOwner)return {};
 for(float v:native)if(!std::isfinite(v))return {};
 if(!rotationBasis({native[3],native[4],native[5]},{native[6],native[7],native[8]},
                   {native[9],native[10],native[11]}))return {};
 const auto p=queryView(vr,base,{native[0],native[1],native[2]},ledge);if(!p)return {};
 const auto x=rotate(p->orientation,{1,0,0}),y=rotate(p->orientation,{0,1,0}),z=rotate(p->orientation,{0,0,1});
 return std::array<float,12>{p->position.x,p->position.y,p->position.z,x.x,x.y,x.z,y.x,y.y,y.z,z.x,z.y,z.z};
}
// Use the already compensated stereo cameras. Never apply body yaw, recenter
// or a controller orientation a second time to the native focus query.
inline std::optional<Pose> centerView(const std::array<float,12>& left,const std::array<float,12>& right){
 for(float v:left)if(!std::isfinite(v))return {};
 for(float v:right)if(!std::isfinite(v))return {};
 Vec origin{(left[0]+right[0])*.5f,(left[1]+right[1])*.5f,(left[2]+right[2])*.5f};
 auto l=rotationBasis({left[3],left[4],left[5]},{left[6],left[7],left[8]},{left[9],left[10],left[11]});
 auto r=rotationBasis({right[3],right[4],right[5]},{right[6],right[7],right[8]},{right[9],right[10],right[11]});
 if(!l||!r)return {};
 return Pose{origin,blendRotation(*l,*r,.5f)};
}
// Apply native photo magnification to render rays only, not the OpenXR
// display FOV. Both eyes use one scalar sampled before their render pair.
struct PhotoZoom {
 float baseline{};
 float update(float nativeFov,bool photo){
  if(!std::isfinite(nativeFov)||nativeFov<=5||nativeFov>=170)return 1;
  if(!photo){baseline=nativeFov;return 1;}
  if(baseline<=5)return 1;
  return std::clamp(std::tan(nativeFov*.00872664626f)/std::tan(baseline*.00872664626f),.05f,1.f);
 }
};
inline std::array<float,4> photoRenderFov(std::array<float,4> f,float scale){
 if(!std::isfinite(scale)||scale<.05f||scale>1)return f;
 for(auto& angle:f)angle=std::atan(std::tan(angle)*scale);
 return f;
}
inline bool nearUsable(Vec hand,Vec target,float speed){
 return finite(hand)&&finite(target)&&std::isfinite(speed)&&speed>=0&&speed<1.f&&length(hand-target)<=.25f;
}
// A slow pull has no swipe velocity threshold. Holding the lowered hand keeps
// the native axis down; releasing grip or raising it releases the axis.
struct DownwardPull {
 Vec anchor{};uint64_t tick{};bool anchored{},held{};
 float update(Vec relative,uint64_t now,bool eligible,bool grip){
  if(!eligible||!grip||!finite(relative)||!now){*this={};return 0;}
  if(!anchored||now<tick||now-tick>100){anchor=relative;tick=now;anchored=true;held=false;return 0;}
  tick=now;const auto delta=relative-anchor;
  if(length(delta)>1){*this={};return 0;}
  if(held){if(-delta.y<.05f)held=false;}
  else if(-delta.y>=.12f&&-delta.y>1.2f*std::hypot(delta.x,delta.z))held=true;
  return held?-1.f:0.f;
 }
};
// The native GRIP state, not the squeeze button, owns these gestures. Placing
// an object can enter it on squeeze release; pressing Use again can cancel it.
// Keep the existing left pulse and slow downward hold, plus a symmetric right
// pulse. A return to the anchor rearms horizontal gestures without another Use.
struct ScriptedDirections {
 GripSwipe left{},right{};DownwardPull down{};
 Vec previous{};uint64_t tick{},pulseUntil{};float pulse{};
 std::array<float,2> update(Vec relative,Vec headRight,uint64_t now,bool eligible){
  if(!eligible||!now||!finite(relative)||!finite(headRight)){*this={};return {};}
  if(tick&&(now<tick||now-tick>100||length(relative-previous)>.35f))*this={};
  previous=relative;tick=now;
  if(now<pulseUntil)return {pulse,0};
  const auto y=down.update(relative,now,true,true);
  if(!left.armed&&left.since&&length(relative-left.anchor)<.05f){left={};right={};}
  if(!right.armed&&right.since&&length(relative-right.anchor)<.05f){left={};right={};}
  if(left.update(relative,headRight,now,y==0,true)){pulse=-1;pulseUntil=now+180;right.armed=false;return {-1,0};}
  if(right.update(relative,headRight*-1,now,y==0,true)){pulse=1;pulseUntil=now+180;left.armed=false;return {1,0};}
  return {0,y};
 }
};
}
