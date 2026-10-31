#include "motor/hybrid.hpp"
namespace circle::motor {
QuadSource quadSource(RenderMode mode, bool hasImage, std::uint64_t age, bool cardReady) {
    if (hasImage && (mode != RenderMode::LoadingQuad || age <= 2000)) return QuadSource::GameImage;
    return cardReady ? QuadSource::LoadingCard : QuadSource::None;
}
const char* StereoReadiness::blocker() const {
    if (!camera) return "Motor live gameplay camera and writable view layout are not validated";
    if (!renderPair) return "Motor same-simulation render re-entry ABI is not validated";
    if (!rayTracing) return "per-eye ray origins and RT view dependencies are not validated";
    if (!temporalHistories) return "per-eye temporal/denoiser histories are not isolated";
    if (!tracking) return "predicted headset pose is not valid";
    if (!eyeImages) return "no completed native left/right image pair";
    return nullptr;
}
GameState classify(const Observation& o) {
    if (!o.nativeStateValid) return o.bootValidated && o.bootVideo ? GameState::BootIntro : GameState::Unknown;
    if (o.nativeState == 0) return GameState::MainMenu;
    if (o.nativeState == 1) return GameState::Loading;
    if (o.nativeState != 2) return GameState::Unknown;
    if (o.pauseValidated && o.paused) return GameState::PauseMenu;
    if (o.menuValidated && o.menuActive) return o.mainMenu?GameState::MainMenu:GameState::Menu;
    if (o.cameraValidated && o.cinematicCamera) return GameState::Cutscene;
    if (o.controlValidated && o.playerControl && o.cameraValidated && o.gameplayCamera)
        return GameState::Gameplay;
    return GameState::Unknown;
}
const char* name(GameState s) {
    switch(s) {
    case GameState::BootIntro: return "BOOT_INTRO";
    case GameState::MainMenu: return "MAIN_MENU";
    case GameState::Loading: return "LOADING";
    case GameState::Gameplay: return "GAMEPLAY";
    case GameState::Cutscene: return "CUTSCENE";
    case GameState::Menu: return "MENU";
    case GameState::PauseMenu: return "PAUSE_MENU";
    default: return "UNKNOWN";
    }
}
const char* name(RenderMode m) {
    switch(m) {
    case RenderMode::GameplayStereo: return "GAMEPLAY_STEREO";
    case RenderMode::MenuQuad: return "MENU_QUAD";
    case RenderMode::CinemaQuad: return "CINEMA_QUAD";
    case RenderMode::LoadingQuad: return "LOADING_QUAD";
    default: return "SAFE_MONO_FALLBACK";
    }
}
void HybridController::forceCinema() { manual_ = Manual::Cinema; }
void HybridController::requestStereo() { manual_ = Manual::StereoRequest; }
void HybridController::toggleAutomatic() { automatic = !automatic; manual_ = Manual::None; }
void HybridController::failStereo(const char* reason) { failed_ = true; failure_ = reason; }
const char* HybridController::stereoBlocker(const StereoReadiness& r) const {
    if (failed_) return failure_;
    if (state != GameState::Gameplay) return "player control and gameplay camera ownership are unresolved";
    return r.blocker();
}
bool HybridController::update(const Observation& o, const StereoReadiness& r) {
    const auto previousState = state;
    const auto previousMode = mode;
    state = classify(o); // observation continues even with automatic switching disabled
    mode = RenderMode::CinemaQuad;
    if (state == GameState::Loading) mode = RenderMode::LoadingQuad;
    else if (state==GameState::MainMenu||state==GameState::PauseMenu||state==GameState::Menu) mode=RenderMode::MenuQuad;
    else if (failed_) mode = RenderMode::SafeMonoFallback;
    else if (manual_ != Manual::Cinema && (automatic || manual_ == Manual::StereoRequest) &&
             state == GameState::Gameplay && !r.blocker()) mode = RenderMode::GameplayStereo;
    return state != previousState || mode != previousMode;
}
}
