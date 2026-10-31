#include "motor/default_input.hpp"
#include "motor/focus_prompt.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
using namespace circle::motor::input;
Physical neutral(){Physical p;p.session=1;p.actor=99;p.focused=p.focusFresh=true;p.hand[0].tracked=p.hand[1].tracked=true;return p;}
int main(){
 for(unsigned h=0;h<2;++h){
  Profile profile;auto p=neutral();profile.update(p,Context::Carrying,1000);
  p.hand[1].y=-1;p.hand[h].grip=1;profile.update(p,Context::Carrying,1010);
  p.speed[h]=2.5f;p.motionTick=1020;auto o=profile.update(p,Context::Carrying,1020,1u<<h);
  assert(!o.physicalAttacks); // release decision precedes item melee
  p.speed[h]=0;p.motionTick=1070;p.hand[h].grip=0;
  o=profile.update(p,Context::Carrying,1070);assert(o.carryAction==CarryAction::ThrowPrime&&o.throwToken==1070&&!o.physicalAttacks&&!(o.pad.buttons&(X|Y)));
  o=profile.update(p,Context::Carrying,1110);assert(o.carryAction==CarryAction::ThrowPrime&&!o.physicalAttacks&&!(o.pad.buttons&(X|Y)));
  o=profile.update(p,Context::Carrying,1250);assert(o.carryAction==CarryAction::ThrowPrime&&o.throwToken==1070);
  o=profile.update(p,Context::Carrying,1490);assert(o.carryAction==CarryAction::None);
  o=profile.update(p,Context::Carrying,1510);assert(o.carryAction==CarryAction::None); // no repeated throw
 }
 // Native pickup/attack transitions must not erase ownership or the real release.
 for(bool releaseBeforePickup:{false,true}){
  Profile profile;auto p=neutral();profile.update(p,Context::Unarmed,1000);
  p.pickupReady=true;p.useTarget=55;profile.update(p,Context::Carrying,1010);
  p.hand[1].y=-1;p.hand[1].grip=1;profile.update(p,Context::Carrying,1020);
  if(releaseBeforePickup)p.hand[1].grip=0;
  profile.update(p,Context::Unknown,1030);
  p.pickupReady=false;p.useTarget=0;
  profile.update(p,Context::Carrying,1040);
  p.hand[1].grip=0;
  profile.update(p,Context::Carrying,1050);
  auto o=profile.update(p,Context::Carrying,1090);
  assert(o.carryAction==CarryAction::Drop);
 }
 {
  Profile profile;auto p=neutral();profile.update(p,Context::Carrying,1000);
  p.hand[1].y=-1;p.hand[1].grip=1;profile.update(p,Context::Carrying,1010);
  p.focusFresh=false;p.hand[1].grip=0;profile.update(p,Context::Carrying,1020);
  p.focusFresh=true;profile.update(p,Context::Carrying,1100);
  assert(profile.update(p,Context::Carrying,1140).carryAction==CarryAction::Drop);
  assert(profile.update(p,Context::Carrying,1340).carryAction==CarryAction::None);
  assert(profile.update(p,Context::Carrying,1380).carryAction==CarryAction::Drop);
 }
 {ThrowOnce gate;
  assert(gate.consume(1,99,1000,1000));
  assert(!gate.consume(1,99,1000,1001)&&!gate.consume(1,99,1000,1200));
  assert(!gate.consume(1,99,1300,1299)&&!gate.consume(1,99,1300,1800));
  assert(gate.consume(1,99,1900,1910));
  assert(gate.consume(2,99,2000,2000));
 }
 for(int variant=0;variant<5;++variant){
  Profile profile;auto p=neutral();profile.update(p,Context::Carrying,1000);p.hand[1].y=-1;p.hand[1].grip=1;profile.update(p,Context::Carrying,1010);
  p.speed[variant==1?0:1]=variant==0?1.f:3.f;p.motionTick=variant==2?800:1020;profile.update(p,Context::Carrying,1020);
  const uint64_t release=variant==3?1400:1050;p.hand[1].grip=0;p.speed={};
  if(variant==4)p.placeTarget=44;
  profile.update(p,Context::Carrying,release);auto o=profile.update(p,Context::Carrying,release+40);
  assert(o.carryAction==(variant==4?CarryAction::Place:CarryAction::Drop));
 }
 {Profile profile;auto p=neutral();profile.update(p,Context::Carrying,1000);p.hand[1].y=-1;p.hand[1].grip=1;profile.update(p,Context::Carrying,1010);
  p.speed[1]=3;p.motionTick=1020;profile.update(p,Context::Carrying,1020);p.hand[1].grip=0;profile.update(p,Context::Carrying,1030);
  p.focused=false;assert(profile.update(p,Context::Carrying,1100).carryAction==CarryAction::None);
  p.focused=true;assert(profile.update(p,Context::Carrying,1200).carryAction==CarryAction::None);
 }
 // Actual museum prompt values: floor item 0x22, vitrine 0x02.
 const auto pickup=circle::motor::interaction::decodeFocus(0x22,21,21);
 const auto vitrine=circle::motor::interaction::decodeFocus(0x02,44,44);
 assert(pickup.fresh&&pickup.useTarget==21&&!pickup.placeTarget&&pickup.otherTarget==21);
 assert(vitrine.fresh&&vitrine.placeTarget==44&&!vitrine.otherTarget);
 assert(!circle::motor::interaction::decodeFocus(0x22,~0ull,~0ull).fresh);
 assert(!circle::motor::interaction::decodeFocus(0x80,21,21).fresh);
 assert(circle::motor::interaction::decodeFocus(8,44,~0ull).placeTarget==44);
 assert(circle::motor::interaction::decodeFocus(0,~0ull,~0ull).fresh);
 for(unsigned h=0;h<2;++h){
  circle::motor::interaction::PickupStage stage;
  assert(!stage.update(99,1,pickup));
  assert(stage.update(99,14,pickup));
  assert(stage.update(99,14,{})); // transient focus loss does not mean held
  assert(!stage.update(99,10,{}));
  assert(stage.update(99,14,pickup));
  assert(!stage.update(100,14,{}));
  Profile profile;auto p=neutral();profile.update(p,Context::Unarmed,1000);
  // Recorded live sequence: merely looking at a floor pickup equips OBJECT
  // long before X/Grip. CARRYING must not eat its X or acknowledge pickup yet.
  p.pickupReady=true;p.useTarget=21;profile.update(p,Context::Carrying,1010);
  p.hand[1].y=-1;p.hand[h].grip=1;
  assert(profile.update(p,Context::Carrying,1020).pad.buttons&X);
  assert(profile.update(p,Context::Carrying,1300).pad.buttons&X);
  p.pickupReady=false;p.useTarget=0;
  assert(!(profile.update(p,Context::Carrying,1310).pad.buttons&(X|Y)));
  p.placeTarget=44;p.hand[h].grip=0;profile.update(p,Context::Carrying,1320);
  assert(profile.update(p,Context::Carrying,1360).carryAction==CarryAction::Place);
 }
 for(unsigned hand=0;hand<2;++hand){
  // Gaze pickup must work exactly like X even without a near-hand point or
  // asynchronously published target. One press only, no held-grip auto-repeat.
  Profile profile;auto p=neutral();p.focusFresh=false;
  profile.update(p,Context::Unarmed,1000);p.hand[hand].grip=1;
  assert(profile.update(p,Context::Unarmed,1010).pad.buttons&X);
  assert(profile.update(p,Context::Unarmed,1100).pad.buttons&X);
  assert(!(profile.update(p,Context::Unarmed,1130).pad.buttons&X));
  p.useTarget=21;p.focusFresh=true;
  assert(!(profile.update(p,Context::Unarmed,1200).pad.buttons&X));
  profile.update(p,Context::Carrying,1250);p.placeTarget=44;p.hand[hand].grip=0;
  profile.update(p,Context::Carrying,1260);
  assert(profile.update(p,Context::Carrying,1300).carryAction==CarryAction::Place);
 }
 for(unsigned hand=0;hand<2;++hand){
  Profile profile;auto p=neutral();profile.update(p,Context::Unarmed,1000);
  p.useTarget=pickup.useTarget;p.hand[hand].grip=.8f;assert(!(profile.update(p,Context::Unarmed,1010).pad.buttons&X));
  p.hand[hand].grip=.95f;assert(profile.update(p,Context::Unarmed,1020).pad.buttons&X);
  // Native slot changes acknowledge pickup; holding must not hold X forever.
  p.useTarget=0;auto out=profile.update(p,Context::Carrying,1040);assert(!(out.pad.buttons&(X|Y)));
  assert(!(profile.update(p,Context::Carrying,9000).pad.buttons&(X|Y)));
  p.hand[hand^1].grip=1;profile.update(p,Context::Carrying,9010);
  p.hand[hand^1].grip=0;assert(!(profile.update(p,Context::Carrying,9020).pad.buttons&(X|Y)));
  p.placeTarget=vitrine.placeTarget;p.useTarget=vitrine.useTarget;p.releaseUseTarget=vitrine.otherTarget;p.hand[hand].grip=0;
  assert(!(profile.update(p,Context::Carrying,9030).pad.buttons&(X|Y)));
  out=profile.update(p,Context::Carrying,9070);assert(out.carryAction==CarryAction::Place&&(out.pad.buttons&X)&&!(out.pad.buttons&Y));
  out=profile.update(p,Context::Carrying,9100);assert(out.pad.buttons&X);
  out=profile.update(p,Context::Unarmed,9120);assert(!(out.pad.buttons&Y));
 }
 {Profile profile;auto p=neutral();profile.update(p,Context::Carrying,1000);
  p.hand[1].y=-1;p.hand[1].grip=1;assert(!(profile.update(p,Context::Carrying,1010).pad.buttons&(X|Y)));
  p.hand[1].grip=0;profile.update(p,Context::Carrying,1020);
  auto out=profile.update(p,Context::Carrying,1060);assert(out.carryAction==CarryAction::Drop&&(out.pad.buttons&Y)&&!(out.pad.buttons&X));
  assert(!(profile.update(p,Context::Carrying,1260).pad.buttons&(X|Y)));
 }
 for(int reason=0;reason<4;++reason){Profile profile;auto p=neutral();profile.update(p,Context::Carrying,1000);p.hand[1].y=-1;p.hand[1].grip=1;profile.update(p,Context::Carrying,1010);
  auto ctx=Context::Carrying;
  if(reason==0)p.focused=false;if(reason==1)p.hand[1].tracked=false;if(reason==2)ctx=Context::Menu;if(reason==3)p.session=2;
  profile.update(p,ctx,1020);p.focused=true;p.hand[1].tracked=true;p.hand[1].grip=0;
  assert(!(profile.update(p,Context::Carrying,1080).pad.buttons&Y));
  assert(!(profile.update(p,Context::Carrying,1300).pad.buttons&Y));
 }
 {Profile profile;auto p=neutral();profile.update(p,Context::Carrying,1000);p.hand[1].y=-1;p.hand[1].grip=1;profile.update(p,Context::Carrying,1010);
  p.placeTarget=44;p.hand[1].grip=0;profile.update(p,Context::Carrying,1020);p.placeTarget=45;
  assert(!(profile.update(p,Context::Carrying,1060).pad.buttons&(X|Y)));
 }
 for(bool stale:{false,true}){Profile profile;auto p=neutral();profile.update(p,Context::Carrying,1000);p.hand[1].y=-1;p.hand[1].grip=1;profile.update(p,Context::Carrying,1010);
  p.hand[1].grip=0;if(stale)p.focusFresh=false;else p.releaseUseTarget=55;
  profile.update(p,Context::Carrying,1020);assert(!(profile.update(p,Context::Carrying,1100).pad.buttons&(X|Y)));
 }
 {Profile profile;auto p=neutral();profile.update(p,Context::Carrying,1000);p.hand[1].y=-1;p.hand[1].grip=1;profile.update(p,Context::Carrying,1010);
  p.hand[0].lower=true;p.hand[1].grip=0;auto out=profile.update(p,Context::Carrying,1020);assert((out.pad.buttons&X)&&!(out.pad.buttons&Y));
 }
}
