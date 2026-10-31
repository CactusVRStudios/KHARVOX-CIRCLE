#pragma once
#include <cstdint>
namespace circle::motor::cheats {
void install();
bool ammoEnabled();
void weapon(uint64_t owner,uint64_t entity,uintptr_t loadout,unsigned slot,uintptr_t attachment,uintptr_t active);
}
