#pragma once
#include "hybrid.hpp"
#include "common/pose.hpp"
#include <cmath>

namespace circle::motor {
constexpr bool controllerGuideContext(GameState state, bool inspection) {
    return state == GameState::MainMenu || state == GameState::PauseMenu ||
        (state == GameState::Menu && !inspection);
}

inline circlevr::Pose controllerGuidePose(const circlevr::Pose& menu, float menuWidth,
                                         float guideWidth, bool left) {
    auto result = menu;
    constexpr float angle = 25.f * 3.14159265359f / 180.f;
    const float side = left ? -1.f : 1.f;
    // Hinge about the inner edge. Outer edges approach the player (+local Z),
    // while the gap remains outside the menu even after recentering/tilting.
    const float offset = menuWidth * .5f + .05f + guideWidth * .5f * std::cos(angle);
    result.position = menu.position + rotate(menu.orientation,
        circlevr::Vec3{side * offset, 0, guideWidth * .5f * std::sin(angle)});
    result.orientation = menu.orientation * circlevr::Quat::fromAxisAngle({0,1,0}, -side * angle);
    return result;
}
}
