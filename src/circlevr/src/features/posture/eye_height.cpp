#include "features/posture/eye_height.hpp"

namespace circlevr::posture {

float eyeHeightOffset(float anchorHeadHeight, float targetEyeHeight) {
    return targetEyeHeight - anchorHeadHeight;
}

} // namespace circlevr::posture
