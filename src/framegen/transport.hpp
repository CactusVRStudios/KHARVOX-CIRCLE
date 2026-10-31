#pragma once
#include "bridge.hpp"
#include "policy.hpp"
#include "vkcore/dispatch.hpp"
#include <d3d12.h>
#include <wrl/client.h>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <atomic>
namespace circle::fg {
struct SharedImage {VkImage image{};VkDeviceMemory memory{};Microsoft::WRL::ComPtr<ID3D12Resource> dx;};
struct Input {
 Capture capture{};
 SharedImage depth,motion;
 VkDescriptorPool descriptors{};
 VkDescriptorSet set{};
 std::array<VkImageView,4> views{};
 VkEvent converted{};
 std::atomic<uint64_t> produced{},consumed{};
};
using InputPtr=std::shared_ptr<Input>;
class Transport:public std::enable_shared_from_this<Transport> {
 circlevr::vkcore::DeviceData& dev;
 Microsoft::WRL::ComPtr<ID3D12Device> device;
 Microsoft::WRL::ComPtr<ID3D12Fence> producer,consumer;
 std::mutex mutex;
 bool stopped{};
 VkDescriptorSetLayout setLayout{};
 VkPipelineLayout pipelineLayout{};
 VkPipeline pipeline{};
 VkSampler sampler{};
 std::vector<InputPtr> pool;
 std::unordered_map<uint32_t,InputPtr> pending;
 bool make(SharedImage&,const Resource&);
 bool initializePipeline();
 bool prepareDescriptors(Input&);
 void record(const Capture&);
public:
 Transport(circlevr::vkcore::DeviceData&,ID3D12Device*,ID3D12Fence*,ID3D12Fence*);
 void connect();
 void stop();
 // Only after device idle AND the D3D consumer fence, same as presenter ring.
 void destroyIdle();
 InputPtr take(const circlevr::stereo_seq::RenderTag&,uint64_t producerValue);
};
}
