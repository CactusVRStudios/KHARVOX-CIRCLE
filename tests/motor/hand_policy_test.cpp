#include "hand_policy.hpp"
#include "hand_pose.hpp"
#include "punch_charge.hpp"
#include "firearm_policy.hpp"
#include "native_muzzle.hpp"
#include <cstdio>
using namespace circle::motor::hands;
int main(){int errors=0;auto check=[&](bool ok,const char* msg){if(!ok){++errors;printf("FAIL %s\n",msg);}};
 {
  const Vec camera{-68.563232f,8.945294f,-1.133759f},physics{-68.269623f,9.300914f,-2.228973f},body{-68.522934f,8.963605f,-2.228547f};
  const Vec moved=body+Vec{.4f,-.3f,.02f},current=camera+(moved-body);
  check(length(advanceTarget(camera,physics,moved)-current)>.35f,"recorded physics lag reproduces old arm rejection");
  check(length(advanceTarget(camera,body,moved)-current)<1e-4f,"matching rendered source advances once at vehicle speed");
  const Vec view{0,0,1.1f};
  const Pose shoulder{{0,-.18f,.9f},{}},elbow{{.22f,-.25f,.71f},{}},wrist{{.43f,-.18f,.85f},{}};
  const float upper=length(elbow.position-shoulder.position),lower=length(wrist.position-elbow.position);
  for(float degrees:{-180.f,-90.f,-30.f,0.f,45.f,90.f,180.f}){
   const float half=degrees*.00872664626f;const Quat turn{0,0,std::sin(half),std::cos(half)};
   const auto a=turnAroundView(shoulder,view,turn),b=turnAroundView(elbow,view,turn),c=turnAroundView(wrist,view,turn);
   check(length(turnAroundView(Pose{view,{}},view,turn).position-view)<1e-5f,"torso turn cannot translate seat camera pivot");
   check(std::abs(length(a.position-b.position)-upper)<1e-5f&&std::abs(length(b.position-c.position)-lower)<1e-5f,"seat turn retains arm bone lengths");
   ArmAxisBinding binding;auto solved=stableTrackedArm(a,b,c,c,upper,lower,1,binding,turn);
   check(solved&&length(solved->wrist.position-c.position)<1e-4f,"rotated shoulder/elbow frame reaches tracked wrist through full seated yaw");
   const Vec handDelta{.1f,-.03f,.05f};
   check(length(rebaseHandTarget(current+handDelta,current,camera)-camera-handDelta)<1e-4f,"hand controller offset follows camera independently of vehicle translation");
  }
 }
 {
  // A long prop's root swings around its grip. Test the old 2 m boundary,
  // locomotion, stick yaw and the grip itself, not just the model root.
  const Pose model{{10,20,3},{}};
  const Vec camera{10,20,4.65f},grip{.4f,.1f,1.45f},gripInProp{0,0,-1.1f};
  bool belowOldLimit=false,aboveOldLimit=false;
  for(int degrees=0;degrees<360;degrees+=10){
   const float angle=degrees*.00872664626f;
   const Quat wrist{0,std::sin(angle),0,std::cos(angle)};
   const Pose local{grip-rotate(wrist,gripInProp),wrist};
   belowOldLimit|=length(local.position)<2.f;aboveOldLimit|=length(local.position)>2.f;
   check(carriedModelOriginInRange(local.position),"long prop remains eligible through wrist rotation");
   for(float speed:{0.f,1.f,4.f})for(float yaw:{-5.f,0.f,5.f}){
    const auto render=renderHandModel(model,camera,camera+Vec{speed*.04f,0,0},yaw);
    const auto native=compose(model,local);
    const auto corrected=renderHandAttachment(model,render,native);
    const auto actualGrip=corrected.position+rotate(corrected.orientation,gripInProp);
    const auto expectedGrip=render.position+rotate(render.orientation,grip);
    check(length(actualGrip-expectedGrip)<1e-5f,"long weapon grip follows hands at all wrist angles and movement speeds");
   }
  }
  check(belowOldLimit&&aboveOldLimit,"long prop regression crosses old exclusion boundary");
  check(carriedModelOriginInRange({0,0,4})&&!carriedModelOriginInRange({0,0,4.01f})&&
        !carriedModelOriginInRange({NAN,0,0})&&!carriedModelOriginInRange({0,INFINITY,0}),
        "held origin retains finite bounded envelope");
 }
 {
  const Vec scale{-1,1,1}; // Live r292 Gizeh cargo crate, native and committed placement.
  check(handPlacementScale(scale,true),"mirrored carried crate receives render correction");
  check(!handPlacementScale(scale,false),"mirrored skeleton remains unsupported");
  check(!handPlacementScale({0,1,1},true)&&!handPlacementScale({2,1,1},true)&&
        !handPlacementScale({NAN,1,1},true),"invalid or unsupported carried scales rejected");
  for(float speed:{0.f,1.f,4.f})for(float yaw:{0.f,5.f}){
   const Pose solved{{10,20,3},{}};
   const Vec camera{10,20,4.65f};
   const Pose local{{.45f,0,1.15f},{}};
   const auto render=renderHandModel(solved,camera,camera+Vec{speed*.04f,0,0},yaw);
   const auto native=compose(solved,local);
   const auto corrected=renderHandAttachment(solved,render,native);
   // A mirrored mesh point and the grip retain the same relative transform
   // at rest, under translation and during stick rotation.
   const Vec point{-.2f,.1f,.3f};
   const Vec scaled{point.x*scale.x,point.y*scale.y,point.z*scale.z};
   const auto actual=corrected.position+rotate(corrected.orientation,scaled);
   const auto expected=compose(render,local);
   check(length(actual-(expected.position+rotate(expected.orientation,scaled)))<1e-5f,
         "mirrored mesh and grip share hand render delta at all tested speeds");
  }
 }
 {
  std::array<unsigned char,4096> memory{};
  const auto put=[&]<class T>(uintptr_t address,T value){std::memcpy(memory.data()+address,&value,sizeof(value));};
  const auto read=[&](uintptr_t address,void* output,size_t bytes){
   if(address>memory.size()||bytes>memory.size()-address)return false;
   std::memcpy(output,memory.data()+address,bytes);return true;
  };
  const uint64_t handle=2ull|(7ull<<24)|(1ull<<40);
  const NativeMuzzleSource source{0x100,0x200,handle,99};
  put(0x100,uintptr_t(0x800));put(0x108,uint64_t(5));
  put(0x200,uint64_t(99));put(0x300,handle);put(0x800+2*32+12,uint16_t(7));
  put(0x800+3*32,Vec{1,2,3});put(0x800+3*32+16,Quat{});
  Pose out;
  check(readNativeMuzzle(source,read,out)&&length(out.position-Vec{1,2,3})<1e-6f,"live muzzle resolves generation and transform offset");
  put(0x800+3*32,Vec{2,3,4});
  check(readNativeMuzzle(source,read,out)&&length(out.position-Vec{2,3,4})<1e-6f,"live muzzle reads updated hand placement instead of shot snapshot");
  const auto saved=out;
  put(0x800+2*32+12,uint16_t(8));
  check(!readNativeMuzzle(source,read,out)&&length(out.position-saved.position)==0,"recycled native transform rejected without changing output");
  put(0x800+2*32+12,uint16_t(7));put(0x300,handle+1);
  check(!readNativeMuzzle(source,read,out),"replacement attachment handle rejected");
  put(0x300,handle);put(0x200,uint64_t(100));
  check(!readNativeMuzzle(source,read,out),"foreign attachment owner rejected");
  put(0x200,uint64_t(99));memory[0x401]=1;
  check(!readNativeMuzzle(source,read,out),"hidden holstered attachment rejected");
  memory[0x401]=0;put(0x108,uint64_t(3));
  check(!readNativeMuzzle(source,read,out),"transform offset cannot exceed pool capacity");
  put(0x108,uint64_t(5));unsigned samples=0;
  const auto torn=[&](uintptr_t address,void* output,size_t bytes){
   if(!read(address,output,bytes))return false;
   if(address==0x860&&++samples==2)static_cast<unsigned char*>(output)[0]^=1;
   return true;
  };
  check(!readNativeMuzzle(source,torn,out),"changing transform snapshot rejected");
  put(0x800+3*32+16,Quat{0,0,0,0});
  check(!readNativeMuzzle(source,read,out),"invalid quaternion rejected");
 }
 {
  // A nearly extended arm must not hit/release its reach limit as the grounded
  // camera residual oscillates on stairs. Solve without that visual residual,
  // then carry the complete skeleton and equipment with the render camera.
  const Pose shoulder{{0,0,1.3f},{}},elbow{{.24f,.18f,1.3f},{}},wrist{{.48f,0,1.3f},{}};
  const Vec relative{.58f,0,-.4f};
  unsigned oldClamps=0;
  for(float residual:{-.22f,-.15f,0.f,.15f,.22f}){
   circle::motor::locomotion::CameraAnchor anchor{true,{},true,1.6f};
   anchor.visualHeightOffset=.1f+residual;anchor.stairHeightOffset=residual;
   const Vec physics{.12f,-.1f,.18f};const Pose model{physics,{}};
   const Vec rawCamera=physics+Vec{0,0,1.6f};
   const Vec visualCamera=anchor.place(rawCamera,physics,0);
   const Vec solveCamera=anchor.placeForSkeleton(rawCamera,physics,0);
   const Vec sampledCamera{0,0,1.55f};
   const Pose target{rebaseHandTarget(sampledCamera+relative,sampledCamera,solveCamera)-physics,{}};
   auto solved=trackedArm(shoulder,elbow,wrist,target,.3f,.3f,1);
   check(solved.has_value()&&!solved->clamped,"stair residual cannot clamp a reachable controller target");
   if(!solved)continue;
   auto old=trackedArm(shoulder,elbow,wrist,{visualCamera+relative-physics,{}},.3f,.3f,1);
   oldClamps+=old&&old->clamped;
   for(unsigned eye=0;eye<2;++eye){
    const auto draw=renderHandModel(model,solveCamera,visualCamera);
    const auto hand=compose(draw,solved->wrist);
    check(length(hand.position-visualCamera-relative)<1e-5f,"both eye hands share camera stair translation exactly once");
    const auto item=compose(draw,compose(solved->wrist,Pose{{.03f,.01f,0},{}}));
    check(length(item.position-hand.position-Vec{.03f,.01f,0})<1e-5f,"held item remains attached throughout stair residual");
    check(length(compose(draw,shoulder).position-(physics+shoulder.position+Vec{0,0,residual}))<1e-5f,"shoulder moves with complete rendered skeleton instead of freezing or stretching");
    check(std::abs((visualCamera.z-draw.position.z)-1.7f)<1e-5f,"ten centimetre camera lift relative to body preserved");
   }
  }
  check(oldClamps>=2,"regression reproduces previous stair reach clamp");
  circle::motor::locomotion::CameraAnchor disabled{};disabled.stairHeightOffset=.2f;
  check(length(disabled.placeForSkeleton({1,2,3},{4,5,6},0)-Vec{1,2,3})<1e-6f,"disabled stabilization does not move skeleton camera");
 }
 {
  // IK runs before render placement. A later body translation/rotation must
  // not move the already solved wrist relative to the matching render camera.
  const Pose solvedModel{{10,20,1},{0,0,.38268343f,.92387953f}};
  const Vec solvedCamera{10.02f,20.03f,2.7f};
  const Pose wristWorld{{10.3f,20.5f,2.4f},{}};
  const auto local=compose(inverse(solvedModel),wristWorld);
  for(float turn:{-20.f,-3.4f,0.f,3.4f,20.f}){
   const Vec camera=solvedCamera+Vec{.1f,-.2f,.05f};
   const auto actual=compose(renderHandModel(solvedModel,solvedCamera,camera,turn),local);
   const float half=turn*.00872664626f;const Quat q{0,0,std::sin(half),std::cos(half)};
   check(length(rotate(inverse(Pose{{},q}).orientation,actual.position-camera)-(wristWorld.position-solvedCamera))<1e-5f,"artificial yaw preserves camera-relative solved wrist across variable sample delays");
   check(length(rotate(actual.orientation,{1,0,0})-rotate(q,{1,0,0}))<1e-5f,"wrist orientation follows same artificial yaw as camera");
  }
  for(const Vec movement: {Vec{.1f,0,0},Vec{0,-.12f,0},Vec{0,0,.16f},Vec{0,0,0}}){
   const auto camera=solvedCamera+movement;
   const auto draw=renderHandModel(solvedModel,solvedCamera,camera);
   const auto actual=compose(draw,local);
   check(length((actual.position-camera)-(wristWorld.position-solvedCamera))<1e-5f,"render camera translation retains solved controller-relative wrist");
   check(std::abs(actual.orientation.w-wristWorld.orientation.w)<1e-5f,"render placement retains IK wrist orientation instead of later body yaw");
   const auto right=compose(draw,local);
   check(length(right.position-actual.position)<1e-6f,"both eyes use the identical anchored model placement");
  }
 }
 {
  // Regression: controller motion must never be cancelled by a camera-relative
  // weapon-root capture. Include translations, body turns, and moving gear.
  float frozenRootError=0;
  for(int frame=0;frame<180;++frame){
   const float phase=frame*.04f;
   const Pose model{{frame*.025f,.1f*std::sin(phase),0},{0,0,std::sin(phase/2),std::cos(phase/2)}};
   const Vec camera=model.position+Vec{0,0,1.65f};
   const auto draw=renderHandModel(model,camera,camera+Vec{.02f,-.03f,.04f},3.f);
   const Pose wrist{{.3f+.12f*std::sin(phase),.2f,1.35f},{std::sin(phase/2),0,0,std::cos(phase/2)}};
   const Pose grip{{.025f,.042f,.048f},{}};
   const auto weapon=compose(wrist,grip);
   const auto renderedWrist=compose(draw,wrist);
   const auto renderedWeapon=renderHandAttachment(model,draw,compose(model,weapon));
   const auto actualGrip=compose(inverse(renderedWrist),renderedWeapon);
   check(length(actualGrip.position-grip.position)<1e-5f,"walking and turning preserve weapon-to-wrist position");
   check(length(rotate(actualGrip.orientation,{1,0,0})-Vec{1,0,0})<1e-5f,"weapon and wrist retain the same orientation frame");
   for(unsigned part=0;part<7;++part){
    const Pose local{{.01f*part,.02f*std::sin(phase+part),.03f},{0,0,std::sin(phase/2),std::cos(phase/2)}};
    const auto nativePart=compose(model,compose(weapon,local));
    const auto renderedPart=renderHandAttachment(model,draw,nativePart);
    const auto relative=compose(inverse(renderedWeapon),renderedPart);
    check(length(relative.position-local.position)<1e-5f,"cylinder/trigger/bullets retain current animated offsets during locomotion");
    check(length(rotate(relative.orientation,{1,0,0})-rotate(local.orientation,{1,0,0}))<1e-5f,"gear animation rotation survives common frame correction");
   }
   // With no render correction (native/idle), placement is an identity mapping.
   const auto native=compose(model,weapon);
   check(length(renderHandAttachment(model,model,native).position-native.position)<1e-5f,"idle frame does not retain a previous VR correction");
   frozenRootError=std::max(frozenRootError,length(weapon.position-Vec{.325f,.242f,1.398f}));
  }
  check(frozenRootError>.1f,"camera-relative freeze reproduces more than ten centimetres of false weapon displacement");
 }
 for(unsigned hand=0;hand<2;++hand){
  std::array<PunchCharge,2> charge;uint64_t tick=1000;
  for(auto& c:charge)c.update(1,1,tick,true,0,0,false);
  tick+=10;charge[hand].update(1,1,tick,true,1,1,false);
  for(unsigned i=0;i<50;++i){tick+=10;check(charge[hand].update(1,1,tick,true,1,1,false)==0,"charging alone produces no attack request");}
  check(charge[1-hand].update(1,1,tick,true,1,0,true)==0,"charge stays on its own hand");
  tick+=10;check(charge[hand].update(1,1,tick,true,1,1,true)==510,"physical swing captures actual hold duration");
  for(unsigned i=0;i<40;++i){tick+=10;charge[hand].update(1,1,tick,true,1,1,false);}
  check(charge[hand].update(1,1,tick,true,1,1,true)==400,"second stroke recharges without releasing trigger");
  tick+=10;check(charge[hand].update(1,1,tick,true,1,0,false)==0,"trigger release cancels without attack");
  tick+=10;check(charge[hand].update(1,1,tick,true,1,0,true)==0,"ordinary physical punch has no charge credit");
 }
 {
  PunchCharge c;
  c.update(1,1,1000,true,1,0,false); // firearm outside melee: local neutral
  c.update(1,1,1010,true,1,1,false); // both triggers pressed together
  for(uint64_t t=1020;t<=1510;t+=10)c.update(1,1,t,true,1,1,false);
  check(c.update(1,1,1520,true,1,1,true)==510,"simultaneous melee selection and charge yields charged physical strike");
  check(c.update(1,1,1530,true,1,0,false)==0,"leaving firearm melee mode cancels local charge without attack");
 }
 {
  PunchCharge c;c.update(1,1,1000,true,1,0,false);
  c.update(1,1,1010,true,1,1,false);
  c.update(1,1,1020,false,1,1,false);
  check(c.update(1,1,1030,true,1,1,false)==0,"recovery restarts charge without an attack");
  for(uint64_t t=1040;t<1530;t+=10)c.update(1,1,t,true,1,1,false);
  check(c.update(1,1,1530,true,1,1,true)==500,"held trigger recharges after native combat interruption");
 }
 for(unsigned cancel=0;cancel<6;++cancel){
  PunchCharge c;uint64_t tick=1000;c.update(1,1,tick,true,1,0,false);
  for(unsigned i=0;i<60;++i){tick+=10;c.update(1,1,tick,true,1,1,false);}
  const auto owner=cancel==0?2u:1u,epoch=cancel==1?2u:1u;
  tick=cancel==2?900:cancel==3?tick+200:tick+10;
  check(c.update(owner,epoch,tick,cancel!=4,cancel==5?0.f:1.f,1,true)==0,"owner/recenter/time/tracking/context/grip loss cancels charge");
  tick+=10;check(c.update(owner,epoch,tick,true,1,1,true)==0,"held trigger cannot resurrect cancelled charge");
  tick+=10;c.update(owner,epoch,tick,true,1,0,false);
  tick+=10;c.update(owner,epoch,tick,true,1,1,false);
  tick+=50;check(c.update(owner,epoch,tick,true,1,1,true)==50,"fresh trigger rearms after cancellation");
 }
 check(creditedPunchStart(5000000,500)==4500020,"native game clock conversion matches reflected 999960Hz");
 check(creditedPunchStart(100,2000)==1,"native start time cannot underflow or become zero sentinel");
 for(unsigned hand=0;hand<2;++hand){
  check(!physicalChargeReleaseReady(1000,0,hand,0),"local trigger charge alone cannot dispatch native attack");
  check(!physicalChargeReleaseReady(1000,1,hand,2),"precharge must reach native accepted CHARGING state");
  check(!physicalChargeReleaseReady(349,2,hand,1),"short/normal strike retains native ordinary release");
  check(physicalChargeReleaseReady(350,2,hand,1),"accepted physical charged strike dispatches without waiting for button release");
  check(physicalChargeReleaseReady(1000,2,hand,2),"precharge-only credit releases once native CHARGING is confirmed");
  check(physicalChargeReleaseReady(1000,2,hand,6),"accepted precharge transition also dispatches charged stroke");
  check(!physicalChargeReleaseReady(1000,2,hand,9),"one charged dispatch per physical stroke");
  check(!physicalChargeReleaseReady(1000,2,hand,0),"uncredited native/gamepad charge is not taken over");
 }
 check(!physicalChargeReleaseReady(1000,2,2,1),"unknown native hand fails closed");
 for(unsigned hand=0;hand<2;++hand){
  check(physicalItemReleaseReady(0,2,hand,0),"ordinary item swing releases at native acceptance without trigger hold");
  check(physicalItemReleaseReady(349,2,hand,0),"short charge remains ordinary damage");
  check(!physicalItemReleaseReady(350,2,hand,0),"charged item strike retains charged path");
  check(!physicalItemReleaseReady(0,1,hand,0),"unaccepted precharge cannot release item strike");
  check(!physicalItemReleaseReady(0,2,hand,8),"accepted item strike cannot dispatch twice");
 }
 check(!physicalItemReleaseReady(0,2,2,0),"invalid item strike hand rejected");
 check(physicalStrikeHandValid(true,0)&&physicalStrikeHandValid(true,1),"both fists support immediate ordinary release");
 check(!physicalStrikeHandValid(true,~0u),"fists require explicit native hand");
 check(physicalStrikeHandValid(false,~0u),"held item permits native default weapon hand");
 check(!physicalStrikeHandValid(false,2),"invalid native hand rejected");
 for(int x=-8;x<=8;++x)for(int y=-8;y<=8;++y)for(int z=-4;z<=4;++z){auto a=solveArm({0,0,0},{0,.2f,-.2f},{x*.1f,y*.1f,z*.1f},.30f,.2689f,{0,0,-1});check(a.valid,"finite arm solution");check(std::abs(length(a.elbow)-.3f)<.0001f,"upper arm length preserved");check(std::abs(length(a.wrist-a.elbow)-.2689f)<.0001f,"forearm length preserved");check(length(a.wrist)<.569f,"unreachable target clamped");}
 check(!solveArm({},{},{},0,.3f,{}).valid,"invalid native length rejected");
 HandFrame f{{4,5,1.65f},{0,1.7f,0},{{1,0,0},{0,-1,0},{0,0,1},1,true},0};
 auto p=f.toWorld({.2f,1.4f,-.3f});check(p&&length(*p-Vec{4.3f,4.8f,1.35f})<.0001f,"controller delta uses body camera height");
 f.trackingHead={1,2,1};auto q=f.toWorld({1.2f,1.7f,.7f});check(q&&length(*p-*q)<.0001f,"shared tracking translation cannot double roomscale");
 Request r;r.grip=r.trigger=r.punch=true;check(route(Context::Unarmed,r,true)==Intent::Grab,"grip consumes punch/trigger");r={};r.trigger=r.punch=true;check(route(Context::Firearm,r,true)==Intent::Fire,"armed trigger produces one action");r={};r.punch=true;check(route(Context::Traversal,r,true)==Intent::None,"no traversal punch");r.pause=true;check(route(Context::Unknown,r,false)==Intent::Pause,"pause available in unknown context");
 PunchDetector punch;uint64_t t=1000;Vec h{0,.2f,-.2f};punch.update(h,{0,1,0},t,Context::Unarmed,true,false,false);
 for(int i=0;i<20;++i){t+=10;check(!punch.update(h,{0,1,0},t,Context::Unarmed,true,false,false),"stationary hands cannot punch");}
 int hits=0;for(int i=0;i<12;++i){t+=10;h.y+=.025f;hits+=punch.update(h,{0,1,0},t,Context::Unarmed,true,false,false);}check(hits==1,"deliberate strike fires once");
 for(int i=0;i<20;++i){t+=10;h.y+=.003f;check(!punch.update(h,{0,1,0},t,Context::Unarmed,true,false,false),"slow reach no retrigger");}
 check(!punch.update({8,8,8},{0,1,0},t+10,Context::Unarmed,true,false,false),"tracking jump rejected");
 check(!punch.update(h,{0,1,0},t+20,Context::Traversal,true,false,false),"climbing clears gesture");
 BlockDetector block;check(!block.update({-.2f,.3f,-.2f},{.2f,.3f,-.2f},Context::Unarmed,true,false,1000),"guard requires hold");check(block.update({-.2f,.3f,-.2f},{.2f,.3f,-.2f},Context::Unarmed,true,false,1150),"guard dwell");check(!block.update({},{},Context::Menu,true,false,1200),"menu clears block");
 Pose body{{3,-4,2},{0,0,.70710678f,.70710678f}},local{{.3f,.2f,-.1f},{}};
 for(int i=0;i<360;i+=15){float angle=i*circle::motor::locomotion::radians;Vec x{std::cos(angle),std::sin(angle),0},y{-std::sin(angle),std::cos(angle),0};auto rot=rotationBasis(x,y,{0,0,1});check(rot&&length(rotate(*rot,{1,0,0})-x)<.00001f,"basis rotation roundtrip all quadrants");}
 check(!rotationBasis({1,0,0},{0,1,0},{0,0,-1}),"reflection is not a rotation");
 for(int i=-10;i<=10;++i){Pose a{{0,0,0},{}},b{{.3f,0,0},{}},c{{.3f,.2689f,0},{}};auto arm=trackedArm(a,b,c,{{i*.1f,.3f,.2f},{}},.3f,.2689f,1);check(arm&&length(arm->shoulder.position)==0,"IK root remains fixed");if(arm){check(std::abs(length(arm->elbow.position-arm->shoulder.position)-.3f)<.0001f,"pose upper length");check(length(rotate(arm->shoulder.orientation,{.3f,0,0})-(arm->elbow.position-arm->shoulder.position))<.0001f,"upper orientation agrees with solved elbow");check(length(rotate(arm->elbow.orientation,{0,.2689f,0})-(arm->wrist.position-arm->elbow.position))<.0001f,"lower orientation agrees with solved wrist");}}
 auto roundtrip=compose(inverse(body),compose(body,local));check(length(roundtrip.position-local.position)<.00001f,"model/world pose round trip");
 auto palm=palmTarget(body,{{{.1f,0,0},{}}});check(palm&&length(palm->position-Vec{3,-3.9f,2})<.00001f,"palm offset rotates with controller");
 check(!palmTarget(body,{{{9,0,0},{}}}),"invalid calibration rejected");
 std::array<Pose,3> joints{body,local,Pose{{.1f,0,0},{}}};std::array<short,3> parents{-1,0,1};
 auto resolved=modelPose(joints,parents,2);auto expected=compose(compose(body,local),joints[2]);check(resolved&&length(resolved->position-expected.position)<.00001f,"local hierarchy resolved without native cache mutation");
 std::array<Pose,3> cache{};cache[1]={{8,9,10},{}};std::array<unsigned char,3> flags{0,1,0};
 auto authoritative=modelPose(joints,parents,2,cache,flags);check(authoritative&&length(authoritative->position-Vec{8.1f,9,10})<.00001f,"dirty child inherits authoritative native IK cache");
 parents[0]=2;check(!modelPose(joints,parents,2),"cyclic skeleton rejected");parents[0]=-1;parents[1]=99;check(!modelPose(joints,parents,2),"out of range parent rejected");
 TrackingSmoothing filter;Pose grip{{0,0,0},{}},aim{{0,0,-.1f},{}};
 GripSwipe swipe;
 check(!swipe.update({0,0,0},{1,0,0},1000,true,true)&&swipe.update({-.18f,0,0},{1,0,0},1250,true,true),"deliberate left hand swipe drives native left action");
 check(!swipe.update({-.4f,0,0},{1,0,0},1350,true,true),"held grip cannot repeat interaction");
 swipe.update({0,0,0},{1,0,0},1400,true,false);
 swipe.update({0,0,0},{1,0,0},1500,true,true);
 check(!swipe.update({.2f,0,0},{1,0,0},1700,true,true),"rightward return does not fire left prompt");
 swipe.update({0,0,0},{1,0,0},1800,false,true);
 check(!swipe.update({-.3f,0,0},{1,0,0},1900,true,true),"context loss clears old swipe");
 filter.update(grip,aim,1000000000,1,true);grip.position.x=.01f;aim.position.x=.01f;
 filter.update(grip,aim,1010000000,1,true);
 check(grip.position.x>0&&grip.position.x<.01f,"tracking-space filter attenuates small movements");
 check(length(aim.position-grip.position-Vec{0,0,-.1f})<.00001f,"smoothing preserves rigid grip aim offset");
 const auto once=grip;grip={{.01f,0,0},{}};aim={{.01f,0,-.1f},{}};
 filter.update(grip,aim,1010000000,1,true);
 check(length(grip.position-once.position)<1e-7f,"duplicate XR sample reuses filtered pose without raw/filtered oscillation");
 grip.position.x=1;aim.position.x=1;filter.update(grip,aim,1020000000,1,true);check(grip.position.x==1,"tracking discontinuity resets filter");
 grip.position.x=1.01f;filter.update(grip,aim,1030000000,2,true);check(grip.position.x==1.01f,"recenter never drags old hand pose");
 {
  HeadRelativeSmoothing relative;
  const Pose held{{.25f,-.25f,-.45f},{}},aimLocal{{.25f,-.25f,-.55f},{}};
  for(unsigned f=0;f<180;++f){
   const float angle=float(f)*.035f;
   Pose head{{float(f)*.01f,1.6f,0},{0,std::sin(angle*.5f),0,std::cos(angle*.5f)}};
   auto g=compose(head,held),a=compose(head,aimLocal);
   relative.update(g,a,head,1000000000ull+f*11000000ull,1,true,.020f);
   const auto local=compose(inverse(head),g);
   check(length(local.position-held.position)<2e-5f,"rigid physical head/hand turn and translation have no smoothing lag");
   check(std::abs(local.orientation.w)> .99999f,"head-relative orientation does not trail physical body turn");
  }
 }
 {
  HandRenderStamp stamp{17,100,4,5000};
  check(stamp.matches(17,100,4,5010),"both eyes may use the successfully applied simulation pose");
  check(!stamp.matches(18,100,4,5010)&&!stamp.matches(17,101,4,5010)&&!stamp.matches(17,100,5,5010)&&!stamp.matches(17,100,4,5100),"render hand latch rejects other owner simulation recenter and stale data");
  // A late camera yaw creates apparent hand slip even with a stationary
  // controller. Using the applied pose's orientation removes that mismatch.
  Pose appliedHead{{2,3,1.6f},{0,0,std::sin(.2f),std::cos(.2f)}};
  const Pose relativeHand{{.4f,.2f,-.2f},{}};
  auto handWorld=compose(appliedHead,relativeHand);
  auto lateHead=appliedHead;lateHead.orientation={0,0,std::sin(.24f),std::cos(.24f)};
  check(length(compose(inverse(lateHead),handWorld).position-relativeHand.position)>.02f,"replay exposes old skeleton versus newer stick-turn camera displacement");
  check(length(compose(inverse(appliedHead),handWorld).position-relativeHand.position)<1e-6f,"same applied frame removes artificial turn displacement");
 }
 check(length(advanceTarget({5,2,1},{4,2,0},{4.12f,2,0})-Vec{5.12f,2,1})<.00001f,"moving rig carries target across physics render timing gap");
 HandBlend blend;check(blend.update(1,2,3,1000,true,Context::Unarmed)==1,"validated tracked rig takes control immediately");
 for(uint64_t ms=1010;ms<=1200;ms+=10)blend.update(1,2,3,ms,true,Context::Unarmed);
 check(blend.weight==1,"tracked hands blend in");check(blend.update(1,2,3,1220,false,Context::Unarmed)<1,"tracking loss fades out");
 check(blend.update(1,2,3,1240,true,Context::Traversal)==0,"scripted traversal immediately native");
 check(blend.update(1,4,3,1300,true,Context::Unarmed)==1,"recreated validated rig has no native animation timeout");
 blend.reset();check(blend.update(1,4,4,1310,true,Context::Exploration)==1,"resume and recenter immediately restore controller pose");
 for(unsigned h=0;h<2;++h){auto wrist=motorWristFromAim(h);float side=h?1.f:-1.f;
  check(length(rotate(wrist,{0,0,side})-Vec{0,0,-1})<.00001f,"mirrored wrist points along controller aim");
  check(length(rotate(wrist,{-side,0,0})-Vec{0,1,0})<.00001f,"mirrored thumb points up");
 }
 VelocityPunchDetector vp;
 check(trackedArmMovement(0)&&trackedArmMovement(1)&&trackedArmMovement(2)&&trackedArmMovement(9),"standing crouching prone and combat retain tracked arms");
 check(!trackedArmMovement(3)&&!trackedArmMovement(6)&&!trackedArmMovement(10)&&!trackedArmMovement(255),"traversal grab and unknown remain native");
 check(trackedArmMovement(20)&&trackedVisualArmMovement(20),"wade retains physical hand interaction and equipment eligibility");
 for(unsigned mode:{7u,8u})check(trackedVisualArmMovement(mode)&&!trackedArmMovement(mode),"swimming tracks visual arms without enabling ground combat gestures");
 for(unsigned mode:{3u,6u,10u,23u,26u,255u})check(!trackedVisualArmMovement(mode),"scripted and inspection limbs retain animation ownership");
 check(!vp.update({0,0,0},{0,1,0},1000,true),"XR velocity rest arms detector");
 check(!vp.update({0,1.5f,0},{0,1,0},1005,true),"subthreshold movement does not attack");
 check(vp.update({0,2.1f,0},{0,1,0},1010,true),"forward strike crosses 2m/s");
 check(!vp.update({0,2,0},{0,1,0},1020,true),"held fast strike fires only once");
 vp.update({0,.4f,0},{0,1,0},1100,true);
 check(!vp.update({0,2.1f,0},{0,1,0},1200,true),"native cooldown retained");
 check(vp.update({0,2.1f,0},{0,1,0},1400,true),"half threshold rearm permits next strike");
 vp.update({0,0,0},{0,1,0},1800,true);
 check(vp.update({2,0,0},{0,1,0},1810,true),"sideways sweep uses speed magnitude");
 check(!vp.update({0,2,0},{0,1,0},1820,false),"invalid context never punches");
 check(!vp.update({0,2,0},{0,1,0},1830,true),"tracking return mid strike cannot fire");
 for(Vec speed:{Vec{3,0,0},Vec{0,-3,0},Vec{0,0,3},Vec{0,0,-3},Vec{20,0,0}}){
  VelocityPunchDetector magnitude;magnitude.update({}, {},1000,true);
  check(magnitude.update(speed,{},1010,true),"all directions and very fast valid XR swings cross threshold");
  check(!magnitude.update(speed,{},1500,true),"sustained speed does not spam attacks");
  magnitude.update({}, {},1510,true);
  check(magnitude.update(speed,{},1520,true),"next settled swing attacks again");
  check(!magnitude.update({NAN,0,0},{},2000,true),"invalid tracking velocity rejected");
 }
 check(fistAmount(-.1f)==0&&fistAmount(0)==0&&fistAmount(.5f)==.5f&&fistAmount(1)==1&&fistAmount(1.1f)==1,"analog grip maps continuously to fist blend");
 check(!fistReady(.8999f)&&fistReady(.9f)&&fistReady(1)&&!fistReady(NAN)&&!fistReady(INFINITY),"grip threshold inclusive and invalid samples fail closed");
 std::array<VelocityPunchDetector,2> gripPunch;
 gripPunch[0].update({},{0,1,0},2000,fistReady(1));
 gripPunch[1].update({},{0,1,0},2000,fistReady(.5f));
 check(gripPunch[0].update({0,2.1f,0},{0,1,0},2010,fistReady(1))&&!gripPunch[1].update({0,2.1f,0},{0,1,0},2010,fistReady(.5f)),"gripping one hand cannot enable the other punch");
 gripPunch[0].update({},{0,1,0},2400,fistReady(.8f));
 check(!gripPunch[0].update({0,2.1f,0},{0,1,0},2410,fistReady(1)),"closing grip mid swing cannot trigger attack without rearming");
 gripPunch[0].update({},{0,1,0},2500,fistReady(1));
 check(gripPunch[0].update({0,2.1f,0},{0,1,0},2510,fistReady(.9f)),"closed fist arms at rest then permits forward strike");
 std::vector<Pose> fingerLocal(22);std::vector<short> fingerParents(22,-1);
 const float fingerPalmLengths[5]={.0738f,.0746f,.0635f,.0706f,.0145f};
 for(unsigned side=0;side<2;++side){
  for(unsigned f=0;f<5;++f)for(unsigned j=0;j<4;++j){unsigned id=1+f*4+j;fingerParents[id]=j?short(id-1):0;fingerLocal[id].position={0,0,(side?1.f:-1.f)*(j==0?.015f:j==1?fingerPalmLengths[f]:.025f)};}
  auto opened=gripFingerPose(fingerLocal,fingerParents,0,Pose{},0);
  auto halfway=gripFingerPose(fingerLocal,fingerParents,0,Pose{},.5f);
  auto closed=gripFingerPose(fingerLocal,fingerParents,0,Pose{},1);
  check(opened&&halfway&&closed,"validated mirrored native finger chains produce finite poses");
  if(opened&&halfway&&closed){
   auto gesture=gripFingerPose(fingerLocal,fingerParents,0,Pose{},1,1);
   auto partial=gripFingerPose(fingerLocal,fingerParents,0,Pose{},1,.5f);
   check(gesture&&partial,"cosmetic pose valid for both native mirrored hands");
   if(gesture&&partial)for(unsigned j=0;j<20;++j){
    const auto actual=(*gesture)[j].local.orientation;
    const auto expected=j>=5&&j<=7?(*opened)[j].local.orientation:(*closed)[j].local.orientation;
    check(std::abs(actual.x-expected.x)+std::abs(actual.y-expected.y)+std::abs(actual.z-expected.z)+std::abs(actual.w-expected.w)<1e-5f,"only the middle phalanges open; four other fingers retain the fist");
    check(length((*gesture)[j].local.position-fingerLocal[j+1].position)==0,"gesture preserves bone lengths");
   }
   if(partial)check((*partial)[5].local.orientation.x>(*closed)[5].local.orientation.x&&(*partial)[5].local.orientation.x<0,"trigger smoothly extends middle finger");
   check((*opened)[1].local.orientation.w==1&&(*halfway)[1].local.orientation.x<0&&(*halfway)[1].local.orientation.x>(*closed)[1].local.orientation.x,"grip continuously closes fingers from open to fist");
   for(unsigned j=0;j<20;++j)check(length((*closed)[j].local.position-fingerLocal[j+1].position)==0,"finger control preserves every native bone length");
   for(auto& pose:fingerLocal)pose.orientation={-.6f,0,0,.8f};
   auto running=gripFingerPose(fingerLocal,fingerParents,0,Pose{},0);
   check(running&&(*running)[1].local.orientation.w==1&&(*running)[0].local.orientation.w==(*opened)[0].local.orientation.w,"running fist animation cannot affect released tracked fingers");
  }
 }
 fingerParents[4]=0;check(!gripFingerPose(fingerLocal,fingerParents,0,Pose{},1),"unknown topology rejected without touching weapon/helper joints");
 // Replay gait camera offsets across the render/animation sampling boundary.
 // Held controller must retain exactly the same camera-relative displacement.
 for(int frame=1;frame<120;++frame){
  Vec oldCamera{frame*.05f,.02f*std::sin(frame*.3f),1.65f+.04f*std::sin(frame*.7f)};
  Vec newCamera{(frame+1)*.05f,.02f*std::sin((frame+1)*.3f),1.65f+.04f*std::sin((frame+1)*.7f)};
  const Vec heldOffset{.3f,.2f,-.25f};
  const auto hand=rebaseHandTarget(oldCamera+heldOffset,oldCamera,newCamera);
  check(length(hand-newCamera-heldOffset)<1e-5f,"held hand stays camera-relative across gait phase and player translation");
 }
 // Native roots and bend planes must follow the current animation, while a
 // reachable controller target stays fixed. Exercise mirrored arm chains.
 for(float side:{-1.f,1.f})for(int step=0;step<120;++step){
  const float phase=step*.02f;
  Pose root{{.02f*std::sin(phase),.02f*std::cos(phase),0},{0,0,std::sin(phase*.2f),std::cos(phase*.2f)}};
  Pose a=compose(root,Pose{}),b=compose(root,Pose{{side*.3f,0,0},{}}),c=compose(root,Pose{{side*.3f,.2689f,0},{}});
  Pose target{{side*.25f,.25f,.1f},{}};
  auto solved=trackedArm(a,b,c,target,.3f,.2689f,1);
  check(solved&&length(solved->shoulder.position-a.position)<1e-5f,"current native shoulder follows animation rather than first tracked pose");
  if(solved){
   check(length(solved->wrist.position-target.position)<1e-5f,"native shoulder movement retains reachable controller wrist target");
   check(std::abs(length(solved->elbow.position-solved->shoulder.position)-.3f)<1e-5f&&std::abs(length(solved->wrist.position-solved->elbow.position)-.2689f)<1e-5f,"both animated arm chains retain native segment lengths");
   check(length(rotate(solved->shoulder.orientation,{side*.3f,0,0})-(solved->elbow.position-a.position))<1e-5f,"mirrored upper arm rotation agrees with solved elbow");
   check(length(rotate(solved->elbow.orientation,{0,.2689f,0})-(solved->wrist.position-solved->elbow.position))<1e-5f,"mirrored lower arm rotation agrees with solved wrist");
  }
 }
 {
  Pose a{{0,0,0},{}},b{{.3f,0,0},{}},c{{.3f,.2689f,0},{}};
  for(unsigned hand=0;hand<2;++hand){
   ArmAxisBinding binding;Pose target{{.35f,hand==0?.15f:-.15f,-.12f},{}};
   const auto first=stableTrackedArm(a,b,c,target,.3f,.2689f,hand,binding);
   check(bool(first),"stable arm calibration succeeds");
   for(int i=0;i<100&&first;++i){
    const float angle=i*.06f;Quat spin{std::sin(angle),0,0,std::cos(angle)};
    auto movedB=b,movedC=c;movedB.orientation=spin;movedC.orientation=spin;
    movedB.position={.3f*std::cos(angle),.3f*std::sin(angle),0};
    const auto arm=stableTrackedArm(a,movedB,movedC,target,.3f,.2689f,hand,binding);
    check(arm&&length(arm->wrist.position-target.position)<1e-5f,"animation twist cannot displace tracked wrist");
    if(arm){check(length(arm->elbow.position-first->elbow.position)<1e-5f,"native elbow animation cannot flip stable bend plane");
     check(length(rotate(arm->shoulder.orientation,{.3f,0,0})-(arm->elbow.position-a.position))<1e-5f,"stable upper bone axis retains calibrated joint geometry");
     check(length(rotate(arm->elbow.orientation,{0,.2689f,0})-(arm->wrist.position-arm->elbow.position))<1e-5f,"stable lower bone axis retains calibrated joint geometry");}
   }
   auto raised=a;raised.position.z=.05f;const auto arm=stableTrackedArm(raised,b,c,target,.3f,.2689f,hand,binding);
   check(arm&&length(arm->shoulder.position-raised.position)<1e-5f,"stable axes do not freeze animated shoulder position");
  }
 }
 {
  AnchoredArm anchor;Pose a{{0,.2f,1.4f},{}},b{{.2f,.25f,1.2f},{}},c{{.4f,.2f,1.1f},{}};
  check(anchor.capture(a,b,c,{0,0,1.7f}),"shoulder anchor captures valid native proportions");
  auto still=anchor.at({0,0,1.7f});
  check(length(still.shoulder.position-a.position)<1e-6f,"fixed shoulder does not inherit later animation shifts");
  auto moved=anchor.at({1,2,2.2f});
  check(length(moved.shoulder.position-a.position-Vec{1,2,.5f})<1e-5f,"locomotion and height transport anchored shoulder");
  check(length((moved.elbow.position-moved.shoulder.position)-(b.position-a.position))<1e-5f,"anchor retains arm proportions during body movement");
 }
 {
  const std::vector<short> parents{-1,0,1,2,3,4,3,6,3,8,1};
  const auto ids=upperBodyAncestors(parents,5,7);
  check(ids==std::vector<short>({2,3,4,6}),"torso ancestry excludes pelvis, root, arms, legs and camera branch");
  auto cyclic=parents;cyclic[2]=3;
  check(upperBodyAncestors(cyclic,5,7).empty(),"cyclic torso ancestry rejected");
  auto foreign=parents;foreign[6]=-1;
  check(upperBodyAncestors(foreign,5,7).empty(),"unrelated arm roots rejected");
  std::vector<Pose> native(parents.size());
  for(size_t i=0;i<native.size();++i)native[i].position={0,0,float(i)*.1f};
  AnchoredTorso torso;check(torso.capture(ids,native,{0,0,1.7f}),"valid torso captures");
  const auto baseline=native[2];native[2]={{1,2,3},{0,0,.7071068f,.7071068f}};
  auto held=torso.at(0,{0,0,1.7f});
  check(length(held.position-baseline.position)<1e-6f,"attack displacement cannot move captured torso");
  check(length(rotate(held.orientation,{1,0,0})-Vec{1,0,0})<1e-6f,"attack rotation cannot rotate captured torso");
  held=torso.at(0,{2,3,2.2f});
  check(length(held.position-baseline.position-Vec{2,3,.5f})<1e-5f,"torso follows stable locomotion and camera height");
  const auto local=compose(inverse(native[2]),held);
  const auto restored=compose(native[2],local);
  check(length(restored.position-held.position)<1e-5f,"moving lower parent compensated by torso local pose");
  Pose camera{{.1f,.2f,1.7f},{.2f,0,0,.9797959f}};
  const auto preserved=compose(held,compose(inverse(held),camera));
  check(length(preserved.position-camera.position)<1e-5f&&length(rotate(preserved.orientation,{0,0,1})-rotate(camera.orientation,{0,0,1}))<1e-5f,"camera global pose preserved across torso substitution");
 }
 {
  for(float distance:{.34f,.3544f,.3726f,.4001f,.36f,.2f})
   check(correlatedTrackedRig(distance,.02f,true,true),"r250 measured strike camera excursion retains tracked rig");
  check(!correlatedTrackedRig(.4001f,.02f,false,true),"new rig still requires native camera ownership");
  check(!correlatedTrackedRig(.4f,.02f,true,false),"inactive camera anchor cannot bridge native discrepancy");
  check(!correlatedTrackedRig(.4f,.36f,true,true),"stable view mismatch rejects rig");
  check(!correlatedTrackedRig(.76f,.02f,true,true),"large native discontinuity rejects rig");
  check(!correlatedTrackedRig(NAN,.02f,true,true),"invalid native correlation rejected");
 }
 {
  for(int pose=23;pose<=33;++pose)check(twoHandFirearmPose(pose)==(pose>=26&&pose<=30),"long-gun pose family excludes pistol, turret, camera and whip");
  NativeSupportPose support;Pose native{{.3f,.04f,-.02f},{}};
  check(!support.observe(native,1000)&&!support.observe(native,1100)&&support.observe(native,1200),"native support settles before binding");
  auto attack=native;attack.position.x=.6f;support.observe(attack,1300);
  check(length(support.value.position-native.position)<1e-6f,"attack animation cannot recapture held support point");
  for(uint64_t t:{1400ull,1500ull,1600ull,1700ull})support.observe(attack,t);
  check(length(support.value.position-attack.position)<1e-6f,"settled post-load support refreshes initial contact");
  Pose head{{0,0,1.7f},normalized({.5f,.5f,-.5f,.5f})};
  Pose nearShoulder{{-.1f,-.18f,1.48f},{}};Vec aimHand{.4f,-.18f,1.48f};
  auto stock=gunstockWrist(nearShoulder,{.3f,0,0},aimHand,head);
  auto jitter=nearShoulder;jitter.position.y+=.01f;
  auto two=supportedWrist(jitter,{.3f,0,0},aimHand);
  auto stable=gunstockWrist(jitter,{.3f,0,0},aimHand,head);
  const Vec expected=rotate(stock.orientation,{1,0,0});
  check(length(rotate(stable.orientation,{1,0,0})-expected)<length(rotate(two.orientation,{1,0,0})-expected),"virtual shoulder reduces near-stock aiming jitter");
  Pose yaw{{2,3,.2f},normalized({0,0,.5f,.8660254f})};
  auto transported=gunstockWrist(compose(yaw,jitter),{.3f,0,0},compose(yaw,Pose{aimHand,{}}).position,compose(yaw,head));
  check(length(rotate(transported.orientation,{1,0,0})-rotate(yaw.orientation,rotate(stable.orientation,{1,0,0})))<1e-5f,"stock follows physical and stick yaw without lag");
  SupportGrab g;check(!g.update(true,0,true),"neutral arms support grip");
  check(g.update(true,1,true)&&g.latched,"fresh left grip latches near calibrated support");
  check(g.update(true,1,false)&&g.latched,"support remains held while aiming away from initial point");
  check(g.update(false,1,false)&&!g.latched,"tracking loss releases aim but consumes grip to avoid reload");
  check(!g.update(true,0,true)&&!g.latched,"release frees hand and rearms");
  check(!g.update(true,1,false),"distant fresh grip remains available for reload");
  check(!g.update(true,1,true),"moving held reload grip near gun cannot latch");
  g.update(true,0,true);check(g.update(true,1,true)&&g.latched,"repeat grab works without weapon redraw");
  Pose primary{{0,0,0},{}};Vec point{.3f,.04f,-.02f},left{.4f,.12f,.08f};
  auto aimed=supportedWrist(primary,point,left);
  check(length(unit(rotate(aimed.orientation,point))-unit(left))<1e-5f,"calibrated vector aims at offhand");
  Pose turn{{2,3,4},normalized({0,0,.7f,.7f})};
  auto rotated=supportedWrist(compose(turn,primary),point,compose(turn,Pose{left,{}}).position);
  check(length(rotate(rotated.orientation,point)-rotate(turn.orientation,rotate(aimed.orientation,point)))<1e-5f,"physical/stick rotation and translation preserve two-hand aim");
  check(length(aimed.position-primary.position)<1e-6f,"gunstock never moves primary hand position");
 }
 {
  Pose source{{1,2,3},{}},current{{1.3f,2.2f,3},{}},local{{.5f,0,.2f},{}};
  const auto spawned=compose(source,local);
  const auto corrected=renderHandAttachment(source,current,spawned);
  check(length(corrected.position-compose(current,local).position)<1e-5f,"flash uses shot-time source frame across body movement");
  Pose shot{{1,2,3},{}};
  check(muzzleFlashMatches(shot,shot,1000,1100),"fresh exact local muzzle effect matched");
  auto effect=shot;effect.position.y+=.02f;
  check(!muzzleFlashMatches(shot,effect,1000,1100),"nearby unrelated flash excluded");
  effect=shot;effect.orientation={0,0,1,0};
  check(!muzzleFlashMatches(shot,effect,1000,1100),"opposite-direction effect excluded");
  effect=shot;effect.orientation={1,0,0,0};
  check(muzzleFlashMatches(shot,effect,1000,1100),"particle roll may differ while muzzle forward matches");
  check(!muzzleFlashMatches(shot,shot,1000,1251)&&!muzzleFlashMatches(shot,shot,1000,999),"stale/future shot excluded");
  for(const char* asset:{"weapon/particle_weapon_revolver01_muzzle_02","weapon/particle_weapon_smg01_muzzle_02","weapon/particle_weapon_pistol01_muzzle_01"})
   check(muzzleParticleAsset(asset),"captured firearm muzzle asset eligible");
  for(const char* asset:{"weapon/particle_weapon_pistol01_impact_01","props/lighter/particle_prop_lighter_01.prt","world/particle_weapon_smg01_muzzle_02"})
   check(!muzzleParticleAsset(asset),"unrelated effect excluded from muzzle path");
  const Pose authored{{.002f,0,0},{1,0,0,0}};
  const auto flash=compose(shot,authored);
  for(const auto axis:std::array<Vec,3>{Vec{1,0,0},Vec{0,1,0},Vec{0,0,1}}){
   const Pose handTurn{{1.2f,1.9f,3.1f},{axis.x*.70710678f,axis.y*.70710678f,axis.z*.70710678f,.70710678f}};
   const auto result=renderMuzzleParticle(shot,handTurn,flash,1000,1100);
   const auto expected=compose(handTurn,authored);
   check(result&&length(result->position-expected.position)<1e-5f&&
    length(rotate(result->orientation,{0,1,0})-rotate(expected.orientation,{0,1,0}))<1e-5f,
    "flash follows current muzzle translation and physical hand rotation while retaining authored roll/offset");
   if(result){const auto again=renderMuzzleParticle(shot,handTurn,flash,1000,1100);
    check(again&&length(again->position-result->position)<1e-6f,"second eye recomputes from spawn and cannot accumulate a render delta");}
  }
  check(!renderMuzzleParticle(shot,shot,flash,1000,1251),"expired flash cannot be reattached");
  auto unrelated=flash;unrelated.position.x+=.1f;
  check(!renderMuzzleParticle(shot,shot,unrelated,1000,1100),"unmatched shot cannot move an effect");
 }
 {
  // Regression: a shifted animation camera must move the whole rendered body,
  // not be replaced by the stable camera before computing its rigid correction.
  const Vec view{10,20,3.7f},relativeHand{.25f,-.35f,-.2f};
  const Pose nativeModel{{10,20,2},{}};
  const std::array<Vec,4> joints{{{0,0,0},{0,0,.9f},{0,0,1.4f},{.2f,0,1.4f}}};
  for(Vec excursion:{Vec{},Vec{.44f,.1f,-.3f},Vec{-.4f,.2f,.15f}}){
   const Vec raw=view+excursion;
   const auto model=renderHandModel(nativeModel,raw,view);
   const auto localCamera=compose(inverse(nativeModel),Pose{raw,{}});
   check(length(compose(model,localCamera).position-view)<1e-5f,"animated camera joint lands exactly on rendered view");
   const Vec solvedHand=rebaseHandTarget(view+relativeHand,view,raw);
   const auto localHand=compose(inverse(nativeModel),Pose{solvedHand,{}});
   check(length(compose(model,localHand).position-view-relativeHand)<1e-5f,"rigid body correction preserves tracked hand offset");
   for(size_t i=1;i<joints.size();++i){
    const auto a=compose(model,Pose{joints[i-1],{}}),c=compose(model,Pose{joints[i],{}});
    check(std::abs(length(a.position-c.position)-length(joints[i-1]-joints[i]))<1e-5f,"whole-body placement cannot stretch spine or shoulders");
   }
   const auto old=renderHandModel(nativeModel,view,view);
   check(std::abs(length(compose(old,localCamera).position-view)-length(excursion))<1e-5f,"previous stable-camera input leaves native body displacement uncorrected");
  }
 }
 return errors?1:0;
}

