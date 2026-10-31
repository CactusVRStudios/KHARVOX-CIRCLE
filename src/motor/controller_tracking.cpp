// OpenXR lifecycle follows ARGENT XrActions.inc and CircleVR input_xr.cpp.
// No donor game action IDs, skeletal offsets or input hooks are used here.
#include "controller_tracking.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <mutex>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <algorithm>
namespace circle::motor::hands {
namespace {
std::mutex mutex;Snapshot snapshot;
uint64_t sessionGeneration{};
std::array<Snapshot,16> history{};
struct Pulse {float amplitude{};uint32_t durationMs{};uint64_t tick{};};
std::array<Pulse,2> pulses{}, activePulses{};
float gameRumble{}, gamePeak{};uint64_t gameTick{};
std::array<bool,2> gamePlaying{};
enum Action {Grip,Aim,Stick,Trigger,Squeeze,Lower,Upper,Click,Menu,Haptic,ThumbRest,Count};
struct Runtime {
 XrInstance instance{};XrSession session{};XrSpace local{};XrActionSet set{};
 std::array<XrAction,Count> actions{};std::array<XrPath,2> paths{};
 std::array<XrSpace,2> grip{},aim{};bool ready{};uint64_t sequence{};unsigned lastMask=~0u;
#define FN(n) PFN_##n n{};
 FN(xrStringToPath) FN(xrCreateActionSet) FN(xrDestroyActionSet) FN(xrCreateAction)
 FN(xrSuggestInteractionProfileBindings) FN(xrAttachSessionActionSets)
 FN(xrCreateActionSpace) FN(xrDestroySpace) FN(xrSyncActions)
 FN(xrGetActionStatePose) FN(xrGetActionStateFloat) FN(xrGetActionStateBoolean)
 FN(xrGetActionStateVector2f) FN(xrLocateSpace) FN(xrGetCurrentInteractionProfile) FN(xrPathToString)
 FN(xrApplyHapticFeedback) FN(xrStopHapticFeedback)
#undef FN
} xr;
bool check(XrResult r,const char* op){if(XR_FAILED(r)){circlevr::vkcore::logf("motor-hands: %s failed %d",op,int(r));return false;}return true;}
bool path(const char* name,XrPath& p){return check(xr.xrStringToPath(xr.instance,name,&p),name);}
bool suggest(const char* profile,bool index){
 std::vector<XrActionSuggestedBinding> bindings;
 const auto add=[&](Action a,unsigned h,const char* suffix){XrPath p{};std::string s=h?"/user/hand/right":"/user/hand/left";s+=suffix;if(!path(s.c_str(),p))return false;bindings.push_back({xr.actions[a],p});return true;};
 for(unsigned h=0;h<2;++h){
  if(!add(Grip,h,"/input/grip/pose")||!add(Aim,h,"/input/aim/pose")||!add(Stick,h,"/input/thumbstick")||
   !add(Trigger,h,"/input/trigger/value")||!add(Squeeze,h,index?"/input/squeeze/force":"/input/squeeze/value")||!add(Click,h,"/input/thumbstick/click")||
   !add(Lower,h,index||h?"/input/a/click":"/input/x/click")||!add(Upper,h,index||h?"/input/b/click":"/input/y/click")||!add(Haptic,h,"/output/haptic"))return false;
 }
 for(unsigned h=0;h<2;++h)if(!add(ThumbRest,h,index?"/input/trackpad/touch":"/input/thumbrest/touch"))return false;
 if(!index&&!add(Menu,0,"/input/menu/click"))return false;
 XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
 if(!path(profile,info.interactionProfile))return false;
 info.countSuggestedBindings=uint32_t(bindings.size());info.suggestedBindings=bindings.data();
 const bool ok=check(xr.xrSuggestInteractionProfileBindings(xr.instance,&info),profile);
 circlevr::vkcore::logf("motor-hands: profile %s bindings %zu accepted %d",profile,bindings.size(),ok);return ok;
}
bool locate(Action action,unsigned h,XrSpace space,XrTime time,XrPosef& pose,XrVector3f* velocity,bool* velocityValid=nullptr,bool* available=nullptr){
 XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};get.action=xr.actions[action];get.subactionPath=xr.paths[h];
 XrActionStatePose active{XR_TYPE_ACTION_STATE_POSE};
 if(XR_FAILED(xr.xrGetActionStatePose(xr.session,&get,&active))||!active.isActive)return false;
 if(available)*available=true;
 XrSpaceVelocity v{XR_TYPE_SPACE_VELOCITY};XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION,&v};
 constexpr auto required=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
 if(XR_FAILED(xr.xrLocateSpace(space,xr.local,time,&loc))||(loc.locationFlags&required)!=required)return false;
 const auto& p=loc.pose;float norm=p.orientation.x*p.orientation.x+p.orientation.y*p.orientation.y+p.orientation.z*p.orientation.z+p.orientation.w*p.orientation.w;
 if(!std::isfinite(p.position.x)||!std::isfinite(p.position.y)||!std::isfinite(p.position.z)||!std::isfinite(norm)||std::abs(norm-1)>.05f)return false;
 pose=p;if(velocity&&(v.velocityFlags&XR_SPACE_VELOCITY_LINEAR_VALID_BIT)&&std::isfinite(v.linearVelocity.x)&&std::isfinite(v.linearVelocity.y)&&std::isfinite(v.linearVelocity.z)){*velocity=v.linearVelocity;if(velocityValid)*velocityValid=true;}
 return true;
}
}
void detach(){
 {std::lock_guard lock(mutex);snapshot={};history={};pulses={};activePulses={};gameRumble=gamePeak=0;gameTick=0;gamePlaying={};}
 if(xr.ready&&xr.xrStopHapticFeedback)for(unsigned h=0;h<2;++h){XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};info.action=xr.actions[Haptic];info.subactionPath=xr.paths[h];xr.xrStopHapticFeedback(xr.session,&info);}
 for(auto s:xr.grip)if(s&&xr.xrDestroySpace)xr.xrDestroySpace(s);
 for(auto s:xr.aim)if(s&&xr.xrDestroySpace)xr.xrDestroySpace(s);
 if(xr.set&&xr.xrDestroyActionSet)xr.xrDestroyActionSet(xr.set);
 xr={};
}
bool attach(PFN_xrGetInstanceProcAddr gipa,XrInstance instance,XrSession session,XrSpace local,bool plus){
 detach();++sessionGeneration;xr.instance=instance;xr.session=session;xr.local=local;
#define LOAD(n) if(XR_FAILED(gipa(instance,#n,reinterpret_cast<PFN_xrVoidFunction*>(&xr.n)))||!xr.n){detach();return false;}
 LOAD(xrStringToPath) LOAD(xrCreateActionSet) LOAD(xrDestroyActionSet) LOAD(xrCreateAction)
 LOAD(xrSuggestInteractionProfileBindings) LOAD(xrAttachSessionActionSets)
 LOAD(xrCreateActionSpace) LOAD(xrDestroySpace) LOAD(xrSyncActions)
 LOAD(xrGetActionStatePose) LOAD(xrGetActionStateFloat) LOAD(xrGetActionStateBoolean)
 LOAD(xrGetActionStateVector2f) LOAD(xrLocateSpace) LOAD(xrGetCurrentInteractionProfile) LOAD(xrPathToString)
 LOAD(xrApplyHapticFeedback) LOAD(xrStopHapticFeedback)
#undef LOAD
 const auto build=[&] {
 if(!path("/user/hand/left",xr.paths[0])||!path("/user/hand/right",xr.paths[1]))return false;
 XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};strcpy_s(set.actionSetName,"circle_hands");strcpy_s(set.localizedActionSetName,"Circle Controllers");
 if(!check(xr.xrCreateActionSet(instance,&set,&xr.set),"create set"))return false;
 const char* names[]={"grip_pose","aim_pose","stick","trigger","squeeze","lower_button","upper_button","stick_click","menu","haptic","thumb_rest_touch"};
 const XrActionType types[]={XR_ACTION_TYPE_POSE_INPUT,XR_ACTION_TYPE_POSE_INPUT,XR_ACTION_TYPE_VECTOR2F_INPUT,XR_ACTION_TYPE_FLOAT_INPUT,XR_ACTION_TYPE_FLOAT_INPUT,XR_ACTION_TYPE_BOOLEAN_INPUT,XR_ACTION_TYPE_BOOLEAN_INPUT,XR_ACTION_TYPE_BOOLEAN_INPUT,XR_ACTION_TYPE_BOOLEAN_INPUT,XR_ACTION_TYPE_VIBRATION_OUTPUT,XR_ACTION_TYPE_BOOLEAN_INPUT};
 for(unsigned i=0;i<Count;++i){XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};strcpy_s(info.actionName,names[i]);strcpy_s(info.localizedActionName,names[i]);info.actionType=types[i];info.countSubactionPaths=2;info.subactionPaths=xr.paths.data();if(!check(xr.xrCreateAction(xr.set,&info,&xr.actions[i]),names[i]))return false;}
 bool supported=suggest("/interaction_profiles/oculus/touch_controller",false);
 supported=suggest("/interaction_profiles/valve/index_controller",true)||supported;
 if(plus)supported=suggest("/interaction_profiles/meta/touch_controller_plus",false)||supported;
 if(!supported)return false;
 XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&xr.set;
 if(!check(xr.xrAttachSessionActionSets(session,&attach),"attach"))return false;
 for(unsigned h=0;h<2;++h){XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};space.subactionPath=xr.paths[h];space.poseInActionSpace.orientation.w=1;space.action=xr.actions[Grip];if(!check(xr.xrCreateActionSpace(session,&space,&xr.grip[h]),"grip space"))return false;space.action=xr.actions[Aim];if(!check(xr.xrCreateActionSpace(session,&space,&xr.aim[h]),"aim space"))return false;}
 return true;};
 if(!build()){detach();return false;}xr.ready=true;circlevr::vkcore::logf("motor-hands: tracking-only actions attached; native input/rig untouched");return true;
}
void sync(XrTime time,bool focused){
 std::array<bool,2> available{};
 Snapshot next;next.session=sessionGeneration;next.time=time;next.tick=GetTickCount64();next.sequence=++xr.sequence;
 XrActiveActionSet active{xr.set,XR_NULL_PATH};XrActionsSyncInfo info{XR_TYPE_ACTIONS_SYNC_INFO};info.countActiveActionSets=1;info.activeActionSets=&active;
 next.focused=xr.ready&&focused&&xr.xrSyncActions(xr.session,&info)==XR_SUCCESS;
 if(next.focused)for(unsigned h=0;h<2;++h){auto& hand=next.hands[h];
  hand.tracked=locate(Grip,h,xr.grip[h],time,hand.grip,&hand.velocity,&hand.velocityValid,&available[h]);hand.aimTracked=locate(Aim,h,xr.aim[h],time,hand.aim,nullptr);
  if(!hand.tracked)continue;
  XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};get.subactionPath=xr.paths[h];
  auto scalar=[&](Action a){get.action=xr.actions[a];XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};return XR_SUCCEEDED(xr.xrGetActionStateFloat(xr.session,&get,&state))&&state.isActive&&std::isfinite(state.currentState)?state.currentState:0.f;};
  auto boolean=[&](Action a){get.action=xr.actions[a];XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};return XR_SUCCEEDED(xr.xrGetActionStateBoolean(xr.session,&get,&state))&&state.isActive&&state.currentState;};
  hand.trigger=scalar(Trigger);hand.squeeze=scalar(Squeeze);hand.lower=boolean(Lower);hand.upper=boolean(Upper);hand.click=boolean(Click);hand.menu=boolean(Menu);hand.thumbRest=boolean(ThumbRest);
  get.action=xr.actions[Stick];XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};if(XR_SUCCEEDED(xr.xrGetActionStateVector2f(xr.session,&get,&state))&&state.isActive&&std::isfinite(state.currentState.x)&&std::isfinite(state.currentState.y))hand.stick=state.currentState;
 }
 unsigned mask=next.focused?4:0;for(unsigned h=0;h<2;++h)if(next.hands[h].tracked)mask|=1<<h;
 if(mask!=xr.lastMask){circlevr::vkcore::logf("motor-hands: tracking transition focused %d left %d right %d",next.focused,next.hands[0].tracked,next.hands[1].tracked);xr.lastMask=mask;
  if(next.focused)for(unsigned h=0;h<2;++h){XrInteractionProfileState profile{XR_TYPE_INTERACTION_PROFILE_STATE};char name[XR_MAX_PATH_LENGTH]{};uint32_t count=0;
   if(XR_SUCCEEDED(xr.xrGetCurrentInteractionProfile(xr.session,xr.paths[h],&profile))&&profile.interactionProfile&&XR_SUCCEEDED(xr.xrPathToString(xr.instance,profile.interactionProfile,sizeof(name),&count,name)))circlevr::vkcore::logf("motor-hands: hand %u current profile %s",h,name);
  }
 }
 if(next.focused&&next.sequence%180==0)for(unsigned h=0;h<2;++h){const auto& v=next.hands[h];circlevr::vkcore::logf("motor-hands: sample %llu hand %u tracked %d pos %.3f %.3f %.3f trigger %.2f grip %.2f stick %.2f %.2f buttons %d%d%d%d",next.sequence,h,v.tracked,v.grip.position.x,v.grip.position.y,v.grip.position.z,v.trigger,v.squeeze,v.stick.x,v.stick.y,v.lower,v.upper,v.click,v.menu);}
 std::array<Pulse,2> pending{};float rumble=0;
 {std::lock_guard lock(mutex);snapshot=next;history[next.sequence%history.size()]=next;pending=pulses;pulses={};
  if(!next.focused){gameRumble=gamePeak=0;gameTick=0;}
  if(gameTick&&next.tick>=gameTick&&next.tick-gameTick<=2000)rumble=std::max(gameRumble,gamePeak);
  gamePeak=0;
 }
 if(xr.ready)for(unsigned h=0;h<2;++h){
  XrHapticActionInfo haptic{XR_TYPE_HAPTIC_ACTION_INFO};haptic.action=xr.actions[Haptic];haptic.subactionPath=xr.paths[h];
  if(!next.focused||!available[h]){xr.xrStopHapticFeedback(xr.session,&haptic);activePulses[h]={};gamePlaying[h]=false;continue;}
  const auto& p=pending[h];const bool fresh=p.tick&&next.tick>=p.tick&&next.tick-p.tick<=100;
  auto& event=activePulses[h];
  if(fresh)event=p;
  const auto remaining=event.tick&&next.tick>=event.tick&&next.tick-event.tick<event.durationMs?event.durationMs-uint32_t(next.tick-event.tick):0;
  if(!fresh&&!rumble&&!gamePlaying[h])continue;
  const float amplitude=std::max(rumble,remaining?event.amplitude:0.f);
  if(amplitude<=0){xr.xrStopHapticFeedback(xr.session,&haptic);gamePlaying[h]=false;continue;}
  XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};vibration.amplitude=amplitude;vibration.frequency=XR_FREQUENCY_UNSPECIFIED;
  vibration.duration=XrDuration(std::max(remaining,rumble?50u:0u))*1000000;
  const bool applied=check(xr.xrApplyHapticFeedback(xr.session,&haptic,reinterpret_cast<const XrHapticBaseHeader*>(&vibration)),"game/event haptic");
  gamePlaying[h]=applied&&rumble>0;
  static std::array<uint64_t,2> report{};
  if(next.tick-report[h]>=2000){report[h]=next.tick;
   circlevr::vkcore::logf("motor-haptic-output: hand %u available %d tracked %d amplitude %.3f duration %u applied %d",h,available[h],next.hands[h].tracked,amplitude,unsigned(vibration.duration/1000000),applied);}


 }
}
Snapshot latest(){std::lock_guard lock(mutex);if(!snapshot.tick||GetTickCount64()-snapshot.tick>100)return {};return snapshot;}
Snapshot atTime(XrTime time){std::lock_guard lock(mutex);if(!snapshot.focused)return {};const auto now=GetTickCount64();for(const auto& s:history)if(s.time==time&&s.tick&&now-s.tick<=100){auto out=s;for(unsigned h=0;h<2;++h)if(!snapshot.hands[h].tracked)out.hands[h]={};return out;}return {};}
bool requestGameRumble(uint16_t low,uint16_t high){
 std::lock_guard lock(mutex);const auto now=GetTickCount64();
 if(!snapshot.focused||!snapshot.tick||now<snapshot.tick||now-snapshot.tick>100)return false;
 // Xbox motors encode frequency bands, not anatomical hands. Preserve native
 // intensity on both OpenXR outputs, with a bounded 60% comfort gain.
 gameRumble=.6f*float(std::max(low,high))/65535.f;
 gamePeak=std::max(gamePeak,gameRumble);gameTick=now;
 static uint64_t report{};if((low||high)&&now-report>2000){report=now;circlevr::vkcore::logf("motor-game-haptics: native motors %u/%u forwarded to both XR hands",low,high);}
 return true;
}
bool requestHaptic(unsigned hand,float amplitude,uint32_t durationMs){
 if(hand>=2||!std::isfinite(amplitude)||amplitude<=0||!durationMs)return false;
 std::lock_guard lock(mutex);const auto now=GetTickCount64();
 if(!snapshot.focused||!snapshot.hands[hand].tracked||!snapshot.tick||now-snapshot.tick>100)return false;
 auto& p=pulses[hand];p.amplitude=std::max(p.amplitude,std::min(amplitude,1.f));p.durationMs=std::max(p.durationMs,std::min(durationMs,200u));p.tick=now;return true;
}
}
