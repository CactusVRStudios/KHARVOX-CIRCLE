#include "default_input.hpp"
#include "firearm_policy.hpp"
#include <cstdio>
#include <limits>
using namespace circle::motor::input;
int main(){
 int failures=0;auto check=[&](bool b,const char* why){if(!b){++failures;std::printf("FAIL %s\n",why);}};
 auto neutral=[] {Physical p;p.focused=true;p.session=1;p.hand[0].tracked=p.hand[1].tracked=true;return p;};
 {
  Native n{true,false,true,false,false,1,10,5,0};n.firearm=true;
  check(context(n)==Context::SeatedFirearm,"observed passenger pickup SMG uses seated firearm controls");
  n.firearm=false;check(context(n)==Context::Seated,"generic pickup is not a firearm");
  n.slot=4;check(context(n)==Context::SeatedFirearm,"inventory pistol works in passenger seat");
  n.slot=10;n.firearm=true;n.projection=false;
  check(context(n)==Context::SeatedFirearm,"seat retains axes through render handoff");n.projection=true;
  n.mode=24;check(context(n)==Context::BoatDriver,"armed inventory cannot override boat driver");n.mode=1;
  Profile p;auto v=neutral();v.actor=7;v.temporaryFirearm=9;p.update(v,context(n),1000);
  v.hand[1].grip=1;auto o=p.update(v,context(n),1010);
  check(o.aim&&!o.pad.rt&&!(o.pad.buttons&X),"seated right grip readies gun without Use/exit");
  v.hand[1].trigger=.8f;v.hand[0].x=.7f;v.hand[0].y=.9f;v.hand[1].x=.6f;v.hand[1].y=.8f;
  o=p.update(v,context(n),1020,3);
  check(o.aim&&o.pad.rt==.8f&&(combatActions(o)&(uint64_t(1)<<5)),"passenger trigger and native aim action both reach gun");
  check(!o.pad.lx&&!o.pad.ly&&!o.pad.rx&&!o.pad.ry&&!o.motion&&o.viewTurn==.6f&&!o.physicalAttacks&&!o.holster,"passenger only turns VR yaw; no walking pitch or melee");
  v.hand[1].trigger=0;v.supportGrip=true;v.hand[0].grip=1;o=p.update(v,context(n),1030);
  check(!o.reload&&!(o.pad.buttons&X),"support grip does not reload or exit seat");
  v.supportGrip=false;o=p.update(v,context(n),1040);
  check(o.reload&&combatActions(o)==(uint64_t(1)<<20)&&!o.pad.rt,"seated offhand reload uses same native action");
  v=neutral();p.update(v,context(n),1050);v.hand[0].lower=true;o=p.update(v,context(n),1060);
  check((o.pad.buttons&X)&&!o.pad.rt,"seated manual Use remains available");
  v=neutral();p.update(v,context(n),1070);v.hand[0].upper=true;v.hand[0].grip=1;
  v.hand[0].x=v.hand[0].y=v.hand[1].x=v.hand[1].y=1;o=p.update(v,context(n),1080);
  check(o.modifier&&!o.pad.lx&&!o.pad.ly&&!o.pad.rx&&!o.pad.ry&&!o.viewTurn,"compatibility page cannot bypass seat axis ownership");
  v=neutral();p.update(v,context(n),1100);v.hand[0].menu=true;
  check(p.update(v,context(n),1110).pause,"seated firearm still permits pause");
  Profile focus;v=neutral();v.hand[1].grip=v.hand[1].trigger=1;
  check(!focus.update(v,context(n),1200).pad.rt,"held trigger cannot fire on seat entry");
  v=neutral();focus.update(v,context(n),1210);v.hand[1].grip=1;focus.update(v,context(n),1220);
  v.hand[1].trigger=1;check(focus.update(v,context(n),1230).pad.rt==1,"fresh seat trigger fires");
  v.focused=false;check(!focus.update(v,context(n),1240).pad.rt,"focus loss stops fire");
  v.focused=true;check(!focus.update(v,context(n),1250).pad.rt,"focus return requires trigger release");
  n.menu=true;check(context(n)==Context::Menu,"menu still owns passenger inputs");n.menu=false;
  n.type=0;check(context(n)==Context::Firearm,"dismount restores ordinary firearm controls");
 }
 {
  Native n{true,false,true,false,false,24,0,0,0};
  check(context(n)==Context::BoatDriver,"boat driver retains native traversal input route");
  Profile p;auto v=neutral();p.update(v,context(n),1000);
  v.hand[0].x=.6f;v.hand[0].y=.7f;v.hand[1].x=-.5f;v.hand[1].y=.4f;
  v.hand[0].trigger=.3f;v.hand[1].trigger=.9f;
  auto o=p.update(v,context(n),1020);
  check(o.pad.lx==.6f&&o.pad.ly==.7f&&o.pad.rx==0&&o.pad.ry==0,
        "boat steering retained while native look is suppressed");
  check(o.pad.lt==.3f&&o.pad.rt==.9f&&!o.motion,"boat triggers retained without HMD-relative walking");
  v.hand[0].upper=true;v.hand[0].grip=1;v.hand[1].upper=true;
  o=p.update(v,context(n),1040);
  check(!o.pad.buttons&&!o.pad.rx&&!o.pad.ry&&!o.pad.rt,"boat compatibility chord cannot equip or rotate player");
  v=neutral();p.update(v,context(n),1100);v.hand[0].menu=true;
  check(p.update(v,context(n),1120).pause,"driver pause remains available");
  n.slot=13;check(context(n)==Context::BoatDriver,"equipment cannot steal driver controls");
  Profile book;v=neutral();book.update(v,Context::BoatDriver,2000);
  v.hand[0].upper=true;book.update(v,Context::BoatDriver,2010);
  v.hand[0].upper=false;o=book.update(v,Context::BoatDriver,2100);
  check(o.pad.buttons==Back&&!o.motion&&!o.pad.rx,"short Y opens book while driving");
  check(!book.update(v,Context::BoatDriver,2200).pad.buttons,"driver book pulse releases");
  n.slot=16;n.journal=1;n.projection=false;
  check(context(n)==Context::Journal,"raised driver journal gets modal navigation");
  n.journal=2;check(context(n)==Context::BoatMap,"lowered driver map retains dedicated boat steering");
  Profile map;v=neutral();map.update(v,context(n),3000);
  v.hand[0].y=.7f;v.hand[1].x=.8f;v.hand[1].y=.6f;v.hand[1].grip=1;
  o=map.update(v,context(n),3010);
  check(o.pad.ly==.7f&&!o.pad.rx&&!o.pad.ry&&!o.motion&&o.pad.buttons==RB,"boat map pages without native look or grip exit");
  v=neutral();map.update(v,context(n),3020);v.hand[0].upper=true;map.update(v,context(n),3030);
  v.hand[0].upper=false;check(map.update(v,context(n),3100).pad.buttons==Back,"boat map can close with short Y");
  n.slot=1;n.journal=0;check(context(n)==Context::BoatDriver,"closing book restores driver controls");

 }
 {
  Native n{true,false,true,false,false,1,1,5,0};
  check(context(n)==Context::Seated,"measured passenger seat is no longer unknown");
  n.projection=false;check(context(n)==Context::Seated,"seat input ownership survives render handoff before immersive publication");n.projection=true;
  for(unsigned h=0;h<2;++h){
   Profile seat;auto v=neutral();seat.update(v,Context::Seated,1000);
   v.hand[h].grip=1;auto use=seat.update(v,Context::Seated,1010);
   check((use.pad.buttons&X)&&!use.physicalAttacks&&!use.pad.rt&&!use.pad.lt&&!use.motion,"passenger grip uses without attacking or driving");
   v.hand[h].grip=0;check(!(seat.update(v,Context::Seated,1020).pad.buttons&X),"passenger Use releases with grip");
  }
  Profile p;auto v=neutral();p.update(v,context(n),1000);
  v.hand[0].x=.8f;v.hand[0].y=.9f;v.hand[1].x=.7f;v.hand[1].y=.8f;
  auto o=p.update(v,context(n),1020);
  check(!o.pad.lx&&!o.pad.ly&&!o.pad.rx&&!o.pad.ry&&o.viewTurn==.7f&&!o.motion,"seated yaw is VR-only; no walking or native pitch");
  n.projection=false;o=p.update(v,context(n),1030);
  check(o.context==Context::Seated&&!o.pad.rx&&!o.pad.ry&&o.viewTurn==.7f,"render transition cannot leak native look axes");n.projection=true;
  unsigned seatTick=1040;
  for(unsigned slot:{14u,0u,18u,1u}){
   n.slot=slot;o=p.update(v,context(n),seatTick);seatTick+=10;
   check(context(n)==Context::Seated&&!o.pad.lx&&!o.pad.ly&&!o.pad.rx&&!o.pad.ry&&o.viewTurn==.7f,
         "boarding equipment changes cannot release seat axes to native pitch or movement");
  }
  n.type=0;check(context(n)==Context::Unarmed,"ordinary crouch retains gameplay input");
  n.type=5;n.mode=24;check(context(n)==Context::BoatDriver,"boat driver retains independent native steering");
  n.mode=1;n.menu=true;check(context(n)==Context::Menu,"pause still owns navigation");
 }
 for(auto ctx:{Context::Normal,Context::Carrying,Context::Melee}){
  Profile p;auto v=neutral();v.actor=7;v.equippedFood=true;p.update(v,ctx,1000);
  v.hand[1].grip=1;p.update(v,ctx,1020);
  auto held=p.update(v,ctx,1600,2);
  check(!held.pad.buttons&&!held.physicalAttacks,"food grip and hand movement neither Use nor strike; mouth gesture can own RT");
  v.hand[1].trigger=.8f;auto rt=p.update(v,ctx,1620,2);
  check(rt.pad.rt==.8f&&!rt.physicalAttacks,"native food RT preserved even in carried-item/melee slot");
  v.hand[0].grip=1;rt=p.update(v,ctx,1640,3);
  check(!(rt.pad.buttons&LB)&&!rt.physicalAttacks,"food does not enter two-fist guard or physical strikes");
  v.hand[1].trigger=0;p.update(v,ctx,1660);v.hand[0].lower=true;
  check(p.update(v,ctx,1680).pad.buttons&X,"manual Use retained with food");
 }
 {
  BlowState state;state.observe(7,1000);
  check(state.active(7,1099)&&!state.active(7,1100)&&!state.active(8,1010)&&!state.active(7,999),"blow observation requires matching owner, fresh time and monotonic clock");
  state.end(8);check(state.active(7,1010),"another entity cannot end player blow state");
  state.end(7);check(!state.active(7,1010),"native effect exit revokes blow immediately");
  Native n{true,false,true,false,false,0,1,0,0};n.blow=true;
  check(context(n)==Context::Blow,"native active air-effect routes ordinary unarmed scene to blow");
  n.blow=false;check(context(n)==Context::Unarmed,"ordinary fists retain physical melee");n.blow=true;
  for(unsigned slot:{4u,6u,10u,13u,17u,18u,23u}){n.slot=slot;check(context(n)!=Context::Blow,"air observation does not override held tools or weapons");}n.slot=1;
  n.menu=true;check(context(n)==Context::Menu,"menu overrides blow");n.menu=false;
  n.animated=true;check(context(n)!=Context::Blow,"authored animation excludes blow");n.animated=false;
  n.fresh=false;check(context(n)==Context::Unknown,"stale native state excludes blow");n.fresh=true;
  n.projection=false;check(context(n)!=Context::Blow,"cinema excludes blow");
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);
  v.hand[0].trigger=v.hand[1].trigger=1;
  check(!blowActions(p.update(v,Context::Blow,1010)),"held attack triggers cannot activate blow on context entry");
  v=neutral();p.update(v,Context::Blow,1020);
  for(unsigned hand=0;hand<2;++hand){
   v.hand[hand].trigger=1;auto o=p.update(v,Context::Blow,1030+hand*20,3);
   check(!o.blow&&!o.physicalAttacks&&!combatActions(o)&&!o.pad.lt&&!o.pad.rt,"one trigger or a swing cannot blow or send combat aliases");
   v.hand[hand].trigger=0;p.update(v,Context::Blow,1040+hand*20);
  }
  v.hand[0].trigger=.8f;v.hand[1].trigger=.9f;v.hand[1].x=.7f;
  auto o=p.update(v,Context::Blow,1070,3);
  check(blowActions(o)==(1u|(1u<<15))&&!combatActions(o)&&!o.physicalAttacks&&!o.gestures&&!o.pad.lt&&!o.pad.rt,"two index triggers send only native air attack actions, no grip or swing required");
  check(o.motion&&o.pad.rx==.7f,"blow preserves ordinary VR stick turning");
  check(blowActions(p.update(v,Context::Blow,1080))==blowActions(o),"hold stays held without a repeat macro");
  v.hand[0].trigger=0;check(!blowActions(p.update(v,Context::Blow,1090)),"release either trigger ends blow input");
  v.hand[0].trigger=1;v.focused=false;check(!blowActions(p.update(v,Context::Blow,1100)),"focus loss releases blow");
  v.focused=true;check(!blowActions(p.update(v,Context::Blow,1110)),"focus return needs fresh trigger presses");
  v=neutral();p.update(v,Context::Blow,1120);v.hand[0].trigger=v.hand[1].trigger=1;
  v.hand[1].tracked=false;check(!blowActions(p.update(v,Context::Blow,1130)),"missing hand prevents two-trigger blow");
  v.hand[1].tracked=true;check(!blowActions(p.update(v,Context::Blow,1140)),"tracking return keeps held trigger blocked");
  v=neutral();p.update(v,Context::Blow,1150);v.hand[0].trigger=v.hand[1].trigger=1;
  o=p.update(v,Context::Unarmed,1160,3);
  check(!blowActions(o)&&!o.physicalAttacks&&!o.pad.lt&&!o.pad.rt,"scene exit requires neutral before combat and cannot leak held blow input");
  v=neutral();p.update(v,Context::Blow,1170);v.hand[0].trigger=v.hand[1].trigger=1;v.hand[0].menu=true;
  o=p.update(v,Context::Blow,1180);check(o.pause&&!blowActions(o),"pause takes priority over blow");
 }
 {
  Native state{true,false,true,false,false,14,1,5,0};
  const auto ctx=context(state);Profile p;auto v=neutral();p.update(v,ctx,1000);
  v.hand[0].x=.6f;v.hand[1].x=.8f;v.hand[1].trigger=1;
  auto o=p.update(v,ctx,1010);
  check(ctx==Context::Ledge&&o.pad.buttons==RB&&o.pad.rt==0&&!combatActions(o),"hanging trigger requests native whip without attack or yank");
  check(o.viewTurn==.8f&&o.pad.rx==0&&o.pad.ry==0&&o.pad.lx==.6f,"hanging whip retains VR turning and native shimmy input");
  v.hand[1].trigger=0;check(!(p.update(v,ctx,1020).pad.buttons&RB),"hanging whip releases with trigger");
  v.hand[1].trigger=1;v.focused=false;p.update(v,ctx,1030);v.focused=true;
  check(!(p.update(v,ctx,1040).pad.buttons&RB),"focus restoration cannot fire a held whip trigger");
 }
 {
  // Native dedicated inventory slots must reach the same complete gesture
  // path as ordinary MELEE, not just be admitted by the velocity detector.
  namespace hands=circle::motor::hands;
  for(unsigned slot:{8u,23u,24u,26u}){
   Native n;n.fresh=n.projection=true;n.mode=0;n.slot=slot;
   const auto ctx=context(n);check(ctx==Context::Melee,"spade/crozier/baton use melee input context");
   Profile profile;auto v=neutral();profile.update(v,ctx,1000);
   v.hand[1].grip=1;profile.update(v,ctx,1010);
   v.hand[1].trigger=1;
   auto idle=profile.update(v,ctx,1020);
   check(!idle.physicalAttacks&&idle.pad.rt==0,"inventory melee trigger alone cannot produce a strike");
   const bool eligible=hands::itemStrikeEligible(slot,1,1,1,{});
   hands::VelocityPunchDetector swing;
   check(!swing.update({}, {},1020,eligible),"inventory weapon arms at physical rest");
   const bool fired=swing.update({0,2.1f,0},{0,1,0},1030,eligible);
   const auto attack=profile.update(v,ctx,1030,fired?2:0);
   check(fired&&attack.physicalAttacks==2&&(combatActions(attack)&1)&&attack.pad.rt==0,
    "real inventory weapon swing reaches native attack immediately");
   check(hands::meleeChargeSlot(slot)&&hands::physicalStrikePulseMs(slot)==450,"inventory weapon uses ordinary melee charge and release window");
   check(!hands::itemStrikeEligible(slot,1,.5f,0,{}),"inventory weapon requires closed grip");
   n.menu=true;check(context(n)==Context::Menu,"inventory melee cannot override menu context");
   n.menu=false;n.mode=23;check(context(n)==Context::Inspection,"inventory melee cannot override scripted inspection");
  }
  for(unsigned slot:{25u,27u}){
   Native n;n.fresh=n.projection=true;n.mode=0;n.slot=slot;
   check(context(n)==Context::Firearm&&hands::firearmSlot(slot)&&hands::meleeChargeSlot(slot),"fixed Luger/Beretta slots retain firearm and melee-mode routing");
   check(!hands::itemStrikeEligible(slot,1,1,1,{},false)&&hands::itemStrikeEligible(slot,1,1,1,{},true),"inventory pistols preserve firing versus melee arbitration");
  }
  for(unsigned slot:{0u,5u,6u,7u,9u,11u,12u,13u,15u,16u,17u,18u,19u,20u,21u,22u,28u,255u})
   check(!hands::itemMeleeSlot(slot),"tools explosives whip and unsupported slots are not blindly admitted as melee");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Firearm,1000);
  v.hand[1].grip=1;auto o=p.update(v,Context::Firearm,1010);
  check(o.aim&&!o.reload&&!o.firearmMelee,"right grip selects shooting readiness");
  v.hand[1].trigger=.8f;o=p.update(v,Context::Firearm,1020);
  check(o.pad.rt==.8f,"right trigger fires with right grip");
  v.hand[0].grip=1;o=p.update(v,Context::Firearm,1030,3);
  check(o.reload&&!o.aim&&o.pad.rt==0&&o.physicalAttacks==0&&combatActions(o)==(uint64_t(1)<<20)&&!(o.pad.buttons&(X|LB)),"left grip isolates reload from shots/use/guard");
  v.hand[0].grip=0;o=p.update(v,Context::Firearm,1040);
  check(o.aim&&o.pad.rt==0,"reload release cannot fire held trigger");
  v.hand[1].trigger=0;p.update(v,Context::Firearm,1050);
  v.hand[1].trigger=.8f;o=p.update(v,Context::Firearm,1060);check(o.pad.rt==.8f,"fresh trigger fires after reload");
  v.hand[1].trigger=0;v.hand[0].lower=true;o=p.update(v,Context::Firearm,1070);
  check((o.pad.buttons&X)&&!o.reload,"X remains native use/grip while armed");
  v.hand[0].grip=1;o=p.update(v,Context::Firearm,1080);
  check((o.pad.buttons&X)&&!o.reload,"manual X takes priority over reload grip");
  v.hand[0].lower=false;v.hand[1].grip=0;o=p.update(v,Context::Firearm,1090);
  check(o.reload&&!o.aim&&o.pad.rt==0,"left grip alone reloads without shooting readiness");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Firearm,1000);
  v.hand[1].grip=v.hand[1].trigger=1;
  auto o=p.update(v,Context::Firearm,1010);
  check(o.firearmMelee&&!o.aim&&o.pad.rt==0,"charged grip cannot change melee into shooting");
  p.update(v,Context::Firearm,1500,2);o=p.update(v,Context::Firearm,1580,2);
  check(o.physicalAttacks==2&&!o.aim&&o.pad.rt==0,"charged physical swing dispatches weapon strike with trigger held");
  p.update(v,Context::Firearm,1600,0);
  v.hand[1].grip=0;p.update(v,Context::Firearm,1610);
  v.hand[1].grip=1;o=p.update(v,Context::Firearm,1620);
  check(o.firearmMelee&&!o.aim,"charged regrip retains melee mode");
  v.hand[1].trigger=v.hand[1].grip=0;p.update(v,Context::Firearm,1630);
  v.hand[1].grip=1;o=p.update(v,Context::Firearm,1640);
  check(!o.firearmMelee&&o.aim,"fresh uncharged right grip still selects shooting");
 }
 for(bool held:{false,true})for(auto ctx:{Context::Unarmed}){
  Profile p;auto v=neutral();p.update(v,ctx,1000);
  v.hand[0].grip=v.hand[1].grip=1;v.hand[1].click=true;
  auto o=p.update(v,ctx,1010,3);
  check(o.push&&!o.holster&&o.pad.buttons==0&&o.pad.rt==0&&combatActions(o)==0,"push chord waits for pause arbitration without attacks");
  v.hand[1].click=held;o=p.update(v,ctx,1140,3);
  check(o.push&&!o.holster&&o.pad.buttons==RS&&o.pad.lt==0&&o.pad.rt==0&&combatActions(o)==0,"unarmed ready grips plus right click sends only native push");
  v.hand[0].grip=v.hand[1].grip=0;o=p.update(v,ctx,1160);
  check(o.push&&o.pad.buttons==RS&&!o.holster,"grip release cannot change accepted push into holster");
  o=p.update(v,ctx,1300);check(!(o.pad.buttons&(RS|Y|LS)),"push is one bounded pulse even for held click");
  v.hand[1].click=false;p.update(v,ctx,1310);
  v.hand[0].click=true;p.update(v,ctx,1320);
  o=p.update(v,ctx,1450);
  check(!o.push&&(o.pad.buttons&LS),"sprint works after completed push without another right click");
  v.hand[0].click=false;p.update(v,ctx,1500);
  v.hand[1].click=true;p.update(v,ctx,1600);v.hand[1].click=false;o=p.update(v,ctx,1610);
  check(o.holster&&o.pad.buttons==Y,"next ordinary click still holsters");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);
  v.hand[0].grip=v.hand[1].grip=1;v.hand[1].click=true;p.update(v,Context::Unarmed,1010);
  v.hand[0].click=true;auto o=p.update(v,Context::Unarmed,1050);
  check(o.pause&&!o.push&&!o.holster&&o.pad.buttons==Start,"two stick pause retains priority over push chord");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);
  v.hand[0].click=true;p.update(v,Context::Unarmed,1010);
  auto o=p.update(v,Context::Unarmed,1140);
  check((o.pad.buttons&LS)&&!o.push,"ordinary left click still sprints");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);
  v.hand[0].trigger=v.hand[1].trigger=1;v.hand[0].click=true;p.update(v,Context::Unarmed,1010);
  auto o=p.update(v,Context::Unarmed,1140);
  check(!o.push&&(o.pad.buttons&LS),"triggers alone cannot accidentally select push");
 }
 for(auto ctx:{Context::Firearm,Context::Melee,Context::Whip,Context::Carrying,Context::Normal}){
  Profile p;auto v=neutral();p.update(v,ctx,1000);
  v.hand[0].grip=v.hand[1].grip=1;v.hand[1].click=true;p.update(v,ctx,1010);
  auto o=p.update(v,ctx,1140);
  check(!o.push&&o.holster&&o.pad.buttons==Y,"held equipment cannot push; right click holsters");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);
  v.hand[0].grip=v.hand[1].grip=.6f;v.hand[1].click=true;p.update(v,Context::Unarmed,1010);
  auto o=p.update(v,Context::Unarmed,1140);
  check(!o.push&&o.holster,"partial grips without combat readiness do not push");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);
  v.hand[0].grip=v.hand[1].grip=1;v.hand[0].click=true;p.update(v,Context::Unarmed,1010);
  auto o=p.update(v,Context::Unarmed,1140);
  check(!o.push&&(o.pad.buttons&LS),"left click still sprints while guarding");
 }
 for(bool intermediateUnarmed:{false,true})for(bool shooting:{false,true}){
  Profile p;auto v=neutral();v.actor=77;p.update(v,Context::Firearm,1000);
  v.hand[1].grip=1;
  if(!shooting){v.hand[0].trigger=1;}
  auto out=p.update(v,Context::Firearm,1010);
  check(out.firearmMelee!=shooting,"pre-climb firearm mode selected");
  v.hand[1].trigger=1;
  out=p.update(v,Context::Ledge,1100);
  check(!out.aim&&out.pad.rt==0,"native climbing suppresses firearm aim and firing");
  p.update(v,Context::Ledge,2000);
  if(intermediateUnarmed)p.update(v,Context::Unarmed,2010);
  out=p.update(v,Context::Firearm,2020);
  check(out.firearmMelee!=shooting&&out.aim==shooting,"climb return restores selected mode and held readiness through transient unarmed slot");
  check(out.pad.rt==0,"held trigger cannot fire on climb return");
  v.hand[1].trigger=0;p.update(v,Context::Firearm,2030);
  if(!shooting){v.hand[0].trigger=0;v.hand[1].grip=0;p.update(v,Context::Firearm,2040);v.hand[1].grip=1;p.update(v,Context::Firearm,2045);}
  v.hand[1].trigger=.7f;out=p.update(v,Context::Firearm,2050);
  check(out.aim&&out.pad.rt==.7f,"fresh post-climb trigger fires without weapon re-equip");
 }
 {
  Native climb{true,false,false,false,false,3,1,5,0};
  Native crouch{true,false,true,false,false,1,1,0,0};
  Native standing=crouch;standing.mode=0;
  StandAfterClimb s;
  check(!s.update(crouch,1,1,100,true,false),"ordinary crouch is never auto-toggled");
  for(unsigned mode:{3u,4u,6u,13u,14u}){
   s.reset();climb.mode=mode;climb.type=mode==13?7:5;
   check(!s.update(climb,1,1,100,true,false),"climb keeps native controls");
   check(!s.update(crouch,1,1,116,true,false),"climb exit waits for native recovery");
   check(!s.update(crouch,1,1,200,true,false),"early recovery must not consume stand pulse");
   check(s.update(crouch,1,1,366,true,false),"settled climb exit requests stand");
   check(s.update(crouch,1,1,450,true,false),"stand pulse survives multiple input polls");
   check(!s.update(crouch,1,1,486,true,false),"stand pulse has bounded duration");
   for(uint64_t t=500;t<2000;t+=50)check(!s.update(crouch,1,1,t,true,false),"blocked headroom cannot repeat crouch toggle");
  }
  s.reset();s.update(climb,1,1,100,true,false);
  check(!s.update(standing,1,1,116,true,false),"already standing cannot be toggled into crouch");
  check(!s.update(crouch,1,1,132,true,false),"brief standing handoff retains recovery window");
  s.update(crouch,1,1,250,true,false);
  check(s.update(crouch,1,1,366,true,false),"brief standing handoff allows settled crouch correction");
  check(!s.update(standing,1,1,400,true,false)&&!s.update(crouch,1,1,416,true,false),"standing acknowledgement ends request permanently");
  s.reset();s.update(climb,1,1,100,true,false);
  Native waiting=crouch;waiting.projection=false;
  check(!s.update(waiting,1,1,116,true,false)&&!s.update(waiting,1,1,250,true,false)&&s.update(crouch,1,1,366,true,false),"wait for immersive admission and native recovery");
  for(unsigned scenario=0;scenario<8;++scenario){
   s.reset();s.update(climb,1,1,100,true,false);
   auto n=crouch;uint64_t actor=1,session=1,t=116;bool focus=true,manual=false;
   switch(scenario){case 0:n.menu=true;break;case 1:n.fresh=false;break;
    case 2:actor=2;break;case 3:session=2;break;case 4:t=400;break;
    case 5:t=90;break;case 6:focus=false;break;case 7:manual=true;break;}
   check(!s.update(n,actor,session,t,focus,manual),"ownership/focus/manual/discontinuity cancels stand");
   check(!s.update(crouch,1,1,420,true,false),"cancelled climb cannot replay later");
  }
  for(unsigned mode:{2u,7u,10u,11u,12u,15u,16u,23u,26u}){
   s.reset();auto other=climb;other.mode=mode;
   s.update(other,1,1,100,true,false);
   check(!s.update(crouch,1,1,116,true,false),"unrelated actions do not arm standing");
  }
  s.reset();s.update(climb,1,1,100,true,false);auto inspection=crouch;inspection.slot=13;
  check(!s.update(inspection,1,1,116,true,false),"inspection never receives synthetic B");
 }
 {
  Native n{true,false,true,false,false,0,17,0,0};
  check(context(n)==Context::Photo,"photo equipment has dedicated grip zoom context");
  n.menu=true;check(context(n)==Context::Menu,"photo does not override pause");
  n.menu=false;n.mode=10;check(context(n)==Context::Grip,"photo does not override scripted interaction");
  Profile p;auto v=neutral();p.update(v,Context::Photo,100);
  v.hand[0].trigger=1;v.hand[0].grip=1;
  auto o=p.update(v,Context::Photo,110,3);
  check(o.pad.buttons==LB&&o.pad.lt==1&&!o.physicalAttacks&&!o.gestures,"left photo grip zooms out without interact or punch");
  v.hand[0].grip=0;v.hand[1].grip=1;v.hand[1].trigger=1;
  o=p.update(v,Context::Photo,120,3);
  check(o.pad.buttons==RB&&o.pad.lt==1&&o.pad.rt==1,"right photo grip zooms in preserving shutter and viewfinder");
  check(p.update(v,Context::Photo,130).pad.buttons==RB,"photo zoom hold reaches engine");
  v.hand[1].grip=0;check(p.update(v,Context::Photo,140).pad.buttons==0,"photo zoom releases");
  v.hand[1].grip=1;check(p.update(v,Context::Photo,150).pad.buttons==RB,"photo zoom can repeat");
  v=neutral();v.hand[0].y=1;v.hand[1].x=.6f;
  o=p.update(v,Context::Photo,160);check(o.motion&&o.pad.ly==1&&o.pad.rx==.6f,"photo preserves VR locomotion and turn route");
  v.focused=false;o=p.update(v,Context::Photo,170);check(o.pad.buttons==0&&o.pad.lt==0&&o.pad.rt==0,"photo focus loss releases inputs");
 }
 {
  Native n{true,false,true,false,false,0,16,0,0};
  check(context(n)==Context::Journal,"unobserved held journal uses safe navigation fallback");
  n.journal=2;check(context(n)==Context::Map,"native lowered journal selects movable Map");
  n.journal=1;n.projection=false;check(context(n)==Context::Journal,"raised Journal/Travels survives mono projection transition");
  n.journal=2;check(context(n)==Context::Map,"return to Map survives projection warmup");
  n.projection=true;
  n.menu=true;check(context(n)==Context::Menu,"pause ray retains priority over held map");
  n.menu=false;n.fresh=false;check(context(n)==Context::Unknown,"stale map cannot claim journal inputs");
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);
  v.hand[1].trigger=1;auto o=p.update(v,Context::Journal,1010,3);
  check(o.pad.buttons==0&&!o.motion&&!o.physicalAttacks,"enter journal blocks held trigger and combat");
  v=neutral();p.update(v,Context::Journal,1020);
  for(unsigned h=0;h<2;++h){
   v.hand[h].grip=1;o=p.update(v,Context::Journal,1030+h*100,3);
   check(o.pad.buttons==(h?RB:LB)&&o.pad.lt==0&&o.pad.rt==0,"journal grip becomes native tab bumper only");
   check(!o.motion&&!o.gestures&&!o.aim&&combatActions(o)==0,"journal does not apply gameplay motion or combat");
   check(p.update(v,Context::Journal,1040+h*100).pad.buttons==(h?RB:LB),"tab hold preserves native timing");
   v.hand[h].grip=0;check(p.update(v,Context::Journal,1050+h*100).pad.buttons==0,"tab release reaches native UI");
   v.hand[h].grip=1;check(p.update(v,Context::Journal,1060+h*100).pad.buttons==(h?RB:LB),"next tab press can repeat");
   v.hand[h].grip=0;p.update(v,Context::Journal,1070+h*100);
  }
  v.hand[1].lower=true;check(p.update(v,Context::Journal,1300).pad.buttons==A,"Quest A confirms journal selection");
  v=neutral();v.hand[1].upper=true;check(p.update(v,Context::Journal,1310).pad.buttons==B,"Quest B backs out of journal");
  v=neutral();v.hand[0].y=.6f;v.hand[1].y=-.7f;o=p.update(v,Context::Journal,1320);
  check(o.pad.buttons==Up&&o.pad.lx==0&&o.pad.ly==0&&o.pad.rx==0&&o.pad.ry==0&&!o.motion,"journal left stick navigates without movement or rotation");
  check(journalActions(o)==(uint64_t(1)<<7),"right stick down zooms out without look input");
  v.hand[1].y=.8f;o=p.update(v,Context::Journal,1321);check(journalActions(o)==(uint64_t(1)<<6),"right stick up zooms in");
  v.hand[1].y=0;o=p.update(v,Context::Journal,1322);check(journalActions(o)==0,"zoom release clears native action");
  v.hand[1].x=1;v.hand[0].upper=true;v.hand[0].grip=1;o=p.update(v,Context::Journal,1323);
  check(o.pad.rx==0&&o.pad.ry==0&&o.pad.lx==0&&o.pad.ly==0&&!o.motion,"compatibility chord cannot rotate modal journal body");
  v=neutral();p.update(v,Context::Journal,1324);
  v=neutral();v.hand[1].trigger=1;p.update(v,Context::Journal,1330);
  v.hand[1].grip=1;o=p.update(v,Context::Firearm,1340);
  check(o.pad.rt==0&&o.pad.buttons==0&&!o.aim,"closing journal cannot fire held trigger or grip");
  v=neutral();p.update(v,Context::Unarmed,1350);v.hand[0].y=1;
  check(p.update(v,Context::Unarmed,1360).motion,"normal motion restored after journal");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,100);
  v.hand[0].y=.8f;v.hand[1].x=.9f;v.hand[1].upper=true;
  auto o=p.update(v,Context::Map,110,3);
  check(o.motion&&o.pad.ly==.8f&&o.pad.rx==.9f&&o.pad.buttons==A,"Map retains VR walk turn and Quest B jump");
  check(!o.gestures&&combatActions(o)==0,"Map never enables physical combat");
  o=p.update(v,Context::Journal,120);
  check(!o.motion&&o.pad.buttons==0&&o.pad.rx==0&&o.pad.ly==0,"Map to modal journal releases held movement and buttons");
  v=neutral();p.update(v,Context::Journal,130);v.hand[0].y=-1;
  check(p.update(v,Context::Journal,140).pad.buttons==Down,"modal menu navigation after neutral");
  o=p.update(v,Context::Map,150);check(o.motion&&o.pad.ly==0,"menu selection cannot become movement on return to Map");
  v=neutral();p.update(v,Context::Map,160);v.hand[1].x=.8f;
  check(p.update(v,Context::Map,170).pad.rx==.8f,"Map turn restored after neutral");
  v.focused=false;o=p.update(v,Context::Map,180);check(!o.motion&&o.pad.rx==0,"focus loss releases Map turning");
 }
 for(auto ctx:{Context::Map,Context::Journal}){
  Profile p;auto v=neutral();p.update(v,ctx,100);
  v.hand[0].trigger=.7f;auto o=p.update(v,ctx,110);
  check(o.pad.lt==.7f&&o.pad.rt==0&&o.pad.buttons==0,"left trigger supplies native analog zoom trigger without use/combat");
  check(p.update(v,ctx,120).pad.lt==.7f,"zoom hold preserved");
  v.hand[0].grip=1;v.hand[1].grip=1;o=p.update(v,ctx,130);
  check(o.pad.lt==.7f&&o.pad.buttons==(LB|RB),"zoom trigger and tab grips remain independent");
  v.hand[0].trigger=0;check(p.update(v,ctx,140).pad.lt==0,"trigger release releases native zoom");
  v=neutral();v.hand[0].trigger=1;p.update(v,ctx,150);
  o=p.update(v,Context::Unarmed,160);check(o.pad.lt==0&&o.pad.buttons==0,"held zoom cannot interact after closing journal");
  v.focused=false;o=p.update(v,ctx,170);check(o.pad.lt==0,"focus loss releases zoom");
 }
 {
  Native n{true,false,true,false,false,14,1,5,0};check(context(n)==Context::Ledge,"ledge hang explicit context");
  Profile p;auto v=neutral();p.update(v,Context::Ledge,100);
  v.hand[0].x=.8f;v.hand[1].x=1;v.hand[1].y=1;auto o=p.update(v,Context::Ledge,110,3);
  check(o.pad.lx==.8f&&o.pad.rx==0&&o.pad.ry==0&&!o.motion&&!o.gestures&&combatActions(o)==0,"native shimmy retained without artificial look or combat");
  v.hand[1].lower=true;check(p.update(v,Context::Ledge,120).pad.buttons&B,"ledge Quest A releases ledge");
  v.hand[1].lower=false;v.hand[1].upper=true;check(p.update(v,Context::Ledge,130).pad.buttons&A,"ledge Quest B pulls up");
  v.hand[1].upper=false;check(!(p.update(v,Context::Ledge,140).pad.buttons&(A|B)),"ledge buttons release");
  v=neutral();p.update(v,Context::Normal,130);v.hand[1].x=.8f;
  check(p.update(v,Context::Normal,140).motion,"normal VR motion restored after hang");
 }
 {
  Native n{true,false,false,false,false,13,18,7,0};
  check(context(n)==Context::WhipClimb,"whip climbing identified before projection activation");
  Profile p;auto v=neutral();p.update(v,Context::WhipClimb,100);
  v.hand[0].x=.7f;v.hand[0].y=1;v.hand[1].x=1;v.hand[1].y=-1;
  for(unsigned t=110;t<510;t+=20){auto o=p.update(v,Context::WhipClimb,t,3);
   check(o.pad.lx==.7f&&o.pad.ly==1&&o.pad.rx==0&&o.pad.ry==0&&!o.motion&&!o.gestures&&!combatActions(o),"rope axes held without artificial turn or physical combat");}
  v.hand[0].y=-1;check(p.update(v,Context::WhipClimb,520).pad.ly==-1,"rope descent retained");
  v.hand[1].lower=true;check(p.update(v,Context::WhipClimb,530).pad.buttons&B,"Quest A releases rope");
  v.hand[1].lower=false;v.hand[1].upper=true;check(p.update(v,Context::WhipClimb,540).pad.buttons&A,"Quest B jumps from rope");
  v=neutral();auto o=p.update(v,Context::WhipClimb,550);check(!o.pad.buttons&&o.pad.ly==0,"rope axes and buttons release");
  v.focused=false;o=p.update(v,Context::WhipClimb,560);check(!o.pad.buttons&&o.pad.ly==0,"rope focus loss neutralizes input");
  v=neutral();p.update(v,Context::Normal,570);v.hand[1].x=.8f;check(p.update(v,Context::Normal,580).motion,"VR controls restored on rope exit");
 }
 {
  Native n{true,false,false,false,false,4,1,5,0};
  check(context(n)==Context::Ladder,"ladder climbing identified before projection activation");
  Profile p;auto v=neutral();p.update(v,Context::Ladder,100);
  v.hand[0].x=.7f;v.hand[0].y=1;v.hand[1].x=1;v.hand[1].y=-1;
  for(unsigned t=110;t<510;t+=20){auto o=p.update(v,Context::Ladder,t,3);
   check(o.pad.lx==.7f&&o.pad.ly==1&&o.pad.rx==0&&o.pad.ry==0&&!o.motion&&!o.gestures&&!combatActions(o),"ladder axes held without artificial turn or physical combat");}
  v.hand[0].y=-1;check(p.update(v,Context::Ladder,520).pad.ly==-1,"ladder descent retained");
  v.hand[1].lower=true;check(p.update(v,Context::Ladder,530).pad.buttons&B,"Quest A releases ladder");
  v.hand[1].lower=false;v.hand[1].upper=true;check(p.update(v,Context::Ladder,540).pad.buttons&A,"Quest B jumps from ladder");
  v=neutral();auto o=p.update(v,Context::Ladder,550);check(!o.pad.buttons&&o.pad.ly==0,"ladder axes and buttons release");
  v.focused=false;o=p.update(v,Context::Ladder,560);check(!o.pad.buttons&&o.pad.ly==0,"ladder focus loss neutralizes input");
  v=neutral();p.update(v,Context::Normal,570);v.hand[1].x=.8f;check(p.update(v,Context::Normal,580).motion,"VR controls restored on ladder exit");
 }
 {
  Native n{true,false,false,false,false,16,1,5,0};
  check(context(n)==Context::RopeSlide,"rope slide climbing identified before projection activation");
  Profile p;auto v=neutral();p.update(v,Context::RopeSlide,100);
  v.hand[0].x=.7f;v.hand[0].y=1;v.hand[1].x=1;v.hand[1].y=-1;
  for(unsigned t=110;t<510;t+=20){auto o=p.update(v,Context::RopeSlide,t,3);
   check(o.pad.lx==.7f&&o.pad.ly==1&&o.pad.rx==0&&o.pad.ry==0&&!o.motion&&!o.gestures&&!combatActions(o),"rope slide axes held without artificial turn or physical combat");}
  v.hand[0].y=-1;check(p.update(v,Context::RopeSlide,520).pad.ly==-1,"rope slide descent retained");
  v.hand[1].lower=true;check(p.update(v,Context::RopeSlide,530).pad.buttons&B,"Quest A releases rope slide");
  v.hand[1].lower=false;v.hand[1].upper=true;check(p.update(v,Context::RopeSlide,540).pad.buttons&A,"Quest B jumps from rope slide");
  v=neutral();auto o=p.update(v,Context::RopeSlide,550);check(!o.pad.buttons&&o.pad.ly==0,"rope slide axes and buttons release");
  v.focused=false;o=p.update(v,Context::RopeSlide,560);check(!o.pad.buttons&&o.pad.ly==0,"rope slide focus loss neutralizes input");
  v=neutral();p.update(v,Context::Normal,570);v.hand[1].x=.8f;check(p.update(v,Context::Normal,580).motion,"VR controls restored on rope slide exit");
 }
 {
  Native n{true,false,false,false,false,12,18,7,0};
  check(context(n)==Context::WhipSwing,"whip swinging identified before projection activation");
  Profile p;auto v=neutral();p.update(v,Context::WhipSwing,100);
  v.hand[0].x=.7f;v.hand[0].y=1;v.hand[1].x=1;v.hand[1].y=-1;
  for(unsigned t=110;t<510;t+=20){auto o=p.update(v,Context::WhipSwing,t,3);
   check(o.pad.lx==.7f&&o.pad.ly==1&&o.pad.rx==0&&o.pad.ry==0&&!o.motion&&!o.gestures&&!combatActions(o),"swing axes held without artificial turn or physical combat");}
  v.hand[0].y=-1;check(p.update(v,Context::WhipSwing,520).pad.ly==-1,"swing descent retained");
  v.hand[1].lower=true;check(p.update(v,Context::WhipSwing,530).pad.buttons&B,"Quest A releases swing");
  v.hand[1].lower=false;v.hand[1].upper=true;check(p.update(v,Context::WhipSwing,540).pad.buttons&A,"Quest B jumps from swing");
  v=neutral();auto o=p.update(v,Context::WhipSwing,550);check(!o.pad.buttons&&o.pad.ly==0,"swing axes and buttons release");
  v.focused=false;o=p.update(v,Context::WhipSwing,560);check(!o.pad.buttons&&o.pad.ly==0,"swing focus loss neutralizes input");
  v=neutral();p.update(v,Context::Normal,570);v.hand[1].x=.8f;check(p.update(v,Context::Normal,580).motion,"VR controls restored on swing exit");
 }
 for(unsigned mode:{7u,8u}){
  Native n{true,false,true,false,false,mode,1,0,0};
  check(context(n)==Context::Swimming,"surface and underwater retain swimming input routing");
  n.animated=true;check(context(n)==Context::Cinematic,"scripted water animation keeps native ownership");n.animated=false;
  n.type=5;check(context(n)==Context::Unknown,"nonordinary water physics retains fallback");n.type=0;
  n.menu=true;check(context(n)==Context::Menu,"pause overrides swimming");
 }
 {
  // r307 Himalaya trace repeatedly alternates WALK0 / WADE20 while holding
  // slot17 camera, slot16 journal/map and slot3 gun. Movement cannot replace
  // their action layout. Replay the same transitions with held input.
  for(unsigned slot:{1u,3u,6u,10u,16u,17u,18u,20u,23u}){
   Native n{true,false,true,false,false,0,slot,0,0};
   if(slot==16)n.journal=2;
   const auto expected=context(n);n.mode=20;
   check(context(n)==expected,"wade keeps equipped item/fist context");
   Profile p;auto v=neutral();p.update(v,expected,1000);
   v.hand[0].y=.8f;
   for(unsigned i=0;i<20;++i){n.mode=i%2?0:20;auto o=p.update(v,context(n),1010+i*10);
    check(o.context==expected,"walk/wade cannot change item context");
    check(o.pad.ly==.8f&&o.motion,"walk/wade retains movement without neutral reset");
    check(nativeWaterMotion(n,o)==(n.mode==20),"water basis follows posture independently of equipment");
   }
  }
  Native n{true,false,true,false,false,20,3,0,0};
  Profile p;auto v=neutral();p.update(v,context(n),1000);
  v.hand[1].click=true;p.update(v,context(n),1010);
  auto o=p.update(v,context(n),1080);
  check(o.holster&&(o.pad.buttons&Y),"wading firearm can be holstered");
  n.slot=16;n.journal=1;n.projection=false;
  check(context(n)==Context::Journal,"raised journal in wade survives disabled projection");
  Profile book;v=neutral();book.update(v,context(n),2000);
  v.hand[0].grip=1;o=book.update(v,context(n),2010);
  check(o.pad.buttons==LB&&!o.motion&&!nativeWaterMotion(n,o),"wade journal previous page without locomotion");
  v.hand[0].grip=0;v.hand[1].grip=1;o=book.update(v,context(n),2020);
  check(o.pad.buttons==RB,"wade journal next page");
  n.journal=2;n.projection=true;check(context(n)==Context::Map,"wade map remains identifiable");
  Profile map;v=neutral();map.update(v,context(n),2100);v.hand[1].grip=1;v.hand[0].y=.8f;
  o=map.update(v,context(n),2110);
  check((o.pad.buttons&RB)&&o.motion&&nativeWaterMotion(n,o),"wade map keeps tabs and native movement basis");
  n.slot=1;n.journal=0;
  for(unsigned h=0;h<2;++h){
   Profile fists;v=neutral();fists.update(v,context(n),3000);
   v.hand[h].grip=1;v.hand[h].thumbRest=true;fists.update(v,context(n),3010);
   v.hand[h].trigger=1;o=fists.update(v,context(n),3020);
   check(o.gestures&&o.fingerGesture[h]==1,"wade permits middle-finger gesture with empty hands");
   n.mode=0;o=fists.update(v,context(n),3030);n.mode=20;o=fists.update(v,context(n),3040);
   check(o.fingerGesture[h]==1,"walk/wade does not reset held cosmetic gesture");
   v.focused=false;o=fists.update(v,context(n),3050);
   check(o.fingerGesture[h]==0&&!nativeWaterMotion(n,o),"focus loss clears gesture and water input");
  }
  n.animated=true;check(context(n)==Context::Cinematic,"scripted wade keeps authored input");n.animated=false;
  n.type=5;check(context(n)==Context::Unknown,"nonordinary wade keeps fallback");n.type=0;
  n.projection=false;check(context(n)==Context::Unknown,"wade equipment needs projection");n.projection=true;
  n.menu=true;check(context(n)==Context::Menu,"wade menu keeps priority");n.menu=false;
  n.fresh=false;check(context(n)==Context::Unknown,"stale wade cannot enable equipment");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);
  v.hand[0].y=1;v.hand[1].x=.8f;v.hand[1].y=-1;
  p.update(v,Context::Unarmed,1010);
  auto o=p.update(v,Context::Swimming,1020,3);
  check(o.motion&&o.pad.ly==1&&o.pad.rx==.8f&&o.pad.ry==0,"enter water with held movement; right stick cannot pitch camera");
  check(!o.gestures&&!o.physicalAttacks&&!o.aim,"swimming does not synthesize combat");
  v.hand[1].upper=true;o=p.update(v,Context::Swimming,1030);check(o.pad.buttons&A,"Quest B retains native jump/ascend");
  v.hand[1].upper=false;v.hand[1].lower=true;o=p.update(v,Context::Swimming,1040);check(o.pad.buttons&B,"Quest A retains native crouch/descend");
  v.hand[1].lower=false;o=p.update(v,Context::Unarmed,1050);check(o.motion&&o.pad.ly==1,"leaving water restores locomotion without releasing left stick");
  o=p.update(v,Context::Menu,1060);check(!o.motion&&o.pad.ly==0,"pause clears water motion");
  o=p.update(v,Context::Swimming,1070);check(o.pad.ly==0,"held movement cannot replay after menu");
  v=neutral();p.update(v,Context::Swimming,1080);v.hand[0].y=1;
  check(p.update(v,Context::Swimming,1090).pad.ly==1,"fresh movement resumes after menu");
  v.focused=false;check(p.update(v,Context::Swimming,1100).pad.ly==0,"focus loss releases swimming");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);
  v.hand[0].grip=1;auto o=p.update(v,Context::Unarmed,1010);check(!(o.pad.buttons&LB),"one fist does not block");
  v.hand[1].grip=1;p.update(v,Context::Unarmed,1200);o=p.update(v,Context::Unarmed,1350);check(o.pad.buttons&LB,"two fists hold native block after interact pulse");
  o=p.update(v,Context::Unarmed,1400,1);check(o.physicalAttacks==1&&!(o.pad.buttons&LB),"physical strike takes priority over guard");
  o=p.update(v,Context::Unarmed,1600);check(o.pad.buttons&LB,"guard resumes after strike");
  check((combatActions(o)&2),"held fists rearm native guard after consumed attack edge");
  check(combatActions(o)==2,"guard sets only native held block, no attack or synthetic edge");
  for(uint64_t t:{1700u,1800u,1900u})check((combatActions(p.update(v,Context::Unarmed,t))&2),"held guard survives native recovery longer than input pulse");
  auto attack=p.update(v,Context::Unarmed,2000,2);
  check(!(combatActions(attack)&2),"rearming cannot swallow physical attack");
  check((combatActions(p.update(v,Context::Unarmed,2200))&2),"second strike returns to guard without grip release");
  auto gated=o;gated.context=Context::Grip;check(!heldGuard(gated),"QTE cannot rearm combat block");
  gated=o;gated.modifier=true;check(!heldGuard(gated),"modifier block retains native timing");
  gated=o;gated.pause=true;check(!heldGuard(gated),"pause cannot rearm block");
  v.hand[0].grip=0;check(!(p.update(v,Context::Unarmed,2300).pad.buttons&LB),"opening either fist releases guard");
  v.hand[0].grip=1;v.focused=false;check(!(p.update(v,Context::Unarmed,2400).pad.buttons&LB),"focus loss releases guard");
 }
 {
  Profile p;auto v=neutral();v.actor=7;p.update(v,Context::Unarmed,1000);
  v.hand[0].grip=v.hand[1].grip=1;p.update(v,Context::Unarmed,1010);
  check(heldGuard(p.update(v,Context::Unarmed,1200)),"guard established before hit");
  for(unsigned cycle=0;cycle<3;++cycle){
   uint64_t t=1300+cycle*1500;
   auto o=p.update(v,Context::Unarmed,t,cycle==0?0:cycle==1?1:2);
   if(cycle)check(!heldGuard(o)&&o.physicalAttacks,"strike retains priority while grips stay held");
   else check(heldGuard(o),"opponent hit starts while guard is held, without own strike");
   o=p.update(v,Context::Unknown,t+200);
   check(!heldGuard(o)&&!o.physicalAttacks,"hit transition emits no combat actions");
   p.update(v,Context::Unknown,t+900);
   o=p.update(v,Context::Unarmed,t+1000);
   check(heldGuard(o)&&(combatActions(o)&2)&&!(o.pad.buttons&X),"hit recovery resumes guard without grip edges or interaction");
  }
  p.update(v,Context::Unknown,6000);v.hand[0].grip=0;p.update(v,Context::Unknown,6100);
  v.hand[0].grip=1;check(!heldGuard(p.update(v,Context::Unarmed,6200)),"regrip during unknown does not create retained guard");
 }
 for(auto interruption:{Context::Menu,Context::Grip,Context::Cinematic,Context::Firearm}){
  Profile p;auto v=neutral();v.actor=7;p.update(v,Context::Unarmed,1000);
  v.hand[0].grip=v.hand[1].grip=1;p.update(v,Context::Unarmed,1010);p.update(v,Context::Unarmed,1200);
  p.update(v,Context::Unknown,1300);p.update(v,interruption,1400);p.update(v,Context::Unknown,1500);
  check(!heldGuard(p.update(v,Context::Unarmed,1600)),"menu script weapon invalidates guard continuity");
 }
 for(unsigned loss=0;loss<4;++loss){
  Profile p;auto v=neutral();v.actor=7;p.update(v,Context::Unarmed,1000);
  v.hand[0].grip=v.hand[1].grip=1;p.update(v,Context::Unarmed,1010);p.update(v,Context::Unarmed,1200);
  p.update(v,Context::Unknown,1300);
  if(loss==0)v.focused=false;if(loss==1)v.hand[0].tracked=false;if(loss==2)v.actor=8;if(loss==3)++v.session;
  p.update(v,Context::Unknown,1400);v.focused=true;v.hand[0].tracked=true;
  check(!heldGuard(p.update(v,Context::Unarmed,1500)),"focus tracking owner session loss invalidates guard continuity");
 }
 for(unsigned h=0;h<2;++h){
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);
  auto& hand=v.hand[h];hand.grip=1;hand.thumbRest=true;p.update(v,Context::Unarmed,1010);
  auto o=p.update(v,Context::Unarmed,1020,1u<<h);
  check(o.physicalAttacks==(1u<<h),"resting thumb without trigger does not block normal punch");
  hand.trigger=.5f;o=p.update(v,Context::Unarmed,1030,1u<<h);
  check(o.fingerGesture[h]==.5f&&o.fingerGesture[1-h]==0&&o.physicalAttacks==(1u<<h),"same-hand thumb-rest gesture preserves physical attack");
  hand.trigger=1;o=p.update(v,Context::Unarmed,1040,3);
  check(o.fingerGesture[h]==1&&o.physicalAttacks==3,"both hands can strike with cosmetic gesture");
  hand.thumbRest=false;o=p.update(v,Context::Unarmed,1050,1u<<h);
  check(o.fingerGesture[h]==0&&o.physicalAttacks==(1u<<h),"lifting thumb does not consume a physical punch");
  hand.trigger=0;p.update(v,Context::Unarmed,1060);o=p.update(v,Context::Unarmed,1070,1u<<h);
  check(o.physicalAttacks==(1u<<h),"neutral trigger restores normal punches");
  hand.thumbRest=true;hand.trigger=1;o=p.update(v,Context::Unarmed,1080);
  check(o.fingerGesture[h]==0,"already held trigger cannot enter gesture");
  hand.trigger=0;p.update(v,Context::Unarmed,1090);hand.trigger=1;o=p.update(v,Context::Unarmed,1100);
  check(o.fingerGesture[h]==1,"neutral trigger arms gesture again");
  v.focused=false;o=p.update(v,Context::Unarmed,1110);check(o.fingerGesture[h]==0,"focus loss clears pose");
  v.focused=true;o=p.update(v,Context::Unarmed,1120,3);check(o.fingerGesture[h]==0,"focus return cannot reuse held gesture");
 }
 for(auto ctx:{Context::Firearm,Context::Melee,Context::Carrying,Context::Whip,Context::Grip,Context::Traversal,Context::Menu,Context::Inspection}){
  Profile a,b;auto plain=neutral();a.update(plain,ctx,1000);b.update(plain,ctx,1000);
  plain.hand[0].grip=plain.hand[1].grip=1;auto touched=plain;touched.hand[0].thumbRest=touched.hand[1].thumbRest=true;
  a.update(plain,ctx,1010);b.update(touched,ctx,1010);
  plain.hand[0].trigger=touched.hand[0].trigger=.7f;plain.hand[1].trigger=touched.hand[1].trigger=.8f;
  auto x=a.update(plain,ctx,1020),y=b.update(touched,ctx,1020);
  check(y.fingerGesture[0]==0&&y.fingerGesture[1]==0&&x.pad.buttons==y.pad.buttons&&x.pad.lt==y.pad.lt&&x.pad.rt==y.pad.rt&&x.aim==y.aim,"touch leaves weapons scripts and menu routing unchanged");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);v.hand[1].grip=1;v.hand[1].thumbRest=true;p.update(v,Context::Unarmed,1010);v.hand[1].trigger=1;p.update(v,Context::Unarmed,1020);
  v.hand[1].click=true;auto o=p.update(v,Context::Unarmed,1030);check(o.fingerGesture[1]==0,"stick click cancels cosmetic pose");
  v.hand[1].click=false;o=p.update(v,Context::Unarmed,1040);check(o.holster&&o.fingerGesture[1]==0,"thumb rest cannot steal holster click");
 }
 for(auto ctx:{Context::Firearm,Context::Melee,Context::Carrying,Context::Whip,Context::Unarmed}){
  Profile p;auto v=neutral();p.update(v,ctx,1000);v.hand[1].click=true;
  check(!p.update(v,ctx,1010).holster,"right click waits for pause chord arbitration");
  v.hand[1].click=false;auto o=p.update(v,ctx,1040);
  check(o.holster&&o.pad.buttons==Y&&!o.aim&&combatActions(o)==0,"short right click selects fists without melee or aim");
  check(!p.update(v,ctx,1241).holster,"holster pulse releases");
  v.hand[1].click=true;p.update(v,ctx,1300);
  check(p.update(v,ctx,1421).holster,"held right click holsters once after chord window");
  check(!p.update(v,ctx,1622).holster,"held click never repeats holster");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Firearm,1000);
  v.hand[1].click=true;p.update(v,Context::Firearm,1010);v.hand[0].click=true;
  auto o=p.update(v,Context::Firearm,1050);check(o.pause&&!o.holster&&o.pad.buttons==Start,"both clicks pause without holstering");
 }
 for(auto ctx:{Context::Unarmed,Context::Melee,Context::Carrying})for(unsigned h=0;h<2;++h){
  Profile p;auto v=neutral();p.update(v,ctx,1000);v.hand[h].grip=1;v.hand[h].trigger=1;
  for(unsigned t=1010;t<2010;t+=10){auto o=p.update(v,ctx,t);check(o.pad.lt==0&&o.pad.rt==0&&combatActions(o)==0,"holding charge never leaks native attack/throw/aim");}
  const unsigned releaseDecision=0; // trigger-held swings cannot throw
  auto o=p.update(v,ctx,2020,1u<<h);o=p.update(v,ctx,2100+releaseDecision,1u<<h);
  check(combatActions(o)==(uint64_t(1)<<(h?0:15)),"velocity routes only the corresponding native hand action");
  o=p.update(v,ctx,2360+releaseDecision);check(combatActions(o)==0,"attack ends while trigger remains held");
  v.hand[h].trigger=0;o=p.update(v,ctx,2370+releaseDecision);check(combatActions(o)==0&&o.pad.lt==0&&o.pad.rt==0,"trigger release cannot execute or repeat charged item attack");
 }
 for(auto ctx:{Context::Grip,Context::Traversal,Context::Unknown,Context::Inspection}){
  Profile p;auto v=neutral();p.update(v,ctx,1000);v.hand[1].click=true;v.hand[0].trigger=.6f;v.hand[1].trigger=.8f;
  auto o=p.update(v,ctx,1200,3);check(!o.holster&&combatActions(o)==0&&o.pad.lt==.6f&&o.pad.rt==.8f,"scripts keep native trigger semantics and no gameplay holster");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Firearm,1000);
  v.hand[0].trigger=1;p.update(v,Context::Firearm,1005);v.hand[0].trigger=0;auto o=p.update(v,Context::Firearm,1010);check(o.pad.lt==0&&o.pad.rt==0,"selected shooting mode remains idle without right trigger");
  v.hand[0].trigger=0;v.hand[1].grip=.9f;o=p.update(v,Context::Firearm,1020);check(o.aim&&o.pad.lt==0&&o.pad.rt==0&&!(o.pad.buttons&X),"right grip aims without interact or fire");
  v.hand[1].trigger=.6f;o=p.update(v,Context::Firearm,1030);check(o.aim&&o.pad.lt==0&&o.pad.rt==.6f,"right trigger fires while grip aims");
  v.hand[1].grip=0;o=p.update(v,Context::Firearm,1040);check(o.pad.lt==0&&o.pad.rt==0,"grip release cannot become native melee");
  v.hand[1].grip=1;o=p.update(v,Context::Firearm,1050);check(o.pad.rt==0,"regrip with trigger held cannot auto fire");
 }
 for(auto ctx:{Context::Firearm,Context::Melee,Context::Carrying}){
  Profile p;auto v=neutral();p.update(v,ctx,1000);v.hand[1].grip=1;p.update(v,ctx,1010);
  auto o=p.update(v,ctx,1020,2);check(o.pad.lt==0&&o.pad.rt==0,"item gesture first leaves native aim/throw");
  o=p.update(v,ctx,1100,2);check(!o.aim&&o.pad.lt==0&&o.pad.rt==0&&o.physicalAttacks==2,"item gesture pulses native attack without aim");
  o=p.update(v,ctx,1300);check(o.pad.rt==0,"item strike releases once");
  o=p.update(v,Context::Grip,1310);check(o.pad.rt==0&&o.physicalAttacks==0,"QTE transition cancels queued item attack");
 }
 for(unsigned cancel=0;cancel<5;++cancel){
  Profile p;auto v=neutral();p.update(v,Context::Firearm,1000);v.hand[1].grip=1;
  p.update(v,Context::Firearm,1010,2);p.update(v,Context::Firearm,1100,2);
  p.update(v,Context::Firearm,1350);
  p.update(v,Context::Firearm,1400,2);
  check(p.update(v,Context::Firearm,1480,2).physicalAttacks==2,"fresh second item gesture attacks again");
  if(cancel==0)v.hand[1].grip=0;
  if(cancel==1)v.hand[1].tracked=false;
  if(cancel==2)v.focused=false;
  if(cancel==3)v.hand[0].menu=true;
  if(cancel==4)v.session=2;
  check(p.update(v,Context::Firearm,1490,2).physicalAttacks==0,"item attack releases on grip/tracking/focus/menu/session loss");
 }
 for(unsigned hand=0;hand<2;++hand){
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);
  v.hand[hand].grip=1;v.hand[hand].trigger=1;
  for(uint64_t t=1010;t<2010;t+=10){auto o=p.update(v,Context::Unarmed,t);check(o.pad.lt==0&&o.pad.rt==0,"grip+trigger hold sends no native attack");}
  auto o=p.update(v,Context::Unarmed,2020,1u<<hand);
  check(o.physicalAttacks==(1u<<hand)&&o.pad.lt==0&&o.pad.rt==0,"charged gesture uses correct hand despite held trigger");
  o=p.update(v,Context::Unarmed,2120);check(o.pad.rt==0&&o.pad.lt==0,"physical punch pulse releases even with trigger still held");
  v.hand[hand].trigger=0;o=p.update(v,Context::Unarmed,2130);check(o.pad.rt==0&&o.pad.lt==0,"trigger release without gesture has no attack");
  v.hand[hand].trigger=1;p.update(v,Context::Unarmed,2140);
  o=p.update(v,Context::Firearm,2150);check(o.pad.rt==0,"weapon switch does not turn held charge into gunfire");
  v.hand[hand].trigger=0;p.update(v,Context::Firearm,2160);v.hand[hand].trigger=1;
  o=p.update(v,Context::Firearm,2170);check(o.firearmMelee==(hand==1)&&o.pad.rt==0&&o.pad.lt==0,"only a fresh left trigger changes default melee; neither hand leaks a shot");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Firearm,1000);
  v.hand[1].grip=1;p.update(v,Context::Firearm,1010);
  v.hand[0].trigger=1;p.update(v,Context::Firearm,1012);v.hand[0].trigger=0;p.update(v,Context::Firearm,1014);
  v.hand[1].trigger=1;
  auto o=p.update(v,Context::Firearm,1020);
  check(!o.aim&&o.pad.rt==0&&o.pad.lt==0&&!o.physicalAttacks,"firearm melee charge alone neither fires nor strikes");
  p.update(v,Context::Firearm,1400,2);
  o=p.update(v,Context::Firearm,1480,2);
  check(o.physicalAttacks==2&&!o.aim&&o.pad.rt==0,"held triggers permit physical weapon strike without shooting");
  o=p.update(v,Context::Firearm,1490,0);
  check(o.physicalAttacks==0&&o.pad.rt==0,"accepted physical release ends latched item pulse despite held charge trigger");
  p.update(v,Context::Firearm,1800);
  v.hand[0].trigger=0;o=p.update(v,Context::Firearm,1810);
  check(o.firearmMelee&&!o.aim,"left trigger release retains selected melee mode");
  v.hand[0].trigger=1;o=p.update(v,Context::Firearm,1815);
  check(!o.firearmMelee,"next left trigger press switches back to shooting");
  check(o.pad.rt==0,"exiting melee with trigger held cannot fire");
  v.hand[1].trigger=0;p.update(v,Context::Firearm,1820);
  v.hand[1].trigger=1;o=p.update(v,Context::Firearm,1830);
  check(o.aim&&o.pad.rt==1,"fresh right trigger restores ordinary firearm shot");
 }
 uint16_t reached{};unsigned axes{};
 for(auto script:{Context::Climbing,Context::Traversal,Context::Grip,Context::Cinematic,Context::Unknown}){
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);
  v.hand[0].x=.25f;v.hand[0].y=1;v.hand[1].x=.8f;v.hand[1].upper=true;
  p.update(v,Context::Unarmed,1010);
  auto o=p.update(v,script,1020);
  check(o.pad.ly==1&&o.pad.lx==.25f&&!o.motion&&!o.gestures,"held movement enters native traversal without suppressing QTE direction");
  check(!o.pad.buttons&&o.pad.rx==0,"buttons and turn stick remain neutral-gated at traversal entry");
  o=p.update(v,Context::Unarmed,1200);
  check(o.motion&&o.pad.ly==1&&o.pad.lx==.25f,"held movement resumes immediately on landing");
  check(!o.pad.buttons&&o.pad.rx==0,"landing does not retrigger jump or turn");
  v.focused=false;p.update(v,script,1210);v.focused=true;p.update(v,script,1220);
  check(p.update(v,Context::Unarmed,1230).pad.ly==0,"traversal cannot bypass focus neutral lock");
  v.hand[0].x=v.hand[0].y=0;p.update(v,Context::Unarmed,1240);v.hand[0].y=1;
  check(p.update(v,Context::Unarmed,1250).pad.ly==1,"fresh movement after focus neutral");
  p.update(v,Context::Menu,1260);check(p.update(v,Context::Unarmed,1270).pad.ly==0,"menu exit still requires movement neutral");
 }
 // Native beam attachment must not silently swap jump/crouch. Native
 // ownership and the explicit Xbox compatibility page remain intact.
 {
  Native n;n.fresh=true;n.mode=0;n.type=7;n.slot=18;
  check(context(n)==Context::WhipAttached,"whip physics recognized even without VR locomotion");
  n.menu=true;check(context(n)==Context::Menu,"menu overrides attachment");n.menu=false;
  n.mode=10;check(context(n)==Context::Grip,"GRIP prompt retains its native page");
  n.mode=200;check(context(n)==Context::Unknown,"unknown posture keeps Xbox fallback");
  Profile p;auto v=neutral();p.update(v,Context::Whip,1000);v.hand[0].y=1;v.hand[1].upper=true;p.update(v,Context::Whip,1010);
  auto o=p.update(v,Context::WhipAttached,1020,3);
  check(!o.motion&&!o.gestures&&o.pad.buttons==0&&o.pad.ly==1,"attachment neutral gates held jump, retains native direction");
  v=neutral();p.update(v,Context::WhipAttached,1030);v.hand[1].upper=true;
  for(unsigned t=1040;t<1400;t+=20)check(p.update(v,Context::WhipAttached,t).pad.buttons==A,"Quest B holds native jump while attached");
  v.hand[1].upper=false;check(!p.update(v,Context::WhipAttached,1410).pad.buttons,"jump release forwarded");
  v.hand[1].lower=true;check(p.update(v,Context::WhipAttached,1420).pad.buttons==B,"Quest A crouches/releases while attached");
  v.hand[0].upper=true;v.hand[0].grip=1;check(p.update(v,Context::WhipAttached,1430).pad.buttons==A,"explicit native A/B compatibility preserved");
  v=neutral();p.update(v,Context::Unarmed,1440);v.hand[1].upper=true;
  check(p.update(v,Context::Unarmed,1450).pad.buttons==A,"same Quest B jump after detachment");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Whip,1000);
  v.rightWhip=true;v.whipAttached=true;v.hand[1].grip=1;p.update(v,Context::Whip,1020);
  v.hand[1].trigger=1;auto o=p.update(v,Context::Whip,1040);
  check(o.pad.buttons==(LB|RB)&&o.pad.rt==0,"attached object yank works with right grip held in ordinary whip context");
  v.hand[1].trigger=0;check(!p.update(v,Context::Whip,1060).pad.buttons,"grip holds whip while trigger releases yank");
  v.whipAttached=false;v.hand[1].trigger=1;o=p.update(v,Context::Whip,1080);
  check(!(o.pad.buttons&(LB|RB)),"free owned whip does not yank");
 }
 {
  Native n;n.fresh=true;n.slot=13;n.mode=0;n.type=0;n.projection=true;
  check(context(n)==Context::Inspection,"ability book leaves journal but remains inspection");
  n.projection=false;check(context(n)==Context::Inspection,"book classification survives flat presentation");
  n.menu=true;check(context(n)==Context::Menu,"pause menu retains priority over book");
  n.menu=false;n.fresh=false;check(context(n)==Context::Unknown,"stale book equipment cannot force inspection");
  n.fresh=true;n.slot=1;n.projection=true;check(context(n)==Context::Unarmed,"closing book restores gameplay context");
 }
 // Enumerate actual physical combinations, not a claimed mapping table.
 {
  Profile p;auto v=neutral();p.update(v,Context::WhipAttached,1000);
  v.hand[1].trigger=1;
  for(unsigned t=1020;t<1120;t+=20){auto o=p.update(v,Context::WhipAttached,t);
   check(o.pad.buttons==(LB|RB)&&o.pad.rt==0&&!o.gestures,"attached right trigger holds native yank chord without attack");}
  v.hand[1].trigger=0;check(!p.update(v,Context::WhipAttached,1140).pad.buttons,"yank chord releases with trigger");
  v.hand[1].trigger=1;check(p.update(v,Context::WhipAttached,1160).pad.buttons==(LB|RB),"yank can repeat after release");
  auto o=p.update(v,Context::Unarmed,1180);check(!(o.pad.buttons&(LB|RB)),"detachment releases yank buttons");
  v=neutral();p.update(v,Context::WhipClimb,1200);v.hand[1].trigger=1;
  o=p.update(v,Context::WhipClimb,1220);check(!(o.pad.buttons&(LB|RB)),"climb trigger does not become yank chord");
 }
 // Enumerate actual physical combinations, not a claimed mapping table.
 for(unsigned bits=0;bits<256;++bits){Profile profile;auto v=neutral();profile.update(v,Context::Unknown,1000);
  v.hand[0].lower=bits&1;v.hand[0].upper=bits&2;v.hand[0].click=bits&4;v.hand[1].lower=bits&8;v.hand[1].upper=bits&16;v.hand[1].click=bits&32;v.hand[0].trigger=bits&64?1.f:0;v.hand[1].trigger=bits&128?1.f:0;
  reached|=profile.update(v,Context::Unknown,1010).pad.buttons;
  auto o=profile.update(v,Context::Unknown,1300);reached|=o.pad.buttons;if(o.pad.lt==1)axes|=256;if(o.pad.rt==1)axes|=512;
  v=neutral();reached|=profile.update(v,Context::Unknown,1350).pad.buttons;
 }
 for(bool modifier:{false,true})for(unsigned stick=0;stick<2;++stick)for(unsigned dimension=0;dimension<2;++dimension)for(float direction:{-1.f,1.f}){
  Profile profile;auto v=neutral();profile.update(v,Context::Unknown,1000);v.hand[0].upper=modifier;
  (dimension?v.hand[stick].y:v.hand[stick].x)=direction;auto o=profile.update(v,Context::Unknown,1010);reached|=o.pad.buttons;
  float value=stick?(dimension?o.pad.ry:o.pad.rx):(dimension?o.pad.ly:o.pad.lx);
  if(value==direction)axes|=1u<<(stick*4+dimension*2+(direction>0));
 }
 {Profile p;auto v=neutral();p.update(v,Context::Unknown,1000);v.hand[0].upper=true;p.update(v,Context::Unknown,1010);v.hand[0].upper=false;reached|=p.update(v,Context::Unknown,1100).pad.buttons;}
 for(auto b:{Up,Down,Left,Right,Start,Back,LS,RS,LB,RB,A,B,X,Y})check((reached&b)!=0,"all fourteen Xbox buttons reachable from profile");
 check(axes==1023,"all eight signed stick axes and both analog triggers reachable");
 {
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);v.hand[1].upper=true;check(p.update(v,Context::Unarmed,1010).pad.buttons==A,"Quest B jumps");v.hand[1].upper=false;v.hand[1].lower=true;check(p.update(v,Context::Unarmed,1020).pad.buttons==B,"Quest A crouches");
  v=neutral();v.hand[0].upper=true;check(p.update(v,Context::Unarmed,1100).pad.buttons==0,"Y pending has no native action");v.hand[0].upper=false;check(p.update(v,Context::Unarmed,1200).pad.buttons==Back,"short Y opens inventory");check(p.update(v,Context::Unarmed,1300).pad.buttons==0,"tap releases");
  v.hand[0].upper=true;p.update(v,Context::Unarmed,1310);
  v.hand[1].x=1;check(p.update(v,Context::Unarmed,1320).pad.buttons==Right,"held Y stick selects quick item");
  v.hand[1].x=0;v.hand[0].lower=true;
  check(p.update(v,Context::Unarmed,1330).pad.buttons==X,"held Y X confirms instead of toggling inventory");
  check(p.update(v,Context::Unarmed,1340).pad.buttons==X,"confirm hold preserved");
  v.hand[0].lower=false;check(p.update(v,Context::Unarmed,1350).pad.buttons==0,"confirm releases while Y stays held");
  v.hand[0].lower=true;check(p.update(v,Context::Unarmed,1360).pad.buttons==X,"confirm may repeat without releasing Y");
  v.hand[0].upper=false;check(p.update(v,Context::Unarmed,1370).pad.buttons==0,"leaving modifier blocks held confirm and inventory tap");
  v=neutral();p.update(v,Context::Unarmed,1380);
  v.hand[0].upper=true;p.update(v,Context::Unarmed,1400);check(p.update(v,Context::Unarmed,1650).modifier,"250ms modifier threshold");v.hand[1].trigger=1;check(p.update(v,Context::Unarmed,1700).pad.buttons==RB,"Y right trigger native whip bumper");v.hand[0].upper=false;auto released=p.update(v,Context::Unarmed,1710);check(released.pad.buttons==0&&released.pad.rt==0,"modifier release cannot fire inventory or held ordinary trigger");v.hand[1].trigger=0;p.update(v,Context::Unarmed,1720);v.hand[1].trigger=1;check(p.update(v,Context::Unarmed,1730).pad.rt==0,"fresh trigger only charges after modifier release");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Normal,1000);v.hand[0].click=true;check(p.update(v,Context::Normal,1010).pad.buttons==0,"sprint deferred for pause chord");v.hand[1].click=true;auto r=p.update(v,Context::Normal,1050);check(r.pad.buttons==Start&&r.pause&&!r.motion,"fresh pause chord suppresses sprint/motion");check(p.update(v,Context::Normal,1200).pad.buttons==0,"held pause cannot repeat or sprint");
  v=neutral();p.update(v,Context::Normal,1300);v.hand[0].click=true;p.update(v,Context::Normal,1400);check(p.update(v,Context::Normal,1550).pad.buttons==LS,"standalone sprint held after chord window");v.hand[1].click=true;check(!(p.update(v,Context::Normal,1600).pad.buttons&Start),"old click plus new click cannot pause");
 }
 for(auto ctx:{Context::Grip,Context::Climbing,Context::Traversal,Context::Inspection,Context::Cinematic,Context::Unknown}){
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);v.hand[1].trigger=1;p.update(v,Context::Unarmed,1010);
  auto transition=p.update(v,ctx,1020,3);check(transition.pad.rt==0&&!transition.motion&&!transition.gestures,"enter native context releases old combat and disables artificial systems");
  v=neutral();p.update(v,ctx,1030);v.hand[1].trigger=.7f;
  for(uint64_t t=1040;t<2040;t+=10)check(p.update(v,ctx,t,3).pad.rt==.7f,"native timed hold duration preserved without gesture injection");
  v.hand[1].trigger=0;check(p.update(v,ctx,2050).pad.rt==0,"hold release forwarded");
  for(uint64_t t=2060;t<2260;t+=20){v.hand[0].lower=true;check(p.update(v,ctx,t).pad.buttons&X,"repeated native press forwarded");v.hand[0].lower=false;check(!(p.update(v,ctx,t+10).pad.buttons&X),"repeated native release forwarded");}
  v.hand[0].x=-.6f;v.hand[0].y=.4f;v.hand[1].x=.3f;v.hand[1].y=-.7f;auto o=p.update(v,ctx,2300);check(o.pad.lx==-.6f&&o.pad.ly==.4f&&o.pad.rx==.3f&&o.pad.ry==-.7f,"script directions are native, analog, unrotated");
  v=neutral();p.update(v,Context::Unarmed,2400);v.hand[1].lower=true;check(p.update(v,Context::Unarmed,2410).motion&&p.update(v,Context::Unarmed,2410).pad.buttons==B,"normal controls restored after script");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Grip,1000);check(p.update(v,Context::Grip,1010,0,-1).pad.lx==-1,"GRIP swipe pulse preserved");v.hand[0].x=1;check(p.update(v,Context::Grip,1020,0,-1).pad.lx==1,"manual QTE direction takes priority over gesture");
  v=neutral();v.hand[1].trigger=1;p.update(v,Context::Grip,1030);check(p.update(v,Context::Climbing,1040).pad.rt==1,"script stage transition preserves native hold");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Unarmed,1000);v.hand[0].grip=v.hand[1].grip=1;check(p.update(v,Context::Unarmed,1010).pad.buttons==X,"fresh unarmed grip also presses native interact");check(p.update(v,Context::Unarmed,1020,2).physicalAttacks==2,"confirmed gesture right trigger");v.hand[1].trigger=1;p.update(v,Context::Unarmed,1030,2);v.hand[1].trigger=0;check(p.update(v,Context::Unarmed,1040,2).physicalAttacks==2,"trigger release does not cut or duplicate an already-authorized physical punch");
  p.update(v,Context::Unarmed,1100,0);check(p.update(v,Context::Unarmed,1200,2).physicalAttacks==2,"next gesture rearmed");
  v.hand[0].upper=true;check(p.update(v,Context::Unarmed,1210,3).pad.rt==0,"Y suppresses physical punches");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Normal,1000);v.hand[1].trigger=1;v.hand[0].lower=true;p.update(v,Context::Normal,1010);
  check(p.update(v,Context::Menu,1020).pad.buttons==0,"menu entry releases old gameplay keys");v=neutral();p.update(v,Context::Menu,1030);v.hand[1].lower=true;check(p.update(v,Context::Menu,1040).pad.buttons==A,"menu lower confirms");v.hand[1].trigger=1;check(p.update(v,Context::Menu,1050).pad.rt==0,"menu ray alone owns trigger");
  v.focused=false;check(p.update(v,Context::Menu,1060).pad.buttons==0,"focus loss releases");v.focused=true;check(p.update(v,Context::Menu,1070).pad.buttons==0,"held input cannot replay on focus return");
  v=neutral();p.update(v,Context::Menu,1080);v.hand[1].upper=true;check(p.update(v,Context::Menu,1090).pad.buttons==B,"fresh back after resume");v.hand[1].tracked=false;check(p.update(v,Context::Menu,1100).pad.buttons==0,"disconnect releases");v.hand[1].tracked=true;check(p.update(v,Context::Menu,1110).pad.buttons==0,"held disconnect recovery blocked");v=neutral();p.update(v,Context::Menu,1120);v.hand[1].upper=true;v.session=2;check(p.update(v,Context::Menu,1130).pad.buttons==0,"session restart releases held key");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Normal,1000);v.hand[0].upper=true;v.hand[0].grip=1;v.hand[0].x=-1;v.hand[0].y=.5f;v.hand[1].x=.5f;v.hand[1].y=-1;v.hand[1].lower=true;
  auto o=p.update(v,Context::Normal,1010);check(o.modifier&&!o.motion&&o.pad.lx==-1&&o.pad.ly==.5f&&o.pad.rx==.5f&&o.pad.ry==-1&&o.pad.buttons==A,"native-axis page reachable even in ordinary gameplay");
 }
 Pad real{X,.7f,.2f,-.5f,.1f,.8f,.2f},vr{X|A,-1,-1,1,1,.3f,.9f};auto merged=merge(real,vr);
 check(merged.buttons==(X|A)&&merged.lx==real.lx&&merged.ly==real.ly&&merged.rx==real.rx&&merged.ry==real.ry&&merged.lt==.8f&&merged.rt==.9f,"physical priority and duplicate logical button union");
 for(unsigned mode=0;mode<29;++mode){Native n;n.fresh=n.projection=true;n.mode=mode;n.slot=1;auto c=context(n);if(mode==0||mode==1||mode==2||mode==9||mode==20)check(gameplay(c),"ordinary and wade modes permit equipped-hand actions");else check(!gameplay(c),"all other known modes retain native routing");}
 for(auto ctx:{Context::Melee,Context::Carrying}){
  Profile p;auto v=neutral();v.actor=77;p.update(v,ctx,1000);
  v.hand[0].grip=v.hand[1].grip=1;v.hand[1].trigger=1;
  check(heldGuard(p.update(v,ctx,1010)),"both grips hold item guard");
  for(uint64_t t:{1200u,1800u,2400u}){
   p.update(v,ctx,t,2);
   auto attack=p.update(v,ctx,t+185,2);
   check(attack.physicalAttacks==2&&!heldGuard(attack),"repeated item strokes override guard without button release");
   check(heldGuard(p.update(v,ctx,t+210,0)),"item guard resumes after every stroke");
  }
  p.update(v,Context::Unknown,2700);
  check(heldGuard(p.update(v,ctx,2800)),"item guard resumes after native hit interruption");
  v.hand[0].grip=0;check(!heldGuard(p.update(v,ctx,2810)),"opening hand cancels item guard");
 }
 {
  Profile p;auto v=neutral();v.actor=77;v.focusFresh=true;p.update(v,Context::Carrying,1000);
  v.hand[1].grip=1;v.hand[1].trigger=1;p.update(v,Context::Carrying,1010);
  v.hand[1].trigger=0;v.hand[1].y=-1;v.speed[1]=3;v.motionTick=1100;p.update(v,Context::Carrying,1100,2);
  check(!p.update(v,Context::Carrying,1250,2).physicalAttacks,"stick down reserves physical swing for throwing");
  v.speed[1]=0;v.motionTick=1300;v.hand[1].grip=0;
  auto o=p.update(v,Context::Carrying,1300,2);
  check(o.carryAction==CarryAction::ThrowPrime&&!o.physicalAttacks&&o.pad.rt==0,"grip release with neutral trigger prioritizes stick-selected throw over queued item strike");
 }
 for(unsigned h=0;h<2;++h){
  Profile p;auto v=neutral();v.actor=77;v.focusFresh=true;p.update(v,Context::Carrying,1000);
  v.hand[h].grip=1;v.hand[1].y=-1;p.update(v,Context::Carrying,1010);
  v.hand[h].trigger=1;p.update(v,Context::Carrying,1100,1u<<h);
  auto o=p.update(v,Context::Carrying,1180,1u<<h);
  check(o.physicalAttacks==(1u<<h)&&o.carryAction==CarryAction::None,"trigger overrides stick reservation for physical item strike per hand");
  v.speed[h]=3;v.motionTick=1430;v.hand[h].grip=0;
  o=p.update(v,Context::Carrying,1430);
  check(o.carryAction!=CarryAction::ThrowPrime&&!o.physicalAttacks,"trigger-held grip release never throws or invents a strike");
 }
 {
  Native n;n.fresh=n.projection=true;n.mode=0;n.slot=10;
  check(context(n)==Context::Carrying,"ordinary pickup remains carrying");
  n.firearm=true;check(context(n)==Context::Firearm,"native firearm override classifies pickup gun");
  n.mode=4;n.type=5;check(context(n)==Context::Ladder,"pickup firearm cannot override traversal ownership");
 }
 {
  Profile p;auto v=neutral();v.actor=77;v.focusFresh=true;
  p.update(v,Context::Unarmed,1000);v.hand[1].grip=1;
  p.update(v,Context::Unarmed,1010);v.temporaryFirearm=123;
  auto o=p.update(v,Context::Firearm,1020);
  check(o.aim&&!o.firearmMelee&&!(o.pad.buttons&X),"pickup gun immediately ready with original grip held");
  v.hand[1].trigger=1;o=p.update(v,Context::Firearm,1030);
  check(o.pad.rt==1&&!o.physicalAttacks,"pickup gun trigger shoots instead of striking");
  v.hand[1].trigger=0;v.hand[1].grip=0;
  p.update(v,Context::Firearm,1040);o=p.update(v,Context::Firearm,1100);
  check(o.carryAction==CarryAction::None&&!(o.pad.buttons&Y),"releasing pickup gun grip never invokes drop");
  v.hand[1].grip=1;v.hand[0].grip=1;o=p.update(v,Context::Firearm,1110);
  check(o.reload&&!o.aim,"pickup gun supports left grip reload");
 }
 for(bool fresh:{false,true}){
  Profile p;auto v=neutral();v.actor=77;p.update(v,Context::Firearm,1000);
  v.focusFresh=fresh;v.pickupTarget=v.useTarget=55;v.hand[1].grip=1;
  auto o=p.update(v,Context::Firearm,1010);
  check(fresh?((o.pad.buttons&X)&&!o.aim&&!o.reload&&!o.physicalAttacks):(!(o.pad.buttons&X)&&o.aim),"only fresh native pickup prompt takes priority over readiness");
  if(fresh){
   v.hand[0].grip=1;v.hand[1].trigger=1;o=p.update(v,Context::Firearm,1050,3);
   check((o.pad.buttons&X)&&!o.aim&&!o.reload&&!o.physicalAttacks&&o.pad.rt==0,"pickup pulse excludes reload and strikes");
   v.hand[0].grip=0;v.hand[1].trigger=0;v.pickupTarget=v.useTarget=0;v.temporaryFirearm=55;
   o=p.update(v,Context::Firearm,1060);
   o=p.update(v,Context::Firearm,1140);
   check(o.aim&&!o.firearmMelee&&!(o.pad.buttons&X),"held pickup grip becomes ready after bounded use pulse");
  }
 }
 {
  Profile p;auto v=neutral();v.actor=77;p.update(v,Context::Firearm,1000);
  v.focusFresh=true;v.useTarget=55;v.hand[1].grip=1;
  auto o=p.update(v,Context::Firearm,1010);
  check(o.aim&&!(o.pad.buttons&X),"generic non-pickup focus does not steal readiness");
 }
 for(bool resting:{false,true})for(auto ctx:{Context::Melee,Context::Carrying}){
  Profile p;auto v=neutral();v.actor=77;v.focusFresh=true;p.update(v,ctx,1000);
  v.hand[1].grip=1;v.hand[1].thumbRest=resting;p.update(v,ctx,1010);
  p.update(v,ctx,1100,2);
  check(p.update(v,ctx,1200,2).physicalAttacks==2,"ordinary swing has no throw arbitration without stick down");
  auto o=p.update(v,ctx,1210,2);
  check(o.physicalAttacks==2&&o.carryAction==CarryAction::None,"grip-only stroke executes even with resting thumb when grip stays held");
 }
 for(auto ctx:{Context::Melee,Context::Carrying}){
  Profile p;auto v=neutral();v.actor=77;p.update(v,ctx,1000);
  v.hand[1].grip=1;v.hand[1].trigger=1;
  for(unsigned t=1010;t<1800;t+=10)check(!p.update(v,ctx,t).physicalAttacks,"heavy charge hold alone never starts attack");
  v.hand[1].trigger=0;check(!p.update(v,ctx,1800).physicalAttacks,"cancel heavy charge without swing does nothing");
  v.hand[1].trigger=1;p.update(v,ctx,1900,2);
  check(p.update(v,ctx,1980,2).physicalAttacks==2,"heavy trigger swing bypasses 320ms throw arbitration");
 }
 for(auto ctx:{Context::Ledge,Context::WhipClimb,Context::WhipSwing,Context::Ladder,Context::RopeSlide}){
  Profile p;auto v=neutral();p.update(v,ctx,100);
  v.hand[0].y=.8f;v.hand[1].x=1;v.hand[1].y=.7f;
  auto o=p.update(v,ctx,110);
  check(o.viewTurn==1&&o.pad.rx==0&&o.pad.ry==0&&!o.motion&&o.pad.ly==.8f,"traversal routes VR yaw only and preserves native climb");
  v.hand[1].x=-1;check(p.update(v,ctx,120).viewTurn==-1,"traversal turns both directions");
  v.hand[0].upper=true;check(p.update(v,ctx,130).viewTurn==0,"modifier blocks traversal yaw");
  v=neutral();p.update(v,ctx,140);v.hand[1].x=1;v.focused=false;
  check(p.update(v,ctx,150).viewTurn==0,"focus loss blocks traversal yaw");
  v.focused=true;check(p.update(v,ctx,160).viewTurn==0,"held yaw needs neutral after focus return");
  v.hand[1].x=0;p.update(v,ctx,170);v.hand[1].x=1;
  check(p.update(v,ctx,180).viewTurn==1,"neutral rearms traversal yaw");
  v.hand[1].tracked=false;check(p.update(v,ctx,190).viewTurn==0,"untracked hand blocks traversal yaw");
 }
 for(auto ctx:{Context::Normal,Context::Climbing,Context::Traversal,Context::Menu,Context::Cinematic,Context::Unknown}){
  Profile p;auto v=neutral();p.update(v,ctx,100);v.hand[1].x=1;
  check(p.update(v,ctx,110).viewTurn==0,"unrecognized states never use traversal yaw route");
 }
 for(auto ctx:{Context::Carrying,Context::Melee}){
  Profile p;auto v=neutral();v.actor=77;v.focusFresh=true;p.update(v,ctx,1000);
  v.hand[1].grip=1;p.update(v,ctx,1010);v.hand[1].y=1;
  auto o=p.update(v,ctx,1020);check(o.carryAction==CarryAction::None&&o.pad.ry==0,"up peak does not throw or move native vertical axis");
  v.hand[1].y=0;o=p.update(v,ctx,1100);
  check(o.carryAction==CarryAction::ThrowPrime&&o.throwToken==1100&&!o.physicalAttacks,"quick upward flick throws held object without velocity or grip release");
  check(p.update(v,ctx,1200).throwToken==1100,"flick produces one throw token");
 }
 {
  Profile p;auto v=neutral();v.actor=77;v.focusFresh=true;p.update(v,Context::Carrying,1000);
  v.hand[1].grip=1;v.hand[1].thumbRest=true;p.update(v,Context::Carrying,1010);
  v.speed[1]=3;v.motionTick=1020;p.update(v,Context::Carrying,1020);
  v.hand[1].grip=0;
  check(p.update(v,Context::Carrying,1030).carryAction!=CarryAction::ThrowPrime,"thumb rest no longer selects throw");
 }
 {
  ThrowStick g;g.update(true,0,0,100);g.update(true,0,1,110);
  check(!g.update(true,0,0,400),"held up is not a flick");
  g.update(true,0,1,410);g.update(false,0,1,420);
  check(!g.update(true,0,0,430),"tracking or context interruption cancels flick");
  g.update(true,.8f,1,440);check(!g.update(true,0,0,450),"diagonal turn does not throw");
 }
 for(int reason=0;reason<7;++reason){
  Profile p;auto v=neutral();v.actor=77;v.focusFresh=true;p.update(v,Context::Carrying,1000);
  v.hand[1].grip=1;p.update(v,Context::Carrying,1010);
  if(reason==0)v.placeTarget=42;
  if(reason==1)v.focusFresh=false;
  if(reason==2)v.releaseUseTarget=42;
  if(reason==3)v.hand[1].trigger=1;
  if(reason==4)v.hand[0].trigger=1;
  v.hand[1].y=1;p.update(v,Context::Carrying,1020);
  if(reason==5)v.focused=false;
  if(reason==6)v.hand[1].tracked=false;
  v.hand[1].y=0;
  check(p.update(v,Context::Carrying,1100).carryAction!=CarryAction::ThrowPrime,"flick respects placement, use, trigger, focus and tracking ownership");
 }
 {
  Profile p;auto v=neutral();v.actor=77;p.update(v,Context::Firearm,1000);
  v.hand[1].grip=1;p.update(v,Context::Firearm,1010);
  v.supportGrip=true;v.hand[0].grip=1;v.hand[1].trigger=1;
  auto o=p.update(v,Context::Firearm,1020,3);
  check(o.aim&&o.pad.rt==1&&!o.reload&&!(o.pad.buttons&LB)&&!o.physicalAttacks,"two-hand left grip retains firing and suppresses reload/block/strikes");
  v.hand[0].grip=0;v.supportGrip=false;o=p.update(v,Context::Firearm,1030);
  check(o.aim&&o.pad.rt==1&&!o.reload,"support release retains shooting without reload");
  v.hand[1].trigger=0;v.hand[0].grip=1;o=p.update(v,Context::Firearm,1040);
  check(o.reload&&!o.aim,"grip outside support point retains reload");
 }
 {
  Profile p;auto v=neutral();v.actor=77;p.update(v,Context::Firearm,1000);
  v.hand[1].grip=1;p.update(v,Context::Firearm,1010);
  v.hand[0].trigger=1;p.update(v,Context::Firearm,1020);
  v.hand[0].trigger=0;v.hand[1].grip=0;v.hand[0].grip=1;v.supportGrip=true;
  auto o=p.update(v,Context::Firearm,1030);
  check(o.firearmMelee&&!o.aim&&!o.reload&&!(o.pad.buttons&LB),"support reservation works in melee without forcing shooting readiness");
 }
 {
  Profile p;auto v=neutral();v.actor=77;p.update(v,Context::Firearm,1000);
  p.update(v,Context::Firearm,1010);v.hand[1].y=1;
  auto o=p.update(v,Context::Firearm,1020);
  check(o.holster&&o.pad.buttons==Y&&!o.aim&&!o.pad.rt,"stick up drops firearm immediately");
  check(!p.update(v,Context::Firearm,1250).holster,"held up does not repeat");
  v.hand[1].y=0;p.update(v,Context::Firearm,1260);
  v.hand[1].y=1;v.hand[1].x=1;
  check(!p.update(v,Context::Firearm,1270).holster,"diagonal turn does not drop");
  v.focused=false;p.update(v,Context::Firearm,1280);
  v.focused=true;v.hand[1].x=0;
  check(!p.update(v,Context::Firearm,1290).holster,"focus return with held up cannot drop");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::Firearm,1000);
  v.hand[1].click=true;p.update(v,Context::Firearm,1010);
  check(p.update(v,Context::Firearm,1071).holster,"holster starts after shortened 60ms arbitration");
 }
 {
  Profile p;auto v=neutral();p.update(v,Context::WhipAttached,1000);
  v.hand[1].x=.8f;v.hand[1].y=1;
  auto o=p.update(v,Context::WhipAttached,1010);
  check(o.viewTurn==.8f&&o.pad.rx==0&&o.pad.ry==0,"attached whip turns only VR yaw; no native pitch/yaw");
  v.hand[1].y=-1;v.hand[1].trigger=1;
  o=p.update(v,Context::WhipAttached,1020);
  check(o.viewTurn==.8f&&o.pad.rx==0&&o.pad.ry==0&&(o.pad.buttons&(LB|RB))==(LB|RB),"whip pull preserved with vertical stick blocked");
 }
 for(auto ctx:{Context::Melee,Context::Carrying,Context::Firearm})for(bool heavy:{false,true}){
  Profile p;auto v=neutral();p.update(v,ctx,1000);
  v.hand[1].grip=1;v.hand[1].trigger=heavy?1.f:0.f;p.update(v,ctx,1010);
  const auto o=p.update(v,ctx,1020,2);
  check(o.physicalAttacks==2,"item velocity edge executes immediately without throw selection");
 }
 {
  Native n{true,false,false,false,false,0,1,5,0};n.whipPrompt=true;
  check(context(n)==Context::WhipPrompt,"verified interactive continuation recognized before projection");
  n.whipPrompt=false;check(context(n)==Context::Unknown,"unverified script is not a whip prompt");
  n.whipPrompt=true;n.animated=true;check(context(n)==Context::Cinematic,"authored camera excluded from whip prompt");
  Profile p;auto v=neutral();p.update(v,Context::WhipPrompt,1000);
  v.hand[1].trigger=1;v.hand[1].x=.7f;v.hand[1].y=1;
  auto o=p.update(v,Context::WhipPrompt,1010);
  check(o.pad.buttons==RB&&o.pad.rt==0&&o.viewTurn==.7f&&o.pad.rx==0&&o.pad.ry==0&&!combatActions(o),"prompt trigger is RB without attack or native pitch");
  v.hand[1].trigger=0;check(!(p.update(v,Context::WhipPrompt,1020).pad.buttons&RB),"RB releases with trigger");
  v.focused=false;v.hand[1].trigger=1;p.update(v,Context::WhipPrompt,1030);
  v.focused=true;check(!(p.update(v,Context::WhipPrompt,1040).pad.buttons&RB),"held prompt trigger blocked on focus return");
 }
 for(unsigned hand=0;hand<2;++hand){
  Profile p;auto v=neutral();p.update(v,Context::WhipClimb,1000);
  v.hand[hand].trigger=.8f;auto o=p.update(v,Context::WhipClimb,1010);
  check(o.pad.lt==.8f&&o.pad.rt==0&&!(o.pad.buttons&(LB|RB)),"either trigger climbs without yank or attack");
  v.hand[1-hand].trigger=1;o=p.update(v,Context::WhipClimb,1020);
  check(o.pad.lt==1,"two held triggers merge into one native climb hold");
  v.hand[hand].trigger=0;v.hand[1-hand].trigger=0;
  check(p.update(v,Context::WhipClimb,1030).pad.lt==0,"climb hold releases with both neutral");
 }
 Native unknown;unknown.fresh=true;unknown.projection=true;unknown.mode=200;check(context(unknown)==Context::Unknown,"future mode retains Xbox fallback");
 std::printf("Coverage: buttons %04x / axes %03x; %d failures\n",reached,axes,failures);return failures?1:0;
}
