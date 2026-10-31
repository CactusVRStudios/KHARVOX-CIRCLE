#pragma once
#include <cstdint>
namespace circle::motor::scatter {
void installEarly();
void retire(std::uintptr_t device);
// True only after this exact native pass received its own resources/state.
bool filtered(std::uintptr_t state,std::uint32_t counter);
}
