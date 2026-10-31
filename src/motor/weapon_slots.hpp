#pragma once

namespace circle::motor::hands {
// Native weaponSlot_t (GOG/Steam 1.0.17), reflected in r62. Named inventory
// equipment has its own slots instead of MELEE/PICKUP. Share classification
// between gesture admission, charge/release and the controller input context.
constexpr bool firearmSlot(unsigned s){return s==2||s==3||s==4||s==25||s==27;}
constexpr bool meleeWeaponSlot(unsigned s){return s==8||s==23||s==24||s==26;}
constexpr bool carriedStrikeSlot(unsigned s){return meleeWeaponSlot(s)||s==10||s==14;}
constexpr bool itemMeleeSlot(unsigned s){return firearmSlot(s)||carriedStrikeSlot(s);}
constexpr bool meleeChargeSlot(unsigned s){return s==1||itemMeleeSlot(s);}
constexpr unsigned physicalStrikePulseMs(unsigned s){return carriedStrikeSlot(s)?450:200;}
}
