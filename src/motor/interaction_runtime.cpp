#include "laser_sight.hpp"
#include "interaction_runtime.hpp"
#include "interaction_policy.hpp"
#include "build_profile.hpp"
#include "sequential.hpp"
#include "hand_runtime.hpp"
#include "vkcore/log.hpp"
#include <safetyhook.hpp>
#include <windows.h>
#include <mutex>
#include <cstring>
#include <atomic>

namespace circle::motor::interaction {
namespace {
struct Frame {Pose view{};Vec base{};std::array<Vec,2> hand{};std::array<bool,2> tracked{};uint64_t owner{},tick{},epoch{};bool valid{},ledge{},passenger{};};
struct Target {Vec point{},origin{};uint64_t owner{},id{},tick{},epoch{};bool valid{};};
std::mutex mutex;Frame frame;Target target;
std::atomic<uint64_t> magnifierOwner{},magnifierTick{};
struct Focus {uint64_t owner{},tick{},epoch{};FocusActions actions{};unsigned prompt{};};
Focus focus;
struct Query {Frame frame{};Vec origin{},candidate{},selected{};uintptr_t tracker{};uint64_t selectedId{};bool active{},candidateValid{},selectedValid{};};
thread_local Query query;
bool read(uintptr_t p,void* out,size_t size){SIZE_T got{};return p&&ReadProcessMemory(GetCurrentProcess(),(void*)p,out,size,&got)&&got==size;}
template<class T>T at(uintptr_t p){T out{};read(p,&out,sizeof(out));return out;}
void completedFocus(uintptr_t tracker,const Frame& previous){
 Focus f;f.owner=previous.owner;f.tick=GetTickCount64();f.epoch=previous.epoch;
 unsigned char prompt{};uint64_t entity{},usable{};
 if(read(tracker+0xa68,&prompt,1)&&read(tracker+0xa30,&entity,8)&&read(tracker+0xa38,&usable,8)){
  f.prompt=prompt;f.actions=decodeFocus(prompt,entity,usable);
 }
 std::lock_guard lock(mutex);
 if(f.prompt!=focus.prompt||f.actions.useTarget!=focus.actions.useTarget||f.actions.placeTarget!=focus.actions.placeTarget)
  circlevr::vkcore::logf("motor-carry-focus: completed owner %llx tracker %p prompt %02x entity %llx usable %llx use %llx place %llx fresh %d",f.owner,(void*)tracker,f.prompt,entity,usable,f.actions.useTarget,f.actions.placeTarget,f.actions.fresh);
 focus=f;
}
bool ordinary(const locomotion::NativeSample& n,uint64_t now){return n.valid&&now>=n.tick&&now-n.tick<100&&!n.animated&&!n.localSpace&&!n.movementType&&!n.explicitReason&&hands::trackedArmMovement(n.movementMode)&&sequential::projectionAllowed()&&!sequential::menuActive();}
bool focusContext(const locomotion::NativeSample& n,uint64_t now){
 if(n.valid&&now>=n.tick&&now-n.tick<100&&passengerTargeting(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace,hands::weaponSlot(n.entity))&&
    sequential::projectionAllowed()&&!sequential::menuActive())return true;
 if(n.valid&&now>=n.tick&&now-n.tick<100&&ledgeTargeting(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace)&&
    sequential::projectionAllowed()&&!sequential::menuActive())return true;
 return ordinary(n,now)||(n.valid&&now>=n.tick&&now-n.tick<100&&
  locomotion::nativeGripInteraction(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace)&&
  magnifierActive()&&sequential::projectionAllowed()&&!sequential::menuActive());
}
uintptr_t scriptViewCopy(uintptr_t record,uintptr_t source,unsigned output){
 const auto now=GetTickCount64();const auto n=locomotion::latestNative();
 const auto owner=at<uint64_t>(record);
 if(!source||!record||!n.gameTime||owner!=n.entity||!focusContext(n,now)||!hands::latest().focused)return 0;
 Frame f;
 {
  std::lock_guard lock(mutex);
  if(!frame.valid||frame.owner!=owner||now<frame.tick||now-frame.tick>100)return 0;
  // Position/direction/angles are separate native getters. Latch one HMD
  // snapshot for their common simulation instant even across worker threads.
  static Frame cached;static uint64_t simulation{};static uintptr_t view{};
  if(simulation!=n.gameTime||view!=source||cached.owner!=owner||cached.epoch!=frame.epoch||
     now<cached.tick||now-cached.tick>100){cached=frame;simulation=n.gameTime;view=source;}
  f=cached;
 }
 if(f.ledge!=ledgeTargeting(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace)||
    f.passenger!=passengerTargeting(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace,hands::weaponSlot(owner)))return 0;
 std::array<float,12> native{};
 if(!read(source+0xa8,native.data(),sizeof(native)))return 0;
 const auto corrected=scriptViewTransform(owner,n.entity,native,f.view,f.base,f.ledge);
 if(!corrected)return 0;
 // Audited getter tails read only +A8..D7. Redirect their local register,
 // never the shared camera/component or a script's NPC/cutscene camera.
 struct Copy {std::array<unsigned char,0xa8> unused{};std::array<float,12> camera{};};
 static_assert(offsetof(Copy,camera)==0xa8);
 thread_local Copy copy;copy.camera=*corrected;
 static std::atomic<unsigned> counts[3]{};
 const auto count=++counts[output];
 if(count<=6||count%600==0)circlevr::vkcore::logf("motor-script-view: output %u owner %llx simulation %llu native forward %.3f %.3f %.3f HMD forward %.3f %.3f %.3f count %u",output,owner,n.gameTime,native[3],native[4],native[5],(*corrected)[3],(*corrected)[4],(*corrected)[5],count);
 return reinterpret_cast<uintptr_t>(&copy);
}
void scriptPosition(safetyhook::Context& r){
 if(auto p=scriptViewCopy(at<uintptr_t>(r.rsp+0x58),r.rax,0))r.rax=p;
}
void scriptAngles(safetyhook::Context& r){
 if(auto p=scriptViewCopy(at<uintptr_t>(r.rsp+0x68),r.rcx,1))r.rcx=p;
}
void scriptDirection(safetyhook::Context& r){
 if(auto p=scriptViewCopy(at<uintptr_t>(r.rsp+0x58),r.rdi,2))r.rdi=p;
}
void installScriptView(){
 const auto base=(uintptr_t)GetModuleHandleW(nullptr);
 struct Hook {uintptr_t rva;const char* bytes;size_t size;void(*callback)(safetyhook::Context&);};
 const Hook hooks[]={
  {0x1d75884,"\xf2\x0f\x10\x80\xa8\x00\x00\x00",8,scriptPosition},
  {0x1d75aaf,"\x48\x81\xc1\xb4\x00\x00\x00",7,scriptAngles},
  {0x1d75cdd,"\x48\x8b\xc8\x83\xeb\x03",6,scriptDirection}};
 for(const auto& h:hooks)if(!build::matches(h.rva,h.bytes,h.size)){
  circlevr::vkcore::logf("motor-script-view: fingerprint rejected; native script view retained");return;
 }
 std::vector<safetyhook::MidHook> pending;
 for(const auto& h:hooks){auto hook=safetyhook::MidHook::create((void*)(base+build::rva(h.rva)),h.callback);if(!hook){circlevr::vkcore::logf("motor-script-view: hook creation failed; native script view retained");return;}pending.push_back(std::move(*hook));}
 for(auto& h:pending)new safetyhook::MidHook(std::move(h));
 circlevr::vkcore::logf("motor-script-view: local VR player script position/angles/direction getters installed; native camera unchanged");
}
bool whipQueryFrame(uint64_t owner,Frame& f){
 const auto now=GetTickCount64();const auto n=locomotion::latestNative();
 const bool ledge=ledgeTargeting(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace);
 if(owner!=n.entity||!(ordinary(n,now)||(ledge&&focusContext(n,now)))||!hands::latest().focused)return false;
 const auto slot=hands::weaponSlot(owner);if(!ledge&&slot!=1&&slot!=18)return false;
 std::lock_guard lock(mutex);f=frame;
 return f.valid&&f.ledge==ledge&&f.owner==owner&&now>=f.tick&&now-f.tick<=100;
}
void whipPerception(safetyhook::Context& r){
 // AimAssistPerceptionUpdateJob: resolved owner record at query+18,
 // actor const at query+28. Only this candidate's origin/forward reads use
 // the private copy. Native component, distance and occlusion stay intact.
 Frame f;const auto owner=at<uint64_t>(at<uintptr_t>(r.rbp+0xb8));
 if(!whipQueryFrame(owner,f))return;
 const auto pose=queryView(f.view,f.base,at<Vec>(r.rax+0x3f0),f.ledge);if(!pose)return;
 struct Basis {std::array<unsigned char,0x3f0> unused{};Vec origin{},forward{};};
 static_assert(offsetof(Basis,forward)==0x3fc);
 thread_local Basis basis;
 basis.origin=pose->position;basis.forward=rotate(pose->orientation,{1,0,0});
 r.rax=reinterpret_cast<uintptr_t>(&basis);
 static std::atomic<uint64_t> logged{};const auto now=GetTickCount64();auto last=logged.load();
 if(now-last>2000&&logged.compare_exchange_strong(last,now))circlevr::vkcore::logf("motor-whip-perception: owner %llx VR forward %.3f %.3f %.3f",owner,basis.forward.x,basis.forward.y,basis.forward.z);
}
void whipAimView(safetyhook::Context& r){
 // AimAssistJob copies origin/axis from idRenderView and later calls its
 // view-matrix accessor. Redirect RSI for the entire query so both agree.
 // Audited reads end at +607; the accessor only reads +A8/+B4/+607.
 Frame f;const auto owner=at<uint64_t>(at<uintptr_t>(r.rbp+0x108));
 if(!whipQueryFrame(owner,f))return;
 const auto pose=queryView(f.view,f.base,at<Vec>(r.rsi+0xa8),f.ledge);if(!pose)return;
 alignas(16) thread_local std::array<unsigned char,0x608> view;
 if(!read(r.rsi,view.data(),view.size()))return;
 const std::array<Vec,3> axes{rotate(pose->orientation,{1,0,0}),rotate(pose->orientation,{0,1,0}),rotate(pose->orientation,{0,0,1})};
 std::memcpy(view.data()+0xa8,&pose->position,sizeof(Vec));
 std::memcpy(view.data()+0xb4,axes.data(),sizeof(axes));
 r.rsi=reinterpret_cast<uintptr_t>(view.data());
 static std::atomic<uint64_t> logged{};const auto now=GetTickCount64();auto last=logged.load();
 if(now-last>2000&&logged.compare_exchange_strong(last,now))circlevr::vkcore::logf("motor-whip-aim-view: owner %llx VR forward %.3f %.3f %.3f",owner,axes[0].x,axes[0].y,axes[0].z);
}
void photoQueryView(safetyhook::Context& r){
 // FindPhotoTargetsUpdateJob, after resolving this entity's native query.
 // Only the stack-local const render-view pointer is redirected. The same
 // copy supplies both frustum construction and later origin/forward tests.
 const auto owner=at<uint64_t>(at<uintptr_t>(r.rbp-0x78));
 const auto now=GetTickCount64();const auto n=locomotion::latestNative();
 if(owner!=n.entity||hands::weaponSlot(owner)!=17||!ordinary(n,now)||!hands::latest().focused)return;
 Frame f;{std::lock_guard lock(mutex);f=frame;}
 if(!f.valid||f.owner!=owner||now<f.tick||now-f.tick>100)return;
 const auto source=at<uintptr_t>(r.rbp-0x48);
 const auto pose=queryView(f.view,f.base,at<Vec>(source+0xa8));if(!pose)return;
 alignas(16) thread_local std::array<unsigned char,0x608> view;
 if(!read(source,view.data(),view.size()))return;
 const std::array<Vec,3> axes{rotate(pose->orientation,{1,0,0}),rotate(pose->orientation,{0,1,0}),rotate(pose->orientation,{0,0,1})};
 std::memcpy(view.data()+0xa8,&pose->position,sizeof(Vec));
 std::memcpy(view.data()+0xb4,axes.data(),sizeof(axes));
 const auto replacement=reinterpret_cast<uintptr_t>(view.data());
 std::memcpy(reinterpret_cast<void*>(r.rbp-0x48),&replacement,sizeof(replacement));
 static std::atomic<uint64_t> logged{};auto last=logged.load();
 if(now-last>1000&&logged.compare_exchange_strong(last,now))circlevr::vkcore::logf("motor-photo-query: owner %llx HMD forward %.3f %.3f %.3f native photo FOV %.3f",owner,axes[0].x,axes[0].y,axes[0].z,at<float>(at<uintptr_t>(r.rbp-0x38)+0x8c));
}
void instantThrow(safetyhook::Context& r){
 // State0 has accepted native event7, resolved the throwable component and
 // passed inventory/actor eligibility. Stop before allocating animation 2F.
 // Enter the SAME job's state4 launch branch with a complete native request.
 const auto owner=at<uint64_t>(at<uintptr_t>(r.rsp+0x58));
 const auto n=locomotion::latestNative();const auto now=GetTickCount64();
 const auto request=at<uintptr_t>(r.rbp+0x38);
 const auto item=at<uint64_t>(r.rdi);
 if(owner!=n.entity||!ordinary(n,now)||!request||item==~uint64_t{}||!item||at<unsigned>(request)!=0)return;
 std::array<unsigned char,24> bytes{};if(!read(request,bytes.data(),bytes.size()))return;
 if(!locomotion::consumeInstantThrow(owner))return;
 const unsigned ready=4;const float speed=1.f;
 std::memcpy(bytes.data(),&ready,4);std::memcpy(bytes.data()+8,&item,8);std::memcpy(bytes.data()+16,&speed,4);
 // Same fatigue predicate used by the native state2 release path.
 const auto stamina=at<uintptr_t>(r.rsp+0x128); // query + E8
 using CanThrow=bool(__fastcall*)(uintptr_t);
 bytes[20]=stamina?!reinterpret_cast<CanThrow>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+build::rva(0x1a847e0))(stamina):0;
 bytes[21]=0;
 std::memcpy(reinterpret_cast<void*>(request),bytes.data(),bytes.size());
 r.rip=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+build::rva(0x1922270);
 circlevr::vkcore::logf("motor-instant-throw: owner %llx item %llx native eligibility accepted; skip windup, launch same job",owner,item);
}
void throwView(safetyhook::Context& r){
 // ThrowerUpdateJob launch: RSI is its resolved query, +18 owner, +28 actor.
 // The remaining routine only READS the actor's camera; all native item,
 // request and physics pointers stay identical in the private query copy.
 const auto owner=at<uint64_t>(at<uintptr_t>(r.rsi+0x18));
 const auto n=locomotion::latestNative();const auto now=GetTickCount64();
 const auto slot=hands::weaponSlot(owner);
 if(owner!=n.entity||(slot!=8&&slot!=10)||!ordinary(n,now)||!hands::latest().focused)return;
 Frame f;{std::lock_guard lock(mutex);f=frame;}
 if(!f.valid||f.owner!=owner||now<f.tick||now-f.tick>100)return;
 const auto actor=at<uintptr_t>(r.rsi+0x28);
 const auto pose=queryView(f.view,f.base,at<Vec>(actor+0x3f0));if(!pose)return;
 struct Copies {std::array<unsigned char,0x110> query;std::array<unsigned char,0x548> actor;};
 thread_local Copies copy;
 if(!read(r.rsi,copy.query.data(),copy.query.size())||!read(actor,copy.actor.data(),copy.actor.size()))return;
 const std::array<Vec,3> axes{rotate(pose->orientation,{1,0,0}),rotate(pose->orientation,{0,1,0}),rotate(pose->orientation,{0,0,1})};
 std::memcpy(copy.actor.data()+0x3f0,&pose->position,sizeof(Vec));
 std::memcpy(copy.actor.data()+0x3fc,axes.data(),sizeof(axes));
 const auto replacement=reinterpret_cast<uintptr_t>(copy.actor.data());
 std::memcpy(copy.query.data()+0x28,&replacement,sizeof(replacement));
 r.rsi=reinterpret_cast<uintptr_t>(copy.query.data());
 circlevr::vkcore::logf("motor-vr-throw: owner %llx slot %u forward %.3f %.3f %.3f; native speed/physics retained",owner,slot,axes[0].x,axes[0].y,axes[0].z);
}
bool muzzleTransform(uintptr_t pool,uint64_t handle,Pose& pose){
 if(handle==UINT64_MAX)return false;
 const uint64_t index=handle&((uint64_t{1}<<24)-1),offset=(handle>>40)&0x3ff;
 const auto count=at<uint64_t>(pool+8),data=at<uintptr_t>(pool);
 if(!data||index+offset>=count||count>10000000||at<uint16_t>(data+index*32+12)!=uint16_t(handle>>24))return false;
 const auto entry=data+(index+offset)*32;
 return read(entry,&pose.position,sizeof(Vec))&&read(entry+16,&pose.orientation,sizeof(Quat))&&valid(pose);
}
void firearmMuzzle(safetyhook::Context& r){
 // idFireParms pose construction, AFTER native camera convergence/origin
 // adjustment and BEFORE native spread. RSI=context, R13=attachment query,
 // R12=job, RDI=output pose pack. All belong to this shot's native job.
 const auto n=locomotion::latestNative();const auto owner=at<uint64_t>(r.rsi+0x28);
 const auto now=GetTickCount64();
 if(owner!=n.entity||!n.valid||now<n.tick||now-n.tick>=100||
    !locomotion::trackedFirearmState(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace)||
    !sequential::projectionAllowed()||sequential::menuActive())return;
 const auto weapon=at<uint64_t>(at<uintptr_t>(r.r13+0x18));
 const auto attachment=at<uintptr_t>(r.r13+0x48);
 // Use the same world attachment handle as Laser Sight. The former model-
 // accessor + local-muzzle composition could disagree during initial binding.
 Pose world{},aim{};const char* reason="world muzzle handle";
 const hands::NativeMuzzleSource source{at<uintptr_t>(r.r12+0x7b0),attachment,at<uint64_t>(attachment+0x100),owner};
 bool okay=muzzleTransform(source.pool,source.handle,world);
 if(okay){reason="tracked weapon ownership/context";okay=hands::firearmShotPose(owner,weapon,world,Pose{},aim);}
 static std::atomic<unsigned> accepted{},rejected{};
 if(!okay){const auto count=++rejected;if(count<=20||count%100==0)circlevr::vkcore::logf("motor-model-muzzle: native fallback owner %llx weapon %llx reason %s count %u",owner,weapon,reason,count);return;}
 hands::rememberMuzzleFlash(owner,weapon,aim,source);
 const Vec previous=at<Vec>(r.rdi+0x1c);
 std::memcpy((void*)r.rdi,&aim.position,sizeof(Vec));
 std::memcpy((void*)(r.rdi+0xc),&aim.orientation,sizeof(Quat));
 std::memcpy((void*)(r.rdi+0x1c),&aim.position,sizeof(Vec));
 std::memcpy((void*)(r.rdi+0x28),&aim.orientation,sizeof(Quat));
 const auto count=++accepted;
 if(count<=30||count%100==0)circlevr::vkcore::logf("motor-model-muzzle: applied owner %llx weapon %llx origin correction %.4f %.4f %.4f count %u",owner,weapon,aim.position.x-previous.x,aim.position.y-previous.y,aim.position.z-previous.z,count);
}
void firearmQuery(safetyhook::Context& r){
 // Audit final parameters; NEVER rebase them against a hand from a different
 // update. Spread/weapon mechanics after firearmMuzzle stay native.
 const auto shotQuery=r.r15,owner=at<uint64_t>(shotQuery+8);
 if(!shotQuery||owner!=locomotion::latestNative().entity)return;
 const auto start=at<Vec>(shotQuery+0x34),offset=at<Vec>(shotQuery+0x40);
 const auto orientation=at<Quat>(shotQuery+0x5c);const auto direction=rotate(orientation,{1,0,0});
 static std::atomic<unsigned> shots{};const auto count=++shots;
 if(count<=40||count%100==0)circlevr::vkcore::logf("motor-controller-shot: owner %llx weapon %llx origin %.4f %.4f %.4f muzzle offset %.4f %.4f %.4f direction %.4f %.4f %.4f spread %.5f count %u",owner,at<uint64_t>(shotQuery+0x18),start.x,start.y,start.z,offset.x,offset.y,offset.z,direction.x,direction.y,direction.z,at<float>(shotQuery+0x78),count);
}
void focusView(safetyhook::Context& r){
 query={};const auto n=locomotion::latestNative();const auto now=GetTickCount64();
 // idFocusTrackerJob query: entity record at rsp+78, actor const at rbp+38,
 // tracker at rbp+48. Transform handles have already passed generation checks.
 const auto owner=at<uint64_t>(at<uintptr_t>(r.rsp+0x78));
 if(!focusContext(n,now)||owner!=n.entity)return;
 Frame f;{std::lock_guard lock(mutex);f=frame;target={};}
 if(!f.valid||f.owner!=owner||now<f.tick||now-f.tick>100||!hands::latest().focused)return;
 // Read the completed previous query BEFORE this job clears A30/A38 at
 // 137F08D. Our old 1380E49 sample was only the end of the immediate pass:
 // deferred visibility results populate the real targets later at 138166D/78.
 // Sampling that intermediate state paired a HUD prompt with an empty target.
 completedFocus(r.r15,f);
 const bool ledge=ledgeTargeting(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace);
 if(f.ledge!=ledge)return;
 const bool passenger=passengerTargeting(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace,hands::weaponSlot(n.entity));
 if(f.passenger!=passenger)return;
 Vec nativeOrigin{};if(!read(r.rbp-0x78,&nativeOrigin,sizeof(nativeOrigin)))return;
 const auto view=queryView(f.view,f.base,nativeOrigin,f.ledge);if(!view)return;
 query.frame=f;query.origin=view->position;query.tracker=r.r15;query.active=true;
 // Stack copies only: keep the actor/body and all scripted cameras untouched.
 std::memcpy((void*)(r.rbp-0x78),&query.origin,sizeof(Vec));
 std::memcpy((void*)(r.rbp+0x90),&f.view.orientation,sizeof(Quat));
 static uint64_t logged{};if(now-logged>2000){logged=now;const auto forward=rotate(f.view.orientation,{1,0,0});circlevr::vkcore::logf("motor-vr-focus: owner %llx origin %.3f %.3f %.3f forward %.3f %.3f %.3f epoch %llu",owner,query.origin.x,query.origin.y,query.origin.z,forward.x,forward.y,forward.z,f.epoch);}
}
void candidatePoint(safetyhook::Context& r){
 // Native usable range calculation has resolved marker/shape world position.
 // Only the direct call from our active focus query can seed a hand target.
 const auto base=(uintptr_t)GetModuleHandleW(nullptr);
 if(!query.active||at<uintptr_t>(r.rbp+0x1f)!=base+build::rva(0x137fb86))return;
 query.candidateValid=read(r.rbp-0x59,&query.candidate,sizeof(Vec))&&finite(query.candidate)&&length(query.candidate-query.origin)<5.f;
}
void selectedPoint(safetyhook::Context& r){
 if(!query.active||query.tracker!=r.r15)return;
 query.selectedId=at<uint64_t>(r.r15+0xa38);query.selected=query.candidate;query.selectedValid=query.candidateValid;
 query.candidateValid=false;
}
void finishFocus(safetyhook::Context& r){
 if(!query.active)return;
 const auto q=query;query={};
 if(at<uint64_t>(at<uintptr_t>(r.rsp+0x78))!=q.frame.owner||at<uintptr_t>(r.rbp+0x48)!=q.tracker)return;
 Target next;const auto finalId=at<uint64_t>(q.tracker+0xa38);
 if(q.selectedValid&&finalId!=~uint64_t{}&&finalId==q.selectedId)
  next={q.selected,q.origin,q.frame.owner,finalId,GetTickCount64(),q.frame.epoch,true};
 static thread_local uint64_t lastId=~uint64_t{};
 const auto reportedId=next.valid?next.id:~uint64_t{};
 if(reportedId!=lastId){lastId=reportedId;circlevr::vkcore::logf("motor-hand-target: valid %d owner %llx target %llx point %.3f %.3f %.3f",next.valid,q.frame.owner,reportedId,next.point.x,next.point.y,next.point.z);}
 std::lock_guard lock(mutex);target=next;
}
}
void observeMagnifier(uint64_t owner){
 magnifierOwner.store(owner);magnifierTick.store(GetTickCount64());
}
bool magnifierActive(){
 const auto n=locomotion::latestNative();const auto now=GetTickCount64();const auto tick=magnifierTick.load();
 return n.valid&&now>=n.tick&&now-n.tick<100&&tick&&now>=tick&&now-tick<100&&
  magnifierOwner.load()==n.entity&&hands::weaponSlot(n.entity)==10&&
  !n.animated&&!n.localSpace&&!n.explicitReason;
}
void publishView(const std::array<experimental::LiveEye,2>& eyes,const locomotion::CameraMotion& motion){
 Frame next;const auto n=locomotion::latestNative();const auto now=GetTickCount64();const auto& e=eyes[0];
 const auto input=hands::atTime(e.rig.displayTime);auto center=centerView(e.camera,eyes[1].camera);
 if(motion.referenceValid&&focusContext(n,now)&&e.valid&&eyes[1].valid&&e.frameToken==eyes[1].frameToken&&e.rig.valid&&e.rig.tracked&&input.focused&&now>=input.tick&&now-input.tick<100&&center){
  next.view=*center;next.base={e.base[0],e.base[1],e.base[2]};next.owner=n.entity;next.tick=now;next.epoch=motion.epoch;next.valid=true;
  next.ledge=ledgeTargeting(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace);
  next.passenger=passengerTargeting(n.movementMode,n.movementType,n.explicitReason,n.animated,n.localSpace,hands::weaponSlot(n.entity));
  const auto& l=e.rig.positions[0];const auto& r=e.rig.positions[1];Vec head{(l[0]+r[0])*.5f,(l[1]+r[1])*.5f,(l[2]+r[2])*.5f};
  const float yaw=std::atan2(e.base[4],e.base[3]);
  locomotion::GroundFrame basis{{std::cos(yaw),std::sin(yaw),0},{std::sin(yaw),-std::cos(yaw),0},{0,0,1},1,true};
  HandFrame hf{next.base,head,basis,motion.referenceYaw};
  for(unsigned h=0;h<2;++h){const auto& hand=input.hands[h];const auto& p=hand.grip.position;auto world=hf.toWorld({p.x,p.y,p.z});if(hand.tracked&&world){next.hand[h]=*world;next.tracked[h]=true;}}
 }
 std::lock_guard lock(mutex);frame=next;
}
unsigned nearHands(const hands::Snapshot& input,const locomotion::NativeSample& n){
 const auto now=GetTickCount64();if(!ordinary(n,now)||!input.focused||now<input.tick||now-input.tick>100)return 0;
 Frame f;Target t;{std::lock_guard lock(mutex);f=frame;t=target;}
 if(!f.valid||!t.valid||f.owner!=n.entity||t.owner!=n.entity||f.epoch!=t.epoch||now<f.tick||now<t.tick||now-f.tick>100||now-t.tick>100)return 0;
 unsigned mask{};for(unsigned h=0;h<2;++h){const auto& v=input.hands[h];
  // Rebase both into the native query instant. Do not measure a world-space
  // hand from one player position against an object from another frame.
  const Vec hand=f.hand[h]+t.origin-f.view.position;
  if(f.tracked[h]&&v.tracked&&v.velocityValid&&nearUsable(hand,t.point,length({v.velocity.x,v.velocity.y,v.velocity.z})))mask|=1u<<h;
 }
 return mask;
}
FocusActions focusActions(const locomotion::NativeSample& n){
 const auto now=GetTickCount64();if(!ordinary(n,now)||!hands::latest().focused)return {};
 std::lock_guard lock(mutex);
 if(!frame.valid||focus.owner!=n.entity||focus.epoch!=frame.epoch||now<focus.tick||now-focus.tick>100)return {};
 return focus.actions;
}
void install(){laser::install();static std::once_flag once;std::call_once(once,[]{
 installScriptView();
 const auto base=(uintptr_t)GetModuleHandleW(nullptr);
 struct Hook {uintptr_t rva;const char* bytes;size_t size;void(*callback)(safetyhook::Context&);};
 const Hook hooks[]={
  {0x151ea84,"\x49\x8b\x9e\x08\x02\x00\x00",7,photoQueryView},
  {0x19221b0,"\x49\x8b\x96\xd8\x02\x00\x00",7,instantThrow},
  {0x192335f,"\x48\x8b\x5e\x28\xf3\x0f\x59\xc1",8,throwView},
  {0x12755ac,"\x48\x8d\x88\xf0\x03\x00\x00",7,whipPerception},
  {0x126fff1,"\xf2\x0f\x10\x86\xa8\x00\x00\x00",8,whipAimView},
  {0x144f80d,"\x4c\x8b\x4e\x28\x4d\x8d\x84\x24\xc0\x03\x00\x00",12,firearmMuzzle},
  {0x1a07d49,"\x41\x8b\xd6\xf0\x0f\xc1\x93\x20\x80\x08\x00",11,firearmQuery},
  {0x137eba4,"\xf3\x0f\x10\x8d\x94\x00\x00\x00",8,focusView},
  {0x138234e,"\xf3\x0f\x10\x5e\x1c",5,candidatePoint},
  {0x137fc96,"\x0f\x10\x47\x20",4,selectedPoint},
  {0x1380e49,"\x48\x8b\x75\xe8",4,finishFocus}};
 for(const auto& h:hooks)if(!build::matches(h.rva,h.bytes,h.size)){circlevr::vkcore::logf("motor-vr-focus: fingerprint rejected; native input retained");return;}
 std::vector<safetyhook::MidHook> pending;
 for(const auto& h:hooks){auto hook=safetyhook::MidHook::create((void*)(base+build::rva(h.rva)),h.callback);if(!hook)return;pending.push_back(std::move(*hook));}
 for(auto& h:pending)new safetyhook::MidHook(std::move(h));
 circlevr::vkcore::logf("motor-controller-shot: asset model-space muzzle hook before native spread; final shot audit, no late IK rebase");
 circlevr::vkcore::logf("motor-vr-focus: native focus view and accepted usable point hooks installed; range/occlusion stay native; near-hand radius 0.25m");
 });}
}
