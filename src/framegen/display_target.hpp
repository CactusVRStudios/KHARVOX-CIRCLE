#pragma once
#include <cmath>
#include <cstdint>

namespace circle::fg {
// xrWaitFrame may report the compositor's throttled application cadence.
// Interpolation fills physical display slots, not those application slots.
inline std::int64_t displayTargetNs(std::int64_t runtimePeriod, double headsetHz,
                                    double measuredBaseMs) {
 const auto valid=[](double ms){return std::isfinite(ms)&&ms>=4.&&ms<=40.;};
 const double physical=std::isfinite(headsetHz)&&headsetHz>0?1000./headsetHz:0.;
 if(valid(physical))return std::llround(physical*1e6);
 const double reported=double(runtimePeriod)*1e-6;
 if(valid(measuredBaseMs)){
  if(!valid(reported))return std::llround(measuredBaseMs*1e6);
  const double ratio=reported/measuredBaseMs;
  // Without a refresh-rate extension only a known integer throttle can be
  // removed. A non-multiple can be a genuine headset refresh change.
  if(ratio>=.97&&std::abs(ratio-std::round(ratio))<=.08)
   return std::llround(measuredBaseMs*1e6);
 }
 return valid(reported)?runtimePeriod:0;
}
}
