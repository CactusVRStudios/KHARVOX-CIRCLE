#pragma once
#include "native_muzzle.hpp"
#include "experimental.hpp"
#include "locomotion_runtime.hpp"
#include "locomotion_probe.hpp"
#include "controller_tracking.hpp"
#include "whip_policy.hpp"
#include "hand_pose.hpp"
namespace circle::motor::hands {
void maintainComfortControls();
// Updated by XR sampling even when native animations/cinema own the view.
void publishInteractionTracking(const experimental::EyeRig&);
void resetInteractionTracking();
void publishTargets(const experimental::EyeRig&,const std::array<float,12>&,const locomotion::CameraMotion&);
void applyTrackedArms(uintptr_t rig,uintptr_t context);
  // Match visible arms without replacing the current headset render sample.
  bool appliedViewFrame(const experimental::EyeRig&,const locomotion::CameraMotion&);
void publishRenderAnchor(uint64_t token,const std::array<float,12>& camera,bool matched,const locomotion::CameraMotion& motion);
bool renderModelPlacement(uint64_t owner,uint64_t token,std::array<float,15>& placement,bool carried=false);
void auditUploadedModel(uint64_t owner,uint64_t token,const std::array<float,16>& matrix,unsigned eye);
void observeCombatContext(uint64_t entity,unsigned weaponSlot,bool dead,uintptr_t loadout);
unsigned punchTriggers(const Snapshot&,const locomotion::NativeSample&);
unsigned weaponSlot(uint64_t owner);
void observeEquippedWeaponPose(uint64_t owner,unsigned slot,uintptr_t loadout,int pose);
bool equippedFirearm(uint64_t owner);
bool supportGripInput(const Snapshot&,uint64_t owner,bool allowed);
uint64_t temporaryFirearm(uint64_t owner);
uint64_t combatLifeGeneration();
void observeDigestible(uintptr_t loadout,uintptr_t declaration);
bool equippedFood(uint64_t owner);
bool mouthConsumeInput(const Snapshot&,uint64_t owner,bool allowed,bool manual);
void rememberMuzzleFlash(uint64_t owner,uint64_t weapon,Pose pose,NativeMuzzleSource source);
bool localMuzzleFlash(uint64_t owner,Pose pose);
bool renderMuzzleFlash(uint64_t owner,uint64_t token,std::array<float,15>& placement);
bool firearmShotPose(uint64_t owner,uint64_t weapon,Pose model,Pose localMuzzle,Pose& pose);
std::array<float,2> interactionStick(const Snapshot&,const locomotion::NativeSample&);
void enableWhipInput();
void enablePunchCharge();
void setFirearmMeleeMode(bool enabled);
unsigned consumePunchCharge(uintptr_t loadout,unsigned nativeHand,unsigned stage);
bool consumeOrdinaryItemStrike(uintptr_t loadout,unsigned nativeHand,unsigned state);
bool consumePhysicalStrikeTiming(uintptr_t loadout,unsigned action);
WhipOutput whipInput(const Snapshot&,const locomotion::NativeSample&,uintptr_t loadout=0);
}
