#pragma once
#include "stereo_seq/eye_tags.hpp"
#include <array>
#include <cstdint>
namespace circlevr::vkcore {
// Lightweight interface for native render hooks; Vulkan copying stays in the
// presenter. These calls do nothing without the diagnostic environment flag.
bool motionInputCaptureRequested();
void noteMotionImage(const stereo_seq::RenderTag& tag, std::uint64_t image,
                     std::uint32_t width,std::uint32_t height,
                     const std::array<float,2>& scale,const std::array<float,2>& jitter,
                     const std::array<float,16>& clipToPrevious);
}
