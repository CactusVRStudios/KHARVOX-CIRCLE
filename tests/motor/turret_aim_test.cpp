#include "mounted_gun_policy.hpp"
#include "mounted_gun_transform.hpp"
#include <cstdio>
using namespace circle::motor;using namespace circle::motor::turret;
namespace {
Quat rotation(float degrees,Vec axis){float h=degrees*.00872664626f;return {axis.x*std::sin(h),axis.y*std::sin(h),axis.z*std::sin(h),std::cos(h)};}
Quat hand(float right,float up=0){return hands::multiply(rotation(-right,{0,1,0}),rotation(up,{1,0,0}));}
bool close(Vec a,Vec b){return hands::length(a-b)<.0002f;}
bool close(Pose a,Pose b){return close(a.position,b.position)&&std::abs(std::abs(a.orientation.x*b.orientation.x+a.orientation.y*b.orientation.y+a.orientation.z*b.orientation.z+a.orientation.w*b.orientation.w)-1)<.0001f;}
}
int main(){int failures=0;auto check=[&](bool ok,const char* msg){if(!ok){++failures;std::printf("FAIL %s\n",msg);}};
 input::Native n{};n.fresh=true;n.mode=27;n.slot=22;n.type=5;
 check(input::turretAimContext(n),"observed stationary scene recognized");
 for(int i=0;i<8;++i){auto bad=n;switch(i){case 0:bad.menu=true;break;case 1:bad.fresh=false;break;case 2:bad.slot=2;break;case 3:bad.mode=0;break;case 4:bad.type=0;break;case 5:bad.reason=1;break;case 6:bad.animated=true;break;case 7:bad.local=true;break;}check(!input::turretAimContext(bad),"unrelated native ownership excluded");}
 const Pose mount{{100,200,10},rotation(35,{0,0,1})};
 const Pose local{{-.4f,0,.65f},rotation(180,{0,0,1})};
 const Pose camera=hands::compose(mount,local);
 SeatAnchor seat;check(seat.update(1,22,mount,camera,{},false),"seat captures mount-relative camera");
 const auto epoch=seat.generation;
 for(float yaw:{-90.f,0.f,90.f})for(float pitch:{-20.f,13.f}){
  Pose native{{101.f+yaw*.005f,200.f,10.3f},hands::multiply(rotation(yaw,{0,0,1}),rotation(pitch,{0,1,0}))};
  check(seat.update(1,22,mount,native,hand(yaw,pitch),false),"native camera movement remains valid input");
  check(close(seat.camera(mount),camera),"native stick/body pitch yaw and translation cannot drag seated camera");
  check(seat.generation==epoch,"head/stick motion does not recapture seat");
 }
 const Pose moving{{105,198,12},hands::multiply(rotation(72,{0,0,1}),rotation(14,{1,0,0}))};
 check(close(seat.camera(moving),hands::compose(moving,local)),"seat follows full airplane translation rotation and roll");
 seat.update(1,22,moving,hands::compose(moving,local),hand(45),true);
 check(close(seat.camera(moving),hands::compose(moving,local)),"recenter changes headset heading only");
 check(seat.update(1,23,moving,hands::compose(moving,local),{},false)&&seat.generation!=epoch,"new gun creates new reference");
 auto matrixValue=matrix(camera);auto roundTrip=pose(matrixValue);
 check(roundTrip&&close(*roundTrip,camera),"full mount basis survives camera matrix conversion");
 const auto m=matrix(camera);locomotion::GroundFrame basis{{m[3],m[4],m[5]},{-m[6],-m[7],-m[8]},{m[9],m[10],m[11]},1,true};
 hands::HandFrame hf{camera.position,{0,1.6f,0},basis,0};
 auto wrist=hf.toWorld({.25f,1.3f,-.45f});
 check(wrist&&close(*wrist,camera.position+basis.forward*.45f+basis.right*.25f+basis.up*(-.3f)),"controller wrist uses fixed camera basis");
 const Vec limitsLow{-20,-150,0},limitsHigh{13,-40,0};
 for(int hz:{40,72,90,120}){
  Aim a;uint64_t ns=1000000000,dt=1000000000/hz;Vec native{0,-90,0};
  auto run=[&](Quat q,bool allowed=true,bool manual=false){ns+=dt;return a.update(1,22,5,ns,q,native,limitsLow,limitsHigh,allowed,manual);};
  check(close(run(hand(0)),native),"first sample never turns gun");
  auto wanted=run(hand(10,5));check(close(wanted,{5,-80,0}),"right/up controller rotation uses headset-verified mounted gun signs");
  check(close(run(hand(10,5)),wanted),"holding pose does not continue turning");
  check(close(a.update(1,22,5,ns,hand(10,5),native,limitsLow,limitsHigh,true,false),wanted),"duplicate sampling does not double aim");
  native.y-=5;wanted=run(hand(30,10),true,true);
  check(close(wanted,{5,-85,0}),"stick adds native delta without snapping to old camera heading");
  check(close(run(hand(30,10)),wanted),"manual release rebases controller while preserving gun target");
  check(close(run(hand(35,10)),{5,-80,0}),"motion resumes from current gun target");
  wanted=run(hand(-20),false);check(close(run(hand(-20)),wanted),"pause or tracking recovery cannot catch up old motion");
  check(close(native,{0,-95,0}),"native camera angles never modified");
 }
 Aim a;Vec native{0,-90,0};
 a.update(1,22,1,1000000000,hand(0),native,limitsLow,limitsHigh,true,false);
 for(int i=1;i<=5;++i)a.update(1,22,1,1000000000+i*10000000,hand(float(i*20),float(i*10)),native,limitsLow,limitsHigh,true,false);
 check(close(a.command,{13,-40,0}),"private gun angles obey native mechanical limits");
 const auto held=a.command;
 check(close(a.update(1,22,1,2000000000,hand(-45),native,limitsLow,limitsHigh,true,false),held),"stale XR sample rebases without jump");
 check(close(a.update(1,22,2,2010000000,hand(45),native,limitsLow,limitsHigh,true,false),held),"new XR session does not turn gun");
 check(close(a.update(1,22,2,2020000000,{NAN,0,0,1},native,limitsLow,limitsHigh,true,false),held),"invalid controller pose cannot affect gun");
 Aim stickAim;stickAim.update(1,22,1,1000000000,{},native,limitsLow,limitsHigh,true,false);
 for(int i=1;i<=10;++i)stickAim.update(1,22,1,1000000000+i*10000000,{},native,limitsLow,limitsHigh,true,true,{1,0,0},.01f);
 check(close(stickAim.command,{0,-99,0})&&close(native,{0,-90,0}),"right stick steers gun directly without moving native camera angles");
 const auto stop=stickAim.command;
 check(close(stickAim.update(1,22,1,1200000000,{},native,limitsLow,limitsHigh,true,true,{0,0,0},.01f),stop),"stick stop retains target");
 // Independent axis cases, including reversed movement back to the start.
 for(unsigned axis=0;axis<2;++axis){
  Aim motion;const Vec low{-180,-180,0},high{180,180,0};
  motion.update(1,22,1,1000000000,hand(0),{},low,high,true,false);
  const auto turn=motion.update(1,22,1,1010000000,hand(axis?0:12,axis?8:0),{},low,high,true,false);
  check(close(turn,axis?Vec{8,0,0}:Vec{0,12,0}),"right and up each steer in the corrected direction");
  check(close(motion.update(1,22,1,1020000000,hand(0),{},low,high,true,false),{}),"returning controller reverses both axes without drift");
 }
 // Emulate Motor's generation-tagged transform pool, with a simulation
 // snapshot deliberately one 25 ms tick behind the 27 m/s airplane.
 std::array<unsigned char,1024> memory{};
 const auto put=[&](size_t address,const auto& value){std::memcpy(memory.data()+address,&value,sizeof(value));};
 const MountSource source{32,64,128,22,uint64_t(7)<<24|1};
 put(32,uint64_t(22));put(64+32,source.handle);put(128,uintptr_t(256));put(136,uint64_t(8));
 put(256+32+12,uint16_t(7));
 const auto read=[&](uintptr_t address,void* out,size_t size){
  if(address>memory.size()||size>memory.size()-address)return false;
  std::memcpy(out,memory.data()+address,size);return true;
 };
 RenderMount render;SeatAnchor movingSeat;
 for(int hz:{40,72,90,120}){
  render.reset();movingSeat.reset();
  for(unsigned i=0;i<60;++i){
   Pose visible{{100+27.f*float(i)/hz,20,5},rotation(float(i)*.1f,{0,0,1})};
   Pose simulation=visible;simulation.position.x-=.675f;
   put(256+32,visible.position);put(256+32+16,visible.orientation);
   Pose current;
   check(readMount(source,read,current)&&close(current,visible),"view root reads current pool, not the stale job snapshot");
   check(render.update(1,22,3,&current),"current render root acquired");
   const Pose authored=hands::compose(visible,local);
   check(movingSeat.update(1,22,render.pose,authored,{},false),"mount-relative seat follows view-time root");
   check(close(hands::compose(hands::inverse(visible),movingSeat.camera(render.pose)),local),"camera remains stationary relative to moving/rotating airplane at all refresh rates");
   check(hands::length(movingSeat.camera(simulation).position-authored.position)>.6f,"fixture reproduces large old simulation phase error");
   const auto before=render.pose;
   check(render.update(1,22,3,nullptr)&&close(render.pose,before),"failed read holds latest root instead of moving back to job pose");
  }
 }
 Pose sentinel{{-5,-6,-7},{}};Pose result=sentinel;
 put(32,uint64_t(23));check(!readMount(source,read,result)&&close(result,sentinel),"recycled entity record rejected without publishing pose");put(32,uint64_t(22));
 put(256+32+12,uint16_t(8));check(!readMount(source,read,result),"recycled transform generation rejected");put(256+32+12,uint16_t(7));
 put(136,uint64_t(1));check(!readMount(source,read,result),"out-of-range transform handle rejected");put(136,uint64_t(8));
 put(64+32,uint64_t(9));check(!readMount(source,read,result),"changed component handle rejected");put(64+32,source.handle);
 unsigned reads=0;
 const auto torn=[&](uintptr_t address,void* out,size_t size){
  if(!read(address,out,size))return false;
  if(address==288&&++reads==2)static_cast<unsigned char*>(out)[0]^=1;
  return true;
 };
 check(!readMount(source,torn,result),"concurrent transform update rejected");
 check(!render.update(1,23,3,nullptr),"different gun cannot inherit held root");
 render.update(1,22,3,&mount);check(!render.update(1,22,4,nullptr),"new life cannot inherit held root");
 SeatAnchor retained;retained.update(1,22,mount,camera,{},false);
 const auto retainedEpoch=retained.generation;
 Pose displaced=camera;displaced.position.x+=10;
 check(retained.update(1,22,moving,displaced,{},false)&&retained.generation==retainedEpoch&&close(retained.camera(moving),hands::compose(moving,local)),"native camera excursion cannot release or recapture established seat");
 check(!retained.update(1,23,moving,displaced,{},false),"new seat still requires nearby valid native camera");
 // Boat seats can be farther from the hull origin than the airplane gun.
 SeatAnchor boatSeat;const Pose stern{{-6,0,1.2f},{}};
 const auto boatCamera=hands::compose(mount,stern);
 check(!boatSeat.update(8,90,mount,boatCamera,{},false),"airplane entry radius unchanged");
 check(boatSeat.update(8,90,mount,boatCamera,{},false,12.f),"boat driver seat accepts hull-relative stern position");
 const auto boatEpoch=boatSeat.generation;
 check(boatSeat.update(8,90,moving,displaced,hand(75),false,12.f)&&
       close(boatSeat.camera(moving),hands::compose(moving,stern))&&boatSeat.generation==boatEpoch,
       "boat turning and native camera look cannot move the driver seat");
 boatSeat.update(8,90,moving,displaced,hand(75),true,12.f);
 check(close(boatSeat.camera(moving),hands::compose(moving,stern)),"boat recenter retains fixed position");
 const auto beforeBoarding=boatSeat.camera(moving);
 const auto driverEye=hands::compose(moving,Pose{{-4.7f,.4f,.35f},{}}).position;
 check(boatSeat.followDriverPosition(moving,driverEye,12.f),"driver origin follows completed boarding placement");
 check(hands::length(boatSeat.camera(moving).position-driverEye)<.001f&&
       close(boatSeat.camera(moving),Pose{driverEye,beforeBoarding.orientation})&&boatSeat.generation==boatEpoch,
       "boarding updates translation without resetting boat heading or reference epoch");
 const auto settled=boatSeat.camera(moving);
 check(!boatSeat.followDriverPosition(moving,{NAN,0,0},12.f)&&close(boatSeat.camera(moving),settled),"invalid driver origin retains last valid seat");
 check(!boatSeat.followDriverPosition(moving,moving.position+Vec{30,0,0},12.f)&&close(boatSeat.camera(moving),settled),"unrelated far camera cannot replace driver origin");
 const auto later=hands::compose(mount,Pose{{-4.7f,.4f,.35f},{}}).position;
 check(boatSeat.followDriverPosition(mount,later,12.f)&&hands::length(boatSeat.camera(mount).position-later)<.001f,"moving and turning boat keeps current native eye origin");
 const auto headReference=boatSeat.reference;
 for(float yaw:{0.f,45.f,90.f,135.f,180.f}){
  Pose driver{later,rotation(yaw,{0,0,1})};
  check(boatSeat.followDriverPose(mount,driver,12.f)&&close(boatSeat.camera(mount),driver),"camera follows full authored boarding turn");
 }
 check(close(Pose{{},boatSeat.reference},Pose{{},headReference})&&boatSeat.generation==boatEpoch,"boarding rotation never consumes headset freelook or recenters");
 boatSeat.reset();
 check(boatSeat.update(8,91,mount,camera,{},false,12.f)&&close(boatSeat.camera(mount),camera),"exit and different boat recapture authored driver position");
 std::printf("Mounted gun/seat policy: %d failures\n",failures);return failures?1:0;
}

