#pragma once
#include "mounted_gun_policy.hpp"
#include "experimental.hpp"
namespace circle::motor::turret {
void install();
void manualInput(bool manual);
void stickInput(float x,float y);
bool ready(uint64_t owner);
struct View {Pose camera{},mount{};Quat reference{};uint64_t epoch{};};
bool view(const experimental::EyeRig&,const std::array<float,12>& nativeCamera,bool recenter,View&);
}
