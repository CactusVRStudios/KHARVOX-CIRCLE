#pragma once
#include "hand_policy.hpp"
#include <span>
#include <vector>

namespace circle::motor::hands {
struct Quat {float x{},y{},z{},w{1};};
inline float norm2(Quat q){return q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;}
inline bool valid(Quat q){return std::isfinite(norm2(q))&&std::abs(norm2(q)-1)<.01f;}
inline Quat multiply(Quat a,Quat b){return {
 a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
 a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
 a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
 a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
inline Vec rotate(Quat q,Vec v){Vec xyz{q.x,q.y,q.z};auto t=cross(xyz,v)*2;return v+t*q.w+cross(xyz,t);}
struct Pose {Vec position{};Quat orientation{};};
inline bool valid(Pose p){return finite(p.position)&&valid(p.orientation);}
inline Pose compose(Pose a,Pose b){return {a.position+rotate(a.orientation,b.position),multiply(a.orientation,b.orientation)};}
inline Pose inverse(Pose p){Quat q{-p.orientation.x,-p.orientation.y,-p.orientation.z,p.orientation.w};return {rotate(q,p.position*-1),q};}
inline Quat normalized(Quat q){float n=std::sqrt(norm2(q));return n>1e-6f?Quat{q.x/n,q.y/n,q.z/n,q.w/n}:Quat{};}
inline Quat between(Vec from,Vec to){
 from=unit(from);to=unit(to);float d=std::clamp(dot(from,to),-1.f,1.f);
 if(d<-.9999f){Vec axis=unit(cross(from,std::abs(from.z)<.9f?Vec{0,0,1}:Vec{0,1,0}));return {axis.x,axis.y,axis.z,0};}
 auto c=cross(from,to);return normalized({c.x,c.y,c.z,1+d});
}
inline Quat blendRotation(Quat a,Quat b,float weight){
 if(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w<0)b={-b.x,-b.y,-b.z,-b.w};
 return normalized({a.x+(b.x-a.x)*weight,a.y+(b.y-a.y)*weight,a.z+(b.z-a.z)*weight,a.w+(b.w-a.w)*weight});
}
// ARGENT's adaptive tracking-space filter, slightly stronger here: 20 ms at
// rest and 5 ms in fast motion. Raw velocity remains available to gestures.
struct TrackingSmoothing {
 Pose filtered{},previous{};uint64_t tick{},epoch{};bool held{};
 void update(Pose& grip,Pose& aim,uint64_t now,uint64_t reference,bool tracked,float restSeconds=.020f){
  if(!tracked||!valid(grip)||!valid(aim)){held=false;return;}
  const Pose raw=grip;const float dt=now>tick?float(now-tick)*1e-9f:0;
  // The native renderer can consume one XR sample more than once. Reuse the
  // exact filtered result, rather than alternating filtered/raw on dt==0.
  if(held&&epoch==reference&&now==tick){aim=compose(compose(filtered,inverse(raw)),aim);grip=filtered;return;}
  const float travel=length(raw.position-previous.position);
  const auto q=normalized(raw.orientation),old=normalized(filtered.orientation);
  const float cosine=old.x*q.x+old.y*q.y+old.z*q.z+old.w*q.w;
  if(restSeconds<=0||!held||epoch!=reference||dt<=0||dt>.1f||travel>.35f||std::abs(cosine)<.7071067f)filtered=raw;
  else {
   const auto pq=normalized(previous.orientation);
   const float angular=2*std::acos(std::clamp(std::abs(pq.x*q.x+pq.y*q.y+pq.z*q.z+pq.w*q.w),0.f,1.f))/dt;
   const float activity=std::clamp(std::max(travel/dt/1.5f,angular/6.f),0.f,1.f);
   const float alpha=1-std::exp(-dt/(restSeconds*(1-.75f*activity)));
   filtered.position=filtered.position+(raw.position-filtered.position)*alpha;
   filtered.orientation=blendRotation(old,q,alpha);
  }
  previous=raw;tick=now;epoch=reference;held=true;
  aim=compose(compose(filtered,inverse(raw)),aim);grip=filtered;
 }
};
// Filter only motion relative to the head. A rigid head/hand turn or translation
// must not become hand lag against the current, unfiltered headset pose.
struct HeadRelativeSmoothing {
 TrackingSmoothing filter;
 void update(Pose& grip,Pose& aim,Pose head,uint64_t time,uint64_t epoch,bool tracked,float seconds){
  if(!valid(head)){filter.held=false;return;}
  auto localGrip=compose(inverse(head),grip),localAim=compose(inverse(head),aim);
  filter.update(localGrip,localAim,time,epoch,tracked,seconds);
  grip=compose(head,localGrip);aim=compose(head,localAim);
 }
};
struct HandRenderStamp {
 uint64_t owner{},simulation{},epoch{},tick{};
 bool matches(uint64_t who,uint64_t sim,uint64_t generation,uint64_t now)const{
  return owner&&owner==who&&simulation==sim&&epoch==generation&&now>=tick&&now-tick<100;
 }
};
// Targets and the final rig are sampled at different pipeline stages. Carry
// the target with the player's intervening translation, never compare moving
// world origins as though they belonged to one instant.
inline Vec advanceTarget(Vec target,Vec sampledPhysics,Vec rigOrigin){return target+rigOrigin-sampledPhysics;}
// Controller targets are camera-relative. Physics-only advancement leaves the
// previous frame's animated camera offset in the hand pose (visible gait bob).
inline Vec rebaseHandTarget(Vec target,Vec sampledCamera,Vec currentCamera){return currentCamera+(target-sampledCamera);}
inline Pose turnAroundView(Pose pose,Vec view,Quat turn){
 return {view+rotate(turn,pose.position-view),normalized(multiply(turn,pose.orientation))};
}
// A solved skeleton is expressed in its IK model frame, not in a later native
// interpolated model frame. Translate that exact frame with the render camera.
inline Pose renderHandModel(Pose solvedModel,Vec solvedCamera,Vec renderCamera,float yawDelta=0){
 const float half=yawDelta*.00872664626f;
 const Quat turn{0,0,std::sin(half),std::cos(half)};
 solvedModel.position=renderCamera+rotate(turn,solvedModel.position-solvedCamera);
 solvedModel.orientation=normalized(multiply(turn,solvedModel.orientation));
 return solvedModel;
}
// Native equipment already includes controller IK and its own joint animation.
// Apply only the skeleton's render-frame delta; a stored camera-relative root
// would cancel hand movement and detach the weapon from the solved wrist.
inline Pose renderHandAttachment(Pose solvedModel,Pose renderModel,Pose nativeAttachment){
 return compose(renderModel,compose(inverse(solvedModel),nativeAttachment));
}
// This is a model-origin envelope, not an arm-reach limit. Long held props
// can place their authored origin beyond the wrist: the live Khmer bolt
// crosses the old 2 m gate (1.75--2.54 m) as the wrist turns. Use the same
// bounded envelope at registration/revalidation and render placement. Call
// only after first-person/equipped ownership checks; never for world models.
inline bool carriedModelOriginInRange(Vec delta){
 return finite(delta)&&dot(delta,delta)<=16.f;
}
// Native placement stores signed scale separately from its proper rotation.
// Mirrored carried props still need the same rigid render delta as their hands.
inline bool handPlacementScale(Vec scale,bool carried){
 if(!finite(scale))return false;
 if(carried)scale={std::abs(scale.x),std::abs(scale.y),std::abs(scale.z)};
 return std::abs(scale.x-1)<=.001f&&std::abs(scale.y-1)<=.001f&&std::abs(scale.z-1)<=.001f;
}
// Columns map unit axes to the destination. Only proper orthonormal bases.
inline std::optional<Quat> rotationBasis(Vec x,Vec y,Vec z){
 if(!finite(x)||!finite(y)||!finite(z)||std::abs(dot(x,x)-1)>.002f||std::abs(dot(y,y)-1)>.002f||std::abs(dot(z,z)-1)>.002f||length(cross(x,y)-z)>.003f)return {};
 float m[3][3]={{x.x,y.x,z.x},{x.y,y.y,z.y},{x.z,y.z,z.z}};Quat q;
 float tr=m[0][0]+m[1][1]+m[2][2];
 if(tr>0){float s=std::sqrt(tr+1)*2;q={ (m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,.25f*s};}
 else {int i=m[1][1]>m[0][0]?1:0;if(m[2][2]>m[i][i])i=2;int j=(i+1)%3,k=(i+2)%3;float s=std::sqrt(1+m[i][i]-m[j][j]-m[k][k])*2;float v[4]{};v[i]=.25f*s;v[j]=(m[j][i]+m[i][j])/s;v[k]=(m[k][i]+m[i][k])/s;v[3]=(m[k][j]-m[j][k])/s;q={v[0],v[1],v[2],v[3]};}
 return valid(q)?std::optional<Quat>{normalized(q)}:std::nullopt;
}
struct ArmPose {Pose shoulder{},elbow{},wrist{};bool clamped{};};
// Keep shoulder translation and the seed skeleton independent of native combat
// animation. The camera/body reference still transports the entire arm.
struct AnchoredArm {
 Pose shoulder{},elbow{},wrist{};Vec camera{};bool ready{};
 bool capture(Pose a,Pose b,Pose c,Vec view){
  if(!valid(a)||!valid(b)||!valid(c)||!finite(view))return false;
  shoulder=a;elbow=b;wrist=c;camera=view;ready=true;return true;
 }
 ArmPose at(Vec view)const{
  const Vec delta=view-camera;
  return {{shoulder.position+delta,shoulder.orientation},{elbow.position+delta,elbow.orientation},
          {wrist.position+delta,wrist.orientation},false};
 }
};
// Derive the upper-body ancestor set from both verified arm chains. Leave
// root and its first child (lower-body base) native. Reject cycles/foreign rigs.
// An authored camera-joint excursion must not drop an established VR rig.
// Initial ownership still requires the native correlation. Continuity also
// requires the stabilized view to match and bounds the native excursion.
inline bool correlatedTrackedRig(float rawDistance,float stableDistance,bool established,bool anchored){
 return std::isfinite(rawDistance)&&std::isfinite(stableDistance)&&
  (rawDistance<=.35f||(established&&anchored&&rawDistance<=.75f&&stableDistance<=.35f));
}
inline std::vector<short> upperBodyAncestors(std::span<const short> parents,short right,short left){
 std::vector<short> paths[2];const short shoulders[]={right,left};
 for(unsigned h=0;h<2;++h){
  int id=shoulders[h];if(id<0||size_t(id)>=parents.size())return {};
  for(size_t n=0;n<parents.size();++n){
   id=parents[id];if(id<0)break;
   if(size_t(id)>=parents.size()||std::find(paths[h].begin(),paths[h].end(),id)!=paths[h].end())return {};
   paths[h].push_back(short(id));
  }
  if(id>=0)return {};
  std::reverse(paths[h].begin(),paths[h].end());
 }
 if(paths[0].size()<3||paths[1].size()<3||paths[0][0]!=paths[1][0]||paths[0][1]!=paths[1][1])return {};
 std::vector<short> result;
 for(auto& path:paths)for(size_t i=2;i<path.size();++i)if(std::find(result.begin(),result.end(),path[i])==result.end())result.push_back(path[i]);
 return result;
}
struct AnchoredTorso {
 std::vector<short> joints;std::vector<Pose> poses;Vec camera{};
 void reset(){*this={};}
 bool capture(std::span<const short> ids,std::span<const Pose> global,Vec view){
  if(ids.empty()||!finite(view))return false;
  for(auto id:ids)if(id<0||size_t(id)>=global.size()||!valid(global[id]))return false;
  joints.assign(ids.begin(),ids.end());poses.clear();for(auto id:ids)poses.push_back(global[id]);camera=view;return true;
 }
 Pose at(size_t i,Vec view)const {return {poses[i].position+view-camera,poses[i].orientation};}
};
// Absolute bone frames avoid inheriting a new animation twist each frame.
// The fixed joint-to-bone conversions are calibrated once per tracked rig.
inline std::optional<Quat> armBoneFrame(Vec direction,Vec pole){
 const auto x=unit(direction);const auto z=unit(cross(x,pole),{0,0,1});
 return rotationBasis(x,unit(cross(z,x)),z);
}
struct ArmAxisBinding {
 Quat upper{},lower{};bool valid{};
 bool capture(Pose a,Pose b,Pose c){
  Vec normal=cross(c.position-b.position,b.position-a.position);
  if(length(normal)<.0001f)return false;
  const auto u=armBoneFrame(b.position-a.position,cross(normal,b.position-a.position));
  const auto l=armBoneFrame(c.position-b.position,cross(normal,c.position-b.position));
  if(!u||!l)return false;
  upper=multiply(inverse(Pose{{},*u}).orientation,a.orientation);
  lower=multiply(inverse(Pose{{},*l}).orientation,b.orientation);valid=true;return true;
 }
};
inline std::optional<ArmPose> stableTrackedArm(Pose shoulder,Pose elbow,Pose wrist,Pose target,float upper,float lower,unsigned hand,ArmAxisBinding& binding,Quat bodyTurn={}){
 if(!valid(shoulder)||!valid(elbow)||!valid(wrist)||!valid(target)||hand>1)return {};
 if(!binding.valid&&!binding.capture(shoulder,elbow,wrist))return {};
 // Motor body coordinates: forward X, left Y, up Z. Keep the elbow down
 // and outside the torso; the caller supplies the anchored shoulder position.
 const Vec pole=rotate(bodyTurn,{-.2f,hand==0?.55f:-.55f,-1.f});
 const auto solved=solveArm(shoulder.position,elbow.position,target.position,upper,lower,pole);
 if(!solved.valid)return {};
 const auto u=armBoneFrame(solved.elbow-shoulder.position,pole);
 const auto l=armBoneFrame(solved.wrist-solved.elbow,pole);
 if(!u||!l)return {};
 return ArmPose{{shoulder.position,normalized(multiply(*u,binding.upper))},
  {solved.elbow,normalized(multiply(*l,binding.lower))},
  {solved.wrist,target.orientation},solved.clamped};
}
inline std::optional<ArmPose> trackedArm(Pose shoulder,Pose elbow,Pose wrist,Pose target,float upper,float lower,float weight){
 if(!valid(shoulder)||!valid(elbow)||!valid(wrist)||!valid(target)||!std::isfinite(weight)||weight<0||weight>1)return {};
 Vec position=wrist.position+(target.position-wrist.position)*weight;
 auto solved=solveArm(shoulder.position,elbow.position,position,upper,lower,elbow.position-shoulder.position);
 if(!solved.valid)return {};
 ArmPose out{shoulder,elbow,wrist,solved.clamped};
 out.shoulder.orientation=normalized(multiply(between(elbow.position-shoulder.position,solved.elbow-shoulder.position),shoulder.orientation));
 out.elbow.position=solved.elbow;
 out.elbow.orientation=normalized(multiply(between(wrist.position-elbow.position,solved.wrist-solved.elbow),elbow.orientation));
 out.wrist.position=solved.wrist;out.wrist.orientation=blendRotation(wrist.orientation,target.orientation,weight);
 return out;
}
struct PalmCalibration {Pose controllerToPalm{};};
// Motor's mirrored wrist axes: right +Z / left -Z follows the hand;
// the thumb side is right -X / left +X. Aim -Z is forward, +Y thumb-up.
inline Quat motorWristFromAim(unsigned hand){
 return *rotationBasis(hand==0?Vec{0,1,0}:Vec{0,-1,0},Vec{-1,0,0},hand==0?Vec{0,0,1}:Vec{0,0,-1});
}
inline std::optional<Pose> palmTarget(Pose controller,const PalmCalibration& calibration){
 if(!valid(controller)||!valid(calibration.controllerToPalm)||length(calibration.controllerToPalm.position)>.25f)return {};
 return compose(controller,calibration.controllerToPalm);
}
// Copy-only skeleton resolver. This never invokes Motor or marks its caches
// valid. Parents and local transforms must originate from the same evaluation.
inline std::optional<Pose> modelPose(std::span<const Pose> locals,std::span<const short> parents,unsigned joint,
 std::span<const Pose> cached={},std::span<const unsigned char> cacheValid={}){
 if(locals.size()!=parents.size()||joint>=locals.size()||locals.size()>4096)return {};
 if((!cached.empty()||!cacheValid.empty())&&(cached.size()!=locals.size()||cacheValid.size()!=locals.size()))return {};
 std::vector<unsigned> chain;chain.reserve(32);int id=int(joint);Pose result;
 while(id>=0){
  if(size_t(id)>=locals.size()||chain.size()>=locals.size())return {};
  // Motor's cached global pose is authoritative after IK. Reconstructing it
  // from local transforms discards native solver corrections/rounding.
  if(!cacheValid.empty()&&cacheValid[id]){if(!valid(cached[id]))return {};result=cached[id];id=-1;break;}
  if(!valid(locals[id]))return {};
  chain.push_back(unsigned(id));id=parents[id];
 }
 if(id!=-1)return {};
 for(auto i=chain.rbegin();i!=chain.rend();++i)result=compose(result,locals[*i]);
 return valid(result)?std::optional<Pose>{result}:std::nullopt;
}
// Measured Motor player skeleton: each wrist owns five 4-joint finger
// chains (metacarpal then three phalanges). Weapon refs start after these.
// Both hands use the same local curl axis; translations are mirrored by Motor.
struct FingerPose {unsigned id{};Pose local{},global{};};
inline std::optional<std::array<FingerPose,20>> gripFingerPose(
    std::span<const Pose> locals,std::span<const short> parents,unsigned wrist,
    Pose trackedWrist,float squeeze,float middleExtension=0) {
 if(locals.size()!=parents.size()||wrist+20>=locals.size()||!valid(trackedWrist)||!std::isfinite(squeeze)||!std::isfinite(middleExtension))return {};
 constexpr std::array<Quat,5> roots{{
  {-.0510f,-.0779f,-.0223f,.9954f}, {-.0747f,-.0050f,.0506f,.9959f},
  {-.0695f,.1148f,.1726f,.9758f}, {-.0807f,.0502f,.0792f,.9923f},
  {.1528f,-.4546f,-.4632f,.7453f}}};
 constexpr float palmLengths[5]={.0738f,.0746f,.0635f,.0706f,.0145f};
 const float amount=fistAmount(squeeze);
 std::array<FingerPose,20> out{};
 for(unsigned finger=0;finger<5;++finger){Pose parent=trackedWrist;
  for(unsigned joint=0;joint<4;++joint){const unsigned index=finger*4+joint,id=wrist+1+index;
   if(parents[id]!=int(joint?id-1:wrist)||!valid(locals[id]))return {};
   const auto delta=locals[id].position;const float distance=length(delta);
   if(joint==0){if(distance<.005f||distance>.04f)return {};}
   else if(joint==1){if(std::abs(distance-palmLengths[finger])>.004f)return {};}
   else if(distance<.012f||distance>.055f)return {};
   Pose local=locals[id]; // retain native translations and bone lengths
   if(joint==0)local.orientation=normalized(roots[finger]);
   else {
    // Absolute local pose, not an additive delta on top of the locomotion
    // animation. This prevents running/combat from closing released hands.
    constexpr float curl[3]={1.25f,1.65f,1.10f};
    constexpr float thumbCurl[3]={.80f,.70f,.65f};
    const float angle=-(finger==4?thumbCurl[joint-1]:curl[joint-1]);
    const Quat closed{std::sin(angle*.5f),0,0,std::cos(angle*.5f)};
    // Measured chain order: index, middle, little, ring, thumb. Chain1 is
    // the central/longest finger in both mirrored native hand captures.
    const float curlAmount=finger==1?amount*(1-fistAmount(middleExtension)):amount;
    local.orientation=blendRotation(Quat{},closed,curlAmount);
   }
   parent=compose(parent,local);if(!valid(parent))return {};
   out[index]={id,local,parent};
  }
 }
 return out;
}
struct HandBlend {
 uint64_t owner{},rig{},epoch{},tick{};float weight{};
 void reset(){owner=rig=epoch=tick=0;weight=0;}
 float update(uint64_t newOwner,uint64_t newRig,uint64_t newEpoch,uint64_t now,bool tracked,Context context){
  owner=newOwner;rig=newRig;epoch=newEpoch;
  const bool eligible=owner&&rig&&tracked&&(context==Context::Exploration||context==Context::Unarmed);
  // Valid controller targets own the final arm pose on the first frame.
  // A reacquire ramp exposes jump/crawl animation and feels like input lockout.
  tick=now;weight=eligible?1.f:0.f;return weight;
 }
};
}
