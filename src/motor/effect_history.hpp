#pragma once
#include "stereo_seq/eye_tags.hpp"
#include <optional>

namespace circle::motor {
// Use the pass's captured backend counter, not the advancing global counter.
// Until Motor's scattering volumes AND filter state are isolated, mixing the
// last render mixes the opposite eye. Keep the current volume with weight 1.
inline std::optional<unsigned> scatterStereoEye(bool installed, bool stopped,
    bool primaryView, unsigned counter,
    const std::optional<circlevr::stereo_seq::RenderTag>& tag) {
    if (!installed || stopped || !primaryView || !tag || !tag->tick ||
        !circlevr::stereo_seq::drawsEyeView(*tag) || tag->backendFrame != counter + 1u)
        return {};
    return unsigned(circlevr::stereo_seq::eyeIndex(tag->eye));
}
}
