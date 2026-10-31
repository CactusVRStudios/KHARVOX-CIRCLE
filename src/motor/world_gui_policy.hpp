#pragma once
#include <cstdint>
namespace circle::motor {
constexpr bool reuseGlyphPayload(std::uint64_t savedToken,std::uint64_t token,unsigned savedRender,unsigned render,unsigned bytes){
 return bytes>0&&bytes<=0x4000*48&&bytes%48==0&&savedToken==token&&savedRender+1==render;
}
// Motor stamps dynamic world GUI surfaces once per simulation update. The
// second render advances the world frame without producing another GUI commit.
// Like CircleVR, accept only the immediately preceding stamp for eye R.
constexpr bool reuseWorldGuiStamp(bool rightEye, std::int64_t committed, std::uint64_t current) {
    return rightEye && committed >= 0 && current > 0 &&
           static_cast<std::uint64_t>(committed) == current - 1;
}
constexpr bool replayGuiBatch(bool commonHud, bool worldSurface, int renderTexture){
    return !commonHud || worldSurface || renderTexture > 0;
}
}
