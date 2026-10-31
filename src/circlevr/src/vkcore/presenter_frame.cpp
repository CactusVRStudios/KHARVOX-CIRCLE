#include "motor/interaction_runtime.hpp"
#include "motor/cinema_timing.hpp"
#include "motor/hud_projection.hpp"
#include "framegen/pacing.hpp"
#include "motor/hand_runtime.hpp"
#include "motor/locomotion_probe.hpp"
// The XR worker: its thread body and the frame loop (one image per XR frame, the projection layer for
// head-tracked images and the cinema quad otherwise).

#include "vkcore/fence_wait.hpp"
#include "features/pacing/slot_choice.hpp"
#include "motor/sequential.hpp"
#include "vkcore/presenter_impl.hpp"
#include "vkcore/presenter_menu_release.hpp"

#include "features/comfort/vignette.hpp"
#include "game/eternal/game_action.hpp"
#include "vkcore/controllers.hpp"
#include "motor/controller_tracking.hpp"
#include "vkcore/debug_commands.hpp"
#include "vkcore/frame_pacing.hpp"
#include "vkcore/gpu_timing.hpp"
#include "vkcore/head_sweep.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/light_cull.hpp"
#include "vkcore/menu_model_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/stall_watch.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/test_keys.hpp"
#include "vkcore/ui_engine.hpp"
#include "vkcore/vram_watch.hpp"
#include "xr_math/cinema_quad.hpp"
#include "motor/experimental.hpp"

#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace circlevr::vkcore {

void XrPresenter::Impl::updateImage(LONGLONG frameStart, bool retry) {
    // The frames table's reason until a new image is taken or nothing newer is found handed over.
    frameLogEyes.repeat = FrameRepeat::Waiting;
    if(!retireGpuCopy()){++xrBusyRepeats;return;}
    if(fgAwaitAck)return; // retry the released XR image until xrEndFrame accepts it
    if(!retry) frame_pacing::onHeadsetFrame(displayPeriod.load());
    if(fgPrepared.slot!=UINT32_MAX){
        const auto started=qpcNow();
        const bool alreadyDone=copyFence->GetCompletedValue()>=fgPrepared.fence;
        if(!alreadyDone&&!waitFenceFor(copyFence.Get(),fgPrepared.fence,copyEvent,
             pacing::newestWaitSeconds(displayPeriod.load()*1e-9,qpcSeconds(started-frameStart)))){
            ++xrBusyRepeats;return; // keep allocator, inputs and outputs until GPU completion
        }
        static uint64_t prepared{},ready{};static LONGLONG report{};
        ++prepared;if(alreadyDone)++ready;
        if(!report||qpcSeconds(qpcNow()-report)>=5){report=qpcNow();
            CIRCLE_VR_LOG("vr-fg-prefetch: prepared %llu readyBeforeXR %llu lastWait %.3f ms",prepared,ready,qpcSeconds(qpcNow()-started)*1000.);
        }
        fgGenerator.pollTiming();
    }
    if (heldCopy.stalled) {
        if (copyFence->GetCompletedValue() < copyFenceValue) {
            ++xrRepeats;
            // Held this long, the GPU itself is in trouble (a graphics card reset), not one slow frame.
            if (!heldCopy.longLogged && qpcSeconds(qpcNow() - heldCopy.sinceQpc) >= 2.0) {
                heldCopy.longLogged = true;
                heldCopy.logged = true;
                CIRCLE_VR_LOG(
                    "d3d12: copy did not finish within 2 s; slot %u and the XR image are held until it does",
                    heldCopy.slot);
            }
            return;
        }
        heldCopy.stalled = false;
        if (heldCopy.logged) {
            CIRCLE_VR_LOG("d3d12: the held copy finished after %.0f ms; copies resume",
                    qpcSeconds(qpcNow() - heldCopy.sinceQpc) * 1000.0);
        }
        completeCopy(heldCopy.slot, heldCopy.value, heldCopy.view, heldCopy.hasView);
        return;
    }
    const std::uint64_t packed = latest.load();
    const std::uint64_t newest = packed >> 2;
    if (newest <= lastConsumed && fgEndpointSlot==UINT32_MAX) {
        ++xrRepeats;
        frameLogEyes.repeat = frameLogEyes.lastPresent.load(); // why the game's last present handed none
        return;
    }
    // OpenXR swapchain rules (T-081): after a timeout the image stays acquired and is waited on again
    // next frame; it is released only after a successful wait.
    if (acquiredIndex < 0) {
        std::uint32_t index = 0;
        if (XR_FAILED(xr.xrAcquireSwapchainImage(xrSwapchain, nullptr, &index))) {
            return;
        }
        acquiredIndex = index;
        acquiredWaited = false;
    }
    if (!acquiredWaited) {
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = kSwapchainWaitTimeout;
        const XrResult r = timedCall(inRuntime.waitImage, xr.xrWaitSwapchainImage, xrSwapchain, &wait);
        if (r == XR_TIMEOUT_EXPIRED) {
            CIRCLE_VR_LOG("xr: swapchain image wait timed out; repeating the last image");
            return;
        }
        if (XR_FAILED(r)) {
            return;
        }
        acquiredWaited = true;
        if(!runtimeAcquireJoin())return;
    }

    // A slot whose frame finished rendering (presenter_ring.cpp); none yet: the image stays acquired.
    std::uint32_t slotIndex = 0;
    std::uint64_t value = 0;
    if(fgPrepared.slot!=UINT32_MAX){slotIndex=fgPrepared.slot;value=fgPrepared.value;}
    else if(fgEndpointSlot!=UINT32_MAX){slotIndex=fgEndpointSlot;value=fgEndpointValue;}
    else if (!takeRenderedSlot(slotIndex, value, frameStart)) {
        ++xrBusyRepeats;
        return;
    }
    RingSlot& slot = ring[slotIndex];
    ViewRecord slotView = slot.view;
    const bool slotHasView = slot.hasView;
    const bool slotUi = slot.ui.written && acquireUiXrImage(); // UI layer: the GUI image goes along
    // Our own reference since the swapchain was made (presenter_swapchains.hpp): valid even if the runtime
    // lets go of the image early.
    ID3D12Resource* target = xrImages[static_cast<std::size_t>(acquiredIndex)].Get();

    d3dAllocator->Reset();
    d3dList->Reset(d3dAllocator.Get(), nullptr);
    bool generated=false;
    if(fgPrepared.slot!=UINT32_MAX){
        slotView=fgPrepared.view;generated=fgPrepared.generated;
        fgCopyGenerated=generated;fgPrepared={};
    }else generated=prepareFramegen(slot,slotIndex,value,slotView);
    std::array<D3D12_RESOURCE_BARRIER, 2> barriers{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = slot.resource.Get();
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = target;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    d3dList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = target;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = slot.resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    if(generated){
        auto toRender=barriers[1];std::swap(toRender.Transition.StateBefore,toRender.Transition.StateAfter);
        d3dList->ResourceBarrier(1,&toRender);
        fgGenerator.compositeTo(d3dList.Get(),target,DXGI_FORMAT(xrSwapchainFormat),fgOutputIndex);
        d3dList->ResourceBarrier(1,&barriers[1]);
    }else d3dList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    for (auto& b : barriers) {
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    }
    d3dList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    if (slotUi) {
        recordUiXrCopy(slot);
    }
    const HRESULT closed = d3dList->Close();
    if (FAILED(closed)) {
        // A list that failed to close must never be executed (the device would be removed).
        if (!loggedCloseFailure) {
            loggedCloseFailure = true;
            CIRCLE_VR_LOG("d3d12: the copy's command list failed to close (0x%08lx): ring %ux%u, XR image %llux%u; "
                    "the frame is skipped (image %lld of %zu)",
                    static_cast<unsigned long>(closed), ringExtent.width, ringExtent.height,
                    static_cast<unsigned long long>(target->GetDesc().Width), target->GetDesc().Height,
                    static_cast<long long>(acquiredIndex), xrImages.size());
        }
        slot.state.store(kSlotFree);
        fgEndpointSlot=UINT32_MAX;fgGenerator.reset();fgCadence.reset();fgPreviousValid=false;
        return;
    }

    // GPU-side wait for the Vulkan write of this slot, then the copy.
    if(FAILED(d3dQueue->Wait(sharedFence.Get(),value))){
        CIRCLE_VR_LOG("d3d12: copy source GPU wait failed; stopping XR worker");
        stop.store(true);return;
    }
    ID3D12CommandList* lists[] = {d3dList.Get()};
    d3dQueue->ExecuteCommandLists(1, lists);
    if(FAILED(d3dQueue->Signal(copyFence.Get(),++copyFenceValue))){
        CIRCLE_VR_LOG("d3d12: copy completion signal failed; retaining slot and stopping XR worker");
        stop.store(true);return;
    }
    for(auto& input:slot.fgInputs)if(input)input->consumed=copyFenceValue;
    // SteamVR's binding queue waits for this submission on the GPU. Releasing
    // the XR image does not require stalling the CPU; input leases and allocator
    // reuse still wait for the copy fence in retireGpuCopy(). Other runtimes
    // retain the synchronous path. The override permits a same-scene A/B test.
    static const bool asyncEnabled=[](){
        wchar_t value[16]{};
        return !(GetEnvironmentVariableW(L"CIRCLE_STEAMVR_ASYNC_COPY",value,16)==1&&value[0]==L'0');
    }();
    if(runtimeBinding.queue()&&fgTransport&&asyncEnabled){
        pendingGpuCopy={slotIndex,copyFenceValue,fgCopyGenerated};
        ++asyncCopies;
        completeCopy(slotIndex,value,slotView,slotHasView,false);
        return;
    }
    // At most a couple of display periods, so the frame loop never waits on a busy GPU for long (public issue
    // #19); past that the frame shows the last image and a later frame picks the copy up.
    const std::uint32_t waitMs = pacing::copyWaitMs(displayPeriod.load());
    const LONGLONG waitStart = qpcNow();
    const bool copied = waitFence(copyFence.Get(), copyFenceValue, copyEvent, waitMs);
    copyWait.add(qpcSeconds(qpcNow() - waitStart) * 1000.0);
    if (!copied) {
        // The slot stays marked as being read, so the game never writes it while D3D12 might, and
        // nothing is recorded again (the allocator may still be executing) until the copy is done.
        ++heldCopy.count;
        heldCopy.logged = heldCopy.count <= 5 || heldCopy.count % 100 == 0;
        heldCopy.longLogged = false;
        if (heldCopy.logged) {
            CIRCLE_VR_LOG(
                "d3d12: copy did not finish within %u ms; slot %u and the XR image are held until it does "
                "(%u so far)",
                waitMs, slotIndex, heldCopy.count);
        }
        heldCopy.sinceQpc = waitStart;
        heldCopy.stalled = true;
        heldCopy.slot = slotIndex;
        heldCopy.value = value;
        heldCopy.view = slotView;
        heldCopy.hasView = slotHasView;
        return;
    }
    completeCopy(slotIndex, value, slotView, slotHasView);
}

void XrPresenter::Impl::completeCopy(std::uint32_t slotIndex,
                                     std::uint64_t value,
                                     const ViewRecord& view,
                                     bool hasView, bool gpuComplete) {
    frameLogEyes.shown = ring[slotIndex].eyes; // read before the slot is free again
    frameLogEyes.repeat = FrameRepeat::New;
    // Query data belongs to this completed GPU submission, including warmup.
    if(gpuComplete)fgGenerator.pollTiming();
    fgGenerated=fgCopyGenerated;
    if(gpuComplete&&!fgGenerated){ring[slotIndex].fgInputs={};ring[slotIndex].state.store(kSlotFree);}
    // Generated output retains the actual endpoint slot (color + input leases)
    // through both GPU consumers and until the next successful real output.
    fgAwaitAck=circle::fg::requested();
    lastConsumed = value;fgCompletedValue=value;

    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if(!runtimeReleaseJoin())return;
    xr.xrReleaseSwapchainImage(xrSwapchain, &release);
    acquiredIndex = -1;
    acquiredWaited = false;
    finishUiXrCopy();
    if (!hasImage) {
        CIRCLE_VR_LOG("xr: first game frame on the screen (timeline value %llu)",
                static_cast<unsigned long long>(value));
    }
    hasImage = true;
    ++performanceImageSerial;
    lastGameImageTicks = GetTickCount64();
    shownView = view;
    shownHasView = hasView;
    ++xrCopies;
}

void XrPresenter::Impl::frame() {
    if (standaloneDiagnostic) { frameDiagnostic(); return; }
    prefetchFramegen();
    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState state{XR_TYPE_FRAME_STATE};
    if (const XrResult r = timedCall(inRuntime.waitFrame, xr.xrWaitFrame, session, &waitInfo, &state);
        XR_FAILED(r)) {
        if (!loseOnRuntimeFailure(r, "xrWaitFrame")) {
            Sleep(5);
        }
        return;
    }
    const LONGLONG frameStart = qpcNow(); // the headset's frame begins (slot_choice.hpp's wait)
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (const XrResult r = xr.xrBeginFrame(session, &beginInfo); XR_FAILED(r)) {
        loseOnRuntimeFailure(r, "xrBeginFrame");
        return;
    }
    refresh.onFrame(state.predictedDisplayPeriod);
    // The camera hook predicts the head for about when the game's next frame will be shown: one display
    // period on, at most 1.5x the headset's refresh period while the runtime throttles
    // (presenter_refresh.hpp); under KHARVOX_CIRCLE_POSE_LEAD the period as reported, later by how late the frames
    // shown were (display_lead.hpp).
    nextDisplayTime.store(state.predictedDisplayTime +
                          refresh.predictionPeriod(state.predictedDisplayPeriod, settings.poseLead) +
                          displayLead.leadNs());
    displayPeriod.store(state.predictedDisplayPeriod);
    if(circle::fg::requested())circle::fg::displayPulse(refresh.framegenPeriod(state.predictedDisplayPeriod),state.shouldRender&&sessionFocused.load());
    circle::motor::cinema_timing::displayPeriod(state.predictedDisplayPeriod,refresh.reportedHz());
    circle::motor::hands::sync(state.predictedDisplayTime,sessionState==XR_SESSION_STATE_FOCUSED);
    if(circle::motor::experimental::enabled()) {
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime=state.predictedDisplayTime;locate.space=localSpace;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        std::array<XrView,2> eyeViews{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};
        std::uint32_t n=0;
        const auto result=xr.xrLocateViews(session,&locate,&viewState,2,&n,eyeViews.data());
        const auto needed=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        circle::motor::experimental::EyeRig rig;
        rig.valid=XR_SUCCEEDED(result) && n==2 && (viewState.viewStateFlags&needed)==needed;
        const auto tracked=XR_VIEW_STATE_ORIENTATION_TRACKED_BIT|XR_VIEW_STATE_POSITION_TRACKED_BIT;
        rig.tracked=rig.valid&&(viewState.viewStateFlags&tracked)==tracked;
        rig.displayTime=state.predictedDisplayTime;
        rig.ticks=GetTickCount64();rig.locatedQpc=qpcNow();
        if(rig.valid)for(unsigned eye=0;eye<2;++eye) {
            const auto& p=eyeViews[eye].pose;
            rig.positions[eye]={p.position.x,p.position.y,p.position.z};
            rig.orientations[eye]={p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w};
            const auto& f=eyeViews[eye].fov;
            rig.fovs[eye]={f.angleLeft,f.angleRight,f.angleUp,f.angleDown};
        }
        circle::motor::experimental::publishRig(rig);
    }
    // Motor: no synthetic input.
    if (settings.mode == Mode::HeadTracked && hooks.gameView && !trackingReady.load() &&
        state.predictedDisplayTime > 0) {
        trackingReady.store(true, std::memory_order_release);
        CIRCLE_VR_LOG("head: tracking ready; the game's view follows the headset");
    }

    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    std::array<XrCompositionLayerQuad,2> cinemaQuads{};
    XrCompositionLayerQuad uiQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad reticleQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::array<XrCompositionLayerProjectionView, 2> projectionViews{};
    XrCompositionLayerQuad fade{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad vignetteQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    std::array<XrCompositionLayerQuad, 2> pointerQuads{},laserQuads{};
    for (XrCompositionLayerQuad& q : pointerQuads) {
        q.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    }
    std::array<XrCompositionLayerQuad, WristHud::kMaxQuads> hudQuads{}; // wrist mode: pieces and wrist quads
    // Back to front: the projection (or the cinema quad), the comfort vignette, the UI quad (or in wrist mode
    // its head-locked pieces and the wrist quads), the reticle or the menu pointer (beam, dot), then the fade
    // on top. Entries are written as layers are added.
    std::array<const XrCompositionLayerBaseHeader*, 12 + WristHud::kMaxQuads> layers{};
    layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
    std::uint32_t layerCount = 0;
    const auto addPointer = [&] {
        const std::uint32_t n = fillPointerQuads(pointerQuads.data());
        for (std::uint32_t i = 0; i < n && layerCount < layers.size() && layerCount < performanceMaxLayers; ++i) {
            layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&pointerQuads[i]);
        }
    };
    bool uiShown = false;
    bool nativePairShown = false;
    bool livePairShown = false;
    if (state.shouldRender) {
        updateImage(frameStart);
        // Publish the predicted rig/input before waiting: game rendering needs
        // this data to produce the next pair. Never gate before that publication.
        const auto realWaitStart=GetTickCount64();
        while(realFrameSubmission.wait(settings.onlySubmitRealFrames,!stop.load(),resizeRequested.load(),
              performanceImageSerial,GetTickCount64()-realWaitStart)){
            Sleep(1);
            // updateImage also finishes an outstanding GPU copy. Its acquisition,
            // timeout and release ownership remain in the existing copy path.
            if(heldCopy.stalled||(latest.load()>>2)>lastConsumed){
                const auto repeatsBefore=xrRepeats,busyBefore=xrBusyRepeats;
                updateImage(frameStart,true);
                // Retries within one application frame are not submitted repeats.
                xrRepeats=repeatsBefore;xrBusyRepeats=busyBefore;
            }
        }
        const bool cinemaImage = !(hasImage && shownHasView && shownView.showEyes) || shownView.cutscene;
        const bool enteredCinema = cinemaEntry.update(cinemaImage);
        // Always consume the request, even while waiting for tracking. Otherwise
        // short-circuiting !quadPlaced queues an extra placement after capture.
        const bool replace = replaceScreen.exchange(false, std::memory_order_acq_rel);
        if (enteredCinema || replace) {
            quadPlaced=false;
            quadReadiness.trackedSince=0;
            placeAttempts=0;
            CIRCLE_VR_LOG("xr: screen re-anchor requested (%s)",enteredCinema?"presented cinema entry":"recenter");
        }
        if (!quadPlaced) placeQuad(state.predictedDisplayTime);
        // ARGENT startup fallback: VIEW space puts the temporary screen at eye
        // height. Only a settled, actively tracked pose may anchor it in LOCAL.
        const XrSpace screenSpace=quadPlaced?localSpace:viewSpace;
        const XrPosef screenPose=quadPlaced?quadPose:XrPosef{{0,0,0,1},{0,0,-kScreenDistanceMetres}};
        if(circle::motor::sequential::requested()){
            circle::motor::experimental::CinemaPanel panel;
            panel.position={quadPose.position.x,quadPose.position.y,quadPose.position.z};
            panel.orientation={quadPose.orientation.x,quadPose.orientation.y,quadPose.orientation.z,quadPose.orientation.w};
            panel.width=quadSize.width;panel.valid=quadPlaced;
            circle::motor::experimental::publishCinemaPanel(panel);
        }
        if (settings.mode == Mode::HeadTracked && fovWatch.due(qpcSeconds(frameStart))) {
            updateTargetFov(state.predictedDisplayTime);
        }
        // Once the multiplayer guard is off the game's image is shown only on the flat cinema quad,
        // never as a head-tracked projection (the camera hook no longer writes the view).
        const bool guardOff = settings.mode == Mode::HeadTracked && !mp_guard::allowsGameTouch();
        if (guardOff && !loggedGuardCinema) {
            loggedGuardCinema = true;
            CIRCLE_VR_LOG("xr: multiplayer guard %s: the game is shown on the flat cinema screen from now on; no "
                    "camera writes, head aim or key injection for the rest of this process",
                    mp_policy::toString(mp_guard::state()));
            status::flat(
                "an online mode or a multiplayer invite switched VR off for this session (restart from "
                "the launcher)");
        }
        // Menus: the game's cursor decides whether a menu is up; its panel shows the UI quad over a
        // head-tracked frame, or the game's whole frame on the cinema path (presenter_menu.cpp).
        const bool motorMenuActive=circle::motor::sequential::menuActive();
        const bool headTracked = !motorMenuActive && hasImage && shownHasView && !guardOff &&
            hybrid.mode == circle::motor::RenderMode::GameplayStereo;
        if (settings.mode == Mode::HeadTracked) updateMenu(state.predictedDisplayTime, headTracked ? settings.ui.enabled && uiFresh() : hasImage);
        if(!motorMenuActive&&circle::motor::sequential::requested()&&hasImage&&shownHasView&&shownView.showEyes&&shownView.cutscene){
            // ARGENT StereoProjection: the same fixed 16:9 plane, one image
            // per eye. Publish only complete same-token pairs, never AER.
            // Flat and stereo film render over the entire texture, with authored
            // 16:9 optics. The panel supplies its display aspect. Applying the
            // old native letterbox crop here would crop/stretch it a second time.
            const auto band=ui_layer::PixelRect{0,0,eyeExtent.width,eyeExtent.height};
            for(unsigned e=0;e<2;++e){auto& q=cinemaQuads[e];q.type=XR_TYPE_COMPOSITION_LAYER_QUAD;
                q.space=screenSpace;q.pose=screenPose;q.size={quadSize.width,quadSize.width*9.f/16.f};
                q.eyeVisibility=e?XR_EYE_VISIBILITY_RIGHT:XR_EYE_VISIBILITY_LEFT;
                q.subImage.swapchain=xrSwapchain;
                uint32_t cropX=band.x,cropWidth=band.width;
                if(!shownView.monoCinema){
                    const auto crop=shownView.cinemaCrop[e];
                    if(std::isfinite(crop[0]+crop[1])&&crop[0]>=0&&crop[1]>0&&crop[0]+crop[1]<=1.00001f){
                        cropX=std::min(eyeExtent.width-1,static_cast<uint32_t>(std::lround(crop[0]*eyeExtent.width)));
                        cropWidth=std::clamp(static_cast<uint32_t>(std::lround(crop[1]*eyeExtent.width)),1u,eyeExtent.width-cropX);
                    }
                }
                q.subImage.imageRect={{static_cast<int32_t>(cropX+e*eyeExtent.width),static_cast<int32_t>(band.y)},
                    {static_cast<int32_t>(cropWidth),static_cast<int32_t>(band.height)}};
                layers[e]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&q);
            }
            quad=cinemaQuads[0];layerCount=2;
            if(shownView.monoCinema){
                // One identical center-camera image for both eyes. Use the
                // frame's metadata, not a newer gameplay/menu state.
                quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
                layers[0]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
                layerCount=1;
            }
            ++xrQuadFrames;
        } else if(!motorMenuActive&&(circle::motor::experimental::liveActive() || circle::motor::sequential::requested()) && hasImage && shownHasView && shownView.showEyes &&
           (circle::motor::sequential::requested() || (!heldCopy.stalled && qpcSeconds(frameStart-shownView.locatedQpc)<0.25))) {
            fillProjectionViews(projectionViews);
            projection.space=localSpace;projection.viewCount=2;projection.views=projectionViews.data();
            layers[0]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);layerCount=1;
            livePairShown=true;++xrProjectionFrames;
        } else if (!circle::motor::experimental::liveEnabled() && !circle::motor::sequential::requested() && !heldCopy.stalled && fillNativePair(projectionViews)) {
            projection.space=localSpace;
            projection.viewCount=2;
            projection.views=projectionViews.data();
            layers[0]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
            layerCount=1;
            nativePairShown=true;
            ++xrProjectionFrames;
        } else if (headTracked) {
            // Mono: both eyes get the whole image with the head pose and FOV it was rendered with. Stereo:
            // each eye gets its half with its own pose and FOV (presenter_stereo.cpp).
            fillProjectionViews(projectionViews);
            projection.layerFlags = 0;
            projection.space = localSpace;
            projection.viewCount = static_cast<std::uint32_t>(projectionViews.size());
            projection.views = projectionViews.data();
            layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
            layerCount = 1;
            // The comfort vignette goes right over the game's view and under everything else: the HUD in the
            // corners and the aim dot stay readable while the world at the edges darkens, and the collision
            // fade still covers it all. Never with a menu up (the panel is what the player looks at then).
            if (settings.ui.vignette != ui_layer::VignetteMode::Off) {
                const controllers::ArtificialMotion stick = controllers::artificialMotion();
                comfort::VignetteMotion motion;
                motion.turnDegreesPerSecond = stick.turnDegreesPerSecond;
                motion.moveMagnitude = stick.move;
                motion.gameMotion = controllers::forcedView() ||
                                    game::contains(controllers::heldActions(), game::GameAction::Dash);
                const auto make = [this](XrSwapchain& swapchain, const std::vector<std::uint8_t>& pixels,
                                         std::uint32_t width, std::uint32_t height, const char* what) {
                    return createStaticImage(swapchain, pixels, width, height, what);
                };
                if (vignette.prepare(settings.ui.vignette, motion, !menuUp.load(std::memory_order_relaxed),
                                     qpcSeconds(qpcNow()), viewSpace, make, vignetteQuad)) {
                    layers[layerCount++] =
                        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&vignetteQuad);
                }
            }
            // UI layer: the game's GUI on its own quad in front (only with a fresh GUI image).
            uiShown = settings.ui.enabled && fillUiQuad(uiQuad);
            if (uiShown) {
                // Wrist mode (presenter_wrist.cpp): the UI quad's head-locked pieces and the wrist quads;
                // not while the UI quad is the menu panel.
                const std::uint32_t hud =
                    menuOn || menuHeld
                        ? 0
                        : wrist.fill(settings.ui, uiExtent.width, uiExtent.height, uiQuad, hudQuads.data());
                for (std::uint32_t i = 0; i < hud; ++i) {
                    layers[layerCount++] =
                        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&hudQuads[i]);
                }
                if (hud == 0) {
                    layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&uiQuad);
                }
                if (menuOn) {
                    // A menu over the game (pause, the in-game screens): the UI quad becomes the
                    // world-locked menu panel, and the pointer replaces the hand-aim dot.
                    placeOnMenuPanel(uiQuad);
                    addPointer();
                } else if (menuHeld) {
                    // The cursor went but the menu's backdrop is still up: the panel stays, no pointer.
                    placeOnMenuPanel(uiQuad);
                } else if (fillReticleQuad(reticleQuad)) {
                    // Hand aim: the dot on the weapon hand's ray, after the UI quad.
                    layers[layerCount++] =
                        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&reticleQuad);
                }
            }
            // The head in geometry fades the view to black, over everything (docs/VR_ROOMSCALE.md).
            const float alpha = room.fade(qpcSeconds(qpcNow()));
            if (fadeLayer.prepare(xr, runtimeQueue(), alpha, viewSpace, fade)) {
                layers[layerCount] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&fade);
                ++layerCount;
            }
            ++xrProjectionFrames;
            if (!loggedFirstProjection) {
                loggedFirstProjection = true;
                CIRCLE_VR_LOG("xr: first head-tracked frame (view %llu, fov %.2f x %.2f deg)",
                        static_cast<unsigned long long>(shownView.seq),
                        (shownView.fov.angleRight - shownView.fov.angleLeft) * 57.29578f,
                        (shownView.fov.angleUp - shownView.fov.angleDown) * 57.29578f);
            }
        } else if (hasImage || loadingImageReady) {
            const bool loading = hybrid.mode == circle::motor::RenderMode::LoadingQuad;
            const bool stale = loading && (!hasImage || GetTickCount64()-lastGameImageTicks > 2000);
            if (stale != staleLoadingShown) {
                staleLoadingShown=stale;
                CIRCLE_VR_LOG("hybrid: loading presentation %s",stale ? "stable card (no fresh game image)" : "live game image");
            }
            const auto source=circle::motor::quadSource(hybrid.mode,hasImage,
                GetTickCount64()-lastGameImageTicks,loadingImageReady);
            const bool showCard = source == circle::motor::QuadSource::LoadingCard;
            if (source == circle::motor::QuadSource::None) {
                // No fresh image: keep the XR frame loop alive without an invalid layer.
                layerCount=0;
            } else {
                ++xrQuadFrames;
                // Repeat a released image only while it is suitable for the native state.
                quad.layerFlags = 0;
                quad.space = screenSpace;
                quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                quad.subImage.swapchain = showCard ? loadingSwapchain : xrSwapchain;
                quad.subImage.imageRect = {
                    {0, 0},
                    {static_cast<std::int32_t>(eyeExtent.width), static_cast<std::int32_t>(eyeExtent.height)}};
                quad.subImage.imageArrayIndex = 0;
                quad.pose = screenPose;
                quad.size = quadSize;
                if(circle::motor::sequential::requested()&&!showCard){
                    // Motor's native 16:9 framing is restored outside gameplay.
                    // Show its centered content band, never stretch the tall eye target.
                    const auto band=ui_layer::wideContentRect(eyeExtent.width,eyeExtent.height);
                    quad.subImage.imageRect={{static_cast<int32_t>(band.x),static_cast<int32_t>(band.y)},
                        {static_cast<int32_t>(band.width),static_cast<int32_t>(band.height)}};
                    quad.size.height=quad.size.width*9.f/16.f;
                }
                if (showCard) {
                    quad.subImage.imageRect={{0,0},{512,256}};
                    quad.size.height=quad.size.width*0.5f;
                }
                layerCount = 1;
                if (menuOn) {
                    // The title screen and the main menu: the whole frame on the menu panel, with the pointer.
                    placeOnMenuPanel(quad);
                    addPointer();
                } else if (menuHeld) {
                    // The cursor went but the backdrop is still up: retain the stable menu panel.
                    placeOnMenuPanel(quad);
                } else if (!showCard && !circle::motor::sequential::requested()) {
                    if (const auto band = cinemaView.band(eyeExtent.width, eyeExtent.height)) {
                        // A cutscene drawn for a flat display's shape: only its band, on a screen of that shape.
                        quad.subImage.imageRect.offset.y = static_cast<std::int32_t>(band->y);
                        quad.subImage.imageRect.extent.height = static_cast<std::int32_t>(band->height);
                        quad.size.height = quadSize.width * static_cast<float>(band->height) /
                            static_cast<float>(eyeExtent.width);
                    }
                }
            }
        }
        if (circle::motor::sequential::requested() && hasImage && layerCount && settings.ui.enabled) {
            uiShown = fillUiQuad(uiQuad);
            if (uiShown) {
                const auto anchor=circle::motor::hudAnchor(livePairShown,
                    circle::motor::sequential::menuActive(),quad.space!=XR_NULL_HANDLE);
                if (anchor==circle::motor::HudAnchor::CinemaImage) {
                    // The same UI image for BOTH eyes, exactly on the cinema plane.
                    uiQuad.space = quad.space; uiQuad.pose = quad.pose; uiQuad.size = quad.size;
                    circleHudYawTime = 0.0;
                } else if(anchor==circle::motor::HudAnchor::MenuScreen) {
                    // Pause may precede its rendered cinema image, especially
                    // while a generated/real endpoint pair is being delivered.
                    // Submit a valid menu anchor now, so xrEndFrame can accept
                    // the pending image and the next cinema frame can arrive.
                    uiQuad.space=screenSpace;uiQuad.pose=screenPose;
                    uiQuad.size={quadSize.width,quadSize.width*9.f/16.f};
                    circleHudYawTime=0.0;
                    static unsigned transitions=0;
                    if(++transitions<=8)CIRCLE_VR_LOG("xr: early menu HUD anchored to valid screen while gameplay endpoint completes (FG %d pending %u)",fgGenerated,fgEndpointSlot);
                } else if (const auto head = locateHead(state.predictedDisplayTime)) {
                    // Gameplay HUD follows horizontal head heading only. VIEW
                    // space would reintroduce pitch/roll during composition.
                    const auto forward = rotate(head->orientation, Vec3{0, 0, -1});
                    const double hudTime = qpcSeconds(frameStart);
                    const double hudDt = hudTime - circleHudYawTime;
                    if (forward.x*forward.x + forward.z*forward.z > 1e-6f) {
                        const float targetYaw = std::atan2(-forward.x, -forward.z);
                        // Ten-degree deadzone, then time-based soft following.
                        // Startup and explicit recenter still align immediately.
                        if (circleHudYawTime == 0.0 || hudDt <= 0.0 || hudDt > 0.25 || replace)
                            circleHudYaw = targetYaw;
                        else {
                            circleHudYaw = circle::motor::followHudYaw(circleHudYaw, targetYaw, hudDt);
                        }
                    }
                    circleHudYawTime = hudTime;
                    Pose levelHead = *head;
                    levelHead.orientation = Quat::fromAxisAngle(Vec3{0, 1, 0}, circleHudYaw);
                    auto panel = xr_math::cinemaQuadPose(levelHead, settings.ui.distanceMetres);
                    const auto native = circle::motor::locomotion::latestNative();
                    const bool photo = native.valid && GetTickCount64()-native.tick<100 &&
                        circle::motor::hands::weaponSlot(native.entity)==17 &&
                        !native.animated && !native.localSpace && !native.explicitReason;
                    if (photo || circle::motor::interaction::magnifierActive() ||
                        circle::motor::sequential::binocularHudActive()) {
                        // A viewfinder is an aiming reticle: no yaw lag or pitch/roll lock.
                        panel.orientation = head->orientation;
                        panel.position = head->position + forward * settings.ui.distanceMetres;
                        circleHudYawTime = 0.0;
                    } else panel.position.y += settings.ui.offsetYMetres;
                    uiQuad.space = localSpace;
                    uiQuad.pose = {{panel.orientation.x, panel.orientation.y, panel.orientation.z, panel.orientation.w},
                                   {panel.position.x, panel.position.y, panel.position.z}};
                } else {
                    // Do not fall back to a tilted head-locked layer on tracking loss.
                    uiShown = false;
                }
                if (uiShown) layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&uiQuad);
            }
        }
        // A menu's 3D model goes where the panel shows the menu (menu_model_hook.hpp).
        menu_model::notePanel(headTracked && uiShown && (menuOn || menuHeld), uiQuad, uiExtent.width,
                              uiExtent.height);
    } else {
        quadReadiness.trackedSince=0;
        quadPlaced=false;
        releaseMenuInput(*this, "the headset is not showing frames");
    }
    // The GUI leaves the eye images only while the quad shows it; menus and loading screens keep it.
    if (settings.ui.enabled) {
        ui_engine::setSkipComposite(settings.ui.skipComposite && uiShown);
    }
    // Motor uses the actual displayed cinema quad, not Eternal's menu addresses.
    // Always update, including loss of focus/tracking, to release held clicks.
    const XrCompositionLayerQuad* motorMenuPanel=nullptr;
    if(state.shouldRender&&layerCount){
        if(uiShown)motorMenuPanel=&uiQuad;
        else if(!livePairShown&&quad.subImage.swapchain==xrSwapchain)motorMenuPanel=&quad;
    }
    std::array<XrCompositionLayerQuad, 2> controllerGuideQuads{};
    if(motorMenuPanel && layerCount+2<=layers.size() && layerCount+2<=performanceMaxLayers &&
       fillControllerGuides(*motorMenuPanel,controllerGuideQuads)) {
        for(const auto& guide:controllerGuideQuads)
            layers[layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&guide);
    }
    if(updateMotorMenu(state.predictedDisplayTime,motorMenuPanel)){
        menuOn=true;addPointer();menuOn=false;
    }
    if(state.shouldRender&&livePairShown&&!shownView.cutscene&&layerCount+2<=layers.size()){
        const auto count=fillLaserQuads(laserQuads.data());
        for(unsigned i=0;i<count;++i)layers[layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&laserQuads[i]);
    }
    // Only a real game-image layer counts, never the loading card or the panel
    // itself. Keep this decision before adding the performance layer.
    bool performanceGameImage=state.shouldRender&&hasImage&&layerCount&&
        (livePairShown||(!nativePairShown&&projection.viewCount)||quad.subImage.swapchain==xrSwapchain);
    XrCompositionLayerQuad performanceQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(state.shouldRender&&layerCount<layers.size()&&layerCount<performanceMaxLayers&&
       fillPerformanceQuad(performanceQuad,qpcSeconds(frameStart),performanceGameImage))
        layers[layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&performanceQuad);
    // Do not claim a repeated game image as a fresh application frame. Empty
    // end frames on stalls preserve OpenXR lifecycle; no old game/UI layers
    // are re-submitted. The runtime may reproject its last completed frame.
    if(settings.onlySubmitRealFrames&&!realFrameSubmission.fresh(performanceImageSerial)){
        layerCount=0;performanceGameImage=false;
        if(++realFrameEmpty<=3||realFrameEmpty%300==0)
            CIRCLE_VR_LOG("xr: real-only empty heartbeat %llu; copied %llu submitted %llu",
                (unsigned long long)realFrameEmpty,(unsigned long long)performanceImageSerial,
                (unsigned long long)realFrameSubmission.submitted);
    }
    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = state.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = layerCount;
    endInfo.layers = layerCount ? layers.data() : nullptr;
    // Snapshot this handover before the next iteration can prefetch more FG.
    // SteamVR never receives the synthesis queue itself on the isolated path.
    if(!runtimeReleaseJoin())return;
    const auto submissionEntered = qpcNow();
    const XrResult r = timedCall(inRuntime.endFrame, xr.xrEndFrame, session, &endInfo);
    if(circle::fg::requested())refresh.framegenSubmission(submissionEntered,fgGenerated&&performanceGameImage,r==XR_SUCCESS);
    if(runtimeBinding.queue()){
        static LONGLONG lastReport{};
        if(!lastReport||qpcSeconds(submissionEntered-lastReport)>=5){lastReport=submissionEntered;
            CIRCLE_VR_LOG("steamvr-handover: joins acquire %llu release %llu output %s result %d; queued work watermark only, compositor delivery measured separately",
                runtimeBinding.acquires(),runtimeBinding.releases(),performanceGameImage?(fgGenerated?"generated":"real"):"empty",int(r));}
    }
    realFrameSubmission.accept(performanceImageSerial,performanceGameImage,r==XR_SUCCESS);
    acknowledgeFramegen(r==XR_SUCCESS,performanceGameImage,state.predictedDisplayTime);
    performanceStats.sample(qpcSeconds(qpcNow()),pairsPublished.load(std::memory_order_relaxed),
        r==XR_SUCCESS,performanceGameImage,performanceImageSerial,fgGenerated);
    if(circle::motor::sequential::requested()) {
        static int lastLayer=-1;static unsigned layerSamples=0;
        const int kind=livePairShown?1:(layerCount?(cinemaQuads[0].subImage.swapchain?3:2):0);
        if(kind!=lastLayer || (kind==1 && ++layerSamples%300==0)) {
            lastLayer=kind;
            CIRCLE_VR_LOG("motor-vr-layer: %s layers %u token %llu result %d",kind==1?"PROJECTION":kind==3?"STEREO_CINEMA_QUADS":kind==2?"CINEMA_QUAD":"NONE",layerCount,shownView.seq,static_cast<int>(r));
            if(kind==1)for(unsigned i=0;i<2;++i){const auto& v=projectionViews[i];
                CIRCLE_VR_LOG("motor-vr-layer: eye %u fov %.6f %.6f %.6f %.6f pose %.5f %.5f %.5f quat %.5f %.5f %.5f %.5f rect %d %d %d %d swapchain %p array %u",i,v.fov.angleLeft,v.fov.angleRight,v.fov.angleUp,v.fov.angleDown,v.pose.position.x,v.pose.position.y,v.pose.position.z,v.pose.orientation.x,v.pose.orientation.y,v.pose.orientation.z,v.pose.orientation.w,v.subImage.imageRect.offset.x,v.subImage.imageRect.offset.y,v.subImage.imageRect.extent.width,v.subImage.imageRect.extent.height,v.subImage.swapchain,v.subImage.imageArrayIndex);
            }
        }
    }
    if(circle::motor::sequential::requested() && livePairShown) {
        static unsigned submitted=0;
        if(++submitted<=32 || submitted%120==0)
            CIRCLE_VR_LOG("motor-seq-xr: submission %u token %llu predicted %lld result %d pose_age_ms %.3f",submitted,
                static_cast<unsigned long long>(shownView.seq),static_cast<long long>(state.predictedDisplayTime),
                static_cast<int>(r),qpcSeconds(frameStart-shownView.locatedQpc)*1000);
        if(XR_SUCCEEDED(r) && submitted==1) {
            status::vr("experimental native sequential stereo projection submitted; visual verification pending");
            status::stereo(true,"Motor render-only second eye; shared temporal histories remain experimental");
        }
    }
    if(circle::motor::experimental::liveEnabled() && livePairShown) {
        static unsigned liveFrames=0;
        if(++liveFrames<=500 || liveFrames%300==0)
            CIRCLE_VR_LOG("live-xr: frame %u view %llu predicted %lld result %d pose_age_ms %.3f",liveFrames,
                static_cast<unsigned long long>(shownView.seq),static_cast<long long>(state.predictedDisplayTime),
                static_cast<int>(r),qpcSeconds(frameStart-shownView.locatedQpc)*1000);
        if(XR_SUCCEEDED(r) && liveFrames==1) {
            status::vr("experimental continuous native alternating-eye projection submitted; temporal offset between eyes");
            status::stereo(true,"experimental alternating eyes; same-state render re-entry remains unvalidated");
        }
    }
    if(nativePairShown && XR_SUCCEEDED(r)) {
        ++nativePairSubmissions;
        if(nativePairSubmissions==1 || nativePairSubmissions%900==0)
            CIRCLE_VR_LOG("experiment: NATIVE frozen stereo pair xrEndFrame %d successful submissions %llu; human depth verification pending",static_cast<int>(r),static_cast<unsigned long long>(nativePairSubmissions));
    }
    if (XR_SUCCEEDED(r) && layerCount > 0 && quad.subImage.swapchain == loadingSwapchain && loadingSwapchain) {
        static bool loggedLoadingSubmission = false;
        if (!loggedLoadingSubmission) {
            loggedLoadingSubmission = true;
            CIRCLE_VR_LOG("hybrid: stable loading card submitted successfully");
        }
    }
    if (!nativePairShown && !livePairShown && XR_SUCCEEDED(r) && layerCount > 0 && hasImage && quad.subImage.swapchain != loadingSwapchain) {
        static bool loggedSubmission = false;
        if (!loggedSubmission) {
            loggedSubmission = true;
            CIRCLE_VR_LOG("motor: first game image layer submitted successfully; MONO cinema checkpoint only");
            status::vr("mono game image submitted to runtime; human headset verification is separate");
            status::stereo(false, "Motor camera and render re-entry have not been validated");
        }
    }
    if (XR_FAILED(r) && !loseOnRuntimeFailure(r, "xrEndFrame")) {
        // Every frame can fail the same way: the first ones, then one line per 900 (10 s at 90 Hz).
        if (++endFrameFailures <= 5 || endFrameFailures % 900 == 0) {
            char text[XR_MAX_RESULT_STRING_SIZE];
            CIRCLE_VR_LOG("xr: xrEndFrame failed: %s (%u so far)", xrText(r, text), endFrameFailures);
            for(uint32_t i=0;i<layerCount;++i)
                CIRCLE_VR_LOG("xr: rejected layer %u type %d space %p",i,int(layers[i]->type),layers[i]->space);
        }
        // Some runtimes return immediately after invalid layer submission.
        // Do not flood them with thousands of retries and future poses per second.
        Sleep(10);
    }
    ++xrFrames;
    if (layerCount) {
        noteShownView(state);
        logFrame(state);
    }
    afterFrame(state);
    watchFrameClock(state);
}

void XrPresenter::Impl::destroyXrObjects() {
    const bool bindingDrained=runtimeBinding.drain(copyEvent,2000);
    if(bindingDrained)retireGpuCopy();
    if(!bindingDrained)CIRCLE_VR_LOG("steamvr-handover: drain timed out; queue/fences retained through process exit");
    trackingReady.store(false);
    controllers::detach();
    circle::motor::hands::detach();
    disableKeepActive();
    frame_pacing::setUnfocused(false);
    std::unique_lock spaceLock(spaceMutex); // no camera hook is locating the head past this point
    // xrEndSession is only valid in STOPPING; destroying the session is valid in any state.
    if (session && sessionRunning && sessionState == XR_SESSION_STATE_STOPPING) {
        xr.xrEndSession(session);
    }
    sessionRunning = false;
    destroyPerformanceOverlay();
    destroyControllerGuides();
    xrSwapchains.destroy(xr, xrSwapchain);
    xrSwapchains.destroy(xr,nativePairSwapchain);
    nativePairHeader={};nativePairPoll=0;nativePairSubmissions=0;
    xrImages.clear();
    loadingSwapchain=XR_NULL_HANDLE;
    loadingImageReady=false;
    destroyUiXrObjects();
    xrSwapchains.forget(fadeLayer.swapchain());
    fadeLayer.destroy(xr);
    for (const XrSwapchain level : vignette.swapchains()) {
        xrSwapchains.forget(level);
    }
    vignette.destroy(xr);
    xrSwapchains.endSession(xr); // the ones kept unused, then the record
    if (floorSpace) {
        xr.xrDestroySpace(floorSpace);
        floorSpace = XR_NULL_HANDLE;
    }
    if (viewSpace) {
        xr.xrDestroySpace(viewSpace);
        viewSpace = XR_NULL_HANDLE;
    }
    if (localSpace) {
        xr.xrDestroySpace(localSpace);
        localSpace = XR_NULL_HANDLE;
    }
    if (session) {
        xr.xrDestroySession(session);
        session = XR_NULL_HANDLE;
    }
    if (instance) {
        xr.xrDestroyInstance(instance);
        instance = XR_NULL_HANDLE;
    }
    runtimeBinding.reset(bindingDrained);
    steamVrRuntime=false;
}

void XrPresenter::Impl::runWorker() {
    setPassThroughThread(true);
    if(!standaloneDiagnostic && circle::motor::sequential::requested()) {
        std::string error;
        if(!circle::motor::sequential::installObservation(error))
            CIRCLE_VR_LOG("motor-seq: unavailable: %s; cinema fallback",error.c_str());
        else {ringEyes=2;openFrameLog();seqActive.store(true,std::memory_order_release);}
    }
    if(circle::motor::experimental::liveEnabled()) {ringEyes=2;openFrameLog();}
    if(!standaloneDiagnostic && circle::motor::experimental::enabled()) {
        std::string error;
        if(!circle::motor::experimental::installObservation(error))
            CIRCLE_VR_LOG("experiment: selected-view observation unavailable: %s",error.c_str());
    }
    // The code this thread runs, and the hooks and import replacements it installs, must outlive any
    // unload of the layer (the loader unloads layers when an instance is destroyed).
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&moduleDirectory), &self);
    VkExtent2D startExtent{};
    {
        std::lock_guard lock(mutex);
        startExtent = requestedExtent;
    }
    CIRCLE_VR_LOG("xr: worker started for a %ux%u game image; mode %s", startExtent.width, startExtent.height,
            settings.mode == Mode::Cinema ? "cinema"
            : settings.stereo.enabled     ? "stereo"
                                          : "head-tracked");
    if (circle::motor::sequential::requested() && settings.ui.enabled && settings.mode != Mode::HeadTracked) startUi();
    if (settings.mode == Mode::HeadTracked) {
        CIRCLE_VR_LOG("head: world scale %.3f unit(s) per metre, head position %s, game FOV %s, aim %s",
                settings.unitsPerMetre, settings.headPosition ? "on" : "off",
                settings.setGameFov ? "set to the headset's" : "the game's own",
                settings.headAim ? "follows the head" : "the game's own (view only)");
        // The camera hook, the stereo hooks and key injection are installed only while the multiplayer
        // guard is armed; once installed they check it again before every change they make.
        if (mp_guard::allowsGameTouch()) {
            hooks = installViewHooks(this);
            startStereo();
            if (settings.ui.enabled) {
                startUi();
            }
            if (settings.skipCinematics) {
                installKeyInjection();
            }
            controllers::installGameHooks();
            installDebugCommands(); // KHARVOX_CIRCLE_DEBUG_COMMANDS: console commands on a schedule (test rig)
            // On unless KHARVOX_CIRCLE_LIGHT_FRUSTUM=0: a light Umbra hides is kept if the view frustum keeps it.
            installLightFrustumCull();
            stall_watch::installCheckpointSaveHook(); // stall lines name the game's own checkpoint saves
            startMenu();
            if (roomScaleSettings().collision || settings.ui.reticle) { // the aim dot's depth uses it too
                installHeadSweep();
            }
        } else {
            CIRCLE_VR_LOG("head: the multiplayer guard is %s; no camera hook, stereo, head aim or key injection",
                    mp_policy::toString(mp_guard::state()));
        }
        if (settings.poseLead) {
            CIRCLE_VR_LOG("head: pose lead on: the head and hands are predicted for when frames are shown");
        }
        if (settings.swayPeriod > 0.0f) {
            CIRCLE_VR_LOG("head: test sway on: yaw %.1f deg, pitch %.1f deg, period %.1f s around a %.1f deg turn, "
                    "added to the tracked pose",
                    settings.swayYaw, settings.swayPitch, settings.swayPeriod, settings.swayBaseYaw);
        }
        if (!hooks.gameView) {
            CIRCLE_VR_LOG("head: no game view hook; the game's view stays as it is and frames show on the screen");
        }
        openFrameLog();
    }
    stall_watch::trackGameTicks(hooks.gameView); // the camera hook's ticks tell play from loading and menus
    // Every stage is followed by a stop check: once shutdown has begun the game's device may be gone.
    const auto running = [this] {
        return !stop.load();
    };
    bool ok = loadOpenXr() && running() && createXrInstance() && running() && waitForSystem() && running() &&
              createD3D12AndSession() && running() && (standaloneDiagnostic ? createDiagnosticPattern() : createRing());
    if (ok) {
        if (!standaloneDiagnostic) { createLoadingImage(); createControllerGuides(); }
        {
            std::lock_guard lock(mutex);
            if (!standaloneDiagnostic) {
                ringReady.store(true);
                requestRebuildIfStale();
            }
        }
        consumerAlive.store(true);
        CIRCLE_VR_LOG("xr: presenter ready; waiting for the session to start");
        // A lost session leaves the inner loop; reconnect() returns once a new session exists (or at
        // shutdown).
        while (!stop.load()) {
            while (!stop.load() && !loss.lost) {
                if (standaloneDiagnostic && GetTickCount64() >= diagnosticDeadline) { stop.store(true); break; }
                if (resizeRequested.load()) {
                    recreateRing();
                }
                pollEvents();
                if (!standaloneDiagnostic) updateHybrid();
                if (!standaloneDiagnostic) circle::motor::experimental::pollObservation();
                refreshDisplayRate(); // between frames: xrWaitFrame has the slack
                vram::poll();
                if (sessionRunning) {
                    frame();
                } else {
                    Sleep(10);
                }
            }
            if (stop.load() || standaloneDiagnostic || !xr_recovery::recovers(loss.kind) || !reconnect()) {
                break;
            }
        }
    } else if (!stop.load()) {
        CIRCLE_VR_LOG("xr: presenter inert; the game runs flat");
        status::flat("VR could not start (see the reason before this in the log)");
    }
    releaseMenuInput(*this, "VR stopped");
    consumerAlive.store(false);
    refresh.logSummary(" at session end");
    if (instance) {
        destroyXrObjects();
    }
    CIRCLE_VR_LOG("xr: worker finished (%llu frame(s), %llu new image(s))",
            static_cast<unsigned long long>(xrFrames), static_cast<unsigned long long>(xrCopies));
    if (frameLog) {
        std::fclose(frameLog);
        frameLog = nullptr;
    }
    SetEvent(workerDone);
}

} // namespace circlevr::vkcore
