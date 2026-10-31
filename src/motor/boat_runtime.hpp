#pragma once
#include "mounted_gun_runtime.hpp"
namespace circle::motor::boat {
void install();
bool view(const experimental::EyeRig&,const std::array<float,12>&,bool,turret::View&);
}
