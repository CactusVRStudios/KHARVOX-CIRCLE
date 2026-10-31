#include "laser_sight.hpp"
#include "cheat_runtime.hpp"
#include "laser_policy.hpp"
#include "laser_hooks.hpp"
#include "hand_runtime.hpp"
#include "hand_pose.hpp"
#include "interaction_policy.hpp"
#include "build_profile.hpp"
#include "sequential.hpp"
#include "vkcore/log.hpp"
#include "vkcore/seh_filter.hpp"
#include <windows.h>
#include <safetyhook.hpp>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstring>
namespace circle::motor::laser {
using namespace hands;
namespace {
struct Weapon {uint64_t owner{},entity{},tick{},life{};uintptr_t active{},state{},attachment{},pool{},loadout{};unsigned slot{};};
struct Collision {uintptr_t query{};uint64_t owner{},tick{};unsigned group{};};
std::mutex mutex;Weapon weapon;Collision collision;
std::atomic<bool> installed{};
bool read(uintptr_t p,void* out,size_t size){SIZE_T got{};return p&&ReadProcessMemory(GetCurrentProcess(),(void*)p,out,size,&got)&&got==size;}
template<class T>T at(uintptr_t p){T v{};read(p,&v,sizeof(v));return v;}
bool fresh(uint64_t tick,uint64_t now){return tick&&now>=tick&&now-tick<100;}
void observeWeapon(safetyhook::Context& r){
 // idLoadoutUpdateJob: completed equipped attachment query, before native action evaluation.
 const auto n=locomotion::latestNative();const auto owner=at<uint64_t>(r.r12);
 if(!n.valid||owner!=n.entity)return;
 Weapon w;w.owner=owner;w.tick=GetTickCount64();w.life=combatLifeGeneration();w.loadout=r.r13;
 const auto q=r.rbp+0x450;
 w.slot=at<unsigned char>(w.loadout+0x10c);
 if(w.slot<28){
  w.entity=at<uint64_t>(at<uintptr_t>(q+0x18));w.active=at<uintptr_t>(q+0x28);
  // Query +0x38 is physics, not a render model. Use the attachment muzzle.
  w.attachment=at<uintptr_t>(q+0x48);
  w.pool=at<uintptr_t>(r.rsi+0x7b0);w.state=at<uintptr_t>(r.rbp+0x168);
 }
 cheats::weapon(w.owner,w.entity,w.loadout,w.slot,w.attachment,w.active);
 std::lock_guard lock(mutex);weapon=w;
}
void observeCollision(safetyhook::Context& r){
 // Native FocusTrackerLine call: RCX is the collision singleton component;
 // the native caller supplies its collision group at stack+40.
 const auto n=locomotion::latestNative();
 const auto owner=at<uint64_t>(r.rsp+0x38);
 if(!n.valid||owner!=n.entity)return;
 std::lock_guard lock(mutex);collision={at<uintptr_t>(r.rcx),owner,GetTickCount64(),at<unsigned>(r.rsp+0x40)};
}
bool canShoot(const Weapon& w){
 const auto now=GetTickCount64();const auto n=locomotion::latestNative();const auto input=latest();
 if(!installed||!enabled()||!fresh(w.tick,now)||w.life!=combatLifeGeneration()||!n.valid||!fresh(n.tick,now)||n.entity!=w.owner||
  !equippedFirearm(w.owner)||weaponSlot(w.owner)!=w.slot||!locomotion::laserReadyInput()||
  !input.focused||!fresh(input.tick,now)||!input.hands[1].tracked||!input.hands[1].aimTracked||
  !locomotion::trackedFirearmState(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace)||
  !sequential::projectionAllowed()||sequential::menuActive()||!w.active||!w.state||!w.attachment||!w.entity)return false;
 uint64_t attachedOwner{},currentWeapon{};std::array<unsigned char,2> hidden{};
 if(!read(w.loadout+0x28+w.slot*8,&currentWeapon,8)||currentWeapon!=w.entity||
  !read(w.attachment,&attachedOwner,8)||attachedOwner!=w.owner||!read(w.attachment+0x201,hidden.data(),2)||hidden[0]||hidden[1])return false;
 std::array<unsigned char,0x368> active{};std::array<unsigned char,0x30> state{};
 return read(w.active,active.data(),active.size())&&read(w.state,state.data(),state.size())&&nativeReady(active,state,n.gameTime);
}
bool worldMuzzle(const Weapon& w,Pose& p){
 uint64_t handle{};if(!read(w.attachment+worldMuzzleOffset,&handle,8)||handle==UINT64_MAX)return false;
 const uint64_t index=handle&((uint64_t{1}<<24)-1),offset=(handle>>40)&0x3ff;
 const auto data=at<uintptr_t>(w.pool),count=at<uint64_t>(w.pool+8);
 if(!data||count>10000000||index+offset>=count||at<uint16_t>(data+index*32+12)!=uint16_t(handle>>24))return false;
 const auto entry=data+(index+offset)*32;
 return read(entry,&p.position,sizeof(Vec))&&read(entry+16,&p.orientation,sizeof(Quat))&&valid(p);
}
// Motor's synchronous multi-hit cast, used by its native floating camera and ping jobs.
// No queued collision request, game action, projectile or damage is created.
using Cast=void(__fastcall*)(uintptr_t,void*,const Vec*,const Vec*,void*,void*,unsigned,uint64_t,unsigned,unsigned,const char*);
bool trace(uintptr_t fn,const Collision& c,uint64_t owner,Vec from,Vec to,float& fraction){
 alignas(16) unsigned char result[0x328]{};
 __try {
  reinterpret_cast<Cast>(fn)(c.query,result,&from,&to,nullptr,nullptr,0x101085,owner,c.group,2,"KHARVOX Laser Sight");
 } __except(circlevr::vkcore::accessViolationOnly(GetExceptionCode())){return false;}
 int count{};std::memcpy(&count,result,4);if(count<0||count>5)return false;
 fraction=1;
 for(int i=0;i<count;++i){float f{};std::memcpy(&f,result+8+i*0xa0,4);if(!std::isfinite(f)||f<0||f>1)return false;fraction=std::min(fraction,f);}
 return true;
}
}
bool enabled(){static const bool on=[] {char b[8]{};return GetEnvironmentVariableA("CIRCLE_LASER_SIGHT",b,8)==1&&b[0]=='1';}();return on;}
bool visible(uint64_t owner,uint64_t entity,uint64_t life){Weapon w;{std::lock_guard lock(mutex);w=weapon;}return w.owner==owner&&w.entity==entity&&w.life==life&&canShoot(w);}
void publish(std::array<experimental::LiveEye,2>& eyes){
 const auto blocked=[](const char* reason){static uint64_t last{};const auto now=GetTickCount64();if(now-last>2000){last=now;circlevr::vkcore::logf("motor-laser: blocked %s",reason);}};
 for(auto& e:eyes)e.laser={};
 if(!enabled())return;
 if(!installed){blocked("hooks not installed");return;}
 Weapon w;Collision c;{std::lock_guard lock(mutex);w=weapon;c=collision;}
 if(!canShoot(w)){blocked("weapon readiness/input/state");return;}
 if(!c.query||c.owner!=w.owner||!fresh(c.tick,GetTickCount64())){blocked("collision context");return;}
 const auto& e=eyes[0];if(!e.valid||!eyes[1].valid||e.frameToken!=eyes[1].frameToken||!e.rig.valid||!e.rig.tracked)return;
 Pose nativeMuzzle{};
 if(!worldMuzzle(w,nativeMuzzle)){blocked("world muzzle handle");return;}
 // The native world muzzle already includes the model-local muzzle transform.
 // Applying the same rigid render delta to it is equivalent to correcting the
 // model first and composing its local muzzle. No physics/render pointer cast.
 auto model=muzzlePlacement(nativeMuzzle);
 if(!renderModelPlacement(w.owner,e.frameToken,model,true)){blocked("frame-matched muzzle placement");return;}
 const auto rotation=rotationBasis({model[3],model[4],model[5]},{model[6],model[7],model[8]},{model[9],model[10],model[11]});
 if(!rotation){blocked("muzzle axes");return;}
 const std::optional<Pose> muzzle=Pose{{model[0],model[1],model[2]},*rotation};
 const Vec from=muzzle->position,to=from+rotate(muzzle->orientation,{100,0,0});float fraction{};
 if(!trace((uintptr_t)GetModuleHandleW(nullptr)+build::rva(0xa490c0),c,w.owner,from,to,fraction)||fraction<=0.0001f){blocked("collision trace");return;}
 const auto center=interaction::centerView(e.camera,eyes[1].camera);if(!center)return;
 const auto& q=e.rig.orientations[0];const Quat head{q[0],q[1],q[2],q[3]};
 const auto& l=e.rig.positions[0];const auto& r=e.rig.positions[1];const Vec headPosition{(l[0]+r[0])*.5f,(l[1]+r[1])*.5f,(l[2]+r[2])*.5f};
 const auto toLocal=[&](Vec world){return localPoint(world,*center,Pose{headPosition,head});};
 const auto start=toLocal(from),end=toLocal(from+(to-from)*fraction);
 if(!finite(start)||!finite(end))return;
 experimental::LaserRay ray;ray.valid=true;ray.hit=fraction<1.f;ray.owner=w.owner;ray.weapon=w.entity;ray.life=w.life;ray.tick=GetTickCount64();ray.from={start.x,start.y,start.z};ray.to={end.x,end.y,end.z};
 for(auto& eye:eyes)eye.laser=ray;
 static uint64_t logged{};if(ray.tick-logged>2000){logged=ray.tick;circlevr::vkcore::logf("motor-laser: owner %llx weapon %llx slot %u frame %llu range %.3f; native muzzle, synchronous collision, frame-matched LOCAL",w.owner,w.entity,w.slot,e.frameToken,100*fraction);}
}
void install(){
 if(!enabled()&&!cheats::ammoEnabled())return;
 static std::once_flag once;std::call_once(once,[]{
  struct Hook{uintptr_t site;const char* bytes;size_t size;void(*callback)(safetyhook::Context&);};
  std::vector<Hook> hooks={{0x144b9b3,"\x4c\x8b\xb5\x68\x01\x00\x00",7,observeWeapon}};
  if(enabled())hooks.push_back({0x1380133,"\xe8\x78\x1e\x67\x00",5,observeCollision});
  for(const auto& h:hooks)if(!build::matches(h.site,h.bytes,h.size)){circlevr::vkcore::logf("motor-laser: unknown hook; disabled");return;}
  if(enabled()&&!build::matches(0xa490c0,castPrologue,sizeof(castPrologue))){circlevr::vkcore::logf("motor-laser: collision cast signature mismatch; disabled");return;}
  std::vector<safetyhook::MidHook> pending;
  for(const auto& h:hooks){auto hook=safetyhook::MidHook::create((void*)((uintptr_t)GetModuleHandleW(nullptr)+build::rva(h.site)),h.callback);if(!hook){circlevr::vkcore::logf("motor-laser: hook creation failed at canonical %llx; disabled",h.site);return;}pending.push_back(std::move(*hook));}
  for(auto& h:pending)new safetyhook::MidHook(std::move(h));installed=true;
  circlevr::vkcore::logf("motor-laser: native weapon hook installed; laser %d ammo cheat %d",enabled(),cheats::ammoEnabled());
  circlevr::vkcore::logf("motor-laser: installation complete");
 });
}
}
