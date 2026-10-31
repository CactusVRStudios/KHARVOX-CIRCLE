#include "ui_layer/motion_target.hpp"
#include <array>
#include <cmath>
#include <cstdio>
int main() {
    using namespace circlevr::ui_layer;
    unsigned failures{};
    auto check=[&](bool ok,const char* text) {
        if(!ok) { ++failures;std::fprintf(stderr,"FAIL: %s\n",text); }
    };
    // Motor's RGBA16F image: XY velocity, ZW auxiliary values. Do not treat
    // the extra channels as another pixel or accidentally count them as motion.
    std::array<std::uint16_t,8> pixels{0x3c00,0xbc00,0x7c00,0x7c00,0,0,0x4000,0x4000};
    check(motionBytesPerPixel(97)==8,"native RGBA16F pixel stride");
    const auto stats=motionStats(reinterpret_cast<const std::uint8_t*>(pixels.data()),sizeof(pixels),2,1,97);
    check(stats&&stats->pixels==2&&stats->nonZero==1&&stats->nonFinite==0,"only XY decoded with correct stride");
    check(stats&&stats->meanAbs[0]==0.5&&stats->meanAbs[1]==0.5,"native half float velocity preserved");
    check(!motionStats(reinterpret_cast<const std::uint8_t*>(pixels.data()),8,2,1,97),"truncated capture rejected");
    ImageCreateDesc desc{vk::kImageType2D,97,1124,1208,1,1,1,1,vk::kUsageSampled|vk::kUsageColorAttachment};
    const auto usage=motionCandidateUsage(desc);
    check(usage&&(*usage&vk::kUsageTransferSrc),"native motion image prepared for diagnostic copy");
    desc.samples=4;
    check(!motionCandidateUsage(desc),"multisample image cannot use single-sample readback");
    return failures?1:0;
}
