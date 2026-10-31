#include "camera_watchdog.hpp"
#include <cassert>
#include <limits>
using namespace circle::motor::locomotion;
int main(){
 CameraPositionWatchdog w;
 auto step=[&](uint64_t t,bool normal,bool interruption,bool compare=false,float error=0){return w.update(7,2,t,true,normal,interruption,compare,error);};
 assert(step(100,true,false)==CameraRepair::None);
 assert(step(110,false,true)==CameraRepair::None); // boarding / authored animation
 assert(step(200,false,true)==CameraRepair::None); // do not fight the script
 assert(step(250,true,false)==CameraRepair::NativeReturn);
 assert(step(260,true,false)==CameraRepair::None); // one correction per episode
 assert(step(270,false,true)==CameraRepair::None); // heavy strike, even a short one
 assert(step(290,true,false)==CameraRepair::NativeReturn);
 for(uint64_t t=300;t<10000;t+=16)assert(step(t,true,false,true,0)==CameraRepair::None);
 assert(step(10000,true,false,true,.2f)==CameraRepair::None);
 for(uint64_t t=10016;t<10250;t+=16)assert(step(t,true,false,true,.2f)==CameraRepair::None);
 assert(step(10256,true,false,true,.2f)==CameraRepair::ReferenceMismatch);
 assert(step(10272,true,false,true,.2f)==CameraRepair::None);
 assert(step(10300,true,false,true,0)==CameraRepair::None);
 assert(step(10400,true,false,true,.2f)==CameraRepair::None);
 assert(step(11000,true,false,true,.2f)==CameraRepair::None); // observation gap restarts timer
 assert(w.update(7,2,11001,false,true,false,true,1)==CameraRepair::None);
 assert(step(11100,true,false,true,.2f)==CameraRepair::None);
 assert(step(11116,true,false,false,1)==CameraRepair::None); // manual/input suspension is not a mismatch
 assert(step(11132,true,false,true,std::numeric_limits<float>::quiet_NaN())==CameraRepair::None);
 assert(w.update(8,2,12000,true,true,false,true,.2f)==CameraRepair::None);
 assert(w.update(8,3,12016,true,true,false,true,.2f)==CameraRepair::None);
 assert(w.update(8,3,100,true,true,false,true,.2f)==CameraRepair::None);

 // A large genuine lean has zero disagreement between the two references.
 RoomscaleView view;RoomscaleFollow follow;
 GroundFrame f{{1,0,0},{0,-1,0},{0,0,1},1,true};
 view.update(7,2,{}, {},f,0,true,100);follow.reset(7,{},{});
 w={};
 for(uint64_t t=100;t<10000;t+=16){
  view.update(7,2,{.4f,0,-.2f},{},f,0,true,t);
  assert(w.update(7,2,t,true,true,false,true,cameraReferenceError(follow,view))==CameraRepair::None);
 }
 assert(length(view.rawOffset)>.4f); // no forced recenter from sustained separation
 follow.origin={.1f,0,0};assert(std::abs(cameraReferenceError(follow,view)-.1f)<.001f);

 // Regression: the old automatic seat recenter discarded all body dimensions.
 CameraAnchorState anchor;
 for(uint64_t t:{100u,200u,1000u})anchor.update(7,2,{-.04f,.02f,1.65f},{},0,true,true,0,t);
 assert(anchor.calibrationLocked&&anchor.postureKnown[0]);
 transferCameraCalibration(anchor,7,2,3);
 anchor.update(7,3,{0,0,.9f},{},180,false,true,1,1100,false);
 auto restored=anchor.update(7,3,{.4f,0,1.9f},{},180,true,true,0,1200,false);
 assert(restored.active&&restored.stableHeight&&std::abs(restored.eyeHeight-1.65f)<.001f);
 assert(std::abs(restored.worldOffset.x+.04f)<.001f);
 transferCameraCalibration(anchor,8,3,4);assert(anchor.trustedEpoch==3);
 transferCameraCalibration(anchor,7,1,4);assert(anchor.trustedEpoch==3);
 anchor.suspend();anchor.update(7,4,{0,0,1.7f},{},0,true,true,0,1300);
 assert(!anchor.calibrationLocked&&!anchor.postureKnown[0]); // explicit recenter retains prior semantics
}
