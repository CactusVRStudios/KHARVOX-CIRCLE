#pragma once
#include <array>
#include <cmath>
#include <optional>

namespace circle::motor {
// Motor's packed inverse reconstructs clip (2u-1,1-2v,z,1) as
// (uv*packed.xy+packed.zw, inverseDepthNumerator) / inverseDepthDenominator.
// The native builder assumes inverse[3] == inverse[7] == 0. Preserve its
// independent depth packing; only complete the lateral affine coefficients.
inline std::optional<std::array<float,4>> packedInverseProjectionXY(
    const std::array<float,16>& inverse) {
    for(float v:inverse)if(!std::isfinite(v))return {};
    for(unsigned i:{1u,2u,4u,6u,8u,9u,10u,12u,13u})
        if(std::abs(inverse[i])>1.e-6f)return {};
    if(inverse[0]<=0||inverse[5]<=0||std::abs(inverse[11])<1.e-6f||
       std::abs(inverse[14])<1.e-6f)return {};
    return std::array<float,4>{2*inverse[0],-2*inverse[5],
        -inverse[0]+inverse[3],inverse[5]+inverse[7]};
}
}
