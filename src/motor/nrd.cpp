#include "motor/nrd.hpp"
#include "motor/nrd_history.hpp"
#include "motor/build_profile.hpp"
#include "motor/sequential.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <safetyhook.hpp>
#include <memory>
#include <optional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace circle::motor::nrd {
namespace {
using Tag=circlevr::stereo_seq::RenderTag;
std::uintptr_t base{};
template<class T>T get(std::uintptr_t p){T v{};SIZE_T n{};if(p)ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&v,sizeof(v),&n);return v;}
struct Pair {void* right{};std::uintptr_t instance[2]{};NrdEyeHistory history[2];std::uint64_t resetTick{};unsigned count[2]{};};
std::recursive_mutex mutex;
std::unordered_map<void*,Pair> pairs;
struct Scope {std::optional<Tag> tag;std::uint32_t frame{};bool primary=false;};
thread_local Scope scope;
using Init=bool(*)(void*,const void*,const void*,void*,void*,void*);
using Destroy=void(*)(void*);
using Draw=void(*)(void*,void*);
using Begin=void(*)(void*);
using Dispatch=void(*)(void*,const void*,unsigned,void*,void*);
Init originalInit{};Destroy originalDestroy{};Begin originalBegin{};Dispatch originalDispatch{};Draw originalDraw[3]{};
bool stereo(){return scope.primary&&scope.tag&&scope.tag->tick&&circlevr::stereo_seq::drawsEyeView(*scope.tag);}
unsigned eye(){return stereo()&&scope.tag->eye==circlevr::stereo_seq::Eye::Right?1:0;}
void* routed(void* p){
    if(!scope.primary||!eye())return p;
    std::lock_guard lock(mutex);auto it=pairs.find(p);
    return it!=pairs.end()&&it->second.right?it->second.right:p;
}
bool init(void* self,const void* desc,const void* instanceDesc,void* device,void* core,void* helper){
    const bool ok=originalInit(self,desc,instanceDesc,device,core,helper);if(!ok)return false;
    std::lock_guard lock(mutex);
    auto* memory=reinterpret_cast<void*(*)(size_t)>(base+build::rva(0x2b81ae8))(0x150);
    auto* right=reinterpret_cast<void*(*)(void*)>(base+build::rva(0xc11660))(memory);
    const bool valid=originalInit(right,desc,instanceDesc,device,core,helper);
    Pair p{};p.instance[0]=get<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(self)+0xf8);
    p.instance[1]=get<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(right)+0xf8);
    if(valid&&p.instance[0]&&p.instance[1]&&p.instance[0]!=p.instance[1])p.right=right;
    // Native Initialize only fails before GPU pool construction. Retain failed
    // CPU storage for process lifetime, rather than invoking unsafe partial teardown.
    pairs[self]=p;
    circlevr::vkcore::logf("motor-nrd: private integration native %p right %p instances %p/%p ready %d",self,p.right,(void*)p.instance[0],(void*)p.instance[1],p.right!=nullptr);
    return ok;
}
void destroy(void* self){
    void* right=nullptr;
    {std::lock_guard lock(mutex);auto it=pairs.find(self);if(it!=pairs.end()){right=it->second.right;pairs.erase(it);}}
    // This is reached inside Motor's GPU-idle retirement/resize path. Use the
    // exact native cleanup, including NRI pools, NRD instance and CPU vectors.
    if(right)reinterpret_cast<void(*)(void*,void**)>(base+build::rva(0xc16330))(nullptr,&right);
    originalDestroy(self);
}
void begin(void* p){originalBegin(routed(p));}
void dispatch(void* p,const void* ids,unsigned count,void* command,void* resources){originalDispatch(routed(p),ids,count,command,resources);}
void draw(unsigned kind,void* a,void* args){
    const auto saved=scope;scope={};
    const auto job=reinterpret_cast<std::uintptr_t>(args),view=get<std::uintptr_t>(job+0x20);
    scope.frame=get<unsigned>(job+0x40);
    scope.primary=view&&get<int>(view+0x18f34)==0;
    if(scope.primary)scope.tag=circlevr::vkcore::seqTagForBackendFrame(scope.frame+1u);
    originalDraw[kind](a,args);scope=saved;
}
void drawMain(void* a,void* b){draw(0,a,b);}void drawWater(void* a,void* b){draw(1,a,b);}void drawTransparent(void* a,void* b){draw(2,a,b);}
void common(safetyhook::Context& r){
    if(!scope.primary)return;
    std::lock_guard lock(mutex);
    // RSI is the original native integration, not a frame-global eye guess.
    auto it=pairs.find(reinterpret_cast<void*>(r.rsi));if(it==pairs.end())return;
    auto& p=it->second;const unsigned e=eye();const bool pairStereo=stereo();
    auto settings=get<NrdSettings>(r.rdx);
    const bool nativeReset=settings[0x1a4]!=0;
    if(pairStereo&&nativeReset)p.resetTick=scope.tag->tick;
    const bool reset=nativeReset||(pairStereo&&p.resetTick==scope.tag->tick);
    const bool restart=p.history[e].prepare(settings,scope.frame,pairStereo&&!sequential::monoVrEnabled(),reset);
    if(e&&p.right)r.rcx=p.instance[1];
    if(pairStereo&&!p.right)settings[0x1a4]=2; // never accumulate opposite-eye history on allocation failure
    std::memcpy(reinterpret_cast<void*>(r.rdx),settings.data(),settings.size());
    const unsigned n=++p.count[e];
    if(n<=6||n%1200==0)circlevr::vkcore::logf("motor-nrd: common eye %u backend %u stereo %d instance %p restart %d previous-proj-x %.6f sample %u",e,scope.frame,pairStereo,(void*)r.rcx,restart,get<float>(r.rdx+0x60),n);
}
void settings(safetyhook::Context& r){
    if(!scope.primary||!eye())return;
    std::lock_guard lock(mutex);
    for(auto& [_,p]:pairs)if(p.instance[0]==r.rcx&&p.right){r.rcx=p.instance[1];return;}
}
}
void installEarly(){
    static std::once_flag once;std::call_once(once,[]{
        if(!sequential::requested()||!build::supported())return;
        // Unexercised outside the native PT/NRD path; keep explicit opt-in.
        wchar_t enabled[8]{};if(GetEnvironmentVariableW(L"CIRCLE_NRD_HISTORY",enabled,8)!=1||enabled[0]!=L'1')return;
        base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        struct Inline {std::uintptr_t at;void* replacement;void** original;};
        Inline sites[]={
            {0xc11790,(void*)init,(void**)&originalInit},{0xc12fe0,(void*)destroy,(void**)&originalDestroy},
            {0xc12580,(void*)begin,(void**)&originalBegin},{0xc12660,(void*)dispatch,(void**)&originalDispatch},
            {0xc17dd0,(void*)drawMain,(void**)&originalDraw[0]},
            {0xc18400,(void*)drawWater,(void**)&originalDraw[1]},
            {0xc18920,(void*)drawTransparent,(void**)&originalDraw[2]}};
        const std::uintptr_t settingSites[]={0xc17361,0xc17391,0xc173c3,0xc174c2,0xc174f5,0xc17527,0xc17574,0xc180e8,0xc182ef,0xc187d5,0xc1885f,0xc18ca2};
        #include "motor/nrd_sites.inl"
        for(auto s:sites)if(!build::rva(s.at))return;
        std::vector<safetyhook::InlineHook> hooks;
        std::vector<safetyhook::MidHook> mids;
        for(auto s:sites){auto h=safetyhook::InlineHook::create((void*)(base+build::rva(s.at)),s.replacement,safetyhook::InlineHook::StartDisabled);if(!h)return;*s.original=h->original<void*>();hooks.push_back(std::move(*h));}
        auto add=[&](std::uintptr_t at,void(*fn)(safetyhook::Context&)){
            if(!build::matches(at,"\xff\x15",2))return false;
            auto h=safetyhook::MidHook::create((void*)(base+build::rva(at)),fn,safetyhook::MidHook::StartDisabled);
            if(!h)return false;mids.push_back(std::move(*h));return true;
        };
        if(!add(0xc179d9,common))return;
        for(auto at:settingSites)if(!add(at,settings))return;
        for(auto& h:hooks)if(!h.enable())return;
        for(auto& h:mids)if(!h.enable())return;
        for(auto& h:hooks)new safetyhook::InlineHook(std::move(h));
        for(auto& h:mids)new safetyhook::MidHook(std::move(h));
        circlevr::vkcore::logf("motor-nrd: per-eye native integrations and camera histories installed (NRD 4.11); main/water/transparency; native checkerboard phase retained");
    });
}
}
