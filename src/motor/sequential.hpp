#pragma once
#include <string>
#include "motor/experimental.hpp"
#include "stereo_seq/eye_tags.hpp"
namespace circle::motor::sequential {
bool requested();
// Current, identity-validated first-person body origin; never changes physics.
bool playerCameraOrigin(uint64_t owner,const std::array<float,3>& camera,std::array<float,3>& origin);
void installSecondaryCacheResources();
bool monoVrEnabled();
// Publish the validated centre view before its single native backend submission.
bool completeMonoFrame(void* info,std::uint64_t token,std::uint32_t renderFrame);
// Independently fingerprinted Motor boundaries; does not unlock DOOM's engine gates.
bool installObservation(std::string& error);
void observePresent(const circlevr::stereo_seq::PresentMatch& match);
bool pairEyes(std::uint64_t token,std::uint32_t rightRender,std::array<experimental::LiveEye,2>& eyes);
bool projectionAllowed();
// Shared render-only camera for measured ledge and whip climbing states.
bool ledgeCamera(std::array<float,12>& camera,bool nativeReady=false,bool assessHandoff=false);
bool menuActive();
bool binocularHudActive();
bool nativeMainMenuActive();
bool allowsRenderSize();
}

namespace circle::motor::sequential {
// Validated Motor-only final GUI composite sites; no donor address enablement.
bool installGuiCompositeHooks(void (*raster)(std::uintptr_t&), void (*compute)(std::uintptr_t&));
}
