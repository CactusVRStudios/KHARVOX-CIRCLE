#pragma once
#include <cstdint>
namespace circle::motor::temporal {
void installEarly();
// Native TAA, or the explicitly enabled and fully installed Streamline SR port.
bool active();
struct UpscalingStatus {const char* requested;int actual;bool stereoVerified;};
UpscalingStatus upscalingStatus();
void prepareEye(unsigned char* view, int eye, std::uint64_t token);
}
