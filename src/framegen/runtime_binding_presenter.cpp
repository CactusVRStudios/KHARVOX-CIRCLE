#include "vkcore/presenter_impl.hpp"
#include "vkcore/log.hpp"
namespace circlevr::vkcore {
bool XrPresenter::Impl::retireGpuCopy(){
    if(!pendingGpuCopy.active())return true;
    const auto completed=copyFence->GetCompletedValue();
    if(completed==UINT64_MAX){
        CIRCLE_VR_LOG("steamvr-async-copy: device removed; retaining GPU leases and stopping XR worker");
        stop.store(true);return false;
    }
    if(!pendingGpuCopy.ready(completed)){++asyncCopyBusy;return false;}
    fgGenerator.pollTiming();
    if(!pendingGpuCopy.generated){
        auto& slot=ring[pendingGpuCopy.slot];
        slot.fgInputs={};slot.state.store(kSlotFree);
    }
    pendingGpuCopy={};
    const auto now=qpcNow();
    if(!asyncCopyReport||qpcSeconds(now-asyncCopyReport)>=5.){
        asyncCopyReport=now;
        CIRCLE_VR_LOG("steamvr-async-copy: handedOver %llu GPU-busy polls %llu; leases retired after fence, no CPU wait before release",
            asyncCopies,asyncCopyBusy);
    }
    return true;
}
bool XrPresenter::Impl::runtimeAcquireJoin(){
    if(runtimeBinding.acquire())return true;
    CIRCLE_VR_LOG("steamvr-handover: acquire fence join failed; stopping XR worker");
    stop.store(true);return false;
}
bool XrPresenter::Impl::runtimeReleaseJoin(){
    if(runtimeBinding.release())return true;
    CIRCLE_VR_LOG("steamvr-handover: release fence join failed; stopping XR worker");
    stop.store(true);return false;
}
}
