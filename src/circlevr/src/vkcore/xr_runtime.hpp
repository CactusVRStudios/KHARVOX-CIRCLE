#pragma once

// The OpenXR runtime's name, set once when the presenter creates its instance and read by the input side
// (the control map's runtime tweaks, features/input/dashboard_pause.hpp). KHARVOX_CIRCLE_TEST_RUNTIME_NAME
// stands in for it in rig tests (a simulator run that behaves as SteamVR).

#include <string>

namespace circlevr::vkcore {

// Keeps `name`, or KHARVOX_CIRCLE_TEST_RUNTIME_NAME when that is set (logged).
void setXrRuntimeName(const char* name);

// Empty until the instance exists.
std::string xrRuntimeName();

} // namespace circlevr::vkcore
