#pragma once
#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

namespace circlevr::vkcore { struct DeviceData; }
namespace circle::fg { struct Capture; struct Resource; }
namespace circle::motor::gpu_audit {
// Explicit development launch only. Normal launches do not install Vulkan hooks.
bool enabled();
bool recording();
void projection(std::uint64_t token,unsigned frame,int eye,const void* viewBytes,unsigned size);
bool temporalEnabled();
void temporalImages(const circle::fg::Capture&, const circle::fg::Resource& inputColor,
                    const circle::fg::Resource& outputColor, bool after, int result);
PFN_vkVoidFunction findHook(const char*);
void deviceCreated(circlevr::vkcore::DeviceData&, PFN_vkVoidFunction (*nextHook)(const char*));
void deviceDestroyed(VkDevice);
void poll();
void placement(std::uint64_t token,unsigned frame,int eye,std::uintptr_t model,
               const char* status,float positionDelta,float axisDelta);
// CPU ownership metadata complements, but never substitutes for, GPU payloads.
void npc(std::uint64_t token, unsigned frame, int eye, std::uintptr_t model,
         std::uintptr_t owner, unsigned index, int joint, const char* asset,
         const char* instance, std::uint64_t flags, const void* surfaceBits,
         unsigned surfaceCount, const float* matrix, const float* previousMatrix,
         const void* skinBytes, unsigned skinSize);
void joints(std::uint64_t token, unsigned frame, int eye, std::uintptr_t source,
            int current, int previous, const void* descriptor, unsigned size,
            bool reused, int slot, std::uintptr_t caller);
}
