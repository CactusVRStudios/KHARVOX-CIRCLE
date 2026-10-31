#pragma once

// Layer log: one file per run in KHARVOX_CIRCLE_LOG_DIR, every line flushed so a crash keeps the tail.
// CIRCLE_ENABLE_LOGGING=1 is required for every diagnostic output (default off).

#include <atomic>
#include <cstdint>
#include <string>
#include <cstdio>

namespace circlevr::vkcore {
bool loggingEnabled();
std::FILE* openLogFileShared(const wchar_t* path,const wchar_t* mode,int share);
int openLogFile(std::FILE** file,const wchar_t* path,const wchar_t* mode);

// A cap for a line that comes with what the player does (presses, clicks, trigger tests): every line is a
// synchronous write on the thread that logs, often a game thread in a hook. The first `first` occurrences
// are logged, then at most one per `intervalMs`. Any thread.
class LogCap {
public:
    constexpr explicit LogCap(std::uint64_t first, std::uint64_t intervalMs = 60'000)
        : first_(first), intervalMs_(intervalMs) {}

    // True when this occurrence is to be logged (`nowMs`: GetTickCount64); `skipped` then holds how many
    // were not since the last one logged.
    bool due(std::uint64_t nowMs, std::uint64_t& skipped) {
        skipped = 0;
        if (seen_.fetch_add(1, std::memory_order_relaxed) < first_) {
            last_.store(nowMs, std::memory_order_relaxed);
            return true;
        }
        std::uint64_t last = last_.load(std::memory_order_relaxed);
        if (nowMs - last < intervalMs_ ||
            !last_.compare_exchange_strong(last, nowMs, std::memory_order_relaxed)) {
            held_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        skipped = held_.exchange(0, std::memory_order_relaxed);
        return true;
    }

private:
    std::uint64_t first_;
    std::uint64_t intervalMs_;
    std::atomic<std::uint64_t> seen_{0};
    std::atomic<std::uint64_t> last_{0};
    std::atomic<std::uint64_t> held_{0};
};

// Opens the log file (once) and writes the loaded marker. Safe to call more than once.
void logOpen();

void logf(const char* format, ...);

// Seconds since the log started: the time in front of every log line.
double logSeconds();

// The value of KHARVOX_CIRCLE_LOG_DIR, empty when unset.
std::wstring logDirectory();

// Logs the file version of the vulkan-1.dll the game loaded (T-079).
void logLoaderVersion();

// Reads an environment variable; false when it is not set.
bool readEnv(const wchar_t* name, std::wstring& value);

} // namespace circlevr::vkcore

#define CIRCLE_VR_LOG(...) ::circlevr::vkcore::logf(__VA_ARGS__)
