#include "frame_generation.hpp"
#include "framegen/bridge.hpp"
#include "build_profile.hpp"
#include "sequential.hpp"
#include "vkcore/log.hpp"
#include <safetyhook.hpp>
#include <windows.h>
#include <atomic>

namespace circle::motor::frame_generation {
namespace {
std::atomic<bool> installed{};
std::atomic<int> requested{-1};
void gate(safetyhook::Context& r){
    if(!installed)return;
    int mode=-1;SIZE_T bytes{};
    if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(r.rax+8),&mode,sizeof(mode),&bytes)&&bytes==sizeof(mode)){
        const int previous=requested.exchange(mode);
        if(previous!=mode)circlevr::vkcore::logf("motor-fg: native request %d; %s; DLSS SR unchanged",mode,sequential::monoVrEnabled()?"MONO TEST: native eligibility/options preserved, generated XR delivery unverified":"STEREO: forced OFF, no paired interpolation");
    }
    // Both sites immediately follow cmp [cvar.internal+8],1 and precede JL.
    // Stereo uses Motor's existing off path, including slDLSSGSetOptions(eOff)
    // on its original thread. Mono preserves native flags at both sites.
    // No foreign API calls, cvar writes or fake eye tags.
    r.rflags=gateFlags(r.rflags,sequential::monoVrEnabled()&&!circle::fg::requested());
}
}
void install(){
    if(!sequential::requested()||!build::supported())return;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    // FG eligibility and its options/tag submission gate. Independent of AA.
    if(!build::matches(0xd0b4cd,"\x7c\x17\xb0\x01\x48\x8b\x4c\x24\x70",9)||
       !build::matches(0xd0b624,"\x0f\x8c\x27\x04\x00\x00",6)){
        circlevr::vkcore::logf("motor-fg: guard fingerprint rejected; keep native FG disabled manually");return;
    }
    auto a=safetyhook::MidHook::create(reinterpret_cast<void*>(base+build::rva(0xd0b4cd)),gate);
    auto b=safetyhook::MidHook::create(reinterpret_cast<void*>(base+build::rva(0xd0b624)),gate);
    if(!a||!b){circlevr::vkcore::logf("motor-fg: guard installation failed");return;}
    new safetyhook::MidHook(std::move(*a));new safetyhook::MidHook(std::move(*b));
    installed=true;
    circlevr::vkcore::logf("motor-fg: native eligibility/options observation installed; %s",sequential::monoVrEnabled()?"MONO TEST: no FG suppression; native checks retained; OpenXR support unverified":"STEREO: FG forced OFF");
}
const char* status(){
    if(!installed)return "FG guard unavailable";
    if(circle::fg::requested())return "Desktop FG off (integrated VR FG requested)";
    const int mode=requested.load();
    if(sequential::monoVrEnabled())return mode>0?"Requested; untracked images excluded":mode==0?"Off (mono test available)":"Waiting for native FG state";
    return mode>0?"FG requested; disabled for VR":mode==0?"Off (VR guard active)":"Waiting for native FG state";
}
bool guardInstalled(){return installed.load();}
}
