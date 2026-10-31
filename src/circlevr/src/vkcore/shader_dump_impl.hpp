#pragma once

// Internals shared by shader_dump.cpp (creation-time hooks, files) and shader_dump_record.cpp (command
// recording, frames).

#include "vkcore/dump_format.hpp"

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace circlevr::vkcore::shader_dump {

#define CIRCLE_VR_DUMP_FUNCTIONS(X)                                                                                \
    X(CreateShaderModule)                                                                                    \
    X(DestroyShaderModule)                                                                                   \
    X(DestroyPipeline)                                                                                       \
    X(CreateGraphicsPipelines)                                                                               \
    X(CreateComputePipelines)                                                                                \
    X(CreatePipelineLayout)                                                                                  \
    X(CreateDescriptorSetLayout)                                                                             \
    X(AllocateDescriptorSets)                                                                                \
    X(UpdateDescriptorSets)                                                                                  \
    X(BeginCommandBuffer)                                                                                    \
    X(CmdBindPipeline)                                                                                       \
    X(CmdBindDescriptorSets)                                                                                 \
    X(CmdPushConstants)                                                                                      \
    X(CmdDraw)                                                                                               \
    X(CmdDrawIndexed)                                                                                        \
    X(CmdDrawIndirect)                                                                                       \
    X(CmdDrawIndexedIndirect)                                                                                \
    X(CmdDrawIndirectCount)                                                                                  \
    X(CmdDrawIndexedIndirectCount)                                                                           \
    X(CmdDispatch)                                                                                           \
    X(CmdDispatchIndirect)                                                                                   \
    X(CmdExecuteCommands)

#define CIRCLE_VR_DUMP_DECLARE_FN(name) PFN_vk##name name = nullptr;

struct DumpDevice {
    VkDevice device = VK_NULL_HANDLE;
    bool isGame = false;
    CIRCLE_VR_DUMP_FUNCTIONS(CIRCLE_VR_DUMP_DECLARE_FN)
};

#undef CIRCLE_VR_DUMP_DECLARE_FN

// The registered device for any handle dispatched through it (device, queue, command buffer).
DumpDevice* deviceOf(void* dispatchable);

enum class Stream { Pipelines, Layouts, Sets, DrawLog };

// Queues whole lines (each ending in '\n') for the background writer.
void write(Stream stream, std::string lines);

// The recording hooks of shader_dump_record.cpp, by Vulkan name (KHR aliases included), or nullptr.
PFN_vkVoidFunction findRecordHook(const char* name);

// Draw-log window state, owned by shader_dump_record.cpp.
bool recording();
std::uint64_t currentFrame();
void startRecording(std::uint32_t skip, std::uint32_t count);
void cacheCommandBegin(VkCommandBuffer cb);
VkPipeline cacheMotionPipeline(VkCommandBuffer cb,VkPipeline original);

} // namespace circlevr::vkcore::shader_dump
