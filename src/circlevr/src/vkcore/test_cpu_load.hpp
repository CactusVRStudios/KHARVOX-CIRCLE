#pragma once

// A test knob, never set by the launcher: KHARVOX_CIRCLE_TEST_CPU_LOAD_MS=<ms>[,<seconds on>,<seconds off>]
// busy-waits at every render's frame end (eye R's nested one included), so a fast processor behaves like a
// slower one: a Route S tick then takes two loads, an alternating tick one (stereo_seq/adaptive_eyes.hpp,
// parseCpuLoad). With the two periods the load goes on and off, so one run shows
// KHARVOX_CIRCLE_ALTERNATE_EYES=auto switching both ways. Parallel Eye Rendering has no frame-end wrapper: there
// the load runs once a render frame in its view dispatch, before the views are dispatched (both views in
// one frame, one load). Unset: nothing (one relaxed load per frame).

namespace circlevr::vkcore::test_cpu_load {

// The frame-end wrapper, for every render frame (Route S).
void atFrameEnd();

// Parallel Eye Rendering's view dispatch (view_slots.cpp), once a render frame.
void atViewDispatch();

} // namespace circlevr::vkcore::test_cpu_load
