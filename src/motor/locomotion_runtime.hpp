#pragma once
#include "experimental.hpp"
#include "locomotion_policy.hpp"
namespace circle::motor::locomotion {
bool laserReadyInput();
bool passengerCombatActive(uint64_t owner);
bool combatCameraActive(uint64_t owner,uint64_t now);
void installInput();
void resetCameraAnchor();
bool menuCursorTarget(int x,int y,bool active);
bool nativeMenuCursorActive();
bool consumeInstantThrow(uint64_t owner);
std::array<float,2> fingerGestures();
bool waterPhysicsBasis(uint64_t owner,unsigned mode,WaterBasis& basis);
// One immutable adjustment per same-state stereo pair.
struct CameraMotion {bool active{},referenceValid{};float referenceYaw{};float vertical{};float referenceCompensation{};uint64_t epoch{};float nativeYaw{};CameraAnchor cameraAnchor{};Vec physics{};bool gravityAligned{};
 bool mounted{},passengerEntryRecenter{};std::array<float,12> mountedRoot{};
 bool armedPassenger{};float passengerBodyYaw{};
};
CameraMotion update(const experimental::EyeRig&,const std::array<float,12>& base,bool recenter,bool calibrate=true);
}
