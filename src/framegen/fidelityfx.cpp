// FidelityFX direct per-eye dispatch; OpenXR presentation remains owned by Circle.
// Circle VR dispatches frame generation independently for each eye.
#include "fidelityfx.hpp"
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/dx12/ffx_api_dx12.h>
#include <windows.h>
#include <filesystem>
#include <mutex>
#include <cmath>
namespace circle::fg {
namespace {
PfnFfxCreateContext createContext{};
PfnFfxDestroyContext destroyContext{};
PfnFfxDispatch dispatch{};
std::once_flag once;
bool error(ffxReturnCode_t r,const char* stage,std::string& reason){
 if(!r)return false;reason=std::string("FidelityFX ")+stage+" failed: "+std::to_string(r);return true;
}
}
bool fidelityRequested(){static bool value=[] {char s[8]{};return GetEnvironmentVariableA("CIRCLE_VR_FSR_FG",s,8)==1&&s[0]=='1';}();return value;}
bool fidelityAvailable(std::string& reason){
 std::call_once(once,[]{
  HMODULE self{};wchar_t path[32768]{};
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&fidelityAvailable),&self))return;
  if(!GetModuleFileNameW(self,path,32768))return;
  auto dll=std::filesystem::path(path).parent_path()/L"CircleFidelityFX_DX12.dll";
  auto module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!module)return;
  createContext=reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(module,"ffxCreateContext"));
  destroyContext=reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(module,"ffxDestroyContext"));
  dispatch=reinterpret_cast<PfnFfxDispatch>(GetProcAddress(module,"ffxDispatch"));
 });
 if(createContext&&destroyContext&&dispatch)return true;
 reason="Bundled FidelityFX DX12 API unavailable";return false;
}
struct FidelityFeature::Impl {
 ffxContext context{};
 ffxCreateBackendDX12Desc backend{};
 ffxCreateContextDescFrameGeneration description{};
 uint64_t frame{};
 ~Impl(){if(context)destroyContext(&context,nullptr);}
};
FidelityFeature::FidelityFeature()=default;
FidelityFeature::~FidelityFeature()=default;
bool FidelityFeature::evaluate(ID3D12Device* device,ID3D12GraphicsCommandList* list,
 ID3D12Resource* color,ID3D12Resource* depth,ID3D12Resource* motion,ID3D12Resource* output,
 const Capture& capture,bool reset,std::string& reason){
 if(!fidelityAvailable(reason))return false;
 const auto& c=capture.camera;auto size=color->GetDesc();auto grid=depth->GetDesc();
 if(!std::isfinite(c.fov)||c.fov<=0||c.fov>=3.14159f||!std::isfinite(capture.frameMs)||capture.frameMs<=0||
    !std::isfinite(capture.meters)||capture.meters<=0){reason="Invalid native FSR camera/timing";return false;}
 const uint32_t flags=capture.fsrFlags & (FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS|
  FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION|FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED|FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE);
 if(impl&&(impl->description.flags!=flags||impl->description.displaySize.width!=size.Width||impl->description.displaySize.height!=size.Height||
  impl->description.maxRenderSize.width!=grid.Width||impl->description.maxRenderSize.height!=grid.Height))impl.reset();
 if(!impl){
  auto made=std::make_unique<Impl>();made->backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;made->backend.device=device;
  auto& d=made->description;d.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,&made->backend.header};d.flags=flags;
  d.displaySize={uint32_t(size.Width),size.Height};d.maxRenderSize={uint32_t(grid.Width),grid.Height};d.backBufferFormat=FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
  if(error(createContext(&made->context,&d.header,nullptr),"create",reason))return false;
  impl=std::move(made);reset=true;
 }
 ffxDispatchDescFrameGenerationPrepare p{};p.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
 p.frameID=++impl->frame;p.commandList=list;p.renderSize={uint32_t(grid.Width),grid.Height};
 p.jitterOffset={c.jitter[0],c.jitter[1]};p.motionVectorScale={c.motionScale[0]*float(grid.Width),c.motionScale[1]*grid.Height};
 p.frameTimeDelta=capture.frameMs;p.cameraNear=c.nearPlane;p.cameraFar=c.farPlane;p.cameraFovAngleVertical=c.fov;p.viewSpaceToMetersFactor=capture.meters;
 p.depth=ffxApiGetResourceDX12(depth);p.motionVectors=ffxApiGetResourceDX12(motion);
 if(error(dispatch(&impl->context,&p.header),"prepare",reason))return false;
 ffxDispatchDescFrameGeneration d{};d.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;d.commandList=list;d.frameID=p.frameID;
 d.presentColor=ffxApiGetResourceDX12(color);d.outputs[0]=ffxApiGetResourceDX12(output,FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
 d.numGeneratedFrames=1;d.reset=reset;d.backbufferTransferFunction=FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;d.minMaxLuminance[1]=1;
 d.generationRect={0,0,int32_t(size.Width),int32_t(size.Height)};
 return !error(dispatch(&impl->context,&d.header),"interpolate",reason);
}
}
