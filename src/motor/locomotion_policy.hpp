#pragma once
// Adapted from KHARVOX-ARGENT RoomscaleFollow.h and BodyYawFollow.h,
// commit de22b367e1a01c5f39fba9f5651724bc2b0a6f29. MIT: argent/LICENSE.
// This policy never writes engine transforms. The native adapter must validate
// ownership, units, basis and the normal-walking state before enabling it.
#include "argent/MovementDirectionPolicy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace circle::motor::locomotion {
struct Vec {float x{},y{},z{};};
inline Vec operator+(Vec a,Vec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec operator-(Vec a,Vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec operator*(Vec a,float b){return {a.x*b,a.y*b,a.z*b};}
inline float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline float length(Vec a){return std::sqrt(dot(a,a));}
inline bool finite(Vec a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline Vec horizontal(Vec a,Vec up){return a-up*dot(a,up);}
inline float wrap(float a){return std::remainder(a,360.f);}
constexpr float radians=.01745329251994329577f;
// ARGENT BodyYawFollow: recenter removes heading only. Capturing pitch/roll
// here would turn a tilted headset into the world's new gravity direction.
inline float trackingYaw(const std::array<float,4>& q){
    return std::atan2(2*(q[3]*q[1]+q[0]*q[2]),1-2*(q[0]*q[0]+q[1]*q[1]))/radians;
}
inline std::array<float,4> uprightReference(const std::array<float,4>& q){
    const float half=trackingYaw(q)*radians*.5f;
    return {0,std::sin(half),0,std::cos(half)};
}
inline Vec rotateWorldYaw(Vec v,float degrees){
    const float c=std::cos(degrees*radians),s=std::sin(degrees*radians);
    return {c*v.x-s*v.y,s*v.x+c*v.y,v.z};
}
inline float nativeTurnStick(float speed,float fullSpeed){
    if(std::abs(speed)<.01f)return 0;
    constexpr float deadzone=8689.f/32767.f;
    return std::copysign(deadzone+(1-deadzone)*std::clamp(std::abs(speed)/fullSpeed,0.f,1.f),speed);
}
struct GroundFrame {
    Vec forward{},right{},up{};float unitsPerMeter{};bool verified{};
    bool valid()const{return verified&&finite(forward)&&finite(right)&&finite(up)&&
        std::isfinite(unitsPerMeter)&&unitsPerMeter>0&&
        std::abs(dot(forward,forward)-1)<.002f&&std::abs(dot(right,right)-1)<.002f&&
        std::abs(dot(up,up)-1)<.002f&&std::abs(dot(forward,right))<.002f&&
        std::abs(dot(up,forward))<.002f&&std::abs(dot(up,right))<.002f;}
    // OpenXR displacement: +X right, +Y up, -Z forward. Reference yaw is
    // horizontal only; native camera pitch/roll never enter this conversion.
    Vec trackingToWorld(Vec p,float referenceYaw)const {
        float c=std::cos(referenceYaw*radians),s=std::sin(referenceYaw*radians);
        return (right*(c*p.x-s*p.z)+forward*(-s*p.x-c*p.z))*unitsPerMeter;
    }
};
enum class State {Unknown,Walking,Running,Crouching,Climbing,Ladder,Swimming,Crawling,Hanging,Scripted,Cinematic,Menu,Paused};
inline bool nativeControlledPosture(unsigned mode){return mode==0||mode==1||mode==2||mode==9;}
// Live ledge-hang observation: mode14, scripted physics5, no local/animated override.
inline bool nativeLedgeHang(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return mode==14&&type==5&&reason==0&&!animated&&!localSpace;
}
// r257 live attached whip: ordinary stance with whip physics7, no authored camera.
// Keep the pre-attachment heading and gravity; do not enable ground motion/IK.
inline bool nativeWhipAttached(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return nativeControlledPosture(mode)&&type==7&&reason==0&&!animated&&!localSpace;
}
// Live rope/whip climb: mode13, whip physics7, native animation owns limbs.
inline bool nativeWhipClimb(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return mode==13&&type==7&&reason==0&&!animated&&!localSpace;
}
// Measured gap swing: mode12, same whip physics7 and native limb ownership.
inline bool nativeWhipSwing(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return mode==12&&type==7&&reason==0&&!animated&&!localSpace;
}
// Only as a bounded continuation of an already admitted swing: physics
// can hand off before the controller leaves mode12 on the following tick.
inline bool whipSwingRelease(unsigned mode,int type){return mode==12&&(type==0||type==5);}
// One aligned frame during the native blend is not a completed handoff.
// Only main-camera evaluations advance this timer; visibility queries do not.
struct TraversalHandoff {
 uint64_t since{},last{};bool tracking{};
 void reset(){since=last=0;tracking=false;}
 bool update(uint64_t now,bool ready){
  if(!ready){reset();return false;}
  if(!tracking||now<last||now-last>=100){since=now;tracking=true;}
  last=now;return now-since>=250;
 }
};
// Native physics-state events own this episode, not render timing. A paused
// or slow scene may remain in its interactive continuation indefinitely.
struct WhipTraversalEpisode {
 uint64_t owner{};bool armed{};
 bool update(uint64_t actor,bool valid,unsigned mode,int type,int reason,bool animated,bool local){
  if(actor!=owner){armed=false;owner=actor;}
  if(!valid||!actor||reason||animated||local){armed=false;return false;}
  if(nativeLedgeHang(mode,type,reason,animated,local)||
     nativeWhipClimb(mode,type,reason,animated,local)||nativeWhipSwing(mode,type,reason,animated,local)){
   armed=true;return false;
  }
  if(armed&&mode==0&&type==5)return true;
  // Native pull-up may precede the same script continuation.
  if(armed&&mode==3&&type==5)return false;
  armed=false;return false;
 }
};
inline bool traversalCameraMatches(Vec rendered,Vec fps){
 return finite(rendered)&&finite(fps)&&std::abs(rendered.x-fps.x)<.05f&&
        std::abs(rendered.y-fps.y)<.05f&&std::abs(rendered.z-fps.z)<.05f;
}
// Live ladder: mode4, scripted physics5, no animated/local override.
// Recorded rope slide: mode16/type5, no scripted camera override.
inline bool nativeRopeSlide(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return mode==16&&type==5&&reason==0&&!animated&&!localSpace;
}
inline bool nativeLadder(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return mode==4&&type==5&&reason==0&&!animated&&!localSpace;
}
// Live chain pulling uses GRIP10 with scripted physics5 and an inactive,
// continuously updated FPS camera. GRIP28 has not been verified for this view.
inline bool nativeGripCamera(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return mode==10&&type==5&&reason==0&&!animated&&!localSpace;
}
inline Vec ledgeBlendAnchor(Vec body,Vec localHeadOffset,float yaw){
 return body+rotateWorldYaw(localHeadOffset,yaw);
}
inline bool ledgeEyePosition(Vec eye,Vec body){
 const auto d=eye-body;
 return finite(eye)&&finite(body)&&std::hypot(d.x,d.y)<.6f&&d.z>.5f&&d.z<2.2f;
}
inline bool waterMode(unsigned mode){return mode==7||mode==8||mode==20;}
inline bool nativeWaterState(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return waterMode(mode)&&type==0&&reason==0&&!animated&&!localSpace;
}
// Motor consumes forward/right vectors in its per-step movement parameters.
// Use HMD pitch for forward swimming; roll must never tilt strafing/gravity.
struct WaterBasis {Vec forward{},right{};bool valid{};};
inline WaterBasis waterBasis(const std::array<float,4>& q,float bodyYaw,float referenceYaw){
 float norm{};for(float v:q){if(!std::isfinite(v))return {};norm+=v*v;}
 if(std::abs(norm-1)>.01f||!std::isfinite(bodyYaw)||!std::isfinite(referenceYaw))return {};
 const Vec xr{-2*(q[0]*q[2]+q[3]*q[1]),2*(q[3]*q[0]-q[1]*q[2]),-(1-2*(q[0]*q[0]+q[1]*q[1]))};
 const float b=bodyYaw*radians;
 const GroundFrame frame{{std::cos(b),std::sin(b),0},{std::sin(b),-std::cos(b),0},{0,0,1},1,true};
 Vec forward=frame.trackingToWorld(xr,referenceYaw);forward.z=xr.y;
 const float heading=(bodyYaw+trackingYaw(q)-referenceYaw)*radians;
 Vec right{std::sin(heading),-std::cos(heading),0};
 const float horizontalLength=std::hypot(forward.x,forward.y);
 if(horizontalLength>.001f)right={forward.y/horizontalLength,-forward.x/horizontalLength,0};
 return {forward,right,finite(forward)&&std::abs(length(forward)-1)<.01f};
}
// Verified movementMode_t records at 300D710/300D758: GRIP_INSPECT and
// GRIP_INSPECT_CROUCHED. Live book dialog: mode26, type0, reason0, no animation.
inline bool nativeObjectInspection(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return (mode==23||mode==26)&&type==0&&reason==0&&!animated&&!localSpace;
}
// Observed in the current grip interaction: GRIP=10, physics movement type=5.
// Native interaction owns translation; controller axes are interaction input.
inline bool nativeGripInteraction(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return (mode==10||mode==28)&&type==5&&reason==0&&!animated&&!localSpace;
}
// A jump removes ground contact, not ownership of the VR view or hands.
inline bool trackedGameplayState(unsigned mode,int movementType,int reason,bool animated,bool localSpace){
 return nativeControlledPosture(mode)&&movementType==0&&reason==0&&!animated&&!localSpace;
}
// Repeated low-ledge pull-ups in r228: mode3/type5, immersive FPS camera,
// no explicit/animated/local camera override. Preserve the existing headset
// heading reference through this state, without enabling locomotion or IK.
inline bool nativeLowLedgeTraversal(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return mode==3&&type==5&&reason==0&&!animated&&!localSpace;
}
// r235 live lateral dodges: mode0/type5 for ~0.5s, no authored camera.
// Admit headset orientation only; native dodge translation remains untouched.
inline bool nativeSideDodge(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return mode==0&&type==5&&reason==0&&!animated&&!localSpace;
}
// Native acceptance starts the episode. action already returns to IDLE while
// its animation node still runs; keep the matching handle through that tail.
struct CombatCameraEpisode {
 uint64_t owner{},start{},lastMotion{},sample{};unsigned handle{};bool armed{};
 void observe(uint64_t who,unsigned action,unsigned id,int node,bool accepted,uint64_t now){
  if(now<sample)return; // worker delivery order is not a clock reset
  if(owner!=who){*this={};owner=who;}
  sample=now;
  if(accepted&&(action==5||action==17)){
   handle=id;start=lastMotion=now;armed=true;
  }else if(armed&&id==handle&&(action==5||action==17||(action==0&&node>=0))&&now>=start&&now-start<10000)
   lastMotion=now;
  else if(armed&&id!=handle)armed=false;
 }
 bool active(uint64_t who,uint64_t now)const {
  return armed&&owner==who&&now>=sample&&now-sample<250&&now>=lastMotion&&now-lastMotion<1000&&now>=start&&now-start<11000;
 }
};
inline bool cameraReferenceChanged(bool valid,uint64_t previousOwner,uint64_t owner){
 // A lunge/teleport changes position, not the player's neutral neck dimensions
 // or the headset heading. Position discontinuities reset local feedback only.
 return !valid||previousOwner!=owner;
}
inline bool nativeCombatCamera(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return nativeControlledPosture(mode)&&(type==0||type==5)&&reason==0&&!animated&&!localSpace;
}
// Measured unarmed passenger seat: crouched posture with scripted physics.
inline bool nativeScriptedSeat(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return mode==1&&type==5&&!reason&&!animated&&!localSpace;
}
// Weapon pose/shot ownership is independent of permission to walk. The
// measured passenger firefight is mode1/type5 with an equipped pickup SMG.
inline bool trackedFirearmState(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return nativeScriptedSeat(mode,type,reason,animated,localSpace)||
  ((!type&&!reason&&!animated&&!localSpace)&&(nativeControlledPosture(mode)||mode==20));
}
inline bool nativeBoatDriver(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return mode==24&&type==5&&!reason&&!animated&&!localSpace;
}
// One alignment per passenger episode. Menu/tracking gaps must not undo a
// deliberate stick turn; a fresh non-seat state or a new actor starts anew.
struct PassengerEntry {
 uintptr_t owner{};
 bool update(uintptr_t actor,bool freshGameplay,bool passenger,bool trackingReady){
  if(!freshGameplay||!actor)return false;
  if(!passenger){owner=0;return false;}
  if(!trackingReady||owner==actor)return false;
  owner=actor;return true;
 }
};
// Only a seat episode that actually equips a firearm gets combat-body
// handling. Holstering or changing items must not disable tracked hands.
struct PassengerCombatEpisode {
 uint64_t owner{},life{};bool active{};
 bool update(uint64_t actor,uint64_t generation,bool freshGameplay,bool seated,bool firearm){
  if(owner!=actor||life!=generation){owner=actor;life=generation;active=false;}
  if(!actor)return false;
  if(freshGameplay){if(!seated)active=false;else if(firearm)active=true;}
  return active&&seated;
 }
};
inline bool headsetViewState(unsigned mode,int type,int reason,bool animated,bool localSpace){
 return nativeScriptedSeat(mode,type,reason,animated,localSpace)||trackedGameplayState(mode,type,reason,animated,localSpace)||
  nativeWhipAttached(mode,type,reason,animated,localSpace)||
  nativeLowLedgeTraversal(mode,type,reason,animated,localSpace)||
  nativeSideDodge(mode,type,reason,animated,localSpace)||
  nativeGripInteraction(mode,type,reason,animated,localSpace)||
  nativeWaterState(mode,type,reason,animated,localSpace)||
  nativeLedgeHang(mode,type,reason,animated,localSpace)||
  nativeWhipClimb(mode,type,reason,animated,localSpace)||
  nativeWhipSwing(mode,type,reason,animated,localSpace)||
  nativeLadder(mode,type,reason,animated,localSpace)||
  nativeRopeSlide(mode,type,reason,animated,localSpace);
}
inline void levelGameplayCamera(std::array<float,12>& camera,float nativeYaw){
 const float c=std::cos(nativeYaw*radians),s=std::sin(nativeYaw*radians);
 camera[3]=c;camera[4]=s;camera[5]=0;
 camera[6]=-s;camera[7]=c;camera[8]=0;
 camera[9]=0;camera[10]=0;camera[11]=1;
}
inline bool nativeCameraPosture(unsigned mode,float horizontalOffset,float height){
 return nativeControlledPosture(mode)&&std::isfinite(horizontalOffset)&&std::isfinite(height)&&horizontalOffset<.3f&&height>(mode==2?.1f:mode==1?.35f:1.f)&&height<2.2f;
}
// The collision body and rendered body can diverge after vehicle exit.
// Accept a visual origin only when the native eye is already above it.
inline bool coherentRenderBody(unsigned mode,Vec camera,Vec physics,Vec body){
 const auto eye=camera-body,delta=body-physics;
 return finite(camera)&&finite(physics)&&finite(body)&&std::hypot(delta.x,delta.y)>.3f&&length(delta)<2.f&&
  std::abs(delta.z)<.25f&&nativeCameraPosture(mode,std::hypot(eye.x,eye.y),eye.z);
}
// A fast scripted passenger vehicle updates rendered body/camera ahead of the
// last physics sample. Use their common frame, including small displacements;
// do not mistake this translation for a camera-joint excursion.
inline bool coherentPassengerBody(Vec camera,Vec physics,Vec body){
 const auto eye=camera-body;
 return finite(camera)&&finite(physics)&&finite(body)&&length(body-physics)<2.f&&
  nativeCameraPosture(1,std::hypot(eye.x,eye.y),eye.z);
}
// ARGENT anchors gameplay translation to accepted physics rather than to the
// animated camera joint. Keep the rendered horizontal offset in world space;
// body follow must not orbit the camera around the physics origin. Retain a
// separate body-local offset only for settled-camera calibration/recovery.
// Optional stance height stabilization removes vertical joint animation while
// physics still owns jumps/steps and native stance transitions remain intact.
struct CameraAnchor {
    bool active{}; Vec localOffset{};
    bool stableHeight{};float eyeHeight{};
    float visualHeightOffset{};
    float stairHeightOffset{}; // render-only translation, not an IK reach change
    Vec worldOffset{};
    Vec trackingOffset{}; // render-only roomscale/body-follow cancellation
    Vec place(Vec camera,Vec physics,float bodyYaw)const {
        if(!active||!finite(camera)||!finite(physics)||!std::isfinite(bodyYaw))return camera;
        const Vec offset=worldOffset+trackingOffset;
        return {physics.x+offset.x,physics.y+offset.y,(stableHeight?physics.z+eyeHeight:camera.z)+visualHeightOffset};
    }
    Vec placeForSkeleton(Vec camera,Vec physics,float bodyYaw)const {
        auto anchor=*this;
        anchor.visualHeightOffset-=stairHeightOffset;
        anchor.trackingOffset={};
        return anchor.place(camera,physics,bodyYaw);
    }
};
// Smooth collision-resolved stair Z only while grounded. Never smooth jumps,
// stance changes, tracking recovery, actor changes or large discontinuities.
struct StairHeight {
 uint64_t owner{},epoch{},tick{};unsigned mode{255};Vec previous{};float height{};
 float update(uint64_t who,uint64_t generation,Vec physics,unsigned posture,bool grounded,bool eligible,uint64_t now){
  if(!eligible||!grounded||!finite(physics)||!who||!now){*this={};return 0;}
  const bool reset=!tick||owner!=who||epoch!=generation||mode!=posture||now<tick||now-tick>100||length(physics-previous)>.5f;
  if(reset)height=physics.z;
  else if(now>tick){
   const float alpha=1-std::exp(-float(now-tick)/70.f);
   height+=alpha*(physics.z-height);
   height=std::clamp(height,physics.z-.22f,physics.z+.22f);
  }
  owner=who;epoch=generation;mode=posture;tick=now;previous=physics;
  return height-physics.z;
 }
};
// UNARMED_COMBAT (9) changes the action, not the standing neck dimensions.
constexpr unsigned cameraAnchorPosture(unsigned mode){return mode==9?0u:mode;}
struct CameraAnchorState {
    CameraAnchor anchor{};uintptr_t owner{};uint64_t epoch{};Vec previousPhysics{};
    unsigned posture{255};uint64_t postureSince{};
    Vec settledOffset{},settledPhysics{};uint64_t settledSince{};
    unsigned repairs{};
    Vec trustedOffset{};uintptr_t trustedOwner{};uint64_t trustedEpoch{},trustedTick{};
    Vec trustedWorldOffset{};float trustedHeight{};unsigned trustedPosture{255};
    std::array<float,10> postureHeights{};
    std::array<bool,10> postureKnown{};
    bool calibrationLocked{};
    // Retain a settled reference through bounded native joint excursions.
    // Never bootstrap from a detached camera or restore another actor/posture.
    // Known dimensions survive same-owner displacement; local feedback rebases.
    bool retains(uintptr_t who,uint64_t generation,Vec camera,Vec physics,unsigned mode)const {
        mode=cameraAnchorPosture(mode);
        const Vec d=camera-physics;
        const bool current=anchor.active&&anchor.stableHeight&&owner==who&&epoch==generation&&posture==mode&&length(physics-previousPhysics)<=.5f;
        const bool stored=calibrationLocked&&trustedOwner==who&&trustedEpoch==generation&&mode<postureKnown.size()&&postureKnown[mode];
        return (current||stored)&&
            nativeControlledPosture(mode)&&finite(camera)&&finite(physics)&&
            std::hypot(d.x,d.y)<.6f&&d.z>.1f&&d.z<2.2f;
    }
    void suspend(){anchor={};owner=0;epoch=0;posture=255;postureSince=settledSince=0;}
    CameraAnchor update(uintptr_t who,uint64_t generation,Vec camera,Vec physics,float bodyYaw,bool allowed,
                        bool stabilizeHeight=false,unsigned mode=255,uint64_t now=0,bool canCalibrate=true){
        mode=cameraAnchorPosture(mode);
        // Body dimensions survive traversal duration. Only an actual identity /
        // reference-generation change (or clock reset) invalidates calibration.
        if(trustedOwner&&((who&&trustedOwner!=who)||trustedEpoch!=generation||(now&&now<trustedTick))){
            trustedOwner=0;trustedTick=0;postureKnown={};calibrationLocked=false;
            suspend();
        }
        const Vec offset=camera-physics;
        const bool retained=retains(who,generation,camera,physics,mode);
        if(!allowed||!who||!finite(camera)||!finite(physics)||!std::isfinite(bodyYaw)||
           (std::hypot(offset.x,offset.y)>.3f&&!retained)){suspend();return {};}
        if(!anchor.active||owner!=who||epoch!=generation||length(physics-previousPhysics)>.5f){
            const bool restore=now&&trustedTick&&now>=trustedTick&&
                trustedOwner==who&&trustedEpoch==generation;
            anchor={true,restore?trustedOffset:rotateWorldYaw({offset.x,offset.y,0},-bodyYaw)};owner=who;epoch=generation;
            anchor.worldOffset=restore?trustedWorldOffset:rotateWorldYaw(anchor.localOffset,bodyYaw);
            calibrationLocked=restore;
            posture=255;postureSince=now;settledSince=0;
            if(restore&&stabilizeHeight&&nativeControlledPosture(mode)&&postureKnown[mode]){
                anchor.eyeHeight=postureHeights[mode];anchor.stableHeight=true;posture=mode;
            }
        }
        if(retained&&std::hypot(offset.x,offset.y)>.3f){
            previousPhysics=physics;settledSince=0;
            return anchor; // Never learn a new neck/height offset from this animation.
        }
        // Vault/blend frames can seed a plausible (<30cm) but wrong neck offset.
        // Only recalibrate against a stationary, settled native ego camera.
        // Compare body-local offsets so turning alone cannot corrupt the anchor.
        const Vec local=rotateWorldYaw(offset,-bodyYaw);
        if(!canCalibrate){settledSince=0;postureSince=now;}
        else if(!now||!nativeControlledPosture(mode)||!settledSince||now<settledSince||
           length(physics-settledPhysics)>.005f||length(local-settledOffset)>.02f||posture!=mode){
            settledOffset=local;settledPhysics=physics;settledSince=now;
        }else if(!calibrationLocked&&now-settledSince>=750){
            const bool horizontalError=std::hypot(local.x-anchor.localOffset.x,local.y-anchor.localOffset.y)>.08f;
            const bool heightError=anchor.stableHeight&&std::abs(local.z-anchor.eyeHeight)>.12f;
            if(horizontalError||heightError){
                anchor.localOffset={local.x,local.y,0};
                if(horizontalError)anchor.worldOffset=rotateWorldYaw(anchor.localOffset,bodyYaw);
                if(heightError)anchor.eyeHeight=local.z;
                ++repairs;
            }
        }
        // Preserve native stance transitions; once settled, ignore animation
        // height and follow current collision-resolved body Z, including jumps.
        if(!stabilizeHeight||!nativeControlledPosture(mode)||offset.z<=.1f||offset.z>=2.2f){
            anchor.stableHeight=false;posture=255;
        }else{
            if(posture!=mode||now<postureSince){posture=mode;postureSince=now;anchor.stableHeight=false;}
            if(!anchor.stableHeight&&canCalibrate&&now-postureSince>=500){
                anchor.eyeHeight=postureKnown[mode]?postureHeights[mode]:offset.z;anchor.stableHeight=true;
            }
        }
        // Settling can establish a baseline, but cannot prove that a later
        // stationary pose is neutral: charged attacks also hold still. Lock
        // the verified reference until its owner/generation changes.
        if(canCalibrate&&now&&settledSince&&now>=settledSince&&now-settledSince>=750&&nativeControlledPosture(mode)){
            calibrationLocked=true;
            trustedOffset=anchor.localOffset;trustedWorldOffset=anchor.worldOffset;
            trustedOwner=who;trustedEpoch=generation;trustedTick=now;
            trustedPosture=anchor.stableHeight?mode:255;trustedHeight=anchor.eyeHeight;
            if(anchor.stableHeight&&!postureKnown[mode]){postureHeights[mode]=anchor.eyeHeight;postureKnown[mode]=true;}
        }else if(calibrationLocked&&anchor.active&&trustedOwner==who&&trustedEpoch==generation)
            trustedTick=now;
        previousPhysics=physics;return anchor;
    }
};
struct Settings {
    bool roomscale=true,collision=true,bodyFollow=true,debug=false;
    float yawDeadzone=25,followSpeed=120,maxResidual=70,snapAngle=45,smoothSpeed=180;
    bool snap=false; kharvox::MovementDirectionMode direction=kharvox::MovementDirectionMode::Head;
    float roomscaleStart=.015f,roomscaleStop=.006f;
};
// Keep the reference in tracking space. Integrating world-space head deltas
// across turns or feeding a collision clamp back into that reference leaves a
// permanent residual even when the headset returns to its original position.
// Only collision-resolved, request-bounded roomscale acknowledgements advance
// the reference. A turn, push, door animation or cutscene is not an acknowledgement.
struct RoomscaleView {
 uintptr_t owner{};uint64_t epoch{},tick{};
 Vec previousHead{},previousPhysics{},position{},trackingOrigin{},rawOffset{};
 Vec update(uintptr_t who,uint64_t generation,Vec head,Vec physics,const GroundFrame& frame,
            float referenceYaw,bool eligible,uint64_t now,Vec acknowledgedTracking={}){
  if(!eligible||!who||!now||!finite(head)||!finite(physics)||!frame.valid()||!std::isfinite(referenceYaw)){
   *this={};return {};
  }
  if(!tick||owner!=who||epoch!=generation||now<tick||now-tick>100||
     length(physics-previousPhysics)>.5f||length(head-previousHead)>.5f){
   *this={};owner=who;epoch=generation;tick=now;previousHead=trackingOrigin=head;previousPhysics=position=physics;
   return {};
  }
  if(finite(acknowledgedTracking))trackingOrigin=trackingOrigin+acknowledgedTracking;
  rawOffset=frame.trackingToWorld(head-trackingOrigin,referenceYaw);
  Vec offset=rawOffset;
  constexpr float maxSeparation=.20f;
  const float distance=length(offset);
  if(distance>maxSeparation)offset=offset*(maxSeparation/distance);
  position=physics+offset;
  previousHead=head;previousPhysics=physics;tick=now;
  return offset;
 }
};
// Motor's joyActive bit is not a reliable source tag after its command merge.
// A recently submitted virtual stick can reappear here as a native command.
inline bool nativeMovementIsManual(bool moving,uint64_t now,uint64_t injectedAt){
    return moving&&(!injectedAt||now<injectedAt||now-injectedAt>150);
}
struct RoomscaleFollow {
    Vec origin{},accepted{},previousPhysics{},worldDirection{},trackingDirection{},acceptedStep{};
    uintptr_t owner{};bool valid{},commanded{},following{};float requestedMeters{};
    void reset(uintptr_t who,Vec physics,Vec head){*this={};owner=who;origin=head;previousPhysics=physics;valid=true;}
    void useViewReference(const RoomscaleView& view,uintptr_t who,uint64_t generation){
        // Manual input rebases command feedback, but must not discard the
        // displacement still displayed by the camera. Both paths consume the
        // same tracking origin when physical body follow resumes.
        if(view.tick&&view.owner==who&&view.epoch==generation&&finite(view.trackingOrigin)){
            origin=view.trackingOrigin;accepted={};
        }
    }
    kharvox::MovementStick2D update(uintptr_t who,Vec physics,Vec head,const GroundFrame& frame,
        float referenceYaw,Vec bodyForward,Vec bodyRight,bool manual,bool enabled,float startThreshold=.015f,float stopThreshold=.006f,bool previousRequestSubmitted=true){
        acceptedStep={};
        if(!enabled||!who||!finite(head)||!finite(physics)||!frame.valid()||!std::isfinite(referenceYaw)){
            valid=false;commanded=false;following=false;return {};
        }
        if(!valid||owner!=who||length(physics-previousPhysics)>frame.unitsPerMeter*.5f){reset(who,physics,head);return {};}
        Vec residual=head-origin-accepted;residual.y=0;
        // Native/manual movement must not accumulate a physical command backlog.
        if(manual){reset(who,physics,head);return {};}
        if(commanded&&previousRequestSubmitted){
            float moved=dot(horizontal(physics-previousPhysics,frame.up),worldDirection)/frame.unitsPerMeter;
            // Accept only the outstanding request, never a native teleport or push.
            if(moved>0&&moved<.12f){float along=dot(residual,trackingDirection);
                acceptedStep=trackingDirection*std::min({moved,requestedMeters,std::max(0.f,along)});
                accepted=accepted+acceptedStep;}
        }
        previousPhysics=physics;commanded=false;
        residual=head-origin-accepted;residual.y=0;const float distance=length(residual);
        if(distance>1.f){reset(who,physics,head);return {};}
        if(!std::isfinite(startThreshold)||!std::isfinite(stopThreshold)||stopThreshold<0||startThreshold<=stopThreshold){following=false;return {};}
        if(following){if(distance<=stopThreshold)following=false;}
        else if(distance>=startThreshold)following=true;
        if(!following)return {};
        trackingDirection=residual*(1/distance);
        worldDirection=frame.trackingToWorld(trackingDirection,referenceYaw)*(1/frame.unitsPerMeter);
        requestedMeters=std::max(0.f,distance-stopThreshold);commanded=true;
        constexpr float deadzone=7849.f/32767.f;
        float magnitude=std::clamp(deadzone+std::min(requestedMeters*20.f,5.5f)/5.5f*(1-deadzone),deadzone+.01f,1.f);
        return {dot(worldDirection,bodyRight)*magnitude,dot(worldDirection,bodyForward)*magnitude};
    }
};
struct BodyYawFollow {
    float nativePrevious{},acceptedPhysicalYaw{},artificialYaw{},command{},sign{},smoothVelocity{};
    bool valid{},snapArmed=true;uint64_t previousTick{};
    void reset(float nativeYaw){*this={};nativePrevious=nativeYaw;valid=true;}
    void suspend(bool preserveSnap=false){command=0;smoothVelocity=0;previousTick=0;if(!preserveSnap)snapArmed=true;}
    // Adapter supplies a measured native turn direction, never a donor guess.
    void observe(float nativeYaw){
        if(!std::isfinite(nativeYaw)){valid=false;command=0;return;}
        if(valid&&std::abs(command)>.001f){float delta=wrap(nativeYaw-nativePrevious);
            if(std::abs(delta)<30){acceptedPhysicalYaw=wrap(acceptedPhysicalYaw+delta);
                if(std::abs(delta)>.01f)sign=delta*command>=0?1.f:-1.f;}}
        nativePrevious=nativeYaw;valid=true;
    }
    void turn(float stick,uint64_t tick,const Settings& settings,bool active){
        turnMicros(stick,tick*1000,settings,active);
    }
    void turnMicros(float stick,uint64_t tick,const Settings& settings,bool active){
        uint64_t dt=previousTick&&tick>=previousTick?tick-previousTick:0;previousTick=tick;smoothVelocity=0;
        if(!active||!std::isfinite(stick)){snapArmed=true;return;}
        if(std::abs(stick)<.25f)snapArmed=true;
        float change=0;
        if(settings.snap){if(snapArmed&&std::abs(stick)>.7f){change=-std::copysign(settings.snapAngle,stick);snapArmed=false;}}
        else if(dt<=250000&&std::abs(stick)>.15f){
            smoothVelocity=-std::copysign((std::min(std::abs(stick),1.f)-.15f)/.85f,stick)*settings.smoothSpeed;
            change=smoothVelocity*float(dt)*.000001f;
        }
        artificialYaw=wrap(artificialYaw+change);
    }
    float request(float physicalYaw,const Settings& settings,bool manual,bool active){
        command=0;if(!active||!valid||manual||!settings.bodyFollow||std::abs(sign)!=1||!std::isfinite(physicalYaw))return 0;
        float residual=wrap(physicalYaw+artificialYaw-acceptedPhysicalYaw);
        float excess=std::abs(residual)-settings.yawDeadzone;
        // Adapter must map this requested angular velocity into the measured
        // native turn response. Returning degrees/sec avoids guessed sensitivity.
        // Artificial yaw must not outrun the body at the physical follow cap.
        // Feed forward smooth turn; only the residual uses the physical limit.
        const float correction=excess>0?std::copysign(std::min(settings.followSpeed,excess*4.f),residual):0;
        command=(smoothVelocity+correction)/sign;
        return command;
    }
};
inline bool mayFollow(State state,bool tracked,bool owner,bool calibrated,bool collision){
    return state==State::Walking&&tracked&&owner&&calibrated&&collision;
}
}
