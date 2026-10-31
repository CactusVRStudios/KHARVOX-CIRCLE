#include "motor/build_profile.hpp"
#include "framegen/bridge.hpp"
#include "motor/temporal.hpp"
#include "motor/fsr.hpp"
#include "motor/scatter.hpp"
#include "motor/nrd.hpp"
#include "motor/gi_probe.hpp"
#include "motor/gi.hpp"
#include "motor/frame_generation.hpp"
#include "motor/cinema_timing.hpp"
#include "motor/adapter.hpp"
#include "motor/sequential.hpp"
#include "motor/temporal_continuity.hpp"
#include "motor/temporal_target.hpp"
#include "motor/material_history.hpp"
#include "motor/refraction.hpp"
#include "motor/diagnostics.hpp"
#include "motor/streamline_policy.hpp"
#include "stereo_seq/ngx_eye.hpp"
#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/log.hpp"
#include "vkcore/motion_probe.hpp"
#include <safetyhook.hpp>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <optional>
#include <vector>
#include <cmath>

namespace circle::motor::temporal {
namespace {
using circlevr::stereo_seq::AccumRole;
using circlevr::stereo_seq::Eye;
using Tag=circlevr::stereo_seq::RenderTag;
std::uintptr_t base=0;
std::atomic<bool> installed{false},ready{false};
std::atomic<bool> streamlineInstalled{false};
bool streamlineRequested=false;
std::array<std::atomic<ULONGLONG>,2> slLastSuccess{};
circlevr::stereo_seq::NgxOutputBook outputBook;
std::mutex outputMutex;
std::uintptr_t device=0,second=0;
std::mutex lifecycle;
using Selector=void*(*)(void*,const void*);
using Resize=void(*)(void*,const int*,const int*);
Selector originals[6]{};
Resize originalResize=nullptr;
TemporalContinuity continuity;
std::uint64_t lastToken=0,pairSequence=0;
std::mutex pairMutex;
std::atomic<std::uint64_t> materialGeneration{};
thread_local MaterialHistoryBinding materialBinding;
MaterialHistoryKey materialKey(std::uintptr_t view,std::uintptr_t command,const Tag& tag){
    return {view,command,device,tag.tick,materialGeneration.load(),
        tag.backendFrame,tag.eyeSeq,tag.eye==Eye::Right?1u:0u};
}
template<class T> T get(std::uintptr_t p){T v{};SIZE_T n=0;if(p)ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&v,sizeof(v),&n);return v;}
template<class T> void put(std::uintptr_t p,const T& v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));}
int aa(){return get<int>(get<std::uintptr_t>(base+build::rva(0x44cccd0))+8);}
bool enabled(){const int mode=aa();return installed && ready && (mode==1 || (mode==3 && fsr::requested()) || (mode==2 && streamlineInstalled));}
std::optional<Tag> tagFor(std::uintptr_t view){
    if(!enabled() || !view || get<int>(view+0x18f34)!=0)return {};
    auto tag=circlevr::vkcore::seqTagInFlight();
    if(!tag || !tag->tick || !circlevr::stereo_seq::drawsEyeView(*tag))return {};
    // Film convergence has the opposite P02 sign to headset optics. Eye
    // ownership comes from the applied scheduler tag, never that coefficient.
    // select() also synchronizes the native active rectangle before use.
    return tag;
}
bool dimensionsMatch(){
    if(!device || !second)return false;
    const auto left=device+8;
    constexpr unsigned offsets[]={0x58,0x60,0x68,0xa0,0xa8,0xb8};
    for(auto o:offsets){
        auto l=get<std::uintptr_t>(left+o),r=get<std::uintptr_t>(second+o);
        if(!l || !r || l==r)return false;
        const auto li=get<std::uintptr_t>(l+0x10),ri=get<std::uintptr_t>(r+0x10);
        if(!li || !ri || li==ri)return false;
        const int w=get<int>(li+0xa4),h=get<int>(li+0xa8);
        if(w<=0 || h<=0 || w!=get<int>(ri+0xa4) || h!=get<int>(ri+0xa8))return false;
    }
    return true;
}
void built(safetyhook::Context& r){
    if(!installed || r.rsi!=0)return;
    std::lock_guard lock(lifecycle);ready=false;
    ++materialGeneration;
    // Same native construction sequence as slot zero; the private name index is 1.
    // Retire old device allocations instead of reusing objects from a lost device.
    auto* slot=new unsigned char[0x1b0]{};
    reinterpret_cast<void(*)(void*,unsigned)>(base+build::rva(0xbc7f10))(slot,1);
    reinterpret_cast<void(*)(void*,void*)>(base+build::rva(0xbc5c40))(reinterpret_cast<void*>(r.rdi),slot);
    device=r.rdi;second=reinterpret_cast<std::uintptr_t>(slot);
    continuity.clear();
    ready=dimensionsMatch();
    if(streamlineRequested&&!streamlineInstalled&&aa()==2){
        reinterpret_cast<void(*)(void*,int)>(base+build::rva(0x4be7a0))(reinterpret_cast<void*>(base+build::rva(0x44cccd0)),ready?1:0);
        circlevr::vkcore::logf("motor-temporal: incomplete Streamline port; native fallback to AA %d",aa());
    }
    circlevr::vkcore::logf("motor-temporal: native private slot built context %p right %p valid %d AA %d; main targets %p/%p vs %p/%p",
        reinterpret_cast<void*>(device),slot,ready.load()?1:0,aa(),
        reinterpret_cast<void*>(get<std::uintptr_t>(device+0x60)),reinterpret_cast<void*>(get<std::uintptr_t>(device+0x68)),
        reinterpret_cast<void*>(get<std::uintptr_t>(second+0x58)),reinterpret_cast<void*>(get<std::uintptr_t>(second+0x60)));
}
void retiring(safetyhook::Context& r){
    gi::retire(r.rcx);
    scatter::retire(r.rcx);
    if(r.rcx!=device)return;
    std::lock_guard lock(lifecycle);ready=false;device=0;second=0;
    ++materialGeneration;
    // As in the donor, private CPU objects are process-lifetime. Never expose
    // them after native context retirement (Vulkan device teardown owns GPU state).
    circlevr::vkcore::logf("motor-temporal: device context retired; private history unavailable");
}
void resize(void* slot,const int* size,const int* upscaled){
    originalResize(slot,size,upscaled);
    if(reinterpret_cast<std::uintptr_t>(slot)!=device+8 || !second)return;
    std::lock_guard lock(lifecycle);ready=false;
    ++materialGeneration;
    originalResize(reinterpret_cast<void*>(second),size,upscaled);
    continuity.clear();
    ready=dimensionsMatch();
    circlevr::vkcore::logf("motor-temporal: native resize %dx%d -> %dx%d; right target validation %d",size[0],size[1],upscaled[0],upscaled[1],ready.load()?1:0);
}
void* select(unsigned kind,void* rs,const void* view){
    void* native=originals[kind](rs,view);
    auto tag=tagFor(reinterpret_cast<std::uintptr_t>(view));
    if(!tag || get<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(rs)+0x11c8)!=device)
        return native;
    const auto pick=circlevr::stereo_seq::pickAccumulation((kind==1 || kind==4)?AccumRole::History:AccumRole::Output,&*tag,true);
    const auto slot=pick.pair?second:device+8;
    const unsigned offset=kind==2?0x68:kind==5?0xb8:(kind>=3?0xa0:0x58)+8*pick.index;
    void* target=reinterpret_cast<void*>(get<std::uintptr_t>(slot+offset));
    if(!target){ready=false;return native;}
    // Native framing updates the slot-zero targets without a storage resize.
    // Mirror ONLY the active dimensions into the selected private target; its
    // GPU image, allocation, history contents and ping-pong index stay its own.
    const auto sourceSize=get<TemporalTargetSize>(reinterpret_cast<std::uintptr_t>(native));
    const auto targetSize=get<TemporalTargetSize>(reinterpret_cast<std::uintptr_t>(target));
    const auto activeSize=temporalActiveSize(sourceSize,targetSize);
    if(!activeSize){
        ready=false;
        circlevr::vkcore::logf("motor-temporal: invalid active region selector %u native %dx%d/%dx%d own %dx%d/%dx%d; private targets disabled",kind,sourceSize[0],sourceSize[1],sourceSize[2],sourceSize[3],targetSize[0],targetSize[1],targetSize[2],targetSize[3]);
        return native;
    }
    if(targetSize[2]!=(*activeSize)[0]||targetSize[3]!=(*activeSize)[1]){
        put(reinterpret_cast<std::uintptr_t>(target)+8,*activeSize);
        static std::atomic<unsigned> fixes{};const auto n=++fixes;
        if(n<=24||n%1200==0)circlevr::vkcore::logf("motor-temporal-region: eye %u selector %u backend %u target %p active %dx%d -> %dx%d allocation %dx%d",pick.pair,kind,tag->backendFrame,target,targetSize[2],targetSize[3],(*activeSize)[0],(*activeSize)[1],targetSize[0],targetSize[1]);
    }
    if(kind==0||kind==3){std::lock_guard lock(outputMutex);outputBook.note(reinterpret_cast<std::uint64_t>(target),*tag);}
    static std::atomic<unsigned> counts[6]{};
    auto n=++counts[kind];
    if(n<=8 || (kind==0 && n%1200==0))
        circlevr::vkcore::logf("motor-temporal: selector %u eye %u eyeSeq %u backend %u index %d target %p",kind,pick.pair,tag->eyeSeq,tag->backendFrame,pick.index,target);
    return target;
}
void* output(void* a,const void* b){return select(0,a,b);}
void* history(void* a,const void* b){return select(1,a,b);}
void* opaque(void* a,const void* b){return select(2,a,b);}
void* highOutput(void* a,const void* b){return select(3,a,b);}
void* highHistory(void* a,const void* b){return select(4,a,b);}
void* highOpaque(void* a,const void* b){return select(5,a,b);}
void postState(safetyhook::Context& r){
    auto tag=tagFor(r.r12);if(!tag)return;
    // C28A6B stores the post-process state's pointer. Keep reset counters per eye.
    if(tag->eye==Eye::Right)r.rax=get<std::uintptr_t>(second+8);
    const auto eye=tag->eye==Eye::Right?1:0;
    if(continuity.reset(eye,tag->eyeSeq,tag->backendFrame,sequential::monoVrEnabled()?1u:2u))
        put<int>(r.rax,3);
}
void earlyHistory(safetyhook::Context& r){
    materialBinding.clear();
    const auto tag=tagFor(r.r12);if(!tag)return;
    view_audit::sample(view_audit::Stage::MaterialBind,tag->eye==Eye::Right?1u:0u,
        tag->backendFrame,tag->eyeSeq,tag->tick,r.r12);
    // The native cached override otherwise bypasses the history selector.
    r.rdi=reinterpret_cast<std::uintptr_t>(history(reinterpret_cast<void*>(base+build::rva(0x4a717f0)),reinterpret_cast<void*>(r.r12)));
    if(diagnostics::materialEyeHistory().load()&&enabled())
        materialBinding.note(materialKey(r.r12,r.r15,*tag),get<std::uintptr_t>(r.rdi+0x10));
}
void distortionHistory(safetyhook::Context& r){
    const auto tag=tagFor(r.r12);
    if(!tag||!diagnostics::materialEyeHistory().load()){materialBinding.clear();return;}
    const auto image=materialBinding.take(materialKey(r.r12,r.r15,*tag));
    const auto native=get<std::uintptr_t>(device+8+0x48);
    // C28342 has returned idImage+E0. C28354 binds distortionlastframemap.
    // Motor otherwise selects slot zero's _distortion0 for both eyes, despite
    // viewcolorlastframemap just having received an eye-specific history.
    // Adapt CircleVR's onDistortionBind, using the job's own selection instead
    // of a process-global eye. The existing temporal slot owns the GPU lifetime.
    if(!image||!native||r.rax!=native+0xe0||
       get<std::uintptr_t>(base+build::rva(0x4a729b8))!=device)return;
    const auto size=get<std::array<int,2>>(image+0xa4);
    if(size[0]<=0||size[1]<=0)return;
    r.rax=image+0xe0;
    static std::atomic<unsigned> counts[2]{};
    const unsigned eye=tag->eye==Eye::Right?1u:0u,n=++counts[eye];
    if(n<=8||n%1200==0)
        circlevr::vkcore::logf("motor-material-history: distortionlastframemap eye %u backend %u eyeSeq %u tick %llu native %p selected %p size %dx%d generation %llu sample %u",
            eye,tag->backendFrame,tag->eyeSeq,tag->tick,(void*)native,(void*)image,size[0],size[1],materialGeneration.load(),n);
}
bool check(std::uintptr_t rva,const char* bytes,std::size_t n){return build::matches(rva,bytes,n);}
#include "motor/streamline.inl"
bool inlineHook(std::uintptr_t rva,void* fn,void** original){
    auto result=safetyhook::InlineHook::create(reinterpret_cast<void*>(base+build::rva(rva)),fn,safetyhook::InlineHook::StartDisabled);
    if(!result)return false;
    auto* hook=new safetyhook::InlineHook(std::move(*result));*original=hook->original<void*>();
    return bool(hook->enable());
}
}
bool active(){return enabled();}
UpscalingStatus upscalingStatus(){
    static const auto requested=[] {static char value[32]{};GetEnvironmentVariableA("CIRCLE_UPSCALING",value,sizeof(value));return value;}();
    const auto now=GetTickCount64();const auto l=slLastSuccess[0].load(),r=slLastSuccess[1].load();
    const int mode=base&&build::supported()?aa():-1;
    const bool leftFresh=l&&now>=l&&now-l<2000;
    const bool rightFresh=r&&now>=r&&now-r<2000;
    return {requested[0]?requested:"unknown",mode,mode==3?fsr::active():mode==2&&leftFresh&&(sequential::monoVrEnabled()||rightFresh)};
}
void prepareEye(unsigned char* view,int,std::uint64_t token){
    slRetryTransition(token);
    if(!enabled())return;
    std::lock_guard lock(pairMutex);
    if(token!=lastToken){lastToken=token;++pairSequence;}
    // Confirmed Motor idRenderView/renderView subsample byte read by C6B041.
    view[0x602]=circlevr::stereo_seq::taaSubSample(pairSequence,get<int>(get<std::uintptr_t>(base+build::rva(0x44d3d80))+8));
}
void installEarly(){
    static std::once_flag once;
    std::call_once(once,[]{
        if(!sequential::requested())return;
        wchar_t value[16]{};
        if(GetEnvironmentVariableW(L"CIRCLE_VR_PROJECTION",value,16) && wcscmp(value,L"phase5")==0)return;
        base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if(fsr::requested()&&!fsr::install()){circlevr::vkcore::logf("motor-fsr: API hook installation failed; FSR selection unavailable");return;}
        frame_generation::install();
        cinema_timing::install();
        refraction::installEarly();
        struct Mid{std::uintptr_t rva;const char* bytes;std::size_t size;void(*fn)(safetyhook::Context&);};
        const Mid mids[]={
            {0xbbc257,"\x48\x8b\x05\x12\x72\x8f\x03",7,built},
            {0xbbc9d0,"\x48\x89\x5c\x24\x08\x48\x89\x6c\x24\x10",10,retiring},
            {0xc28a6b,"\x49\x89\x85\x48\x69\x9c\x00",7,postState},
            {0xc282d1,"\x48\x8b\xce\xe8\x77\xc5\x06\x00",8,earlyHistory},
            {0xc28347,"\x48\x8b\x15\x8a\x1a\x8a\x03",7,distortionHistory}};
        const std::uintptr_t selectors[]={0xc98ac0,0xc98b10,0xc98b50,0xc98b70,0xc98bc0,0xc98c10};
        Selector replacements[]={output,history,opaque,highOutput,highHistory,highOpaque};
        for(const auto& p:mids)if(!check(p.rva,p.bytes,p.size)){circlevr::vkcore::logf("motor-temporal: fingerprint rejected at %llx",p.rva);return;}
        for(unsigned i=0;i<6;++i)if(!check(selectors[i],i==2||i==5?"\x48\x63\x82\x34\x8f\x01\x00":"\x40\x53\x48\x83\xec\x20",i==2||i==5?7:6))return;
        if(!check(0xbc80b0,"\x48\x89\x5c\x24\x10\x48\x89\x6c\x24\x18",10) ||
           !check(0xbc7f10,"\x48\x89\x5c\x24\x08",5) || !check(0xbc5c40,"\x48\x89\x5c\x24\x18",5))return;
        HMODULE self=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&installEarly),&self))return;
        for(unsigned i=0;i<6;++i)if(!inlineHook(selectors[i],reinterpret_cast<void*>(replacements[i]),reinterpret_cast<void**>(&originals[i])))return;
        if(!inlineHook(0xbc80b0,reinterpret_cast<void*>(resize),reinterpret_cast<void**>(&originalResize)))return;
        for(const auto& p:mids){auto hook=safetyhook::MidHook::create(reinterpret_cast<void*>(base+build::rva(p.rva)),p.fn);if(!hook)return;new safetyhook::MidHook(std::move(*hook));}
        installStreamline();
        installed=true;
        scatter::installEarly();
        nrd::installEarly();
        gi_probe::installEarly();
        gi::installEarly();
        circlevr::vkcore::logf("motor-temporal: native temporal slot/lifecycle/6 selectors installed; Streamline SR/RR per-eye hooks %d",streamlineInstalled.load()?1:0);
    });
}
}
