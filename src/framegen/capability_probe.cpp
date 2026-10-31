#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <nvsdk_ngx_helpers_dlssg_d3d.h>
#include <cstdio>
#include <filesystem>
using Microsoft::WRL::ComPtr;
// Circle-owned project identity. Never impersonate the reference port or the game.
constexpr char ProjectId[]="8d6d5463-c0bf-45e9-83f4-842882929649";
void NVSDK_CONV ngxLog(const char* s,NVSDK_NGX_Logging_Level,NVSDK_NGX_Feature){if(s)fprintf(stderr,"NGX: %s\n",s);}
int wmain(int argc,wchar_t** argv){
 if(argc!=2&&argc!=3){fprintf(stderr,"Usage: CircleFramegenProbe <game-directory> [community-runtime-directory]\n");return 2;}
 if(argc==3){auto proxy=std::filesystem::absolute(argv[2])/L"version.dll";
  if(!LoadLibraryExW(proxy.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)){fprintf(stderr,"Community loader failed: %lu\n",GetLastError());return 9;}}
 const auto path=std::filesystem::path(argv[1])/L"streamline";
 if(!std::filesystem::exists(path/L"nvngx_dlssg.dll"))return 3;
 ComPtr<IDXGIFactory6> factory;ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;
 if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return 4;
 for(UINT i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;++i){
  DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);
  if(d.VendorId==0x10de&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)))){wprintf(L"GPU: %s\n",d.Description);break;}
  adapter.Reset();
 }
 if(!device)return 5;
 const wchar_t* paths[]={path.c_str()};NVSDK_NGX_FeatureCommonInfo info{};
 info.PathListInfo.Path=paths;info.PathListInfo.Length=1;
 info.LoggingInfo.LoggingCallback=&ngxLog;info.LoggingInfo.MinimumLoggingLevel=NVSDK_NGX_LOGGING_LEVEL_ON;
 const auto logs=std::filesystem::absolute(L"logs");
 auto result=NVSDK_NGX_D3D12_Init_with_ProjectID(ProjectId,NVSDK_NGX_ENGINE_TYPE_CUSTOM,"KHARVOX CIRCLE",logs.c_str(),device.Get(),&info);
 printf("Init=0x%08X SDK=0x%08X\n",unsigned(result),unsigned(NVSDK_NGX_Version_API));
 if(NVSDK_NGX_FAILED(result))return 6;
 NVSDK_NGX_Parameter* caps{};result=NVSDK_NGX_D3D12_GetCapabilityParameters(&caps);
 if(NVSDK_NGX_FAILED(result)||!caps)return 7;
 int available=0,driver=0,init=0;unsigned frames=0;
 caps->Get(NVSDK_NGX_Parameter_FrameGeneration_Available,&available);
 caps->Get(NVSDK_NGX_Parameter_FrameGeneration_NeedsUpdatedDriver,&driver);
 caps->Get(NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult,&init);
 caps->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax,&frames);
 printf("Available=%d NeedsDriver=%d FeatureInit=0x%08X MaxFrames=%u\n",available,driver,unsigned(init),frames);
 NVSDK_NGX_D3D12_DestroyParameters(caps);
 // No NGX shutdown: same rule used by the in-process integration.
 return available?0:8;
}
