#pragma once
#include <cstdint>
namespace circle::fg {
void displayPulse(int64_t periodNs,bool visible);
void pacingReady(bool ready);
void pacingMultiplier(unsigned multiplier);
void pacingPair(bool missedAdjacentPair);
// Called only once per published real pair, outside presenter/queue locks.
// Returns true when FG owns pacing (do not additionally apply old 1x pacing).
bool paceProducer();
double pacingIntervalMs();
void autoGpuSample(unsigned eye,double milliseconds,bool complete=true);
unsigned autoMultiplier(unsigned maximum,double fgMs,unsigned generated);
void autoOutput(uint64_t serial,bool accepted,int64_t displayTimeNs=0);
double producerHoldMs();
bool fractionalPacingActive();
double fractionalFactor();
double displayPeriodMs();
unsigned fractionalSample(unsigned maximum,double fgMs,unsigned generated,double sourceInterval,bool continuous,double sourceWaitMs=0);
bool preserveRealPairs();
struct ProducerStamp {double timeMs{},waitTotalMs{};};
ProducerStamp pairPublished(uint64_t value);
void pairRetired(uint64_t value);
}
