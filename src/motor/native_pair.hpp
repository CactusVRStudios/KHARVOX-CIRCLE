#pragma once
#include <cstdint>

namespace circle::motor::experimental {
// File transport of two completed, unmodified native-renderer RGBA images.
// See tools/pack-native-pair.py; all fields are little endian, followed by SBS rows.
struct NativePairHeader {
    char magic[8];
    std::uint32_t width,height,paused;
    float fovX,fovY;
    float positions[2][3];
    float orientations[2][4];
};
static_assert(sizeof(NativePairHeader)==84);
}
