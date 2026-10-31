#include "runtime_binding_queue.hpp"
#include "gpu_copy_lease.hpp"
#include <dxgi1_6.h>
#include <cstdio>
using Microsoft::WRL::ComPtr;
int main(){
    unsigned failures{};
    auto check=[&](bool ok,const char* why){if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}};
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;ComPtr<ID3D12Device> device;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||
       FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))||
       FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)))){
        std::fprintf(stderr,"WARP D3D12 device unavailable\n");return 1;
    }
    D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> work;
    ComPtr<ID3D12Fence> gate,after,shown;
    if(FAILED(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&work)))||
       FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)))||
       FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&after)))||
       FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&shown))))return 1;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return 1;
    auto wait=[&](ID3D12Fence* fence,std::uint64_t value,DWORD ms){
        if(fence->GetCompletedValue()>=value)return true;
        return SUCCEEDED(fence->SetEventOnCompletion(value,event))&&WaitForSingleObject(event,ms)==WAIT_OBJECT_0&&
            fence->GetCompletedValue()>=value&&fence->GetCompletedValue()!=UINT64_MAX;
    };
    circle::fg::RuntimeBindingQueue isolated;
    check(isolated.acquire()&&isolated.release(),"shared-queue fallback does not require fences");
    check(!isolated.initialize(nullptr,work.Get()),"invalid initialization preserves fallback");
    if(!isolated.initialize(device.Get(),work.Get())){CloseHandle(event);return 1;}
    check(isolated.queue()!=work.Get(),"runtime has independent queue");
    check(!isolated.initialize(device.Get(),work.Get()),"live queue cannot be replaced without draining");
    check(isolated.release(),"handover captures completed work watermark");
    check(SUCCEEDED(work->Wait(gate.Get(),1)),"block future synthesis behind captured watermark");
    work->Signal(after.Get(),1);
    isolated.queue()->Signal(shown.Get(),1);
    check(wait(shown.Get(),1,2000),"runtime can finish prior handover while future synthesis is blocked");
    check(after->GetCompletedValue()==0,"future work remains blocked, so isolation was exercised");
    gate->Signal(1);check(wait(after.Get(),1,2000),"unblock future work");
    isolated.queue()->Wait(gate.Get(),2);
    check(isolated.acquire(),"reacquisition joins runtime ownership back to writer");
    work->Signal(after.Get(),2);
    Sleep(10);
    check(after->GetCompletedValue()==1,"writer cannot run before runtime relinquishes acquired image");
    gate->Signal(2);check(wait(after.Get(),2,2000),"writer resumes when ownership transfers");
    // Submit an unfinished copy, hand it over without a CPU wait, then verify
    // both compositor consumption and producer-slot reuse remain fenced.
    work->Wait(gate.Get(),3);
    work->Signal(after.Get(),3);
    circle::fg::GpuCopyLease lease{2,3,false};
    check(isolated.release(),"unfinished copy may be handed over with GPU synchronization");
    isolated.queue()->Signal(shown.Get(),3);
    Sleep(10);
    check(shown->GetCompletedValue()<3,"runtime cannot consume an unfinished async copy");
    check(!lease.ready(after->GetCompletedValue()),"producer lease stays pinned while copy is pending");
    check(!lease.ready(UINT64_MAX),"removed device is not successful GPU retirement");
    gate->Signal(3);
    check(wait(shown.Get(),3,2000),"runtime consumes async image once copy completes");
    check(lease.ready(after->GetCompletedValue()),"producer slot can retire after its copy fence");
    lease={};check(!lease.active(),"retired lease permits subsequent recording");
    check(isolated.drain(event,2000),"both queue chains drain before teardown");
    isolated.reset(true);check(!isolated.queue(),"session teardown clears binding");
    check(isolated.initialize(device.Get(),work.Get()),"session recreation gets new fence timelines");
    check(isolated.acquires()==0&&isolated.releases()==0,"recreated counters start clean");
    check(isolated.acquire()&&isolated.release()&&isolated.drain(event,2000),"recreated session can roundtrip ownership");
    isolated.reset(true);CloseHandle(event);
    std::printf("WARP runtime queue isolation: %u failure(s)\n",failures);
    return failures?1:0;
}
