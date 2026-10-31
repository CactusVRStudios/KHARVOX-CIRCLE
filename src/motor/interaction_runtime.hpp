#pragma once
#include "experimental.hpp"
#include "locomotion_runtime.hpp"
#include "controller_tracking.hpp"
#include "locomotion_probe.hpp"
#include "focus_prompt.hpp"
namespace circle::motor::interaction {
void install();
void observeMagnifier(uint64_t owner);
bool magnifierActive();
void publishView(const std::array<experimental::LiveEye,2>&,const locomotion::CameraMotion&);
unsigned nearHands(const hands::Snapshot&,const locomotion::NativeSample&);
FocusActions focusActions(const locomotion::NativeSample&);
}
