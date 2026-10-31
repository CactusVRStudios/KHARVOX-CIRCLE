#pragma once
#include <cstdint>

namespace circle::fg {
// CPU handover is not GPU completion. Keep the producer slot and the single
// command allocator unavailable until the exact copy submission has retired.
struct GpuCopyLease {
    uint32_t slot=UINT32_MAX;
    uint64_t fence{};
    bool generated{};
    bool active() const { return slot!=UINT32_MAX; }
    bool ready(uint64_t completed) const {
        return active() && completed!=UINT64_MAX && completed>=fence;
    }
};
}
