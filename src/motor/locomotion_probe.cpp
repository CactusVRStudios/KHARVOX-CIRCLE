#include "cheat_runtime.hpp"
#include "motor/diagnostics.hpp"
#include "motor/build_profile.hpp"
#include "motor/locomotion_probe.hpp"
#include "motor/locomotion_runtime.hpp"
#include "motor/adapter.hpp"
#include "motor/interaction_runtime.hpp"
#include "motor/hand_runtime.hpp"
#include "motor/default_input.hpp"
#include "motor/whip_policy.hpp"
#include "motor/sequential.hpp"
#include "motor/mounted_gun_runtime.hpp"
#include "motor/boat_runtime.hpp"
#include <safetyhook.hpp>
#include "vkcore/log.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <cstdio>

namespace circle::motor::locomotion {
void installHandProbe();
namespace {
std::mutex sampleMutex;
NativeSample sample;
input::BlowState blowState;
WhipTraversalEpisode whipEpisode;
uint64_t journalOwner{},journalTick{};
int observedJournal{-1};
bool read(uintptr_t p,void* dst,size_t n){SIZE_T got=0;return p&&ReadProcessMemory(GetCurrentProcess(),(void*)p,dst,n,&got)&&got==n;}
template<class T>T at(uintptr_t p){T v{};read(p,&v,sizeof(v));return v;}
void observeBlow(safetyhook::Context& r){
    // AirAttackEffectStateJob's active-state update, immediately after its
    // entity query. This callback executes before testing attack1/attack2,
    // so recognition does not depend on already managing to blow/punch.
    const auto n=latestNative();const auto now=GetTickCount64();
    if(!n.valid||now<n.tick||now-n.tick>=100||r.rdi!=n.entity||
       at<uint64_t>(at<uintptr_t>(r.rbp+8))!=n.entity||
       at<uintptr_t>(r.rbp+0x78)!=n.userCmd)return;
    std::lock_guard lock(sampleMutex);
    if(!blowState.active(n.entity,now))circlevr::vkcore::logf("motor-blow-context: active owner %llx; native air-effect update, both index triggers reserved",n.entity);
    blowState.observe(n.entity,now);
}
void endBlow(safetyhook::Context& r){
    // The paired native exit clears the effect's spine override. Revoke the
    // trigger route immediately, rather than retaining it until timeout.
    std::lock_guard lock(sampleMutex);
    if(blowState.owner==r.rdx)circlevr::vkcore::logf("motor-blow-context: ended owner %llx",r.rdx);
    blowState.end(r.rdx);
}
void journalObserve(safetyhook::Context& r){
    const auto owner=at<uint64_t>(r.rsp+0x50);
    const auto player=latestNative();
    if(!player.valid||player.entity!=owner)return;
    const auto stance=at<int>(r.rdx+0x50);
    const auto state=at<int>(r.rdi);
    const auto slot=at<unsigned char>(r.r15+0x10c);
    {std::lock_guard lock(sampleMutex);journalOwner=owner;journalTick=GetTickCount64();observedJournal=slot==16&&state>=1&&state<=2?state:0;}
    static uint64_t last{};static int previous=-1,previousSlot=-1;
    const auto now=GetTickCount64();
    if(state!=previous||slot!=previousSlot||(diagnostics::enabled()&&now-last>1000)){
        previous=state;previousSlot=slot;last=now;
        circlevr::vkcore::logf("motor-journal-state: owner %llx state %d stance %d slot %u node %p weaponState %p",owner,state,stance,slot,(void*)r.rdi,(void*)r.rdx);
    }
}
void waterMovement(safetyhook::Context& r){
    // Before native physics dispatch: RBP+390 owns this step's temporary
    // parameters; +160/+16C are its forward/right vectors. The water path
    // 1581FD0 consumes them directly, including forward.z for diving.
    const auto owner=at<uint64_t>(at<uintptr_t>(r.rbp-0x78));
    const auto controller=at<uintptr_t>(r.rbp+0x38);
    const auto mode=at<unsigned char>(controller+0x1f5);
    if(!nativeWaterState(mode,at<int>(r.rdi+0x58),at<int>(r.rdi+0x60),at<unsigned char>(r.rdi+0x1bf)!=0,at<unsigned char>(r.rdi+0x48)!=0))return;
    WaterBasis basis;if(!waterPhysicsBasis(owner,mode,basis))return;
    std::array<Vec,2> original{};
    if(!read(r.rbp+0x4f0,original.data(),sizeof(original))||!finite(original[0])||!finite(original[1])||
       std::abs(length(original[0])-1)>.02f||std::abs(length(original[1])-1)>.02f)return;
    const std::array<Vec,2> directions{basis.forward,basis.right};
    std::memcpy((void*)(r.rbp+0x4f0),directions.data(),sizeof(directions));
    static std::atomic<uint64_t> last{};const auto now=GetTickCount64();
    if(now-last.load()>1000){last=now;circlevr::vkcore::logf("motor-water-basis: owner %llx mode %u head forward %.4f %.4f %.4f right %.4f %.4f %.4f native forward %.4f %.4f %.4f move %d/%d",owner,mode,basis.forward.x,basis.forward.y,basis.forward.z,basis.right.x,basis.right.y,basis.right.z,original[0].x,original[0].y,original[0].z,int(at<signed char>(r.rbp+0x490)),int(at<signed char>(r.rbp+0x491)));}
}
void observe(safetyhook::Context& r){
    static std::atomic<unsigned> count{0};const auto n=++count;
    std::array<unsigned char,0x1f8> state{};
    if(!read(r.rdi,state.data(),state.size()))return;
    const auto value=[&]<class T>(size_t o){T v{};std::memcpy(&v,state.data()+o,sizeof(v));return v;};
    NativeSample next;
    next.physics=r.rdi;next.userCmd=at<uintptr_t>(r.rbp-0x68);next.viewAngles=at<uintptr_t>(r.rbp-0x58);
    next.controller=at<uintptr_t>(r.rbp+0x38);next.entity=at<uint64_t>(at<uintptr_t>(r.rbp-0x78));
    next.position=value.operator()<Vec>(8);next.angles=at<Vec>(next.viewAngles+0x40);
    next.tick=GetTickCount64();next.gameTime=value.operator()<uint64_t>(0);
    next.movementType=value.operator()<int>(0x58);next.explicitReason=value.operator()<int>(0x60);next.movementFlags=value.operator()<unsigned>(0x50);
    next.walking=state[0x1b6]==1;next.ground=state[0x1b4]==1;next.animated=state[0x1bf]!=0;next.localSpace=state[0x48]!=0;
    next.movementMode=at<unsigned char>(next.controller+0x1f5);
    next.valid=finite(next.position)&&finite(next.angles)&&read(next.userCmd,next.command.data(),next.command.size());
    NativeSample previous;
    {std::lock_guard lock(sampleMutex);previous=sample;
     if(previous.valid&&(next.gameTime<previous.gameTime||next.physics!=previous.physics||next.controller!=previous.controller))whipEpisode={};
     next.whipPrompt=whipEpisode.update(next.entity,next.valid,next.movementMode,next.movementType,next.explicitReason,next.animated,next.localSpace);
     sample=next;}
    if(next.whipPrompt!=previous.whipPrompt)circlevr::vkcore::logf("motor-whip-prompt-event: owner %llx active %d mode %u type %d; native state transition",next.entity,next.whipPrompt,next.movementMode,next.movementType);
    if(next.valid&&(!previous.valid||next.entity!=previous.entity||next.movementMode!=previous.movementMode||
        next.movementType!=previous.movementType||next.explicitReason!=previous.explicitReason||
        next.animated!=previous.animated||next.localSpace!=previous.localSpace))
        circlevr::vkcore::logf("motor-native-state-edge: owner %llx mode %u type %d reason %d animated %d local %d pos %.4f %.4f %.4f",next.entity,next.movementMode,next.movementType,next.explicitReason,next.animated,next.localSpace,next.position.x,next.position.y,next.position.z);
    // idUserInputCmd reflection: buttons +0, bindSet +40. Capture edges
    // before periodic movement logging so short native inputs aren't missed.
    uint64_t buttons{},lastButtons{};int bind{},lastBind{};
    std::memcpy(&buttons,next.command.data(),8);std::memcpy(&bind,next.command.data()+40,4);
    std::memcpy(&lastButtons,previous.command.data(),8);std::memcpy(&lastBind,previous.command.data()+40,4);
    // Native local command edge, shared by RT and motion whip input. Require
    // continuity so load/focus recovery cannot replay an already-held action.
    if(next.valid&&previous.valid&&next.entity==previous.entity&&next.tick>=previous.tick&&next.tick-previous.tick<100&&
       (buttons&hands::whipCrack)&&!(lastButtons&hands::whipCrack)&&hands::weaponSlot(next.entity)==18&&
       trackedGameplayState(next.movementMode,next.movementType,next.explicitReason,next.animated,next.localSpace)&&
       sequential::projectionAllowed()&&!sequential::menuActive()){
        const bool feedback=hands::requestHaptic(1,.55f,65);
        circlevr::vkcore::logf("motor-action-haptic: native whip command hand 1 queued %d; action feedback, not hit confirmation",feedback);
    }
    if(next.valid&&(!previous.valid||buttons!=lastButtons||bind!=lastBind||next.entity!=previous.entity)){
        circlevr::vkcore::logf("motor-input-edge: tick %llu entity %llx bind %d buttons %016llx changed %016llx",next.tick,next.entity,bind,buttons,buttons^lastButtons);
    }
    if(!diagnostics::enabled()||(n>20&&n%120))return;
    circlevr::vkcore::logf("motor-locomotion-probe: entity %llx cmd %p viewAngles %p controller %p pitch/yaw/roll %.4f %.4f %.4f move %d %d mode %u",
        next.entity,(void*)next.userCmd,(void*)next.viewAngles,(void*)next.controller,next.angles.x,next.angles.y,next.angles.z,
        (int)(signed char)next.command[8],(int)(signed char)next.command[9],next.movementMode);
    circlevr::vkcore::logf("motor-locomotion-probe: step %u state %p pos %.6f %.6f %.6f velocity %.5f %.5f %.5f type %d flags %x explicit %d ground %u walking %u animated %u local %u",
        n,(void*)r.rdi,value.operator()<float>(8),value.operator()<float>(12),value.operator()<float>(16),
        value.operator()<float>(20),value.operator()<float>(24),value.operator()<float>(28),value.operator()<int>(0x58),value.operator()<unsigned>(0x50),value.operator()<int>(0x60),
        state[0x1b4],state[0x1b6],state[0x1bf],state[0x48]);
    // Read-only candidates, not yet authorized as local-player/input pointers.
    for(unsigned offset: {8u,0x38u,0x48u,0x58u,0x68u,0x78u,0x88u,0x98u}){
        auto p=at<uintptr_t>(r.rbp+offset);std::array<unsigned char,48> bytes{};
        if(!read(p,bytes.data(),bytes.size()))continue;
        char hex[97]{};for(unsigned i=0;i<bytes.size();++i)sprintf_s(hex+2*i,3,"%02x",bytes[i]);
        circlevr::vkcore::logf("motor-locomotion-probe: candidate %x ptr %p first48 %s",offset,(void*)p,hex);
    }
}
}
NativeSample latestNative(){std::lock_guard lock(sampleMutex);return sample;}
bool blowInteractionActive(){
    std::lock_guard lock(sampleMutex);const auto now=GetTickCount64();
    return sample.valid&&now>=sample.tick&&now-sample.tick<100&&blowState.active(sample.entity,now);
}
int journalState(){
    std::lock_guard lock(sampleMutex);
    return sample.valid&&journalOwner==sample.entity&&journalTick&&GetTickCount64()-journalTick<250?observedJournal:-1;
}
bool flatInteractionActive(){
    const auto s=latestNative();
    // Selecting an ability book leaves journal slot16 for native book slot13.
    // Use fresh equipment ownership, not a timer on the previous journal.
    const bool book=s.valid&&GetTickCount64()-s.tick<100&&hands::weaponSlot(s.entity)==13;
    return book||journalState()==1||objectInspectionActive();
}
bool objectInspectionActive(){
    const auto s=latestNative();
    return s.valid&&GetTickCount64()-s.tick<100&&
        nativeObjectInspection(s.movementMode,s.movementType,s.explicitReason,s.animated,s.localSpace);
}
void installProbe(){
    static std::once_flag once;std::call_once(once,[]{
        const auto base=(uintptr_t)GetModuleHandleW(nullptr);
        constexpr unsigned char expected[]={0x83,0x7f,0x58,0x05,0x0f,0x85,0x21,0x01,0,0};
        std::array<unsigned char,sizeof(expected)> actual{};
        if(!isTargetProcess()||!build::matches(0x1588c1a,expected,sizeof(expected)))return;
        auto hook=safetyhook::MidHook::create((void*)(base+build::rva(0x1588c1a)),observe);
        if(hook){new safetyhook::MidHook(std::move(*hook));circlevr::vkcore::logf("motor-locomotion-probe: native physics-step observer installed; NO movement injection");}
        cheats::install();
        installHandProbe();
        turret::install();
        boat::install();
        interaction::install();
        constexpr unsigned char blowBytes[]={0x48,0x83,0x7d,0x08,0x00};
        constexpr unsigned char blowExitBytes[]={0x48,0x81,0xec,0x08,0x01,0x00,0x00};
        if(build::matches(0x18e2234,blowBytes,sizeof(blowBytes))&&build::matches(0x18e25d0,blowExitBytes,sizeof(blowExitBytes))){
            auto observeHook=safetyhook::MidHook::create((void*)(base+build::rva(0x18e2234)),observeBlow);
            auto exitHook=safetyhook::MidHook::create((void*)(base+build::rva(0x18e25d0)),endBlow);
            if(observeHook&&exitHook){new safetyhook::MidHook(std::move(*observeHook));new safetyhook::MidHook(std::move(*exitHook));circlevr::vkcore::logf("motor-blow-context: native air-effect update/exit observers installed");}
        }
        constexpr unsigned char journalBytes[]={0x38,0x5f,0x04,0x0f,0x84,0x08,0x04,0,0};
        if(build::matches(0x1de6100,journalBytes,sizeof(journalBytes))){
            auto journal=safetyhook::MidHook::create((void*)(base+build::rva(0x1de6100)),journalObserve);
            if(journal){new safetyhook::MidHook(std::move(*journal));circlevr::vkcore::logf("motor-journal-state: native journal state observer installed");}
        }
        constexpr unsigned char waterBytes[]={0xe8,0xf6,0xc6,0xff,0xff};
        if(build::matches(0x1588c15,waterBytes,sizeof(waterBytes))){
            auto water=safetyhook::MidHook::create((void*)(base+build::rva(0x1588c15)),waterMovement);
            if(water){new safetyhook::MidHook(std::move(*water));circlevr::vkcore::logf("motor-water-basis: temporary native movement basis hook installed; head-directed swim, native physics retained");}
        }
    });
}
}
