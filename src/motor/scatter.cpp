#include "motor/scatter.hpp"
#include "motor/gi.hpp"
#include "motor/scatter_history.hpp"
#include "motor/build_profile.hpp"
#include "motor/sequential.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <safetyhook.hpp>
#include <atomic>
#include <mutex>
#include <vector>

namespace circle::motor::scatter {
namespace {
std::uintptr_t base=0,device=0,stateOwner=0;
std::array<std::uintptr_t,4> engine{},right{};
MotorScatterHistory history;
std::mutex mutex;
bool ready=false;
std::atomic<std::uintptr_t> preparedState{};
std::atomic<unsigned> preparedCounter{};
unsigned generation=0;
template<class T>T get(std::uintptr_t p){T v{};SIZE_T n{};if(p)ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&v,sizeof(v),&n);return v;}
template<class T>void put(std::uintptr_t p,const T& v){memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));}
using Create=void*(*)(void*,const char*,const void*,int);
using Resize=bool(*)(void*,int,int,int,int);
std::array<circlevr::stereo_seq::ScatterPair,2> pairs(const std::array<std::uintptr_t,4>& v){
    return {{{reinterpret_cast<void*>(v[0]),reinterpret_cast<void*>(v[1])},
             {reinterpret_cast<void*>(v[2]),reinterpret_cast<void*>(v[3])}}};
}
bool dimensions(){
    const auto size=get<std::array<int,3>>(engine[0]+0xa4);
    if(size[0]<=0||size[1]<=0||size[2]<=0)return false;
    for(auto set:{engine,right})for(auto image:set)
        if(!image||get<std::array<int,3>>(image+0xa4)!=size)return false;
    return true;
}
void built(safetyhook::Context& r){
    // Last native volume is now stored at device+500; its aligned description
    // is still on the constructor's stack. Motor create has an extra usage arg.
    if(get<unsigned>(r.r15)!=0)return; // only the primary device context
    std::lock_guard lock(mutex);ready=false;preparedState=0;stateOwner=0;device=r.r15;
    for(unsigned i=0;i<4;++i)engine[i]=get<std::uintptr_t>(device+0x4e8+i*8);
    if(!engine[0]||!engine[1]||!engine[2]||!engine[3])return;
    ++generation;
    for(unsigned i=0;i<4;++i){
        char name[96]{};sprintf_s(name,"circleScatterEyeR_%u_%u",generation,i);
        right[i]=reinterpret_cast<std::uintptr_t>(reinterpret_cast<Create>(base+build::rva(0xbeb140))(
            nullptr,name,reinterpret_cast<const void*>(r.rbp+0x3d0),0x5b));
    }
    bool distinct=true;
    for(unsigned i=0;i<4;++i){for(auto left:engine)distinct&=right[i]!=left;
        for(unsigned j=0;j<i;++j)distinct&=right[i]!=right[j];}
    history.reset(pairs(engine),pairs(right));ready=distinct&&history.ready()&&dimensions();
    circlevr::vkcore::logf("motor-scatter-private: built context %p ready %d native %p/%p/%p/%p right %p/%p/%p/%p",
        reinterpret_cast<void*>(device),ready,reinterpret_cast<void*>(engine[0]),reinterpret_cast<void*>(engine[1]),reinterpret_cast<void*>(engine[2]),reinterpret_cast<void*>(engine[3]),
        reinterpret_cast<void*>(right[0]),reinterpret_cast<void*>(right[1]),reinterpret_cast<void*>(right[2]),reinterpret_cast<void*>(right[3]));
}
void resized(safetyhook::Context&){
    gi::resized();
    std::lock_guard lock(mutex);preparedState=0;
    if(!device||get<std::uintptr_t>(base+build::rva(0x4a729b8))!=device||!history.ready())return;
    ready=false;
    const auto resizedImage=get<std::uintptr_t>(device+0x500);
    if(!resizedImage)return;
    const auto size=get<std::array<int,3>>(resizedImage+0xa4);const auto mips=get<int>(resizedImage+0xb4);
    if(size[0]<=0||size[1]<=0||size[2]<=0||mips<=0)return;
    for(auto set:{engine,right})for(auto image:set)if(image&&image!=resizedImage)
        reinterpret_cast<Resize>(base+build::rva(0xdb8890))(reinterpret_cast<void*>(image),size[0],size[1],size[2],mips);
    history.invalidate();ready=dimensions();
    circlevr::vkcore::logf("motor-scatter-private: resize all eight %dx%dx%d mips %d valid %d",size[0],size[1],size[2],mips,ready);
}
void retireContext(std::uintptr_t context){
    std::lock_guard lock(mutex);if(context!=device)return;
    // Native destructor owns the four original references. Restore its slots;
    // private named images follow the image manager/device lifetime.
    for(unsigned i=0;i<4;++i)put(device+0x4e8+i*8,engine[i]);
    preparedState=0;ready=false;device=stateOwner=0;engine={};right={};
}
void setup(safetyhook::Context& r){
    preparedState=0;
    const auto context=get<std::uintptr_t>(r.rcx+0x88),view=get<std::uintptr_t>(r.rcx+0x20);
    const auto state=get<std::uintptr_t>(context+8);const auto counter=get<unsigned>(context+0x10);
    if(!context||!state||!view||get<int>(view+0x18f34)!=0)return;
    std::lock_guard lock(mutex);
    if(!ready||get<std::uintptr_t>(base+build::rva(0x4a729b8))!=device)return;
    if(stateOwner!=state){history.reset(pairs(engine),pairs(right));stateOwner=state;}
    const auto tag=circlevr::vkcore::seqTagForBackendFrame(counter+1u);
    const bool stereo=tag&&tag->tick&&circlevr::stereo_seq::drawsEyeView(*tag);
    const auto eye=stereo&&!sequential::monoVrEnabled()?tag->eye:circlevr::stereo_seq::Eye::Mono;
    const auto plan=history.before(eye,counter,get<MotorScatterState>(state+0x60));
    if(plan.load)put(state+0x60,*plan.load);
    for(unsigned i=0;i<2;++i){put(device+0x4e8+i*16,plan.slots[i].packed0);put(device+0x4f0+i*16,plan.slots[i].packed1);}
    if(stereo){preparedCounter=counter;preparedState.store(state,std::memory_order_release);}
    static unsigned counts[2]{};const auto e=eye==circlevr::stereo_seq::Eye::Right?1:0;const auto n=++counts[e];
    if(n<=8||n%1200==0)circlevr::vkcore::logf("motor-scatter-private: eye %d counter %u write %p/%p read %p/%p state %p restore %d restart %d sample %u",
        e,counter,plan.slots[counter&1].packed0,plan.slots[counter&1].packed1,plan.slots[(counter&1)^1].packed0,plan.slots[(counter&1)^1].packed1,reinterpret_cast<void*>(state),bool(plan.load),plan.restarted,n);
}
}
bool filtered(std::uintptr_t state,std::uint32_t counter){return preparedState.load(std::memory_order_acquire)==state&&state&&preparedCounter==counter;}
void retire(std::uintptr_t context){retireContext(context);}
void installEarly(){
    static std::once_flag once;std::call_once(once,[]{
        if(!sequential::requested()||!build::supported())return;
        wchar_t off[8]{};if(GetEnvironmentVariableW(L"CIRCLE_SCATTER_HISTORY",off,8)==1&&off[0]==L'0')return;
        base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        struct Site{std::uintptr_t at;const char* bytes;size_t n;void(*fn)(safetyhook::Context&);};
        const Site sites[]={
            {0xbc21f6,"\x48\x8d\x8d\xb0\x12\x00\x00",7,built},
            {0xcbb836,"\x48\x8b\x0d\x63\x67\xdb\x03",7,resized},
            {0xc42ad0,"\x48\x8b\xc4\x48\x89\x58\x08",7,setup}};
        for(const auto& s:sites)if(!build::matches(s.at,s.bytes,s.n))return;
        if(!build::matches(0xbeb140,"\x48\x89\x6c\x24\x18",5)||!build::matches(0xdb8890,"\x48\x89\x5c\x24\x08",5)||!build::rva(0x4a729b8))return;
        // Build all hooks disabled so partial installation cannot swizzle slots.
        std::vector<safetyhook::MidHook> hooks;
        for(const auto& s:sites){auto h=safetyhook::MidHook::create(reinterpret_cast<void*>(base+build::rva(s.at)),s.fn,safetyhook::MidHook::StartDisabled);if(!h)return;hooks.push_back(std::move(*h));}
        for(auto& h:hooks)if(!h.enable())return;
        for(auto& h:hooks)new safetyhook::MidHook(std::move(h));
        circlevr::vkcore::logf("motor-scatter-private: Motor allocation/setup/resize/retirement installed; donor per-eye volume history");
    });
}
}
