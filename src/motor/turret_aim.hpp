#pragma once
#include "default_input.hpp"

namespace circle::motor::input {
// Observed airplane rear cannon: mode27, STATIONARY slot22, physics5.
// Do not enable general camera turning, hand IK, or melee in this state.
inline bool turretAimContext(const Native& n) {
 return n.fresh&&!n.menu&&n.mode==27&&n.slot==22&&n.type==5&&
        !n.reason&&!n.animated&&!n.local;
}
}
