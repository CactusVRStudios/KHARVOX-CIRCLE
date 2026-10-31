// Reuses CircleVR's D3D12 session, static image upload, event handling and cleanup.
// Distinct test images prove the eye path only; this is not stereoscopic game rendering.
#include "vkcore/presenter_impl.hpp"
#include "vkcore/status_file.hpp"
#include <algorithm>
#include <vector>

namespace circlevr::vkcore {
bool XrPresenter::Impl::createDiagnosticPattern() {
    constexpr unsigned side = 512;
    std::vector<std::uint8_t> pixels(side * 2 * side * 4);
    const char* glyphs[2][7] = {
        {"10000","10000","10000","10000","10000","10000","11111"},
        {"11110","10001","10001","11110","10100","10010","10001"}
    };
    for (unsigned eye = 0; eye < 2; ++eye) {
        for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
            const bool grid = x % 64 < 2 || y % 64 < 2;
            const bool border = x < 12 || y < 12 || x >= side - 12 || y >= side - 12;
            const bool cross = (x > 250 && x < 262) || (y > 250 && y < 262);
            bool letter = false;
            if (x >= 64 && x < 144 && y >= 64 && y < 176) letter = glyphs[eye][(y-64)/16][(x-64)/16] == '1';
            const auto offset = (static_cast<std::size_t>(y) * side * 2 + eye * side + x) * 4;
            for (unsigned channel = 0; channel < 3; ++channel) {
                pixels[offset + channel] = static_cast<std::uint8_t>(letter || cross ? 255 : border ? ((eye == 0 && channel == 0) || (eye == 1 && channel == 2) ? 240 : 25) : grid ? 110 : 22);
            }
            pixels[offset + 3] = 255;
        }
    }
    const bool ok = createStaticImage(diagnosticSwapchain, pixels, side * 2, side, "stereo diagnostic L/R");
    if (ok) CIRCLE_VR_LOG("diagnostic: pattern uploaded; LEFT red L / RIGHT blue R; no game renderer involved");
    return ok;
}

void XrPresenter::Impl::frameDiagnostic() {
    XrFrameState state{XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    if (XR_FAILED(xr.xrWaitFrame(session, &wait, &state))) { stop.store(true); return; }
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(xr.xrBeginFrame(session, &begin))) { stop.store(true); return; }
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = state.predictedDisplayTime;
    locate.space = localSpace;
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};
    uint32_t viewCount = 0;
    const auto located = xr.xrLocateViews(session, &locate, &viewState, 2, &viewCount, views.data());
    std::array<XrCompositionLayerProjectionView,2> projectionViews{};
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = localSpace;
    layer.viewCount = 2;
    layer.views = projectionViews.data();
    const auto validFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    const bool visible = state.shouldRender && XR_SUCCEEDED(located) && viewCount == 2 && (viewState.viewStateFlags & validFlags) == validFlags;
    if (visible) {
        for (uint32_t eye = 0; eye < 2; ++eye) {
            projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projectionViews[eye].pose = views[eye].pose;
            projectionViews[eye].fov = views[eye].fov;
            projectionViews[eye].subImage.swapchain = diagnosticSwapchain;
            projectionViews[eye].subImage.imageRect = {{static_cast<int32_t>(eye * 512),0},{512,512}};
        }
        const auto now = GetTickCount64();
        if (now - diagnosticLastPoseLog >= 2000) {
            diagnosticLastPoseLog = now;
            const auto& pose = views[0].pose;
            CIRCLE_VR_LOG("diagnostic: valid predicted left-eye pose xyz %.4f %.4f %.4f qxyzw %.4f %.4f %.4f %.4f time %lld", pose.position.x,pose.position.y,pose.position.z,pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w,static_cast<long long>(state.predictedDisplayTime));
        }
    }
    const XrCompositionLayerBaseHeader* layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = state.predictedDisplayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = visible ? 1U : 0U;
    end.layers = visible ? layers : nullptr;
    const auto result = xr.xrEndFrame(session, &end);
    ++xrFrames;
    if (visible && XR_SUCCEEDED(result)) {
        ++xrProjectionFrames;
        if (xrProjectionFrames == 1) status::vr("stereo test pattern submitted; human headset verification is separate");
        if (xrProjectionFrames == 1 || xrProjectionFrames % 900 == 0) CIRCLE_VR_LOG("diagnostic: stereo pattern submitted successfully count %llu xrEndFrame %d",static_cast<unsigned long long>(xrProjectionFrames),static_cast<int>(result));
    } else if (XR_FAILED(result)) { CIRCLE_VR_LOG("diagnostic: xrEndFrame failed %d",static_cast<int>(result)); stop.store(true); }
}
}

extern "C" __declspec(dllexport) int WINAPI CircleRunXrDiagnostic(unsigned seconds) {
    using namespace circlevr::vkcore;
    logOpen();
    DeviceData noGameDevice;
    auto impl = std::make_shared<XrPresenter::Impl>(noGameDevice);
    impl->standaloneDiagnostic = true;
    impl->requestedExtent = {1024,512};
    impl->requestedFormat = VK_FORMAT_R8G8B8A8_UNORM;
    impl->diagnosticDeadline = GetTickCount64() + std::clamp(seconds, 5U, 300U) * 1000ULL;
    impl->workerDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    impl->runWorker(); // donor startup, event and frame loop, with static images instead of game copies
    const bool submitted = impl->xrProjectionFrames > 0;
    if (impl->copyEvent) { CloseHandle(impl->copyEvent); impl->copyEvent = nullptr; }
    if (impl->workerDone) { CloseHandle(impl->workerDone); impl->workerDone = nullptr; }
    return submitted ? 0 : 2;
}
