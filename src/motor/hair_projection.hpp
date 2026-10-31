#pragma once
#include <array>
#include <cmath>
#include <optional>
#include <limits>

namespace circle::motor {
// Compare four times the actual projected area with the native minimum:
// equivalent to a quarter-size cutoff, approximately twice the distance.
// This does not alter the projected bounds, LOD choice or raster dimensions.
inline std::optional<float> hairVisibilityArea(float area) {
    if(!std::isfinite(area)||area<0||area>std::numeric_limits<float>::max()/4)return {};
    return area*4;
}
// Motor 89ADEB..89AEA7 projects (width,width,1,1) to estimate strand size.
// For asymmetric optics this measures a position, including the lens centre,
// rather than an extent. With the native perspective denominator independent
// of x/y, subtracting projected (0,0,1,1) cancels P02/P03 and P12/P13.
// Return clip-space numerators; native perspective divide, pixel conversion,
// strand-density compensation, thickness setting and LOD still run unchanged.
inline std::optional<std::array<float,2>> hairWidthNumerators(
    const std::array<float,16>& p,float width) {
    for(float v:p)if(!std::isfinite(v))return {};
    if(!std::isfinite(width)||width<0 || p[0]<=0 || p[5]<=0 ||
       p[12]!=0 || p[13]!=0 || p[15]!=0 || std::fabs(p[14])<0.00001f)return {};
    const std::array<float,2> result{(p[0]+p[1])*width,(p[4]+p[5])*width};
    if(!std::isfinite(result[0])||!std::isfinite(result[1]))return {};
    return result;
}
}
