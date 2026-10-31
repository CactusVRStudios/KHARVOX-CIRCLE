#pragma once

// Per-eye motion-vector capture for the TAA smear work (KHARVOX_CIRCLE_CAPTURE_MOTION=<dir>[,<every N pairs>],
// ui_layer/motion_target.hpp). With the variable set, images that could be the game's velocity targets get
// TRANSFER_SRC when the game creates them and their layouts are followed (ui_vulkan.hpp); the exposure hook
// notes the velocity target each eye's render bound (exposure_hooks.hpp).
// Motor records each eye at its OWN present: its other native velocity image
// may be cleared before R completes. The donor route retains its R-time copy.
// Once the shared timeline shows both copies done, a background thread writes
// <dir>\mv-<pid>-p<n>-L.raw and -R.raw (the pixels as the image holds them, row after row) and a .txt file
// with the format, size, ticks and statistics of each eye. An in-headset capture writes <base>-MV-L.raw and
// so on next to its images.
//
// Needs the exposure hook (any Route S run) and the UI layer (the Vulkan hooks). Reads game memory only.
// All calls except the writer thread run on the present hook under the presenter's mutex.

#include "stereo_seq/eye_tags.hpp"
#include "stereo_seq/seq_settings.hpp"
#include "ui_layer/gui_target.hpp"
#include "vkcore/dispatch.hpp"
#include "vkcore/motion_probe.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace circlevr::vkcore {

// The velocity target an eye's last render bound, read from the post-process context. The pointers are the
// engine's (a render target or an image; resolved when copied).
struct MotionTarget {
    std::uintptr_t current = 0;  // post-process + 0x60: what this render's TAA and DLSS read
    std::uintptr_t previous = 0; // + 0x68: the other one of the pair
    std::uint64_t tick = 0;
    std::uint32_t backendFrame = 0;
    std::uint32_t eyeSeq = 0;
    // `current` resolved while the render was recorded (an image set's member in use changes per frame):
    bool resolved = false;
    bool viaTarget = false;          // `current` is a render target, `fields` its image's
    ui_layer::GuiImageFields fields; // the image's (vkImage: the set when it is one)
    int setIndex = -1;               // the set's member in use (-1: a single image)
    std::uint64_t vkImage = 0;       // the VkImage the render used
    bool nativeVulkan = false; // resolved Motor Streamline input, not a donor idImage
    std::array<float,2> scale{}, jitter{};
    std::array<float,16> clipToPrevious{};
};
// The exposure hook (exposure_hooks.cpp, render-view job, under Route S) for each tagged render: notes
// `tag`'s eye's velocity target from the post-process context. Nothing unless KHARVOX_CIRCLE_CAPTURE_MOTION is
// set.
void noteMotionTarget(const stereo_seq::RenderTag& tag, const std::byte* postProcessContext);
// Eye 0 (left) or 1 (right); nullopt before that eye's first noted render.
std::optional<MotionTarget> motionTargetFor(int eye);

class MotionCapture {
public:
    void configure(const stereo_seq::CaptureSetting& setting);
    bool enabled() const { return every_ != 0; }

    // The next pair also takes a capture, written as <base>-MV-*.
    void armOnce(std::wstring base) { onceBase_ = std::move(base); }

    // Motor's expected tag selects one eye's capture at its own boundary.
    // Without expected, retain the donor's paired capture at eye R.
    bool record(DeviceData& dev, VkCommandBuffer cb, std::uint32_t family, bool eyeR,
                const stereo_seq::RenderTag* expected = nullptr);
    // A copy command buffer was submitted (timeline value; nothing unless record took this one) or not.
    void submitted(std::uint64_t timelineValue);
    void cancel();
    // Hands a completed capture to the writer thread.
    void poll(DeviceData& dev, std::uint64_t completedTimeline);
    void destroy(DeviceData& dev);

private:
    enum class State { Idle, LeftRecorded, LeftSubmitted, Recorded, Submitted };
    struct Eye {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0; // of the buffer
        bool coherent = true;
        // This capture's image:
        bool taken = false;
        std::int32_t format = 0;
        VkExtent2D extent{};
        std::string note; // what was read, or why this eye was not copied
    };
    bool ensureBuffer(DeviceData& dev, Eye& eye, VkDeviceSize size);
    void freeBuffer(DeviceData& dev, Eye& eye);
    // One eye's checks and copy; false (with the reason in eye.note) when not copied.
    bool recordEye(DeviceData& dev, VkCommandBuffer cb, std::uint32_t family, int index,
                   const MotionTarget* expected = nullptr);

    std::wstring directory_;
    std::uint32_t every_ = 0;
    std::uint64_t pairs_ = 0; // eye R copies seen
    State state_ = State::Idle;
    std::uint64_t index_ = 0;
    std::uint64_t value_ = 0;
    std::uint64_t completed_ = 0;
    stereo_seq::RenderTag leftTag_{};
    bool nativeSplit_ = false;
    std::wstring onceBase_;
    std::wstring currentBase_; // the one-shot being taken
    std::array<Eye, 2> eyes_{};
    std::shared_ptr<std::atomic<bool>> writerBusy_ = std::make_shared<std::atomic<bool>>(false);
    std::uint64_t written_ = 0;
    std::uint64_t failures_ = 0;
};

} // namespace circlevr::vkcore
