#include "motor/build_profile.hpp"
#include "motor/diagnostics.hpp"
#include "locomotion_runtime.hpp"
#include "locomotion_probe.hpp"
#include "camera_watchdog.hpp"
#include "hand_runtime.hpp"
#include "punch_charge.hpp"
#include "physical_melee_timing.hpp"
#include "firearm_policy.hpp"
#include "controller_tracking.hpp"
#include "game_haptics.hpp"
#include "menu_pointer_policy.hpp"
#include "default_input.hpp"
#include "prompt_bindings.hpp"
#include "mounted_gun_runtime.hpp"
#include "interaction_runtime.hpp"
#include "motor/sequential.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <Xinput.h>
#include <mutex>
#include <atomic>
#include <cstring>
#include <safetyhook.hpp>
#include <cstdlib>
#include <chrono>
namespace circle::motor::locomotion {
namespace {
std::mutex mutex;
std::mutex cursorMutex;
std::mutex whipAttachMutex;
std::mutex combatCameraMutex;
CombatCameraEpisode combatCamera;
uint64_t whipAttachOwner{},whipAttachTick{};bool whipAttached{};
int cursorX{},cursorY{};uint64_t cursorTick{};bool cursorEnabled{};
std::atomic<bool> cursorInstalled{false};
void applyMenuCursor(safetyhook::Context&){
 std::lock_guard lock(cursorMutex);
 if(!cursorEnabled||GetTickCount64()-cursorTick>100)return;
 const auto base=(uintptr_t)GetModuleHandleW(nullptr);uintptr_t cursor{};SIZE_T got{};
 if(!ReadProcessMemory(GetCurrentProcess(),(void*)(base+build::rva(0x4a1dc78)),&cursor,8,&got)||got!=8||!cursor)return;
 unsigned char active{};
 if(!ReadProcessMemory(GetCurrentProcess(),(void*)cursor,&active,1,&got)||got!=1||active!=1)return;
 const int xy[2]={cursorX,cursorY};const unsigned char dirty=1;
 if(!WriteProcessMemory(GetCurrentProcess(),(void*)(cursor+0xc),xy,sizeof(xy),&got)||got!=sizeof(xy))return;
 WriteProcessMemory(GetCurrentProcess(),(void*)(cursor+2),&dirty,1,&got);
 static uint64_t lastLog{};const auto now=GetTickCount64();
 if(now-lastLog>2000){lastLog=now;circlevr::vkcore::logf("motor-native-menu-cursor: applied %d,%d age %llu",cursorX,cursorY,now-cursorTick);}
}
RoomscaleFollow follow;
CameraAnchorState cameraAnchor;
StairHeight stairHeight;
RoomscaleView roomscaleView;
CameraPositionWatchdog cameraWatchdog;
BodyYawFollow bodyFollow;
float pendingTurn{},nativeTurnSpeed=180,previousYaw{};
uint64_t previousYawTick{};
float movementYaw{};
WaterBasis waterDirection{};uint64_t waterOwner{},waterTick{};
uint64_t published{},nativeTime{},epoch{};
uint64_t lastRoomscaleInput{};
bool followSubmitted{}; // the current pending translation actually reached native input
Vec referenceHead{};
float referenceYaw{},referenceBodyYaw{};
bool ready{},installed{};
XINPUT_GAMEPAD realPad{};
kharvox::MovementStick2D pending{};
using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
std::atomic<GetState> original{};
float yaw(const std::array<float,4>& q){return trackingYaw(q);}
bool enabled(){static bool on=[] {char b[8]{};return GetEnvironmentVariableA("CIRCLE_ROOMSCALE",b,8)&&b[0]=='1';}();return on;}
bool debugEnabled(){static const bool on=[] {char b[8]{};return GetEnvironmentVariableA("CIRCLE_ROOMSCALE_DEBUG",b,8)&&b[0]=='1';}();return on;}
bool inputEnabled(){static bool on=[] {char b[8]{};if(GetEnvironmentVariableA("CIRCLE_SIMULATOR_DESKTOP_INPUT",b,8)==1&&b[0]=='1')return false;return GetEnvironmentVariableA("CIRCLE_INPUT",b,8)&&b[0]=='1';}();return on;}
bool inputDebug(){static bool on=[] {char b[8]{};return GetEnvironmentVariableA("CIRCLE_INPUT_DEBUG",b,8)&&b[0]=='1';}();return on;}
input::Profile defaultProfile;
input::Result profileResult;
uint64_t profileTime{};
input::Pad unpackPad(const XINPUT_GAMEPAD& p){return {p.wButtons,p.sThumbLX/32767.f,p.sThumbLY/32767.f,p.sThumbRX/32767.f,p.sThumbRY/32767.f,p.bLeftTrigger/255.f,p.bRightTrigger/255.f};}
XINPUT_GAMEPAD packPad(input::Pad p){XINPUT_GAMEPAD out{};out.wButtons=p.buttons;out.sThumbLX=SHORT(input::axis(p.lx)*32767);out.sThumbLY=SHORT(input::axis(p.ly)*32767);out.sThumbRX=SHORT(input::axis(p.rx)*32767);out.sThumbRY=SHORT(input::axis(p.ry)*32767);out.bLeftTrigger=BYTE(input::scalar(p.lt)*255);out.bRightTrigger=BYTE(input::scalar(p.rt)*255);return out;}
input::Result resolveInput(bool foreground){
 static uint64_t profileLife{};const auto life=hands::combatLifeGeneration();
 if(profileLife!=life){defaultProfile={};profileResult={};profileTime=0;profileLife=life;}
 const auto snapshot=hands::latest();input::Physical physical;physical.focused=foreground&&snapshot.focused;physical.session=snapshot.session;
 physical.motionTick=snapshot.tick;
 for(unsigned h=0;h<2;++h){const auto& v=snapshot.hands[h];if(v.tracked&&v.velocityValid)physical.speed[h]=std::hypot(v.velocity.x,v.velocity.y,v.velocity.z);}
 for(unsigned h=0;h<2;++h){const auto& v=snapshot.hands[h];physical.hand[h]={v.stick.x,v.stick.y,v.trigger,v.squeeze,v.lower,v.upper,v.click,v.menu,v.tracked,v.thumbRest};}
 const auto n=latestNative();const auto now=GetTickCount64();
 const bool fresh=n.valid&&now-n.tick<100;
 const input::Native inputNative{fresh,sequential::menuActive(),sequential::projectionAllowed(),n.animated,n.localSpace,n.movementMode,fresh?hands::weaponSlot(n.entity):0,n.movementType,n.explicitReason,journalState(),fresh&&hands::equippedFirearm(n.entity),n.whipPrompt,blowInteractionActive()};
 const auto ctx=input::context(inputNative);
 physical.equippedFood=fresh&&hands::equippedFood(n.entity);
 physical.rightWhip=hands::whipInput(snapshot,n).held;
 {std::lock_guard lock(whipAttachMutex);physical.whipAttached=fresh&&whipAttachOwner==n.entity&&now>=whipAttachTick&&now-whipAttachTick<100&&whipAttached;}
 physical.nearUsableHands=interaction::nearHands(snapshot,n);
 const auto focus=interaction::focusActions(n);physical.actor=n.entity;physical.focusFresh=focus.fresh;
 physical.pickupTarget=focus.pickupTarget;physical.temporaryFirearm=hands::temporaryFirearm(n.entity);
 physical.useTarget=focus.useTarget;physical.placeTarget=focus.placeTarget;physical.releaseUseTarget=focus.otherTarget;
 static interaction::PickupStage pickupStage;
 physical.pickupReady=pickupStage.update(n.entity,hands::weaponSlot(n.entity),focus);
 static uint64_t pauseWindow=[] {char b[32]{};if(!GetEnvironmentVariableA("CIRCLE_PAUSE_WINDOW_MS",b,32))return 60ull;return std::clamp(strtoull(b,nullptr,10),50ull,300ull);}();
 physical.supportGrip=hands::supportGripInput(snapshot,n.entity,input::firearmContext(ctx)&&fresh&&sequential::projectionAllowed()&&!sequential::menuActive());
 const auto interactionAxis=hands::interactionStick(snapshot,n);
 auto out=defaultProfile.update(physical,ctx,now,hands::punchTriggers(snapshot,n),interactionAxis[0],pauseWindow,interactionAxis[1]);
 // Mounted aiming is consumed only by the native gun joint calculation.
 // Do not synthesize right-stick motion: that also turns the native camera.
 turret::manualInput(std::hypot(physical.hand[1].x,physical.hand[1].y)>=.15f||
  std::hypot(realPad.sThumbRX/32767.f,realPad.sThumbRY/32767.f)>=.15f);
 const bool mouthAllowed=fresh&&physical.focused&&physical.equippedFood&&
  (ctx==input::Context::Normal||ctx==input::Context::Carrying||ctx==input::Context::Melee)&&
  !out.modifier&&!out.pause&&!out.holster&&!out.pad.buttons&&!physical.rightWhip&&
  !physical.hand[0].upper&&!physical.hand[0].menu&&!physical.hand[1].click&&!physical.hand[0].click&&
  !n.animated&&!n.localSpace&&!n.movementType&&!n.explicitReason;
 if(hands::mouthConsumeInput(snapshot,n.entity,mouthAllowed,
    physical.hand[1].trigger>.1f||out.pad.rt>.1f||realPad.bRightTrigger>25))out.pad.rt=1.f;
 out.waterMotion=input::nativeWaterMotion(inputNative,out);
 static hands::VelocityWhipPull velocityPull;
 static uint64_t pullLife{};if(pullLife!=life){velocityPull={};pullLife=life;}
 const auto& pullHand=snapshot.hands[1];const auto& q=pullHand.aim.orientation;
 const hands::Vec pullForward=hands::rotate({q.x,q.y,q.z,q.w},{0,0,-1});
 const bool pullContext=ctx==input::Context::WhipAttached||
  (ctx==input::Context::Whip&&physical.rightWhip&&physical.whipAttached);
 const bool pullSafe=pullContext&&fresh&&snapshot.tick&&now>=snapshot.tick&&now-snapshot.tick<100&&
  physical.focused&&pullHand.tracked&&pullHand.aimTracked&&pullHand.velocityValid&&
  !out.modifier&&!out.pause&&!snapshot.hands[0].upper&&!snapshot.hands[0].menu&&
  !(snapshot.hands[0].click&&pullHand.click)&&sequential::projectionAllowed()&&!sequential::menuActive();
 const bool pulling=velocityPull.update(n.entity,snapshot.session,snapshot.time>0?uint64_t(snapshot.time/1000000):0,
  {pullHand.velocity.x,pullHand.velocity.y,pullHand.velocity.z},pullForward,pullSafe);
 if(pulling){out.pad.buttons|=input::LB|input::RB;out.pad.rt=0;}
 static bool previousPull{};
 if(pulling&&!previousPull)circlevr::vkcore::logf("motor-whip-velocity-pull: owner %llx context %s; native yank pulse",n.entity,input::name(ctx));
 previousPull=pulling;
 static input::StandAfterClimb standAfterClimb;
 const bool stand=standAfterClimb.update(inputNative,n.entity,physical.session,now,
     physical.focused&&physical.hand[1].tracked,
     physical.hand[1].lower||out.modifier||out.pause||(realPad.wButtons&input::B));
 if(stand)out.pad.buttons|=input::B;
 static bool standingPulse{};
 if(stand!=standingPulse){standingPulse=stand;circlevr::vkcore::logf("motor-post-climb-stand: pulse %d owner %llx mode %u type %d; native crouch toggle with headroom checks",stand,n.entity,n.movementMode,n.movementType);}
 hands::setFirearmMeleeMode(out.context==input::Context::Firearm&&out.firearmMelee&&!out.modifier&&!out.pause&&physical.focused);
 static bool previousMelee{};if(previousMelee!=out.firearmMelee){previousMelee=out.firearmMelee;circlevr::vkcore::logf("motor-firearm-mode: %s",previousMelee?"melee":"shooting");}
 static auto previousCarry=input::CarryAction::None;
 if(out.carryAction!=previousCarry){previousCarry=out.carryAction;circlevr::vkcore::logf("motor-grip-carry: action %u context %s owner %llx use %llx place %llx fresh %d",unsigned(out.carryAction),input::name(ctx),n.entity,physical.useTarget,physical.placeTarget,physical.focusFresh);}
 static bool interacting{};const bool handUse=(out.pad.buttons&input::X)&&!physical.hand[0].lower&&input::gameplay(ctx);
 if(handUse!=interacting){interacting=handUse;circlevr::vkcore::logf("motor-hand-interact: native X %d near mask %u owner %llx slot %u pickupReady %d target %llx",handUse,physical.nearUsableHands,n.entity,hands::weaponSlot(n.entity),physical.pickupReady,physical.useTarget);}
 static unsigned gripMask{};unsigned currentGrip=gripMask;
 for(unsigned h=0;h<2;++h){const auto& hand=physical.hand[h];if(!hand.tracked||hand.grip<=.65f)currentGrip&=~(1u<<h);else if(hand.grip>=.9f)currentGrip|=1u<<h;}
 if(currentGrip!=gripMask){gripMask=currentGrip;circlevr::vkcore::logf("motor-grip-edge: mask %u grip %.3f/%.3f trigger %.3f/%.3f context %s slot %u pickupReady %d focus %d use %llx fresh %d Xbox %04x",gripMask,physical.hand[0].grip,physical.hand[1].grip,physical.hand[0].trigger,physical.hand[1].trigger,input::name(ctx),hands::weaponSlot(n.entity),physical.pickupReady,physical.focused,physical.useTarget,physical.focusFresh,out.pad.buttons);}
 static input::Context movementContext=input::Context::Unknown;static unsigned inputMode=255;
 if(ctx!=movementContext||inputMode!=n.movementMode){
  circlevr::vkcore::logf("motor-input-context: %s -> %s; mode %u slot %u journal %d waterBasis %d held left %.3f/%.3f routed %.3f/%.3f motion %d",input::name(movementContext),input::name(ctx),n.movementMode,inputNative.slot,inputNative.journal,out.waterMotion,physical.hand[0].x,physical.hand[0].y,out.pad.lx,out.pad.ly,out.motion);
  movementContext=ctx;inputMode=n.movementMode;
 }
 static unsigned previousCosmetic{};const unsigned cosmetic=(out.fingerGesture[0]>=.9f?1u:0)|(out.fingerGesture[1]>=.9f?2u:0);
 if(cosmetic!=previousCosmetic){previousCosmetic=cosmetic;circlevr::vkcore::logf("motor-hand-cosmetic: mask %u extension %.3f %.3f native triggers %.3f %.3f context %s",cosmetic,out.fingerGesture[0],out.fingerGesture[1],out.pad.lt,out.pad.rt,input::name(ctx));}
 if(inputDebug()){
  static XINPUT_GAMEPAD previous{};static int oldContext=-1,oldModifier=-1;const auto pad=packPad(out.pad);
  if(std::memcmp(&pad,&previous,sizeof(pad))||oldContext!=int(ctx)||oldModifier!=int(out.modifier)){
   circlevr::vkcore::logf("motor-default-input: context %s mode %u type %d slot %u modifier %d focus %d tracked %d/%d buttons %04x LT/RT %u/%u LS %d,%d RS %d,%d motion %d gestures %d; native acknowledgement in motor-input-edge",input::name(ctx),n.movementMode,n.movementType,fresh?hands::weaponSlot(n.entity):0,out.modifier,physical.focused,physical.hand[0].tracked,physical.hand[1].tracked,pad.wButtons,pad.bLeftTrigger,pad.bRightTrigger,pad.sThumbLX,pad.sThumbLY,pad.sThumbRX,pad.sThumbRY,out.motion,out.gestures);
   const auto& l=physical.hand[0];const auto& r=physical.hand[1];
   circlevr::vkcore::logf("motor-input-source: L lower/upper/click %d%d%d trigger/grip %.3f/%.3f axis %.3f %.3f; R %d%d%d trigger/grip %.3f/%.3f axis %.3f %.3f",l.lower,l.upper,l.click,l.trigger,l.grip,l.x,l.y,r.lower,r.upper,r.click,r.trigger,r.grip,r.x,r.y);
   previous=pad;oldContext=int(ctx);oldModifier=out.modifier;
  }
 }
 prompts::publish(out.context,out.firearmMelee,inputEnabled());
 profileResult=out;profileTime=now;return out;
}
const Settings& settings(){static Settings value=[] {
 Settings s;
 const auto number=[](const char* name,float fallback,float low,float high){char b[64]{};if(!GetEnvironmentVariableA(name,b,64))return fallback;char* end{};float v=std::strtof(b,&end);return end&&!*end&&std::isfinite(v)&&v>=low&&v<=high?v:fallback;};
 s.yawDeadzone=number("CIRCLE_BODY_DEADZONE",25,0,60);s.followSpeed=number("CIRCLE_BODY_SPEED",120,10,360);
 s.roomscaleStart=number("CIRCLE_ROOMSCALE_START",.015f,.005f,.1f);s.roomscaleStop=number("CIRCLE_ROOMSCALE_STOP",.006f,0,s.roomscaleStart*.9f);
 s.maxResidual=number("CIRCLE_BODY_RESIDUAL",70,s.yawDeadzone,120);s.snapAngle=number("CIRCLE_SNAP_ANGLE",45,10,90);s.smoothSpeed=number("CIRCLE_TURN_SPEED",180,10,360);
 char b[24]{};if(GetEnvironmentVariableA("CIRCLE_BODY_FOLLOW",b,24))s.bodyFollow=b[0]=='1';
 if(GetEnvironmentVariableA("CIRCLE_TURN_MODE",b,24))s.snap=std::strcmp(b,"snap")==0;
 if(GetEnvironmentVariableA("CIRCLE_MOVE_DIRECTION",b,24)&&!std::strcmp(b,"offhand"))s.direction=kharvox::MovementDirectionMode::OffHand;
 return s;}();return value;}
// Motor only consumes a newer packet. Neutral releases must advance the same
// counter as movement, including focus loss and transitions to the menu.
void publishPad(XINPUT_STATE* output,const XINPUT_GAMEPAD& pad,DWORD result){
 static DWORD packet=0;static XINPUT_GAMEPAD last{};
 if(result==ERROR_SUCCESS&&int32_t(output->dwPacketNumber-packet)>0)packet=output->dwPacketNumber;
 if(std::memcmp(&last,&pad,sizeof(pad))){++packet;last=pad;}
 output->dwPacketNumber=packet;output->Gamepad=pad;
}
DWORD WINAPI getState(DWORD user,XINPUT_STATE* output){
 auto fn=original.load();auto result=fn?fn(user,output):DWORD(ERROR_DEVICE_NOT_CONNECTED);
 if(user||!output)return result;
 std::lock_guard lock(mutex);realPad=result==ERROR_SUCCESS?output->Gamepad:XINPUT_GAMEPAD{};
 if(!inputEnabled())return result;
 DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
 const auto resolved=resolveInput(foreground==GetCurrentProcessId());
 auto virtualPad=resolved.pad;
 // Only ordinary locomotion is transformed into the VR heading. Scripted,
 // inspection and unknown contexts receive the original native axes/holds.
 if(resolved.motion&&foreground==GetCurrentProcessId()&&sequential::projectionAllowed()){
  const auto movement=kharvox::fullTravelMovementStick({virtualPad.lx,virtualPad.ly});
  // Water receives a head-oriented 3D physics basis below. Rotating these
  // axes as well would apply headset yaw twice.
  const auto rotated=resolved.waterMotion?movement:kharvox::rotateMovementStickForDirection(movement,movementYaw);
  virtualPad.lx=rotated.x;virtualPad.ly=rotated.y;virtualPad.ry=0;
  if(ready&&inputEnabled()&&GetTickCount64()-published<=100){
   if(enabled()&&std::hypot(virtualPad.lx,virtualPad.ly)<.15f&&std::hypot(float(realPad.sThumbLX),float(realPad.sThumbLY))<4915){virtualPad.lx=pending.x;virtualPad.ly=pending.y;if(pending.x||pending.y){lastRoomscaleInput=GetTickCount64();followSubmitted=true;}}
   char turn[4]{};if(GetEnvironmentVariableA("CIRCLE_BODY_NATIVE_TURN",turn,4)&&turn[0]=='1')virtualPad.rx=pendingTurn;
  }else virtualPad.rx=0;
 }else if(resolved.motion){
  virtualPad.lx=virtualPad.ly=virtualPad.rx=virtualPad.ry=0;
 }
 // Preserve a physical pad independently, including axes in native space.
 auto combined=input::merge(unpackPad(realPad),virtualPad);
 const auto mountedNative=latestNative();
 if(resolved.context==input::Context::Traversal&&mountedNative.valid&&mountedNative.movementMode==27&&
    mountedNative.movementType==5&&hands::weaponSlot(mountedNative.entity)==22){
  combined.lx=combined.ly=0; // No walking/body translation at the fixed gun seat.
  const bool gunReady=turret::ready(mountedNative.entity);
  turret::stickInput(gunReady?combined.rx:0.f,gunReady?combined.ry:0.f);
  combined.rx=combined.ry=0; // Both aiming inputs feed only the gun's private angles.
 }else turret::stickInput(0,0);
 if(input::seatedContext(resolved.context))combined.lx=combined.ly=combined.rx=combined.ry=0;
 if(resolved.context==input::Context::BoatDriver||resolved.context==input::Context::BoatMap){
  combined.rx=combined.ry=0;
  const unsigned navigation=resolved.context==input::Context::BoatMap?input::LB|input::RB:0;
  combined.buttons&=input::Start|input::Back|input::X|input::A|input::B|navigation;
 }
 const auto merged=packPad(combined);
 if(inputDebug()){static XINPUT_GAMEPAD last{};if(std::memcmp(&last,&merged,sizeof(last))){circlevr::vkcore::logf("motor-input-output: Xbox buttons %04x LT/RT %u/%u LS %d,%d RS %d,%d physical result %lu",merged.wButtons,merged.bLeftTrigger,merged.bRightTrigger,merged.sThumbLX,merged.sThumbLY,merged.sThumbRX,merged.sThumbRY,result);last=merged;}}
 publishPad(output,merged,result);return ERROR_SUCCESS;
}
void discover(safetyhook::Context& r){if(r.rdi==0&&inputEnabled())r.rflags&=~uintptr_t(0x40);}
template<class T>bool whipRead(uintptr_t p,T& out){SIZE_T got{};return p&&ReadProcessMemory(GetCurrentProcess(),(void*)p,&out,sizeof(out),&got)&&got==sizeof(out);}
void selectShoulderWhip(safetyhook::Context& r){
 // Normal Indiana loadout selector, after the native bit13 equip edge.
 // Only add a draw edge; all following availability/inhibit checks and the
 // native switchWeaponInputAction queue remain responsible for equipping.
 uintptr_t loadout{},cmd{};uint64_t attachment{},buttons{},previous{};
 if(!whipRead(r.rdi+0x38,loadout)||!loadout||!whipRead(r.rdi+0xb8,cmd)||!cmd||
    !whipRead(loadout+0xb8,attachment)||attachment==UINT64_MAX||
    !whipRead(cmd,buttons)||!whipRead(cmd+0x30,previous))return;
 const auto out=hands::whipInput(hands::latest(),latestNative(),loadout);
 if(!out.held||!(out.actions&hands::whipSelect)||!(buttons&hands::whipSelect)||(previous&hands::whipSelect))return;
 r.r10=(r.r10&~uintptr_t(0xff))|1;
 circlevr::vkcore::logf("motor-whip-equip: shoulder draw routed to native Indiana selector; loadout %p; no crack edge",(void*)loadout);
}
void creditPhysicalPunch(safetyhook::Context& r,uintptr_t query,unsigned nativeHand,unsigned stage){
 // Called only at Motor's accepted charge-start timestamp stores. No native
 // charge state exists while the user merely holds/releases the VR trigger.
 uintptr_t loadout{};unsigned char slot{};
 if(!whipRead(query+0x148,loadout)||!loadout||!whipRead(loadout+0x10c,slot)||!hands::meleeChargeSlot(slot))return;
 const auto charge=hands::consumePunchCharge(loadout,nativeHand,stage);
 if(!charge)return;
 const auto before=int64_t(r.rax);if(before<=0)return;
 r.rax=uintptr_t(hands::creditedPunchStart(before,charge));
 circlevr::vkcore::logf("motor-punch-charge: native hand %u stage %u credited %u ms start %lld -> %lld; physical strike only",nativeHand,stage,charge,before,int64_t(r.rax));
}
void creditPunchStart(safetyhook::Context& r){creditPhysicalPunch(r,r.rbx,unsigned(r.rsi),0);}
void creditPunchPrecharge(safetyhook::Context& r){creditPhysicalPunch(r,r.rdi,unsigned(r.rbx),1);}
void creditPunchTransition(safetyhook::Context& r){
 unsigned hand=~0u;if(whipRead(r.rcx+0xb8,hand))creditPhysicalPunch(r,r.rdi,hand,2);
}
// Suppress the combat-only advance channel throughout ordinary tracked VR.
// Native walking, turning, jumping and hit-event timing use separate channels.
void suppressPhysicalAttackAdvance(uintptr_t query){
 const auto n=latestNative();const auto input=hands::latest();const auto now=GetTickCount64();
 uintptr_t record{},player{},shared{};uint64_t owner{};unsigned action{},handle{};
 if(!n.valid||now-n.tick>100||!input.focused||now-input.tick>100||!input.hands[1].tracked||
    !sequential::projectionAllowed()||sequential::menuActive()||
    !whipRead(query+0x18,record)||!whipRead(record,owner)||owner!=n.entity||
    !whipRead(query+0x28,player)||!player||!whipRead(query+0x48,shared)||!shared||
    !whipRead(shared+0x19c,action)||!whipRead(shared+0x18c,handle))return;
 if(!inputEnabled()||!trackedGameplayState(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace))return;
 float distance{};
 if(!whipRead(player+0x110,distance)||!std::isfinite(distance)||std::abs(distance)>10.f||distance==0)return;
 const float zero=0;SIZE_T written{};
 if(WriteProcessMemory(GetCurrentProcess(),(void*)(player+0x110),&zero,sizeof(zero),&written)&&written==sizeof(zero))
  circlevr::vkcore::logf("motor-physical-attack-advance: owner %llx action %u handle %u native chargeAttackMove distance %.4f -> 0; native hit events retained",owner,action,handle,distance);
}
void finishPhysicalCharge(safetyhook::Context& r){
 // idMeleeUpdateJob, immediately after a native charge was accepted. The
 // ordinary short LT/RT pulse only starts charging; releasing it later still
 // depends on the flat game's charge-animation availability. A VR strike has
 // already charged locally AND swung, so dispatch the native charged-release
 // helper now, on its own job thread and with its original query/context.
 uintptr_t combat{},loadout{},node{};unsigned state{},hand{};unsigned char slot{};
 if(!whipRead(r.rdi+0x28,combat)||!combat||!whipRead(r.rdi+0x148,loadout)||!loadout||
    !whipRead(loadout+0x10c,slot)||!hands::meleeChargeSlot(slot)||!whipRead(combat+0xa0,state)||state!=2||
    !whipRead(combat+0xb8,hand)||(hand>1&&hand!=UINT_MAX)||!whipRead(combat+0xa8,node)||!node)return;
 if(hands::consumeOrdinaryItemStrike(loadout,hand,state)){
  // Same short-release transition as 148F682..148F6FF: keep the accepted
  // combo node, clear charge once, dispatch its ordinary (not charged) attack.
  // Native helper retains attack declaration, cooldown and downstream damage.
  std::array<unsigned char,32> cleared{};const unsigned invalid=~0u;
  std::memcpy(cleared.data()+24,&invalid,sizeof(invalid));
  std::memcpy(reinterpret_cast<void*>(combat+0xa0),cleared.data(),cleared.size());
  using Ordinary=bool(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,int);
  reinterpret_cast<Ordinary>(uintptr_t(GetModuleHandleW(nullptr))+build::rva(0x1494ad0))(r.rsi,r.rdi,r.r14,node,-1);
  suppressPhysicalAttackAdvance(r.rdi);
  circlevr::vkcore::logf("motor-item-immediate-release: hand %u slot %u accepted physical swing -> ordinary attack; charge wait removed",hand,unsigned(slot));
  return;
 }
 uintptr_t chargedDecl{},ordinaryDecl{};unsigned chargedType=~0u,ordinaryType=~0u;
 whipRead(node+0xe8,chargedDecl);whipRead(node+0xe0,ordinaryDecl);
 if(chargedDecl)whipRead(chargedDecl+0x60,chargedType);
 if(ordinaryDecl)whipRead(ordinaryDecl+0x60,ordinaryType);
 const auto charge=hands::consumePunchCharge(loadout,hand,3);
 if(!charge)return;
 using Release=void(*)(uintptr_t,uintptr_t,uintptr_t);
 auto release=reinterpret_cast<Release>(uintptr_t(GetModuleHandleW(nullptr))+build::rva(0x1494420));
 release(r.rsi,r.rdi,r.r14);
 suppressPhysicalAttackAdvance(r.rdi);
 unsigned after=~0u;whipRead(combat+0xa0,after);
 uintptr_t shared{},acceptedDecl{};unsigned action=~0u;
 if(whipRead(r.rdi+0x48,shared)&&shared){whipRead(shared+0x180,acceptedDecl);whipRead(shared+0x19c,action);}
 circlevr::vkcore::logf("motor-charged-result: slot %u hand %u ordinary %p type %u charged %p type %u accepted %p action %u match %d",unsigned(slot),hand,(void*)ordinaryDecl,ordinaryType,(void*)chargedDecl,chargedType,(void*)acceptedDecl,action,chargedDecl&&acceptedDecl==chargedDecl&&action==chargedType);

 circlevr::vkcore::logf("motor-punch-release: native hand %u charge %u ms accepted -> charged release; state %u -> %u; controller IK retained",hand,charge,state,after);
}
void finishAcceptedPunch(safetyhook::Context& r){if((r.rax&0xff)!=0)finishPhysicalCharge(r);}
void auditItemAttack(uintptr_t query,bool accepted){
 // Observe native attack acceptance and the later animation handoff separately.
 // No animation events, damage requests or shared declarations are modified.
 const auto n=latestNative();const auto now=GetTickCount64();
 uintptr_t ownerRecord{},combat{},loadout{};uint64_t owner{};unsigned char slot{};
 if(!n.valid||now-n.tick>100||!whipRead(query+0x18,ownerRecord)||!whipRead(ownerRecord,owner)||owner!=n.entity||
    !whipRead(query+0x48,combat)||!combat||!whipRead(query+0x148,loadout)||
    !whipRead(loadout+0x10c,slot)||!(slot==1||hands::itemMeleeSlot(slot)))return;
 int weaponPose=-1;
 if(whipRead(combat+0x10,weaponPose))hands::observeEquippedWeaponPose(owner,slot,loadout,weaponPose);
 unsigned action{},handle{};int node{};uintptr_t data{};uint64_t start{};
 if(!whipRead(combat+0x19c,action)||!whipRead(combat+0x18c,handle)||!whipRead(combat+0x188,node)||
    !whipRead(combat+0x190,data)||!whipRead(combat+0x158,start))return;
 {std::lock_guard lock(combatCameraMutex);combatCamera.observe(owner,action,handle,node,accepted,now);}
 static std::mutex auditMutex;std::lock_guard lock(auditMutex);
 static uint64_t previousOwner{},last{};static unsigned previousHandle{},previousAction{};static int previousNode{};
 if(!accepted&&owner==previousOwner&&handle==previousHandle&&action==previousAction&&node==previousNode&&now-last<500)return;
 previousOwner=owner;previousHandle=handle;previousAction=action;previousNode=node;last=now;
 float hitFrame=-1,hitTime=-1; if(data){whipRead(data+0x40,hitFrame);whipRead(data+0x44,hitTime);}
 circlevr::vkcore::logf("motor-item-attack-timing: accepted %d owner %llx slot %u simulation %llu action %u handle %u node %d start %llu hitFrame %.3f hitTime %.4f combat %p",accepted,owner,unsigned(slot),n.gameTime,action,handle,node,start,hitFrame,hitTime,(void*)combat);
}
// r148: 46 accelerated strikes, no observed hit event for those handles.
// Disabled unconditionally: an environment override must not re-enable the
// failed event-skipping experiment in an otherwise corrected release.
bool fastPhysicalMelee(){return false;}
void accelerateAcceptedStrike(uintptr_t query){
 if(!fastPhysicalMelee())return;
 uintptr_t combat{},loadout{};unsigned action{};float scale{};int64_t start{},end{};
 if(!whipRead(query+0x48,combat)||!combat||!whipRead(query+0x148,loadout)||!loadout||
    !whipRead(combat+0x19c,action)||!hands::acceleratedStrike(action)||
    !whipRead(combat+0xc0,scale)||!std::isfinite(scale)||scale<=0||scale>32||
    !whipRead(combat+0x158,start)||start<=0||!whipRead(combat+0x100,end))return;
 if(!hands::consumePhysicalStrikeTiming(loadout,action))return;
 const float fast=hands::physicalStrikeTimeScale(scale);
 const int64_t cooldown=hands::physicalStrikeCooldown(start,end);
 // Per-instance pending action, before the animation graph consumes its signal.
 // Do not change shared declarations, pose matrices or animation-event ordering.
 // The native event still owns damage, target checks, durability and stamina.
 std::memcpy(reinterpret_cast<void*>(combat+0xc0),&fast,sizeof(fast));
 std::memcpy(reinterpret_cast<void*>(combat+0x100),&cooldown,sizeof(cooldown));
 unsigned handle{};whipRead(combat+0x18c,handle);
 circlevr::vkcore::logf("motor-fast-melee: action %u handle %u scale %.2f -> %.2f cooldown %lld -> %lld; one physical swing, native hit event retained",action,handle,scale,fast,end-start,cooldown-start);
}
void auditAcceptedItemAttack(safetyhook::Context& r){if(r.rax&0xff){accelerateAcceptedStrike(r.rbx);auditItemAttack(r.rbx,true);}}
void auditPhysicalHitEvent(safetyhook::Context& r){
 // After native node/event validation, before stamina/target processing.
 const auto n=latestNative();uintptr_t record{};uint64_t owner{},start{};unsigned action{},handle{};
 if(!n.valid||GetTickCount64()-n.tick>100||!whipRead(r.rbp-0x68,record)||!whipRead(record,owner)||owner!=n.entity||
    !whipRead(r.rsi+0x19c,action)||!hands::acceleratedStrike(action)||!whipRead(r.rsi+0x18c,handle)||!whipRead(r.rsi+0x158,start))return;
 circlevr::vkcore::logf("motor-melee-hit-event: owner %llx action %u handle %u accepted-to-event %.3f ms; native target/damage processing follows",owner,action,handle,n.gameTime>=start?double(n.gameTime-start)/999.96:-1.0);
}
void auditItemAttackFrame(safetyhook::Context& r){suppressPhysicalAttackAdvance(r.rbp-0x50);auditItemAttack(r.rbp-0x50,false);}
void traceShoulderWhip(safetyhook::Context& r){
 // idWhipUpdateJob, after decoding native held/rising/falling bit13.
 // R12B holds, R15B cracks, BL releases; R13 is the native inhibit mask.
 // Observe only. Grip owns equipment through IDLE/CRACK_END routing, not
 // through attack held/released flags: forcing those strands native LOOSE52.
 uintptr_t loadout{};if(!whipRead(r.rdi+0x10,loadout)||!loadout)return;
 const auto out=hands::whipInput(hands::latest(),latestNative(),loadout);
 const bool held=(r.r12&255)!=0,pressed=(r.r15&255)!=0,released=(r.rbx&255)!=0;
 uintptr_t whip{};unsigned char state=255,slot=0;whipRead(r.rdi+0x38,whip);if(whip)whipRead(whip+0x3d6,state);whipRead(loadout+0x10c,slot);
 uint64_t attached=UINT64_MAX;const auto native=latestNative();
 {std::lock_guard lock(whipAttachMutex);whipAttachOwner=native.entity;whipAttachTick=GetTickCount64();
  whipAttached=native.valid&&whipAttachTick-native.tick<100&&slot==18&&whipRead(whip+0x98,attached)&&attached!=UINT64_MAX;}
 static std::atomic<unsigned> previous{~0u};
 const unsigned trace=state|(unsigned(slot)<<8)|(out.held?1u<<16:0)|(held?1u<<17:0)|(pressed?1u<<18:0)|(r.r13?1u<<19:0);
 if(previous.exchange(trace)!=trace)
  circlevr::vkcore::logf("motor-whip-hold: owned %d held %d crack-edge %d release %d inhibit %llx slot %u state %u loadout %p; native attack flags unchanged",out.held,held,pressed,released,r.r13,slot,state,(void*)loadout);
}
hands::WhipIdleRoute ownedWhipIdle(uintptr_t query,uintptr_t& whip){
 uintptr_t loadout{},inhibitComponent{};uint64_t inhibit{},attached{};
 unsigned char slot{},pendingSlot{},state{},switchState{};
 if(!whipRead(query+0x10,loadout)||!loadout||!whipRead(query+0x38,whip)||!whip||
    !whipRead(query+0x28,inhibitComponent)||!inhibitComponent||!whipRead(inhibitComponent,inhibit)||
    !whipRead(loadout+0x10c,slot)||!whipRead(loadout+0x151,pendingSlot)||!whipRead(loadout+0x150,switchState)||
    !whipRead(whip+0x3d6,state)||!whipRead(whip+0x98,attached))return hands::WhipIdleRoute::Native;
 const auto input=hands::latest();
 const bool owned=input.hands[1].squeeze>.35f&&hands::whipInput(input,latestNative(),loadout).held;
 // Bit5 only inhibits attacks (checked again by the native crack path); it
 // must not turn a temporary attack cooldown into an automatic grip release.
 return hands::whipIdleRoute(owned,(inhibit&0x100000)!=0,slot,pendingSlot,state,attached!=UINT64_MAX,switchState);
}
void keepOwnedWhipJobActive(safetyhook::Context& r){
 // The native precheck otherwise only admits a whip during staged equip or
 // special attachments. Retain its earlier death/movement guards and admit
 // our ordinary, unattached IDLE/CRACK_END hold to the state machine as well.
 uintptr_t whip{};
 if(ownedWhipIdle(r.rdi,whip)==hands::WhipIdleRoute::Native)return;
 r.rip=(uintptr_t)GetModuleHandleW(nullptr)+build::rva(0x19b29c5);
}
void routeOwnedWhipIdle(safetyhook::Context& r){
 // Before ADD RCX,RDX / JMP RCX (exactly five bytes, no overlap with the
 // following IDLE branch target). RCX is still a relative RVA here. IDLE's stock target
 // 19B6B78 cracks on switchWeapon.state==2 and otherwise queues UNEQUIP.
 // Reuse the existing edge/inhibit-checked crack path instead. CRACK_END
 // returns to IDLE using the same state bookkeeping as Motor transitions.
 uintptr_t whip{};const auto route=ownedWhipIdle(r.rdi,whip);
 if(route==hands::WhipIdleRoute::Native)return;
 if(route==hands::WhipIdleRoute::FinishStroke){
  const unsigned char previous=8,idle=31;const uint64_t time=r.rbx,timeout=0;SIZE_T written{};
  if(!WriteProcessMemory(GetCurrentProcess(),(void*)(whip+0x2e0),&previous,1,&written)||
     !WriteProcessMemory(GetCurrentProcess(),(void*)(whip+0x2d0),&time,8,&written)||
     !WriteProcessMemory(GetCurrentProcess(),(void*)(whip+0x2d8),&timeout,8,&written)||
     !WriteProcessMemory(GetCurrentProcess(),(void*)(whip+0x3d6),&idle,1,&written))return;
 }
 uint64_t target=UINT64_MAX;int hitGroup{};unsigned char visible{},inhibited=1;
 const bool objectStroke=whipRead(whip+0x40,target)&&whipRead(whip+0x48,hitGroup)&&
  whipRead(whip+0x148,visible)&&whipRead(r.rbp+0x1a0,inhibited)&&
  hands::whipObjectStroke((r.r15&255)!=0,inhibited!=0,target,hitGroup,visible!=0);
 // Native IDLE's staged entry passes true to StartCrack (state47), whereas
 // the ordinary repeat-stroke entry passes false (state4). The latter alone
 // never entered the attachment sequence at the user's object. Keep its
 // edge/inhibit guards, then let the native staged action handle this target.
 r.rcx=build::rva(objectStroke?0x19b6b8e:0x19b7da4);
 if(objectStroke)circlevr::vkcore::logf("motor-whip-object: physical edge routed to native staged stroke target %llx group %d visible %u; attachment remains native",target,hitGroup,visible);
 static uint64_t last{};const auto now=GetTickCount64();
 if(route==hands::WhipIdleRoute::FinishStroke||(r.r15&255)||now-last>2000){last=now;
  circlevr::vkcore::logf("motor-whip-owned-idle: route %u crack-edge %u; native auto-stow bypassed, slot18 retained",unsigned(route),unsigned(r.r15&255));}
}
void drawWhipWithoutStage(safetyhook::Context& r){
 // Accepted normal-Indiana whip selection. The stock path explicitly writes
 // EQUIP_TYPE_STAGE (7), which triggers the initial staged crack animation.
 // For our separate draw request only, use reflected EQUIP_TYPE_DEFAULT (0).
 // Do not alter selection rejection, the native event queue or QTE requests.
 uintptr_t loadout{},cmd{};uint64_t buttons{},previous{};unsigned char slot{},type{};
 if(!whipRead(r.rdi+0x38,loadout)||!loadout||!whipRead(r.rdi+0xb8,cmd)||!cmd||
    !whipRead(cmd,buttons)||!whipRead(cmd+0x30,previous)||
    !whipRead(r.rsp+0x24,slot)||!whipRead(r.rsp+0x25,type)||slot!=18||type!=7)return;
 const auto out=hands::whipInput(hands::latest(),latestNative(),loadout);
 if(!out.held||!(out.actions&hands::whipSelect)||!(buttons&hands::whipSelect)||(previous&hands::whipSelect))return;
 const unsigned char normal=0;SIZE_T written{};
 if(WriteProcessMemory(GetCurrentProcess(),(void*)(r.rsp+0x25),&normal,1,&written)&&written==1)
  circlevr::vkcore::logf("motor-whip-equip: accepted slot18 DEFAULT draw instead of STAGE attack");
}
void addWhipActions(safetyhook::Context& r){
 // idUsercmdGenLocal::BuildCurrentUsercmd, after native binding resolution
 // and before the 64-byte action array is packed with BTS into buttons.
 // R12 is the input device index; RBX owns the generator; bindset0 is on-foot.
 if(!inputEnabled()||r.r12!=0)return;
 int bind=-1;SIZE_T got{};
 if(!ReadProcessMemory(GetCurrentProcess(),(void*)(r.rbx+0x13a8),&bind,sizeof(bind),&got)||got!=sizeof(bind)||(bind!=0&&bind!=1))return;
 DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
 if(foreground!=GetCurrentProcessId())return;
 input::Result vr;{std::lock_guard lock(mutex);if(profileTime&&GetTickCount64()-profileTime<100)vr=profileResult;}
 const auto snapshot=hands::latest();
 if(snapshot.focused&&(vr.context==input::Context::Journal||vr.context==input::Context::Map)){
  // LB/RB are tab buttons AND gameplay actions in the stock bindset. Retain
  // native tab timing but remove their whip/block/zoom aliases. Zoom has its
  // own stick route, independent of tab changes and camera-look axes.
  for(unsigned action:{1u,6u,7u,13u,15u,16u,40u}){
   const unsigned char down=(input::journalActions(vr)&(uint64_t(1)<<action))!=0;
   WriteProcessMemory(GetCurrentProcess(),(void*)(r.rbx+0x135c+action),&down,1,&got);
  }
  return;
 }
 if(bind!=0)return;
 if(vr.context==input::Context::Blow){
  const auto actions=snapshot.focused&&blowInteractionActive()?input::blowActions(vr):0;
  for(unsigned action:{0u,15u})if(actions&(uint64_t(1)<<action)){
   const unsigned char down=1;
   WriteProcessMemory(GetCurrentProcess(),(void*)(r.rbx+0x135c+action),&down,1,&got);
  }
  static uint64_t previous{};
  if(actions!=previous){previous=actions;circlevr::vkcore::logf("motor-blow-input: both triggers %d native actions %llx; air-effect owns timing",actions!=0,actions);}
  return;
 }
 if(snapshot.focused&&!vr.modifier&&!vr.pause&&input::gameplay(vr.context)&&
    (vr.carryAction==input::CarryAction::ThrowPrime||vr.carryAction==input::CarryAction::ThrowRelease)){
  // Native throw preparation (40) and release (14), without LT/RT's
  // attack/aim/reload aliases. The native thrower retains eligibility/timing.
  for(unsigned action:{0u,5u,14u,15u,40u}){
   const unsigned char down=(vr.carryAction==input::CarryAction::ThrowPrime? action==40:action==14);
   WriteProcessMemory(GetCurrentProcess(),(void*)(r.rbx+0x135c+action),&down,1,&got);
  }
  return;
 }
 // Native LT also binds attack2/throw. VR aim must set only _aim; physical
 // attacks set only their own hand's action, never the overloaded triggers.
 // Reassert held two-grip _block after native binding/inhibit resolution.
 // Motor's input gathering already emits held block event36 when eligible;
 // preserve its previous-button history and timed-parry event35 unchanged.
 if(snapshot.focused&&!vr.modifier&&!vr.pause&&!vr.holster&&(input::gameplay(vr.context)||vr.context==input::Context::SeatedFirearm)){
  const auto actions=input::combatActions(vr);
  for(unsigned action:{0u,1u,5u,15u,20u})if(actions&(uint64_t(1)<<action)){
   const unsigned char down=1;WriteProcessMemory(GetCurrentProcess(),(void*)(r.rbx+0x135c+action),&down,1,&got);
  }
  static bool wasGuard=false;const bool guard=(actions&2)!=0;
  if(guard!=wasGuard){wasGuard=guard;circlevr::vkcore::logf("motor-vr-held-guard: %s native action1 after binding resolution",guard?"held":"released");}
  static unsigned old{};if(old!=vr.physicalAttacks){old=vr.physicalAttacks;circlevr::vkcore::logf("motor-vr-melee-actions: hands %u aim %d context %s; no trigger/throw aliases",old,vr.aim,input::name(vr.context));}
 }
 auto out=(vr.holster||vr.push||vr.reload)?hands::WhipOutput{}:hands::whipInput(snapshot,latestNative());
 // A yank must not also enqueue the free-whip velocity crack.
 if((vr.context==input::Context::Whip||vr.context==input::Context::WhipAttached)&&
    (vr.pad.buttons&(input::LB|input::RB))==(input::LB|input::RB))out.actions&=~hands::whipCrack;
 for(unsigned action:{13u,25u,27u})if(out.actions&(uint64_t(1)<<action)){
  const unsigned char down=1;WriteProcessMemory(GetCurrentProcess(),(void*)(r.rbx+0x135c+action),&down,1,&got);
 }
 static uint64_t last{};if(last!=out.actions){last=out.actions;circlevr::vkcore::logf("motor-whip-native-input: actions %llx bindset %d device %llu",out.actions,bind,r.r12);}
}
void gateMouse(safetyhook::Context& r){
 // Native in_mouse conditional consumers, not an OS hook or saved cvar edit.
 const bool suppress=enabled()&&sequential::projectionAllowed()&&!sequential::menuActive();
 if(suppress)r.rflags|=uintptr_t(0x40);
 static std::atomic<int> previous{-1};int old=previous.exchange(suppress?1:0);
 if(old!=(suppress?1:0))circlevr::vkcore::logf("motor-vr-mouse: native mouse %s",suppress?"suppressed for VR gameplay":"available for menus");
}
}
bool laserReadyInput(){
 std::lock_guard lock(mutex);const auto& r=profileResult;const auto now=GetTickCount64();
 return inputEnabled()&&profileTime&&now>=profileTime&&now-profileTime<100&&input::firearmContext(r.context)&&(r.motion||r.context==input::Context::SeatedFirearm)&&r.aim&&!r.firearmMelee&&!r.reload&&!r.holster&&!r.push&&!r.pause&&!r.modifier&&!r.physicalAttacks&&!(r.pad.buttons&input::X);
}
bool passengerCombatActive(uint64_t owner){
 const auto n=latestNative();const auto now=GetTickCount64();
 if(!owner||!n.valid||n.entity!=owner||now<n.tick||now-n.tick>=100)return false;
 const bool seated=nativeScriptedSeat(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace);
 const bool firearm=hands::equippedFirearm(owner);const auto life=hands::combatLifeGeneration();
 static std::mutex episodeMutex;static PassengerCombatEpisode episode;
 std::lock_guard lock(episodeMutex);
 return episode.update(owner,life,sequential::projectionAllowed()&&!sequential::menuActive(),seated,firearm);
}
std::array<float,2> fingerGestures(){
 std::lock_guard lock(mutex);
 return inputEnabled()&&profileTime&&GetTickCount64()-profileTime<100&&profileResult.context==input::Context::Unarmed?
  profileResult.fingerGesture:std::array<float,2>{};
}
bool waterPhysicsBasis(uint64_t owner,unsigned mode,WaterBasis& basis){
 const auto now=GetTickCount64();const auto snapshot=hands::latest();
 std::lock_guard lock(mutex);
 if(!inputEnabled()||!waterMode(mode)||owner!=waterOwner||!waterDirection.valid||now<waterTick||now-waterTick>100||
    !snapshot.focused||!snapshot.hands[0].tracked||now-snapshot.tick>100||!profileResult.motion||
    !profileResult.waterMotion||now-profileTime>100||
    std::hypot(float(realPad.sThumbLX),float(realPad.sThumbLY))>=4915||!sequential::projectionAllowed()||sequential::menuActive())return false;
 basis=waterDirection;return true;
}
void observeNativeFoodReady(safetyhook::Context& r){
 // idDigestibleInputActionGatheringJob, after equipped-entity resolution,
 // PickupItem/Digestible lookup, before native health/stamina eligibility.
 // PickupItem data is resolved; Digestible is a required presence-only query.
 // RDI still owns the validated satchel declaration; no game state is written.
 hands::observeDigestible(*reinterpret_cast<const uintptr_t*>(r.rsp+0x78),r.rdi);
}
void installInput(){
 if(!inputEnabled())return;
 prompts::install();
 auto base=(unsigned char*)GetModuleHandleW(nullptr);
 constexpr unsigned char foodReady[]={0x83,0xf9,0x01,0x75,0x41,0x48,0x8b,0x55,0x98};
 if(build::matches(0x130ab9f,foodReady,sizeof(foodReady))){
  auto food=safetyhook::MidHook::create(base+build::rva(0x130ab9f),observeNativeFoodReady);
  if(food){new safetyhook::MidHook(std::move(*food));circlevr::vkcore::logf("motor-mouth-use: native food presence observer installed; immediate mouth entry, withdraw to repeat; RT retained");}
 }
 constexpr unsigned char itemAccepted[]={0x84,0xc0,0x0f,0x84};
 constexpr unsigned char itemFrame[]={0x48,0x8b,0x4d,0xf8};
 if(build::matches(0x148cd17,itemAccepted,sizeof(itemAccepted))&&build::matches(0x148b79e,itemFrame,sizeof(itemFrame))){
  auto accepted=safetyhook::MidHook::create(base+build::rva(0x148cd17),auditAcceptedItemAttack);
  auto frame=safetyhook::MidHook::create(base+build::rva(0x148b79e),auditItemAttackFrame);
  if(accepted&&frame){new safetyhook::MidHook(std::move(*accepted));new safetyhook::MidHook(std::move(*frame));
   circlevr::vkcore::logf("motor-item-attack-timing: physical strike timing %d; animation observer installed",fastPhysicalMelee());}
 }
 constexpr unsigned char hitEvent[]={0x83,0xbe,0x9c,0x01,0,0,0};
 if(build::matches(0x10e214a,hitEvent,sizeof(hitEvent))){
  auto hit=safetyhook::MidHook::create(base+build::rva(0x10e214a),auditPhysicalHitEvent);
  if(hit){new safetyhook::MidHook(std::move(*hit));circlevr::vkcore::logf("motor-melee-hit-event: native event observer installed");}
 }

 // Accepted native charge, precharge and precharge->charge timestamp writes.
 // Native stamina, attack availability, hand selection and damage are retained.
 constexpr unsigned char chargeStart[]={0x48,0x89,0x81,0xb0,0,0,0,0x48,0x8b,0xcf};
 constexpr unsigned char chargePre[]={0x48,0x89,0x81,0xb0,0,0,0,0x48,0x8b,0x47,0x28};
 constexpr unsigned char chargeAccepted[]={0x84,0xc0,0x0f,0x85};
 constexpr unsigned char preAccepted[]={0x41,0x0f,0xb6,0xc4,0xe9};
 constexpr unsigned char chargedRelease[]={0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x08};
 constexpr unsigned char ordinaryRelease[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10};
 if(build::matches(0x1494350,chargeStart,sizeof(chargeStart))&&
    build::matches(0x148f55f,chargePre,sizeof(chargePre))&&build::matches(0x148f82d,chargePre,sizeof(chargePre))&&
    build::matches(0x148f484,chargeAccepted,sizeof(chargeAccepted))&&build::matches(0x148f88e,preAccepted,sizeof(preAccepted))&&
    build::matches(0x1494420,chargedRelease,sizeof(chargedRelease))&&build::matches(0x1494ad0,ordinaryRelease,sizeof(ordinaryRelease))){
  auto start=safetyhook::MidHook::create(base+build::rva(0x1494350),creditPunchStart);
  auto pre=safetyhook::MidHook::create(base+build::rva(0x148f55f),creditPunchPrecharge);
  auto transition=safetyhook::MidHook::create(base+build::rva(0x148f82d),creditPunchTransition);
  auto accepted=safetyhook::MidHook::create(base+build::rva(0x148f484),finishAcceptedPunch);
  auto preAcceptedHook=safetyhook::MidHook::create(base+build::rva(0x148f88e),finishPhysicalCharge);
  if(start&&pre&&transition&&accepted&&preAcceptedHook){new safetyhook::MidHook(std::move(*start));new safetyhook::MidHook(std::move(*pre));new safetyhook::MidHook(std::move(*transition));
   new safetyhook::MidHook(std::move(*accepted));new safetyhook::MidHook(std::move(*preAcceptedHook));
   hands::enablePunchCharge();circlevr::vkcore::logf("motor-punch-charge: native timing and accepted charged-release hooks installed; physical strike dispatches attack; tracked arms retained");}
 }
 constexpr unsigned char whipBytes[]={0x48,0x8b,0x7c,0x24,0x50,0x41,0x8b,0xd7};
 constexpr unsigned char whipSelectBytes[]={0x80,0xf9,0x01,0x74,0x2d,0x48,0x8b,0x47,0x48};
 constexpr unsigned char whipHoldBytes[]={0x48,0x8b,0xc1,0x44,0x88,0x8d,0x90,0x01,0,0};
 constexpr unsigned char whipDrawBytes[]={0xf2,0x0f,0x10,0x44,0x24,0x20,0xf2,0x0f,0x11,0x03};
 constexpr unsigned char whipActiveBytes[]={0x41,0x80,0xf8,0x02,0x0f,0x84};
 constexpr unsigned char whipRouteBytes[]={0x48,0x03,0xca,0xff,0xe1};
 constexpr unsigned char whipActiveReturnBytes[]={0xb0,0x01,0xeb,0x29};
 constexpr unsigned char whipSwingBytes[]={0x45,0x84,0xff,0x74,0x65};
 constexpr unsigned char whipObjectBytes[]={0x48,0x89,0x5c,0x24,0x28,0xc6,0x44,0x24,0x20,0x01};
 if(build::matches(0x668d35,whipBytes,sizeof(whipBytes))&&build::matches(0x1576959,whipSelectBytes,sizeof(whipSelectBytes))&&build::matches(0x19b6528,whipHoldBytes,sizeof(whipHoldBytes))&&build::matches(0x1576aff,whipDrawBytes,sizeof(whipDrawBytes))&&
    build::matches(0x19b254c,whipActiveBytes,sizeof(whipActiveBytes))&&build::matches(0x19b6b73,whipRouteBytes,sizeof(whipRouteBytes))&&build::matches(0x19b29c5,whipActiveReturnBytes,sizeof(whipActiveReturnBytes))&&build::matches(0x19b7da4,whipSwingBytes,sizeof(whipSwingBytes))&&build::matches(0x19b6b8e,whipObjectBytes,sizeof(whipObjectBytes))){
  auto whip=safetyhook::MidHook::create(base+build::rva(0x668d35),addWhipActions);
  auto select=safetyhook::MidHook::create(base+build::rva(0x1576959),selectShoulderWhip);
  auto trace=safetyhook::MidHook::create(base+build::rva(0x19b6528),traceShoulderWhip);
  auto draw=safetyhook::MidHook::create(base+build::rva(0x1576aff),drawWhipWithoutStage);
  auto active=safetyhook::MidHook::create(base+build::rva(0x19b254c),keepOwnedWhipJobActive);
  auto route=safetyhook::MidHook::create(base+build::rva(0x19b6b73),routeOwnedWhipIdle);
  if(whip&&select&&trace&&draw&&active&&route&&route->original_bytes().size()==5){new safetyhook::MidHook(std::move(*whip));new safetyhook::MidHook(std::move(*select));new safetyhook::MidHook(std::move(*trace));new safetyhook::MidHook(std::move(*draw));new safetyhook::MidHook(std::move(*active));new safetyhook::MidHook(std::move(*route));hands::enableWhipInput();circlevr::vkcore::logf("motor-shoulder-whip: validated action/equip/trace/draw/idle hooks installed; grip owns equipment only, native attack history/flags untouched; punch-equivalent forward gesture");}
 }
 // Motor's actual cursor is at global4A1DC78, XY at +C/+10, dirty +2.
 // EB6A77 runs after event polling (also idle); EB6B72 precedes UI dispatch
 // after native mouse accumulation/clamping. Both writes are game-thread only.
 constexpr unsigned char cursorPoll[]={0x8b,0x55,0xc7,0x44,0x89,0xa6,0x70};
 constexpr unsigned char cursorDispatch[]={0x48,0x8b,0x0d,0x9f,0x6e,0xb6,0x03};
 if(build::matches(0xeb6a77,cursorPoll,sizeof(cursorPoll))&&build::matches(0xeb6b72,cursorDispatch,sizeof(cursorDispatch))){
  auto poll=safetyhook::MidHook::create(base+build::rva(0xeb6a77),applyMenuCursor);
  auto dispatch=safetyhook::MidHook::create(base+build::rva(0xeb6b72),applyMenuCursor);
  if(poll&&dispatch){new safetyhook::MidHook(std::move(*poll));new safetyhook::MidHook(std::move(*dispatch));cursorInstalled=true;circlevr::vkcore::logf("motor-native-menu-cursor: validated poll/dispatch hooks installed");}
 }
 // Registration 196570 names in_mouse, wrapper 3EBC720. Gate device capture
 // and cursor recenter at their existing conditionals, preserving cvar bytes.
 constexpr unsigned char mouseDevice[]={0x74,0x1b,0x48,0x8b,0x0d};
 constexpr unsigned char mouseCenter[]={0x0f,0x84,0xa1,0,0,0};
 if(build::matches(0x729758,mouseDevice,sizeof(mouseDevice))&&build::matches(0xef9165,mouseCenter,sizeof(mouseCenter))){
  auto device=safetyhook::MidHook::create(base+build::rva(0x729758),gateMouse);
  auto center=safetyhook::MidHook::create(base+build::rva(0xef9165),gateMouse);
  if(device&&center){new safetyhook::MidHook(std::move(*device));new safetyhook::MidHook(std::move(*center));circlevr::vkcore::logf("motor-vr-mouse: validated native in_mouse consumers installed");}
 }
 // Measured import slot belongs to Motor XINPUT1_4.dll. Resolve the named
 // export and compare the actual pointer; do not copy donor ordinal numbers.
 auto module=GetModuleHandleW(L"XINPUT1_4.dll");if(!module)return;
 auto named=(GetState)GetProcAddress(module,"XInputGetState");if(!named)return;
 auto slot=(void**)(base+build::rva(0x2bea2d0));
 if(*slot!=(void*)named){circlevr::vkcore::logf("motor-roomscale: XInput import mismatch; disabled");return;}
 constexpr unsigned char discoveryBytes[]={0x44,0x38,0x6b,0x01,0x74,0x60};
 if(!build::matches(0x72b5b6,discoveryBytes,sizeof(discoveryBytes)))return;
 auto discovery=safetyhook::MidHook::create(base+build::rva(0x72b5ba),discover);
 if(!discovery)return;
 DWORD protect=0;if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&protect))return;
 circlevr::vkcore::logf("motor-game-haptics: XInputSetState import bridge installed %d",haptics::installGameHaptics());
 original=named;InterlockedExchangePointer(slot,(void*)&getState);DWORD ignored=0;VirtualProtect(slot,sizeof(void*),protect,&ignored);installed=true;
 new safetyhook::MidHook(std::move(*discovery));
 circlevr::vkcore::logf("motor-roomscale: Motor XINPUT1_4 GetState import bridge installed; native movement/collision, no transform writes");
}
bool consumeInstantThrow(uint64_t owner){
 const auto snapshot=hands::latest();const auto n=latestNative();const auto now=GetTickCount64();
 if(!snapshot.focused||now<snapshot.tick||now-snapshot.tick>100||!n.valid||n.entity!=owner||now<n.tick||now-n.tick>100)return false;
 std::lock_guard lock(mutex);static input::ThrowOnce once;
 if(!profileTime||now<profileTime||now-profileTime>100||profileResult.carryAction!=input::CarryAction::ThrowPrime||
    (profileResult.context!=input::Context::Carrying&&profileResult.context!=input::Context::Melee))return false;
 return once.consume(snapshot.session,owner,profileResult.throwToken,now);
}
bool menuCursorTarget(int x,int y,bool active){
 std::lock_guard lock(cursorMutex);cursorEnabled=active&&cursorInstalled&&x>=0&&y>=0&&x<16384&&y<16384;
 cursorX=x;cursorY=y;cursorTick=GetTickCount64();return cursorEnabled;
}
bool nativeMenuCursorActive(){
 if(!cursorInstalled||!build::supported())return false;
 const auto base=(uintptr_t)GetModuleHandleW(nullptr);uintptr_t cursor{};SIZE_T got{};
 unsigned char active{};
 return ReadProcessMemory(GetCurrentProcess(),(void*)(base+build::rva(0x4a1dc78)),&cursor,sizeof(cursor),&got)&&
  got==sizeof(cursor)&&cursor&&ReadProcessMemory(GetCurrentProcess(),(void*)cursor,&active,1,&got)&&got==1&&active==1;
}
bool combatCameraActive(uint64_t owner,uint64_t now){
 std::lock_guard lock(combatCameraMutex);return combatCamera.active(owner,now);
}
void resetCameraAnchor(){std::lock_guard lock(mutex);cameraAnchor.suspend();stairHeight={};roomscaleView={};
 // Re-enter with one shared positional reference, not an outstanding command
 // from before the menu/traversal. Heading and trusted body dimensions survive.
 follow.valid=false;follow.commanded=false;follow.following=false;follow.acceptedStep={};
 followSubmitted=false;pending={};nativeTime=0;ready=false;
}
CameraMotion update(const experimental::EyeRig& rig,const std::array<float,12>& camera,bool recenter,bool calibrate){
 const auto native=latestNative();const auto now=GetTickCount64();
 bool combatMotion{};{std::lock_guard lock(combatCameraMutex);combatMotion=combatCamera.active(native.entity,now);}
 const bool combatView=combatMotion&&nativeCombatCamera(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace);
 const auto turnTime=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
 std::lock_guard lock(mutex);
 CameraMotion out;
 std::array<float,3> bodyOrigin{};
 const bool visualBody=sequential::playerCameraOrigin(native.entity,{camera[0],camera[1],camera[2]},bodyOrigin);
 const Vec viewPhysics=visualBody?Vec{bodyOrigin[0],bodyOrigin[1],bodyOrigin[2]}:native.position;
 static bool previousVisualBody{};static uint64_t visualBodyLog{};
 if(previousVisualBody!=visualBody||(visualBody&&now-visualBodyLog>=2000)){
  previousVisualBody=visualBody;visualBodyLog=now;
  circlevr::vkcore::logf("motor-render-body-reference: active %d owner %llx physics %.4f %.4f %.4f body %.4f %.4f %.4f camera %.4f %.4f %.4f; native eye retained, ordinary turn/IK ownership",
   visualBody,native.entity,native.position.x,native.position.y,native.position.z,viewPhysics.x,viewPhysics.y,viewPhysics.z,camera[0],camera[1],camera[2]);
 }
 // This runs once for the shared stereo pair, before command/view updates.
 const Vec observedHead{(rig.positions[0][0]+rig.positions[1][0])*.5f,(rig.positions[0][1]+rig.positions[1][1])*.5f,(rig.positions[0][2]+rig.positions[1][2])*.5f};
 const bool freshCamera=native.valid&&now>=native.tick&&now-native.tick<100&&rig.valid&&rig.tracked&&
  hands::latest().focused&&diagnostics::cameraStabilization().load();
 const bool ordinaryCamera=trackedGameplayState(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace)&&!combatMotion;
 const bool manualCamera=std::hypot(float(realPad.sThumbLX),float(realPad.sThumbLY))>=4915||
  !profileTime||now<profileTime||now-profileTime>=100||!profileResult.motion||std::hypot(profileResult.pad.lx,profileResult.pad.ly)>.15f;
 const bool comparableReference=follow.valid&&follow.owner==native.entity&&roomscaleView.owner==native.entity&&
  roomscaleView.epoch==epoch&&roomscaleView.tick&&now>=roomscaleView.tick&&now-roomscaleView.tick<100&&!manualCamera;
 const float referenceError=comparableReference?cameraReferenceError(follow,roomscaleView):0.f;
 const auto repair=cameraWatchdog.update(native.entity,epoch,now,freshCamera,ordinaryCamera,!ordinaryCamera,comparableReference,referenceError);
 if(repair!=CameraRepair::None){
  cameraAnchor.suspend();stairHeight={};roomscaleView={};
  follow.reset(native.entity,native.position,observedHead);followSubmitted=false;pending={};nativeTime=0;ready=false;
  circlevr::vkcore::logf("motor-camera-watchdog: positional reset reason %s owner %llx epoch %llu mode %u type %d referenceError %.4f; yaw and trusted height retained",
   repair==CameraRepair::NativeReturn?"native-return":"reference-mismatch",native.entity,epoch,native.movementMode,native.movementType,referenceError);
 }
 Vec acknowledgedFollow{}; // consumed exactly once by this view update
 const auto finish=[&] {
  out.physics=viewPhysics;
  out.armedPassenger=passengerCombatActive(native.entity);
  // Shared stick/reference turn, excluding physical head freelook. A moving
  // boat may change native yaw; compensate in the same reference as the hands.
  out.passengerBodyYaw=out.armedPassenger?-out.referenceCompensation:0.f;
  const bool allowed=out.referenceValid&&rig.valid&&rig.tracked&&hands::latest().focused&&
   diagnostics::cameraStabilization().load()&&
   (trackedGameplayState(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace)||
    nativeWhipAttached(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace)||
    (combatView&&cameraAnchor.calibrationLocked));
  const auto oldRepairs=cameraAnchor.repairs;
  const bool learn=calibrate&&!combatMotion&&allowed&&native.valid&&now>=native.tick&&now-native.tick<100&&native.ground&&native.movementType==0;
  out.cameraAnchor=cameraAnchor.update(native.valid?native.entity:0,out.epoch,{camera[0],camera[1],camera[2]},viewPhysics,native.angles.y,allowed,true,native.movementMode,now,learn);
  out.gravityAligned=native.valid&&now>=native.tick&&now-native.tick<100&&rig.valid&&rig.tracked&&
   hands::latest().focused&&std::isfinite(native.angles.y)&&
   (combatView||headsetViewState(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace));
  const float stair=stairHeight.update(native.entity,out.epoch,native.position,native.movementMode,native.ground,
   allowed&&out.cameraAnchor.active&&nativeControlledPosture(native.movementMode),now);
  // One snapshot feeds both eyes and the same camera-relative hand/item path.
  if(out.cameraAnchor.active){out.cameraAnchor.visualHeightOffset=.10f+stair;out.cameraAnchor.stairHeightOffset=stair;}
  const Vec renderHead{(rig.positions[0][0]+rig.positions[1][0])*.5f,(rig.positions[0][1]+rig.positions[1][1])*.5f,(rig.positions[0][2]+rig.positions[1][2])*.5f};
  const float body=out.nativeYaw*radians;
  const GroundFrame viewFrame{{std::cos(body),std::sin(body),0},{std::sin(body),-std::cos(body),0},{0,0,1},1,true};
  const bool manualView=std::hypot(float(realPad.sThumbLX),float(realPad.sThumbLY))>=4915||
   (profileTime&&now>=profileTime&&now-profileTime<100&&std::hypot(profileResult.pad.lx,profileResult.pad.ly)>.15f)||
   nativeMovementIsManual(native.command[8]||native.command[9],now,lastRoomscaleInput)||!native.ground;
  const bool bodyMotion=native.ground&&native.walking&&
   (std::abs(pendingTurn)>.001f||follow.commanded||follow.following||
    (lastRoomscaleInput&&now>=lastRoomscaleInput&&now-lastRoomscaleInput<=150));
  out.cameraAnchor.trackingOffset=roomscaleView.update(native.entity,out.epoch,renderHead,native.position,viewFrame,out.referenceYaw,
   allowed&&out.active&&out.cameraAnchor.active&&enabled()&&settings().roomscale,now,acknowledgedFollow);
  // A disabled native motion route already rebases/suspends RoomscaleFollow.
  // Suspend its view counterpart too; never keep an older camera-only origin
  // through scripted control, modifier, focus or input-ownership handoffs.
  static bool viewOwned{};
  const bool ownsView=roomscaleView.owner!=0;
  if(viewOwned!=ownsView){viewOwned=ownsView;
   circlevr::vkcore::logf("motor-roomscale-reference: %s owner %llx epoch %llu motion %d anchor %d HMD %.5f %.5f; camera and command feedback share ownership",ownsView?"acquired":"suspended",native.entity,out.epoch,out.active,out.cameraAnchor.active,renderHead.x,renderHead.z);}
  static bool lastCombatMotion{};
  if(combatMotion!=lastCombatMotion){lastCombatMotion=combatMotion;
   circlevr::vkcore::logf("motor-combat-camera: protected %d owner %llx epoch %llu mode %u type %d anchor %d trusted %d; native displacement retained, animation calibration blocked",combatMotion,native.entity,out.epoch,native.movementMode,native.movementType,out.cameraAnchor.active,cameraAnchor.calibrationLocked);}
  static uint64_t viewLog{};
  if(length(out.cameraAnchor.trackingOffset)>.005f&&now-viewLog>=500){viewLog=now;
   circlevr::vkcore::logf("motor-headset-position: offset %.5f %.5f manual %d bodyFollow %d combat %d epoch %llu; raw %.5f %.5f HMD %.5f %.5f origin %.5f %.5f ack %.5f %.5f; tracking-space reference, acknowledged translation only",out.cameraAnchor.trackingOffset.x,out.cameraAnchor.trackingOffset.y,manualView,bodyMotion,combatMotion,out.epoch,roomscaleView.rawOffset.x,roomscaleView.rawOffset.y,renderHead.x,renderHead.z,roomscaleView.trackingOrigin.x,roomscaleView.trackingOrigin.z,acknowledgedFollow.x,acknowledgedFollow.z);}
  if(cameraAnchor.repairs>oldRepairs)circlevr::vkcore::logf("motor-camera-anchor: repaired settled native ego offset owner %llx mode %u local %.4f %.4f height %.4f",native.entity,native.movementMode,out.cameraAnchor.localOffset.x,out.cameraAnchor.localOffset.y,out.cameraAnchor.eyeHeight);
  static uint64_t lastLog{};
  if(out.cameraAnchor.active&&now-lastLog>1000){lastLog=now;
   const auto p=out.cameraAnchor.place({camera[0],camera[1],camera[2]},native.position,native.angles.y);
   circlevr::vkcore::logf("motor-camera-anchor: owner %llx mode %u physics %.5f %.5f %.5f removed XYZ %.5f %.5f %.5f heightLock %d height %.4f trusted %d learn %d grounded %d gravity %d nativeUp %.4f %.4f %.4f; roomscale %.3f %.3f",native.entity,native.movementMode,native.position.x,native.position.y,native.position.z,camera[0]-p.x,camera[1]-p.y,camera[2]-p.z,out.cameraAnchor.stableHeight,out.cameraAnchor.eyeHeight,cameraAnchor.calibrationLocked,learn,native.ground,out.gravityAligned,camera[9],camera[10],camera[11],pending.x,pending.y);
  }
  return out;
 };
 const bool water=nativeWaterState(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace);
 waterDirection={};waterOwner=0;waterTick=0;
 out.referenceValid=bodyFollow.valid&&follow.owner==native.entity&&native.valid&&now-native.tick<100&&
  (combatView||headsetViewState(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace));
 static bool lowLedgeView{};
 const bool retainLedgeView=out.referenceValid&&nativeLowLedgeTraversal(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace);
 if(retainLedgeView!=lowLedgeView){lowLedgeView=retainLedgeView;circlevr::vkcore::logf("motor-low-ledge-view: active %d owner %llx mode %u type %d; headset orientation retained, native traversal translation",retainLedgeView,native.entity,native.movementMode,native.movementType);}
 out.nativeYaw=native.angles.y;
 // The view anchor is independent of whether ground locomotion can currently
 // issue a command. Account for every native body turn, including in flight.
 if(out.referenceValid)bodyFollow.acceptedPhysicalYaw=wrap(native.angles.y-referenceBodyYaw);
 out.referenceCompensation=bodyFollow.acceptedPhysicalYaw-bodyFollow.artificialYaw;
 out.referenceYaw=referenceYaw+out.referenceCompensation;out.epoch=epoch;
 Vec head{(rig.positions[0][0]+rig.positions[1][0])*.5f,(rig.positions[0][1]+rig.positions[1][1])*.5f,(rig.positions[0][2]+rig.positions[1][2])*.5f};
 const Vec difference=Vec{camera[0],camera[1],camera[2]}-viewPhysics;
 const bool passenger=nativeScriptedSeat(native.movementMode,native.movementType,native.explicitReason,native.animated,native.localSpace);
 static PassengerEntry passengerEntry;
 const bool seatTracking=inputEnabled()&&installed&&rig.valid&&rig.tracked&&hands::latest().focused;
 const bool seatEntered=passengerEntry.update(native.entity,
  native.valid&&now>=native.tick&&now-native.tick<100&&sequential::projectionAllowed()&&!sequential::menuActive(),
  passenger,seatTracking);
 const bool automaticSeatRecenter=seatEntered&&!recenter;
 if(seatEntered){recenter=true;out.passengerEntryRecenter=true;}
 // A seated actor has scripted physics and no ground contact. It cannot
 // reach the ordinary locomotion recenter below. Rebase the same yaw state
 // consumed by rendering, stick turning and interaction, without moving it.
 if(recenter&&passenger&&inputEnabled()&&installed&&rig.valid&&rig.tracked&&
    hands::latest().focused&&native.valid&&now>=native.tick&&now-native.tick<100){
  referenceHead=head;referenceYaw=yaw(rig.orientations[0]);referenceBodyYaw=native.angles.y;
  follow.reset(native.entity,native.position,head);nativeTime=0;
  // Automatic passenger alignment changes yaw, not the actor's dimensions.
  if(automaticSeatRecenter)transferCameraCalibration(cameraAnchor,native.entity,epoch,epoch+1);
  ++epoch;
  bodyFollow.reset(native.angles.y);bodyFollow.sign=-1;
  previousYaw=native.angles.y;previousYawTick=turnTime;
  ready=false;pending={};pendingTurn=0;roomscaleView={};
  out.referenceValid=true;out.referenceCompensation=0;out.referenceYaw=referenceYaw;out.epoch=epoch;
  circlevr::vkcore::logf("motor-passenger-recenter: owner %llx epoch %llu nativeYaw %.3f headYaw %.3f; seat position retained, shared view/interaction reference",native.entity,epoch,native.angles.y,referenceYaw);
  if(seatEntered)circlevr::vkcore::logf("motor-passenger-entry: owner %llx epoch %llu; initial tracked forward aligned with native seat, previous artificial yaw discarded",native.entity,epoch);
 }
 bool valid=inputEnabled()&&installed&&rig.valid&&rig.tracked&&native.valid&&now-native.tick<100&&
  native.movementType==0&&native.explicitReason==0&&!native.animated&&!native.localSpace&&
  (water||(native.walking&&native.ground&&(nativeCameraPosture(native.movementMode,std::hypot(difference.x,difference.y),difference.z)||cameraAnchor.retains(native.entity,epoch,{camera[0],camera[1],camera[2]},viewPhysics,native.movementMode))));
 // Scripted hanging owns the body, but must not own VR yaw. Only route
 // recognized traversal contexts; never inject a native stick/body command.
 static bool traversalTurning{};
 const bool wasTraversalTurning=traversalTurning;
 const auto traversalInput=hands::latest();
 const input::Native traversalNative{true,false,true,native.animated,native.localSpace,native.movementMode,hands::weaponSlot(native.entity),native.movementType,native.explicitReason,-1,hands::equippedFirearm(native.entity),native.whipPrompt};
 const auto traversalContext=input::context(traversalNative);
 traversalTurning=inputEnabled()&&installed&&out.referenceValid&&rig.valid&&rig.tracked&&
  input::viewTurnContext(traversalContext)&&profileResult.context==traversalContext&&
  profileTime&&now>=profileTime&&now-profileTime<100&&
  traversalInput.focused&&now>=traversalInput.tick&&now-traversalInput.tick<100&&traversalInput.hands[1].tracked&&
  !profileResult.modifier&&!profileResult.pause&&!traversalInput.hands[0].upper&&
  sequential::projectionAllowed()&&!sequential::menuActive()&&!recenter;
 if(traversalTurning){
  if(!wasTraversalTurning)bodyFollow.suspend(true);
  ready=false;pending={};pendingTurn=0;follow.valid=false;
  bodyFollow.turnMicros(profileResult.viewTurn,turnTime,settings(),true);
  out.referenceCompensation=bodyFollow.acceptedPhysicalYaw-bodyFollow.artificialYaw;
  out.referenceYaw=referenceYaw+out.referenceCompensation;
  return finish();
 }
 if(!valid){ready=false;pending={};pendingTurn=0;follow.valid=false;bodyFollow.suspend();return finish();}
 const bool newReference=cameraReferenceChanged(bodyFollow.valid,follow.owner,native.entity);
 if(newReference&&!recenter){experimental::recenterLive();ready=false;pending={};pendingTurn=0;return finish();}
 if(recenter){
  circlevr::vkcore::logf("motor-camera-reference: recenter owner %llx epoch %llu -> %llu previousOwner %llx; position alone no longer resets calibration",native.entity,epoch,epoch+1,follow.owner);
  referenceHead=head;referenceYaw=yaw(rig.orientations[0]);referenceBodyYaw=native.angles.y;follow.reset(native.entity,native.position,head);nativeTime=0;++epoch;
  bodyFollow.reset(native.angles.y);bodyFollow.sign=-1;previousYaw=native.angles.y;previousYawTick=turnTime;
 }
 else if(!ready||(enabled()&&!follow.valid)){
  // A short tracking/menu suspension must not replace the render reference.
  // Only reanchor positional feedback; preserve accepted body/head yaw.
  follow.reset(native.entity,native.position,head);bodyFollow.suspend(wasTraversalTurning);bodyFollow.nativePrevious=native.angles.y;
  previousYaw=native.angles.y;previousYawTick=turnTime;nativeTime=0;
 }
 ready=true;published=now;
 const float body=native.angles.y*radians;
 GroundFrame frame{{std::cos(body),std::sin(body),0},{std::sin(body),-std::cos(body),0},{0,0,1},1,true};
 // Input resolution is independent of roomscale. Modifier/menu/scripted
 // routes suspend artificial motion without dropping their native commands.
 const auto handInput=hands::latest();
 const bool motionInput=handInput.focused&&!handInput.hands[0].upper&&
  profileTime&&now-profileTime<=100&&profileResult.motion;
 auto controlPad=realPad;
 if(motionInput){
  if(std::hypot(float(controlPad.sThumbLX),float(controlPad.sThumbLY))<4915&&handInput.hands[0].tracked){
   const auto v=kharvox::fullTravelMovementStick({profileResult.pad.lx,profileResult.pad.ly});controlPad.sThumbLX=SHORT(v.x*32767);controlPad.sThumbLY=SHORT(v.y*32767);
  }
  if(std::hypot(float(controlPad.sThumbRX),float(controlPad.sThumbRY))<4915&&handInput.hands[1].tracked)controlPad.sThumbRX=SHORT(input::axis(profileResult.pad.rx)*32767);
 }else{pending={};pendingTurn=0;bodyFollow.suspend();follow.valid=false;return finish();}
 const bool manual=std::hypot(float(controlPad.sThumbLX),float(controlPad.sThumbLY))>4915||
  (nativeMovementIsManual(native.command[8]||native.command[9],now,lastRoomscaleInput));
 if(native.gameTime!=nativeTime){
  const auto& config=settings();
  char turnGate[4]{};bool turning=GetEnvironmentVariableA("CIRCLE_BODY_NATIVE_TURN",turnGate,4)&&turnGate[0]=='1';
  if(turning){
   // Estimate the native response from acknowledged yaw. A stall/large jump
   // never becomes a calibration sample or an artificial turn catch-up.
   float delta=wrap(native.angles.y-previousYaw);auto dt=turnTime-previousYawTick;
   constexpr float turnDeadzone=8689.f/32767.f;
   const float submittedMagnitude=std::clamp((std::abs(pendingTurn)-turnDeadzone)/(1-turnDeadzone),0.f,1.f);
   if(dt>0&&dt<100000&&submittedMagnitude>.1f&&std::abs(delta)>.01f&&std::abs(delta)<20)
    nativeTurnSpeed=.9f*nativeTurnSpeed+.1f*std::clamp(std::abs(delta)*1000000.f/float(dt)/submittedMagnitude,30.f,720.f);
   const bool physicalTurn=std::hypot(float(realPad.sThumbRX),float(realPad.sThumbRY))>=4915;
   bodyFollow.nativePrevious=native.angles.y;bodyFollow.turnMicros(physicalTurn?0.f:controlPad.sThumbRX/32767.f,turnTime,config,true);
   float requested=bodyFollow.request(wrap(yaw(rig.orientations[0])-referenceYaw),config,physicalTurn||native.command[0x26]!=0,true);
   pendingTurn=nativeTurnStick(requested,nativeTurnSpeed);
   static uint64_t auditTick{};
   if(now-auditTick>=500){auditTick=now;
    const float measured=dt>0&&dt<100000?delta*1000000.f/float(dt):0;
    const float residual=wrap(yaw(rig.orientations[0])-referenceYaw+bodyFollow.artificialYaw-bodyFollow.acceptedPhysicalYaw);
    circlevr::vkcore::logf("motor-turn-rate: native %.3f deg/s requested %.3f artificial %.3f estimatedFull %.3f output %.4f residual %.3f dt %llu nativeYaw %.3f",measured,requested*bodyFollow.sign,bodyFollow.smoothVelocity,nativeTurnSpeed,pendingTurn,residual,dt,native.angles.y);
   }
  }
  previousYaw=native.angles.y;previousYawTick=turnTime;
  float effectiveReference=referenceYaw+bodyFollow.acceptedPhysicalYaw-bodyFollow.artificialYaw;
  movementYaw=wrap(yaw(rig.orientations[0])-effectiveReference);
  const auto controllers=hands::latest();const auto& offhand=controllers.hands[0];const auto& orientation=offhand.aim.orientation;
  const float forwardX=-2*(orientation.x*orientation.z+orientation.w*orientation.y);
  const float forwardZ=-(1-2*(orientation.x*orientation.x+orientation.y*orientation.y));
  movementYaw=kharvox::resolveMovementDirectionYaw(config.direction,movementYaw,controllers.focused&&offhand.aimTracked,forwardX,forwardZ,effectiveReference,0).degrees;
  if(water||combatMotion){pending={};follow.reset(native.entity,native.position,head);}
  else {
   if(!manual)follow.useViewReference(roomscaleView,native.entity,out.epoch);
   pending=follow.update(native.entity,native.position,head,frame,effectiveReference,frame.forward,frame.right,manual,enabled(),config.roomscaleStart,config.roomscaleStop,followSubmitted);
   acknowledgedFollow=follow.acceptedStep;
  }
  followSubmitted=false;
  nativeTime=native.gameTime;
 }
 if(water){waterDirection=waterBasis(rig.orientations[0],native.angles.y,referenceYaw+bodyFollow.acceptedPhysicalYaw-bodyFollow.artificialYaw);waterOwner=native.entity;waterTick=now;}
 out.active=true;out.referenceValid=true;out.referenceCompensation=bodyFollow.acceptedPhysicalYaw-bodyFollow.artificialYaw;out.referenceYaw=referenceYaw+out.referenceCompensation;out.vertical=0;out.epoch=epoch;
 static unsigned turnCalls=0;if(debugEnabled()&&++turnCalls%120==0)circlevr::vkcore::logf("motor-body-yaw: native %.3f head %.3f accepted %.3f artificial %.3f stick %.3f sign %.0f",native.angles.y,wrap(yaw(rig.orientations[0])-referenceYaw),bodyFollow.acceptedPhysicalYaw,bodyFollow.artificialYaw,pendingTurn,bodyFollow.sign);
 static unsigned calls=0;if(debugEnabled()&&++calls%120==0)circlevr::vkcore::logf("motor-roomscale: epoch %llu owner %llx native %.4f %.4f %.4f accepted %.4f %.4f command %.3f %.3f manual %d",epoch,native.entity,native.position.x,native.position.y,native.position.z,follow.accepted.x,follow.accepted.z,pending.x,pending.y,manual);
 return finish();
}
}
