#pragma once
#include <cstdint>
#include <vector>
#include <span>
namespace circle::motor::secondary_cache {
bool requested();
bool ready();
void shaderReady();
void shaderFailed();
void markHistory(std::uint32_t backend);
bool usesHistory(std::uint32_t backend);
// Exact known modules only. The original module is retained for non-stereo work.
std::vector<std::uint32_t> patch(std::span<const std::uint32_t> words);
}
