#pragma once
#include <array>
#include <cstdint>
namespace circle::motor {
// Each eye normally reappears after two backend renders. eyeSeq alone cannot
// detect a menu gap for R, because only mono/L's sequence advances in a menu.
struct TemporalContinuity {
    struct Last {std::uint32_t sequence=0,backend=0;bool valid=false;};
    std::array<Last,2> last{};
    void clear(){last={};}
    bool reset(unsigned eye,std::uint32_t sequence,std::uint32_t backend,unsigned stride=2){
        auto& l=last[eye];
        const bool needed=!l.valid || sequence-l.sequence!=1u || backend-l.backend!=stride;
        l={sequence,backend,true};return needed;
    }
};
}
