#include "low_performance.hpp"
#include "low_performance.generated.hpp"
#include "build_profile.hpp"
#include "vkcore/log.hpp"
#include <safetyhook.hpp>
#include <windows.h>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace circle::motor::low_performance {
namespace {
struct Bound { uintptr_t object; const Setting* setting; };
// Process-lifetime state, like the other Motor hooks; never unloaded under the loader lock.
auto& bound = *new std::vector<Bound>;
std::atomic<bool> enabled{false};
std::atomic<uint64_t> traceCalls{}, indirectTraceCalls{}, asBuilds{};
bool read(uintptr_t at, void* out, size_t size) {
    SIZE_T got{};
    return at && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at), out, size, &got) && got == size;
}
void hold(safetyhook::Context& r) {
    if (!enabled.load(std::memory_order_acquire)) return;
    for (const auto& b : bound) if (r.rcx == b.object) {
        r.rdx = reinterpret_cast<uintptr_t>(b.setting->value);
        break;
    }
}
}
void traceRecorded(bool indirect) {
    if (enabled.load(std::memory_order_relaxed)) (indirect ? indirectTraceCalls : traceCalls).fetch_add(1, std::memory_order_relaxed);
}
void buildsRecorded(std::uint32_t count) {
    if (enabled.load(std::memory_order_relaxed)) asBuilds.fetch_add(count, std::memory_order_relaxed);
}
void reportWorkload() {
    if (!enabled.load(std::memory_order_acquire)) return;
    static std::atomic<ULONGLONG> previous{};
    auto before=previous.load(); const auto now=GetTickCount64();
    if(now-before<5000 || !previous.compare_exchange_strong(before,now)) return;
    circlevr::vkcore::logf("motor-low-performance-rt: last interval direct traces %llu indirect traces %llu AS builds %llu; CPU recording counts, compute ray queries not included",
        traceCalls.exchange(0),indirectTraceCalls.exchange(0),asBuilds.exchange(0));
}
void install() {
    char option[8]{}, mode[32]{};
    GetEnvironmentVariableA("CIRCLE_LOW_PERFORMANCE", option, sizeof(option));
    GetEnvironmentVariableA("KHARVOX_CIRCLE_MODE", mode, sizeof(mode));
    if (std::strcmp(option, "1") || std::strcmp(mode, "cinema")) return;
    static std::once_flag once;
    std::call_once(once, [] {
        const auto profile = build::current();
        if (!profile) { circlevr::vkcore::logf("motor-low-performance: refused unknown executable"); return; }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto setter = base + build::rva(kSetter);
        constexpr unsigned char signature[] = {0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xfa,0x48,0x8b,0xd9,
            0x45,0x84,0xc0,0x75,0x7f,0x48,0x8b,0x11,0x8b,0x42,0x40,0x0f,0xba,0xe0,0x0e,0x73,0x1d};
        unsigned char actual[sizeof(signature)]{}, second[6]{};
        if (!read(setter, actual, sizeof(actual)) || std::memcmp(actual, signature, sizeof(actual)) ||
            !read(setter+0x3e, second, sizeof(second)) || std::memcmp(second, "\x0f\xba\xe0\x0f\x73\x1d", 6)) {
            circlevr::vkcore::logf("motor-low-performance: setter fingerprint rejected; no changes"); return;
        }
        // Preflight EVERY CVar before making any code or setting changes.
        for (const auto& s : kSettings) {
            const auto object = base + build::rva(s.rva);
            uintptr_t internal{}, namePtr{}; char name[128]{};
            if (!read(object, &internal, 8) || !internal || !read(object+0x30, &namePtr, 8) ||
                !read(namePtr, name, std::strlen(s.name)+1) || std::strcmp(name, s.name)) {
                circlevr::vkcore::logf("motor-low-performance: %s not ready; no changes", s.name); bound.clear(); return;
            }
            bound.push_back({object, &s});
        }
        DWORD oldProtection{};
        if (!VirtualProtect(reinterpret_cast<void*>(setter+0x1f), 0x25, PAGE_EXECUTE_READWRITE, &oldProtection)) {
            circlevr::vkcore::logf("motor-low-performance: code protection failed; no changes"); bound.clear(); return;
        }
        // ECLIPSE's two branch changes. No file writes, no graphics-capability spoofing.
        *reinterpret_cast<volatile unsigned char*>(setter+0x1f) = 0xeb;
        *reinterpret_cast<volatile unsigned char*>(setter+0x42) = 0xeb;
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(setter), 0x44);
        auto hook = safetyhook::MidHook::create(reinterpret_cast<void*>(setter), hold);
        if (!hook) {
            *reinterpret_cast<volatile unsigned char*>(setter+0x1f) = 0x73;
            *reinterpret_cast<volatile unsigned char*>(setter+0x42) = 0x73;
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(setter), 0x44);
        }
        DWORD ignored{};
        VirtualProtect(reinterpret_cast<void*>(setter+0x1f), 0x25, oldProtection, &ignored);
        if (!hook) { circlevr::vkcore::logf("motor-low-performance: hook failed; unlock rolled back"); bound.clear(); return; }
        new safetyhook::MidHook(std::move(*hook));
        enabled.store(true, std::memory_order_release);
        using Set = bool (*)(void*, const char*, bool);
        const auto set = reinterpret_cast<Set>(setter);
        size_t verified = 0;
        for (const auto& b : bound) {
            set(reinterpret_cast<void*>(b.object), b.setting->value, false);
            uintptr_t internal{}, stringPtr{}; char value[96]{};
            bool ok = read(b.object, &internal, 8) && read(internal, &stringPtr, 8) &&
                read(stringPtr, value, std::strlen(b.setting->value)+1) && !std::strcmp(value, b.setting->value);
            verified += ok;
            circlevr::vkcore::logf("motor-low-performance: %s=%s %s", b.setting->name, b.setting->value, ok ? "verified" : "READBACK MISMATCH");
        }
        circlevr::vkcore::logf("motor-low-performance: enabled before Vulkan instance creation; %zu/%zu values verified; native setter holds preset; AA/TAA owned by VR. r_useRayTracing=0 disables native RT use; device support retained for initialization. Indirect lighting may be missing; compute ray queries are not audited.", verified, bound.size());
    });
}
}
