#include "cheat_policy.hpp"
#include <cassert>
#include <limits>
using namespace circle::motor::cheats;
int main(){
 Options o;
 assert(!o.invulnerable&&!o.stealth&&!o.stamina&&!o.oxygen&&!o.ammo&&!o.damageBoost);
 assert(damage(10,7,8,7,o)==10);
 o.damageBoost=true;
 assert(damage(10,7,8,7,o)==990);
 assert(damage(10,7,7,8,o)==10);
 assert(damage(10,7,8,9,o)==10);
 assert(damage(10,0,8,0,o)==10);
 assert(damage(10,UINT64_MAX,8,UINT64_MAX,o)==10);
 assert(damage(10,7,0,7,o)==10);
 assert(damage(10,7,7,7,o)==10);
 o.invulnerable=true;
 assert(damage(10,7,7,8,o)==0);
 assert(damage(10,7,7,7,o)==0);
 assert(damage(-5,7,7,8,o)==-5); // healing is not damage
 assert(damage(0,7,7,8,o)==0);
 assert(damage(10,7,8,9,o)==10);
 assert(std::isnan(damage(std::numeric_limits<float>::quiet_NaN(),7,8,7,o)));
 o.multiplier=2;assert(damage(10,7,8,7,o)==20);
 o.multiplier=999;assert(damage(10,7,8,7,o)==990);
 o.multiplier=0;assert(damage(10,7,8,7,o)==10);
 o.multiplier=std::numeric_limits<float>::quiet_NaN();assert(damage(10,7,8,7,o)==10);
 assert(refill(0,6)&&refill(5,6));
 assert(!refill(6,6)&&!refill(7,6)&&!refill(-1,6)&&!refill(0,0)&&!refill(1,100000));
 assert(player(7,7)&&!player(7,8)&&!player(0,0)&&!player(UINT64_MAX,UINT64_MAX));
}
