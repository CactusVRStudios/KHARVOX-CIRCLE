#pragma once
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <cstdint>
namespace circle::motor {
enum class HudAnchor { Head, CinemaImage, MenuScreen };
// A native pause state can arrive before the corresponding rendered image.
// Never borrow the zero-initialized cinema quad from a projection-only frame.
constexpr HudAnchor hudAnchor(bool liveProjection, bool nativeMenu, bool cinemaAnchorValid){
    if(liveProjection&&!nativeMenu)return HudAnchor::Head;
    return cinemaAnchorValid?HudAnchor::CinemaImage:HudAnchor::MenuScreen;
}
// Names from Motor's binocular SWF and its generated materials. Match only
// this screen, never a world prop or an arbitrary material containing 'scope'.
constexpr bool binocularHudMaterial(std::string_view name){
    return name.starts_with("hud_binoculars_fla.material_") ||
        name=="swf/hud/hud_binoculars.swf" ||
        name.starts_with("textures/guis/hud/binoculars/hud_binoculars_background.png$");
}
constexpr bool freshBinocularHud(uint64_t seen,uint64_t now,bool projection,bool menu){
    return projection&&!menu&&seen&&now>=seen&&now-seen<250;
}
// Horizontal HUD follow: stationary within ten degrees; smooth only the
// excess angle, so there is no jump or oscillation at the deadzone boundary.
inline float followHudYaw(float current, float target, double dt){
    constexpr float pi=3.14159265358979323846f, deadzone=pi/18.f;
    if(!std::isfinite(current)||!std::isfinite(target)||!std::isfinite(dt)||dt<=0)return current;
    const float error=std::remainder(target-current,2*pi);
    const float excess=std::fabs(error)-deadzone;
    if(excess<=0)return current;
    const float alpha=static_cast<float>(1.0-std::exp(-dt/0.18));
    return std::remainder(current+alpha*std::copysign(excess,error),2*pi);
}
// Head-relative 16:9 HUD, two metres away. Screen GUI is authored once in
// normalized screen coordinates, not in the asymmetric optical coordinates.
inline std::optional<std::array<float,4>> hudViewport(const std::array<float,4>& fov,float eyeX){
    for(float a:fov)if(!std::isfinite(a)||std::fabs(a)>=1.55f)return {};
    if(!std::isfinite(eyeX)||std::fabs(eyeX)>.1f)return {};
    const float l=std::tan(fov[0]),r=std::tan(fov[1]),u=std::tan(fov[2]),d=std::tan(fov[3]);
    if(r-l<.1f||u-d<.1f)return {};
    constexpr float tx=.65f,ty=tx*9.f/16.f,distance=2.f;
    return std::array<float,4>{(-tx-eyeX/distance-l)/(r-l),(u-ty)/(u-d),2*tx/(r-l),2*ty/(u-d)};
}
}
