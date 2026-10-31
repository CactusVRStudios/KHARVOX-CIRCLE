// Bringing VR back after the headset or its runtime went away (common/xr_recovery.hpp). The D3D12 device, its
// queue and fences, and the shared ring with the game's side of it stay as they are; the OpenXR instance,
// session, spaces and swapchains are made again on the same device once the runtime finds the headset. The
// game runs flat meanwhile, as it does before the first session starts.

#include "vkcore/fence_wait.hpp"
#include "vkcore/pose_guards.hpp"
#include "vkcore/presenter_impl.hpp"
#include "vkcore/presenter_menu_release.hpp"

#include "vkcore/status_file.hpp"

#include <cstring>
#include <optional>

namespace circlevr::vkcore {

namespace {

// Every D3D12 copy into the old swapchains must finish before they go; a copy held since a stall (frame())
// gives its ring slot back once it has.
bool copiesDone(XrPresenter::Impl& p) {
    if (!waitFence(p.copyFence.Get(), p.copyFenceValue, p.copyEvent, 2000)) {
        return false;
    }
    if(!p.retireGpuCopy())return false;
    if (p.heldCopy.stalled) {
        p.ring[p.heldCopy.slot].state.store(kSlotFree);
        p.heldCopy.stalled = false;
    }
    return true;
}

// A removed D3D12 device (the graphics card was reset: DXGI_ERROR_DEVICE_HUNG, _RESET or _REMOVED) never
// takes a session again; the runtime refuses it (XR_ERROR_GRAPHICS_DEVICE_INVALID) for the rest of the game.
// VR stays off and the launcher says why, instead of waiting for a headset that is not the problem.
bool deviceRemoved(XrPresenter::Impl& p) {
    const HRESULT reason = p.d3dDevice->GetDeviceRemovedReason();
    if (SUCCEEDED(reason)) {
        return false;
    }
    p.loggedDeviceRemoved = true;
    p.logD3dHealth();
    ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    if (SUCCEEDED(p.d3dDevice.As(&dred))) {
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
        if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs))) {
            unsigned nodes=0;
            for(auto* n=breadcrumbs.pHeadAutoBreadcrumbNode;n&&nodes++<32;n=n->pNext){
                const auto done=n->pLastBreadcrumbValue?*n->pLastBreadcrumbValue:0;
                CIRCLE_VR_LOG("d3d12 DRED: queue %ls list %ls completed %u/%u",
                    n->pCommandQueueDebugNameW?n->pCommandQueueDebugNameW:L"unnamed",
                    n->pCommandListDebugNameW?n->pCommandListDebugNameW:L"unnamed",done,n->BreadcrumbCount);
                if(n->pCommandHistory)for(unsigned i=done>4?done-4:0;i<n->BreadcrumbCount&&i<done+8;++i)
                    CIRCLE_VR_LOG("d3d12 DRED: operation %u = %u",i,unsigned(n->pCommandHistory[i]));
            }
        }
        D3D12_DRED_PAGE_FAULT_OUTPUT1 fault{};
        if(SUCCEEDED(dred->GetPageFaultAllocationOutput1(&fault))){
            CIRCLE_VR_LOG("d3d12 DRED: page fault address %llx",fault.PageFaultVA);
            for(int released=0;released<2;++released){
                unsigned nodes=0;
                for(auto* n=released?fault.pHeadRecentFreedAllocationNode:fault.pHeadExistingAllocationNode;n&&nodes++<24;n=n->pNext)
                    CIRCLE_VR_LOG("d3d12 DRED: %s allocation type %u name %ls",released?"freed":"live",unsigned(n->AllocationType),n->ObjectNameW?n->ObjectNameW:L"unnamed");
            }
        }
    }
    CIRCLE_VR_LOG("xr: reconnect: the presenter's D3D12 device was removed (0x%08lx): the graphics card was reset; "
            "VR off",
            static_cast<unsigned long>(reason));
    status::flat("the graphics card was reset; VR is off");
    return true;
}

// The frame loop's state that belongs to one session.
void resetSessionState(XrPresenter::Impl& p) {
    // copiesDone() has drained the queue before releasing any prepared leases.
    if(p.fgPrepared.slot<kRingSize){p.ring[p.fgPrepared.slot].fgInputs={};p.ring[p.fgPrepared.slot].state.store(kSlotFree);}
    p.fgPrepared={};
    if(p.fgEndpointSlot<kRingSize){p.ring[p.fgEndpointSlot].fgInputs={};p.ring[p.fgEndpointSlot].state.store(kSlotFree);}
    p.fgEndpointSlot=UINT32_MAX;p.fgCadence.reset();p.fgGenerator.reset();p.fgPreviousValid=p.fgAwaitAck=p.fgGenerated=false;
    p.sessionState = XR_SESSION_STATE_UNKNOWN;
    p.sessionRunning = false;
    p.sessionFocused.store(false, std::memory_order_relaxed);
    p.clock.wasFocused = false;
    p.clock.watch.reset();
    p.clock.gaps.restart();
    p.acquiredIndex = -1;
    p.acquiredWaited = false;
    p.hasImage = false;
    p.shownHasView = false;
    p.quadPlaced = false;
    p.quadReadiness = {};
    p.cinemaEntry = {};
    p.placeAttempts = 0;
    p.fovWatch.reset();
    pose_guards::resetAll();
    p.loggedFirstProjection = false;
    p.endFrameFailures = 0;
    p.loss.beginFailures = 0;
    p.loss.beginFailedQpc = 0;
}

// One attempt: a new instance, the headset's system on the same graphics card, and a session on the
// presenter's D3D12 device. False leaves nothing made.
bool tryReconnect(XrPresenter::Impl& p, bool log) {
    if (!p.createXrInstance(true)) {
        if (log) {
            CIRCLE_VR_LOG("xr: reconnect: the headset runtime is not answering yet");
        }
        return false;
    }
    XrSystemGetInfo info{XR_TYPE_SYSTEM_GET_INFO};
    info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    const XrResult system = p.xr.xrGetSystem(p.instance, &info, &p.systemId);
    if (XR_FAILED(system)) {
        if (log) {
            char text[XR_MAX_RESULT_STRING_SIZE];
            CIRCLE_VR_LOG("xr: reconnect: no headset yet (%s)", p.xrText(system, text));
        }
        return false;
    }
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    p.xr.xrGetSystemProperties(p.instance, p.systemId, &sp);
    VkExtent2D ring{};
    {
        std::lock_guard lock(p.mutex);
        ring = p.ringExtent;
    }
    // The ring stays, so the headset must take its images as they are.
    if (sp.graphicsProperties.maxSwapchainImageWidth < ring.width ||
        sp.graphicsProperties.maxSwapchainImageHeight < ring.height) {
        if (log) {
            CIRCLE_VR_LOG("xr: reconnect: '%s' takes images up to %ux%u, smaller than the %ux%u in use",
                    sp.systemName, sp.graphicsProperties.maxSwapchainImageWidth,
                    sp.graphicsProperties.maxSwapchainImageHeight, ring.width, ring.height);
        }
        return false;
    }
    XrGraphicsRequirementsD3D12KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
    const LUID ours = p.d3dDevice->GetAdapterLuid();
    if (XR_FAILED(p.xr.xrGetD3D12GraphicsRequirementsKHR(p.instance, p.systemId, &req)) ||
        std::memcmp(&req.adapterLuid, &ours, sizeof(LUID)) != 0) {
        if (log) {
            CIRCLE_VR_LOG("xr: reconnect: the runtime now uses another graphics card than the game");
        }
        return false;
    }
    {
        std::lock_guard lock(p.mutex);
        p.maxSwapchainWidth = sp.graphicsProperties.maxSwapchainImageWidth;
        p.maxSwapchainHeight = sp.graphicsProperties.maxSwapchainImageHeight;
    }
    CIRCLE_VR_LOG("xr: reconnect: system '%s'", sp.systemName);
    return p.createSession();
}

} // namespace

bool XrPresenter::Impl::reconnect() {
    if (loss.beginGaveUp) {
        CIRCLE_VR_LOG("xr: no reconnect: xrBeginSession kept losing the session; VR is off for this game");
        return false;
    }
    releaseMenuInput(*this, "the session ended");
    consumerAlive.store(false); // the game's presents pass through meanwhile
    const LONGLONG lostQpc = qpcNow();
    CIRCLE_VR_LOG("xr: reconnecting (%s); the D3D12 device and the ring stay, the OpenXR objects are made again",
            xr_recovery::lossName(loss.kind));
    for (std::uint32_t attempt = 0; !stop.load(); ++attempt) {
        if (deviceRemoved(*this)) {
            return false;
        }
        // In short steps, so shutdown is not held up.
        for (std::uint32_t waited = 0; waited < xr_recovery::retryDelayMs(attempt) && !stop.load();
             waited += 100) {
            Sleep(100);
        }
        if (stop.load()) {
            break;
        }
        const bool log = xr_recovery::logsAttempt(attempt);
        if (!copiesDone(*this)) {
            if (log) {
                CIRCLE_VR_LOG("xr: reconnect: the last copy to the headset has not finished; waiting");
            }
            continue;
        }
        destroyXrObjects();
        resetSessionState(*this);
        if (!tryReconnect(*this, log)) {
            destroyXrObjects();
            status::waiting(
                "the headset disconnected or its runtime stopped: reconnect it or start the runtime "
                "again (VR comes back by itself)");
            continue;
        }
        loss.lost = false;
        if (!standaloneDiagnostic) { createLoadingImage(); createControllerGuides(); }
        ++loss.reconnects;
        // The new LOCAL space may not be where the old one was: re-anchored as after a recenter (the posture
        // stays).
        room.onSpaceChange(std::nullopt, true);
        menuReplace.store(true, std::memory_order_relaxed);
        consumerAlive.store(true);
        status::waiting("the headset is back; waiting for its session to start");
        CIRCLE_VR_LOG("xr: reconnected after %.1f s (attempt %u, reconnect %u); waiting for the session to start",
                qpcSeconds(qpcNow() - lostQpc), attempt + 1, loss.reconnects);
        return true;
    }
    return false;
}

} // namespace circlevr::vkcore
