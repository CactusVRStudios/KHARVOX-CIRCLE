#pragma once
#include <openxr/openxr.h>
#include <array>
#include <cstdint>
namespace circle::motor::hands {
struct Hand {
 XrPosef grip{},aim{}; XrVector3f velocity{}; XrVector2f stick{};
 float trigger{},squeeze{}; bool lower{},upper{},click{},menu{};
 bool tracked{},aimTracked{},velocityValid{},thumbRest{};
};
struct Snapshot {std::array<Hand,2> hands{};XrTime time{};uint64_t tick{},sequence{},session{};bool focused{};};
bool attach(PFN_xrGetInstanceProcAddr,XrInstance,XrSession,XrSpace,bool touchPlus);
void sync(XrTime,bool focused);
void detach();
Snapshot latest();
Snapshot atTime(XrTime);
// Called only by a verified native event adapter. Queued for the XR thread;
// stale/tracking-lost requests are discarded, never replayed after focus loss.
bool requestGameRumble(uint16_t low, uint16_t high);
bool requestHaptic(unsigned hand,float amplitude,uint32_t durationMs);
}
