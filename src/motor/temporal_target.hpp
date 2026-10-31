#pragma once
#include <array>
#include <optional>
namespace circle::motor {
// Motor render-target header: allocated width/height, then active width/height.
// DBC9B0 initializes both pairs; letterboxing later changes only the active
// pair. Matching the backing images alone misses that change (r36 regression).
using TemporalTargetSize=std::array<int,4>;
constexpr std::optional<std::array<int,2>> temporalActiveSize(
    const TemporalTargetSize& native,const TemporalTargetSize& own){
    if(native[0]<=0||native[1]<=0||native[0]!=own[0]||native[1]!=own[1]||
       native[2]<=0||native[3]<=0||native[2]>native[0]||native[3]>native[1])return {};
    return std::array<int,2>{native[2],native[3]};
}
}
