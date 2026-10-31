// A small stability check for the donor's feature-chain edit: keep Motor's mandatory RT enabled.
#include "vkcore/device_features.hpp"
#include <iostream>
#include <stdexcept>

static void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
int main() {
    using Chain = circlevr::vkcore::TimelineFeatureChain;
    VkPhysicalDeviceVulkan12Features core{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    core.bufferDeviceAddress = VK_TRUE;
    VkPhysicalDeviceRayQueryFeaturesKHR query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,&core};
    query.rayQuery = VK_TRUE;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rays{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,&query};
    rays.rayTracingPipeline = VK_TRUE;
    rays.rayTracingPipelineTraceRaysIndirect = VK_TRUE;
    rays.rayTraversalPrimitiveCulling = VK_TRUE;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,&rays};
    acceleration.accelerationStructure = VK_TRUE;
    acceleration.accelerationStructureIndirectBuild = VK_TRUE;
    acceleration.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;
    Chain plan;
    check(plan.enable(&acceleration)==Chain::Result::Merged,"Timeline merge failed");
    const auto* a=static_cast<const VkPhysicalDeviceAccelerationStructureFeaturesKHR*>(plan.head());
    const auto* r=static_cast<const VkPhysicalDeviceRayTracingPipelineFeaturesKHR*>(a->pNext);
    const auto* q=static_cast<const VkPhysicalDeviceRayQueryFeaturesKHR*>(r->pNext);
    const auto* c=static_cast<const VkPhysicalDeviceVulkan12Features*>(q->pNext);
    check(a->accelerationStructure && a->accelerationStructureIndirectBuild && a->descriptorBindingAccelerationStructureUpdateAfterBind,"AS features lost");
    check(r->rayTracingPipeline && r->rayTracingPipelineTraceRaysIndirect && r->rayTraversalPrimitiveCulling && q->rayQuery,"RT features lost");
    check(c->timelineSemaphore && c->bufferDeviceAddress,"Core features lost");
    check(!core.timelineSemaphore && acceleration.pNext==&rays && rays.pNext==&query && query.pNext==&core,"Original game chain modified");
    core.timelineSemaphore=VK_TRUE;
    check(plan.enable(&acceleration)==Chain::Result::AlreadyEnabled && plan.head()==&acceleration,"Existing feature chain was replaced");
    core.timelineSemaphore=VK_FALSE;
    VkBaseOutStructure unknown{VK_STRUCTURE_TYPE_MAX_ENUM,reinterpret_cast<VkBaseOutStructure*>(&acceleration)};
    check(plan.enable(&unknown)==Chain::Result::Unsupported && plan.head()==&unknown,"Unknown structure did not fail closed");
    std::cout<<"PASS: RT pipeline, ray query, AS and buffer device address preserved; original inputs unchanged; unknown chain fails closed.\n";
}
