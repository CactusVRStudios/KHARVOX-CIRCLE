#pragma once
#include <array>
#include <cstdint>
#include <cmath>
namespace circle::motor::input {
enum class CarryAction {None,Place,Drop,ThrowPrime,ThrowRelease};
struct CarrySample {
 bool allowed{},carrying{},focused{},focusFresh{};
 uint64_t session{},actor{},placeTarget{},otherTarget{};
 std::array<bool,2> tracked{};
 std::array<float,2> grip{};
 std::array<float,2> speed{};
 uint64_t motionTick{};
 std::array<bool,2> throwMode{};
 bool flickThrow{};
};
struct CarryResult {CarryAction action{};unsigned hands{};bool neutral{},owned{};uint64_t throwToken{};};
struct ThrowOnce {
 uint64_t session{},actor{},token{};
 bool consume(uint64_t s,uint64_t a,uint64_t t,uint64_t now){
  if(!s||!a||!t||now<t||now-t>420)return false;
  if(session==s&&actor==a&&token==t)return false;
  session=s;actor=a;token=t;return true;
 }
};
// Neutral -> up -> neutral within 250 ms. Holding up never throws.
struct ThrowStick {
 bool armed{};uint64_t raised{};
 bool update(bool allowed,float x,float y,uint64_t now){
  if(!allowed||!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>.45f){*this={};return false;}
  if(raised&&(now<raised||now-raised>250)){raised=0;armed=false;}
  if(std::abs(y)<=.25f){const bool fire=raised!=0;raised=0;armed=true;return fire;}
  if(y<-.25f){raised=0;armed=false;return false;}
  if(y>=.75f&&armed){raised=now;armed=false;}
  return false;
 }
};
class GripCarry {
 int hand=-1,pending=-1;
 uint64_t pendingUntil{},session{},actor{},start{},target{},fastAt{};
 std::array<bool,2> armed{};
 CarryAction action{};
 bool initialized{};
public:
 void cancel(){hand=pending=-1;pendingUntil=start=target=fastAt=0;armed={};action=CarryAction::None;}
 void pickup(unsigned h,uint64_t now){pending=int(h);pendingUntil=now+5000;}
 CarryResult update(const CarrySample& s,uint64_t now){
  if(!initialized||session!=s.session||actor!=s.actor){cancel();session=s.session;actor=s.actor;initialized=true;}
  if(!s.allowed||!s.focused){cancel();return {};}
  for(unsigned h=0;h<2;++h){if(!s.tracked[h])armed[h]=false;else if(s.grip[h]<=.65f)armed[h]=true;}
  if(hand>=0&&!s.tracked[hand]){cancel();return {};}
  if(pending>=0&&(now>pendingUntil||!s.tracked[pending]))pending=-1;
  if(!s.carrying){hand=-1;action=CarryAction::None;start=fastAt=0;return {};}
  if(action!=CarryAction::None){
   // Never redirect a release to a different object while waiting for X-up.
   if(action==CarryAction::Place&&(!s.focusFresh||s.placeTarget!=target)){cancel();return {};}
   const bool throwing=action==CarryAction::ThrowPrime;
   if(now<start||now-start>=(throwing?420u:action==CarryAction::Drop?2000u:240u)){action=CarryAction::None;return {};}
   if(throwing)return {CarryAction::ThrowPrime,0,false,false,start};
   const bool neutral=action==CarryAction::Drop?(now-start)%240<40:now-start<40;
   return {neutral?CarryAction::None:action,0,neutral,false};
  }
  if(hand<0&&pending>=0){hand=pending;pending=-1;armed[hand]=false;}
  if(hand<0){
   for(unsigned h=0;h<2;++h)if(s.tracked[h]&&armed[h]&&s.grip[h]>=.9f){hand=int(h);armed[h]=false;break;}
  }
  if(hand<0)return {};
  // A short velocity peak may precede the squeeze release. Never refresh it
  // from stale tracking or another hand; holding still expires it after 300ms.
  if(!s.throwMode[hand])fastAt=0;
  if(s.throwMode[hand]&&s.motionTick&&now>=s.motionTick&&now-s.motionTick<=100&&
     std::isfinite(s.speed[hand])&&s.speed[hand]>=2.f&&s.speed[hand]<15.f)fastAt=s.motionTick;
  const unsigned mask=1u<<hand;
  if(s.flickThrow&&s.focusFresh&&!s.placeTarget&&!s.otherTarget){
   hand=pending=-1;fastAt=0;start=now;action=CarryAction::ThrowPrime;
   return {action,mask,false,false,start};
  }
  if(s.grip[hand]>.65f)return {CarryAction::None,mask,false,true};
  // A stale query or another non-placement use prompt must not turn into a
  // destructive drop. The original X/stick controls remain available.
  if(!s.focusFresh||(!s.placeTarget&&s.otherTarget))return {};
  hand=-1;pending=-1;
  action=s.placeTarget?CarryAction::Place:(fastAt&&now>=fastAt&&now-fastAt<=300?CarryAction::ThrowPrime:CarryAction::Drop);
  fastAt=0;target=s.placeTarget;start=now;
  if(action==CarryAction::ThrowPrime)return {action,mask,false,false,start};
  return {CarryAction::None,mask,true,false};
 }
};
}
