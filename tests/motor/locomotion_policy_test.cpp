#include "locomotion_policy.hpp"
#include <cstdio>
#include <limits>
using namespace circle::motor::locomotion;
int main(){
 int errors=0;auto check=[&](bool b,const char* label){if(!b){++errors;std::fprintf(stderr,"FAIL %s\n",label);}};
 auto near=[](float a,float b){return std::abs(a-b)<.001f;};
 {
  PassengerCombatEpisode seat;
  check(!seat.update(7,1,true,true,false),"ordinary passenger remains on original path");
  check(seat.update(7,1,true,true,true),"equipped gun identifies combat passenger episode");
  check(seat.update(7,1,true,true,false),"holstering retains free tracked hands/body");
  check(!seat.update(7,1,false,false,false)&&seat.update(7,1,true,true,false),"menu transient cannot erase combat seat episode");
  check(!seat.update(7,1,true,false,false)&&!seat.update(7,1,true,true,false),"dismount resets before unrelated boat episode");
  seat.update(7,1,true,true,true);
  check(!seat.update(7,2,true,true,false),"new life/load revokes combat seat scope");
  seat.update(7,2,true,true,true);
  check(!seat.update(8,2,true,true,false),"another player owner cannot inherit combat seat scope");
 }
 {
  // Read-only r354 boat samples: camera/body agree while sampled physics lags.
  const Vec camera{-68.563232f,8.945294f,-1.133759f};
  const Vec physics{-68.269623f,9.300914f,-2.228973f};
  const Vec rendered{-68.522934f,8.963605f,-2.228547f};
  check(coherentPassengerBody(camera,physics,rendered),"moving passenger camera uses matching rendered body");
  check(coherentPassengerBody(camera,rendered,rendered),"source remains coherent after boat stops");
  check(!coherentPassengerBody(camera,physics,rendered+Vec{0,1,0}),"nearby foreign body cannot pass camera correlation");
  check(!coherentPassengerBody(camera,physics+Vec{3,0,0},rendered),"stale distant physics excluded");
  check(!coherentPassengerBody({NAN,0,0},physics,rendered),"nonfinite passenger frame excluded");
 }
 check(trackedFirearmState(1,5,0,false,false),"passenger muzzle and laser may follow tracked firearm");
 for(unsigned mode:{0u,1u,2u,9u,20u})check(trackedFirearmState(mode,0,0,false,false),"ordinary weapon pose contexts retained");
 for(unsigned mode:{0u,4u,10u,24u,27u,28u})check(!trackedFirearmState(mode,5,0,false,false),"unrelated scripted seats/traversals cannot gain handheld gun routing");
 check(!trackedFirearmState(1,5,1,false,false)&&!trackedFirearmState(1,5,0,true,false)&&!trackedFirearmState(1,5,0,false,true),"authored camera overrides retain ownership");
 {
  PassengerEntry seat;
  check(!seat.update(7,true,true,false),"seat entry waits for tracked/focused pose");
  check(seat.update(7,true,true,true),"initial seated load aligns without manual recenter");
  BodyYawFollow turn;turn.reset(-.77f);turn.artificialYaw=-169.52f;
  turn.reset(-.77f);
  check(near(turn.artificialYaw,0),"entry removes inherited rearward turn");
  check(!seat.update(7,true,true,true),"subsequent frames preserve free look and stick yaw");
  check(!seat.update(7,false,false,true),"menu/stale native state cannot end passenger episode");
  check(!seat.update(7,true,true,true),"return from menu cannot recenter again");
  check(!seat.update(7,true,true,false),"tracking loss keeps seat episode");
  check(!seat.update(7,true,true,true),"tracking recovery preserves chosen heading");
  check(!seat.update(7,true,false,true),"confirmed dismount rearms entry");
  check(seat.update(7,true,true,true),"boarding again aligns once");
  check(seat.update(8,true,true,true),"new seated actor gets its own reference");
 }
 {
  const Vec physics{273.90546f,515.02478f,12.98565f};
  const Vec body{273.03662f,515.49518f,12.98596f};
  const Vec eye{273.02484f,515.50751f,14.62336f};
  check(coherentRenderBody(0,eye,physics,body),"measured dismount camera belongs to visible body despite displaced collision origin");
  check(!coherentRenderBody(0,eye,physics,physics),"physics origin cannot impersonate the rendered body");
  check(!coherentRenderBody(0,eye,physics,body+Vec{0,0,1}),"different-height actor rejected");
  check(!coherentRenderBody(27,eye,physics,body),"mounted camera does not enter ordinary render-body recovery");
  check(!coherentRenderBody(0,eye,body,body),"normal aligned gameplay retains existing physics anchor");
  CameraAnchorState anchor;
  auto pose=anchor.update(7,1,eye,body,0,true,true,0,1000);
  const auto result=pose.place(eye,body,0);
  check(near(result.x,eye.x)&&near(result.y,eye.y),"camera stays over visible body instead of jumping to collision body");
 }
 {
  GroundFrame frame{{1,0,0},{0,-1,0},{0,0,1},1,true};
  RoomscaleView view;RoomscaleFollow follow;Vec head{},body{};
  view.update(7,3,head,body,frame,0,true,100);follow.reset(7,body,head);
  head.z=-.08f;body.x=.1f;
  follow.update(7,body,head,frame,0,frame.forward,frame.right,true,true);
  view.update(7,3,head,body,frame,0,true,116);
  follow.useViewReference(view,7,3);
  auto command=follow.update(7,body,head,frame,0,frame.forward,frame.right,false,true);
  check(command.y>0,"releasing manual movement follows the displacement actually shown by the camera");
  body.x+=.04f;follow.useViewReference(view,7,3);
  follow.update(7,body,head,frame,0,frame.forward,frame.right,false,true);
  const auto offset=view.update(7,3,head,body,frame,0,true,132,follow.acceptedStep);
  check(near(offset.x,.04f)&&near(view.position.x,.18f),"shared reference consumes accepted physical travel once without moving rendered head");
  const auto origin=follow.origin;
  follow.useViewReference(view,8,3);follow.useViewReference(view,7,4);
  check(length(follow.origin-origin)==0,"another player or reference epoch cannot supply movement origin");
  follow.valid=false;view.update(7,3,head,body,frame,0,false,148);
  head.x=.15f;follow.reset(7,body,head);
  check(length(view.update(7,3,head,body,frame,0,true,164))==0,"scripted/focus return cannot retain a camera-only reference");
 }
 {
  GroundFrame frame{{1,0,0},{0,-1,0},{0,0,1},1,true};
  RoomscaleView view;const Vec body{910,-905,.5f};
  view.update(1,1,{},body,frame,0,true,100);
  for(unsigned i=1;i<=4;++i)view.update(1,1,{i*.1f,0,0},body,frame,0,true,100+i*16);
  for(unsigned i=1;i<=4;++i)view.update(1,1,{.4f-i*.1f,0,0},body,frame,0,true,164+i*16);
  check(length(view.position-body)<.001f,"lean past collision limit then return cannot store permanent camera offset");
  view={};view.update(1,1,{},body,frame,0,true,300);
  view.update(1,1,{.1f,0,0},body,frame,0,true,316);
  view.update(1,1,{},body,frame,90,true,332);
  check(length(view.position-body)<.001f,"closed headset motion across artificial turn cannot accumulate world-space drift");
  view={};view.update(1,1,{},body,frame,0,true,400);
  view.update(1,1,{},body+Vec{.1f,0,0},frame,0,true,416);
  check(length(view.position-body-Vec{.1f,0,0})<.001f,"native body motion without acknowledged roomscale translation cannot detach camera");
 }
 {
  CombatCameraEpisode combat;
  combat.observe(7,17,8,-2,true,100);
  check(combat.active(7,100),"accepted heavy strike protects camera before hit event");
  combat.observe(7,0,8,1660,false,320);
  check(combat.active(7,500),"IDLE action with running strike node retains combat protection");
  combat.observe(7,0,8,-1,false,600);
  check(combat.active(7,600),"animation recovery keeps bounded protection");
  combat.observe(7,0,8,1660,false,800);
  combat.observe(7,0,8,-1,false,700);
  check(combat.active(7,810),"out of order worker sample cannot erase combat protection");
  for(unsigned t=900;t<=1900;t+=100)combat.observe(7,0,8,-1,false,t);
  check(!combat.active(7,1900),"idle nodes cannot permanently inhibit camera learning");
  combat.observe(7,5,9,-2,true,2000);
  check(!combat.active(7,2300)&&!combat.active(8,2100),"stale combat sample and other player do not own camera");
  combat.observe(7,34,10,-2,true,2400);
  check(!combat.active(7,2400),"new noncombat action releases episode");
  combat.observe(8,17,1,-2,true,2500);
  check(combat.active(8,2500),"new player can start separate episode");
  for(unsigned t=2600;t<=14000;t+=100)combat.observe(8,0,1,1660,false,t);
  check(!combat.active(8,14000),"stuck animation node has bounded lifetime");
  check(!cameraReferenceChanged(true,7,7),"same actor displacement never requests full recenter");
  check(cameraReferenceChanged(false,7,7)&&cameraReferenceChanged(true,7,8),"startup and actor changes still establish headset reference");
  for(unsigned mode:{0u,1u,2u,9u}){
   check(nativeCombatCamera(mode,5,0,false,false),"accepted strike keeps view ownership through controlled posture blend");
   check(!nativeCombatCamera(mode,5,1,false,false)&&!nativeCombatCamera(mode,5,0,true,false)&&!nativeCombatCamera(mode,5,0,false,true),"authored camera overrides remain excluded from combat protection");
  }
  check(!nativeCombatCamera(23,0,0,false,false)&&!nativeCombatCamera(14,5,0,false,false)&&!nativeCombatCamera(0,7,0,false,false),"combat cannot claim book or traversal cameras");
 }
 {
  GroundFrame frame{{1,0,0},{0,-1,0},{0,0,1},1,true};
  RoomscaleView view;Vec body{10,20,0},head{};
  view.update(1,1,head,body,frame,0,true,100);
  // A small physical lean is real and must survive the fix. Then a native
  // strike advances the capsule inside the old 150 ms follow window.
  head.x=.03f;const auto lean=view.update(1,1,head,body,frame,0,true,116);
  const auto before=view.position;body.x+=.16f;
  const auto offset=view.update(1,1,head,body,frame,0,true,132);
  check(length(offset-lean)<.001f&&near(view.position.x,before.x+.16f),"heavy strike cannot become permanent negative head offset during follow");
  body.x+=.02f;
  check(length(view.update(1,1,head,body,frame,0,true,148)-lean)<.001f,"native recovery motion preserves real head lean without a follow tail");
  head.x=0;
  check(length(view.update(1,1,head,body,frame,0,true,164))<.001f,"returning actual head to start restores offset without opening book");
  for(unsigned t=180;t<500;t+=16)view.update(1,1,head,body,frame,0,true,t);
  check(length(view.position-body)<.001f,"no residual remains after heavy attack at rest");
 }
 {
  CameraAnchorState saved;
  for(unsigned t=100;t<=1200;t+=100)saved.update(7,1,{-.04f,.02f,1.65f},{},0,true,true,0,t);
  saved.suspend();
  auto a=saved.update(7,1,{.4f,0,1.8f},{},90,true,true,0,1300,false);
  check(a.active&&a.stableHeight&&near(a.eyeHeight,1.65f)&&near(a.worldOffset.x,-.04f),"trusted camera restores even while return joint exceeds bootstrap distance");
  a=saved.update(7,1,{1.4f,0,1.8f},{1,0,0},90,true,true,0,1400,false);
  check(a.active&&a.stableHeight&&near(a.eyeHeight,1.65f)&&near(a.place({1.4f,0,1.8f},{1,0,0},90).x,.96f),"same player lunge keeps reference while following physics");
  saved.suspend();
  check(!saved.update(8,1,{.4f,0,1.8f},{},90,true,true,0,1500,false).active,"stored offset cannot bootstrap another player from detached camera");
 }
 {
  GroundFrame frame{{1,0,0},{0,-1,0},{0,0,1},1,true};
  RoomscaleView view;
  const Vec origin{10,20,3};
  view.update(1,1,{},origin,frame,0,true,100);
  // A moving headset with a body that follows in alternating lateral steps.
  for(unsigned i=1;i<=120;++i){
   const float phase=i*.12f;
   const Vec head{.06f*std::sin(phase),0,-.05f*std::cos(phase)};
   const Vec physics=origin+Vec{.08f*std::sin(phase-.8f),.07f*std::cos(phase),0};
   const auto step=physics-view.previousPhysics;
   // Supply the confirmed step explicitly; raw physics alone is not enough.
   const auto offset=view.update(1,1,head,physics,frame,0,true,100+i*16,{-step.y,0,-step.x});
   const auto expected=origin+frame.trackingToWorld(head,0);
   check(length(physics+offset-expected)<.001f,"body stepping cannot displace current headset position");
  }
  const auto oldPosition=view.position;const auto oldHead=view.previousHead;const auto oldPhysics=view.previousPhysics;
  const Vec walk{.1f,.02f,0};
  auto offset=view.update(1,1,oldHead,oldPhysics+walk,frame,0,true,2036);
  check(length(oldPhysics+walk+offset-oldPosition-walk)<.001f,"manual walking advances camera without losing existing head offset");
  const Vec pushed{.02f,0,0};
  view.update(1,1,oldHead,oldPhysics+walk,frame,0,true,2100);
  view.update(1,1,oldHead,oldPhysics+walk,frame,0,true,2180);
  const auto beforePush=view.position;
  offset=view.update(1,1,oldHead,oldPhysics+walk+pushed,frame,0,true,2200);
  check(length(oldPhysics+walk+pushed+offset-beforePush-pushed)<.001f,"native translation outside follow window remains effective");
  for(unsigned i=1;i<=20;++i){
   offset=view.update(1,1,oldHead+Vec{float(i)*.05f,0,0},oldPhysics+walk+pushed,frame,0,true,2200+i*16);
   check(length(offset)<=.20001f,"blocked body cannot permit unbounded camera travel through wall");
  }
  check(length(view.update(2,1,{},origin,frame,0,true,2600))==0,"new actor resets render feedback");
  check(length(view.update(2,2,{},origin,frame,0,true,2616))==0,"recenter resets render feedback");
  check(length(view.update(2,2,{},origin,frame,0,false,2632))==0&&!view.owner,"scripted ownership clears render feedback");
  view.update(1,1,{},origin,frame,0,true,2700);
  check(length(view.update(1,1,{},origin+Vec{2,0,0},frame,0,true,2716))==0,"teleport rebases instead of opposing physics");
  check(length(view.update(1,1,{},origin,frame,0,true,2900))==0,"stale sampling rebases");
  check(length(view.update(1,1,{},origin,frame,0,true,2800))==0,"clock reversal rebases");
  CameraAnchor anchor{true,{},true,1.6f};anchor.trackingOffset={.05f,-.03f,0};
  const auto rendered=anchor.place({0,0,1.6f},{},0);
  const auto skeleton=anchor.placeForSkeleton({0,0,1.6f},{},0);
  check(near(rendered.x,.05f)&&near(rendered.y,-.03f)&&near(skeleton.x,0)&&near(skeleton.y,0),"head residual is a render translation, not extra arm reach");
 }
 {
  GroundFrame frame{{1,0,0},{0,-1,0},{0,0,1},1,true};
  RoomscaleFollow follow;RoomscaleView view;Vec body{},head{};unsigned tick=100;
  auto sample=[&](bool manual=false){
   follow.update(1,body,head,frame,0,frame.forward,frame.right,manual,true);
   return view.update(1,1,head,body,frame,0,true,tick+=16,follow.acceptedStep);
  };
  sample();head.z=-.1f;sample();body.x=.04f;
  auto offset=sample();
  check(near(offset.x,.06f)&&near(body.x+offset.x,.1f),"confirmed roomscale step removes only already-followed physical head motion");
  sample();check(length(follow.acceptedStep)==0&&near(view.position.x,.1f),"same native position cannot acknowledge a step twice");
  body.y=.05f;sample();
  check(near(view.position.y,.05f)&&length(follow.acceptedStep)==0,"perpendicular door push cannot become roomscale acknowledgement");
  body.x+=.11f;offset=sample();
  check(length(follow.acceptedStep)<=.06001f&&near(view.position.x,body.x+.006f),"root motion cannot cancel beyond outstanding real headset displacement");
  body.x+=.1f;sample(true);
  check(length(follow.acceptedStep)==0&&near(view.position.x,body.x+.006f),"manual walk advances body and cannot replay previous acknowledgement");
  // Suspension/re-entry after a cutscene must not replay a prior accepted step.
  view.update(1,1,head,body,frame,0,false,tick+=16);
  body.x+=3;follow.reset(1,body,head);offset=sample();
  check(length(offset)==0&&length(follow.acceptedStep)==0,"scripted movement and resumed gameplay establish fresh positional feedback");
  head.z-=.03f;offset=sample();
  check(near(offset.x,.03f),"real physical lean remains available after cutscene return");
 }
 for(unsigned mode:{0u,1u,2u,9u}){
  check(nativeWhipAttached(mode,7,0,false,false),"attached whip retains headset camera");
  std::array<float,12> camera{1,2,3,0,0,1,1,0,0,0,1,0};
  const float entryYaw=35,artificialYaw=60,nativeYaw=-120;
  levelGameplayCamera(camera,nativeYaw);
  const auto forward=rotateWorldYaw({camera[3],camera[4],camera[5]},-(wrap(nativeYaw-entryYaw)-artificialYaw));
  check(near(forward.z,0)&&near(forward.x,std::cos((entryYaw+artificialYaw)*radians))&&
        near(forward.y,std::sin((entryYaw+artificialYaw)*radians)),"whip body heading and tilt cannot redirect VR view");
  check(near(camera[0],1)&&near(camera[1],2)&&near(camera[2],3)&&near(camera[11],1),"leveling retains translation and world up");
 }
 for(const auto state:std::array<std::array<int,2>,12>{{{0,7},{1,7},{2,7},{9,7},{0,0},{3,5},{4,5},{10,5},{12,7},{13,7},{14,5},{16,5}}}){
  check(headsetViewState(state[0],state[1],0,false,false),"all supported immersive states retain heading reference");
  check(!headsetViewState(state[0],state[1],1,false,false)&&!headsetViewState(state[0],state[1],0,true,false)&&
        !headsetViewState(state[0],state[1],0,false,true),"authored overrides retain camera ownership");
 }
 {
  StairHeight stair;
  check(near(stair.update(1,1,{0,0,0},0,true,true,100),0),"stairs initialize without camera jump");
  const auto first=stair.update(1,1,{.02f,0,.18f},0,true,true,116);
  check(first<-.1f&&first>-.18f,"step height eases instead of snapping");
  float residual=first;for(uint64_t t=132;t<700;t+=16)residual=stair.update(1,1,{.02f,0,.18f},0,true,true,t);
  check(std::abs(residual)<.001f,"stair correction converges without permanent drift");
  check(stair.update(1,1,{.04f,0,.4f},0,false,true,710)==0,"airborne movement has no smoothing delay");
  check(stair.update(1,1,{.04f,0,.4f},0,true,true,726)==0,"landing reinitializes cleanly");
  check(stair.update(1,1,{.05f,0,.5f},1,true,true,742)==0,"crouch transition resets smoothing");
  check(stair.update(2,1,{.05f,0,.6f},1,true,true,758)==0,"actor change resets smoothing");
  check(stair.update(2,2,{.05f,0,.7f},1,true,true,774)==0,"recenter resets smoothing");
  check(stair.update(2,2,{2,0,2},1,true,true,790)==0,"teleport remains immediate");
  CameraAnchor a{true,{0,0,0},true,1.6f};a.visualHeightOffset=.1f;
  const auto eye=a.place({0,0,1.6f},{0,0,0},0);
  check(near(eye.z,1.7f),"camera lift is exactly ten centimetres and is not accumulated");
  check(near(a.place(eye,{0,0,0},0).z,1.7f),"repeated anchor placement does not double lift");
 }
 const auto climbAnchor=ledgeBlendAnchor({1,2,3},{0,-.2f,1.27f},0);
 check(near(climbAnchor.x,1)&&near(climbAnchor.y,1.8f)&&near(climbAnchor.z,4.27f),"pull-up anchor follows fresh body translation without outside-camera blend");
 const auto turnedClimb=ledgeBlendAnchor({1,2,3},{0,-.2f,1.27f},90);
 check(near(turnedClimb.x,1.2f)&&near(turnedClimb.y,2),"pull-up head offset remains body-local");
 check(traversalCameraMatches({1,2,3},{1.01f,2,3}),"aligned render camera allows traversal handoff");
 TraversalHandoff handoff;
 check(!handoff.update(1000,true),"single aligned blend frame must retain bridge");
 check(!handoff.update(1016,false),"camera mismatch interrupts handoff");
 for(uint64_t t=1032;t<1282;t+=16)check(!handoff.update(t,true),"handoff requires continuous alignment");
 check(handoff.update(1282,true),"stable native camera completes handoff");
 handoff.reset();check(!handoff.update(2000,true)&&!handoff.update(2250,true),"missing camera samples cannot complete handoff");
 handoff.reset();check(!handoff.update(3000,true)&&!handoff.update(2990,true),"clock reversal restarts handoff");
 check(!traversalCameraMatches({1,2,3},{1,2.06f,3}),"FPS readiness alone does not end traversal bridge");
 check(!traversalCameraMatches({1,2,3},{1,2,std::numeric_limits<float>::quiet_NaN()}),"invalid FPS position rejects handoff");
 check(nativeRopeSlide(16,5,0,false,false),"recorded rope slide accepted");
 check(!nativeRopeSlide(12,5,0,false,false)&&!nativeRopeSlide(16,7,0,false,false)&&!nativeRopeSlide(16,5,1,false,false)&&!nativeRopeSlide(16,5,0,true,false)&&!nativeRopeSlide(16,5,0,false,true),"rope slide rejects unrelated and overridden cameras");
 check(nativeLadder(4,5,0,false,false),"measured ladder admitted");
 check(nativeGripCamera(10,5,0,false,false),"measured chain interaction admits native FPS camera");
 check(!nativeGripCamera(28,5,0,false,false)&&!nativeGripCamera(23,0,0,false,false)&&
       !nativeGripCamera(10,0,0,false,false)&&!nativeGripCamera(10,5,1,false,false)&&
       !nativeGripCamera(10,5,0,true,false)&&!nativeGripCamera(10,5,0,false,true),
       "chain camera rejects unverified grip, inspection and explicit camera overrides");
 check(ledgeEyePosition({-150.8280f,-16.8177f,12.9977f},{-150.966217f,-17.001982f,11.439651f}),
       "live chain FPS anchor is inside accepted head bounds");
 check(!nativeLadder(6,5,0,false,false)&&!nativeLadder(4,0,0,false,false)&&!nativeLadder(4,5,1,false,false)&&!nativeLadder(4,5,0,true,false)&&!nativeLadder(4,5,0,false,true),"ladder excludes unverified modes and camera overrides");
 check(nativeLedgeHang(14,5,0,false,false),"measured ledge hang accepted");
 check(whipSwingRelease(12,0)&&whipSwingRelease(12,5),"swing physics handoff continuation");
 check(!whipSwingRelease(10,5)&&!whipSwingRelease(23,0)&&!whipSwingRelease(12,8),"unrelated scripted states do not extend swing");
 check(nativeWhipSwing(12,7,0,false,false),"measured gap swing accepted");
 check(!nativeWhipSwing(12,5,0,false,false)&&!nativeWhipSwing(12,7,1,false,false)&&!nativeWhipSwing(12,7,0,true,false)&&!nativeWhipSwing(12,7,0,false,true),"swing rejects unrelated and overridden camera states");
 check(nativeWhipClimb(13,7,0,false,false),"measured whip climb accepted");
 check(!nativeWhipClimb(13,5,0,false,false)&&!nativeWhipClimb(0,7,0,false,false)&&
       !nativeWhipClimb(13,7,1,false,false)&&!nativeWhipClimb(13,7,0,true,false)&&
       !nativeWhipClimb(13,7,0,false,true),"only measured whip climb owns immersive camera");
 check(ledgeEyePosition({64.2803f,57.1061f,21.1923f},{64.023018f,56.946148f,20.093035f}),"live whip head origin is plausible");
 for(unsigned mode:{0u,6u,13u,23u})check(!nativeLedgeHang(mode,5,0,false,false),"other modes retain native camera ownership");
 check(!nativeLedgeHang(14,0,0,false,false)&&!nativeLedgeHang(14,5,1,false,false)&&!nativeLedgeHang(14,5,0,true,false)&&!nativeLedgeHang(14,5,0,false,true),"scripted overrides reject ledge camera");
 check(ledgeEyePosition({.02f,-.2f,1.27f},{0,0,0}),"measured head anchor accepted");
 check(!ledgeEyePosition({0,2,1.3f},{0,0,0})&&!ledgeEyePosition({0,0,0},{0,0,0}),"outside or missing head anchor rejected");
 // An animation-only lateral camera displacement must not become a VR step.
 {
  CameraAnchorState vertical;
  auto a=vertical.update(1,1,{0,0,1.65f},{0,0,0},0,true,true,0,100);
  check(!a.stableHeight,"entry allows native stance to settle");
  a=vertical.update(1,1,{0,0,1.65f},{0,0,0},0,true,true,0,600);
  for(float bob:{-.08f,0.f,.08f}){
   a=vertical.update(1,1,{0,0,1.65f+bob},{0,0,0},0,true,true,0,620);
   check(near(a.place({0,0,1.65f+bob},{0,0,0},0).z,1.65f),"vertical gait animation removed");
  }
  a=vertical.update(1,1,{0,0,2.1f},{0,0,.3f},0,true,true,0,640);
  check(near(a.place({0,0,2.1f},{0,0,.3f},0).z,1.95f),"jump and stair physics height retained without animation offset");
  a=vertical.update(1,1,{0,0,1.3f},{0,0,.3f},0,true,true,1,660);
  check(!a.stableHeight&&near(a.place({0,0,1.3f},{0,0,.3f},0).z,1.3f),"crouch transition keeps native height");
  a=vertical.update(1,1,{0,0,1.1f},{0,0,.3f},0,true,true,1,1160);
  check(a.stableHeight&&near(a.eyeHeight,.8f),"crouch establishes independent height");
  check(!vertical.update(1,1,{0,0,1.1f},{0,0,.3f},0,false,true,14,1170).active,"ledge and script ownership reset height lock");
  a=vertical.update(2,2,{0,0,1.7f},{0,0,0},0,true,true,0,1180);
  check(!a.stableHeight,"new owner cannot inherit crouched height");
 }
 {
  CameraAnchorState vault;
  vault.update(1,1,{.23f,.1f,1.65f},{0,0,0},0,true,true,0,100);
  vault.update(1,1,{-.06f,0,1.65f},{0,0,0},0,true,true,0,200);
  auto a=vault.update(1,1,{-.06f,0,1.65f},{0,0,0},0,true,true,0,900);
  check(near(a.localOffset.x,.23f),"vault offset does not follow short animation samples");
  a=vault.update(1,1,{-.06f,0,1.65f},{0,0,0},0,true,true,0,1000);
  check(near(a.localOffset.x,-.06f)&&near(a.localOffset.y,0)&&vault.repairs==1,"settled native camera repairs stale vault anchor");
  for(unsigned t=1100;t<=3100;t+=100){
   float bob=(t/100)%2?.07f:-.07f;
   a=vault.update(1,1,{-.06f+bob,0,1.65f},{0,0,0},0,true,true,0,t);
  }
  check(near(a.localOffset.x,-.06f)&&vault.repairs==1,"oscillating gait cannot replace recovered anchor");
  for(unsigned t=3200;t<=5200;t+=100){
   float p=float(t-3200)*.0001f;
   a=vault.update(1,1,{p+.15f,0,1.65f},{p,0,0},0,true,true,0,t);
  }
  check(near(a.localOffset.x,-.06f),"moving body cannot calibrate animation lag into anchor");
  check(!vault.update(1,1,{0,0,1.65f},{0,0,0},0,false,true,14,5300).active,"script camera retains ownership");
 }
 {
  CameraAnchorState v;
  v.update(1,1,{-.06f,0,1.65f},{0,0,0},0,true,true,0,100);
  v.update(1,1,{-.06f,0,1.65f},{0,0,0},0,true,true,0,200);
  v.update(1,1,{-.06f,0,1.65f},{0,0,0},0,true,true,0,1000);
  v.suspend();
  check(!v.update(1,1,{2,0,1},{1,0,0},0,false,true,3,1100).active,"traversal retains native ownership");
  auto a=v.update(1,1,{1.23f,0,1.65f},{1,0,0},0,true,true,0,1200);
  check(near(a.localOffset.x,-.06f),"returning blend cannot replace settled neck anchor");
  a=v.update(1,1,{1.33f,0,1.65f},{1.1f,0,0},0,true,true,0,1300);
  check(near(a.place({1.33f,0,1.65f},{1.1f,0,0},0).x,1.04f),"moving immediately after vault preserves body-local camera");
  v.suspend();a=v.update(1,2,{.1f,0,1.65f},{0,0,0},0,true,true,0,1400);
  check(near(a.localOffset.x,.1f),"recenter rejects old trusted anchor");
  v.suspend();a=v.update(2,2,{.12f,0,1.65f},{0,0,0},0,true,true,0,1500);
  check(near(a.localOffset.x,.12f),"different actor rejects trusted anchor");
  v.suspend();a=v.update(1,1,{.15f,0,1.65f},{0,0,0},0,true,true,0,10000);
  check(near(a.localOffset.x,.15f),"return to old actor cannot resurrect invalidated reference");
 }
 CameraAnchorState anchor;
 {
  CameraAnchorState combat;
  for(unsigned t:{100u,200u,1000u})
   combat.update(1,1,{-.06f,.03f,1.65f},{},0,true,true,0,t);
  // A heavy attack briefly yields camera ownership, then holds a plausible
  // but displaced native joint pose. Neither stillness nor yaw makes it neutral.
  combat.update(1,1,{.2f,.1f,1.5f},{},0,false,true,0,1100);
  auto a=combat.update(1,1,{.2f,.1f,1.5f},{},90,true,true,0,1200);
  check(a.stableHeight&&near(a.eyeHeight,1.65f),"short combat interruption restores verified eye height immediately");
  check(near(a.worldOffset.x,-.06f)&&near(a.worldOffset.y,.03f),"combat return with changed yaw cannot orbit verified camera");
  for(unsigned t=1300;t<=6000;t+=100)
   a=combat.update(1,1,{.2f,.1f,1.5f},{},90,true,true,0,t);
  auto eye=a.place({.2f,.1f,1.5f},{},90);
  check(near(eye.x,-.06f)&&near(eye.y,.03f)&&near(eye.z,1.65f)&&combat.repairs==0,
        "stationary attack recovery cannot recalibrate verified neck or height");
  a=combat.update(1,1,{.2f,.1f,.9f},{},90,true,true,1,6100);
  check(!a.stableHeight,"combat protection still permits crouch transition");
  a=combat.update(1,1,{.2f,.1f,.9f},{},90,true,true,1,6700);
  check(a.stableHeight&&near(a.eyeHeight,.9f),"new posture establishes its own height");
  combat.suspend();
  a=combat.update(1,1,{.1f,0,1.7f},{},0,true,true,0,13000);
  check(a.stableHeight&&near(a.eyeHeight,1.65f)&&near(a.localOffset.x,-.06f),"long interruption restores standing reference independently of last crouch");
  // Repeated strong attacks can yield an unavailable owner sample and a long
  // held pose. Missing telemetry is not evidence of a different actor.
  combat.update(0,1,{},{},0,false,true,0,14000,false);
  check(combat.trustedOwner==1,"missing owner sample suspends without erasing known body dimensions");
  combat.update(1,1,{.2f,.1f,1.9f},{},0,false,true,0,20000,false);
  a=combat.update(1,1,{.2f,.1f,1.9f},{},180,true,true,0,21000,true);
  check(a.stableHeight&&near(a.eyeHeight,1.65f)&&near(a.worldOffset.x,-.06f)&&near(a.worldOffset.y,.03f),"long heavy attack and missing sample cannot teach recovery joint pose");
  a=combat.update(1,1,{.02f,0,1.7f},{},0,true,true,0,100,true);
  check(!a.stableHeight&&!combat.trustedOwner&&near(a.localOffset.x,.02f),"clock reset clears active and trusted calibration together");
 }
 {
  CameraAnchorState traversal;
  for(unsigned t:{100u,200u,1000u})
   traversal.update(7,1,{-.06f,.03f,1.65f},{},0,true,true,0,t,true);
  // Reproduce the observed 12+ second whip/climb episode, then a moving,
  // elevated recovery camera. Suspension duration is not an identity change.
  for(unsigned t=1100;t<18000;t+=100)
   traversal.update(7,1,{.2f,.1f,2.f},{},90,false,true,13,t,false);
  auto a=traversal.update(7,1,{10.2f,20.1f,6.89f},{10,20,5},90,true,true,0,18000,false);
  check(a.stableHeight&&near(a.eyeHeight,1.65f)&&near(a.worldOffset.x,-.06f)&&near(a.worldOffset.y,.03f),"long whip sequence restores original neck and height before landing");
  for(unsigned t=18100;t<22000;t+=100){
   const Vec body{10,20,5+float(t-18100)*.001f};
   a=traversal.update(7,1,body+Vec{.2f,.1f,1.89f},body,135,true,true,0,t,false);
   check(near(a.place(body+Vec{.2f,.1f,1.89f},body,135).z,body.z+1.65f),"airborne physics follows body without learning animated eye height");
  }
  for(unsigned t=22000;t<=24000;t+=100)
   a=traversal.update(7,1,{.2f,.1f,1.89f},{},135,true,true,0,t,true);
  check(near(a.eyeHeight,1.65f)&&traversal.repairs==0,"stationary landing pose cannot replace trusted calibration");
  // First entering a crouch still uses native transition height. Once learned,
  // returning to a known standing posture must not overwrite standing height.
  for(unsigned t=24100;t<=25200;t+=100)
   a=traversal.update(7,1,{0,0,.9f},{},135,true,true,1,t,true);
  check(a.stableHeight&&near(a.eyeHeight,.9f),"separate crouch baseline is learned");
  a=traversal.update(7,1,{0,0,1.89f},{},135,true,true,0,25300,true);
  check(!a.stableHeight,"standing transition retains native blend");
  a=traversal.update(7,1,{0,0,1.89f},{},135,true,true,0,25900,true);
  check(a.stableHeight&&near(a.eyeHeight,1.65f),"return to standing recovers stored height rather than landing joint");
  traversal.suspend();a=traversal.update(7,2,{0,0,1.7f},{},0,true,true,0,26000,true);
  check(!a.stableHeight&&!traversal.postureKnown[0],"recenter generation invalidates prior stance heights");
 }
 {
  CameraAnchorState entry;
  for(unsigned t=100;t<=3000;t+=100)
   entry.update(1,1,{.2f,0,1.9f},{},0,true,true,0,t,false);
  check(!entry.anchor.stableHeight&&!entry.trustedOwner,"no initial height calibration from a suspended/airborne pose");
  for(unsigned t=3100;t<=4100;t+=100)
   entry.update(1,1,{0,0,1.65f},{},0,true,true,0,t,true);
  check(entry.anchor.stableHeight&&near(entry.anchor.eyeHeight,1.65f)&&near(entry.anchor.localOffset.x,0),"grounded neutral pose can establish first calibration");
 }
 auto anchored=anchor.update(1,1,{10.02f,20.01f,1.64f},{10,20,0},0,true);
 for(float shift:{-.08f,-.02f,0.f,.02f,.08f}){
  anchored=anchor.update(1,1,{10.02f+shift,20.01f-shift,1.64f},{10,20,0},0,true);
  auto p=anchored.place({10.02f+shift,20.01f-shift,1.64f},{10,20,0},0);
  check(near(p.x,10.02f)&&near(p.y,20.01f),"lateral animation sway suppressed independently of physics");
 }
 auto p=anchored.place({10.2f,20.3f,1.1f},{10.1f,20.2f,.3f},0);
 check(near(p.x,10.12f)&&near(p.y,20.21f)&&near(p.z,1.1f),"actual collision-resolved movement and native height preserved without smoothing");
 p=anchored.place({10,20,2},{10,20,0},90);
 check(near(p.x,10.02f)&&near(p.y,20.01f)&&near(p.z,2),"body follow cannot orbit camera around physics origin");
 for(float initialYaw:{-135.f,0.f,73.f,179.f}){
  CameraAnchorState turn;
  const Vec physics{10,20,3};
  const Vec local{-.06f,.03f,1.65f};
  auto a=turn.update(7,3,physics+rotateWorldYaw(local,initialYaw),physics,initialYaw,true,true,0,100);
  const auto start=a.place(physics+local,physics,initialYaw);
  for(unsigned i=1;i<=200;++i){
   const float yaw=wrap(initialYaw+i*3.f);
   const auto camera=physics+rotateWorldYaw(local,yaw);
   a=turn.update(7,3,camera,physics,yaw,true,true,0,100+i*16);
   const auto eye=a.place(camera,physics,yaw);
   check(near(eye.x,start.x)&&near(eye.y,start.y),"continuous body follow and yaw wraps preserve camera XY");
   const Vec step{.1f,-.2f,.3f};
   const auto moved=a.place(camera+step,physics+step,yaw);
   check(near(moved.x,start.x+step.x)&&near(moved.y,start.y+step.y),"real movement retained during body follow");
   const auto skeleton=a.placeForSkeleton(camera,physics,yaw);
   check(near(skeleton.x,eye.x)&&near(skeleton.y,eye.y),"skeleton and render share turn-independent anchor");
  }
  check(turn.repairs==0,"body yaw alone never triggers settled-offset repair");
 }
 check(!anchor.update(1,1,{10,20,1.64f},{10,20,0},0,false).active,"scripted/menu/tracking loss invalidates camera anchor");
 anchored=anchor.update(1,1,{10.04f,20,1.64f},{10,20,0},0,true);
 check(near(anchored.localOffset.x,.04f),"resume anchors current camera without stale correction");
 anchored=anchor.update(2,1,{10.03f,20,1.64f},{10,20,0},0,true);
 check(near(anchored.localOffset.x,.03f),"owner change resets camera anchor");
 anchored=anchor.update(2,2,{10.01f,20,1.64f},{10,20,0},0,true);
 check(near(anchored.localOffset.x,.01f),"recenter resets camera anchor");
 anchored=anchor.update(2,2,{30.02f,20,1.64f},{30,20,0},0,true);
 check(near(anchored.localOffset.x,.02f),"teleport resets camera anchor");
 check(!anchor.update(2,2,{31,20,1.64f},{30,20,0},0,true).active,"detached camera is not forced onto body");
 for(unsigned mode:{7u,8u,20u}){
  check(nativeWaterState(mode,0,0,false,false),"verified water modes accepted independently of ground contact");
  check(!nativeWaterState(mode,5,0,false,false)&&!nativeWaterState(mode,0,1,false,false)&&!nativeWaterState(mode,0,0,true,false)&&!nativeWaterState(mode,0,0,false,true),"scripted water states never take forced movement basis");
 }
 check(!waterMode(0)&&!waterMode(10)&&!waterMode(255),"ground QTE and unknown modes untouched");
 for(float pitch:{-1.2f,0.f,1.2f})for(float yaw:{-2.f,0.f,2.f}){
  // qYaw * qPitch; positive XR pitch points upward.
  const float sy=std::sin(yaw/2),cy=std::cos(yaw/2),sp=std::sin(pitch/2),cp=std::cos(pitch/2);
  const std::array<float,4> q{cy*sp,sy*cp,-sy*sp,cy*cp};
  auto b=waterBasis(q,30,10);
  const float worldYaw=yaw+20*radians;
  check(b.valid&&near(b.forward.x,std::cos(pitch)*std::cos(worldYaw))&&near(b.forward.y,std::cos(pitch)*std::sin(worldYaw))&&near(b.forward.z,std::sin(pitch)),"swim direction includes HMD pitch and heading exactly once");
  check(near(b.right.z,0)&&near(dot(b.forward,b.right),0)&&near(length(b.right),1),"strafe stays horizontal and perpendicular");
  auto compensated=waterBasis(q,70,50);check(length(compensated.forward-b.forward)<.001f,"native body yaw compensation does not redirect swimming");
 }
 auto rolled=waterBasis({0,0,.70710678f,.70710678f},0,0);
 check(rolled.valid&&near(rolled.forward.x,1)&&near(rolled.forward.z,0)&&near(rolled.right.z,0),"head roll cannot cause diving or banked strafing");
 check(!waterBasis({0,0,0,0},0,0).valid&&!waterBasis({NAN,0,0,1},0,0).valid,"invalid tracking cannot replace native basis");
 check(nativeObjectInspection(23,0,0,false,false)&&nativeObjectInspection(26,0,0,false,false),"standing and crouched inspect use cinema and native action input");
 for(unsigned mode:{0u,1u,2u,9u,10u,22u,24u,25u,27u,28u,255u})
  check(!nativeObjectInspection(mode,0,0,false,false),"other postures and scripted interactions do not become object inspection");
 check(!nativeObjectInspection(26,5,0,false,false)&&!nativeObjectInspection(26,0,1,false,false)&&!nativeObjectInspection(26,0,0,true,false)&&!nativeObjectInspection(26,0,0,false,true),"scripted or local-space inspection transitions retain native ownership");
 check(nativeGripInteraction(10,5,0,false,false),"observed grip state retains native interaction input");
 check(!nativeGripInteraction(0,5,0,false,false)&&!nativeGripInteraction(10,0,0,false,false)&&!nativeGripInteraction(10,5,0,true,false),"other immobilized and scripted states cannot receive grip gestures");
 // Airborne native/body changes are cancelled against the original anchor;
 // landing does not recapture the user's newly turned headset direction.
 for(float nativeYaw:{35.f,60.f,110.f,-175.f}){
  const float anchor=35,headTurn=80,artificial=20;
  const float compensation=wrap(nativeYaw-anchor)-artificial;
  check(near(wrap(nativeYaw-compensation+headTurn),wrap(anchor+headTurn+artificial)),"airborne and landing body turns cannot redirect view");
 }
 check(trackedGameplayState(0,0,0,false,false)&&trackedGameplayState(1,0,0,false,false)&&trackedGameplayState(2,0,0,false,false),"jump and crawl retain view ownership independently of ground contact");
 check(!trackedGameplayState(3,0,0,false,false)&&!trackedGameplayState(0,0,0,true,false)&&!trackedGameplayState(0,0,1,false,false),"scripted cameras retain native ownership");
 check(nativeSideDodge(0,5,0,false,false)&&headsetViewState(0,5,0,false,false),"observed side dodge retains headset view reference");
 check(!trackedGameplayState(0,5,0,false,false),"side dodge keeps native movement ownership");
 for(unsigned mode:{1u,2u,3u,4u,9u,14u})check(!nativeSideDodge(mode,5,0,false,false),"dodge admission excludes unrelated postures");
 check(!nativeSideDodge(0,0,0,false,false)&&!nativeSideDodge(0,7,0,false,false)&&
       !nativeSideDodge(0,5,1,false,false)&&!nativeSideDodge(0,5,0,true,false)&&
       !nativeSideDodge(0,5,0,false,true),"dodge excludes other physics and authored cameras");
 for(int type:{0,5,5,5,0}){
  std::array<float,12> camera{10,20,30,1,0,0,0,.8660254f,.5f,0,-.5f,.8660254f};
  check(headsetViewState(0,type,0,false,false),"heading reference survives dodge entry and exit");
  if(headsetViewState(0,type,0,false,false))levelGameplayCamera(camera,30);
  check(camera[8]==0&&camera[9]==0&&camera[10]==0&&camera[11]==1,"native dodge roll removed before headset rotation");
  check(camera[0]==10&&camera[1]==20&&camera[2]==30,"dodge translation is not cancelled or frozen");
 }
 check(nativeLowLedgeTraversal(3,5,0,false,false),"observed low ledge retains headset orientation");
 for(unsigned mode:{0u,1u,2u,4u,6u,10u,11u,12u,13u,14u,15u,16u,23u,255u})
  check(!nativeLowLedgeTraversal(mode,5,0,false,false),"low ledge policy does not admit unrelated scripted states");
 check(!nativeLowLedgeTraversal(3,0,0,false,false)&&!nativeLowLedgeTraversal(3,7,0,false,false)&&
       !nativeLowLedgeTraversal(3,5,1,false,false)&&!nativeLowLedgeTraversal(3,5,0,true,false)&&
       !nativeLowLedgeTraversal(3,5,0,false,true),"low ledge excludes other physics and camera overrides");
 {
  BodyYawFollow body;body.reset(35);body.artificialYaw=20;
  for(float yaw:{35.f,70.f,-179.f,179.f,90.f}){
   body.suspend();
   check(body.valid&&near(body.artificialYaw,20),"traversal suspension preserves established VR reference");
   std::array<float,12> camera{1,2,3,0,0,-1,1,0,0,0,-1,0};
   levelGameplayCamera(camera,yaw);
   const auto forward=rotateWorldYaw({camera[3],camera[4],camera[5]},-(wrap(yaw-35)-body.artificialYaw));
   check(near(forward.z,0)&&near(std::atan2(forward.y,forward.x)/radians,55),"native traversal pitch and yaw cannot steer headset base");
   check(camera[0]==1&&camera[1]==2&&camera[2]==3,"traversal path remains native");
  }
  check(!trackedGameplayState(3,5,0,false,false),"orientation admission does not enable gameplay physics anchoring");
 }
 for(float bodyYaw:{-120.f,0.f,65.f}){
  std::array<float,12> camera{1,2,3,0,0,1,1,0,0,0,1,0};
  levelGameplayCamera(camera,bodyYaw);
  auto view=rotateWorldYaw({camera[3],camera[4],camera[5]},-bodyYaw);
  check(near(view.x,1)&&near(view.y,0)&&near(view.z,0),"airborne view retains accepted body yaw compensation");
  check(camera[0]==1&&camera[1]==2&&camera[2]==3&&camera[11]==1,"native jump height preserved while animation rotation removed");
 }
 // Yaw * pitch * roll: recenter must preserve tracking gravity even when
 // the headset is strongly tilted; both eyes then retain their real tilt.
 auto product=[](std::array<float,4> a,std::array<float,4> b){return std::array<float,4>{
 a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
 a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
 a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
 a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};};
 for(float heading:{-140.f,0.f,75.f})for(float pitch:{-60.f,0.f,60.f})for(float roll:{-80.f,0.f,80.f}){
  const float h=heading*radians*.5f,p=pitch*radians*.5f,rh=roll*radians*.5f;
  const std::array<float,4> yq{0,std::sin(h),0,std::cos(h)};
  const auto tilt=product({std::sin(p),0,0,std::cos(p)},{0,0,std::sin(rh),std::cos(rh)});
  const auto head=product(yq,tilt),ref=uprightReference(head);
  check(near(ref[0],0)&&near(ref[2],0)&&near(trackingYaw(ref),heading),"recenter reference removes pitch and roll");
  const auto relative=product({-ref[0],-ref[1],-ref[2],ref[3]},head);
  for(unsigned k=0;k<4;++k)check(near(relative[k],tilt[k]),"recenter retains actual headset tilt instead of baking it into horizon");
  const auto level=product({-ref[0],-ref[1],-ref[2],ref[3]},yq);
  check(near(level[0],0)&&near(level[1],0)&&near(level[2],0)&&near(level[3],1),"head upright after tilted recenter restores level horizon");
 }
 check(nativeCameraPosture(0,.02f,1.64f)&&nativeCameraPosture(1,.02f,.8f)&&nativeCameraPosture(9,.02f,1.64f),"native standing crouching combat camera accepted");
 check(!nativeCameraPosture(3,.02f,.8f)&&!nativeCameraPosture(1,.4f,.8f)&&!nativeCameraPosture(1,.02f,.1f),"scripted or mismatched cameras rejected");
 // Fixture only: engine +X forward, -Y right, +Z up, one unit/metre.
 // Runtime must independently verify this basis and scale.
 GroundFrame f{{1,0,0},{0,-1,0},{0,0,1},1,true};
 auto step=[&](RoomscaleFollow& r,Vec native,Vec head,bool manual=false,bool enabled=true){return r.update(1,native,head,f,0,f.forward,f.right,manual,enabled);};
 RoomscaleFollow r;step(r,{},{});auto s=step(r,{}, {0,0,-.1f});check(s.y>0&&near(s.x,0),"physical forward");
 r={};step(r,{},{});s=step(r,{}, {0,0,.1f});check(s.y<0,"physical backward");
 r={};step(r,{},{});s=step(r,{}, {.1f,0,0});check(s.x>0&&near(s.y,0),"physical right");
 r={};step(r,{},{});s=step(r,{}, {-.1f,0,0});check(s.x<0,"physical left");
 r={};step(r,{},{});s=step(r,{}, {0,1,0});check(near(s.x,0)&&near(s.y,0),"height never locomotes");
 r={};step(r,{},{});step(r,{}, {0,0,-.1f});
 for(int i=0;i<100;++i)step(r,{}, {0,0,-.1f});
 check(length(r.accepted)==0,"wall does not acknowledge blocked motion");
 {
  RoomscaleFollow notSent;
  step(notSent,{},{});step(notSent,{}, {0,0,-.1f});
  notSent.update(1,{.05f,0,0},{0,0,-.1f},f,0,f.forward,f.right,false,true,.015f,.006f,false);
  check(length(notSent.accepted)==0&&length(notSent.acceptedStep)==0,"prepared but unsubmitted input cannot acknowledge unrelated native movement");
 }
 s=step(r,{},{});check(near(s.x,0)&&near(s.y,0),"back away from wall clears request");
 step(r,{}, {0,0,-.1f});step(r,{.04f,0,0},{0,0,-.1f});check(near(r.accepted.z,-.04f),"accept actual native motion only");
 step(r,{10,0,0},{0,0,-.1f});check(length(r.accepted)==0&&!r.commanded,"teleport rebases instead of acknowledging");
 step(r,{10,0,0},{.2f,0,-.1f},true);s=step(r,{10,0,0},{.2f,0,-.1f});check(near(s.x,0)&&near(s.y,0),"manual priority no backlog");
 step(r,{10,0,0},{.3f,0,-.1f},false,false);s=step(r,{10,0,0},{.3f,0,-.1f});check(near(s.x,0)&&near(s.y,0),"pause resume rebases");
 r.reset(1,{10,0,0},{.5f,0,-.1f});s=step(r,{10,0,0},{.5f,0,-.1f});check(near(s.x,0)&&near(s.y,0),"recenter clears pending");
 s=r.update(2,{10,0,0},{.6f,0,0},f,0,f.forward,f.right,false,true);check(r.owner==2&&!r.commanded,"new player resets");
 check(!mayFollow(State::Climbing,true,true,true,true)&&!mayFollow(State::Scripted,true,true,true,true)&&!mayFollow(State::Unknown,true,true,true,true),"special/unknown states closed");
 check(!mayFollow(State::Walking,false,true,true,true)&&!mayFollow(State::Walking,true,true,true,false),"tracking loss/collision disabled closed");
 Settings settings;BodyYawFollow yaw;yaw.reset(0);yaw.sign=-1;
 check(yaw.request(20,settings,false,true)==0,"head turns within deadzone independent");
 check(yaw.request(90,settings,false,true)<0,"body follows physical 90 degree turn");
 yaw.observe(10);check(near(yaw.acceptedPhysicalYaw,10),"native yaw acknowledgement");
 check(yaw.request(90,settings,false,false)==0,"scripted body follow off");
 yaw.reset(0);settings.snap=true;yaw.turn(1,100,settings,true);yaw.turn(1,110,settings,true);
 check(near(yaw.artificialYaw,-45),"snap latch once");yaw.turn(0,120,settings,true);yaw.turn(-1,130,settings,true);check(near(yaw.artificialYaw,0),"opposite snap release");
 settings.snap=false;yaw.reset(0);yaw.turn(1,100,settings,true);yaw.turn(1,200,settings,true);check(near(yaw.artificialYaw,-18),"smooth elapsed time");
 yaw.turn(1,2000,settings,true);check(near(yaw.artificialYaw,-18),"stall cannot accumulate turn");
 yaw.acceptedPhysicalYaw=35;const auto savedReference=yaw.nativePrevious;
 yaw.suspend();check(yaw.valid&&near(yaw.acceptedPhysicalYaw,35)&&near(yaw.artificialYaw,-18)&&near(yaw.nativePrevious,savedReference),"tracking suspension preserves shared yaw reference");
 yaw.turn(1,8000,settings,true);check(near(yaw.artificialYaw,-18),"resume cannot accumulate suspended turn time");
 Settings fast;fast.smoothSpeed=240;fast.followSpeed=120;
 {
  BodyYawFollow precise;precise.reset(0);precise.sign=-1;
  precise.turnMicros(1,1000000,fast,true);
  for(uint64_t frame=1;frame<=90;++frame){
   const auto before=precise.artificialYaw;
   precise.turnMicros(1,1000000+frame*11111,fast,true);
   check(std::abs(wrap(precise.artificialYaw-before)+2.66664f)<.0001f,"90Hz smooth turn advances every render rather than coarse clock steps");
   precise.acceptedPhysicalYaw=precise.artificialYaw;
   check(near(precise.request(0,fast,false,true),240),"body feedforward never drops between coarse clock ticks");
  }
  const auto before=precise.artificialYaw;
  precise.turnMicros(1,1999990,fast,true);
  check(near(precise.artificialYaw,before)&&near(precise.request(0,fast,false,true),240),"duplicate timestamp retains velocity without integrating twice");
  precise.turnMicros(0,1999990,fast,true);check(precise.request(0,fast,false,true)==0,"release clears feedforward even at identical timestamp");
 }
 BodyYawFollow continuous;continuous.reset(0);continuous.sign=-1;
 continuous.turn(1,100,fast,true);
 for(uint64_t tick=110;tick<=10100;tick+=10){
  continuous.turn(1,tick,fast,true);
  const float speed=continuous.request(0,fast,false,true);
  check(near(speed,240),"smooth turn bypasses physical follow speed cap");
  continuous.acceptedPhysicalYaw=wrap(continuous.acceptedPhysicalYaw+speed*continuous.sign*.01f);
  check(std::abs(wrap(continuous.artificialYaw-continuous.acceptedPhysicalYaw))<.01f,"continuous turn body remains aligned across yaw wraps");
 }
 continuous.turn(0,10110,fast,true);check(near(continuous.request(0,fast,false,true),0),"released stick stops feedforward");
 continuous.turn(1,10120,fast,true);
 check(continuous.request(0,fast,true,true)==0&&continuous.request(0,fast,false,false)==0,"manual and scripted ownership block feedforward");
 continuous.turn(1,11000,fast,true);check(continuous.smoothVelocity==0,"stall clears feedforward");
 continuous.turn(1,11010,fast,false);check(continuous.smoothVelocity==0,"inactive turn clears feedforward");
 continuous.suspend();check(continuous.smoothVelocity==0,"suspension clears feedforward");
 continuous.reset(0);continuous.sign=-1;check(near(continuous.request(90,fast,false,true),-120),"physical follow retains speed limit");
 fast.snap=true;continuous.turn(1,12000,fast,true);check(continuous.smoothVelocity==0,"snap retains existing residual follow");
 auto move=kharvox::rotateMovementStickForDirection({0,.4f},90);check(near(move.x,-.4f)&&near(move.y,0),"head left 90 preserves analog magnitude");
 auto travel=kharvox::fullTravelMovementStick({0,.4f});check(near(travel.y,.4f),"ARGENT low stick travel preserved");
 travel=kharvox::fullTravelMovementStick({0,.85f});check(near(travel.y,1),"ARGENT full speed at 85 percent");
 travel=kharvox::fullTravelMovementStick({.85f,.85f});check(near(std::hypot(travel.x,travel.y),1)&&near(travel.x,travel.y),"ARGENT diagonal radial clamp");
 auto dir=kharvox::resolveMovementDirectionYaw(kharvox::MovementDirectionMode::OffHand,20,true,-1,0,0,0);
 check(dir.usedOffHand&&near(dir.degrees,90),"offhand horizontal left");
 dir=kharvox::resolveMovementDirectionYaw(kharvox::MovementDirectionMode::OffHand,20,true,0,0,0,0);
 check(!dir.usedOffHand&&near(dir.degrees,20),"vertical offhand falls back");
 dir=kharvox::resolveMovementDirectionYaw(kharvox::MovementDirectionMode::OffHand,20,false,1,0,0,0);check(!dir.usedOffHand,"lost offhand falls back");
 check(length(horizontal({1,2,3},f.up)-Vec{1,2,0})==0,"ground projection");
 auto world=f.trackingToWorld({0,0,-1},90);check(near(world.x,0)&&near(world.y,-1)&&near(world.z,0),"tracking yaw conversion");
 f.verified=false;check(!f.valid(),"unverified coordinates closed");
 GroundFrame stableFrame{{1,0,0},{0,-1,0},{0,0,1},1,true};RoomscaleFollow steady;
 steady.update(1,{}, {},stableFrame,0,stableFrame.forward,stableFrame.right,false,true);
 for(int i=0;i<100;++i){auto noise=steady.update(1,{}, {float(i%3-1)*.004f,0,float(i%5-2)*.003f},stableFrame,0,stableFrame.forward,stableFrame.right,false,true);check(near(noise.x,0)&&near(noise.y,0),"millimetre tracking noise must not command body");}
 auto begin=steady.update(1,{}, {0,0,-.016f},stableFrame,0,stableFrame.forward,stableFrame.right,false,true);check(begin.y>0,"roomscale begins outside 15mm");
 auto continueMove=steady.update(1,{}, {0,0,-.010f},stableFrame,0,stableFrame.forward,stableFrame.right,false,true);check(continueMove.y>0,"hysteresis continues below start threshold");
 auto stopMove=steady.update(1,{}, {0,0,-.005f},stableFrame,0,stableFrame.forward,stableFrame.right,false,true);check(near(stopMove.y,0),"roomscale stops inside 6mm");
 auto noRestart=steady.update(1,{}, {0,0,-.010f},stableFrame,0,stableFrame.forward,stableFrame.right,false,true);check(near(noRestart.y,0),"noise below start cannot restart correction");
 check(!nativeMovementIsManual(true,1100,1000),"own merged command must not reset roomscale");
 check(nativeMovementIsManual(true,1200,1000),"native movement without recent injection has priority");
 check(nativeMovementIsManual(true,1000,0),"initial native movement has priority");
 auto corrected=rotateWorldYaw(rotateWorldYaw({1,0,.2f},60),-35);
 auto expected=rotateWorldYaw({1,0,.2f},25);
 check(length(corrected-expected)<.0001f,"accepted native body yaw removed from camera exactly once");
 check(nativeTurnStick(0,180)==0&&nativeTurnStick(1,180)>8689.f/32767.f,"body turn overcomes native right stick deadzone");
 check(near(nativeTurnStick(-180,180),-1),"native right turn sign and clamp");
 {
  BodyYawFollow turn;turn.reset(0);Settings config;config.snap=false;config.smoothSpeed=90;
  turn.suspend(true);turn.turnMicros(1,1000000,config,true);turn.turnMicros(1,1100000,config,true);
  check(near(turn.artificialYaw,-9)&&turn.command==0,"view-only smooth turn changes yaw without body command");
  turn.suspend(true);turn.turnMicros(1,9000000,config,true);
  check(near(turn.artificialYaw,-9),"traversal transition never catches up suspended time");
  config.snap=true;config.snapAngle=30;turn.turnMicros(1,9100000,config,true);
  const float snapped=turn.artificialYaw;turn.suspend(true);turn.turnMicros(1,9200000,config,true);
  check(near(turn.artificialYaw,snapped)&&turn.command==0,"held snap cannot retrigger across traversal boundary");
  turn.turnMicros(0,9300000,config,true);turn.turnMicros(-1,9400000,config,true);
  check(near(turn.artificialYaw,wrap(snapped+30)),"neutral rearms reverse traversal snap");
 }
 {
  WhipTraversalEpisode e;
  check(!e.update(1,true,0,5,0,false,false),"arbitrary script cannot bootstrap whip episode");
  e.update(1,true,14,5,0,false,false);
  for(unsigned sample=0;sample<100000;++sample)
   check(e.update(1,true,0,5,0,false,false),"prompt persists until state event, independent of elapsed time or render rate");
  check(!e.update(2,true,0,5,0,false,false),"actor change ends episode");
  for(unsigned mode:{12u,13u,14u}){
   e.update(2,true,mode,mode==14?5:7,0,false,false);
   check(e.update(2,true,0,5,0,false,false),"recognized traversal admits interactive successor");
   check(!e.update(2,true,0,0,0,false,false)&&!e.update(2,true,0,5,0,false,false),"gameplay exit clears episode, later dodge stays unrelated");
  }
  e.update(2,true,14,5,0,false,false);e.update(2,true,3,5,0,false,false);
  check(e.update(2,true,0,5,0,false,false),"pull-up transition preserves episode");
  e.update(2,true,0,5,1,false,false);
  check(!e.update(2,true,0,5,0,false,false),"authored camera override ends episode");
  e.update(2,true,14,5,0,false,false);e.update(2,false,0,5,0,false,false);
  check(!e.update(2,true,0,5,0,false,false),"invalid native state clears ownership");
 }
 {
  CameraAnchorState a;
  a.update(7,2,{0,0,1.65f},{0,0,0},0,true,true,0,100);
  auto stable=a.update(7,2,{0,0,1.65f},{0,0,0},0,true,true,0,700);
  for(unsigned i=0;i<20;++i){
   const float x=i%2?.44f:-.36f;
   auto v=a.update(7,2,{x,.01f,1.3f},{0,0,0},float(i*10),true,true,0,720+i*32);
   check(v.active&&v.stableHeight&&near(v.eyeHeight,stable.eyeHeight)&&near(v.worldOffset.x,stable.worldOffset.x),"turn/body excursion retains settled position and height");
  }
  check(!a.retains(8,2,{.44f,0,1.3f},{0,0,0},0)&&!a.retains(7,3,{.44f,0,1.3f},{0,0,0},0)&&
        !a.retains(7,2,{.44f,0,1.3f},{0,0,0},1)&&!a.retains(7,2,{.8f,0,1.3f},{0,0,0},0),"owner epoch stance and detached-camera gates remain");
  auto v=a.update(7,2,{0,0,.8f},{0,0,0},0,true,true,1,1500);
  check(v.active&&!v.stableHeight,"real crouch retains native transition");
  check(!a.update(7,2,{.44f,0,1.3f},{0,0,0},0,false,true,0,1600).active,"script override still releases anchor");
 }
 {
  CameraAnchorState a;
  for(uint64_t t:{100u,700u,1000u})a.update(7,2,{.01f,-.02f,1.65f},{},0,true,true,0,t);
  check(a.calibrationLocked,"neutral camera calibrated before melee");
  for(unsigned i=0;i<40;++i){
   const auto held=a.update(7,2,{.44f,.01f,1.3f},{},0,true,true,9,1020+i*16,false);
   check(held.active&&held.stableHeight&&near(held.eyeHeight,1.65f),"mode9 attack keeps standing camera active immediately");
   check(near(held.worldOffset.x,.01f)&&near(held.worldOffset.y,-.02f),"melee cannot learn attack displacement");
  }
  const auto restored=a.update(7,2,{.01f,-.02f,1.65f},{},0,true,true,0,1800);
  check(restored.active&&restored.stableHeight&&near(restored.eyeHeight,1.65f),"melee exit requires no delayed repair");
  check(!a.update(7,2,{0,0,.8f},{},0,true,true,1,1900).stableHeight,"actual crouch still changes stance");
 }
 return errors?1:0;
}


