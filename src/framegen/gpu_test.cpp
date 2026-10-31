// Standalone GPU validation. Readback is confined to this test executable.
#include "generator.hpp"
#include "community.hpp"
#include "options.hpp"
#include <nvsdk_ngx_helpers_dlssg_d3d.h>
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <thread>
using Microsoft::WRL::ComPtr;
namespace circlevr::vkcore {bool loggingEnabled(){return false;}void logf(const char* fmt,...){va_list args;va_start(args,fmt);vprintf(fmt,args);puts("");va_end(args);}}
void check(bool ok,const char* s){if(!ok){fprintf(stderr,"FAILED %s\n",s);exit(1);}}
void transition(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,0,a,b};l->ResourceBarrier(1,&x);}
int wmain(int argc,wchar_t** argv){
 if(argc<2||argc>3){puts("Usage: CircleFramegenGpuTest <game-directory> [--half-input|--preload-native|--early-community]");return 2;}
 const bool half=argc==3&&std::wstring(argv[2])==L"--half-input";
 ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
 ComPtr<IDXGIFactory6> factory;check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"factory");
 ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> d;
 for(unsigned i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);if((circle::fg::fidelityRequested()||desc.VendorId==0x10de)&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d))))break;adapter.Reset();}
 check(bool(d),"NVIDIA device");ComPtr<ID3D12InfoQueue> messages;d.As(&messages);
 ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
 D3D12_COMMAND_QUEUE_DESC q{};check(SUCCEEDED(d->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))),"queue");
 check(SUCCEEDED(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))),"allocator");
 check(SUCCEEDED(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))),"list");list->Close();
 d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);uint64_t value=0;
 auto execute=[&]{check(SUCCEEDED(list->Close()),"close");ID3D12CommandList* ls[]={list.Get()};queue->ExecuteCommandLists(1,ls);queue->Signal(fence.Get(),++value);fence->SetEventOnCompletion(value,event);check(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"GPU fence");check(SUCCEEDED(d->GetDeviceRemovedReason()),"device removal");};
 auto make=[&](unsigned w,unsigned h,DXGI_FORMAT f,D3D12_RESOURCE_STATES state){ComPtr<ID3D12Resource> p;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.Width=w;r.Height=h;r.DepthOrArraySize=r.MipLevels=1;r.SampleDesc.Count=1;r.Format=f;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;check(SUCCEEDED(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&r,state,nullptr,IID_PPV_ARGS(&p))),"texture");return p;};
 constexpr unsigned w=640,h=480;const unsigned rw=half?w/2:w,rh=half?h/2:h;auto stereo=make(2*w,h,DXGI_FORMAT_B8G8R8A8_UNORM,D3D12_RESOURCE_STATE_COMMON);auto target=make(2*w,h,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,D3D12_RESOURCE_STATE_RENDER_TARGET);
 std::array<circle::fg::InputPtr,2> inputs;for(auto& p:inputs){p=std::make_shared<circle::fg::Input>();p->depth.dx=make(rw,rh,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_COMMON);p->motion.dx=make(rw,rh,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_COMMON);p->capture.depth.width=rw;p->capture.depth.height=rh;
 auto& c=p->capture.camera;for(auto* matrix:{&c.viewToClip,&c.clipToView,&c.lens,&c.clipToPrev,&c.prevToClip})for(int i=0;i<4;++i)(*matrix)[i*5]=1;
 c.motionScale={1,1};c.up={0,1,0};c.right={1,0,0};c.forward={0,0,1};c.nearPlane=.1f;c.farPlane=100;c.fov=1.57f;c.aspect=float(w)/h;c.cameraMotion=1;c.invalidMotion=3.402823466e38f;}
 ComPtr<ID3D12DescriptorHeap> rtv;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=5;d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv));auto base=rtv->GetCPUDescriptorHandleForHeapStart();unsigned step=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
 auto handle=[&](unsigned i){auto x=base;x.ptr+=i*step;return x;};d->CreateRenderTargetView(stereo.Get(),nullptr,handle(0));for(unsigned e=0;e<2;++e){d->CreateRenderTargetView(inputs[e]->depth.dx.Get(),nullptr,handle(1+2*e));d->CreateRenderTargetView(inputs[e]->motion.dx.Get(),nullptr,handle(2+2*e));}
 circle::fg::Generator generator;auto directory=std::filesystem::path(argv[1])/L"streamline";
 const bool early=argc==3&&std::wstring(argv[2])==L"--early-community";
 if(early){DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);std::string reason;check(circle::fg::loadCommunityRuntime(desc.VendorId,desc.Description,reason),"early community load");}
 if(argc==3&&(std::wstring(argv[2])==L"--preload-native"||early)){
  const wchar_t* paths[]={directory.c_str()};NVSDK_NGX_FeatureCommonInfo info{};info.PathListInfo.Path=paths;info.PathListInfo.Length=1;
  wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);
  check(NVSDK_NGX_SUCCEED(NVSDK_NGX_D3D12_Init_with_ProjectID(circle::fg::ProjectId,NVSDK_NGX_ENGINE_TYPE_CUSTOM,"KHARVOX CIRCLE",temp,d.Get(),&info)),"preload native NGX");
  NVSDK_NGX_Parameter* caps{};check(NVSDK_NGX_SUCCEED(NVSDK_NGX_D3D12_GetCapabilityParameters(&caps)),"preload native capabilities");
  unsigned max{};caps->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax,&max);printf("Preinitialized NGX maxFrames=%u\n",max);NVSDK_NGX_D3D12_DestroyParameters(caps);
 }
 check(generator.initialize(d.Get(),queue.Get(),directory.c_str()),"NGX capability");
 if(early&&!circle::fg::fractionalMultiplier()&&!circle::fg::automaticMultiplier())check(generator.count()==circle::fg::requestedMultiplier()-1,"early loader preserves requested multiplier after NGX was already initialized");
 printf("Effective generated frames per pair: %u\n",generator.count());
 unsigned generated=0,expectedGenerated=0;
 const bool zeroTest=GetEnvironmentVariableA("CIRCLE_FG_TEST_ZERO",nullptr,0)>0;
 const bool fractionalTest=GetEnvironmentVariableA("CIRCLE_FG_TEST_FRACTIONAL",nullptr,0)>0;
 const bool prefetchTest=GetEnvironmentVariableA("CIRCLE_FG_TEST_PREFETCH",nullptr,0)>0;
 const unsigned frameCount=fractionalTest?54u:8u;
 char hzText[16]{};GetEnvironmentVariableA("CIRCLE_FG_TEST_HZ",hzText,16);const int testHz=atoi(hzText);const double hz=testHz==72||testHz==120?testHz:90;
 if(circle::fg::fractionalMultiplier()&&GetEnvironmentVariableA("CIRCLE_FG_TEST_BACKPRESSURE",nullptr,0)>0){
  using namespace circle::fg;
  displayPulse(int64_t(1000000000./hz+.5),true);
  fractionalSample(4,0,0,16.667,true);check(fractionalPacingActive(),"Auto ABI loaded for producer-wait test");
  displayPulse(int64_t(1000000000./hz+.5),true);pacingReady(true);
  pairPublished(1);pairPublished(2);const auto before=pairPublished(3);
  std::thread release([]{Sleep(25);pairRetired(3);});
  paceProducer();release.join();const auto after=pairPublished(4);
  const double held=after.waitTotalMs-before.waitTotalMs,elapsed=after.timeMs-before.timeMs;
  check(held>=15&&held<=elapsed+.1,"publication counter captures actual bounded producer wait");
  check(unblockedInterval(elapsed,held)<elapsed*.5,"paired clock excludes own wait instead of render work");
  check(fractionalSample(4,0,0,45,true,held)==1,"blocked queue remains visible after real retirement and before prefetch");
  printf("PASS producer wait %.3f ms / publication %.3f ms; pressure survives retirement\n",held,elapsed);
  pairRetired(4);pacingReady(false);
 }
 for(unsigned frame=0;frame<frameCount;++frame){
 if(circle::fg::fractionalMultiplier()){circle::fg::displayPulse(int64_t(1000000000./hz+.5),true);for(auto& input:inputs)input->capture.producerHoldMs=0;}
 allocator->Reset();list->Reset(allocator.Get(),nullptr);
 transition(list.Get(),stereo.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);
 for(unsigned e=0;e<2;++e){D3D12_RECT rect{LONG(e*w),0,LONG((e+1)*w),LONG(h)};float color[4]={e?.1f:.7f,e?.7f:.1f,.2f,1};list->ClearRenderTargetView(handle(0),color,1,&rect);
 D3D12_RECT moving{LONG(e*w+100+(frame%30)*8),120,LONG(e*w+180+(frame%30)*8),360};float bright[4]={.95f,.95f,.95f,1};list->ClearRenderTargetView(handle(0),bright,1,&moving);
 auto& p=*inputs[e];float depth[4]={.5f,0,0,0},motion[4]{};
 for(unsigned k=0;k<2;++k){auto resource=k?p.motion.dx.Get():p.depth.dx.Get();transition(list.Get(),resource,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ClearRenderTargetView(handle(1+2*e+k),k?motion:depth,0,nullptr);
 if(k){D3D12_RECT mr{LONG((100+(frame%30)*8)*rw/w),LONG(120*rh/h),LONG((180+(frame%30)*8)*rw/w),LONG(360*rh/h)};float mv[4]={-8.f/w,0,0,0};list->ClearRenderTargetView(handle(1+2*e+k),mv,1,&mr);}
 transition(list.Get(),resource,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON);}}
 transition(list.Get(),stereo.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON);
 circle::fg::Identity id{frame+1,1,{frame+1,frame+1},{2*frame+1,2*frame+2},w,h,true,false,true};
 if(fractionalTest){const unsigned counts[]={2,2,3,2,3,3,4,3,4,4,2};generator.selectMultiplier(zeroTest&&frame%2==0?1:counts[frame%11],true);}
 id.sourceMs=(frame+1)*(1000./60);
 bool made=generator.record(list.Get(),stereo.Get(),inputs,id);
 if(prefetchTest){
  // Submit generation before presentation, retain both eyes and outputs, and
  // reuse the allocator only after the preparation fence. The production XR
  // worker overlaps this interval with xrWaitFrame instead of waiting here.
  execute();generator.pollTiming();
  allocator->Reset();list->Reset(allocator.Get(),nullptr);
 }
 if(frame&&generator.count())++expectedGenerated;if(made){++generated;generator.compositeTo(list.Get(),target.Get(),DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);}else{
 transition(list.Get(),stereo.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
 transition(list.Get(),target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);
 list->CopyResource(target.Get(),stereo.Get());
 transition(list.Get(),target.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
 transition(list.Get(),stereo.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
 }execute();generator.pollTiming();printf("Frame %u generated %d count %u GPU %.3fms state %s\n",frame,made,generator.count(),generator.gpuMs,circle::fg::status().state);
 if(circle::fg::fractionalMultiplier())circle::fg::paceProducer();
 }
 check(generated==((zeroTest||circle::fg::fractionalMultiplier())?expectedGenerated:frameCount-1),"per-eye generation after history warmup");
 // Read back only the small synthetic fixture, never live gameplay.
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};uint64_t bytes{};auto td=target->GetDesc();d->GetCopyableFootprints(&td,0,1,0,&footprint,nullptr,nullptr,&bytes);
 ComPtr<ID3D12Resource> readback;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback));
 allocator->Reset();list->Reset(allocator.Get(),nullptr);transition(list.Get(),target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION dest{},src{};dest.pResource=readback.Get();dest.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dest.PlacedFootprint=footprint;src.pResource=target.Get();list->CopyTextureRegion(&dest,0,0,0,&src,nullptr);execute();
 unsigned char* pixels{};D3D12_RANGE range{0,size_t(bytes)};check(SUCCEEDED(readback->Map(0,&range,reinterpret_cast<void**>(&pixels))),"readback map");auto left=pixels+(h/2)*footprint.Footprint.RowPitch+(w-20)*4;auto right=left+w*4;printf("BGRA L %u %u %u %u R %u %u %u %u\n",left[0],left[1],left[2],left[3],right[0],right[1],right[2],right[3]);check(left[2]>left[1]+60&&right[1]>right[2]+60,"distinct generated eyes survive BGRA/sRGB composite");auto hashImage=[&](const unsigned char* data,unsigned eye){uint64_t hash=1469598103934665603ull;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w*4;++x){hash^=data[y*footprint.Footprint.RowPitch+eye*w*4+x];hash*=1099511628211ull;}return hash;};
 auto centroid=[&](const unsigned char* data,unsigned eye){double sum=0;unsigned count=0;for(unsigned y=180;y<220;++y)for(unsigned x=50;x<w-50;++x){auto v=data+y*footprint.Footprint.RowPitch+(eye*w+x)*4;if(v[0]>230&&v[1]>230&&v[2]>230){sum+=x;++count;}}check(count>100,"moving fixture survives generation");return sum/count;};
 std::array<double,2> previousCenter={centroid(pixels,0),centroid(pixels,1)};
 if(zeroTest){const double old=140+((frameCount-2)%30)*8,current=140+((frameCount-1)%30)*8;
  for(auto center:previousCenter)check(center>=old-3&&center<=current+3,"post-prime image uses adjacent fixture positions");}
 const std::array<uint64_t,2> firstHash={hashImage(pixels,0),hashImage(pixels,1)};readback->Unmap(0,nullptr);
 auto previousHash=firstHash;
 for(unsigned intermediate=1;intermediate<generator.count();++intermediate){
  allocator->Reset();list->Reset(allocator.Get(),nullptr);
  transition(list.Get(),target.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
  generator.compositeTo(list.Get(),target.Get(),DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,intermediate);
  transition(list.Get(),target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyTextureRegion(&dest,0,0,0,&src,nullptr);execute();
  check(SUCCEEDED(readback->Map(0,&range,reinterpret_cast<void**>(&pixels))),"second generated image readback");
  const std::array<uint64_t,2> secondHash={hashImage(pixels,0),hashImage(pixels,1)};left=pixels+(h/2)*footprint.Footprint.RowPitch+(w-20)*4;right=left+w*4;
  check(left[2]>left[1]+60&&right[1]>right[2]+60,"second intermediate retains distinct eyes");
  if(zeroTest){for(unsigned eye=0;eye<2;++eye){const auto center=centroid(pixels,eye);printf("Post-prime centroid eye %u: %.3f -> %.3f\n",eye,previousCenter[eye],center);check(center>previousCenter[eye]+.25,"post-prime temporal interpolation progresses toward next real endpoint");previousCenter[eye]=center;}}
  readback->Unmap(0,nullptr);printf("MFG per-eye image hashes: L %016llx / %016llx, R %016llx / %016llx\n",firstHash[0],secondHash[0],firstHash[1],secondHash[1]);
  check(previousHash[0]!=secondHash[0]&&previousHash[1]!=secondHash[1]&&firstHash[0]!=secondHash[0]&&firstHash[1]!=secondHash[1],"MFG produces distinct moving intermediate images for both eyes");
  previousHash=secondHash;
 }
 if(circle::fg::requestedMultiplier()==4&&!fractionalTest&&!circle::fg::fractionalMultiplier()){
  unsigned sequence=9;
  for(unsigned multiplier:{2u,3u,1u,4u,2u}){
   generator.selectMultiplier(multiplier);
   check(generator.count()==multiplier-1,"dynamic factor accepted");
   for(unsigned warm=0;warm<2;++warm,++sequence){
    allocator->Reset();list->Reset(allocator.Get(),nullptr);
    circle::fg::Identity id{sequence,1,{sequence,sequence},{2*sequence-1,2*sequence},w,h,true,false,true};
    const bool made=generator.record(list.Get(),stereo.Get(),inputs,id);
    execute();generator.pollTiming();
    check(made==(multiplier>1&&warm==1),"factor switch resets history and warms both eyes");
   }
   printf("PASS dynamic factor %u\n",multiplier);
  }
 }
 if(prefetchTest)puts("PASS: split FG preparation / later presentation submissions retain both eyes and history");
 generator.destroyIdle();
 unsigned errors=0;if(messages)for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T size{};messages->GetMessage(i,nullptr,&size);std::vector<unsigned char> buffer(size);auto m=reinterpret_cast<D3D12_MESSAGE*>(buffer.data());messages->GetMessage(i,m,&size);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){fprintf(stderr,"D3D12: %s\n",m->pDescription);++errors;}}
 check(!errors,"D3D12 validation errors");CloseHandle(event);puts("PASS: independent generator eyes, warmup, GPU fences, color conversion and composite. No XR delivery claimed.");return 0;
}
