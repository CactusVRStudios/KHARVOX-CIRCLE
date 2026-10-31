#pragma once
#include "experimental.hpp"
#include <algorithm>
#include <cmath>

namespace circle::motor {
// Symmetric overscan: retain both physical eye origins and orientations.
// Submit the same enlarged FOV to OpenXR that native projection builders use;
// changing just the render projection would distort the displayed world.
inline bool symmetricStereoOptics(std::array<experimental::LiveEye,2>& eyes) {
    float horizontal=0,vertical=0;
    for(const auto& eye:eyes){
        if(!eye.valid||eye.cinema)return false;
        for(const auto& f:eye.rig.fovs){
            for(float a:f)if(!std::isfinite(a)||std::abs(a)>=1.57f)return false;
            if(f[0]>=0||f[1]<=0||f[2]<=0||f[3]>=0)return false;
            horizontal=std::max({horizontal,-f[0],f[1]});
            vertical=std::max({vertical,f[2],-f[3]});
        }
    }
    const std::array<float,4> f{-horizontal,horizontal,vertical,-vertical};
    for(auto& eye:eyes)eye.rig.fovs={f,f};
    return true;
}
}
