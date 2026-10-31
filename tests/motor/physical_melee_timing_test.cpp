#include "physical_melee_timing.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
using namespace circle::motor::hands;
int main(){
 for(unsigned a=0;a<48;++a)assert(acceleratedStrike(a)==(a==5||a==17));
 assert(matchingStrikeCredit(5,0,0,100,600));
 assert(matchingStrikeCredit(17,1000,8,100,600));
 assert(!matchingStrikeCredit(17,0,0,100,600));
 assert(!matchingStrikeCredit(5,1000,0,100,600));
 assert(!matchingStrikeCredit(5,0,16,100,600));
 assert(!matchingStrikeCredit(5,0,0,600,600));
 assert(!matchingStrikeCredit(19,1000,0,100,600));
 assert(physicalStrikeTimeScale(1.f)==32.f);
 assert(physicalStrikeTimeScale(0.f)==0.f);
 assert(std::isnan(physicalStrikeTimeScale(std::numeric_limits<float>::quiet_NaN())));
 assert(physicalStrikeCooldown(1000000,1800000)==1119995);
 assert(physicalStrikeCooldown(1000000,1050000)==1050000);
 assert(physicalStrikeCooldown(1000000,900000)==900000);
 assert(physicalStrikeCooldown(0,900000)==900000);
}
