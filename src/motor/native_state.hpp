#pragma once
#include "motor/hybrid.hpp"
#include <cstdint>

namespace circle::motor {
// ReadProcessMemory only. Independent of the disabled donor engine resolver/hooks.
class NativeStateObserver {
public:
    Observation sample();
    const char* validation() const { return reason_; }
private:
    bool attempted_ = false, validated_ = false;
    std::uintptr_t base_ = 0;
    const char* reason_ = "not sampled";
    void validate();
};
}
