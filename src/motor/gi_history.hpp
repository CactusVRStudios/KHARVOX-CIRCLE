#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace circle::motor {
// Spatial reconstruction visits every subpixel in each eye's own history.
// A shared alternating L/R render counter would only visit half of the LUT.
class GiSampleSequence {
    std::uint64_t tick{};
    std::uint32_t phase{};
    bool valid{};
public:
    void reset(){tick=0;phase=0;valid=false;}
    std::uint32_t before(std::uint64_t pairTick,bool stereo,std::uint32_t nativeCounter){
        if(!stereo||!pairTick){reset();return nativeCounter;}
        if(!valid||pairTick<tick){phase=0;valid=true;}
        else if(pairTick!=tick)++phase;
        tick=pairTick;return phase;
    }
};
using GiCameraHistory=std::array<std::byte,0x48>;
struct GiHistoryPlan {
    unsigned eye{},writeBank{};
    bool restart{};
    GiCameraHistory camera{};
    int cut{};
};
class GiHistory {
    struct State {GiCameraHistory camera{};int cut{};std::uint32_t counter{};unsigned bank{};bool valid{};};
    std::array<State,2> eyes{};
    std::optional<unsigned> active;
    std::optional<bool> stereoMode;
    GiHistoryPlan last{};
public:
    void reset(){eyes={};active.reset();stereoMode.reset();last={};}
    GiHistoryPlan before(unsigned eye,bool stereo,std::uint32_t counter,const GiCameraHistory& camera,int cut){
        eye=stereo?eye:0;
        if(active&&stereoMode&&*stereoMode==stereo&&*active==eye&&eyes[eye].counter==counter)return last;
        if(active){eyes[*active].camera=camera;eyes[*active].cut=cut;}
        if(stereoMode&&*stereoMode!=stereo){eyes={};active.reset();}
        auto& s=eyes[eye];
        const bool continuous=s.valid&&std::uint32_t(counter-s.counter)==(stereo?2u:1u);
        const unsigned bank=continuous?(s.bank^1u):0;
        last={eye,bank,!continuous,continuous?s.camera:camera,continuous?s.cut:2};
        s.counter=counter;s.bank=bank;s.valid=true;active=eye;stereoMode=stereo;
        return last;
    }
    // Every native bank slot must resolve to the same eye; native frame parity
    // is a binding convention, not the cadence of that eye's history.
    static unsigned bankForSlot(unsigned nativeSlot,unsigned nativeParity,unsigned writeBank){
        return nativeSlot==nativeParity?writeBank:(writeBank^1u);
    }
};
}
