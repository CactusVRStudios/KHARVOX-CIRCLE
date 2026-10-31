#pragma once
#include "hand_pose.hpp"

namespace circle::motor::hands {
inline bool edibleCategory(unsigned category){return category==1||category==2;}
// HMD-local metres (+Y up, -Z forward). Grip approaches the lower front of
// the headset; no dependence on world yaw, artificial locomotion or eye IPD.
inline float mouthReach(Pose head,Vec grip){
 if(!valid(head)||!finite(grip))return -1;
 const auto local=rotate(inverse(head).orientation,grip-head.position);
 if(local.z>.08f)return 10; // Behind the head cannot count as a bite.
 const auto delta=local-Vec{0,-.10f,-.10f};
 return std::sqrt(delta.x*delta.x/(.15f*.15f)+delta.y*delta.y/(.14f*.14f)+delta.z*delta.z/(.16f*.16f));
}
struct MouthConsume {
 uint64_t owner{},item{},session{},life{},last{},sample{},until{};
 bool armed{};
 bool update(uint64_t actor,uint64_t object,uint64_t xrSession,uint64_t generation,
             uint64_t now,uint64_t poseSample,float reach,bool safe,bool manual){
  if(!safe||!actor||!object||object==UINT64_MAX||!xrSession||!poseSample||
     !std::isfinite(reach)||reach<0||actor!=owner||object!=item||xrSession!=session||
     generation!=life||now<last||(last&&now-last>100)||poseSample<sample){
   *this={};owner=actor;item=object;session=xrSession;life=generation;last=now;sample=poseSample;
   // A newly equipped item away from the face can be approached immediately.
   // Loading/equipping at the mouth still requires a withdrawal first.
   armed=safe&&actor&&object&&object!=UINT64_MAX&&xrSession&&poseSample&&
         std::isfinite(reach)&&reach>=1.55f&&!manual;
   return false;
  }
  last=now;
  if(manual){armed=false;until=0;sample=poseSample;return false;}
  if(now<until){if(reach>1.55f){until=0;return false;}return true;}
  // One new pose entering the mouth volume is sufficient; no dwell timer.
  if(poseSample==sample)return false;
  sample=poseSample;
  if(reach>=1.55f)armed=true;
  else if(reach<=1.f&&armed){armed=false;until=now+600;return true;}
  return false;
 }
};
}
