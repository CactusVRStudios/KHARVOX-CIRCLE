#include "interaction_policy.hpp"
#include "default_input.hpp"
#include "firearm_policy.hpp"
#include <cstdio>
using namespace circle::motor;
using namespace interaction;
int main(){
 int errors{};auto check=[&](bool b,const char* why){if(!b){++errors;std::printf("FAIL %s\n",why);}};
 {
  const std::array<float,12> native{10,20,1.6f,1,0,0,0,1,0,0,0,1};
  for(float yaw:{-2.f,0.f,1.5f})for(float pitch:{.5f,1.f,1.4f}){
   const Quat turn{0,0,std::sin(yaw/2),std::cos(yaw/2)};
   const Quat down{0,std::sin(pitch/2),0,std::cos(pitch/2)};
   const Pose head{{10.1f,20.2f,1.65f},multiply(turn,down)};
   const auto v=scriptViewTransform(7,7,native,head,{10,20,1.6f});
   check(v.has_value(),"local script view accepts tracked downward gaze");
   if(!v)continue;
   const Vec origin{(*v)[0],(*v)[1],(*v)[2]},forward{(*v)[3],(*v)[4],(*v)[5]};
   const Vec hit=origin+forward*(-origin.z/forward.z);
   const float reach=head.position.z/std::tan(pitch);
   check(forward.z<0&&length(hit-Vec{head.position.x+reach*std::cos(yaw),head.position.y+reach*std::sin(yaw),0})<1e-4f,
         "script ray intersects the looked-at floor platform rather than flat-camera horizon");
   auto q=rotationBasis(forward,{(*v)[6],(*v)[7],(*v)[8]},{(*v)[9],(*v)[10],(*v)[11]});
   check(q&&length(rotate(*q,{1,0,0})-forward)<1e-5f,"script angles and direction expose the same basis");
  }
  check(!scriptViewTransform(8,7,native,{},{}),"NPC script view stays native");
  check(!scriptViewTransform(0,0,native,{},{}),"missing player cannot acquire script view");
  check(!scriptViewTransform(7,7,native,{{10,20,1.6f},{}},{30,20,1.6f}),"foreign camera rejected");
  auto invalid=native;invalid[4]=NAN;
  check(!scriptViewTransform(7,7,invalid,{},{}),"invalid script camera rejected");
  invalid=native;invalid[3]=2;
  check(!scriptViewTransform(7,7,invalid,{},{}),"non-rotation script basis rejected");
 }
 check(passengerTargeting(1,5,0,false,false,1),"observed unarmed boat passenger permits head-directed focus");
 check(!passengerTargeting(1,0,0,false,false,1)&&passengerTargeting(24,5,0,false,false,1)&&
       !passengerTargeting(1,5,1,false,false,1)&&!passengerTargeting(1,5,0,true,false,1)&&
       !passengerTargeting(1,5,0,false,true,1),
       "seated targeting includes driver but excludes ordinary crouch and authored cameras");
 for(unsigned slot:{0u,1u,14u,18u})check(passengerTargeting(1,5,0,false,false,slot),"passenger focus survives equipment transitions");
 {
  check(ledgeTargeting(14,5,0,false,false),"observed ledge state enables VR target queries");
  check(!ledgeTargeting(14,0,0,false,false)&&!ledgeTargeting(14,5,1,false,false)&&
   !ledgeTargeting(14,5,0,true,false)&&!ledgeTargeting(14,5,0,false,true),"overridden and mismatched ledge states keep native targeting");
  for(unsigned mode:{0u,3u,4u,12u,13u,16u,23u,255u})check(!ledgeTargeting(mode,5,0,false,false),"other traversal states are not implicitly enabled");
  for(float yaw:{0.f,1.57f,3.14f}){
   const Pose visible{{194.55f,-130.44f,-8.26f},{0,0,std::sin(yaw/2),std::cos(yaw/2)}};
   const Vec base{194.54f,-130.44f,-8.26f},thirdPerson{197,-134,-7};
   const auto query=queryView(visible,base,thirdPerson,true);
   check(query&&length(query->position-visible.position)<1e-6f&&
    length(rotate(query->orientation,{1,0,0})-rotate(visible.orientation,{1,0,0}))<1e-6f,
    "owned hanging query uses rendered head position and turned gaze exactly once");
   check(!queryView(visible,base,thirdPerson),"ordinary foreign camera guard remains unchanged");
  }
  check(!queryView({}, {},{NAN,0,0},true),"invalid native query still rejected while hanging");
 }
 {
  for(int pose=23;pose<=31;++pose)check(hands::pickupFirearmPose(10,pose),"all native Great Circle firearm families supported in pickup slot");
  for(int pose:{-1,0,14,22,32,33,34,35,36,37})check(!hands::pickupFirearmPose(10,pose),"ordinary objects and unknown poses remain carrying");
  check(!hands::pickupFirearmPose(14,24),"reaching stage cannot become an equipped firearm");
 }
 {
  PhotoZoom z;check(z.update(40,true)==1,"unknown photo baseline retains native VR scale");
  check(z.update(90,false)==1,"ordinary camera records baseline without magnification");
  const float scale=z.update(45,true);
  check(std::abs(scale-.41421356f)<1e-5f,"photo zoom uses tangent ratio rather than degree ratio");
  const std::array<float,4> f{-.8f,.9f,.85f,-.75f};
  const auto zoomed=photoRenderFov(f,scale);
  for(unsigned i=0;i<4;++i)check(std::abs(std::tan(zoomed[i])-std::tan(f[i])*scale)<1e-5f,"all optical planes share photo magnification");
  check(photoRenderFov(f,1)==f&&photoRenderFov(f,NAN)==f,"default or invalid photo zoom does not alter optical planes");
  check(z.update(NAN,true)==1&&z.update(0,true)==1,"invalid native FOV cannot zoom VR");
  check(z.update(100,false)==1&&z.baseline==100,"unequip immediately restores ordinary view");
 }
 for(float yaw:{0.f,1.5f,3.f})for(float pitch:{-1.3f,0.f,1.3f}){
  const auto q=multiply(Quat{0,0,std::sin(yaw/2),std::cos(yaw/2)},Quat{0,std::sin(pitch/2),0,std::cos(pitch/2)});
  const Pose vr{{10.1f,20.2f,1.8f},q};const Vec base{10,20,1.8f},native{10.2f,20.1f,1.8f};
  const auto view=queryView(vr,base,native);
  check(view&&length(view->position-Vec{10.3f,20.3f,1.8f})<1e-5f,"target query rebases roomscale exactly once");
  if(view){
   const auto ray=rotate(q,{1,0,0});const auto overhead=view->position+ray*4;
   check(length((overhead-view->position)*.25f-rotate(view->orientation,{1,0,0}))<1e-5f,"overhead target cone and projected aim share VR pitch and yaw");
  }
 }
 check(!queryView({{},{0,0,0,0}},{},{}),"invalid VR query orientation retains native view");
 check(!queryView({}, {},{2,0,0}),"unrelated native camera cannot receive player gaze");
 check(!queryView({}, {},{NAN,0,0}),"invalid query camera retains native view");
 // Muzzle tags can have both a lateral offset and their own rotation. The
 // world ray must follow the visible barrel, not converge on the head ray.
 for(float yaw:{0.f,1.3f,3.1f})for(float pitch:{-.8f,0.f,.6f})for(float roll:{-.7f,0.f,.9f}){
  const auto q=multiply(multiply(Quat{0,0,std::sin(yaw/2),std::cos(yaw/2)},Quat{0,std::sin(pitch/2),0,std::cos(pitch/2)}),Quat{std::sin(roll/2),0,0,std::cos(roll/2)});
  const Pose model{{13,-7,1.7f},q},local{{.24f,.035f,.08f},{0,0,.09983342f,.99500417f}};
  const auto muzzle=hands::modelMuzzle(model,local);
  check(muzzle&&length(muzzle->position-(model.position+rotate(q,local.position)))<1e-5f,"asset muzzle offset follows full model rotation exactly once");
  if(muzzle)check(length(rotate(muzzle->orientation,{1,0,0})-rotate(q,rotate(local.orientation,{1,0,0})))<1e-5f,"local barrel orientation is retained across yaw pitch and roll");
  const Pose moved{{14,-5,2.7f},q};const auto next=hands::modelMuzzle(moved,local);
  check(muzzle&&next&&length((next->position-muzzle->position)-Vec{1,2,1})<1e-5f,"locomotion moves muzzle with model without a second physics delta");
 }
 check(!hands::modelMuzzle({{NAN,0,0},{}},{}),"invalid model retains native shot");
 check(!hands::modelMuzzle({},{{3,0,0},{}}),"invalid distant muzzle rejected");
 check(!hands::modelMuzzle({},{{},{0,0,0,0}}),"invalid muzzle rotation rejected");
 {
  hands::StableWeaponBinding pair;Pose initial{{0,.04f,.05f},{}},corrected{{.04f,.02f,.06f},{0,0,.2f,.9797959f}},ref{{0,.03f,.04f},{}};
  for(uint64_t t=1000;t<1200;t+=20)check(!pair.observe(initial,ref,t,true),"binding pair requires shared settling interval");
  check(pair.observe(initial,ref,1200,true)&&pair.ready,"first pair captured together");
  for(uint64_t t=1220;t<1620;t+=20)check(!pair.observe(corrected,ref,t,true),"replacement does not follow transient native animation");
  check(pair.observe(corrected,ref,1620,true)&&length(pair.value.position-corrected.position)<1e-6f,"later settled shooting grip repairs initial binding without death");
  check(!pair.observe(initial,ref,1640,false)&&pair.ready,"invalid observation retains last accepted grip");
  for(uint64_t t=1660;t<2060;t+=20)check(!pair.observe(initial,ref,t,true),"interrupted candidate needs full new interval");
  check(pair.observe(initial,ref,2060,true),"binding can adapt back to settled native grip");
  hands::StableWeaponBinding movingReference;
  for(uint64_t t=1000;t<1500;t+=20){auto moving=ref;moving.position.x=(t%40)?0:.01f;check(!movingReference.observe(initial,moving,t,true),"stable anchor with moving reference cannot capture mixed pair");}
  hands::StableWeaponGrip grip;
  for(unsigned t=1000;t<1500;t+=20)check(!grip.observe({{-.246f,.247f,-.04f},{}},t),"draw-animation 35cm grip cannot be calibrated");
  for(unsigned t=1500;t<1700;t+=20)check(!grip.observe({{0,.05f,.06f},{}},t),"grip needs continuous settling");
  check(grip.observe({{0,.05f,.06f},{}},1700),"settled native grip captured");
  check(grip.observe({{-.24f,.24f,0},{}},2000)&&length(grip.value.position-Vec{0,.05f,.06f})<1e-6f,"later draw or aim offset cannot replace accepted grip");
 }
 for(float yaw:{0.f,90.f,180.f,-90.f})for(float pitch:{-60.f,0.f,45.f}){
  Quat q=multiply({0,std::sin(yaw*locomotion::radians*.5f),0,std::cos(yaw*locomotion::radians*.5f)},
    {std::sin(pitch*locomotion::radians*.5f),0,0,std::cos(pitch*locomotion::radians*.5f)});
  const Pose controller{{13,-7,1.7f},q};auto fire=hands::motorFirePose(controller);
  check(fire&&length(fire->position-controller.position)<1e-6f,"fire pose retains tracked origin");
  if(fire)check(length(rotate(fire->orientation,{1,0,0})-rotate(q,{0,0,-1}))<1e-5f,"Motor shot axis follows controller across yaw/pitch");
 }
 check(!hands::motorFirePose({{NAN,0,0},{}}),"invalid controller cannot redirect a shot");
 {
  // A native weapon anchor need not coincide with its wrist helper. Preserve
  // the actual native grip under controller/world rotations, including the
  // admitted 8cm helper discrepancy that the previous binding discarded.
  for(float turn:{0.f,.6f,1.2f}){
   Pose wrist{{2,3,4},{0,0,std::sin(turn/2),std::cos(turn/2)}};
   Pose helper{{.025f,.042f,.048f},{}};
   Pose nativeGrip{{.065f,.022f,.058f},{0,std::sin(.2f),0,std::cos(.2f)}};
   const auto anchor=compose(wrist,nativeGrip);
   const auto measured=compose(inverse(wrist),anchor);
   const auto tracked=hands::firearmWristTarget(Pose{{10,20,30},{}},measured);
   check(tracked.has_value(),"actual native wrist-to-weapon transform accepted");
   if(tracked){
    const auto weapon=compose(*tracked,measured);
    check(length(compose(inverse(*tracked),weapon).position-nativeGrip.position)<1e-5f,"tracked pose preserves native grip rather than substituting wrist helper");
    check(length(weapon.position-compose(*tracked,helper).position)>.04f,"regression retains measured nonzero anchor/helper offset");
   }
  }
  hands::StableWeaponGrip melee,shooting;
  for(uint64_t t=1000;t<=1300;t+=20)melee.observe({{.025f,.042f,.048f},{}},t);
  for(uint64_t t=1400;t<=1700;t+=20)shooting.observe({{.06f,.02f,.05f},{}},t);
  check(melee.ready&&shooting.ready&&length(melee.value.position-shooting.value.position)>.03f,"shooting capture must not inherit settled melee grip");
 }
 check(hands::itemStrikeEligible(3,1,1,1,{0,1,0},true),"explicit firearm melee mode allows charged swing");
 check(hands::itemStrikeEligible(3,1,1,0,{0,1,0}),"upright revolver permits physical butt strike");
 check(hands::itemStrikeEligible(3,1,1,0,{0,0,-1})&&!hands::itemStrikeEligible(3,1,1,1,{0,1,0}),"any barrel angle permits melee but firing trigger excludes it");
 for(float angle:{0.f,.7f,1.5f}){
  Pose bind{{.04f,.02f,.06f},{0,std::sin(angle/2),0,std::cos(angle/2)}};
  Pose controller{{2,3,4},{0,0,.5f,.8660254f}};
  auto wrist=hands::firearmWristTarget(controller,bind);auto aim=hands::motorFirePose(controller);
  check(wrist&&length(wrist->position-controller.position)<1e-6f,"weapon binding keeps grip at controller");
  if(wrist&&aim){const auto gun=compose(*wrist,bind);
   check(length(rotate(gun.orientation,{1,0,0})-rotate(aim->orientation,{1,0,0}))<1e-5f,"rendered weapon axis equals shot axis for different native grip offsets");
  }
 }
 check(hands::itemStrikeEligible(8,1,1,0,{0,0,-1})&&!hands::itemStrikeEligible(18,1,1,0,{0,1,0})&&hands::itemStrikeEligible(8,0,1,0,{0,1,0}),"native item actions available to both hands; whip remains separate");
 // Physical rotations, tilted heads and room translation use the rendered
 // pose exactly once. A recenter already reflected in it needs no extra yaw.
 for(float yaw:{0.f,90.f,180.f,-90.f})for(float pitch:{-30.f,0.f,35.f}){
  Quat q=multiply({0,0,std::sin(yaw*locomotion::radians*.5f),std::cos(yaw*locomotion::radians*.5f)},
   {0,std::sin(pitch*locomotion::radians*.5f),0,std::cos(pitch*locomotion::radians*.5f)});
  const Vec f=rotate(q,{1,0,0}),l=rotate(q,{0,1,0}),u=rotate(q,{0,0,1}),origin{13,-7,1.7f};
  std::array<float,12> a{},b{};
  for(unsigned eye=0;eye<2;++eye){auto& c=eye?b:a;auto p=origin+l*(eye?-.032f:.032f);c={p.x,p.y,p.z,f.x,f.y,f.z,l.x,l.y,l.z,u.x,u.y,u.z};}
  auto view=centerView(a,b);check(view&&length(view->position-origin)<1e-5f,"interaction origin is stereo center, not a single eye");
  if(view)check(length(rotate(view->orientation,{1,0,0})-f)<1e-5f,"head-forward target follows physical rotation/pitch without double yaw");
  a[3]=NAN;check(!centerView(a,b),"invalid render pose keeps native focus");
 }
 check(nearUsable({1,2,3},{1.2f,2,3},.2f),"near slow hand eligible");
 check(!nearUsable({1,2,3},{1.3f,2,3},.2f)&&!nearUsable({1,2,3},{1,2,3},2.f)&&!nearUsable({NAN,0,0},{},0),"gaze alone, punch velocity and invalid pose cannot interact");
 DownwardPull pull;float axis{};
 for(unsigned step=0;step<=40;++step)axis=pull.update({0,-float(step)*.004f,0},1000+step*50,true,true);
 check(axis==-1,"slow 16cm downward pull activates stick-down");
 for(unsigned step=1;step<=80;++step)axis=pull.update({0,-.16f,0},3000+step*50,true,true);
 check(axis==-1,"stationary lowered hand preserves arbitrary hold duration");
 check(pull.update({0,-.16f,0},7050,true,false)==0,"grip release releases native direction");
 check(pull.update({0,-.16f,0},7100,true,true)==0,"new grip anchors anew without replay");
 pull={};pull.update({},8000,true,true);check(pull.update({-.2f,-.03f,0},8050,true,true)==0,"horizontal swipe cannot trigger downward hold");
 pull.update({0,-.2f,0},8100,true,true);check(pull.update({0,-.04f,0},8150,true,true)==0,"hand return releases hold");
 pull.update({0,-.2f,0},8200,true,true);check(pull.update({0,-.2f,0},8250,false,true)==0,"context exit releases hold");
 check(pull.update({0,-.3f,0},8300,true,true)==0,"context reentry cannot reuse displacement");
 pull.update({0,-.5f,0},8350,true,true);check(pull.update({0,-.5f,0},8501,true,true)==0,"stale tracking resets active hold");
 using namespace input;
 auto neutral=[](){Physical p;p.focused=true;p.session=1;p.hand[0].tracked=p.hand[1].tracked=true;return p;};
 {
  // Placement releases squeeze BEFORE entering GRIP. No squeeze is needed
  // for a direction, and squeezing again must not send Use/detach.
  Profile profile;auto p=neutral();profile.update(p,input::Context::Carrying,1000);
  profile.update(p,input::Context::Grip,1010);
  p.hand[1].grip=1;
  auto out=profile.update(p,input::Context::Grip,1020,3,0,120,-1);
  check(!(out.pad.buttons&X)&&out.pad.ly==-1&&!out.motion&&!out.physicalAttacks,"GRIP gesture does not detach, move player or punch");
  p.hand[0].lower=true;check(profile.update(p,input::Context::Grip,1030).pad.buttons&X,"manual X can still release scripted hold");
  p.hand[0].lower=false;p.hand[0].y=1;
  check(profile.update(p,input::Context::Grip,1040,0,-1).pad.ly==1,"native stick overrides hand gesture");
  check(profile.update(p,input::Context::Unarmed,1050,0,-1).pad.lx==0,"script direction cannot leak into gameplay");
 }
 {
  ScriptedDirections gesture;std::array<float,2> out{};
  for(unsigned step=0;step<=40;++step)out=gesture.update({0,-float(step)*.004f,0},{1,0,0},1000+step*50,true);
  check(out[1]==-1,"scripted downward pull works without squeeze after entry");
  for(unsigned step=1;step<=40;++step)out=gesture.update({0,-.16f,0},{1,0,0},3000+step*50,true);
  check(out[1]==-1,"scripted downward hold persists without repeated presses");
  check(gesture.update({0,-.03f,0},{1,0,0},5050,true)[1]==0,"returning the hand releases scripted hold");
  gesture.update({0,-.18f,0},{1,0,0},5100,true);
  check(gesture.update({0,-.18f,0},{1,0,0},5150,false)==std::array<float,2>{},"focus/menu/context loss releases direction");
  check(gesture.update({0,-.3f,0},{1,0,0},5200,true)==std::array<float,2>{},"reentry anchors without old movement");
  for(float sign:{-1.f,1.f}){
   gesture={};gesture.update({},{1,0,0},1000,true);
   for(unsigned i=1;i<=5;++i)out=gesture.update({sign*.036f*i,0,0},{1,0,0},1000+50*i,true);
   check(out[0]==sign,"left and right physical swipes produce native direction");
   check(gesture.update({sign*.18f,0,0},{1,0,0},1300,true)[0]==sign,"swipe pulse retains native duration");
   gesture.update({sign*.18f,0,0},{1,0,0},1350,true);gesture.update({sign*.18f,0,0},{1,0,0},1400,true);
   check(gesture.update({sign*.18f,0,0},{1,0,0},1450,true)[0]==0,"stationary hand does not repeat swipe");
   gesture.update({},{1,0,0},1500,true);
   for(unsigned i=1;i<=5;++i)out=gesture.update({sign*.036f*i,0,0},{1,0,0},1500+50*i,true);
   check(out[0]==sign,"hand return rearms swipe without cancelling interaction");
   check(gesture.update({sign*.18f,0,0},{1,0,0},2000,true)==std::array<float,2>{},"tracking gap cancels pulse");
  }
 }
 for(unsigned h=0;h<2;++h){
  Profile profile;auto p=neutral();profile.update(p,input::Context::Unarmed,1000);
  p.nearUsableHands=1u<<h;p.hand[h].grip=.8f;check(!(profile.update(p,input::Context::Unarmed,1010).pad.buttons&X),"partial fist cannot interact");
  p.hand[h].grip=.95f;check(profile.update(p,input::Context::Unarmed,1020).pad.buttons&X,"near same-hand fist closure interacts");
  p.nearUsableHands=0;
  for(unsigned t=1030;t<2030;t+=10)check(profile.update(p,input::Context::Unarmed,t,1u<<h).pad.buttons&X,"interact hold is continuous with no repeated edges");
  auto out=profile.update(p,input::Context::Unarmed,2040,1u<<h);check((h?out.pad.rt:out.pad.lt)==0,"interacting hand cannot also punch");
  check(profile.update(p,input::Context::Grip,2050).pad.buttons&X,"accepted grip interaction retains hold through scripted transition");
  p.hand[h].grip=0;check(!(profile.update(p,input::Context::Grip,2060).pad.buttons&X),"release during scripted hold is forwarded");
  profile.update(p,input::Context::Unarmed,2070);p.hand[h].grip=1;
  check(profile.update(p,input::Context::Unarmed,2080).pad.buttons&X,"fresh grip forwards gaze-based X without touching");
  p.nearUsableHands=1u<<h;check(profile.update(p,input::Context::Unarmed,2090).pad.buttons&X,"gaze press stays continuous during short pulse");
  check(!(profile.update(p,input::Context::Unarmed,2200).pad.buttons&X),"held fist cannot retrigger or extend pulse at a new near target");
  p.hand[h].grip=0;profile.update(p,input::Context::Unarmed,2210);p.hand[h].grip=1;
  check(profile.update(p,input::Context::Unarmed,2220).pad.buttons&X,"release and regrip produces fresh repeated press");
  p.focused=false;check(!(profile.update(p,input::Context::Unarmed,2230).pad.buttons&X),"focus loss releases interaction");
  p.focused=true;check(!(profile.update(p,input::Context::Unarmed,2240).pad.buttons&X),"held grip cannot replay on focus return");
 }
 Profile qte;auto p=neutral();qte.update(p,input::Context::Grip,1000);
 check(qte.update(p,input::Context::Grip,1010,0,0,120,-1).pad.ly==-1,"down gesture routes in native GRIP context");
 p.hand[0].y=.7f;auto out=qte.update(p,input::Context::Grip,1020,0,0,120,-1);check(out.pad.ly==.7f,"manual stick overrides gesture");
 p.hand[0].y=0;out=qte.update(p,input::Context::Grip,1030,0,-1,120,-1);check(out.pad.lx==-1&&out.pad.ly==0,"existing left swipe has priority, no diagonal injection");
 for(auto c:{input::Context::Unarmed,input::Context::Menu,input::Context::Inspection,input::Context::Unknown})check(qte.update(p,c,1040,0,0,120,-1).pad.ly==0,"script gesture never becomes normal locomotion or unknown-context input");
 qte.update(p,input::Context::Unarmed,1050);p.hand[0].lower=true;
 check(qte.update(p,input::Context::Unarmed,1060).pad.buttons&X,"manual X fallback still works after context neutral");
 for(unsigned conflict=0;conflict<5;++conflict){
  Profile profile;auto v=neutral();profile.update(v,input::Context::Unarmed,1000);v.nearUsableHands=2;v.hand[1].grip=1;
  if(conflict==0)v.rightWhip=true;
  if(conflict==1)v.hand[1].trigger=1;
  if(conflict==2)v.hand[1].click=true;
  if(conflict==3)v.hand[0].upper=true;
  if(conflict==4)v.hand[1].tracked=false;
  check(!(profile.update(v,input::Context::Unarmed,1020).pad.buttons&X),"whip/charge/cosmetic/modifier/tracking loss cannot start near-hand use");
 }
 return errors?1:0;
}
