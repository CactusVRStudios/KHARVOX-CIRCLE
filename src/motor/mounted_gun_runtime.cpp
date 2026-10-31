#include "mounted_gun_runtime.hpp"
#include "mounted_gun_transform.hpp"
#include "build_profile.hpp"
#include "hand_runtime.hpp"
#include "locomotion_probe.hpp"
#include "sequential.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <safetyhook.hpp>
#include <mutex>
#include <atomic>
#include <cstring>

namespace circle::motor::turret {
namespace {
struct Frame {uint64_t owner{},gun{},tick{},life{};Pose mount{};MountSource source{};};
std::mutex mutex;Frame frame;SeatAnchor seat;Aim aim;RenderMount renderMount;
std::atomic<uint64_t> manualUntil{};
float stickX{},stickY{};uint64_t stickTick{};
thread_local uintptr_t savedAngles{};
alignas(16) thread_local std::array<unsigned char,80> privateAngles;
bool read(uintptr_t p,void* out,size_t size){SIZE_T got{};return p&&ReadProcessMemory(GetCurrentProcess(),(void*)p,out,size,&got)&&got==size;}
template<class T>T at(uintptr_t p){T v{};read(p,&v,sizeof(v));return v;}
bool context(const locomotion::NativeSample& n){
 return n.valid&&n.movementMode==27&&n.movementType==5&&!n.explicitReason&&!n.animated&&!n.localSpace&&hands::weaponSlot(n.entity)==22;
}
void prepare(safetyhook::Context& r){
 savedAngles=0;
 const auto n=locomotion::latestNative();const auto now=GetTickCount64();
 // Verified MountedGunPlayerUpdateJob: resolved player record, view angles,
 // loadout and separate gun entity query. Validate all ownership links.
 const auto record=at<uintptr_t>(r.rsp+0x58),loadout=at<uintptr_t>(r.rbp-0x48);
 const auto transform=at<uintptr_t>(r.rbp+0xc8),gunRecord=at<uintptr_t>(r.rbp+0xa8);
 if(!context(n))return;
 const auto reject=[&](const char* reason){static std::atomic<uint64_t> logged{};auto previous=logged.load();
  if(now-previous>=2000&&logged.compare_exchange_strong(previous,now))
   circlevr::vkcore::logf("motor-mounted-gun-rejected: %s owner %llx queryOwner %llx viewMatch %d; aiming waits for validated seat",reason,n.entity,at<uint64_t>(record),r.rbx==n.viewAngles);
 };
 if(now<n.tick||now-n.tick>=100||at<uint64_t>(record)!=n.entity||
    r.rbx!=n.viewAngles||!loadout||at<uintptr_t>(r.rbp+0xb8)!=r.rdi){reject("player query ownership");return;}
 const auto gun=at<uint64_t>(loadout+0xd8);
 if(!gun||gun==UINT64_MAX||at<uint64_t>(gunRecord)!=gun){reject("gun query ownership");return;}
 Pose mount{at<Vec>(transform),at<Quat>(transform+0x10)};
 if(!hands::valid(mount)||hands::length(mount.position-Vec{n.position.x,n.position.y,n.position.z})>4||
    !read(r.rbx,privateAngles.data(),privateAngles.size())){reject("mount transform or view buffer");return;}
 const auto input=hands::latest();const auto& hand=input.hands[1];const auto& q=hand.aim.orientation;
 const bool allowed=input.focused&&input.tick&&now>=input.tick&&now-input.tick<100&&
  hand.tracked&&hand.aimTracked&&!input.hands[0].upper&&!input.hands[0].menu&&
  !(input.hands[0].click&&hand.click)&&!sequential::menuActive()&&sequential::projectionAllowed();
 Vec native{},low{},high{};std::memcpy(&native,privateAngles.data()+0x40,12);
 std::memcpy(&low,privateAngles.data(),12);std::memcpy(&high,privateAngles.data()+12,12);
 if(!hands::finite(native)||!hands::finite(low)||!hands::finite(high))return;
 std::lock_guard lock(mutex);
 const auto life=hands::combatLifeGeneration();
 if(frame.life!=life){frame={};seat.reset();aim.reset();renderMount.reset();}
 const bool stickFresh=now>=stickTick&&now-stickTick<100;
 const Vec stick=stickFresh?Vec{stickX,stickY,0}:Vec{};
 const bool manual=now<manualUntil.load()||std::hypot(stick.x,stick.y)>=.15f;
 const float dt=frame.owner==n.entity&&frame.gun==gun&&now>=frame.tick?float(now-frame.tick)*.001f:0.f;
 frame={n.entity,gun,now,life,mount,{gunRecord,transform,at<uintptr_t>(r.r14+0x168),gun,at<uint64_t>(transform+0x20)}};
 const auto wanted=aim.update(n.entity,gun,input.session,input.time>0?uint64_t(input.time):0,
  {q.x,q.y,q.z,q.w},native,low,high,allowed,manual,stick,dt);
 std::memcpy(privateAngles.data()+0x40,&wanted,12);
 savedAngles=r.rbx;r.rbx=reinterpret_cast<uintptr_t>(privateAngles.data());
 static uint64_t logged{};if(now-logged>=2000){logged=now;
  circlevr::vkcore::logf("motor-mounted-gun: owner %llx gun %llx mount %.4f %.4f %.4f controller %d manual %d native %.2f %.2f gunOnly %.2f %.2f; private aim, no player-angle write",
   n.entity,gun,mount.position.x,mount.position.y,mount.position.z,allowed,manual,native.x,native.y,wanted.x,wanted.y);}
}
void restore(safetyhook::Context& r){if(savedAngles){r.rbx=savedAngles;savedAngles=0;}}
}
void manualInput(bool manual){if(manual)manualUntil=GetTickCount64()+100;}
void stickInput(float x,float y){
 std::lock_guard lock(mutex);stickX=input::axis(x);stickY=input::axis(y);stickTick=GetTickCount64();
 if(std::hypot(stickX,stickY)>=.15f)manualUntil=stickTick+100;
}
bool ready(uint64_t owner){
 const auto now=GetTickCount64();std::lock_guard lock(mutex);
 return seat.owner==owner&&frame.owner==owner&&seat.gun==frame.gun&&now>=frame.tick&&now-frame.tick<100;
}
bool view(const experimental::EyeRig& rig,const std::array<float,12>& camera,bool recenter,View& out){
 const auto n=locomotion::latestNative();const auto now=GetTickCount64();
 std::lock_guard lock(mutex);
 if(!context(n)||frame.life!=hands::combatLifeGeneration()){seat.reset();aim.reset();renderMount.reset();return false;}
 if(!rig.valid||!rig.tracked||!frame.owner||frame.owner!=n.entity||now<frame.tick)return false;
 // A temporarily missing native update must not release an established seat
 // back to the aim camera. Input waits for freshness; retain the last mount.
 if(now-frame.tick>=100&&(seat.owner!=n.entity||seat.gun!=frame.gun))return false;
 // The job snapshot can lag the moving airplane by a simulation step. Read
 // its generation-validated root now and latch it once for the stereo pair.
 Pose current;
 const bool sampled=now-frame.tick<100&&readMount(frame.source,read,current);
 if(!renderMount.update(n.entity,frame.gun,frame.life,sampled?&current:nullptr)){
  static uint64_t missed{};if(now-missed>=2000){missed=now;
   circlevr::vkcore::logf("motor-mounted-seat-source: waiting owner %llx gun %llx age %llu; no coherent view-time root",n.entity,frame.gun,now-frame.tick);}
  return false;
 }
 const auto mount=renderMount.pose;
 const auto native=pose(camera);if(!native&&seat.owner!=n.entity)return false;
 // Head yaw is calibrated at entry; real head pitch/roll remain available.
 const auto ref=locomotion::uprightReference(rig.orientations[0]);
 if(!seat.update(n.entity,frame.gun,mount,native.value_or(Pose{{NAN,0,0},{}}),{ref[0],ref[1],ref[2],ref[3]},recenter))return false;
 out={seat.camera(mount),mount,seat.reference,(uint64_t(1)<<63)|seat.generation};
 static uint64_t samples{},held{};static float maxDelta{};
 ++samples;held+=!sampled;maxDelta=std::max(maxDelta,hands::length(mount.position-frame.mount.position));
 static uint64_t logged{};if(now-logged>=2000){logged=now;
  circlevr::vkcore::logf("motor-mounted-seat: owner %llx gun %llx epoch %llu camera %.4f %.4f %.4f native %.4f %.4f %.4f; mount-relative, independent of both aiming inputs",
   n.entity,frame.gun,out.epoch,out.camera.position.x,out.camera.position.y,out.camera.position.z,camera[0],camera[1],camera[2]);
  circlevr::vkcore::logf("motor-mounted-seat-source: owner %llx gun %llx samples %llu held %llu maxJobToView %.5f m jobAge %llu ms; view-time root shared by camera and hands, no simulation fallback",n.entity,frame.gun,samples,held,maxDelta,now-frame.tick);
  samples=held=0;maxDelta=0;
 }
 return true;
}
void install(){
 static std::once_flag once;std::call_once(once,[]{
  constexpr unsigned char begin[]={0xf3,0x0f,0x10,0x53,0x40,0xf3,0x44,0x0f,0x58,0xe0};
  constexpr unsigned char end[]={0x48,0x8b,0x45,0xe8,0x4c,0x8b,0x55,0xe0};
  if(!build::matches(0x14d3792,begin,sizeof(begin))||!build::matches(0x14d3a3c,end,sizeof(end)))return;
  const auto base=(uintptr_t)GetModuleHandleW(nullptr);
  auto a=safetyhook::MidHook::create((void*)(base+build::rva(0x14d3792)),prepare);
  auto b=safetyhook::MidHook::create((void*)(base+build::rva(0x14d3a3c)),restore);
  if(a&&b){new safetyhook::MidHook(std::move(*a));new safetyhook::MidHook(std::move(*b));
   circlevr::vkcore::logf("motor-mounted-gun: private gun-angle path and mount observer installed; native camera component unchanged");}
 });
}
}
