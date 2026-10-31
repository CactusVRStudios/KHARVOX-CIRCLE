#pragma once
#include <cstdint>
namespace circle::motor::low_performance {
// Called only for the game's first Vulkan instance, before device/resource creation.
void install();
// Read-only CPU recording counters. They do not suppress Vulkan commands.
void traceRecorded(bool indirect = false);
void buildsRecorded(std::uint32_t count);
void reportWorkload();
}
