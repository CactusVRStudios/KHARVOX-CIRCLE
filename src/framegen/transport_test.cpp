// Standalone Vulkan -> D3D12 fixture. All CPU readbacks are test-only.
// No XR implementation is linked or instantiated in this transport-only test.
namespace circlevr::vkcore {class XrPresenter {};}
#include "transport.hpp"
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <cstdarg>
#include <cstdio>
#include <cmath>
using Microsoft::WRL::ComPtr;
namespace circlevr::vkcore {void logf(const char* fmt,...){va_list a;va_start(a,fmt);vprintf(fmt,a);puts("");va_end(a);}}
static void check(bool b,const char* s){if(!b){fprintf(stderr,"FAILED: %s\n",s);exit(1);}}
int main(){
 SetEnvironmentVariableA("CIRCLE_VR_DLSS_FG","1");
 ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
 ComPtr<IDXGIFactory6> factory;check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"DXGI factory");
 ComPtr<ID3D12Device> dx;ComPtr<IDXGIAdapter1> adapter;
 for(unsigned i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);if(!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&dx))))break;adapter.Reset();}
 check(bool(dx),"hardware D3D12 device");ComPtr<ID3D12InfoQueue> messages;dx.As(&messages);
 ComPtr<ID3D12Fence> producer,consumer;check(SUCCEEDED(dx->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&producer))),"shared fence");dx->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&consumer));
 ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};dx->CreateCommandQueue(&q,IID_PPV_ARGS(&queue));
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;dx->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator));dx->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list));list->Close();
 auto library=LoadLibraryW(L"vulkan-1.dll");check(library!=nullptr,"Vulkan loader");
 auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(library,"vkGetInstanceProcAddr"));
 auto createInstance=reinterpret_cast<PFN_vkCreateInstance>(gipa({},"vkCreateInstance"));
 VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Circle FG transport fixture";app.apiVersion=VK_API_VERSION_1_2;
 VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ic.pApplicationInfo=&app;VkInstance instance{};check(createInstance(&ic,nullptr,&instance)==VK_SUCCESS,"Vulkan instance");
#define INSTANCE(name) auto name=reinterpret_cast<PFN_vk##name>(gipa(instance,"vk" #name))
 INSTANCE(EnumeratePhysicalDevices);INSTANCE(GetPhysicalDeviceProperties2);INSTANCE(GetPhysicalDeviceFeatures);INSTANCE(GetPhysicalDeviceQueueFamilyProperties);INSTANCE(CreateDevice);INSTANCE(GetDeviceProcAddr);
 unsigned count=0;EnumeratePhysicalDevices(instance,&count,nullptr);std::vector<VkPhysicalDevice> physical(count);EnumeratePhysicalDevices(instance,&count,physical.data());VkPhysicalDevice gpu{};
 auto luid=dx->GetAdapterLuid();for(auto p:physical){VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&id};GetPhysicalDeviceProperties2(p,&props);if(id.deviceLUIDValid&&!memcmp(id.deviceLUID,&luid,sizeof(luid))){gpu=p;break;}}
 check(gpu!=VK_NULL_HANDLE,"same adapter LUID");GetPhysicalDeviceQueueFamilyProperties(gpu,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);GetPhysicalDeviceQueueFamilyProperties(gpu,&count,families.data());unsigned family=UINT32_MAX;for(unsigned i=0;i<count;++i)if(families[i].queueFlags&VK_QUEUE_COMPUTE_BIT){family=i;break;}check(family!=UINT32_MAX,"compute queue");
 VkPhysicalDeviceFeatures features{};GetPhysicalDeviceFeatures(gpu,&features);check(features.shaderStorageImageExtendedFormats,"extended storage");features={};features.shaderStorageImageExtendedFormats=VK_TRUE;
 float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
 const char* extensions[]={"VK_KHR_external_memory_win32","VK_KHR_external_semaphore_win32"};VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};timeline.timelineSemaphore=VK_TRUE;
 VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,&timeline};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qi;dc.pEnabledFeatures=&features;dc.enabledExtensionCount=2;dc.ppEnabledExtensionNames=extensions;VkDevice device{};check(CreateDevice(gpu,&dc,nullptr,&device)==VK_SUCCESS,"Vulkan device");
 // Test process owns this structure until exit; no presenter is constructed.
 auto* data=new circlevr::vkcore::DeviceData;circlevr::vkcore::InstanceData inst{};inst.instance=instance;data->device=device;data->physicalDevice=gpu;data->instance=&inst;data->fgExtendedStorage=true;data->nextGetDeviceProcAddr=GetDeviceProcAddr;
#define LOAD_INSTANCE(name) inst.vk.name=reinterpret_cast<PFN_vk##name>(gipa(instance,"vk" #name));
 CIRCLE_VR_INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#define LOAD_DEVICE(name) data->vk.name=reinterpret_cast<PFN_vk##name>(GetDeviceProcAddr(device,"vk" #name));
 CIRCLE_VR_DEVICE_FUNCTIONS(LOAD_DEVICE)
#define DEVICE(name) auto name=reinterpret_cast<PFN_vk##name>(GetDeviceProcAddr(device,"vk" #name))
 DEVICE(CmdClearDepthStencilImage);DEVICE(QueueWaitIdle);DEVICE(ImportSemaphoreWin32HandleKHR);
 for(auto& f:families)data->queueFamilyFlags.push_back(f.queueFlags);
 VkQueue vq{};data->vk.GetDeviceQueue(device,family,0,&vq);VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pc.queueFamilyIndex=family;pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;VkCommandPool pool{};data->vk.CreateCommandPool(device,&pc,nullptr,&pool);
 VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;VkCommandBuffer cmd{};data->vk.AllocateCommandBuffers(device,&ca,&cmd);data->fgCommands[cmd]={pool,family};
 VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};type.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,&type};VkSemaphore semaphore{};data->vk.CreateSemaphore(device,&sc,nullptr,&semaphore);
 HANDLE shared{};dx->CreateSharedHandle(producer.Get(),nullptr,GENERIC_ALL,nullptr,&shared);VkImportSemaphoreWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR};import.semaphore=semaphore;import.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;import.handle=shared;check(ImportSemaphoreWin32HandleKHR(device,&import)==VK_SUCCESS,"import fence");CloseHandle(shared);
 constexpr unsigned w=64,h=32;VkImage images[2]{};VkDeviceMemory memory[2]{};VkFormat formats[]={VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_R16G16B16A16_SFLOAT};VkImageAspectFlags aspects[]={VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,VK_IMAGE_ASPECT_COLOR_BIT};
 VkPhysicalDeviceMemoryProperties mp{};inst.vk.GetPhysicalDeviceMemoryProperties(gpu,&mp);
 for(unsigned i=0;i<2;++i){VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=formats[i];ci.extent={w,h,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;check(data->vk.CreateImage(device,&ci,nullptr,&images[i])==VK_SUCCESS,"source image");VkMemoryRequirements req{};data->vk.GetImageMemoryRequirements(device,images[i],&req);unsigned mt=0;while(!(req.memoryTypeBits&(1u<<mt)))++mt;VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=mt;check(data->vk.AllocateMemory(device,&ai,nullptr,&memory[i])==VK_SUCCESS,"source memory");data->vk.BindImageMemory(device,images[i],memory[i],0);}
 auto transport=std::make_shared<circle::fg::Transport>(*data,dx.Get(),producer.Get(),consumer.Get());transport->connect();HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 for(uint64_t round=1;round<=32;++round){
  data->vk.ResetCommandBuffer(cmd,0);VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};data->vk.BeginCommandBuffer(cmd,&begin);
  for(unsigned i=0;i<2;++i){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=images[i];b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={aspects[i],0,1,0,1};b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;data->vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
   if(i){VkClearColorValue c{{.125f,-.25f,.5f,1.f}};data->vk.CmdClearColorImage(cmd,images[i],b.newLayout,&c,1,&b.subresourceRange);}else{VkClearDepthStencilValue c{.625f,0};CmdClearDepthStencilImage(cmd,images[i],b.newLayout,&c,1,&b.subresourceRange);}
   b.oldLayout=b.newLayout;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;data->vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
  }
  circle::fg::Capture capture{};capture.command=cmd;capture.tag.backendFrame=unsigned(round);capture.tag.tick=round;capture.tag.eyeSeq=unsigned(round);
  capture.depth={reinterpret_cast<uint64_t>(images[0]),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,w,h,unsigned(formats[0]),VK_IMAGE_USAGE_SAMPLED_BIT};capture.motion={reinterpret_cast<uint64_t>(images[1]),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,w,h,unsigned(formats[1]),VK_IMAGE_USAGE_SAMPLED_BIT};circle::fg::capture(capture);
  if(round==32){auto rejected=capture;rejected.cancelled=true;circle::fg::capture(rejected);}
  auto input=transport->take(capture.tag,round);check(bool(input)==(round!=32),"failed upscaler input cancellation");check(data->vk.EndCommandBuffer(cmd)==VK_SUCCESS,"end conversion");
  VkTimelineSemaphoreSubmitInfo ts{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};ts.signalSemaphoreValueCount=1;ts.pSignalSemaphoreValues=&round;VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO,&ts};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&semaphore;check(data->vk.QueueSubmit(vq,1,&submit,{})==VK_SUCCESS,"submit conversion");
  if(round==32){check(QueueWaitIdle(vq)==VK_SUCCESS,"cancelled recording completion");continue;}
  queue->Wait(producer.Get(),round);allocator->Reset();list->Reset(allocator.Get(),nullptr);
  ComPtr<ID3D12Resource> readbacks[2];D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2]{};
  for(unsigned i=0;i<2;++i){auto* source=i?input->motion.dx.Get():input->depth.dx.Get();auto desc=source->GetDesc();uint64_t size{};dx->GetCopyableFootprints(&desc,0,1,0,&fp[i],nullptr,nullptr,&size);D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=size;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;dx->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readbacks[i]));D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={source,0,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=readbacks[i].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp[i];list->CopyTextureRegion(&to,0,0,0,&from,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);}
  list->Close();ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);queue->Signal(consumer.Get(),round);input->consumed=round;consumer->SetEventOnCompletion(round,event);check(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"interop GPU completion");
  for(unsigned i=0;i<2;++i){float* p{};D3D12_RANGE range{0,16};readbacks[i]->Map(0,&range,reinterpret_cast<void**>(&p));check(std::abs(p[0]-(i?.125f:.625f))<.00001f,"converted first channel");if(i)check(std::abs(p[1]+.25f)<.00001f,"converted motion y");D3D12_RANGE none{};readbacks[i]->Unmap(0,&none);}
 }
 transport->stop();QueueWaitIdle(vq);transport->destroyIdle();CloseHandle(event);
 unsigned errors=0;if(messages)for(uint64_t i=0;i<messages->GetNumStoredMessages();++i){SIZE_T n=0;messages->GetMessage(i,nullptr,&n);std::vector<unsigned char> b(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(b.data());messages->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){puts(m->pDescription);++errors;}}check(!errors,"D3D debug errors");
 puts("PASS: 31 GPU-only D24S8/RGBA16F readbacks, cancelled upscale input, shared fences and lease reuse");return 0;
}
