#include "fsr.hpp"
#include "build_profile.hpp"
#include "sequential.hpp"
#include "temporal_continuity.hpp"
#include "framegen/bridge.hpp"
#include "framegen/fidelityfx.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/ui_vulkan.hpp"
#include "vkcore/log.hpp"
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/ffx_framegeneration.h>
#include <safetyhook.hpp>
#include <windows.h>
#include <filesystem>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstring>
namespace circle::motor::fsr {
namespace {
PfnFfxCreateContext create{};PfnFfxDestroyContext destroy{};PfnFfxDispatch dispatch{};PfnFfxConfigure configure{};
struct Twin {ffxContext right{};uint32_t flags{};TemporalContinuity history;};
std::recursive_mutex mutex;
std::unordered_map<ffxContext,Twin> twins;
std::atomic<bool> installed{};
std::array<std::atomic<ULONGLONG>,2> success{};
ffxReturnCode_t make(ffxContext* ctx,ffxCreateContextDescHeader* desc,const ffxAllocationCallbacks* alloc){
 auto result=create(ctx,desc,alloc);
 if(result||!ctx||!*ctx||!desc||desc->type!=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE)return result;
 std::lock_guard lock(mutex);Twin twin{};twin.flags=reinterpret_cast<ffxCreateContextDescUpscale*>(desc)->flags;
 result=create(&twin.right,desc,alloc);
 if(result){destroy(ctx,alloc);circlevr::vkcore::logf("motor-fsr: second eye context failed %u",result);return result;}
 twins.emplace(*ctx,std::move(twin));circlevr::vkcore::logf("motor-fsr: independent native FSR contexts created");return 0;
}
ffxReturnCode_t drop(ffxContext* ctx,const ffxAllocationCallbacks* alloc){
 std::lock_guard lock(mutex);if(ctx){auto it=twins.find(*ctx);if(it!=twins.end()){
  auto r=destroy(&it->second.right,alloc);if(r)return r;twins.erase(it);
 }}return destroy(ctx,alloc);
}
ffxReturnCode_t config(ffxContext* ctx,const ffxConfigureDescHeader* desc){
 // Native desktop interpolation must not wrap/pace the stereo transport.
 if(desc&&desc->type==FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION){
  auto disabled=*reinterpret_cast<const ffxConfigureDescFrameGeneration*>(desc);disabled.frameGenerationEnabled=false;
  return configure(ctx,&disabled.header);
 }
 std::lock_guard lock(mutex);if(ctx){auto it=twins.find(*ctx);if(it!=twins.end()){
  auto r=configure(&it->second.right,desc);if(r)return r;
 }}return configure(ctx,desc);
}
bool resource(const FfxApiResource& input,circle::fg::Resource& output){
 if(input.description.type!=FFX_API_RESOURCE_TYPE_TEXTURE2D||input.description.depth!=1||input.description.mipCount!=1)return false;
 auto image=reinterpret_cast<VkImage>(input.resource);auto record=circlevr::vkcore::ui_vulkan::recordOf(image);
 if(!record||record->width!=input.description.width||record->height!=input.description.height)return false;
 uint32_t layout=0;
 if(input.state==FFX_API_RESOURCE_STATE_COMPUTE_READ||input.state==FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ)layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
 else if(input.state==FFX_API_RESOURCE_STATE_UNORDERED_ACCESS)layout=VK_IMAGE_LAYOUT_GENERAL;
 else return false;
 output={reinterpret_cast<uint64_t>(image),layout,record->width,record->height,uint32_t(record->format),record->usage};return true;
}
ffxReturnCode_t run(ffxContext* ctx,const ffxDispatchDescHeader* desc){
 std::lock_guard lock(mutex);
 auto it=ctx?twins.find(*ctx):twins.end();
 if(it==twins.end()||!desc)return dispatch(ctx,desc);
 if(desc->type==FFX_API_DISPATCH_DESC_TYPE_UPSCALE){
  static unsigned probes{};if(++probes<=3){const auto& p=*reinterpret_cast<const ffxDispatchDescUpscale*>(desc);
   circlevr::vkcore::logf("motor-fsr: native API render %ux%u depth %ux%u motion %ux%u state %u/%u frameMs %.3f meters %.3f FOV %.3f",p.renderSize.width,p.renderSize.height,p.depth.description.width,p.depth.description.height,p.motionVectors.description.width,p.motionVectors.description.height,p.depth.state,p.motionVectors.state,p.frameTimeDelta,p.viewSpaceToMetersFactor,p.cameraFovAngleVertical);
  }
 }
 auto tag=circlevr::vkcore::seqTagInFlight();
 if(!tag||!circlevr::stereo_seq::drawsEyeView(*tag)){
  it->second.history.clear();return dispatch(ctx,desc);
 }
 const auto eye=circlevr::stereo_seq::eyeIndex(tag->eye);
 auto* selected=eye?&it->second.right:ctx;
 if(desc->type!=FFX_API_DISPATCH_DESC_TYPE_UPSCALE)return dispatch(selected,desc);
 auto d=*reinterpret_cast<const ffxDispatchDescUpscale*>(desc);
 const bool discontinuity=it->second.history.reset(eye,tag->eyeSeq,tag->backendFrame,sequential::monoVrEnabled()?1:2);
 d.reset=d.reset||discontinuity;
 if(circle::fg::fidelityRequested()){
  circle::fg::Capture c{};c.tag=*tag;c.command=d.commandList;c.frameMs=d.frameTimeDelta;c.meters=d.viewSpaceToMetersFactor;c.fsrFlags=it->second.flags;
  auto& camera=c.camera;camera.jitter={d.jitterOffset.x,d.jitterOffset.y};camera.reset=d.reset;
  camera.nearPlane=d.cameraNear;camera.farPlane=d.cameraFar;camera.fov=d.cameraFovAngleVertical;
  if(resource(d.depth,c.depth)&&resource(d.motionVectors,c.motion)&&c.motion.width&&c.motion.height){
   camera.motionScale={d.motionVectorScale.x/c.motion.width,d.motionVectorScale.y/c.motion.height};
   camera.inverted=(c.fsrFlags&FFX_UPSCALE_ENABLE_DEPTH_INVERTED)!=0;
   camera.jittered=(c.fsrFlags&FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION)!=0;
   // Reject partial render grids: the transport currently copies complete images.
   if(d.renderSize.width==c.depth.width&&d.renderSize.height==c.depth.height)circle::fg::capture(c);
   else circle::fg::state("fallback","Native FSR partial depth rectangle unsupported",true);
  }else circle::fg::state("fallback","Native FSR depth/motion resource or layout not verified",true);
 }
 auto result=dispatch(selected,&d.header);
 if(result&&circle::fg::fidelityRequested()){
  circle::fg::Capture rejected{};rejected.tag=*tag;rejected.cancelled=true;circle::fg::capture(rejected);
 }
 if(!result)success[eye]=GetTickCount64();else it->second.history.clear();
 static unsigned counts[2]{};if(++counts[eye]<=4||result)circlevr::vkcore::logf("motor-fsr: eye %u backend %u result %u reset %d render %ux%u output %ux%u",eye,tag->backendFrame,result,d.reset,d.renderSize.width,d.renderSize.height,d.output.description.width,d.output.description.height);
 return result;
}
}
bool requested(){static bool on=[] {char s[32]{};GetEnvironmentVariableA("CIRCLE_UPSCALING",s,32);return std::strcmp(s,"fsr")==0;}();return on;}
bool active(){auto now=GetTickCount64();return success[0]&&now-success[0]<2000&&(sequential::monoVrEnabled()||(success[1]&&now-success[1]<2000));}
bool install(){
 if(installed)return true;if(!requested()||!build::supported())return false;
 wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
 auto dll=std::filesystem::path(path).parent_path()/L"amd_ffx"/L"amd_fidelityfx_vk.dll";
 auto module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)return false;
 // All hooks are prepared disabled. No partially installed eye-routing path.
 const char* names[]={"ffxCreateContext","ffxDestroyContext","ffxDispatch","ffxConfigure"};
 void* replacements[]={reinterpret_cast<void*>(make),reinterpret_cast<void*>(drop),reinterpret_cast<void*>(run),reinterpret_cast<void*>(config)};
 std::array<safetyhook::InlineHook,4> hooks;
 for(unsigned i=0;i<4;++i){auto p=GetProcAddress(module,names[i]);if(!p)return false;
  auto h=safetyhook::InlineHook::create(reinterpret_cast<void*>(p),replacements[i],safetyhook::InlineHook::StartDisabled);if(!h)return false;hooks[i]=std::move(*h);
 }
 create=hooks[0].original<PfnFfxCreateContext>();destroy=hooks[1].original<PfnFfxDestroyContext>();dispatch=hooks[2].original<PfnFfxDispatch>();configure=hooks[3].original<PfnFfxConfigure>();
 for(auto& h:hooks)if(!h.enable())return false;
 for(auto& h:hooks)new safetyhook::InlineHook(std::move(h));installed=true;
 circlevr::vkcore::logf("motor-fsr: native FidelityFX API eye isolation installed; desktop FG disabled");return true;
}
}
