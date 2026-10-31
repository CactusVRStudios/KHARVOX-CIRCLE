#pragma once
#include <string_view>

namespace circle::motor {
// Observed separate beard assets: indiana_wear_head_beard and satipo_wear_beard.
// Unknown names and combined head assets must retain native rendering.
constexpr bool suppressFacialHair(bool adapter, bool stopped, bool disabled, std::string_view name) {
    return adapter && !stopped && disabled && name.ends_with("_beard.hairmodel");
}
// Exact asset names observed on the local first-person player. Never match
// general hair/head models or a substring belonging to another asset.
constexpr bool isHandHair(std::string_view name) {
    return name == "indiana_hand_left.hairmodel" || name == "indiana_hand_right.hairmodel";
}
}
