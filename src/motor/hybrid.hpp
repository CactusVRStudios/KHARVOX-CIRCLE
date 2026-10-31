#pragma once
#include <cstdint>

namespace circle::motor {
enum class GameState { BootIntro, MainMenu, Loading, Gameplay, Cutscene, PauseMenu, Menu, Unknown };
enum class RenderMode { GameplayStereo, MenuQuad, CinemaQuad, LoadingQuad, SafeMonoFallback };
enum class QuadSource { None, GameImage, LoadingCard };
QuadSource quadSource(RenderMode mode, bool hasImage, std::uint64_t imageAgeMs, bool cardReady);

// Indicators are optional until a live Motor owner/accessor has been measured.
// INGAME alone is deliberately insufficient: it also covers scripted cameras.
struct Observation {
    int nativeState = -1;
    bool nativeStateValid = false;
    bool controlValidated = false, playerControl = false;
    bool cameraValidated = false, gameplayCamera = false, cinematicCamera = false;
    bool pauseValidated = false, paused = false;
    bool menuValidated=false, menuActive=false, mainMenu=false;
    bool bootValidated = false, bootVideo = false;
};
struct StereoReadiness {
    bool camera = false, renderPair = false, rayTracing = false;
    bool temporalHistories = false, tracking = false, eyeImages = false;
    const char* blocker() const;
};
GameState classify(const Observation& observation);
const char* name(GameState state);
const char* name(RenderMode mode);

class HybridController {
public:
    bool automatic = true;
    GameState state = GameState::Unknown;
    RenderMode mode = RenderMode::CinemaQuad;
    void forceCinema();
    void requestStereo();
    void toggleAutomatic();
    void failStereo(const char* reason);
    bool update(const Observation& observation, const StereoReadiness& readiness);
    const char* stereoBlocker(const StereoReadiness& readiness) const;
    bool stereoFailed() const { return failed_; }
private:
    enum class Manual { None, Cinema, StereoRequest };
    Manual manual_ = Manual::None;
    bool failed_ = false;
    const char* failure_ = "native stereo failed"; // caller supplies a static diagnostic string
};
}
