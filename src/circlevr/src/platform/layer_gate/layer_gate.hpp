#pragma once

// Whether the Vulkan layer should activate in the current process (T-079, T-109). Portable so the
// rules can be tested without Windows; the layer passes in the exe path and the two variables.

#include <optional>
#include <string_view>

namespace circlevr::layer_gate {

inline constexpr std::wstring_view kTargetExe = L"TheGreatCircle.exe";

enum class Decision {
    Enable,
    WrongProcess, // not TheGreatCircle.exe
    NotEnabled,   // KHARVOX_CIRCLE_ENABLE_LAYER is not "1"
    Disabled,     // KHARVOX_CIRCLE_DISABLE_LAYER is set to a non-empty value
};

// Base name of a Windows or POSIX path: everything after the last '\' or '/'.
std::wstring_view baseName(std::wstring_view path);

// True when the exe path names TheGreatCircle.exe, compared case-insensitively (ASCII).
bool isTargetExecutable(std::wstring_view exePath);

// A missing variable is std::nullopt. Any non-empty disable value wins, as the loader treats
// disable_environment; enable must be exactly "1".
Decision decide(std::wstring_view exePath,
                std::optional<std::wstring_view> enableValue,
                std::optional<std::wstring_view> disableValue);

const char* toString(Decision decision);

} // namespace circlevr::layer_gate
