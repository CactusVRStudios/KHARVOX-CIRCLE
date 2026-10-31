#include "transport.hpp"
#include "vkcore/log.hpp"
#include <algorithm>
#include "inputs_spv.hpp"
namespace circle::fg {
namespace {
DXGI_FORMAT format(uint32_t f){switch(f){
 case VK_FORMAT_R32_SFLOAT:return DXGI_FORMAT_R32_FLOAT;
 case VK_FORMAT_R16G16_SFLOAT:return DXGI_FORMAT_R16G16_FLOAT;
 case VK_FORMAT_R32G32_SFLOAT:return DXGI_FORMAT_R32G32_FLOAT;
 default:return DXGI_FORMAT_UNKNOWN;}}
bool compatible(const Resource& a,const Resource& b){return a.format==b.format&&a.width==b.width&&a.height==b.height;}
}
Transport::Transport(circlevr::vkcore::DeviceData& d,ID3D12Device* dx,ID3D12Fence* p,ID3D12Fence* c):dev(d),device(dx),producer(p),consumer(c){}
void Transport::connect(){std::weak_ptr<Transport> weak=shared_from_this();setCapture([weak](const Capture& c){if(auto self=weak.lock())self->record(c);});}
void Transport::stop(){setCapture({});std::lock_guard lock(mutex);stopped=true;}
void Transport::destroyIdle(){std::lock_guard lock(mutex);pending.clear();for(auto& p:pool){
 for(auto view:p->views)if(view)dev.vk.DestroyImageView(dev.device,view,nullptr);
 if(p->descriptors)dev.vk.DestroyDescriptorPool(dev.device,p->descriptors,nullptr);
 if(p->converted)dev.vk.DestroyEvent(dev.device,p->converted,nullptr);
 for(auto* i:{&p->depth,&p->motion}){
 if(i->image)dev.vk.DestroyImage(dev.device,i->image,nullptr);if(i->memory)dev.vk.FreeMemory(dev.device,i->memory,nullptr);i->image={};i->memory={};i->dx.Reset();}}
 pool.clear();
 if(pipeline)dev.vk.DestroyPipeline(dev.device,pipeline,nullptr);
 if(pipelineLayout)dev.vk.DestroyPipelineLayout(dev.device,pipelineLayout,nullptr);
 if(setLayout)dev.vk.DestroyDescriptorSetLayout(dev.device,setLayout,nullptr);
 if(sampler)dev.vk.DestroySampler(dev.device,sampler,nullptr);
 pipeline={};pipelineLayout={};setLayout={};sampler={};}
bool Transport::initializePipeline(){
 if(pipeline)return true;
 if(!dev.fgExtendedStorage){state("fallback","Vulkan extended storage image formats not enabled",true);return false;}
 VkDescriptorSetLayoutBinding bindings[4]{};
 for(unsigned i=0;i<4;++i){bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
 VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};si.bindingCount=4;si.pBindings=bindings;
 if(!setLayout&&dev.vk.CreateDescriptorSetLayout(dev.device,&si,nullptr,&setLayout)!=VK_SUCCESS)return false;
 VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=1;li.pSetLayouts=&setLayout;
 if(!pipelineLayout&&dev.vk.CreatePipelineLayout(dev.device,&li,nullptr,&pipelineLayout)!=VK_SUCCESS)return false;
 VkSamplerCreateInfo sa{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sa.magFilter=sa.minFilter=VK_FILTER_NEAREST;sa.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sa.addressModeU=sa.addressModeV=sa.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sa.maxLod=0;
 if(!sampler&&dev.vk.CreateSampler(dev.device,&sa,nullptr,&sampler)!=VK_SUCCESS)return false;
 VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=sizeof(circleFgInputs);sm.pCode=circleFgInputs;VkShaderModule shader{};
 if(dev.vk.CreateShaderModule(dev.device,&sm,nullptr,&shader)!=VK_SUCCESS)return false;
 VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pi.layout=pipelineLayout;pi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=shader;pi.stage.pName="main";
 auto result=dev.vk.CreateComputePipelines(dev.device,{},1,&pi,nullptr,&pipeline);dev.vk.DestroyShaderModule(dev.device,shader,nullptr);
 return result==VK_SUCCESS;
}
bool Transport::prepareDescriptors(Input& p){
 // A lease is selected only after both fences and all prior CPU owners retire.
 for(auto& view:p.views){if(view)dev.vk.DestroyImageView(dev.device,view,nullptr);view={};}
 if(!p.descriptors){
  VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2}};
  VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};ci.maxSets=1;ci.poolSizeCount=2;ci.pPoolSizes=sizes;
  if(dev.vk.CreateDescriptorPool(dev.device,&ci,nullptr,&p.descriptors)!=VK_SUCCESS)return false;
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=p.descriptors;ai.descriptorSetCount=1;ai.pSetLayouts=&setLayout;
  if(dev.vk.AllocateDescriptorSets(dev.device,&ai,&p.set)!=VK_SUCCESS)return false;
 }
 VkDescriptorImageInfo images[4]{};VkWriteDescriptorSet writes[4]{};
 for(unsigned i=0;i<4;++i){
  const auto& r=(i&1)?p.capture.motion:p.capture.depth;
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
  vi.image=i<2?reinterpret_cast<VkImage>(r.image):(i==2?p.depth.image:p.motion.image);
  vi.format=i<2?VkFormat(r.format):(i==2?VK_FORMAT_R32_SFLOAT:VK_FORMAT_R32G32_SFLOAT);
  bool depth=i==0&&(r.format==VK_FORMAT_D24_UNORM_S8_UINT||r.format==VK_FORMAT_D32_SFLOAT||r.format==VK_FORMAT_D32_SFLOAT_S8_UINT);
  vi.subresourceRange={VkImageAspectFlags(depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1};
  if(dev.vk.CreateImageView(dev.device,&vi,nullptr,&p.views[i])!=VK_SUCCESS)return false;
  images[i]={i<2?sampler:VK_NULL_HANDLE,p.views[i],i<2?VkImageLayout(r.layout):VK_IMAGE_LAYOUT_GENERAL};
  writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=p.set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;writes[i].pImageInfo=&images[i];
 }
 dev.vk.UpdateDescriptorSets(dev.device,4,writes,0,nullptr);return true;
}
bool Transport::make(SharedImage& out,const Resource& r){
 auto f=format(r.format);if(f==DXGI_FORMAT_UNKNOWN)return false;
 VkPhysicalDeviceExternalImageFormatInfo ex{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};ex.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
 VkPhysicalDeviceImageFormatInfo2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,&ex};query.format=VkFormat(r.format);query.type=VK_IMAGE_TYPE_2D;query.tiling=VK_IMAGE_TILING_OPTIMAL;query.usage=VK_IMAGE_USAGE_STORAGE_BIT;
 VkExternalImageFormatProperties ep{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};VkImageFormatProperties2 props{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,&ep};
 if(dev.instance->vk.GetPhysicalDeviceImageFormatProperties2(dev.physicalDevice,&query,&props)!=VK_SUCCESS||!(ep.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))return false;
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
 D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=r.width;desc.Height=r.height;desc.DepthOrArraySize=desc.MipLevels=1;desc.Format=f;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
 if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&out.dx))))return false;
 HANDLE handle{};if(FAILED(device->CreateSharedHandle(out.dx.Get(),nullptr,GENERIC_ALL,nullptr,&handle)))return false;
 VkExternalMemoryImageCreateInfo ec{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};ec.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
 VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,&ec};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VkFormat(r.format);ci.extent={r.width,r.height,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_STORAGE_BIT;
 auto result=dev.vk.CreateImage(dev.device,&ci,nullptr,&out.image);
 if(result==VK_SUCCESS){
  VkMemoryRequirements req{};dev.vk.GetImageMemoryRequirements(dev.device,out.image,&req);
  VkMemoryWin32HandlePropertiesKHR hp{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
  result=dev.vk.GetMemoryWin32HandlePropertiesKHR(dev.device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,handle,&hp);
  VkPhysicalDeviceMemoryProperties mp{};dev.instance->vk.GetPhysicalDeviceMemoryProperties(dev.physicalDevice,&mp);
  unsigned type=UINT32_MAX;for(unsigned i=0;i<mp.memoryTypeCount;++i)if((hp.memoryTypeBits&req.memoryTypeBits)&(1u<<i)){type=i;if(mp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)break;}
  if(result==VK_SUCCESS&&type!=UINT32_MAX){
   VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated.image=out.image;
   VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,&dedicated};import.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;import.handle=handle;
   VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,&import};ai.allocationSize=req.size;ai.memoryTypeIndex=type;
   result=dev.vk.AllocateMemory(dev.device,&ai,nullptr,&out.memory);
   if(result==VK_SUCCESS)result=dev.vk.BindImageMemory(dev.device,out.image,out.memory,0);
  }else result=VK_ERROR_FEATURE_NOT_PRESENT;
 }
 CloseHandle(handle);return result==VK_SUCCESS;
}
void Transport::record(const Capture& c){
 std::lock_guard lock(mutex);if(stopped)return;
 if(c.cancelled){pending.erase(c.tag.backendFrame);return;}
 if(pending.count(c.tag.backendFrame))return;
 uint32_t family=UINT32_MAX;
 {std::lock_guard commandLock(dev.fgCommandMutex);auto it=dev.fgCommands.find(static_cast<VkCommandBuffer>(c.command));if(it!=dev.fgCommands.end())family=it->second.second;}
 if(family==UINT32_MAX||family>=dev.queueFamilyFlags.size()||!(dev.queueFamilyFlags[family]&VK_QUEUE_COMPUTE_BIT)){state("fallback","Streamline command buffer family not observed",true);return;}
 static unsigned logged{};
 if(++logged<=4)circlevr::vkcore::logf("vr-fg: inputs eye %u tick %llu backend %u seq %u cmd %p depth %ux%u fmt %u usage %x layout %u MV %ux%u fmt %u usage %x layout %u scale %.6f %.6f reset %u",unsigned(c.tag.eye),c.tag.tick,c.tag.backendFrame,c.tag.eyeSeq,c.command,c.depth.width,c.depth.height,c.depth.format,c.depth.usage,c.depth.layout,c.motion.width,c.motion.height,c.motion.format,c.motion.usage,c.motion.layout,c.camera.motionScale[0],c.camera.motionScale[1],c.camera.reset);
 for(auto* r:{&c.depth,&c.motion}){
  if(!r->width||!r->height||r->width>8192||r->height>8192||!(r->usage&VK_IMAGE_USAGE_SAMPLED_BIT)||(r->layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&r->layout!=VK_IMAGE_LAYOUT_GENERAL&&r->layout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)||
   r->left||r->top||(r->extentWidth&&r->extentWidth!=r->width)||(r->extentHeight&&r->extentHeight!=r->height)){
   state("fallback","Depth/MV format, usage or subrect not supported",true);return;
  }
 }
 if(c.depth.width!=c.motion.width||c.depth.height!=c.motion.height){state("fallback","Depth and motion grids differ",true);return;}
 if(!initializePipeline())return;
 // Inputs from a dropped, already completed native command need no color
 // presentation. Retire only after its explicit GPU completion event; an old
 // backend number alone never proves completion. Keep a window for presents
 // still making their way through Motor's queue.
 for(auto it=pending.begin();it!=pending.end();){
  if(c.tag.backendFrame>it->first&&c.tag.backendFrame-it->first>16&&it->second->converted&&dev.vk.GetEventStatus(dev.device,it->second->converted)==VK_EVENT_SET)it=pending.erase(it);
  else ++it;
 }
 InputPtr p;
 for(auto& candidate:pool)if(candidate.use_count()==1&&candidate->converted&&dev.vk.GetEventStatus(dev.device,candidate->converted)==VK_EVENT_SET&&
  (!candidate->produced||producer->GetCompletedValue()>=candidate->produced)&&consumer->GetCompletedValue()>=candidate->consumed&&
  compatible(candidate->capture.depth,c.depth)&&compatible(candidate->capture.motion,c.motion)){p=candidate;break;}
 if(!p){
  // Orphaned command recordings are retained until device idle, never guessed
  // complete from a newer frame number. Bounded pool fails closed under loss.
  if(pool.size()>=24){state("fallback","FG input leases exhausted; restart required",true);return;}
  p=std::make_shared<Input>();pool.push_back(p);
  Resource depth=c.depth,motion=c.motion;depth.format=VK_FORMAT_R32_SFLOAT;motion.format=VK_FORMAT_R32G32_SFLOAT;
  if(!make(p->depth,depth)||!make(p->motion,motion)){state("fallback","Vulkan/D3D12 input import failed",true);return;}
  VkEventCreateInfo ec{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};if(dev.vk.CreateEvent(dev.device,&ec,nullptr,&p->converted)!=VK_SUCCESS){state("fallback","FG completion event allocation failed",true);return;}
 }
 p->capture=c;p->produced=p->consumed=0;
 dev.vk.ResetEvent(dev.device,p->converted);
 if(!prepareDescriptors(*p)){state("fallback","FG input descriptor creation failed",true);return;}
 const auto cmd=static_cast<VkCommandBuffer>(c.command);
 VkMemoryBarrier source{VK_STRUCTURE_TYPE_MEMORY_BARRIER};source.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;source.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
 VkImageMemoryBarrier b[2]{};
 for(unsigned i=0;i<2;++i){auto& x=b[i];x.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  x.srcQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL;x.dstQueueFamilyIndex=family;
  x.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};x.image=i?p->motion.image:p->depth.image;
  x.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;x.newLayout=VK_IMAGE_LAYOUT_GENERAL;x.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
 }
 dev.vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&source,0,nullptr,2,b);
 dev.vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
 dev.vk.CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,1,&p->set,0,nullptr);
 dev.vk.CmdDispatch(cmd,(c.depth.width+7)/8,(c.depth.height+7)/8,1);
 for(auto& x:b){x.oldLayout=VK_IMAGE_LAYOUT_GENERAL;x.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;x.dstAccessMask=0;x.srcQueueFamilyIndex=family;x.dstQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL;}
 dev.vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,b);
 dev.vk.CmdSetEvent(cmd,p->converted,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
 pending.emplace(c.tag.backendFrame,p);countCapture();
}
InputPtr Transport::take(const circlevr::stereo_seq::RenderTag& tag,uint64_t value){
 std::lock_guard lock(mutex);auto it=pending.find(tag.backendFrame);if(it==pending.end())return {};
 auto p=it->second;pending.erase(it);p->produced=value;
 if(p->capture.tag.tick!=tag.tick||p->capture.tag.eyeSeq!=tag.eyeSeq||p->capture.tag.eye!=tag.eye)return {};
 return p;
}
}
