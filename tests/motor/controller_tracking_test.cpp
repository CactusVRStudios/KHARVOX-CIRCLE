#include "controller_tracking.hpp"
#include "game_haptics.hpp"
#include <map>
#include <string>
#include <cstring>
#include <cstdio>
#include <windows.h>
namespace circlevr::vkcore {void logf(const char*,...) {}}
namespace {
uintptr_t next=10;std::map<std::string,XrPath> paths;std::map<XrAction,XrActionType> types;std::map<XrAction,std::string> actionNames;
int errors=0,profiles=0,spaces=0,sets=0;bool tracking=true;XrResult syncResult=XR_SUCCESS;
int restBindings=0;bool restActive=true;std::array<bool,2> restTouched{true,false};
std::array<int,2> perHandHaptics{};
int hapticCount=0,stopCount=0;XrPath hapticHand{};XrHapticVibration lastHaptic{};
void check(bool ok,const char* why){if(!ok){++errors;printf("FAIL %s\n",why);}}
XrResult XRAPI_PTR stringPath(XrInstance,const char* s,XrPath* p){if(!paths.count(s))paths[s]=++next;*p=paths[s];return XR_SUCCESS;}
XrResult XRAPI_PTR createSet(XrInstance,const XrActionSetCreateInfo*,XrActionSet* s){*s=(XrActionSet)++next;++sets;return XR_SUCCESS;}
XrResult XRAPI_PTR destroySet(XrActionSet){--sets;return XR_SUCCESS;}
XrResult XRAPI_PTR createAction(XrActionSet,const XrActionCreateInfo* i,XrAction* a){check(i->countSubactionPaths==2,"separate hand subactions");*a=(XrAction)++next;types[*a]=i->actionType;actionNames[*a]=i->actionName;return XR_SUCCESS;}
XrResult XRAPI_PTR bindings(XrInstance,const XrInteractionProfileSuggestedBinding* i){++profiles;for(unsigned n=0;n<i->countSuggestedBindings;++n){auto b=i->suggestedBindings[n];std::string name;for(auto& [s,p]:paths)if(p==b.binding)name=s;check(!name.empty(),"resolved binding");std::string profileName;for(auto& [s,p]:paths)if(p==i->interactionProfile)profileName=s;
 if(name.find("/squeeze/")!=std::string::npos)check(name.ends_with(profileName.find("valve")!=std::string::npos?"/force":"/value"),"Index force / Quest squeeze value");
 if(profileName.find("valve")!=std::string::npos)check(name.find("/x/")==std::string::npos&&name.find("/y/")==std::string::npos&&name.find("/menu/")==std::string::npos,"Index uses A/B without reserved buttons");check(name.find("system")==std::string::npos,"reserved system button absent");if(actionNames[b.action]=="lower_button")check(name.ends_with(profileName.find("valve")!=std::string::npos||name.find("/right/")!=std::string::npos?"/a/click":"/x/click"),"same logical lower role on Quest/Index");
 if(actionNames[b.action]=="upper_button")check(name.ends_with(profileName.find("valve")!=std::string::npos||name.find("/right/")!=std::string::npos?"/b/click":"/y/click"),"same logical modifier/upper role on Quest/Index");
 if(actionNames[b.action]=="thumb_rest_touch"){++restBindings;check(name.ends_with(profileName.find("valve")!=std::string::npos?"/input/trackpad/touch":"/input/thumbrest/touch")&&types[b.action]==XR_ACTION_TYPE_BOOLEAN_INPUT,"Quest rest or Index trackpad touch, boolean action");}
 if(name.ends_with("/pose"))check(types[b.action]==XR_ACTION_TYPE_POSE_INPUT,"pose action type");if(name.ends_with("haptic"))check(types[b.action]==XR_ACTION_TYPE_VIBRATION_OUTPUT,"haptic action type");}return XR_SUCCESS;}
XrResult XRAPI_PTR attachSet(XrSession,const XrSessionActionSetsAttachInfo* i){check(i->countActionSets==1,"one coherent action set");return XR_SUCCESS;}
XrResult XRAPI_PTR createSpace(XrSession,const XrActionSpaceCreateInfo* i,XrSpace* s){check(i->poseInActionSpace.orientation.w==1,"identity pose offset");*s=(XrSpace)++next;++spaces;return XR_SUCCESS;}
XrResult XRAPI_PTR destroySpace(XrSpace){--spaces;return XR_SUCCESS;}
XrResult XRAPI_PTR syncActions(XrSession,const XrActionsSyncInfo*){return syncResult;}
XrResult XRAPI_PTR pose(XrSession,const XrActionStateGetInfo*,XrActionStatePose* s){s->isActive=true;return XR_SUCCESS;}
XrResult XRAPI_PTR scalar(XrSession,const XrActionStateGetInfo*,XrActionStateFloat* s){s->isActive=true;s->currentState=.75f;return XR_SUCCESS;}
XrResult XRAPI_PTR boolean(XrSession,const XrActionStateGetInfo* i,XrActionStateBoolean* s){s->isActive=true;s->currentState=true;if(actionNames[i->action]=="thumb_rest_touch"){s->isActive=restActive;s->currentState=restTouched[i->subactionPath==paths["/user/hand/right"]?1:0];}return XR_SUCCESS;}
XrResult XRAPI_PTR vector(XrSession,const XrActionStateGetInfo*,XrActionStateVector2f* s){s->isActive=true;s->currentState={.4f,.7f};return XR_SUCCESS;}
XrResult XRAPI_PTR locate(XrSpace,XrSpace,XrTime t,XrSpaceLocation* s){check(t==123,"coherent predicted time");s->locationFlags=tracking?15:3;s->pose.orientation.w=1;s->pose.position={.1f,1.2f,.3f};auto* v=(XrSpaceVelocity*)s->next;v->velocityFlags=XR_SPACE_VELOCITY_LINEAR_VALID_BIT;v->linearVelocity={1,2,3};return XR_SUCCESS;}
XrResult XRAPI_PTR profile(XrSession,XrPath,XrInteractionProfileState* s){s->interactionProfile=XR_NULL_PATH;return XR_SUCCESS;}
XrResult XRAPI_PTR pathString(XrInstance,XrPath,uint32_t,uint32_t*,char*){return XR_ERROR_PATH_INVALID;}
XrResult XRAPI_PTR applyHaptic(XrSession,const XrHapticActionInfo* info,const XrHapticBaseHeader* effect){++hapticCount;++perHandHaptics[info->subactionPath==paths["/user/hand/right"]?1:0];hapticHand=info->subactionPath;lastHaptic=*reinterpret_cast<const XrHapticVibration*>(effect);return XR_SUCCESS;}
XrResult XRAPI_PTR stopHaptic(XrSession,const XrHapticActionInfo*){++stopCount;return XR_SUCCESS;}
XrResult XRAPI_PTR gipa(XrInstance,const char* name,PFN_xrVoidFunction* out){
#define MAP(n,f) if(!strcmp(name,#n)){*out=reinterpret_cast<PFN_xrVoidFunction>(f);return XR_SUCCESS;}
 MAP(xrStringToPath,stringPath) MAP(xrCreateActionSet,createSet) MAP(xrDestroyActionSet,destroySet) MAP(xrCreateAction,createAction)
 MAP(xrSuggestInteractionProfileBindings,bindings) MAP(xrAttachSessionActionSets,attachSet) MAP(xrCreateActionSpace,createSpace) MAP(xrDestroySpace,destroySpace)
 MAP(xrSyncActions,syncActions) MAP(xrGetActionStatePose,pose) MAP(xrGetActionStateFloat,scalar) MAP(xrGetActionStateBoolean,boolean)
 MAP(xrGetActionStateVector2f,vector) MAP(xrLocateSpace,locate) MAP(xrGetCurrentInteractionProfile,profile) MAP(xrPathToString,pathString)
 MAP(xrApplyHapticFeedback,applyHaptic) MAP(xrStopHapticFeedback,stopHaptic)
#undef MAP
 return XR_ERROR_FUNCTION_UNSUPPORTED;
}
}
int main(){using namespace circle::motor::hands;
 check(attach(gipa,(XrInstance)1,(XrSession)2,(XrSpace)3,true),"attach profiles");check(profiles==3,"Touch, Plus and Index supplied");
 sync(123,true);auto s=latest();check(s.focused&&s.hands[0].tracked&&s.hands[1].tracked,"both hand snapshots");check(s.hands[0].trigger==.75f&&s.hands[1].stick.y==.7f&&s.hands[0].velocityValid,"analog and velocity snapshot");
 check(restBindings==6,"Touch and Plus bind both rests; Index binds both trackpad touch sensors");
 check(s.hands[0].thumbRest&&!s.hands[1].thumbRest,"independent thumb-rest snapshots");
 restTouched={false,true};sync(123,true);check(!latest().hands[0].thumbRest&&latest().hands[1].thumbRest,"rest changes propagate by hand");
 restActive=false;sync(123,true);check(!latest().hands[1].thumbRest,"inactive sensor cannot activate gesture");restActive=true;
 check(atTime(123).hands[0].tracked&&!atTime(456).focused,"hand history requires exact head timestamp");
 check(requestHaptic(1,2.f,900),"queue tracked native event");sync(123,true);check(hapticCount==1&&hapticHand==paths["/user/hand/right"]&&lastHaptic.amplitude==1.f&&lastHaptic.duration==200000000,"haptic hand and bounded amplitude/duration");
 sync(123,true);check(hapticCount==1,"pulse consumed once");check(!requestHaptic(2,1,10),"invalid hand rejected");
 check(requestHaptic(0,.5f,20),"queue before focus loss");
 sync(123,false);check(!latest().hands[0].lower&&!latest().hands[1].tracked&&!latest().hands[1].thumbRest,"focus loss releases held input");
 check(!atTime(123).focused,"focus loss invalidates history");
 check(hapticCount==1&&stopCount>=2&&!requestHaptic(0,1,50),"focus loss stops and discards haptics");
 tracking=false;sync(123,true);check(!latest().hands[0].tracked&&latest().hands[0].trigger==0,"valid but untracked pose cannot trigger");
 tracking=true;syncResult=XR_SESSION_NOT_FOCUSED;sync(123,true);check(!latest().focused,"positive nonfocused result is not active");
 syncResult=XR_SUCCESS;sync(123,true);check(requestHaptic(0,.2f,20),"queued event before expiry");Sleep(110);check(!latest().focused,"expired input cannot fire");
 sync(123,true);check(hapticCount==1,"expired haptic not replayed");
 sync(123,false);sync(123,true); // discard previous event envelope
 const auto before=hapticCount;
 XINPUT_VIBRATION motors{65535,12000},zero{};
 check(circle::motor::haptics::setState(1,&motors)==ERROR_DEVICE_NOT_CONNECTED,"other pad is passthrough");
 check(circle::motor::haptics::setState(0,nullptr)==ERROR_DEVICE_NOT_CONNECTED,"null vibration passthrough");
 check(circle::motor::haptics::setState(0,&motors)==ERROR_SUCCESS,"virtual pad accepts rumble");
 check(hapticCount==before,"game thread queues without XR calls");
 circle::motor::haptics::setState(0,&zero);sync(123,true);
 check(hapticCount==before+2&&lastHaptic.amplitude==.6f,"short native pulse survives stop before XR frame on both hands");
 const auto stops=stopCount;sync(123,true);check(stopCount==stops+2,"native zero stops both outputs");
 requestGameRumble(10000,50000);sync(123,true);const auto hold=hapticCount;sync(123,true);check(hapticCount==hold+2,"held native state renewed");
 requestHaptic(0,1,200);sync(123,true);requestGameRumble(0,0);sync(123,true);
 check(lastHaptic.amplitude==1,"native stop preserves local event envelope");
 sync(123,false);check(!requestGameRumble(65535,0),"unfocused native haptic rejected");sync(123,true);
 const auto resume=hapticCount;sync(123,true);check(hapticCount==resume,"no native replay after focus regain");
 requestGameRumble(65535,0);
 // Sleep can return within one GetTickCount64 quantum of the requested delay.
 // Wait on the same clock as the expiry rule, clearly beyond its 2000 ms boundary.
 const auto expiryStart=GetTickCount64();
 while(GetTickCount64()-expiryStart<=2050)Sleep(10);
 sync(123,true);check(hapticCount==resume,"stale native vibration expires");
 sync(123,false);sync(123,true);
 tracking=false;sync(123,true);
 const auto handCounts=perHandHaptics;
 requestGameRumble(65535,0);sync(123,true);
 check(perHandHaptics[0]==handCounts[0]+1&&perHandHaptics[1]==handCounts[1]+1,"connected controllers both receive native rumble during optical tracking loss");
 check(!latest().hands[0].tracked&&!requestHaptic(0,1,20),"pose-dependent event eligibility remains strict");
 sync(123,false);tracking=true;sync(123,true);
 const auto freshCounts=perHandHaptics;sync(123,true);
 check(perHandHaptics==freshCounts,"focus loss discards rumble even when controllers stay connected");

 sync(123,false);sync(123,true);
 auto actionCounts=perHandHaptics;
 check(requestHaptic(1,.55f,65),"right whip action accepted");sync(123,true);
 check(perHandHaptics[0]==actionCounts[0]&&perHandHaptics[1]==actionCounts[1]+1,"whip feedback goes only to right output");
 actionCounts=perHandHaptics;
 check(requestHaptic(0,.45f,45),"left swing accepted");sync(123,true);
 check(perHandHaptics[0]==actionCounts[0]+1&&perHandHaptics[1]==actionCounts[1],"left swing does not restart right output");
 actionCounts=perHandHaptics;
 requestHaptic(0,.45f,45);requestHaptic(1,.45f,45);sync(123,true);
 check(perHandHaptics[0]==actionCounts[0]+1&&perHandHaptics[1]==actionCounts[1]+1,"simultaneous actions reach both hands independently");
 detach();check(spaces==0&&sets==0&&!latest().focused,"cleanup clears spaces, actions, snapshot");
 return errors?1:0;
}
