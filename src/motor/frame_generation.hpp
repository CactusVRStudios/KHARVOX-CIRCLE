#pragma once
#include <cstdint>
namespace circle::motor::frame_generation {
// Stereo still requires the native off path. Mono VR permits an experimental
// native FG test; passing the original flags preserves native eligibility.
constexpr std::uintptr_t nativeOffFlags(std::uintptr_t flags){return (flags&~std::uintptr_t(0x800))|0x80;}
constexpr std::uintptr_t gateFlags(std::uintptr_t flags,bool monoVr){return monoVr?flags:nativeOffFlags(flags);}
void install();
bool guardInstalled();
const char* status();
}
