#include "motor/diagnostics.hpp"
#include "motor/build_profile.hpp"
#include "hand_runtime.hpp"
#include "framegen/bridge.hpp"
#include "hand_pose.hpp"
#include "punch_charge.hpp"
#include "physical_melee_timing.hpp"
#include "thumb_rest_gesture.hpp"
#include "mouth_consume.hpp"
#include "mounted_gun_policy.hpp"
#include "firearm_policy.hpp"
#include "interaction_policy.hpp"
#include "controller_tracking.hpp"
#include "locomotion_probe.hpp"
#include "sequential.hpp"
#include "vkcore/log.hpp"
#include "vkcore/seq_hooks.hpp"
#include <windows.h>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <atomic>

namespace circle::motor::hands {
namespace {
struct Targets {std::array<Pose,2> world{};std::array<bool,2> tracked{};Vec camera{},physics{};uint64_t owner{},tick{},epoch{};experimental::EyeRig eyeRig{};locomotion::CameraMotion motion{};Quat headOrientation{};};
std::mutex targetMutex,applyMutex;Targets targets;
struct AttachmentAudit {Pose wrist{},anchor{},reference{};unsigned slot{};bool valid{},bound{};};
struct AppliedTargets {Targets targets{};HandRenderStamp stamp{};uintptr_t rig{};Pose model{};Vec solvedCamera{};AttachmentAudit attachment{};Pose nativeModel{};bool nativeBodyFrame{};};
AppliedTargets appliedTargets;
struct RenderAnchor {HandRenderStamp stamp{};uint64_t token{};Pose model{},solvedModel{};AttachmentAudit attachment{};Vec solvedCamera{},renderCamera{};bool mounted{};};
RenderAnchor renderAnchor;
struct WeaponPose {Pose native{},tracked{};Vec physics{};uint64_t owner{},tick{},epoch{},weapon{};unsigned slot{};};
std::mutex weaponPoseMutex;WeaponPose weaponPose;
struct MuzzleFlashStamp {uint64_t owner{},weapon{},tick{},life{};Pose pose{};NativeMuzzleSource source{};uint64_t epoch{};bool anchored{};};
std::mutex muzzleFlashMutex;std::array<MuzzleFlashStamp,16> muzzleFlashes{};unsigned nextMuzzleFlash{};
std::mutex supportMutex;
struct SupportState {uint64_t owner{},weapon{},epoch{},tick{},inputTick{};Pose fromAim{};SupportGrab grab{};};
SupportState supportState;
std::mutex combatMutex;
uint64_t combatOwner{},combatTick{};unsigned combatSlot{};bool combatDead=true;
uintptr_t combatLoadout{};bool combatFirearm{};
uint64_t weaponKindOwner{},weaponKindEntity{},weaponKindTick{};uintptr_t weaponKindLoadout{};int equippedPose=-1;
bool pickupCapability(uint64_t owner,unsigned slot,uintptr_t loadout,uint64_t now);
std::array<uint64_t,2> punchUntil{};
std::array<uint64_t,2> punchCreditUntil{};
std::array<unsigned,2> punchChargeMs{},punchChargeStages{};
std::atomic<bool> chargeInstalled{};
std::atomic<uint64_t> firearmMeleeUntil{},combatGeneration{1};
std::mutex interactionMutex;
std::array<std::array<float,2>,2> scriptedAxes{};
uint64_t interactionTick{},interactionOwner{};
std::atomic<uint64_t> interactionEpoch{};
std::atomic<bool> whipInstalled{};
std::mutex consumeMutex;
struct Digestible {uint64_t owner{},item{},tick{},life{};uintptr_t loadout{},decl{};unsigned slot{};bool ready{};};
Digestible digestible;
struct MouthTracking {uint64_t owner{},session{},tick{},sample{},epoch{};float reach{-1};};
MouthTracking mouthTracking;MouthConsume mouthConsume;
ShoulderWhip shoulderWhip;WhipOutput whipOutput;
uint64_t whipOwner{},whipTick{};
bool punchEnabled(){static const bool on=[] {char b[8]{};return GetEnvironmentVariableA("CIRCLE_PHYSICAL_PUNCH",b,8)&&b[0]=='1';}();return on;}
bool enabled(){static const bool on=[] {char b[8]{};return GetEnvironmentVariableA("CIRCLE_HAND_IK",b,8)&&b[0]=='1';}();return on;}
bool read(uintptr_t p,void* out,size_t n){SIZE_T got{};return p&&ReadProcessMemory(GetCurrentProcess(),(void*)p,out,n,&got)&&got==n;}
template<class T>T at(uintptr_t p){T out{};read(p,&out,sizeof(out));return out;}
bool write(uintptr_t p,const void* data,size_t n){SIZE_T got{};return WriteProcessMemory(GetCurrentProcess(),(void*)p,data,n,&got)&&got==n;}
Pose unpack(const std::array<float,12>& v){return {{v[4],v[5],v[6]},{v[0],v[1],v[2],v[3]}};}
void pack(std::array<float,12>& v,Pose p){v[0]=p.orientation.x;v[1]=p.orientation.y;v[2]=p.orientation.z;v[3]=p.orientation.w;v[4]=p.position.x;v[5]=p.position.y;v[6]=p.position.z;}
void suppressNativeBob(){
 // Motor registrations 226F30 / 2A69B0, not Eternal's addresses.
 // Keep numeric lanes coherent; profile loads can re-enable the hand cycle.
 const auto base=(uintptr_t)GetModuleHandleW(nullptr);
 struct Control{uintptr_t rva;const char* name;int value;};
 static constexpr Control controls[]={{0x4543060,"g_setting_hands_bob",0},{0x4583750,"view_skipShakes",1},
  {0x44cd420,"r_skipFlares",1},{0x44d4150,"r_lensFlaresRatio",0}};
 for(const auto& c:controls){
  const auto wrapper=base+build::rva(c.rva);char name[64]{};
  if(!read(at<uintptr_t>(wrapper+0x30),name,std::strlen(c.name)+1)||std::strcmp(name,c.name))continue;
  const auto internal=at<uintptr_t>(wrapper);struct Numeric{int integer;float floating;} before{},after{c.value,float(c.value)};
  if(!read(internal+8,&before,sizeof(before))||before.integer<0||before.integer>1||!std::isfinite(before.floating))continue;
  if(before.integer==after.integer&&before.floating==after.floating)continue;
  if(write(internal+8,&after,sizeof(after)))circlevr::vkcore::logf("motor-vr-bob: %s %d/%.3f -> %d (validated native control)",c.name,before.integer,before.floating,c.value);
 }
}
bool namedArms(uintptr_t rig){
 auto decl=at<uintptr_t>(rig+0x718),list=at<uintptr_t>(decl+0x60);
 if(at<int>(decl+0x68)!=2)return false;
 for(unsigned h=0;h<2;++h){char name[32]{};if(!read(at<uintptr_t>(list+h*0x180+0x30+8),name,sizeof(name)))return false;
  name[31]=0;if(std::strcmp(name,h?"lefthand":"righthand"))return false;}
 return true;
}
}
void observeCombatContext(uint64_t entity,unsigned weaponSlot,bool dead,uintptr_t loadout){
 std::lock_guard lock(combatMutex);
 if(combatOwner!=entity||combatDead!=dead)++combatGeneration;
 if(combatOwner!=entity||combatSlot!=weaponSlot||combatDead!=dead){punchUntil={};punchCreditUntil={};punchChargeMs={};punchChargeStages={};}
 if(combatOwner!=entity||combatSlot!=weaponSlot||combatDead!=dead)circlevr::vkcore::logf("motor-combat-context: owner %llx weapon slot %u dead %d",entity,weaponSlot,dead);
 combatFirearm=firearmSlot(weaponSlot)||pickupCapability(entity,weaponSlot,loadout,GetTickCount64());
 combatOwner=entity;combatSlot=weaponSlot;combatDead=dead;combatLoadout=loadout;combatTick=GetTickCount64();
}
namespace {
bool pickupCapability(uint64_t owner,unsigned slot,uintptr_t loadout,uint64_t now){
 return owner&&owner==weaponKindOwner&&loadout==weaponKindLoadout&&now>=weaponKindTick&&now-weaponKindTick<100&&
  pickupFirearmPose(slot,equippedPose)&&weaponKindEntity&&weaponKindEntity!=UINT64_MAX&&
  weaponKindEntity==at<uint64_t>(loadout+0x28+10*8);
}
}
void observeEquippedWeaponPose(uint64_t owner,unsigned slot,uintptr_t loadout,int pose){
 const auto now=GetTickCount64();const auto weapon=slot<28?at<uint64_t>(loadout+0x28+slot*8):0;
 std::lock_guard lock(combatMutex);
 if(weaponKindOwner!=owner||weaponKindEntity!=weapon||equippedPose!=pose)
  circlevr::vkcore::logf("motor-firearm-capability: owner %llx weapon %llx slot %u native pose %d pickup firearm %d",owner,weapon,slot,pose,pickupFirearmPose(slot,pose));
 weaponKindOwner=owner;weaponKindEntity=weapon;weaponKindLoadout=loadout;weaponKindTick=now;equippedPose=pose;
 if(combatOwner==owner&&combatSlot==slot&&combatLoadout==loadout)
  combatFirearm=firearmSlot(slot)||pickupCapability(owner,slot,loadout,now);
}
uint64_t combatLifeGeneration(){return combatGeneration.load();}
void observeDigestible(uintptr_t loadout,uintptr_t declaration){
 const auto now=GetTickCount64();Digestible next;
 {std::lock_guard lock(combatMutex);
  if(!loadout||loadout!=combatLoadout||combatDead||now-combatTick>=100||combatSlot>=28)return;
  // The native query REQUIRES Digestible presence, but its 0x10 access flag
  // requests no data pointer. RBP+18 is therefore null even on valid food.
  // Native consumption owns consumed/started flags; do not reject this query
  // or dereference a presence-only component output.
  unsigned category{};
  if(!read(declaration+0xf8,&category,4)||!edibleCategory(category))return;
  const auto slot=at<unsigned char>(loadout+0x10c);
  if(slot!=combatSlot)return;
  next={combatOwner,at<uint64_t>(loadout+0x28+slot*8),now,combatGeneration.load(),loadout,declaration,slot,true};
 }
 std::lock_guard lock(consumeMutex);
 if(next.owner!=digestible.owner||next.item!=digestible.item||next.decl!=digestible.decl||next.ready!=digestible.ready||now-digestible.tick>1000)
  circlevr::vkcore::logf("motor-mouth-food: owner %llx item %llx slot %u ready %d; native digestible health/stamina item",next.owner,next.item,next.slot,next.ready);
 digestible=next;
}
bool mouthConsumeInput(const Snapshot& input,uint64_t owner,bool allowed,bool manual){
 const auto now=GetTickCount64();uint64_t item{},life{};uintptr_t loadout{};unsigned slot{};
 {std::lock_guard lock(combatMutex);
  life=combatGeneration.load();slot=combatSlot;loadout=combatLoadout;
  if(combatOwner==owner&&!combatDead&&now-combatTick<100&&slot<28&&loadout)
   item=at<uint64_t>(loadout+0x28+slot*8);
 }
 std::lock_guard lock(consumeMutex);const auto& t=mouthTracking;const auto& d=digestible;
 const bool fresh=allowed&&input.focused&&input.hands[1].tracked&&input.tick&&now>=input.tick&&now-input.tick<100&&
  t.owner==owner&&t.session==input.session&&t.epoch==interactionEpoch.load()&&t.tick&&now>=t.tick&&now-t.tick<100&&
  d.owner==owner&&d.item==item&&d.slot==slot&&d.loadout==loadout&&d.life==life&&d.ready&&d.tick&&now>=d.tick&&now-d.tick<100;
 const bool pulse=mouthConsume.update(owner,item,input.session,life,now,t.sample,t.reach,fresh,manual);
 static uint64_t statusTick{};
 if(item&&d.item==item&&d.owner==owner&&now-statusTick>=2000){
  statusTick=now;circlevr::vkcore::logf("motor-mouth-status: item %llx reach %.3f allowed %d fresh %d manual %d armed %d foodAge %llu poseAge %llu; entry <=1 withdrawal >=1.55",
   item,t.reach,allowed,fresh,manual,mouthConsume.armed,now>=d.tick?now-d.tick:0,now>=t.tick?now-t.tick:0);
 }
 static bool previous{};
 if(pulse!=previous)circlevr::vkcore::logf("motor-mouth-use: RT %d owner %llx item %llx reach %.3f; native consumption rules retained",pulse,owner,item,t.reach);
 previous=pulse;return pulse;
}
bool equippedFood(uint64_t owner){
 const auto now=GetTickCount64();std::lock_guard combatLock(combatMutex);
 if(!owner||combatOwner!=owner||combatDead||now-combatTick>=100||combatSlot>=28||!combatLoadout)return false;
 const auto item=at<uint64_t>(combatLoadout+0x28+combatSlot*8);
 std::lock_guard consumeLock(consumeMutex);const auto& d=digestible;
 return item&&item!=UINT64_MAX&&d.owner==owner&&d.item==item&&d.slot==combatSlot&&
  d.loadout==combatLoadout&&d.life==combatGeneration.load()&&d.tick&&now>=d.tick&&now-d.tick<100;
}
unsigned weaponSlot(uint64_t owner){
 std::lock_guard lock(combatMutex);
 return owner&&combatOwner==owner&&!combatDead&&GetTickCount64()-combatTick<100?combatSlot:0;
}
bool equippedFirearm(uint64_t owner){
 std::lock_guard lock(combatMutex);
 return owner&&combatOwner==owner&&!combatDead&&GetTickCount64()-combatTick<100&&combatFirearm;
}
bool supportGripInput(const Snapshot& input,uint64_t owner,bool allowed){
 const auto now=GetTickCount64();uint64_t weapon{};
 {std::lock_guard lock(combatMutex);if(combatOwner==owner&&combatSlot<28&&combatLoadout)weapon=at<uint64_t>(combatLoadout+0x28+combatSlot*8);}
 std::lock_guard lock(supportMutex);auto& state=supportState;
 if(state.owner!=owner||state.weapon!=weapon){state={};return false;}
 const auto& left=input.hands[0];const auto& right=input.hands[1];
 const bool safe=allowed&&state.tick&&now-state.tick<100&&input.focused&&now-input.tick<100&&
  left.tracked&&right.tracked&&right.aimTracked&&
  !left.upper&&!left.menu&&!left.lower&&!right.click;
 Pose primary{{right.grip.position.x,right.grip.position.y,right.grip.position.z},{right.aim.orientation.x,right.aim.orientation.y,right.aim.orientation.z,right.aim.orientation.w}};
 const Vec hand{left.grip.position.x,left.grip.position.y,left.grip.position.z};
 const bool withinReach=length(compose(primary,state.fromAim).position-hand)<=.16f;
 const bool was=state.grab.latched;
 const bool reserved=state.grab.update(safe,left.squeeze,withinReach);state.inputTick=now;
 if(was!=state.grab.latched)circlevr::vkcore::logf("motor-two-hand: %s owner %llx weapon %llx; left grip reserved, firing retained",state.grab.latched?"grab":"release",owner,weapon);
 return reserved;
}
uint64_t temporaryFirearm(uint64_t owner){
 std::lock_guard lock(combatMutex);
 if(!owner||combatOwner!=owner||combatDead||GetTickCount64()-combatTick>=100||!combatFirearm||combatSlot!=10)return 0;
 const auto entity=at<uint64_t>(combatLoadout+0x28+10*8);
 return entity!=UINT64_MAX?entity:0;
}
void maintainComfortControls(){if(sequential::requested())suppressNativeBob();}
void enableWhipInput(){whipInstalled=true;}
void enablePunchCharge(){chargeInstalled=true;}
void setFirearmMeleeMode(bool on){firearmMeleeUntil=on?GetTickCount64()+100:0;}
unsigned consumePunchCharge(uintptr_t loadout,unsigned nativeHand,unsigned stage){
 const auto slot=weaponSlot(locomotion::latestNative().entity);
 if(!meleeChargeSlot(slot)||(nativeHand>1&&(nativeHand!=UINT_MAX||slot==1))||stage>3)return 0;
 const auto input=latest();const auto native=locomotion::latestNative();
 const unsigned hand=nativeHand==UINT_MAX?1:1-nativeHand; // native weapon-default hand is -1.
 const auto now=GetTickCount64();
 if(!native.valid||now-native.tick>100||!input.focused||now-input.tick>100||!input.hands[hand].tracked||
    !fistReady(input.hands[hand].squeeze)||input.hands[1].click||input.hands[0].upper||input.hands[0].menu||
    native.animated||native.localSpace||native.movementType||native.explicitReason||
    !trackedArmMovement(native.movementMode)||!sequential::projectionAllowed()||sequential::menuActive())return 0;
 std::lock_guard lock(combatMutex);
 if(loadout!=combatLoadout||combatOwner!=native.entity||(punchChargeStages[hand]&(1u<<stage))||now>=punchCreditUntil[hand])return 0;
 if(stage==3&&!physicalChargeReleaseReady(punchChargeMs[hand],2,1-hand,punchChargeStages[hand]))return 0;
 punchChargeStages[hand]|=1u<<stage;
 if(stage==3)punchUntil[hand]=0; // Accepted charged stroke owns release; stop its synthetic hold.
 return punchChargeMs[hand];
}
bool consumePhysicalStrikeTiming(uintptr_t loadout,unsigned action){
 const auto n=locomotion::latestNative();const auto input=latest();const auto now=GetTickCount64();
 if(!punchEnabled()||!acceleratedStrike(action)||!n.valid||now-n.tick>100||!input.focused||now-input.tick>100||
    input.hands[1].click||input.hands[0].upper||input.hands[0].menu||n.animated||n.localSpace||n.movementType||n.explicitReason||
    !trackedArmMovement(n.movementMode)||!sequential::projectionAllowed()||sequential::menuActive())return false;
 std::lock_guard lock(combatMutex);
 if(loadout!=combatLoadout||combatOwner!=n.entity||combatDead||now-combatTick>100||
    !(combatSlot==1||itemMeleeSlot(combatSlot)))return false;
 for(unsigned h=0;h<2;++h){
  if((combatSlot!=1&&h!=1)||!input.hands[h].tracked||!fistReady(input.hands[h].squeeze)||
     !matchingStrikeCredit(action,punchChargeMs[h],punchChargeStages[h],now,punchCreditUntil[h]))continue;
  punchChargeStages[h]|=16;punchUntil[h]=0;return true;
 }
 return false;
}
bool consumeOrdinaryItemStrike(uintptr_t loadout,unsigned nativeHand,unsigned state){
 const auto n=locomotion::latestNative();const auto input=latest();const auto now=GetTickCount64();
 const auto slot=weaponSlot(n.entity);const unsigned hand=nativeHand==UINT_MAX?1:1-nativeHand;
 if(!punchEnabled()||!(slot==1||itemMeleeSlot(slot))||!physicalStrikeHandValid(slot==1,nativeHand)||!n.valid||now-n.tick>100||
    !input.focused||now-input.tick>100||!input.hands[hand].tracked||!fistReady(input.hands[hand].squeeze)||
    input.hands[1].click||input.hands[0].upper||input.hands[0].menu||n.animated||n.localSpace||n.movementType||n.explicitReason||
    !trackedArmMovement(n.movementMode)||!sequential::projectionAllowed()||sequential::menuActive())return false;
 std::lock_guard lock(combatMutex);
 if(loadout!=combatLoadout||combatOwner!=n.entity||combatSlot!=slot||combatDead||now>=punchCreditUntil[hand]||
    !physicalItemReleaseReady(punchChargeMs[hand],state,hand,punchChargeStages[hand]))return false;
 punchChargeStages[hand]|=8;punchUntil[hand]=0;
 return true;
}
WhipOutput whipInput(const Snapshot& input,const locomotion::NativeSample& native,uintptr_t loadout){
 if(!whipInstalled||!enabled()||!input.focused||!input.hands[1].tracked||input.hands[0].upper||input.hands[0].menu||
  (input.hands[0].click&&input.hands[1].click)||sequential::menuActive()||!sequential::projectionAllowed())return {};
 std::lock_guard lock(combatMutex);const auto now=GetTickCount64();
 if(loadout&&loadout!=combatLoadout)return {};
 if(!native.valid||now-native.tick>100||now-input.tick>100||now-whipTick>100||now-combatTick>100||combatDead||
  native.entity!=whipOwner||native.entity!=combatOwner||native.animated||native.localSpace||native.movementType||native.explicitReason||!trackedArmMovement(native.movementMode))return {};
 auto out=whipOutput;
 if(combatSlot!=1&&combatSlot!=18)return {};
 if(combatSlot!=18||input.hands[1].squeeze<=.35f)out.actions&=~whipCrack;
 return out;
}
unsigned punchTriggers(const Snapshot& input,const locomotion::NativeSample& native){
 std::lock_guard lock(combatMutex);const auto now=GetTickCount64();
 if(combatSlot==1&&locomotion::blowInteractionActive())return 0;
 if(!punchEnabled()||combatDead||(combatSlot!=1&&!itemMeleeSlot(combatSlot))||now-combatTick>100)return 0;
 if(!native.valid||now-native.tick>100||combatOwner!=native.entity||native.animated||native.localSpace||native.movementType!=0||native.explicitReason!=0||!trackedArmMovement(native.movementMode))return 0;
 if(!input.focused||now-input.tick>100||input.hands[0].upper||input.hands[0].menu||sequential::menuActive()||!sequential::projectionAllowed())return 0;
 unsigned mask=0;for(unsigned h=0;h<2;++h)if(!(h==1&&whipOutput.held)&&!input.hands[1].click&&input.hands[h].tracked&&fistReady(input.hands[h].squeeze)&&now<punchUntil[h])mask|=1u<<h;
 return mask;
}
void rememberMuzzleFlash(uint64_t owner,uint64_t weapon,Pose pose,NativeMuzzleSource source){
 if(!valid(pose))return;
 const auto now=GetTickCount64();const auto n=locomotion::latestNative();
 MuzzleFlashStamp shot{owner,weapon,now,combatGeneration.load(),pose,source};
 {std::lock_guard lock(targetMutex);const auto& a=appliedTargets;
  shot.anchored=a.stamp.matches(owner,n.gameTime,a.targets.epoch,now)&&valid(a.model);
  if(shot.anchored)shot.epoch=a.targets.epoch;}
 std::lock_guard lock(muzzleFlashMutex);
 muzzleFlashes[nextMuzzleFlash++%muzzleFlashes.size()]=shot;
 static unsigned samples{};if(samples++<32)
  circlevr::vkcore::logf("motor-muzzle-source: owner %llx weapon %llx pool %p attachment %p handle %llx anchored %u",owner,weapon,(void*)source.pool,(void*)source.attachment,source.handle,unsigned(shot.anchored));
}
bool localMuzzleFlash(uint64_t owner,Pose pose){
 const auto now=GetTickCount64();std::lock_guard lock(muzzleFlashMutex);
 for(const auto& shot:muzzleFlashes)if(shot.owner==owner&&shot.life==combatGeneration.load()&&muzzleFlashMatches(shot.pose,pose,shot.tick,now))return true;
 return false;
}
bool renderMuzzleFlash(uint64_t owner,uint64_t token,std::array<float,15>& p){
 const auto reject=[&](unsigned reason){
  static std::atomic<unsigned> counts[8]{};
  static constexpr const char* reasons[]={"effect basis","shot/render frame","current weapon","native muzzle handle","muzzle distance","shot association","particle scale","placement jump"};
  const auto count=++counts[reason];
  if(count<=8||count%1000==0)circlevr::vkcore::logf("motor-muzzle-render-rejected: owner %llx token %llu reason %s count %u",owner,token,reasons[reason],count);
  return false;
 };
 const auto now=GetTickCount64();const auto n=locomotion::latestNative();
 auto q=rotationBasis({p[3],p[4],p[5]},{p[6],p[7],p[8]},{p[9],p[10],p[11]});
 if(!q)return reject(0);
 const Pose effect{{p[0],p[1],p[2]},*q};
 MuzzleFlashStamp shot{};
 {std::lock_guard lock(muzzleFlashMutex);
  for(const auto& candidate:muzzleFlashes)
   if(candidate.owner==owner&&candidate.life==combatGeneration.load()&&candidate.anchored&&
      candidate.tick>=shot.tick&&muzzleFlashMatches(candidate.pose,effect,candidate.tick,now))shot=candidate;
 }
 RenderAnchor a;{std::lock_guard lock(targetMutex);a=renderAnchor;}
 if(!shot.anchored||!token||a.token!=token||shot.epoch!=a.stamp.epoch||
    !a.stamp.matches(owner,n.gameTime,a.stamp.epoch,now)||!latest().focused)return reject(1);
 WeaponPose weapon;{std::lock_guard lock(weaponPoseMutex);weapon=weaponPose;}
 if(!n.valid||n.entity!=owner||now<n.tick||now-n.tick>100||weapon.owner!=owner||weapon.weapon!=shot.weapon||weapon.epoch!=shot.epoch||
    weapon.slot!=weaponSlot(owner)||now<weapon.tick||now-weapon.tick>100||!equippedFirearm(owner))return reject(2);
 Pose muzzle;
 if(!readNativeMuzzle(shot.source,read,muzzle))return reject(3);
 if(length(muzzle.position-a.solvedModel.position)>2.f)return reject(4);
 // The live handle includes weapon animation and controller IK. Apply only
 // the same solved-to-render delta as the visible weapon/laser, exactly once.
 const auto renderedMuzzle=renderHandAttachment(a.solvedModel,a.model,muzzle);
 const auto placed=renderMuzzleParticle(shot.pose,renderedMuzzle,effect,shot.tick,now);
 if(!placed)return reject(5);
 // Preserve particle scale independently of its rigid muzzle frame.
 for(unsigned i=12;i<15;++i)if(!std::isfinite(p[i])||p[i]<=0||p[i]>100)return reject(6);
 const auto wanted=*placed;
 if(!valid(wanted)||length(wanted.position-effect.position)>1.f)return reject(7);
 const auto x=rotate(wanted.orientation,{1,0,0}),y=rotate(wanted.orientation,{0,1,0}),z=rotate(wanted.orientation,{0,0,1});
 const auto sx=p[12],sy=p[13],sz=p[14];
 p={wanted.position.x,wanted.position.y,wanted.position.z,x.x,x.y,x.z,y.x,y.y,y.z,z.x,z.y,z.z,sx,sy,sz};
 static std::atomic<uint64_t> last{};auto previous=last.load();
 if(now-previous>=1000&&last.compare_exchange_strong(previous,now)){
  circlevr::vkcore::logf("motor-muzzle-render: shot age %llu token %llu delta %.4f %.4f %.4f scale %.3f %.3f %.3f",
   now-shot.tick,token,wanted.position.x-effect.position.x,wanted.position.y-effect.position.y,wanted.position.z-effect.position.z,sx,sy,sz);}
 return true;
}
bool firearmShotPose(uint64_t owner,uint64_t weapon,Pose model,Pose localMuzzle,Pose& pose){
 const auto n=locomotion::latestNative();const auto input=latest();const auto now=GetTickCount64();
 if(!enabled()||!equippedFirearm(owner)||!n.valid||n.entity!=owner||now-n.tick>100||
  !sequential::projectionAllowed()||sequential::menuActive()||
  !locomotion::trackedFirearmState(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace)||
  !input.focused||now-input.tick>100||!input.hands[1].tracked||!input.hands[1].aimTracked||input.hands[0].upper||input.hands[0].menu)return false;
 WeaponPose w;{std::lock_guard lock(weaponPoseMutex);w=weaponPose;}
 Targets t;{std::lock_guard lock(targetMutex);t=targets;}
 if(w.owner!=owner||w.weapon!=weapon||w.slot!=weaponSlot(owner)||now<w.tick||now-w.tick>100||w.epoch!=t.epoch)return false;
 const auto muzzle=modelMuzzle(model,localMuzzle);
 if(!muzzle||length(muzzle->position-n.position)>3.f)return false;
 // Model placement already contains the attachment, controller IK and native
 // transform update. Applying the IK delta again double-transforms the muzzle.
 pose=*muzzle;

 static std::atomic<unsigned> count{};if(++count<=30){const auto direction=rotate(pose.orientation,{1,0,0});
  circlevr::vkcore::logf("motor-weapon-model-muzzle: owner %llx weapon %llx local %.4f %.4f %.4f world %.4f %.4f %.4f direction %.4f %.4f %.4f model-anchor distance %.4f pose age %llu",owner,weapon,localMuzzle.position.x,localMuzzle.position.y,localMuzzle.position.z,pose.position.x,pose.position.y,pose.position.z,direction.x,direction.y,direction.z,length(model.position-w.tracked.position),now-w.tick);
 }
 return true;
}
void resetInteractionTracking(){++interactionEpoch;}
void publishInteractionTracking(const experimental::EyeRig& rig){
 const auto input=atTime(rig.displayTime);const auto native=locomotion::latestNative();const auto now=GetTickCount64();
 const auto epoch=interactionEpoch.load();
 {
  const auto& q=rig.orientations[0];const auto& p=input.hands[1].grip.position;
  const Pose head{{(rig.positions[0][0]+rig.positions[1][0])*.5f,(rig.positions[0][1]+rig.positions[1][1])*.5f,(rig.positions[0][2]+rig.positions[1][2])*.5f},{q[0],q[1],q[2],q[3]}};
  const bool usable=rig.valid&&rig.tracked&&input.focused&&input.hands[1].tracked&&input.time>0&&input.tick&&now>=input.tick&&now-input.tick<100&&native.valid&&now>=native.tick&&now-native.tick<100;
  std::lock_guard lock(consumeMutex);
  mouthTracking={usable?native.entity:0,input.session,now,uint64_t(input.time),epoch,usable?mouthReach(head,{p.x,p.y,p.z}):-1.f};
 }
 std::lock_guard lock(interactionMutex);
 static std::array<interaction::ScriptedDirections,2> directions;
 static uint64_t owner{},session{},previousEpoch{};static unsigned mode=~0u;
 const bool eligible=rig.valid&&rig.tracked&&now-rig.ticks<100&&native.valid&&now-native.tick<100&&
  input.focused&&now-input.tick<100&&!sequential::menuActive()&&!input.hands[0].upper&&!input.hands[0].menu&&
  !(input.hands[0].click&&input.hands[1].click)&&
  locomotion::nativeGripInteraction(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace);
 if(!eligible||owner!=native.entity||session!=input.session||previousEpoch!=epoch||mode!=native.movementMode)directions={};
 owner=native.entity;session=input.session;previousEpoch=epoch;mode=native.movementMode;
 const Vec head{(rig.positions[0][0]+rig.positions[1][0])*.5f,(rig.positions[0][1]+rig.positions[1][1])*.5f,(rig.positions[0][2]+rig.positions[1][2])*.5f};
 const auto& q=rig.orientations[0];Vec right=rotate({q[0],q[1],q[2],q[3]},{1,0,0});right.y=0;
 const bool heading=finite(right)&&length(right)>.1f;right=unit(right);
 const uint64_t poseTick=input.time>0?uint64_t(input.time/1000000):0;
 for(unsigned h=0;h<2;++h){const auto& hand=input.hands[h];
  const Vec relative=Vec{hand.grip.position.x,hand.grip.position.y,hand.grip.position.z}-head;
  const auto axes=directions[h].update(relative,right,poseTick,eligible&&heading&&hand.tracked);
  if(axes!=scriptedAxes[h])circlevr::vkcore::logf("motor-scripted-gesture: hand %u axes %.1f %.1f owner %llx mode %u; squeeze independent",h,axes[0],axes[1],native.entity,native.movementMode);
  scriptedAxes[h]=axes;
 }
 interactionTick=now;interactionOwner=eligible?native.entity:0;
}
std::array<float,2> interactionStick(const Snapshot& input,const locomotion::NativeSample& native){
 std::lock_guard lock(interactionMutex);const auto now=GetTickCount64();
 if(!input.focused||now-input.tick>100||!native.valid||now-native.tick>100||sequential::menuActive()||input.hands[0].upper||input.hands[0].menu||
  now-interactionTick>100||native.entity!=interactionOwner||
  !locomotion::nativeGripInteraction(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace))return {};
 // Simultaneous opposing hands cancel instead of oscillating direction.
 std::array<float,2> out{};for(unsigned h=0;h<2;++h)if(input.hands[h].tracked)
  for(unsigned a=0;a<2;++a)out[a]+=scriptedAxes[h][a];
 for(auto& axis:out)axis=std::clamp(axis,-1.f,1.f);
 if(out[0])out[1]=0;return out;
}
void publishTargets(const experimental::EyeRig& rig,const std::array<float,12>& camera,const locomotion::CameraMotion& motion){
 if(enabled()&&sequential::requested())suppressNativeBob();
 Targets next;const auto native=locomotion::latestNative();
 const bool mounted=motion.mounted&&native.movementMode==27&&native.movementType==5&&weaponSlot(native.entity)==22;
 if(enabled()&&motion.referenceValid&&rig.valid&&rig.tracked&&native.valid&&!native.animated&&!native.localSpace&&native.explicitReason==0&&
    (mounted||(native.movementType==0&&trackedVisualArmMovement(native.movementMode))||((locomotion::nativeScriptedSeat(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace)||locomotion::nativeBoatDriver(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace))))){
  auto input=atTime(rig.displayTime);
  static std::array<HeadRelativeSmoothing,2> smooth;
  static const float smoothing=[]{if(circle::fg::requested())return 0.f;char value[32]{};if(!GetEnvironmentVariableA("CIRCLE_HAND_SMOOTHING_MS",value,32))return .020f;const float ms=std::strtof(value,nullptr);return std::isfinite(ms)?std::clamp(ms,0.f,60.f)*.001f:.020f;}();
  const auto& headQ=rig.orientations[0];
  const Pose trackedHead{{(rig.positions[0][0]+rig.positions[1][0])*.5f,(rig.positions[0][1]+rig.positions[1][1])*.5f,(rig.positions[0][2]+rig.positions[1][2])*.5f},{headQ[0],headQ[1],headQ[2],headQ[3]}};
  for(unsigned h=0;h<2;++h){auto& hand=input.hands[h];
   Pose grip{{hand.grip.position.x,hand.grip.position.y,hand.grip.position.z},{hand.grip.orientation.x,hand.grip.orientation.y,hand.grip.orientation.z,hand.grip.orientation.w}};
   Pose aim{{hand.aim.position.x,hand.aim.position.y,hand.aim.position.z},{hand.aim.orientation.x,hand.aim.orientation.y,hand.aim.orientation.z,hand.aim.orientation.w}};
   smooth[h].update(grip,aim,trackedHead,uint64_t(input.time),motion.epoch,input.focused&&hand.tracked&&hand.aimTracked,smoothing);
   hand.grip.position={grip.position.x,grip.position.y,grip.position.z};hand.aim.orientation={aim.orientation.x,aim.orientation.y,aim.orientation.z,aim.orientation.w};
  }
  if(input.focused){
   float yaw=std::atan2(camera[4],camera[3]);
   locomotion::GroundFrame basis{{std::cos(yaw),std::sin(yaw),0},{std::sin(yaw),-std::cos(yaw),0},{0,0,1},1,true};
   if(mounted)basis={{camera[3],camera[4],camera[5]},{-camera[6],-camera[7],-camera[8]},{camera[9],camera[10],camera[11]},1,true};
   auto rotation=rotationBasis(basis.trackingToWorld({1,0,0},motion.referenceYaw),basis.up,basis.trackingToWorld({0,0,1},motion.referenceYaw));
   Vec head{(rig.positions[0][0]+rig.positions[1][0])*.5f,(rig.positions[0][1]+rig.positions[1][1])*.5f,(rig.positions[0][2]+rig.positions[1][2])*.5f};
   HandFrame frame{{camera[0],camera[1],camera[2]},head,basis,motion.referenceYaw};
   if(rotation)next.headOrientation=normalized(multiply(*rotation,trackedHead.orientation));
   if(rotation)for(unsigned h=0;h<2;++h){const auto& hand=input.hands[h];if(!hand.tracked||!hand.aimTracked)continue;const auto& p=hand.grip.position;const auto& q=hand.aim.orientation;
    auto pos=frame.toWorld({p.x,p.y,p.z});if(!pos)continue;next.world[h]={*pos,normalized(multiply(*rotation,{q.x,q.y,q.z,q.w}))};next.tracked[h]=valid(next.world[h]);}
   next.owner=native.entity;next.tick=input.tick;next.camera=frame.nativeCamera;next.physics=motion.physics;next.epoch=motion.epoch;next.eyeRig=rig;next.motion=motion;
  }
 }
 {
  const bool ordinaryProjection=sequential::projectionAllowed()&&!sequential::menuActive();
  const bool blowInteraction=locomotion::blowInteractionActive();
  std::lock_guard lock(combatMutex);
  if(combatSlot==1&&blowInteraction){punchUntil={};punchCreditUntil={};punchChargeMs={};punchChargeStages={};}
  static std::array<VelocityPunchDetector,2> detectors;static std::array<PunchCharge,2> charges;
  static Vec previousHead{};static uint64_t previousTick{},previousEpoch{},previousOwner{},lastDiagnostic{};
  auto input=atTime(rig.displayTime);const auto now=GetTickCount64();
  const uint64_t poseTick=input.time>0?uint64_t(input.time/1000000):0;
  Vec head{(rig.positions[0][0]+rig.positions[1][0])*.5f,(rig.positions[0][1]+rig.positions[1][1])*.5f,(rig.positions[0][2]+rig.positions[1][2])*.5f};
  const auto& orientation=rig.orientations[0];
  bool duplicate=poseTick&&poseTick==previousTick;
  bool stable=previousTick&&poseTick>previousTick&&poseTick-previousTick<=100;
  const auto& wh=input.hands[1];
  const Vec strikeForward=rotate({orientation[0],orientation[1],orientation[2],orientation[3]},{0,0,-1});
  Vec facing=strikeForward;
  facing.y=0;const bool headingValid=length(facing)>.1f;facing=unit(facing,{0,0,-1});
  const Vec shoulderRight{-facing.z,0,facing.x};
  const Vec rel=Vec{wh.grip.position.x,wh.grip.position.y,wh.grip.position.z}-head;
  Vec speed{wh.velocity.x,wh.velocity.y,wh.velocity.z};
  const bool whipEligible=whipInstalled&&enabled()&&ordinaryProjection&&!blowInteraction&&rig.valid&&rig.tracked&&headingValid&&
   native.valid&&now-native.tick<100&&now-input.tick<100&&combatOwner==native.entity&&!combatDead&&now-combatTick<100&&
   !native.animated&&!native.localSpace&&!native.movementType&&!native.explicitReason&&trackedArmMovement(native.movementMode)&&
   input.focused&&!input.hands[0].upper&&!input.hands[0].menu&&!(input.hands[0].click&&wh.click);
  const auto beforeWhip=whipOutput;
  whipOutput=shoulderWhip.update({native.entity,motion.epoch,poseTick,combatSlot,whipEligible,wh.tracked,
   wh.velocityValid&&stable,wh.squeeze,{dot(rel,shoulderRight),rel.y,dot(rel,facing)},{dot(speed,shoulderRight),speed.y,dot(speed,facing)},
   {dot(strikeForward,shoulderRight),strikeForward.y,dot(strikeForward,facing)}});
  whipOwner=native.entity;whipTick=now;
  if(whipOutput.actions!=beforeWhip.actions||whipOutput.held!=beforeWhip.held||whipOutput.equipped!=beforeWhip.equipped)
   circlevr::vkcore::logf("motor-shoulder-whip: held %d equipped %d actions %llx slot %u grip %.2f relative %.3f %.3f %.3f owner %llx",whipOutput.held,whipOutput.equipped,whipOutput.actions,combatSlot,wh.squeeze,dot(rel,shoulderRight),rel.y,dot(rel,facing),native.entity);
  bool eligible=punchEnabled()&&!(combatSlot==1&&blowInteraction)&&trackedArmMovement(native.movementMode)&&ordinaryProjection&&!input.hands[0].upper&&!input.hands[0].menu&&next.owner&&combatOwner==next.owner&&(combatSlot==1||itemMeleeSlot(combatSlot))&&!combatDead&&now-combatTick<100&&now-input.tick<100&&input.focused&&stable&&previousEpoch==motion.epoch&&previousOwner==next.owner;
  for(unsigned h=0;h<2;++h){if(duplicate)break;const auto& hand=input.hands[h];const auto& p=hand.grip.position;const auto& q=hand.aim.orientation;
   const auto& headQ=rig.orientations[0];Vec headForward=rotate({headQ[0],headQ[1],headQ[2],headQ[3]},{0,0,-1});
   Vec aimForward=rotate({q.x,q.y,q.z,q.w},{0,0,-1});
   const bool tracked=eligible&&next.tracked[h]&&hand.velocityValid&&
    (combatSlot==1||itemStrikeEligible(combatFirearm?4:combatSlot,h,hand.squeeze,hand.trigger,aimForward,now<firearmMeleeUntil.load()));
   Vec measured{hand.velocity.x,hand.velocity.y,hand.velocity.z};
   // Use OpenXR linear velocity directly, like ARGENT's swing detector.
   // Artificial locomotion is absent from tracking-space velocity.
   const bool holstering=input.hands[1].click&&!input.hands[0].upper;
   bool fired=detectors[h].update(measured,headForward,poseTick,tracked&&fistReady(hand.squeeze)&&!holstering&&!(h==1&&whipOutput.held));
   const bool chargeMode=!combatFirearm||h==0||(now<firearmMeleeUntil.load());
   // Outside firearm melee mode, keep a neutral local charge state. Entering
   // with both triggers together must not require releasing the charge trigger.
   const auto charge=charges[h].update(native.entity,motion.epoch^(uint64_t(combatSlot)<<32),poseTick,
    chargeInstalled&&meleeChargeSlot(combatSlot)&&eligible&&next.tracked[h]&&!holstering&&!(h==1&&whipOutput.held),hand.squeeze,chargeMode?hand.trigger:0.f,fired);
   if(diagnostics::enabled()&&punchEnabled()&&now-lastDiagnostic>2000)circlevr::vkcore::logf("motor-punch-diagnostic: hand %u eligible %d stable %d tracked %d dt %llu context age %llu aim %.3f forward %.3f trigger %.2f grip %.2f speed %.3f armed %d",h,eligible,stable,tracked,poseTick-previousTick,now-combatTick,dot(aimForward,headForward),dot(Vec{p.x,p.y,p.z}-head,headForward),hand.trigger,hand.squeeze,length(detectors[h].velocity),detectors[h].armed);
   if(!tracked||!fistReady(hand.squeeze)||holstering||(h==1&&whipOutput.held)){punchUntil[h]=0;punchChargeMs[h]=0;}
   if(fired){
    const bool feedback=requestHaptic(h,.45f,45);
    circlevr::vkcore::logf("motor-action-haptic: physical swing hand %u queued %d; action feedback, not hit confirmation",h,feedback);
    punchUntil[h]=now+physicalStrikePulseMs(combatSlot);punchCreditUntil[h]=now+500;punchChargeMs[h]=charge;punchChargeStages[h]=0;
    circlevr::vkcore::logf("motor-physical-punch: hand %u native action %s speed %.3f owner %llx charge %u ms mode %d triggers %.3f/%.3f grip %.3f; controller-owned arms, native hit path",h,h==0?"_attack2":"_attack1",length(detectors[h].velocity),next.owner,charge,chargeMode,input.hands[0].trigger,input.hands[1].trigger,hand.squeeze);}
  }
  if(now-lastDiagnostic>2000)lastDiagnostic=now;
  if(!duplicate){previousHead=head;previousTick=poseTick;previousEpoch=motion.epoch;previousOwner=next.owner;}
 }
 std::lock_guard lock(targetMutex);targets=next;
}
bool appliedViewFrame(const experimental::EyeRig& rig,const locomotion::CameraMotion& motion){
 if(!enabled()||!motion.referenceValid||!latest().focused)return false;
 const auto native=locomotion::latestNative();const auto now=GetTickCount64();
 std::lock_guard lock(targetMutex);
 const auto& saved=appliedTargets;
 const bool use=native.valid&&now-native.tick<100&&saved.stamp.matches(native.entity,native.gameTime,motion.epoch,now)&&
  saved.targets.eyeRig.valid&&saved.targets.eyeRig.tracked&&now>=saved.targets.tick&&now-saved.targets.tick<100;
 static uint64_t logTick{};if(now-logTick>2000){logTick=now;
  circlevr::vkcore::logf("motor-hand-view-sync: matched %d simulation %llu/%llu XR %lld/%lld sample age %llu yaw %.3f/%.3f",use,saved.stamp.simulation,native.gameTime,saved.targets.eyeRig.displayTime,rig.displayTime,now-saved.targets.tick,saved.targets.motion.referenceYaw,motion.referenceYaw);
 }
 if(!use)return false;
 // The arms can use their matched simulation pose; the view must retain its
 // current HMD sample. publishRenderAnchor rebases their native body placement.
 return true;
}
void publishRenderAnchor(uint64_t token,const std::array<float,12>& camera,bool matched,const locomotion::CameraMotion& motion){
 const auto native=locomotion::latestNative();const auto now=GetTickCount64();
 std::lock_guard lock(targetMutex);renderAnchor={};
 const auto& a=appliedTargets;
 if(!matched||!token||!a.stamp.matches(native.entity,native.gameTime,a.targets.epoch,now)||
    !valid(a.model)||!finite(a.solvedCamera))return;
 const Vec renderCamera{camera[0],camera[1],camera[2]};
 if(!finite(renderCamera)||length(renderCamera-a.solvedCamera)>(a.nativeBodyFrame?.75f:.35f))return;
 const auto& solved=a.targets.motion;
 if(motion.mounted&&solved.mounted){
  const auto currentMount=turret::pose(motion.mountedRoot),solvedMount=turret::pose(solved.mountedRoot);
  if(!currentMount||!solvedMount||!valid(a.nativeModel))return;
  const auto rendered=compose(compose(*currentMount,inverse(*solvedMount)),a.model);
  renderAnchor={a.stamp,token,rendered,a.nativeModel,{},a.solvedCamera,renderCamera,true};
  return;
 }
 const float yawDelta=locomotion::wrap((motion.nativeYaw-motion.referenceYaw)-(solved.nativeYaw-solved.referenceYaw));
 if(!std::isfinite(yawDelta)||std::abs(yawDelta)>30)return;
 renderAnchor={a.stamp,token,renderHandModel(a.model,a.solvedCamera,renderCamera,yawDelta),a.model,a.attachment,a.solvedCamera,renderCamera};
 static uint64_t stairLog{};
 if(std::abs(motion.cameraAnchor.stairHeightOffset)>.005f&&now-stairLog>=1000){stairLog=now;
  circlevr::vkcore::logf("motor-hand-stairs: token %llu residual %.5f solvedCameraZ %.5f renderCameraZ %.5f modelDeltaZ %.5f; shared post-IK skeleton/equipment translation",token,motion.cameraAnchor.stairHeightOffset,a.solvedCamera.z,renderCamera.z,renderAnchor.model.position.z-a.model.position.z);
 }
}
bool renderModelPlacement(uint64_t owner,uint64_t token,std::array<float,15>& p,bool carried){
 const auto native=locomotion::latestNative();const auto now=GetTickCount64();
 RenderAnchor a;{std::lock_guard lock(targetMutex);a=renderAnchor;}
 if(carried&&a.mounted)return false; // The fixed gun is not a carried hand attachment.
 if(!token||a.token!=token||!a.stamp.matches(owner,native.gameTime,a.stamp.epoch,now)||!latest().focused)return false;
 // Registered player skeleton or validated first-person carried object.
 // Attachments receive the same rigid delta, retaining their local grip pose.
 // Reject unsupported scale and large discontinuities.
 const Vec old{p[0],p[1],p[2]};
 const Vec rootDelta=old-a.solvedModel.position;
 if(!finite(old)||(carried?!carriedModelOriginInRange(rootDelta):length(rootDelta)>.35f)||
    !handPlacementScale({p[12],p[13],p[14]},carried))return false;
 auto q=rotationBasis({p[3],p[4],p[5]},{p[6],p[7],p[8]},{p[9],p[10],p[11]});
 if(!q)return false;
 auto wanted=carried?renderHandAttachment(a.solvedModel,a.model,Pose{old,*q}):a.model;

 if(carried&&a.attachment.valid){
  // Compare poses from the same successful IK evaluation and stereo pair.
  // Never inspect a stale native animation-context pointer at render time.
  static uint64_t auditTick{};
  if(now-auditTick>=200){auditTick=now;
   const auto local=compose(inverse(a.solvedModel),Pose{old,*q});
   const auto offset=compose(inverse(a.attachment.anchor),local);
   const auto currentRelative=rotate(inverse(a.solvedModel).orientation,old-native.position);
   circlevr::vkcore::logf("motor-attachment-origin: token %llu simulation %llu old %.6f %.6f %.6f solved %.6f %.6f %.6f current %.6f %.6f %.6f currentLocal %.6f %.6f %.6f corrected %.6f %.6f %.6f",token,native.gameTime,old.x,old.y,old.z,a.solvedModel.position.x,a.solvedModel.position.y,a.solvedModel.position.z,native.position.x,native.position.y,native.position.z,currentRelative.x,currentRelative.y,currentRelative.z,wanted.position.x,wanted.position.y,wanted.position.z);

   circlevr::vkcore::logf("motor-attachment-frame: slot %u bound %d token %llu simulation %llu model %.6f %.6f %.6f anchor %.6f %.6f %.6f wrist %.6f %.6f %.6f reference %.6f %.6f %.6f anchorLocal %.6f %.6f %.6f q %.6f %.6f %.6f %.6f",a.attachment.slot,a.attachment.bound,token,native.gameTime,local.position.x,local.position.y,local.position.z,a.attachment.anchor.position.x,a.attachment.anchor.position.y,a.attachment.anchor.position.z,a.attachment.wrist.position.x,a.attachment.wrist.position.y,a.attachment.wrist.position.z,a.attachment.reference.position.x,a.attachment.reference.position.y,a.attachment.reference.position.z,offset.position.x,offset.position.y,offset.position.z,offset.orientation.x,offset.orientation.y,offset.orientation.z,offset.orientation.w);
  }
 }
 const float cosine=std::abs(q->x*wanted.orientation.x+q->y*wanted.orientation.y+q->z*wanted.orientation.z+q->w*wanted.orientation.w);
 if(cosine<.9659f&&!a.mounted)return false;
 const auto x=rotate(wanted.orientation,{1,0,0}),y=rotate(wanted.orientation,{0,1,0}),z=rotate(wanted.orientation,{0,0,1});
 const auto delta=wanted.position-old;
 p={wanted.position.x,wanted.position.y,wanted.position.z,x.x,x.y,x.z,y.x,y.y,y.z,z.x,z.y,z.z,p[12],p[13],p[14]};
 if(carried&&dot(rootDelta,rootDelta)>4.f&&diagnostics::enabled()){
  static uint64_t longPropTick{};
  if(now-longPropTick>=1000){longPropTick=now;
   circlevr::vkcore::logf("motor-carried-origin: corrected owner %llx token %llu rootDistance %.3f m delta %.5f %.5f %.5f; long held model origin",owner,token,length(rootDelta),delta.x,delta.y,delta.z);
  }
 }
 static uint64_t last{};if(now-last>1000){last=now;circlevr::vkcore::logf("motor-hand-render-anchor: token %llu simulation %llu delta %.5f %.5f %.5f rotation %.3f age %llu",token,native.gameTime,delta.x,delta.y,delta.z,2*std::acos(std::clamp(cosine,0.f,1.f))*57.2957795f,now-a.stamp.tick);}
 return true;
}
void auditUploadedModel(uint64_t owner,uint64_t token,const std::array<float,16>& m,unsigned eye){
 if(eye>1)return;
 RenderAnchor a;{std::lock_guard lock(targetMutex);a=renderAnchor;}
 const auto now=GetTickCount64();
 if(a.token!=token||a.stamp.owner!=owner)return;
 static std::mutex mutex;std::lock_guard lock(mutex);
 static uint64_t ticks[2]{};if(now-ticks[eye]<500)return;ticks[eye]=now;
 const auto& p=a.model.position;const auto& q=a.model.orientation;
 circlevr::vkcore::logf("motor-hand-upload-frame: eye %u token %llu wanted %.6f %.6f %.6f q %.6f %.6f %.6f %.6f actual %.6f %.6f %.6f %.6f / %.6f %.6f %.6f %.6f / %.6f %.6f %.6f %.6f / %.6f %.6f %.6f %.6f",eye,token,p.x,p.y,p.z,q.x,q.y,q.z,q.w,m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11],m[12],m[13],m[14],m[15]);
}
void applyTrackedArms(uintptr_t rig,uintptr_t context){
 if(!enabled())return;
 std::lock_guard serial(applyMutex);
 static std::array<HandBlend,2> blends;static std::array<ArmAxisBinding,2> armAxes;static std::array<AnchoredArm,2> anchoredArms;static AnchoredTorso torso;static uintptr_t calibratedRig{};static uint64_t calibratedOwner{},axisEpoch{},lastLog{},lastBlocked{};
 struct WeaponBind {uint64_t owner{},entity{};uintptr_t rig{};unsigned slot{};bool melee{};short reference{-1};Pose local{},referenceLocal{};StableWeaponBinding capture{};NativeSupportPose support{};std::array<Pose,20> supportFingers{};std::array<Pose,20> fingers{};};
 static std::array<WeaponBind,8> weaponBindings;static unsigned nextBinding{};
 static bool nativeBodyCalibration{};
 static uint64_t bindingGeneration{};const auto generation=combatGeneration.load();
 if(bindingGeneration!=generation){weaponBindings={};nextBinding=0;bindingGeneration=generation;anchoredArms={};armAxes={};torso.reset();}
 const auto native=locomotion::latestNative();const auto now=GetTickCount64();
 const unsigned renderEye=circlevr::vkcore::seqChainEye()==circlevr::stereo_seq::Eye::Right?1:0;
 Targets t;{std::lock_guard lock(targetMutex);t=targets;
  if(renderEye==1&&appliedTargets.rig==rig&&appliedTargets.stamp.matches(native.entity,native.gameTime,t.epoch,now))t=appliedTargets.targets;
  if(renderEye==0)appliedTargets.stamp={};
 }
 const auto currentInput=latest();for(unsigned h=0;h<2;++h)t.tracked[h]=t.tracked[h]&&currentInput.focused&&currentInput.hands[h].tracked;
 const bool combatView=locomotion::combatCameraActive(native.entity,now)&&
  locomotion::nativeCombatCamera(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace);
 const bool mounted=t.motion.mounted&&native.movementMode==27&&native.movementType==5&&weaponSlot(native.entity)==22;
 if(!sequential::projectionAllowed()||!native.valid||now-native.tick>100||now-t.tick>100||t.owner!=native.entity||native.animated||native.localSpace||native.explicitReason!=0||
    (!mounted&&!combatView&&!((locomotion::nativeScriptedSeat(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace)||locomotion::nativeBoatDriver(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace)))&&(native.movementType!=0||!trackedVisualArmMovement(native.movementMode)))){
  if(diagnostics::enabled()&&now-lastBlocked>1000){lastBlocked=now;circlevr::vkcore::logf("motor-hand-ik-blocked: projection %d native %d ages %llu/%llu owners %llx/%llx animated %d local %d movement %d reason %d mode %u",sequential::projectionAllowed(),native.valid,now-native.tick,now-t.tick,t.owner,native.entity,native.animated,native.localSpace,native.movementType,native.explicitReason,native.movementMode);}
  for(auto& b:blends)b.reset();
  // Preserve only the validated identity across a short native action. Ordinary
  // gameplay no longer retains torso/shoulder poses from that identity.
  if(!nativeBodyCalibration||!native.valid||native.entity!=calibratedOwner||t.epoch!=axisEpoch)calibratedRig=0;
  return;}
 const auto base=(uintptr_t)GetModuleHandleW(nullptr);
 if(at<uintptr_t>(rig)!=base+build::rva(0x3653ca8)||!namedArms(rig))return;
 // Cockpit now renders only the mounted gun. Keep native simulation/input but
 // stop writing tracked arm poses or publishing a body-placement correction.
 if(mounted){
  {std::lock_guard lock(targetMutex);appliedTargets={};renderAnchor={};}
  for(auto& b:blends)b.reset();calibratedRig=0;return;
 }
 auto reject=[&](const char* reason,float delta){if(now-lastBlocked>1000){lastBlocked=now;circlevr::vkcore::logf("motor-hand-rig-rejected: %s delta %.4f targetAge %llu",reason,delta,now-t.tick);}};
 auto origin=at<Vec>(rig+0xc20);
 std::array<float,3> renderedBody{};
 const bool visualOwner=sequential::playerCameraOrigin(native.entity,{t.camera.x,t.camera.y,t.camera.z},renderedBody)&&
  length(origin-Vec{renderedBody[0],renderedBody[1],renderedBody[2]})<.1f;
 if(!finite(origin)||(!visualOwner&&length(origin-native.position)>.75f)){reject("physics/render ownership distance",length(origin-native.position));return;}
 std::array<Vec,3> axes{};if(!read(rig+0xc2c,axes.data(),36))return;
 auto modelRotation=rotationBasis(axes[0],axes[1],axes[2]);if(!modelRotation)return;
 const Pose nativeModel{origin,*modelRotation};Pose modelWorld=nativeModel;
 static Pose seatModelLocal;static uint64_t seatModelEpoch{},seatModelOwner{};static uintptr_t seatModelRig{};
 if(mounted){
  const auto mount=turret::pose(t.motion.mountedRoot);if(!mount)return;
  if(seatModelEpoch!=t.epoch||seatModelOwner!=native.entity||seatModelRig!=rig){
   seatModelLocal=compose(inverse(*mount),nativeModel);
   seatModelEpoch=t.epoch;seatModelOwner=native.entity;seatModelRig=rig;
  }
  modelWorld=compose(*mount,seatModelLocal);
 }
 const Pose worldModel=inverse(modelWorld);
 const auto count=at<uint64_t>(context+8),bits=at<uintptr_t>(context),locals=at<uintptr_t>(context+0x30),globals=at<uintptr_t>(context+0x38);
 if(!count||count>4096||!bits||!locals||!globals)return;
 std::vector<std::array<float,12>> localRaw(size_t(count),std::array<float,12>{}),globalRaw=localRaw;
 std::vector<short> parents(size_t(count),-1);std::vector<uint64_t> masks(size_t((count+63)/64),0);
 if(!read(locals,localRaw.data(),count*48)||!read(globals,globalRaw.data(),count*48)||!read(at<uintptr_t>(context+0x18),parents.data(),count*2)||!read(bits,masks.data(),masks.size()*8))return;
 std::vector<Pose> lp,gp;std::vector<unsigned char> flags(size_t(count),0);lp.reserve(size_t(count));gp.reserve(size_t(count));
 for(size_t i=0;i<count;++i){lp.push_back(unpack(localRaw[i]));gp.push_back(unpack(globalRaw[i]));flags[i]=(masks[i/64]>>(i%64))&1;}
 auto get=[&](short id){return id>=0?modelPose(lp,parents,unsigned(id),gp,flags):std::optional<Pose>{};};
 // Camera-joint world position is an independent match to the active FPS
 // camera, in addition to physics origin and the player-arm declaration.
 auto camera=get(at<short>(rig+0x720+0x72));
 Vec advancedCamera=mounted?t.camera:advanceTarget(t.camera,t.physics,origin);
 if(t.motion.cameraAnchor.active)advancedCamera.z-=t.motion.cameraAnchor.stairHeightOffset;
 if(!camera)return;
 const Vec rawCamera=compose(modelWorld,*camera).position;
 // Solve in the body's collision-height frame. The stair residual is a shared
 // visual translation applied by publishRenderAnchor, after IK. Feeding it into
 // the wrist target while shoulders remain at raw physics height makes reach
 // clamping oscillate at every step. Keep the constant camera lift here.
 const auto stable=t.motion.cameraAnchor.placeForSkeleton({rawCamera.x,rawCamera.y,rawCamera.z},{origin.x,origin.y,origin.z},native.angles.y);
 // Ordinary gameplay solves against this evaluation's actual camera joint.
 // The complete model is moved rigidly to the stabilized view at publication;
 // using the stabilized point here falsely reports that the native body has
 // already been corrected, leaving its neck behind after an animation.
 const bool nativeBodyFrame=!mounted&&t.motion.cameraAnchor.active&&
  (combatView||locomotion::trackedGameplayState(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace));
 const Vec currentCamera=mounted?t.camera:nativeBodyFrame?rawCamera:Vec{stable.x,stable.y,stable.z};
 const float rawDistance=length(rawCamera-advancedCamera),stableDistance=length(Vec{stable.x,stable.y,stable.z}-advancedCamera);
 const bool established=calibratedRig==rig&&calibratedOwner==native.entity&&axisEpoch==t.epoch&&(nativeBodyFrame||!torso.joints.empty());
 if((mounted&&rawDistance>3.f)||(!mounted&&!correlatedTrackedRig(rawDistance,stableDistance,established,t.motion.cameraAnchor.active))){
  reject("camera correlation",rawDistance);return;
 }
 if(rawDistance>.35f){static uint64_t continuityLog{};if(now-continuityLog>1000){continuityLog=now;
  circlevr::vkcore::logf("motor-hand-rig-continuity: animated camera excursion %.4f stable distance %.4f; confirmed owner/rig/epoch retained",rawDistance,stableDistance);
 }}
 const Vec cameraCorrection=currentCamera-advancedCamera;
 if(calibratedRig!=rig||calibratedOwner!=native.entity||axisEpoch!=t.epoch){calibratedRig=rig;calibratedOwner=native.entity;axisEpoch=t.epoch;armAxes={};anchoredArms={};torso.reset();for(auto& b:blends)b.reset();}
 nativeBodyCalibration=nativeBodyFrame;
 unsigned equippedSlot{};uint64_t equippedEntity{};bool isFirearm{};int nativeWeaponPose=-1;
 {std::lock_guard lock(combatMutex);if(combatOwner==t.owner&&!combatDead&&now-combatTick<100){equippedSlot=combatSlot;isFirearm=combatFirearm;nativeWeaponPose=equippedPose;if(equippedSlot<28)equippedEntity=at<uint64_t>(combatLoadout+0x28+equippedSlot*8);}}
 const bool unarmed=equippedSlot==1;
 const bool armedPassenger=t.motion.armedPassenger&&
  locomotion::nativeScriptedSeat(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace);
 static bool previousPassenger{};
 if(previousPassenger!=armedPassenger){previousPassenger=armedPassenger;anchoredArms={};armAxes={};torso.reset();}
 Quat passengerTurn{};
 if(armedPassenger){
  const float half=t.motion.passengerBodyYaw*.00872664626f;
  const Vec up=rotate(worldModel.orientation,{0,0,1});
  passengerTurn={up.x*std::sin(half),up.y*std::sin(half),up.z*std::sin(half),std::cos(half)};
 }

 const auto fingerGesture=locomotion::fingerGestures();
 struct Change {short id;Pose local,global;};std::vector<Change> changes;
 // Constant upper-body pose in the same stabilized reference as the arms.
 // No attack-start/attack-end pose swap; lower-body ancestors remain native.
 const Vec torsoView=compose(worldModel,Pose{currentCamera,{}}).position;
 bool torsoChanged=false;
 const short cameraId=at<short>(rig+0x720+0x72);
 if(!nativeBodyFrame&&t.tracked[0]&&t.tracked[1]&&cameraId>=0&&size_t(cameraId)<count&&parents[cameraId]>=0){
  auto ids=upperBodyAncestors(parents,at<short>(rig+0x720+0x70),at<short>(rig+0x720+0xa4+0x70));
  if(std::find(ids.begin(),ids.end(),cameraId)!=ids.end())ids.clear();
  if(ids!=torso.joints)torso.reset();
  if(torso.joints.empty()&&!ids.empty()){
   auto resolved=gp;bool complete=true;
   for(auto id:ids){auto pose=get(id);if(pose)resolved[id]=*pose;else complete=false;}
   if(complete&&torso.capture(ids,resolved,torsoView))circlevr::vkcore::logf("motor-torso-anchor: captured %zu upper-body ancestors owner %llx rig %p; root/lower base native, camera preserved",ids.size(),native.entity,(void*)rig);
  }
  for(size_t i=0;i<torso.joints.size();++i){
   const auto id=torso.joints[i];auto parent=get(parents[id]);if(!parent)return;
   for(const auto& changed:changes)if(changed.id==parents[id])parent=changed.global;
   torsoChanged=true;
   auto held=torso.at(i,torsoView);
   if(armedPassenger)held=turnAroundView(held,torsoView,passengerTurn);
   changes.push_back({id,compose(inverse(*parent),held),held});
  }
 }
 WeaponPose nextWeapon{};
 AttachmentAudit attachment{};
 std::optional<Pose> fixedSupport;const std::array<Pose,20>* fixedFingers=nullptr;
 for(unsigned slot=0;slot<2;++slot){const unsigned h=1-slot;const auto p=rig+0x720+slot*0xa4;
  if(at<unsigned char>(p+0x5d)!=1){reject("arm not initialized",float(slot));continue;}
  short shoulder=at<short>(p+0x70),elbow=at<short>(p+0x6e),wrist=at<short>(p+0x6a);
  if(shoulder<0||elbow<0||wrist<0||uint64_t(shoulder)>=count||uint64_t(elbow)>=count||uint64_t(wrist)>=count||parents[elbow]!=shoulder||parents[wrist]!=elbow)continue;
  auto a=get(shoulder),b=get(elbow),c=get(wrist),parent=get(parents[shoulder]);if(!a||!b||!c||!parent)continue;
  const float upper=at<float>(p+0x7c),lower=at<float>(p+0x78);
  if(upper<.1f||upper>.6f||lower<.1f||lower>.6f||std::abs(length(b->position-a->position)-upper)>.01f||std::abs(length(c->position-b->position)-lower)>.01f){reject("arm segment mismatch",std::max(std::abs(length(b->position-a->position)-upper),std::abs(length(c->position-b->position)-lower)));continue;}
  if(!t.tracked[h]){blends[h].reset();continue;}
  Pose worldTarget=t.world[h];worldTarget.position=rebaseHandTarget(worldTarget.position,t.camera,currentCamera);
  Pose target=compose(worldModel,worldTarget);
  target.orientation=normalized(multiply(target.orientation,motorWristFromAim(h)));
  // Slot 0 is the independent weapon anchor (camera child); slot 2 is its
  // reference under the wrist. Native animation drives both before our IK.
  // Preserve native wrist-to-anchor and wrist-to-reference separately.
  // Melee and shooting must not share the first captured grip/finger pose.
  const short weapon=at<short>(p+0x64),reference=at<short>(p+0x68);
  std::optional<Pose> nativeWeapon,weaponParent;
  const bool meleeGrip=now<firearmMeleeUntil.load();
  bool bindWeapon=false;WeaponBind* chosenBinding=nullptr;
  if(!mounted&&h==1&&isFirearm&&equippedEntity&&equippedEntity!=UINT64_MAX&&
     weapon>=0&&reference>=0&&uint64_t(weapon)<count&&uint64_t(reference)<count&&
     parents[reference]==wrist&&parents[weapon]>=0&&parents[weapon]!=wrist){
   nativeWeapon=get(weapon);weaponParent=get(parents[weapon]);auto nativeReference=get(reference);
   if(nativeWeapon&&weaponParent&&nativeReference&&valid(lp[reference])&&length(lp[reference].position)<.4f){
    for(auto& entry:weaponBindings)if(entry.owner==native.entity&&entry.entity==equippedEntity&&entry.rig==rig&&entry.slot==equippedSlot&&entry.reference==reference&&entry.melee==meleeGrip){chosenBinding=&entry;break;}
    if(!chosenBinding){chosenBinding=&weaponBindings[nextBinding++%weaponBindings.size()];*chosenBinding={};chosenBinding->owner=native.entity;chosenBinding->entity=equippedEntity;chosenBinding->rig=rig;chosenBinding->slot=equippedSlot;chosenBinding->reference=reference;chosenBinding->melee=meleeGrip;}
    auto& weaponBind=*chosenBinding;
    bool fingersValid=uint64_t(wrist)+20<count;
    for(unsigned finger=0;finger<5&&fingersValid;++finger)for(unsigned joint=0;joint<4;++joint){const unsigned id=unsigned(wrist)+1+finger*4+joint;fingersValid&=parents[id]==int(joint?id-1:unsigned(wrist))&&valid(lp[id]);}
    const auto nativeGrip=compose(inverse(*c),*nativeWeapon);
    bool supportLocked=false;{std::lock_guard lock(supportMutex);supportLocked=supportState.owner==native.entity&&supportState.weapon==equippedEntity&&supportState.grab.latched;}
    if(!supportLocked&&weaponBind.capture.observe(nativeGrip,lp[reference],now,
       fingersValid&&length(nativeWeapon->position-nativeReference->position)<.08f)){
     weaponBind.local=weaponBind.capture.value;
     weaponBind.referenceLocal=weaponBind.capture.referenceValue;
     for(unsigned i=0;i<20;++i)weaponBind.fingers[i]=lp[unsigned(wrist)+1+i];
     circlevr::vkcore::logf("motor-weapon-binding: SETTLED pair owner %llx weapon %llx slot %u melee %d anchor %d reference %d wrist %d grip %.4f %.4f %.4f reference %.4f %.4f %.4f",native.entity,equippedEntity,equippedSlot,meleeGrip,weapon,reference,wrist,weaponBind.local.position.x,weaponBind.local.position.y,weaponBind.local.position.z,weaponBind.referenceLocal.position.x,weaponBind.referenceLocal.position.y,weaponBind.referenceLocal.position.z);
    }
    // Retain an already solved grip during the native mode transition; replace
    // it only once the new mode has a settled native binding.
    if(!weaponBind.capture.ready)for(auto& entry:weaponBindings)
     if(entry.owner==native.entity&&entry.entity==equippedEntity&&entry.rig==rig&&entry.slot==equippedSlot&&entry.reference==reference&&entry.capture.ready){chosenBinding=&entry;break;}
    if(chosenBinding->capture.ready&&fingersValid){
     auto wristTarget=firearmWristTarget(worldTarget,chosenBinding->local);
     if(wristTarget){target=compose(worldModel,*wristTarget);bindWeapon=true;}
    }
   }
  }
  bool supportHeld=false;
  if(h==1&&bindWeapon&&chosenBinding&&twoHandFirearmPose(nativeWeaponPose)&&t.tracked[0]){
   auto& binding=*chosenBinding;
   // A weapon-local contact remains usable when switching shooting/melee grips.
   for(const auto& other:weaponBindings)if(!binding.support.ready&&other.owner==binding.owner&&other.entity==binding.entity&&other.rig==binding.rig&&other.support.ready){binding.support=other.support;binding.supportFingers=other.supportFingers;break;}
   const short leftWrist=at<short>(rig+0x720+0xa4+0x6a);auto nativeLeft=get(leftWrist);
   bool fingersValid=leftWrist>=0&&size_t(leftWrist)+20<count;
   for(unsigned i=0;i<20&&fingersValid;++i){const auto id=unsigned(leftWrist)+1+i;fingersValid=parents[id]==int(i%4?id-1:unsigned(leftWrist))&&valid(lp[id]);}
   if(nativeLeft&&nativeWeapon&&fingersValid){
    bool held=false;{std::lock_guard lock(supportMutex);held=supportState.grab.latched;}
    // Refresh settled native bindings after initial load, never during a grab.
    if(!held&&binding.support.observe(compose(inverse(*nativeWeapon),*nativeLeft),now)){
     for(unsigned i=0;i<20;++i)binding.supportFingers[i]=lp[unsigned(leftWrist)+1+i];
     circlevr::vkcore::logf("motor-two-hand: calibrated weapon %llx pose %d native support %.4f %.4f %.4f",equippedEntity,nativeWeaponPose,binding.support.value.position.x,binding.support.value.position.y,binding.support.value.position.z);
    }
   }
   if(binding.support.ready){
    const Pose wristSupport=compose(binding.local,binding.support.value);
    const auto canonical=firearmWristTarget(Pose{},binding.local);
    {std::lock_guard lock(supportMutex);auto& state=supportState;
     if(state.owner!=native.entity||state.weapon!=equippedEntity||state.epoch!=t.epoch)state={};
     state.owner=native.entity;state.weapon=equippedEntity;state.epoch=t.epoch;state.tick=now;
     state.fromAim=compose(*canonical,wristSupport);
     supportHeld=state.grab.latched&&now-state.inputTick<100&&currentInput.hands[0].squeeze>.65f;
    }
    if(supportHeld){
     Pose left=t.world[0];left.position=rebaseHandTarget(left.position,t.camera,currentCamera);
     const Pose primaryWorld=compose(modelWorld,target);
     const Pose stock=gunstockWrist(primaryWorld,wristSupport.position,left.position,Pose{currentCamera,t.headOrientation});
     target=compose(worldModel,stock);
     fixedFingers=&binding.supportFingers;
    }
   }
  }
  if(h==0&&fixedSupport)target=*fixedSupport;
  float weight=blends[h].update(native.entity,rig,t.epoch,now,true,Context::Exploration);if(weight<=0)continue;
  // Ordinary shoulders retain their current native parent and bone lengths.
  // Seated special modes retain the existing calibrated arm frame.
  const Vec viewInModel=compose(worldModel,Pose{currentCamera,{}}).position;
  auto& anchored=anchoredArms[h];
  ArmPose baseArm{*a,*b,*c};
  if(!nativeBodyFrame){
   if(!anchored.ready&&!anchored.capture(*a,*b,*c,viewInModel))continue;
   baseArm=anchored.at(viewInModel);
  }
  if(armedPassenger){
   baseArm.shoulder=turnAroundView(baseArm.shoulder,viewInModel,passengerTurn);
   baseArm.elbow=turnAroundView(baseArm.elbow,viewInModel,passengerTurn);
   baseArm.wrist=turnAroundView(baseArm.wrist,viewInModel,passengerTurn);
  }
  auto solved=stableTrackedArm(baseArm.shoulder,baseArm.elbow,baseArm.wrist,target,upper,lower,h,armAxes[h],passengerTurn);if(!solved)continue;
  if(h==1&&supportHeld)fixedSupport=compose(compose(solved->wrist,chosenBinding->local),chosenBinding->support.value);
  // Bounded aggregate: distinguish IK reach limits from alternating target
  // frames without logging every joint or adding GPU synchronization.
  struct TurnAudit {uint64_t tick{},count{},clamped{};float error{},jump{},yawGap{};Vec previous{};bool valid{};};
  static TurnAudit turnAudit[2][2];auto& audit=turnAudit[renderEye][h];
  const auto relative=worldTarget.position-currentCamera;
  ++audit.count;audit.clamped+=solved->clamped;
  audit.error=std::max(audit.error,length(solved->wrist.position-target.position));
  if(audit.valid)audit.jump=std::max(audit.jump,length(relative-audit.previous));
  audit.previous=relative;audit.valid=true;
  audit.yawGap=std::max(audit.yawGap,std::abs(locomotion::wrap(native.angles.y-t.motion.nativeYaw)));
  if(now-audit.tick>1000){
   circlevr::vkcore::logf("motor-hand-turn-audit: eye %u hand %u samples %llu clamped %llu maxError %.5f maxWorldRelativeStep %.5f nativeYawGap %.3f",renderEye,h,audit.count,audit.clamped,audit.error,audit.jump,audit.yawGap);
   audit.tick=now;audit.count=audit.clamped=0;audit.error=audit.jump=audit.yawGap=0;
  }
  // The arm local pose must use the stabilized torso parent's global pose.
  for(const auto& changed:changes)if(changed.id==parents[shoulder])parent=changed.global;
  changes.push_back({shoulder,compose(inverse(*parent),solved->shoulder),solved->shoulder});
  changes.push_back({elbow,compose(inverse(solved->shoulder),solved->elbow),solved->elbow});
  changes.push_back({wrist,compose(inverse(solved->elbow),solved->wrist),solved->wrist});
  if(bindWeapon){
   const auto& weaponBind=*chosenBinding;
   const auto anchor=compose(solved->wrist,weaponBind.local);
   changes.push_back({reference,weaponBind.referenceLocal,compose(solved->wrist,weaponBind.referenceLocal)});
   changes.push_back({weapon,compose(inverse(*weaponParent),anchor),anchor});
   for(unsigned finger=0;finger<5;++finger){auto fingerParent=solved->wrist;
    for(unsigned joint=0;joint<4;++joint){const unsigned i=finger*4+joint;const auto fingerPose=compose(fingerParent,weaponBind.fingers[i]);changes.push_back({short(wrist+1+i),weaponBind.fingers[i],fingerPose});fingerParent=fingerPose;}
   }
   nextWeapon={compose(modelWorld,*nativeWeapon),compose(modelWorld,anchor),origin,native.entity,now,t.epoch,equippedEntity,equippedSlot};
  }
  if(h==1&&weapon>=0&&reference>=0&&uint64_t(weapon)<count&&uint64_t(reference)<count){
   auto anchor=get(weapon),ref=get(reference);
   if(anchor&&ref){
    attachment={solved->wrist,*anchor,*ref,equippedSlot,true,bindWeapon};
    if(bindWeapon){attachment.anchor=compose(solved->wrist,chosenBinding->local);attachment.reference=compose(solved->wrist,chosenBinding->referenceLocal);}
   }
  }
  if(h==0&&fixedSupport&&fixedFingers){
   for(unsigned finger=0;finger<5;++finger){auto parentPose=solved->wrist;
    for(unsigned joint=0;joint<4;++joint){unsigned i=finger*4+joint;const auto global=compose(parentPose,(*fixedFingers)[i]);
     changes.push_back({short(wrist+1+i),(*fixedFingers)[i],global});parentPose=global;}
   }
  }
  if(unarmed||(h==0&&isFirearm&&!fixedSupport)){
   const float extension=currentInput.hands[h].thumbRest&&!currentInput.hands[0].upper&&fistReady(currentInput.hands[h].squeeze)?fingerGesture[h]:0.f;
   const auto fingers=gripFingerPose(lp,parents,unsigned(wrist),solved->wrist,currentInput.hands[h].squeeze,extension);
   if(fingers){for(const auto& finger:*fingers)changes.push_back({short(finger.id),finger.local,finger.global});}
   else reject("finger topology or bone lengths",float(wrist));
  }

 }
 if(changes.empty())return;
 // Reconstruct the camera parent with modified ancestors, then preserve the
 // original camera global exactly. Never let torso edits steer the headset.
 if(torsoChanged){
  auto adjusted=lp;auto scratch=gp;auto uncached=flags;
  // Keep authoritative native globals outside the modified subtrees.
  for(size_t i=0;i<count;++i){int parent=parents[i];
   for(size_t depth=0;parent>=0&&depth<count;++depth){
    if(size_t(parent)>=count)return;
    bool dirty=false;for(const auto& changed:changes)dirty|=changed.id==parent;
    if(dirty){uncached[i]=0;break;}parent=parents[parent];
   }
  }
  for(const auto& changed:changes){adjusted[changed.id]=changed.local;scratch[changed.id]=changed.global;uncached[changed.id]=1;}
  const auto cameraParent=modelPose(adjusted,parents,unsigned(parents[cameraId]),scratch,uncached);
  if(!cameraParent)return;
  changes.push_back({cameraId,compose(inverse(*cameraParent),*camera),*camera});
 }

 auto originalLocal=localRaw,originalGlobal=globalRaw;auto originalMasks=masks;
 for(const auto& c:changes){pack(localRaw[c.id],c.local);pack(globalRaw[c.id],c.global);}
 // Preserve every unrelated local joint and all padding/scale lanes. Dirty
 // descendants follow changed parents exactly as native 963700 requires.
 for(size_t i=0;i<count;++i){int parent=parents[i];for(size_t depth=0;parent>=0&&depth<count;++depth){if(uint64_t(parent)>=count)return;bool changed=false;for(const auto& c:changes)changed|=c.id==parent;if(changed){masks[i/64]&=~(uint64_t(1)<<(i%64));break;}parent=parents[parent];}}
 for(const auto& c:changes)masks[c.id/64]|=uint64_t(1)<<(c.id%64);
 bool okay=true;for(const auto& c:changes)okay=write(locals+size_t(c.id)*48,localRaw[c.id].data(),28)&&write(globals+size_t(c.id)*48,globalRaw[c.id].data(),28)&&okay;
 okay=okay&&write(bits,masks.data(),masks.size()*8);
 if(!okay){for(const auto& c:changes){write(locals+size_t(c.id)*48,originalLocal[c.id].data(),28);write(globals+size_t(c.id)*48,originalGlobal[c.id].data(),28);}write(bits,originalMasks.data(),originalMasks.size()*8);return;}
 {std::lock_guard lock(weaponPoseMutex);weaponPose=nextWeapon;}
 if(renderEye==0){std::lock_guard lock(targetMutex);appliedTargets={t,{native.entity,native.gameTime,t.epoch,now},rig,modelWorld,currentCamera,attachment,nativeModel,nativeBodyFrame};}
 // Count the final native IK exit separately for each render chain. An
 // asymmetric blur must not be mistaken for a second skeletal update.
 static uint64_t eyeApplications[2]{},auditTick{};
 ++eyeApplications[renderEye];
 if(now-auditTick>2000){auditTick=now;
  circlevr::vkcore::logf("motor-body-camera-frame: rigid %d owner %llx eye %u native %.5f %.5f %.5f stable %.5f %.5f %.5f correction %.5f %.5f %.5f; native spine and shoulder positions retained",
   nativeBodyFrame,native.entity,renderEye,rawCamera.x,rawCamera.y,rawCamera.z,stable.x,stable.y,stable.z,stable.x-rawCamera.x,stable.y-rawCamera.y,stable.z-rawCamera.z);
  if(armedPassenger)circlevr::vkcore::logf("motor-passenger-combat-ik: owner %llx eye %u yaw %.3f bodySource %.5f %.5f %.5f correlation raw/stable %.5f/%.5f tracked %d/%d; seated root, tracked upper body and weapon",native.entity,renderEye,t.motion.passengerBodyYaw,t.physics.x,t.physics.y,t.physics.z,rawDistance,stableDistance,t.tracked[0],t.tracked[1]);
  circlevr::vkcore::logf("motor-hand-eye-audit: applications L %llu R %llu current eye %u native render %u rig %p context %p camera %.5f %.5f %.5f origin %.5f %.5f %.5f target tick %llu epoch %llu",eyeApplications[0],eyeApplications[1],renderEye,at<unsigned>(base+build::rva(0x4a71950)),(void*)rig,(void*)context,currentCamera.x,currentCamera.y,currentCamera.z,origin.x,origin.y,origin.z,t.tick,t.epoch);
  eyeApplications[0]=eyeApplications[1]=0;
 }
 if(now-lastLog>2000){lastLog=now;circlevr::vkcore::logf("motor-hand-ik: applied %zu arm/finger joints rig %p owner %llx tracked %d/%d target age %llu grip %.3f/%.3f unarmed %d camera correction %.4f %.4f %.4f",changes.size(),(void*)rig,native.entity,t.tracked[0],t.tracked[1],now-t.tick,currentInput.hands[0].squeeze,currentInput.hands[1].squeeze,unarmed,cameraCorrection.x,cameraCorrection.y,cameraCorrection.z);}
}
}
