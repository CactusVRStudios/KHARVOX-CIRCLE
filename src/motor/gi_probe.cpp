#include "motor/gi_probe.hpp"
#include "motor/build_profile.hpp"
#include "motor/sequential.hpp"
#include "motor/diagnostics.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <safetyhook.hpp>
#include <array>
#include <mutex>
#include <unordered_map>

namespace circle::motor::gi_probe {
namespace {
std::uintptr_t base{};
bool forced{};
std::mutex mutex;
template<class T>T get(std::uintptr_t address){
    T value{};SIZE_T read{};
    if(address)ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&read);
    return value;
}
struct Owner { unsigned counter{}; int eye{-1}; };
std::unordered_map<std::uintptr_t,Owner> submitted;
std::uintptr_t lastDevice{},lastState{};
unsigned calls[3]{};
// Observes the actual temporal-filter consumer. Does not modify registers,
// buffers, engine state, camera history or cvars. "priorEye" identifies the
// last observed filter submission, not GPU completion.
void filter(safetyhook::Context& r){
    if(!forced&&!diagnostics::enabled())return;
    const auto device=get<std::uintptr_t>(base+build::rva(0x4a729b8));
    const auto state=get<std::uintptr_t>(r.rbp-0x29);
    if(!device||!state||state!=get<std::uintptr_t>(device+0x28))return;
    const auto counter=get<unsigned>(state+0x274);
    const auto tag=circlevr::vkcore::seqTagForBackendFrame(counter+1u);
    const bool stereo=tag&&tag->tick&&circlevr::stereo_seq::drawsEyeView(*tag);
    const int eye=stereo?(tag->eye==circlevr::stereo_seq::Eye::Right?1:0):-1;
    const auto parity=counter&1u;
    const auto write=get<std::array<std::uintptr_t,4>>(device+0x828+parity*32);
    const auto history=get<std::array<std::uintptr_t,4>>(device+0x828+(parity^1u)*32);
    const auto blend=get<float>(r.rax+0xc); // selected normal/camera-cut cvar
    std::lock_guard lock(mutex);
    if(device!=lastDevice||state!=lastState){submitted.clear();lastDevice=device;lastState=state;}
    const auto it=submitted.find(history[0]);
    const Owner prior=it==submitted.end()?Owner{}:it->second;
    const unsigned sample=++calls[eye+1];
    if(sample<=12||sample%600==0)
        circlevr::vkcore::logf("motor-gi-probe: eye %d counter %u parity %u state %p blend %.4f cut %d write %p/%p/%p/%p history %p/%p/%p/%p priorEye %d priorCounter %u sample %u",
            eye,counter,parity,(void*)state,blend,get<int>(state+0x278),
            (void*)write[0],(void*)write[1],(void*)write[2],(void*)write[3],
            (void*)history[0],(void*)history[1],(void*)history[2],(void*)history[3],prior.eye,prior.counter,sample);
    if(write[0])submitted[write[0]]={counter,eye};
}
}
void installEarly(){
    static std::once_flag once;std::call_once(once,[]{
        wchar_t enabled[8]{};
        forced=GetEnvironmentVariableW(L"CIRCLE_GI_HISTORY_PROBE",enabled,8)==1&&enabled[0]==L'1';
        if(!sequential::requested()||!build::supported())return;
        base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        #include "motor/gi_probe_sites.inl"
        auto hook=safetyhook::MidHook::create(reinterpret_cast<void*>(base+build::rva(0xbffb96)),filter);
        if(!hook)return;
        new safetyhook::MidHook(std::move(*hook));
        circlevr::vkcore::logf("motor-gi-probe: read-only final-gather temporal-filter observation installed");
    });
}
}
