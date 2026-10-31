#pragma once
#include "stereo_seq/scatter_history.hpp"
#include <cstring>

namespace circle::motor {
// Motor extends Eternal's state at +0x60..+0x94 through +0xB0:
// camera position +90/94/98, reset count +9C, blend +A0, volume owner +A8.
// Keep the donor's resource/parity/clear planner and retain that extra tail.
using MotorScatterState=std::array<std::byte,0x50>;
struct MotorScatterPlan {
    std::array<circlevr::stereo_seq::ScatterPair,2> slots{};
    std::optional<MotorScatterState> load;
    bool restarted=false;
};
class MotorScatterHistory {
    circlevr::stereo_seq::ScatterHistory donor;
    std::array<std::optional<MotorScatterState>,2> states{};
    unsigned active=0;
    std::optional<std::uint32_t> lastCounter;
    MotorScatterPlan lastPlan;
public:
    void reset(const std::array<circlevr::stereo_seq::ScatterPair,2>& left,
               const std::array<circlevr::stereo_seq::ScatterPair,2>& right){
        donor.reset(left,right);states={};active=0;lastCounter.reset();lastPlan={};
    }
    void invalidate(){donor.invalidate();lastCounter.reset();}
    bool ready()const{return donor.ready();}
    MotorScatterPlan before(circlevr::stereo_seq::Eye eye,std::uint32_t counter,const MotorScatterState& current){
        const unsigned e=eye==circlevr::stereo_seq::Eye::Right?1:0;
        // Repeated setup for the same native view must not flip its images twice.
        if(lastCounter&&*lastCounter==counter&&active==e){auto p=lastPlan;p.load.reset();return p;}
        states[active]=current;
        circlevr::stereo_seq::ScatterState prefix{};
        memcpy(prefix.data(),current.data(),prefix.size());
        const auto p=donor.beforeRender(eye,counter,prefix);
        MotorScatterPlan result{p.slots,{},p.restarted};
        if(p.load){result.load=states[e].value_or(current);memcpy(result.load->data(),p.load->data(),p.load->size());}
        active=e;lastCounter=counter;lastPlan=result;return result;
    }
};
}
