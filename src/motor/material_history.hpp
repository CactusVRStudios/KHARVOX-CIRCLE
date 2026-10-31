#pragma once
#include <cstdint>
#include <utility>

namespace circle::motor {
// A render-view job selects history and binds it a few instructions later.
// Never retain a pointer for an unrelated job, backend frame, eye or device.
struct MaterialHistoryKey {
    std::uintptr_t view{}, command{}, device{};
    std::uint64_t tick{}, generation{};
    std::uint32_t backend{}, eyeSequence{}, eye{};
    bool operator==(const MaterialHistoryKey&) const = default;
    bool valid() const { return view && command && device && tick && generation && eye < 2; }
};
class MaterialHistoryBinding {
    MaterialHistoryKey key_{};
    std::uintptr_t image_{};
public:
    void clear() { key_={}; image_=0; }
    void note(const MaterialHistoryKey& key, std::uintptr_t image) {
        clear();
        if (key.valid() && image) { key_=key; image_=image; }
    }
    std::uintptr_t take(const MaterialHistoryKey& key) {
        const auto image=std::exchange(image_,0);
        return key.valid() && key==key_ ? image : 0;
    }
};
}
