#include "motor/refraction.hpp"
#include "motor/refraction_history_guard.hpp"
#include "motor/refraction_depth.hpp"
#include "motor/transparent_sort.hpp"
#include "motor/build_profile.hpp"
#include "motor/diagnostics.hpp"
#include "motor/sequential.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/log.hpp"
#include <safetyhook.hpp>
#include <mutex>
#include <vector>
#include <cmath>
#include <cstring>
#include <atomic>

namespace circle::motor::refraction {
namespace {
std::uintptr_t base{},device{};
std::mutex mutex;
RefractionHistoryGuard history;
template<class T>T get(std::uintptr_t p){T value{};SIZE_T n{};
    if(p)ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&value,sizeof(value),&n);
    return value;}
// Headset-verified r338 workaround: the separate water reflection pass produces
// white patches/dark blotches even with its temporal history disabled.
// Enforce only in this VR installation; do not change the saved graphics profile.
void waterReflectionPolicy(){
    static std::mutex guard;std::lock_guard lock(guard);
    static ULONGLONG last{};const auto now=GetTickCount64();
    if(last&&now-last<1000)return;last=now;
    const auto rva=build::rva(0x44e4910);if(!base||!rva)return;
    const auto wrapper=base+rva;constexpr char expected[]="r_waterReflections";
    char name[sizeof(expected)]{};SIZE_T n{};
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(get<std::uintptr_t>(wrapper+0x30)),name,sizeof(name),&n)||n!=sizeof(name)||std::memcmp(name,expected,sizeof(name)))return;
    const auto value=get<std::uintptr_t>(wrapper);if(!value)return;
    struct Numeric{int integer;float floating;};
    const auto before=get<Numeric>(value+8);
    if(before.integer!=1||before.floating!=1.f)return;
    const Numeric disabled{};
    if(WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(value+8),&disabled,sizeof(disabled),&n)&&n==sizeof(disabled))
        circlevr::vkcore::logf("motor-water-reflections: separate native reflection pass disabled for VR (r338 verified white-patch workaround); saved profile unchanged");
}
#include "refraction_depth_runtime.inl"
const char* name(RefractionHistoryStatus s){switch(s){
    case RefractionHistoryStatus::SameEye:return "same-eye";
    case RefractionHistoryStatus::OtherEye:return "other-eye";
    case RefractionHistoryStatus::Gap:return "gap";
    case RefractionHistoryStatus::Resized:return "resized";
    default:return "unknown";}}
void pass(safetyhook::Context& r,bool masked){
    // Native C32170 context: +10 render view, +F0 captured backend counter.
    // RCX points at the fully assembled argument block immediately before
    // dispatch construction. No global backend counter or global eye is used.
    const auto context=r.r13,args=r.rcx;
    const auto view=get<std::uintptr_t>(context+0x10);
    const auto counter=get<unsigned>(context+0xf0);
    const auto tag=circlevr::vkcore::seqTagForBackendFrame(counter+1u);
    const auto dc=get<std::uintptr_t>(base+build::rva(0x4a729b8));
    const auto update=static_cast<unsigned>(r.rsi);
    std::lock_guard lock(mutex);
    if(device!=dc){device=dc;history.clear();}
    if(!dc||!view||!tag||!tag->tick||!circlevr::stereo_seq::drawsEyeView(*tag)||
       get<int>(view+0x18f34)!=0||update>=2){history.clear();return;}
    // Unmasked mip passes that do not accumulate have no valid history block.
    if(!masked&&get<unsigned char>(args+0x5a)!=1)return;
    const unsigned w=(counter&1)*2+update,prev=((counter&1)^1)*2+update;
    const auto output=get<std::uintptr_t>(args+0x20),input=get<std::uintptr_t>(args+0x28);
    const auto nativeOutput=get<std::uintptr_t>(get<std::uintptr_t>(dc+0xb90+w*8)+0x10);
    const auto nativeInput=get<std::uintptr_t>(get<std::uintptr_t>(dc+0xb90+prev*8)+0x10);
    const auto maskNow=masked?get<std::uintptr_t>(args+0x10):0;
    const auto maskLast=masked?get<std::uintptr_t>(args+0x18):0;
    const auto blendAddress=args+(masked?0x68:0x50);
    const auto blend=get<float>(blendAddress);
    const auto size=get<std::array<int,2>>(output+0xa4);
    const bool valid=output&&input&&output!=input&&output==nativeOutput&&input==nativeInput&&
        size[0]>0&&size[1]>0&&size==get<std::array<int,2>>(input+0xa4)&&
        std::isfinite(blend)&&blend>=0&&blend<=1&&
        (!masked||(maskNow&&maskLast&&maskNow!=maskLast&&
          maskNow==get<std::uintptr_t>(dc+0x780+(counter&1)*8)&&
          maskLast==get<std::uintptr_t>(dc+0x780+((counter&1)^1)*8)));
    if(!valid){
        history.clear();static unsigned rejected{};
        if(++rejected<=8)circlevr::vkcore::logf("motor-refraction: native argument validation rejected; unchanged backend %u masked %d update %u",counter,masked,update);
        return;
    }
    const unsigned eye=tag->eye==circlevr::stereo_seq::Eye::Right?1u:0u;
    const auto status=history.observe(w,prev,{output,counter,tag->eyeSeq,eye,size[0],size[1]},input);
    const bool reset=diagnostics::refractionHistoryGuard().load()&&status!=RefractionHistoryStatus::SameEye;
    if(reset){const float currentOnly=1.0f;std::memcpy(reinterpret_cast<void*>(blendAddress),&currentOnly,sizeof(currentOnly));}
    static unsigned counts[2][2]{};const auto n=++counts[eye][update];
    if(n<=8||n%1200==0)
        circlevr::vkcore::logf("motor-refraction: eye %u backend %u eyeSeq %u tick %llu view %p context %p masked %d update %u write %p read %p maskNow %p maskLast %p size %dx%d scheduledHistory %s blend %.3f -> %.3f sample %u",
            eye,counter,tag->eyeSeq,tag->tick,(void*)view,(void*)context,masked,update,(void*)output,(void*)input,(void*)maskNow,(void*)maskLast,size[0],size[1],name(status),blend,reset?1.0f:blend,n);
}
void masked(safetyhook::Context& r){pass(r,true);}
void plain(safetyhook::Context& r){pass(r,false);}
}
void noteStableSort(std::uintptr_t options,std::uintptr_t view,std::uint32_t frame,std::uint64_t token,unsigned eye){
    waterReflectionPolicy();
    std::lock_guard lock(sortOwnerMutex);
    auto& owner=sortOwners[sortOwnerCursor++%sortOwners.size()];
    owner.options=options;owner.scope.note(view,token,frame,eye,true);
}
void installEarly(){
    static std::once_flag once;std::call_once(once,[]{
        if(!sequential::requested()||!build::supported())return;
        base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        waterReflectionPolicy();
        struct Site{std::uintptr_t at;const char* bytes;unsigned size;void(*fn)(safetyhook::Context&);};
        const Site sites[]={
            {0xc3268e,"\xe8\x1d\x3a\x03\x00",5,masked},
            {0xc3276c,"\xe8\xef\x34\x03\x00",5,plain},
            {0xc316eb,"\x48\x8b\x92\xe0\x00\x00\x00",7,beginDepth},
            {0xc31a43,"\x45\x33\xc9\xc6\x44\x24\x20\x00",8,captureDepth},
            {0xc31b20,"\x45\x85\xe4\x0f\x85\xd5\x00\x00\x00",9,useDepth},
            {0xc31c44,"\x0f\x2f\x7c\x24\x50",5,depthSeparation},
            {0xc33b58,"\x89\x87\xdc\xd3\x01\x00",6,finishDepth},
            {0xc33c80,"\x41\x8b\x89\x68\x01\x00\x00",7,nearestDepth}};
        for(const auto& s:sites)if(!build::matches(s.at,s.bytes,s.size)){
            circlevr::vkcore::logf("motor-refraction: fingerprint rejected at %llx; unchanged",s.at);return;}
        std::vector<safetyhook::MidHook> hooks;
        for(const auto& s:sites){auto hook=safetyhook::MidHook::create(reinterpret_cast<void*>(base+build::rva(s.at)),s.fn,safetyhook::MidHook::StartDisabled);if(!hook)return;hooks.push_back(std::move(*hook));}
        for(auto& hook:hooks)if(!hook.enable())return;
        for(auto& hook:hooks)new safetyhook::MidHook(std::move(hook));
        circlevr::vkcore::logf("motor-refraction: masked/plain native history ownership guard installed; current-only fallback for unverified or foreign-eye history; native images and depth unchanged");
        circlevr::vkcore::logf("motor-refraction-depth: stable draw order + per-eye geometric depth for grouping/nearest selection installed; absolute layer separation, native update budget retained");
    });
}
}
