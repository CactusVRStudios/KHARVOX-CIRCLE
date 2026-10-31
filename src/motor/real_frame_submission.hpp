#pragma once
#include <cstdint>
namespace circle::motor {
// A complete, GPU-copied game image is eligible once per successful submission.
// Runtime heartbeat frames can still end empty after a bounded wait.
struct RealFrameSubmission {
    uint64_t submitted{};
    bool fresh(uint64_t copied) const { return copied && copied!=submitted; }
    bool wait(bool enabled,bool running,bool resizing,uint64_t copied,uint64_t elapsedMs) const {
        return enabled&&running&&!resizing&&!fresh(copied)&&elapsedMs<80;
    }
    void accept(uint64_t copied,bool hasGameLayer,bool succeeded){
        if(hasGameLayer&&succeeded)submitted=copied;
    }
};
}
