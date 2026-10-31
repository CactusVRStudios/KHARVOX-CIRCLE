#pragma once
#include <cstdint>

namespace circle::motor::view_audit {
// CPU observations at existing validated boundaries, never GPU readbacks.
enum class Stage : unsigned { Latch, MaterialBind, HairSetup };
void sample(Stage stage, unsigned eye, std::uint32_t frame, std::uint32_t eyeSequence,
            std::uint64_t token, std::uintptr_t view, std::uintptr_t model = 0);
// Called by the existing once-per-second control poll. A request starts one
// bounded capture; renderer callbacks only copy into memory, never write files.
void poll();
}
