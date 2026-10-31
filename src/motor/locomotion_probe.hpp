#pragma once
#include "locomotion_policy.hpp"
#include <array>
namespace circle::motor::locomotion {
struct NativeSample {
    uintptr_t physics{},userCmd{},viewAngles{},controller{};
    uint64_t entity{},tick{},gameTime{};
    Vec position{},angles{};
    int movementType{},explicitReason{};
    unsigned movementFlags{};
    bool walking{},ground{},animated{},localSpace{};
    unsigned char movementMode{};
    std::array<unsigned char,48> command{};
    bool valid{},whipPrompt{};
};
void installProbe();
NativeSample latestNative();
bool objectInspectionActive();
// Native journal state: 0 closed, 1 raised Journal/Travels, 2 lowered Map.
// -1 means no fresh observation for the current player.
int journalState();
bool blowInteractionActive();
bool flatInteractionActive();
}
