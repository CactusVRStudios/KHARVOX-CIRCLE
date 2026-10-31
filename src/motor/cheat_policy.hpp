#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace circle::motor::cheats {
struct Options { bool invulnerable{},stealth{},stamina{},oxygen{},ammo{},damageBoost{};float multiplier{99}; };
inline bool player(uint64_t local,uint64_t candidate){return local&&local!=UINT64_MAX&&local==candidate;}
inline float damage(float amount,uint64_t local,uint64_t victim,uint64_t attacker,const Options& o){
 if(!std::isfinite(amount)||amount<=0)return amount;
 if(o.invulnerable&&player(local,victim))return 0;
 if(o.damageBoost&&player(local,attacker)&&!player(local,victim)&&victim&&victim!=UINT64_MAX&&std::isfinite(o.multiplier))
  return std::min(amount*std::clamp(o.multiplier,1.f,99.f),1000000.f);
 return amount;
}
inline bool refill(int current,int capacity){return capacity>0&&capacity<=10000&&current>=0&&current<capacity;}
}
