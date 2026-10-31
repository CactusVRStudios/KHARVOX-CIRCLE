#pragma once
#include <array>
#include <cmath>
#include <optional>
namespace circle::motor {
// Every film fills the eye texture; the XR quad supplies the 16:9 physical
// aspect. Its authored horizontal FOV determines the matching vertical FOV,
// independently of the portrait headset render-target aspect.
inline float cinemaVerticalTangent(float tx){return tx*9.f/16.f;}
// A stereoscopic film uses a fixed horizontal baseline in its authored camera
// frame. Head yaw/roll/translation must not turn it into a tracked portal.
inline std::optional<std::array<float,2>> cinemaEyeOffsets(
    const std::array<std::array<float,3>,2>& positions,bool mono) {
    if(mono)return std::array<float,2>{0,0};
    float squared=0;
    for(unsigned k=0;k<3;++k){
        if(!std::isfinite(positions[0][k])||!std::isfinite(positions[1][k]))return {};
        const float d=positions[1][k]-positions[0][k];squared+=d*d;
    }
    const float ipd=std::sqrt(squared);
    if(!std::isfinite(ipd)||ipd<.04f||ipd>.09f)return {};
    return std::array<float,2>{-.5f*ipd,.5f*ipd};
}
// Native-render equivalent of ARGENT's CinematicProjection clip shift.
// Motor renders the authored 16:9 band directly: unlike ARGENT's shader
// transform no full-surface crop factor is needed in this frustum.
inline std::optional<std::array<float,4>> cinemaFrustum(float tx,float ty,float width,float x,float y){
    if(!std::isfinite(tx+ty+width+x+y)||tx<=0||ty<=0||width<=0)return {};
    const float convergence=width/(2*tx);
    return std::array<float,4>{std::atan(-tx-x/convergence),std::atan(tx-x/convergence),
        std::atan(ty-y/convergence),std::atan(-ty-y/convergence)};
}
// Render a symmetric enclosing frustum for native shadow/light consumers.
// Crop at presentation, restoring the exact off-axis rays and convergence.
struct StableCinemaProjection {
 std::array<float,4> fov{};
 std::array<float,2> crop{0,1}; // normalized horizontal origin and width
};
inline std::optional<StableCinemaProjection> stableCinemaProjection(float tx,float ty,float width,float x){
 if(!cinemaFrustum(tx,ty,width,x,0))return {};
 const float shift=x/(width/(2*tx)),extent=tx+std::abs(shift);
 if(!std::isfinite(extent)||extent<=0)return {};
 StableCinemaProjection out;
 out.fov={-std::atan(extent),std::atan(extent),std::atan(ty),-std::atan(ty)};
 const float fraction=tx/extent;
 out.crop={x>=0?0.f:1.f-fraction,fraction};
 return out;
}

}
