#include "motor/sequential.hpp"
// The XR presenter's settings, shutdown and public entry points (xr_presenter.hpp).

#include "vkcore/presenter_impl.hpp"

#include "features/render_size/mirror_window.hpp"

#include "vkcore/frame_pacing.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/ui_engine.hpp"

#include <cmath>
#include <cwchar>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace circlevr::vkcore {

namespace {

bool envFlag(const wchar_t* name, bool fallback) {
    std::wstring value;
    if (!readEnv(name, value) || value.empty()) {
        return fallback;
    }
    return !(value == L"0" || _wcsicmp(value.c_str(), L"false") == 0 || _wcsicmp(value.c_str(), L"off") == 0);
}

} // namespace

Settings readSettings() {
    Settings s;
    s.onlySubmitRealFrames = circle::motor::sequential::requested() && envFlag(L"CIRCLE_ONLY_SUBMIT_REAL_FRAMES", false);
    CIRCLE_VR_LOG("xr: Only Submit Real Frames %s",s.onlySubmitRealFrames?"enabled":"disabled");
    std::wstring value;
    if (readEnv(L"KHARVOX_CIRCLE_MODE", value) && _wcsicmp(value.c_str(), L"cinema") == 0) {
        s.mode = Mode::Cinema;
    }
    if (readEnv(L"KHARVOX_CIRCLE_WORLD_SCALE", value) && !value.empty()) {
        const float scale = std::wcstof(value.c_str(), nullptr);
        if (std::isfinite(scale) && scale > 0.0f) {
            s.unitsPerMetre = scale;
        }
    }
    s.headPosition = envFlag(L"KHARVOX_CIRCLE_HEAD_POSITION", true);
    s.setGameFov = envFlag(L"KHARVOX_CIRCLE_SET_FOV", true);
    s.checkFov = envFlag(L"KHARVOX_CIRCLE_FOV_CHECK", true);
    s.keepActive = envFlag(L"KHARVOX_CIRCLE_KEEP_ACTIVE", true);
    if (readEnv(L"KHARVOX_CIRCLE_AIM", value) && _wcsicmp(value.c_str(), L"view") == 0) {
        s.headAim = false;
    }
    s.skipCinematics = envFlag(L"KHARVOX_CIRCLE_SKIP_CINEMATICS", false);
    // Paced to the headset, every game frame is shown the same time after its pose was taken, so the measured
    // lead lands each frame's pose on its display time: on by default then (frame_pacing.hpp).
    s.poseLead = envFlag(L"KHARVOX_CIRCLE_POSE_LEAD", frame_pacing::configure());
    if (readEnv(L"KHARVOX_CIRCLE_CUTSCENES", value) && _wcsicmp(value.c_str(), L"immersive") == 0) {
        s.cutsceneCinema = false;
    }
    s.cutsceneCutRebase = envFlag(L"KHARVOX_CIRCLE_CUTSCENE_CUT_REBASE", true);
    if (readEnv(L"KHARVOX_CIRCLE_GLORY_KILLS", value) && !value.empty()) {
        std::string narrow;
        for (const wchar_t c : value) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
        if (const auto view = comfort::parseGloryView(narrow)) {
            s.gloryKills = *view;
            CIRCLE_VR_LOG("glory: KHARVOX_CIRCLE_GLORY_KILLS=%s", comfort::gloryViewName(*view));
        } else {
            CIRCLE_VR_LOG("glory: KHARVOX_CIRCLE_GLORY_KILLS '%ls' is not follow, steady, fade or screen; follow",
                    value.c_str());
        }
    }
    if (readEnv(L"KHARVOX_CIRCLE_TEST_GLORY", value) && !value.empty()) {
        double start = -1.0, duration = 0.0;
        if (swscanf_s(value.c_str(), L"%lf,%lf", &start, &duration) == 2 && std::isfinite(start) &&
            std::isfinite(duration) && start >= 0.0 && duration > 0.0) {
            s.testGloryStart = start;
            s.testGloryDuration = duration;
        }
    }
    if (readEnv(L"KHARVOX_CIRCLE_CINEMA_ASPECT", value) && !value.empty()) {
        if (const auto aspect = render_size::parseAspect(value)) {
            s.cinemaAspect = *aspect;
        } else {
            CIRCLE_VR_LOG("cinema: KHARVOX_CIRCLE_CINEMA_ASPECT '%ls' is not 16:9, 16:10, W:H or full; 16:9",
                    value.c_str());
        }
    }
    s.stereo = readStereoSettings();
    std::vector<std::string> uiWarnings;
    s.ui = ui_layer::readUiSettings(
        [](std::wstring_view name) -> std::optional<std::wstring> {
            std::wstring text;
            return readEnv(std::wstring(name).c_str(), text) ? std::optional<std::wstring>(text)
                                                             : std::nullopt;
        },
        uiWarnings);
    for (const std::string& w : uiWarnings) {
        CIRCLE_VR_LOG("ui: %s", w.c_str());
    }
    if (readEnv(L"KHARVOX_CIRCLE_MIRROR", value) && !value.empty()) {
        if (const auto mirror = stereo_seq::parseMirror(value)) {
            s.mirror = *mirror;
        } else {
            CIRCLE_VR_LOG("mirror: KHARVOX_CIRCLE_MIRROR is not left, right or off; left");
        }
    }
    if (readEnv(L"KHARVOX_CIRCLE_TEST_XR_LOSS", value) && !value.empty()) {
        const float seconds = std::wcstof(value.c_str(), nullptr);
        if (std::isfinite(seconds) && seconds > 0.0f) {
            s.testLossSeconds = seconds;
        }
    }
    s.testLossRemovesDevice = readEnv(L"KHARVOX_CIRCLE_TEST_XR_LOSS_REMOVE", value) && value == L"1";
    if (readEnv(L"KHARVOX_CIRCLE_TEST_HEAD_SWAY", value) && !value.empty()) {
        float yaw = 0.0f, pitch = 0.0f, period = 0.0f, base = 0.0f;
        if (swscanf_s(value.c_str(), L"%f,%f,%f,%f", &yaw, &pitch, &period, &base) >= 3 &&
            std::isfinite(yaw) && std::isfinite(pitch) && std::isfinite(period) && std::isfinite(base) &&
            period > 0.1f) {
            s.swayYaw = yaw;
            s.swayPitch = pitch;
            s.swayPeriod = period;
            s.swayBaseYaw = base;
        }
    }
    s.mode = Mode::Cinema; // Motor: mono transport checkpoint, never claim stereo.
    s.stereo.enabled = false; s.headPosition = false; s.setGameFov = false; s.keepActive = false; s.ui.enabled = s.ui.enabled && circle::motor::sequential::requested();
    return s;
}

LONGLONG qpcNow() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double qpcSeconds(LONGLONG delta) {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return static_cast<double>(delta) / static_cast<double>(frequency);
}

std::wstring moduleDirectory() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&moduleDirectory), &self);
    std::vector<wchar_t> path(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(self, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) {
            return {};
        }
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    std::wstring full(path.begin(), path.end());
    const auto slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : full.substr(0, slash);
}

void XrPresenter::Impl::shutdown() {
    if (processTerminating()) {
        return; // process exit: other threads are gone and the loader lock is held; touch nothing
    }
    setViewHookSink(nullptr); // no camera hook callback runs past this point
    setStereoHookSink(nullptr);
    controllers::restoreClimbCvars("the layer shuts down"); // the game's own wall climbing again
    controllers::noteImmersiveCutscene(false);              // no cutscene keeps the arms hidden
    ui_engine::setSkipComposite(false);                     // the game composites its own GUI again
    requestTwoViews(false);                                 // the game goes back to its own single view
    controllers::restoreWheelSlowdown("the layer shuts down");
    if (cutsceneTrack.skipHolding) {
        injectKey(kSkipKey, false, gameWindow());
        cutsceneTrack.skipHolding = false;
    }
    disableKeepActive();
    stop.store(true);
    if (worker.joinable()) {
        // The worker leaves xrWaitFrame within a display period; a runtime that hangs is left behind
        // rather than holding up the game. It holds its own reference to this object and never touches
        // the game's device once `stop` is set.
        if (WaitForSingleObject(workerDone, 3000) == WAIT_OBJECT_0) {
            worker.join();
        } else {
            CIRCLE_VR_LOG("presenter: XR worker did not stop within 3 s; leaving it");
            worker.detach();
            workerLeftBehind = true;
        }
    }
    // Again: a worker still starting up may have registered them after the first clear.
    setViewHookSink(nullptr);
    setStereoHookSink(nullptr);
    std::lock_guard lock(mutex);
    ringReady.store(false);
    if(fgTransport)fgTransport->stop();
    dev.vk.DeviceWaitIdle(dev.device);
    if(!workerLeftBehind&&(!copyFence||copyFence->GetCompletedValue()>=copyFenceValue)){
        if(fgTransport)fgTransport->destroyIdle();
        fgGenerator.destroyIdle();
    }else if(fgTransport){
        // Do not free resources still owned by an abandoned GPU consumer.
        // Retain them until process teardown, as with the abandoned worker.
        new std::shared_ptr<circle::fg::Transport>(fgTransport);
        new circle::fg::Generator(std::move(fgGenerator));
    }
    destroyVulkanObjects();
    if (workerLeftBehind) {
        return; // the worker's D3D12 and OpenXR objects go when it finishes
    }
    if (copyEvent) {
        CloseHandle(copyEvent);
        copyEvent = nullptr;
    }
    if (workerDone) {
        CloseHandle(workerDone);
        workerDone = nullptr;
    }
    CIRCLE_VR_LOG("presenter: shut down (%llu copied, %llu dropped)", static_cast<unsigned long long>(framesCopied),
            static_cast<unsigned long long>(framesDropped));
}

XrPresenter::XrPresenter(DeviceData& device) : impl_(std::make_shared<Impl>(device)) {}

XrPresenter::~XrPresenter() {
    if (impl_) {
        impl_->shutdown();
    }
}

void XrPresenter::onSwapchainCreated(VkSwapchainKHR swapchain, const VkSwapchainCreateInfoKHR& info) {
    impl_->onSwapchainCreated(swapchain, info);
}

void XrPresenter::onSwapchainDestroyed(VkSwapchainKHR swapchain) {
    impl_->onSwapchainDestroyed(swapchain);
}

VkResult XrPresenter::present(VkQueue queue, std::uint32_t queueFamily, const VkPresentInfoKHR* presentInfo) {
    return impl_->present(queue, queueFamily, presentInfo);
}

void XrPresenter::shutdown() {
    if (impl_) {
        impl_->shutdown();
        if (!processTerminating()) {
            impl_.reset(); // a worker left behind keeps its own reference
        }
    }
}

} // namespace circlevr::vkcore
