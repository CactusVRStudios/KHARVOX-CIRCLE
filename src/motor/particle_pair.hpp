#pragma once
#include <cstdint>

namespace circle::motor {
// Motor's simulation token is a timestamp and can repeat while paused.
// Ring ownership therefore also requires adjacent native render passes.
struct ParticlePair {
    uintptr_t world{}, ring{}, pool{};
    uint64_t token{};
    uint32_t render{}, ringFrame{};
    int slot{-1};
    bool left{}, enabled{};

    bool rightMatches(uintptr_t w, uintptr_t r, uintptr_t p, uint64_t t,
                      uint32_t f, uint32_t rf, int s) const {
        return enabled && left && world && ring && token && world == w &&
            ring == r && pool == p && token == t && f - render == 1u &&
            ringFrame == rf && slot >= 0 && slot < 3 && slot == s;
    }
    bool generatedHere(uint32_t stamp) const { return enabled && stamp == ringFrame; }
};
constexpr int previousParticleSlot(int slot) { return slot >= 0 && slot < 3 ? (slot + 2) % 3 : -1; }
enum class ParticleGeneration { Native, ReuseCpu, BindGpuAndGenerate };
constexpr ParticleGeneration particleGeneration(bool pairedRight, bool generatedByLeft, bool gpuOrUnknown) {
    if(!pairedRight || !generatedByLeft)return ParticleGeneration::Native;
    return gpuOrUnknown ? ParticleGeneration::BindGpuAndGenerate : ParticleGeneration::ReuseCpu;
}
}
