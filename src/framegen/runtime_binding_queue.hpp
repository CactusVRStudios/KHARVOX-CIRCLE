#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace circle::fg {
// Worker-thread owned. The OpenXR runtime sees only this queue's handovers;
// subsequent synthesis on work_ cannot extend an already submitted watermark.
class RuntimeBindingQueue {
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> work_,binding_;
    Microsoft::WRL::ComPtr<ID3D12Fence> release_,acquire_;
    std::uint64_t released_{},acquired_{};
public:
    bool initialize(ID3D12Device* device,ID3D12CommandQueue* work){
        if(binding_||!device||!work)return false;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> binding;
        Microsoft::WRL::ComPtr<ID3D12Fence> release,acquire;
        D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.NodeMask=work->GetDesc().NodeMask;
        if(FAILED(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&binding)))||
           FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&release)))||
           FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&acquire))))return false;
        work_=work;binding_=binding;release_=release;acquire_=acquire;
        released_=acquired_=0;
        binding_->SetName(L"Circle SteamVR runtime handover queue");
        return true;
    }
    ID3D12CommandQueue* queue()const{return binding_.Get();}
    std::uint64_t releases()const{return released_;}
    std::uint64_t acquires()const{return acquired_;}
    // After successful xrWaitSwapchainImage: runtime ownership -> writer.
    bool acquire(){return !binding_||
        (SUCCEEDED(binding_->Signal(acquire_.Get(),++acquired_))&&
         SUCCEEDED(work_->Wait(acquire_.Get(),acquired_)));}
    // Before release/end: only writes already queued for this handover.
    bool release(){return !binding_||
        (SUCCEEDED(work_->Signal(release_.Get(),++released_))&&
         SUCCEEDED(binding_->Wait(release_.Get(),released_)));}
    bool drain(HANDLE event,DWORD timeout){
        if(!binding_)return true;
        if(!event||!release()||FAILED(binding_->Signal(acquire_.Get(),++acquired_)))return false;
        const auto done=acquire_->GetCompletedValue();
        if(done==UINT64_MAX)return false;
        if(done>=acquired_)return true;
        return SUCCEEDED(acquire_->SetEventOnCompletion(acquired_,event))&&
            WaitForSingleObject(event,timeout)==WAIT_OBJECT_0&&
            acquire_->GetCompletedValue()!=UINT64_MAX;
    }
    // A GPU timeout must retain all queued fence references until process exit.
    void reset(bool completed){
        if(completed){work_.Reset();binding_.Reset();release_.Reset();acquire_.Reset();}
        else{work_.Detach();binding_.Detach();release_.Detach();acquire_.Detach();}
        released_=acquired_=0;
    }
};
}
