#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace circle::motor::hands {
// Only the measured ordinary and charged strike actions. Charge, guard,
// counter, finisher and synchronized story actions keep their original clocks.
constexpr bool acceleratedStrike(unsigned action) { return action==5||action==17; }
constexpr bool matchingStrikeCredit(unsigned action,unsigned chargeMs,unsigned stages,
                                    uint64_t now,uint64_t expires) {
 return acceleratedStrike(action)&&now<expires&&!(stages&16)&&
        ((action==17)==(chargeMs>=350));
}
inline float physicalStrikeTimeScale(float native) {
 return std::isfinite(native)&&native>0.f&&native<=32.f?32.f:native;
}
constexpr int64_t physicalStrikeCooldown(int64_t now,int64_t nativeEnd) {
 // The physical velocity detector still requires a separate rearmed swing.
 // Never lengthen a native cooldown or modify a clock in another time domain.
 return now>0&&nativeEnd>now?std::min(nativeEnd,now+int64_t(119995)):nativeEnd;
}
}
