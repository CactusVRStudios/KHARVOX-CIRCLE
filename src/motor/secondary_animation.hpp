#pragma once
#include <cstdint>

namespace circle::motor {
// Both native surface builders branch over the secondary-deformation flag
// when r_skipSecondaryAnimation != 0. Select that same branch for owned VR
// renders without changing the cvar or the primary skeletal animation.
constexpr uintptr_t secondaryAnimationFlags(uintptr_t flags,bool pairedVr,bool nativeComparison=false){
    return pairedVr&&!nativeComparison ? flags&~uintptr_t{0x40} : flags;
}
}
