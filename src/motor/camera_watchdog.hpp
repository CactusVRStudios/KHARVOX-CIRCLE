#pragma once
#include "locomotion_policy.hpp"
namespace circle::motor::locomotion {
enum class CameraRepair { None, NativeReturn, ReferenceMismatch };
// Watches ownership and the two positional integrators, never head-to-body
// distance alone: a physically held lean is not drift.
struct CameraPositionWatchdog {
 uint64_t owner{},epoch{},last{},mismatchSince{},lastRepair{};
 bool interrupted{};
 CameraRepair update(uint64_t actor,uint64_t generation,uint64_t now,bool observable,
                     bool ordinary,bool nativeInterruption,bool comparable,float referenceError){
  if(!actor||!observable||!now){last=0;mismatchSince=0;return CameraRepair::None;}
  if(owner!=actor||epoch!=generation||now<last){*this={};owner=actor;epoch=generation;}
  if(last&&now-last>100)mismatchSince=0;
  last=now;
  if(nativeInterruption){interrupted=true;mismatchSince=0;return CameraRepair::None;}
  if(!ordinary){mismatchSince=0;return CameraRepair::None;}
  if(interrupted){interrupted=false;mismatchSince=0;lastRepair=now;return CameraRepair::NativeReturn;}
  if(!comparable||!std::isfinite(referenceError)||referenceError<.02f){mismatchSince=0;return CameraRepair::None;}
  if(!mismatchSince)mismatchSince=now;
  if(now-mismatchSince<250||(lastRepair&&now-lastRepair<1000))return CameraRepair::None;
  mismatchSince=0;lastRepair=now;return CameraRepair::ReferenceMismatch;
 }
};
// Transfer body dimensions across an AUTOMATIC seat-heading rebase. An
// explicit recenter/new actor still uses CameraAnchorState's existing reset.
inline void transferCameraCalibration(CameraAnchorState& state,uint64_t owner,uint64_t from,uint64_t to){
 if(!owner||!state.calibrationLocked||state.trustedOwner!=owner||state.trustedEpoch!=from)return;
 state.trustedEpoch=to;
 state.suspend();
}
inline float cameraReferenceError(const RoomscaleFollow& follow,const RoomscaleView& view){
 const auto d=follow.origin+follow.accepted-view.trackingOrigin;
 return std::hypot(d.x,d.z);
}
}
