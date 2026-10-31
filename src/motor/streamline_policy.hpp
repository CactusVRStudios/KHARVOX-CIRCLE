#pragma once
#include "temporal_continuity.hpp"
#include <array>
#include <cstdint>
namespace circle::motor {
// Motor's native handles are 1 (main) and 2 (secondary RR), confirmed live.
// Keep both intact; right main SR/RR uses 3, secondary RR uses 4.
inline constexpr unsigned rightSrViewport=3;
inline constexpr unsigned rrFeature=1001;
constexpr bool supportedStreamlinePass(unsigned feature,unsigned viewport){
    return (feature==0&&viewport==1)||(feature==rrFeature&&viewport>=1&&viewport<=2);
}
constexpr unsigned eyeViewport(unsigned nativeViewport,unsigned eye){return nativeViewport+2*eye;}
constexpr unsigned streamlineResourceSlot(unsigned feature,unsigned viewport){return feature==0?0:viewport;}
struct StreamlineConstantKey {
    std::uintptr_t token{};std::uint64_t tick{};unsigned backend{},eye{};
    bool operator==(const StreamlineConstantKey&)const=default;
};
constexpr bool reusableStreamlineConstants(bool valid,StreamlineConstantKey saved,StreamlineConstantKey requested){
    return valid&&requested.token&&saved==requested;
}
// Retry only diagnosed duplicate-constants or size transitions, never an arbitrary
// driver failure. A different simulation AND native token are required.
struct StreamlineTransitionRecovery {
    std::uintptr_t failedToken{};
    std::uint64_t failedTick{};
    unsigned attempts{},successes{};
    bool pending{};
    void transient(std::uintptr_t token,std::uint64_t tick){
        failedToken=token;failedTick=tick;pending=token&&tick&&attempts<3;successes=0;
    }
    bool retry(std::uintptr_t token,std::uint64_t tick){
        if(!pending||!token||token==failedToken||!tick||tick==failedTick)return false;
        pending=false;++attempts;return true;
    }
    void success(){if(++successes>=240){attempts=0;pending=false;successes=240;}}
    void cancel(){pending=false;}
};
struct StreamlineResizeGrace {
    unsigned misses{};
    bool defer(){return ++misses<=4;}
    void succeeded(){misses=0;}
};
struct StreamlineHistory {
    TemporalContinuity continuity;
    struct Entry {unsigned width{},height{};std::uint64_t epoch{};};
    std::array<Entry,2> sizes{};
    std::uint64_t epoch=1;
    void invalidate(){++epoch;}
    bool reset(unsigned eye,unsigned sequence,unsigned backend,unsigned w,unsigned h,bool nativeReset,unsigned stride=2){
        auto& e=sizes[eye];
        const bool discontinuous=continuity.reset(eye,sequence,backend,stride);
        const bool changed=e.width!=w||e.height!=h||e.epoch!=epoch;
        // A native cut on L must also invalidate R, even if Motor clears its
        // global reset bit after the first render of the pair.
        if(nativeReset){++epoch;}
        e={w,h,epoch};
        return nativeReset||discontinuous||changed;
    }
};
}
