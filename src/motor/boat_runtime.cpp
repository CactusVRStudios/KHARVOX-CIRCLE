#include "boat_runtime.hpp"
#include "mounted_gun_transform.hpp"
#include "locomotion_probe.hpp"
#include "hand_runtime.hpp"
#include "build_profile.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <safetyhook.hpp>
#include <mutex>

namespace circle::motor::boat {
namespace {
using namespace turret;
struct Frame {uint64_t owner{},boat{},life{},tick{};uintptr_t binding{};MountSource source{};};
std::mutex mutex;Frame frame;SeatAnchor seat;RenderMount root;
bool read(uintptr_t p,void* out,size_t size){SIZE_T got{};return p&&ReadProcessMemory(GetCurrentProcess(),(void*)p,out,size,&got)&&got==size;}
template<class T>T at(uintptr_t p){T value{};read(p,&value,sizeof(value));return value;}
bool context(const locomotion::NativeSample& n){return n.valid&&n.movementMode==24&&!n.explicitReason&&!n.animated;}
void observe(safetyhook::Context& r){
 const auto n=locomotion::latestNative();const auto now=GetTickCount64();
 if(!context(n)||now<n.tick||now-n.tick>=100)return;
 // BoatDriverUpdateJob, immediately before the native driver placement helper.
 // The main query belongs to the driver; the secondary query resolves its boat.
 const auto driver=at<uintptr_t>(r.rsp+0x68),binding=at<uintptr_t>(r.rsp+0x78);
 const auto record=at<uintptr_t>(r.rbp+0x128),transform=at<uintptr_t>(r.rbp+0x158);
 const auto job=at<uintptr_t>(r.rbp+0x370),id=at<uint64_t>(binding+8);
 if(at<uint64_t>(driver)!=n.entity||at<uintptr_t>(r.rbp-0x58)!=n.controller||
    !binding||!at<uint32_t>(binding+0x18)||!id||id==UINT64_MAX||at<uint64_t>(record)!=id)return;
 MountSource source{record,transform,at<uintptr_t>(job+0x1a8),id,at<uint64_t>(transform+0x20)};
 Pose pose;if(!readMount(source,read,pose))return;
 std::lock_guard lock(mutex);
 const auto life=hands::combatLifeGeneration();
 if(frame.owner!=n.entity||frame.boat!=id||frame.life!=life){seat.reset();root.reset();}
 frame={n.entity,id,life,now,binding,source};
}
}
bool view(const experimental::EyeRig& rig,const std::array<float,12>& camera,bool recenter,turret::View& out){
 const auto n=locomotion::latestNative();const auto now=GetTickCount64();
 std::lock_guard lock(mutex);
 if(!context(n)||frame.life!=hands::combatLifeGeneration()||frame.owner!=n.entity||
    at<uint64_t>(frame.binding+8)!=frame.boat||!at<uint32_t>(frame.binding+0x18)){
  frame={};seat.reset();root.reset();return false;
 }
 if(!rig.valid||!rig.tracked||!frame.owner||now<frame.tick)return false;
 Pose current;const bool sampled=readMount(frame.source,read,current);
 if(!root.update(n.entity,frame.boat,frame.life,sampled?&current:nullptr))return false;
 if(now-frame.tick>=100&&seat.owner!=n.entity)return false;
 auto headingCamera=camera;
 // Follow the authored turn into the seat, but not camera animation pitch/roll.
 locomotion::levelGameplayCamera(headingCamera,n.angles.y);
 const auto native=turret::pose(headingCamera);const auto ref=locomotion::uprightReference(rig.orientations[0]);
 if(!seat.update(n.entity,frame.boat,root.pose,native.value_or(Pose{{NAN,0,0},{}}),
                 {ref[0],ref[1],ref[2],ref[3]},recenter,12.f))return false;
 // Do not freeze the first frame of the boarding animation as the seat.
 // The native view origin follows the driver's authored head placement.
 // Follow native body yaw through boarding; keep XR freelook separate.
 const auto previous=seat.camera(root.pose).position;
 const bool followed=sampled&&native&&seat.followDriverPose(root.pose,*native,12.f);
 out={seat.camera(root.pose),root.pose,seat.reference,(uint64_t{1}<<62)|seat.generation};
 static uint64_t logged{};if(now-logged>=2000){logged=now;
  circlevr::vkcore::logf("motor-boat-seat: owner %llx boat %llx rootRead %d jobAge %llu camera %.4f %.4f %.4f; boat-relative freelook, native steering retained",
   n.entity,frame.boat,sampled,now-frame.tick,out.camera.position.x,out.camera.position.y,out.camera.position.z);
  circlevr::vkcore::logf("motor-boat-driver-origin: followed %d corrected %.4f native %.4f %.4f %.4f; authored boarding/head position, independent freelook",followed,hands::length(previous-out.camera.position),camera[0],camera[1],camera[2]);
 }
 return true;
}
void install(){static std::once_flag once;std::call_once(once,[]{
 constexpr unsigned char bytes[]={0x48,0x8b,0x95,0x58,0x01,0x00,0x00,0x48,0x8b,0x8d,0x70,0x03,0x00,0x00};
 if(!build::matches(0x12bd71d,bytes,sizeof(bytes)))return;
 const auto base=(uintptr_t)GetModuleHandleW(nullptr);
 auto hook=safetyhook::MidHook::create((void*)(base+build::rva(0x12bd71d)),observe);
 if(hook){new safetyhook::MidHook(std::move(*hook));circlevr::vkcore::logf("motor-boat-seat: local driver/root observer installed; no native input or camera writes");}
 });}
}
