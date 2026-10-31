#include "cinema_timing.hpp"
#include "build_profile.hpp"
#include "experimental.hpp"
#include "sequential.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <safetyhook.hpp>
#include <atomic>
#include <cmath>
namespace circle::motor::cinema_timing {
namespace {
std::atomic<ULONGLONG> lastScene{};
std::atomic<unsigned> refresh{};
thread_local bool selected=false;
bool fresh(){const auto t=lastScene.load();return t&&GetTickCount64()-t<250;}
void mode(safetyhook::Context& r){
    selected=false;
    int native=-1;SIZE_T n{};
    if(!ReadProcessMemory(GetCurrentProcess(),(void*)r.rbp,&native,sizeof(native),&n)||n!=sizeof(native))return;
    const bool cinema=fresh()&&!experimental::pauseMenuActive();
    // After the native mode==2 comparison, before its fixed-60 fallback.
    // Keep fixedTic, adaptiveTick, time-scale, frame-lock and clock eligibility
    // checks in the original function. Only a validated live cinema may join
    // the same wall-clock adaptive path as gameplay.
    selected=cinema&&refresh.load()>=60;
    const bool overrideMode=adaptiveCinema(selected,false,native,r.xmm8.f32[0]);
    if(overrideMode)r.rflags|=0x40;
    static std::atomic<ULONGLONG> last{};const auto now=GetTickCount64();auto previous=last.load();
    if(cinema&&now-previous>5000&&last.compare_exchange_strong(previous,now)){circlevr::vkcore::logf("motor-cinema-timing: native mode %d speed %.3f refresh %u adaptive override %d",native,r.xmm8.f32[0],refresh.load(),overrideMode);}
}
void rate(safetyhook::Context& r){
    // Native immediate adaptive path: clamp only its minimum frame duration,
    // leaving elapsed wall time and the animation/simulation delta intact.
    if(selected&&fresh()&&!experimental::pauseMenuActive()){
        const auto native=static_cast<unsigned>(r.rcx),target=targetRate(native,refresh.load());
        r.rcx=target;
        static std::atomic<ULONGLONG> last{};const auto now=GetTickCount64();auto before=last.load();
        if(now-before>5000&&last.compare_exchange_strong(before,now))
            circlevr::vkcore::logf("motor-cinema-rate: native adaptive maximum %u target %u; elapsed wall clock retained",native,target);
    }
}
}
void scene(bool eligible){lastScene=eligible?GetTickCount64():0;}
void displayPeriod(std::int64_t ns,double reportedHz){
 const double rate=std::isfinite(reportedHz)&&reportedHz>=60&&reportedHz<=240?reportedHz:(ns>0?1e9/double(ns):0);
 const auto hz=static_cast<unsigned>(std::llround(rate));refresh=hz>=60&&hz<=240?hz:0;
}
void install(){
    if(!sequential::requested()||!build::supported())return;
    if(!build::matches(0xeb302c,"\x0f\x85\xab\x05\x00\x00",6)||!build::matches(0xeb3120,"\xb8\x40\x42\x0f\x00\x48\xf7\xf1",8))return;
    const auto b=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto a=safetyhook::MidHook::create((void*)(b+build::rva(0xeb302c)),mode);
    auto c=safetyhook::MidHook::create((void*)(b+build::rva(0xeb3120)),rate);
    if(!a||!c)return;
    new safetyhook::MidHook(std::move(*a));new safetyhook::MidHook(std::move(*c));
    circlevr::vkcore::logf("motor-cinema-timing: native adaptive cinema path installed; real-time clock retained, refresh bound enabled");
}
}
