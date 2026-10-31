#include "motor/gi.hpp"
#include "motor/gi_history.hpp"
#include "motor/build_profile.hpp"
#include "motor/sequential.hpp"
#include "motor/diagnostics.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <safetyhook.hpp>
#include <mutex>
#include <vector>

namespace circle::motor::gi {
namespace {
std::uintptr_t base{},device{},owner{};
constexpr unsigned offsets[]={0x7f8,0x800,0x808,0x810,0x818,0x820,
    0x828,0x830,0x838,0x840,0x848,0x850,0x858,0x860};
std::array<std::array<std::uintptr_t,14>,2> images{};
GiHistory history;
GiSampleSequence sampleSequence;
unsigned sampleCounter{},samplePhase{};
bool sampleStereo{};
std::mutex mutex;
bool ready=false;
unsigned generation{};
template<class T>T get(std::uintptr_t p){T v{};SIZE_T n{};if(p)ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&v,sizeof(v),&n);return v;}
template<class T>void put(std::uintptr_t p,const T& v){memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));}
using Create=void*(*)(void*,const char*,const void*,int);
using Resize=bool(*)(void*,int,int,int,int);
bool valid(){
    const auto size=get<std::array<int,3>>(images[0][0]+0xa4);
    if(size[0]<=0||size[1]<=0||size[2]<=0)return false;
    for(unsigned e=0;e<2;++e)for(unsigned i=0;i<14;++i){
        const auto image=images[e][i];
        if(!image||get<std::array<int,3>>(image+0xa4)!=size)return false;
        for(unsigned prior=0;prior<e*14+i;++prior)if(images[prior/14][prior%14]==image)return false;
    }
    return true;
}
void restore(){if(device)for(unsigned i=0;i<14;++i)if(images[0][i])put(device+offsets[i],images[0][i]);}
void allocate(safetyhook::Context& r,unsigned index){
    if(get<unsigned>(r.r15)!=0)return;
    std::lock_guard lock(mutex);
    if(index==6){ready=false;owner=0;device=r.r15;images={};history.reset();sampleSequence.reset();sampleStereo=false;++generation;}
    if(device!=r.r15||index>=14)return;
    images[0][index]=r.rax;
    char name[96]{};sprintf_s(name,"circleGiEyeR_%u_%u",generation,index);
    images[1][index]=reinterpret_cast<std::uintptr_t>(reinterpret_cast<Create>(base+build::rva(0xbeb140))(
        nullptr,name,reinterpret_cast<void*>(r.rbp+0x220),0x5e));
    if(index==5){
        ready=valid();
        circlevr::vkcore::logf("motor-gi-private: allocated 14 right history images device %p valid %d SH %p/%p vs %p/%p",(void*)device,ready,(void*)images[0][6],(void*)images[0][10],(void*)images[1][6],(void*)images[1][10]);
    }
}
void sh0(safetyhook::Context& r){if(r.rdi<4)allocate(r,6+unsigned(r.rdi));}
void sh1(safetyhook::Context& r){if(r.rdi<4)allocate(r,10+unsigned(r.rdi));}
void count0(safetyhook::Context& r){allocate(r,0);} void count1(safetyhook::Context& r){allocate(r,1);}
void depth0(safetyhook::Context& r){allocate(r,2);} void depth1(safetyhook::Context& r){allocate(r,3);}
void normal0(safetyhook::Context& r){allocate(r,4);} void normal1(safetyhook::Context& r){allocate(r,5);}
void setup(safetyhook::Context& r){
    const auto state=r.r15,view=r.r13;
    if(!view||get<int>(view+0x18f34)!=0)return;
    std::lock_guard lock(mutex);
    if(!ready||get<std::uintptr_t>(base+build::rva(0x4a729b8))!=device||get<std::uintptr_t>(device+0x28)!=state)return;
    if(!valid()){restore();ready=false;circlevr::vkcore::logf("motor-gi-private: dimension mismatch; restored original slots");return;}
    if(owner!=state){history.reset();sampleSequence.reset();owner=state;}
    const auto counter=get<unsigned>(device+0x1b8);
    const auto tag=circlevr::vkcore::seqTagForBackendFrame(counter+1u);
    const bool stereo=tag&&tag->tick&&circlevr::stereo_seq::drawsEyeView(*tag);
    const unsigned eye=stereo&&tag->eye==circlevr::stereo_seq::Eye::Right?1:0;
    sampleStereo=stereo&&!sequential::monoVrEnabled();sampleCounter=counter;
    samplePhase=sampleSequence.before(tag?tag->tick:0,sampleStereo,counter);
    if(diagnostics::enabled()) {
        static unsigned samples[2]{};const auto sample=++samples[eye];
        if(sample<=8||sample%600==0) {
            circlevr::vkcore::logf("motor-gi-view-probe: eye %u backend %u tick %llu view %p state %p currentP00 %.6f currentP02 %.6f inverseP03 %.6f previousP02 %.6f sample %u",
                eye,counter,tag?tag->tick:0,(void*)view,(void*)state,
                get<float>(view+0x18790),get<float>(view+0x18798),get<float>(view+0x187dc),get<float>(view+0x18818),sample);
        }
    }
    const auto plan=history.before(eye,stereo&&!sequential::monoVrEnabled(),counter,get<GiCameraHistory>(state+0x200),get<int>(state+0x278));
    put(state+0x200,plan.camera);put(state+0x278,plan.cut);
    // Native discontinuity detection expects adjacent renders. Our planner
    // already validates per-eye gaps; do not falsely reset every second frame.
    put(state+0x274,counter-1u);
    for(unsigned slot=0;slot<2;++slot){
        const auto bank=GiHistory::bankForSlot(slot,counter&1u,plan.writeBank);
        for(unsigned pair=0;pair<3;++pair)put(device+offsets[pair*2+slot],images[eye][pair*2+bank]);
        for(unsigned sh=0;sh<4;++sh)put(device+offsets[6+slot*4+sh],images[eye][6+bank*4+sh]);
    }
    static unsigned counts[2]{};const auto n=++counts[eye];
    if(n<=8||n%600==0)circlevr::vkcore::logf("motor-gi-private: eye %u backend %u stereo %d bank %u restart %d cut %d sample %u",eye,counter,stereo,plan.writeBank,plan.restart,plan.cut,n);
}
void spatialSample(safetyhook::Context& r){
    // Only replace the LUT index register; state+274 remains the native
    // counter for bank selection, discontinuity detection and world GI jobs.
    std::lock_guard lock(mutex);
    if(!ready||!sampleStereo||r.r15!=owner||unsigned(r.rax)!=sampleCounter||
       get<unsigned>(owner+0x274)!=sampleCounter||diagnostics::nativeGiSamplePhase().load())return;
    r.rax=samplePhase;
    static unsigned samples{};const auto n=++samples;
    if(n<=16||(diagnostics::enabled()&&n%600==0))
        circlevr::vkcore::logf("motor-gi-sample-phase: backend %u native phase %u -> pair phase %u sample %u",sampleCounter,sampleCounter&3u,samplePhase&3u,n);
}
}
void resized(){
    std::lock_guard lock(mutex);
    if(!device||!images[1][13]||get<std::uintptr_t>(base+build::rva(0x4a729b8))!=device)return;
    ready=false;
    // Called after native global resizing: its currently bound history image
    // holds the new GI resolution. Resize both physical banks of both eyes.
    const auto source=get<std::uintptr_t>(device+0x828);
    const auto size=get<std::array<int,3>>(source+0xa4);const auto mips=get<int>(source+0xb4);
    if(size[0]>0&&size[1]>0&&size[2]>0&&mips>0){
        for(auto set:images)for(auto image:set)if(image&&image!=source)
            reinterpret_cast<Resize>(base+build::rva(0xdb8890))((void*)image,size[0],size[1],size[2],mips);
        history.reset();sampleSequence.reset();sampleStereo=false;ready=valid();
    }
    if(!ready)restore();
    circlevr::vkcore::logf("motor-gi-private: resize all 28 %dx%dx%d valid %d",size[0],size[1],size[2],ready);
}
void retire(std::uintptr_t context){
    std::lock_guard lock(mutex);if(context!=device)return;
    restore();ready=false;device=owner=0;images={};history.reset();sampleSequence.reset();sampleStereo=false;
}
void installEarly(){
    static std::once_flag once;std::call_once(once,[]{
        wchar_t enabled[8]{};if(GetEnvironmentVariableW(L"CIRCLE_GI_HISTORY",enabled,8)!=1||enabled[0]!=L'1')return;
        if(!sequential::requested()||!build::supported())return;
        base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        #include "motor/gi_sites.inl"
        struct Site{std::uintptr_t at;void(*fn)(safetyhook::Context&);};
        const Site sites[]={{0xbc3c3e,sh0},{0xbc3ca6,sh1},{0xbc3d7b,count0},{0xbc3dc2,count1},
            {0xbc3e13,depth0},{0xbc3e5a,depth1},{0xbc3eab,normal0},{0xbc3ef2,normal1},{0xbfccf9,setup},
            {0xbfd27c,spatialSample},{0xbfd29c,spatialSample}};
        std::vector<safetyhook::MidHook> hooks;
        for(auto s:sites){auto h=safetyhook::MidHook::create((void*)(base+build::rva(s.at)),s.fn,safetyhook::MidHook::StartDisabled);if(!h)return;hooks.push_back(std::move(*h));}
        for(auto& h:hooks)if(!h.enable())return;
        for(auto& h:hooks)new safetyhook::MidHook(std::move(h));
        circlevr::vkcore::logf("motor-gi-private: experimental final-gather image/camera isolation installed");
    });
}
}
