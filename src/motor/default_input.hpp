#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "thumb_rest_gesture.hpp"
#include "grip_carry.hpp"
#include "weapon_slots.hpp"

namespace circle::motor::input {
// XInput wire values, independent of Windows/OpenXR for replay tests.
enum Button : uint16_t {Up=1,Down=2,Left=4,Right=8,Start=16,Back=32,LS=64,RS=128,LB=256,RB=512,A=4096,B=8192,X=16384,Y=32768};
struct Pad {uint16_t buttons{};float lx{},ly{},rx{},ry{},lt{},rt{};};
struct Hand {float x{},y{},trigger{},grip{};bool lower{},upper{},click{},menu{},tracked{},thumbRest{};};
struct Physical {std::array<Hand,2> hand{};bool focused{};uint64_t session{};bool rightWhip{};unsigned nearUsableHands{};uint64_t actor{},useTarget{},placeTarget{},releaseUseTarget{};bool focusFresh{},pickupReady{};std::array<float,2> speed{};uint64_t motionTick{};bool whipAttached{};uint64_t pickupTarget{},temporaryFirearm{};bool supportGrip{},equippedFood{};};
enum class Context {Normal,Unarmed,Melee,Firearm,Whip,Carrying,Grip,Climbing,Traversal,Inspection,Menu,Cinematic,Unknown,WhipAttached,Swimming,Journal,Map,Ledge,WhipClimb,WhipSwing,Ladder,RopeSlide,Photo,WhipPrompt,Blow,Seated,BoatDriver,BoatMap,SeatedFirearm};
inline bool seatedContext(Context c){return c==Context::Seated||c==Context::SeatedFirearm;}
inline bool firearmContext(Context c){return c==Context::Firearm||c==Context::SeatedFirearm;}
inline bool viewTurnContext(Context c){return seatedContext(c)||c==Context::WhipPrompt||c==Context::WhipAttached||c==Context::Ledge||c==Context::WhipClimb||c==Context::WhipSwing||c==Context::Ladder||c==Context::RopeSlide;}
inline const char* name(Context c){static constexpr const char* names[]={"NORMAL","UNARMED_COMBAT","MELEE_WEAPON","FIREARM","WHIP","CARRYING","GRIP_INTERACTION","CLIMBING","TRAVERSAL","INSPECTION","MENU","CINEMATIC","UNKNOWN","WHIP_ATTACHED","SWIMMING","JOURNAL","MAP","LEDGE_HANG","WHIP_CLIMB","WHIP_SWING","LADDER","ROPE_SLIDE","PHOTO_CAMERA","WHIP_PROMPT","BLOW_INTERACTION","SCRIPTED_SEAT","BOAT_DRIVER","BOAT_MAP","SEATED_FIREARM"};return names[unsigned(c)];}
struct Native {bool fresh{},menu{},projection{},animated{},local{};unsigned mode{255},slot{};int type{},reason{};int journal{-1};bool firearm{},whipPrompt{},blow{};};
// WADE20 is also used on Himalaya terrain with usable hands/equipment. It
// owns a movement basis, not a modal swimming input layout (surface7/dive8).
inline bool equipmentPosture(unsigned mode){return mode==0||mode==1||mode==2||mode==9||mode==20;}
inline Context context(const Native& n){
 if(n.menu)return Context::Menu;
 if(!n.fresh)return Context::Unknown;
 if(n.whipPrompt&&n.mode==0&&n.type==5&&!n.reason&&!n.animated&&!n.local)return Context::WhipPrompt;
 // Input ownership must survive the cinematic-to-immersive render handoff.
 // The measured native seat state, not last render's projection, owns axes.
 if(n.mode==1&&n.type==5&&!n.reason&&!n.animated&&!n.local)
  return n.firearm||hands::firearmSlot(n.slot)?Context::SeatedFirearm:Context::Seated;
 if(n.mode==24){
  if(n.slot==16&&n.journal==1)return Context::Journal;
  if(n.slot==16&&n.journal==2)return Context::BoatMap;
  return Context::BoatDriver;
 }
 if(n.slot==13)return Context::Inspection; // Ability-book detail/purchase stays flat after leaving Journal.
 if(n.mode==23||n.mode==26)return Context::Inspection;
 if(n.mode==10||n.mode==28)return Context::Grip;
 if(n.mode==16&&n.type==5&&n.reason==0&&!n.animated&&!n.local)return Context::RopeSlide;
 if(n.mode==4&&n.type==5&&n.reason==0&&!n.animated&&!n.local)return Context::Ladder;
 if(n.mode==14&&n.type==5&&n.reason==0&&!n.animated&&!n.local)return Context::Ledge;
 if(n.mode==12&&n.type==7&&n.reason==0&&!n.animated&&!n.local)return Context::WhipSwing;
 if(n.mode==13&&n.type==7&&n.reason==0&&!n.animated&&!n.local)return Context::WhipClimb;
 if(n.mode==4||n.mode==6||n.mode==13||n.mode==14)return Context::Climbing;
 if(n.mode==3||n.mode==11||n.mode==12||n.mode==15||n.mode==16||n.mode==22||n.mode==24||n.mode==25||n.mode==27)return Context::Traversal;
 // Measured beam attachment: physics type7, ordinary posture. Keep native
 // camera/limb/axis ownership, but retain Quest B jump / A crouch-release.
 if(n.type==7&&(n.mode==0||n.mode==1||n.mode==2||n.mode==9))return Context::WhipAttached;
 if(n.animated||n.local||n.reason)return Context::Cinematic;
 // Raised journal must remain identifiable after its presentation disables projection.
 if(n.slot==16&&n.type==0&&equipmentPosture(n.mode)){
  if(n.journal==1)return Context::Journal;
  if(n.journal==2)return Context::Map;
 }
 if(n.type==0&&n.projection&&(n.mode==7||n.mode==8))return Context::Swimming;
 if(n.type!=0||!n.projection||!equipmentPosture(n.mode))return Context::Unknown;
 if(n.blow&&n.slot==1&&!n.firearm)return Context::Blow;
 if(n.firearm||hands::firearmSlot(n.slot))return Context::Firearm;
 if(hands::meleeWeaponSlot(n.slot))return Context::Melee;
 switch(n.slot){case 17:return Context::Photo;case 16:return n.journal==2?Context::Map:Context::Journal;case 1:return Context::Unarmed;case 18:return Context::Whip;case 10:case 14:return Context::Carrying;default:return Context::Normal;}
}
inline bool gameplay(Context c){return c<=Context::Carrying;}
inline bool motionContext(Context c){return gameplay(c)||c==Context::Swimming||c==Context::Map||c==Context::Photo||c==Context::Blow;}
struct BlowState {
 uint64_t owner{},tick{};
 void observe(uint64_t who,uint64_t now){owner=who;tick=now;}
 void end(uint64_t who){if(owner==who)*this={};}
 bool active(uint64_t who,uint64_t now)const{return who&&owner==who&&tick&&now>=tick&&now-tick<100;}
};
// Finish a climb in standing posture via the normal crouch toggle. Never
// write the capsule/stance directly: native headroom checks retain ownership.
struct StandAfterClimb {
 uint64_t session{},owner{},last{},returned{},pulseAt{};
 bool armed{},returning{},pulsing{},sent{};
 void reset(){*this={};}
 bool update(const Native& n,uint64_t actor,uint64_t trackingSession,uint64_t now,
             bool focused,bool manual){
  if(!focused||!n.fresh||n.menu||!actor||!now||manual){reset();return false;}
  if(session!=trackingSession||owner!=actor||now<last|| (last&&now-last>250))reset();
  session=trackingSession;owner=actor;last=now;
  const bool climb=(n.mode==3||n.mode==4||n.mode==6||n.mode==13||n.mode==14)&&
      (n.type==5||n.type==7)&&!n.reason&&!n.animated&&!n.local;
  if(climb){armed=true;returning=pulsing=sent=false;return false;}
  if(!armed)return false;
  const bool ordinary=n.type==0&&!n.reason&&!n.animated&&!n.local&&
      (n.mode==0||n.mode==1||n.mode==9);
  if(!ordinary){reset();return false;}
  if(!returning){returned=now;returning=true;}
  if(now-returned>1500){reset();return false;}
  // Allow the native transition and immersive admission to settle. A brief
  // standing sample can precede the final crouched posture on the next tick.
  if(n.mode!=1){
   if(sent||now-returned>=250)reset();
   return false;
  }
  if(!n.projection)return false;
  if(!motionContext(context(n))){reset();return false;}
  // The first ordinary sample can still inhibit stance during climb recovery.
  if(!sent&&now-returned<250)return false;
  if(!sent){sent=true;pulsing=true;pulseAt=now;}
  if(pulsing&&now-pulseAt<120)return true;
  pulsing=false;return false; // No retries/toggle oscillation under low ceilings.
 }
};
inline unsigned family(Context c){return c==Context::BoatMap?6:c==Context::Blow?5:motionContext(c)?0:c==Context::Menu?1:c==Context::Inspection?2:c==Context::Journal?4:3;}
inline float axis(float v){return std::isfinite(v)?std::clamp(v,-1.f,1.f):0;}
inline float scalar(float v){return std::max(0.f,axis(v));}
inline Pad merge(Pad real,Pad vr){
 real.buttons|=vr.buttons;
 // A physical axis owns its entire stick; never create a mixed direction.
 if(std::hypot(real.lx,real.ly)<.15f){real.lx=vr.lx;real.ly=vr.ly;}
 if(std::hypot(real.rx,real.ry)<.15f){real.rx=vr.rx;real.ry=vr.ry;}
 real.lt=std::max(real.lt,vr.lt);real.rt=std::max(real.rt,vr.rt);return real;
}
struct Result {Pad pad{};float viewTurn{};Context context{Context::Unknown};bool modifier{},motion{},gestures{},pause{},aim{},holster{},push{},reload{},firearmMelee{};unsigned physicalAttacks{};std::array<float,2> fingerGesture{};CarryAction carryAction{};int journalZoom{};uint64_t throwToken{};bool blow{},waterMotion{};};
inline bool nativeWaterMotion(const Native& n,const Result& r){
 return r.motion&&n.fresh&&!n.menu&&n.projection&&!n.animated&&!n.local&&!n.reason&&n.type==0&&
  (n.mode==7||n.mode==8||n.mode==20);
}
inline uint64_t blowActions(const Result& r){return r.context==Context::Blow&&r.blow&&!r.modifier&&!r.pause?(uint64_t(1)|(uint64_t(1)<<15)):0;}
inline uint64_t journalActions(const Result& r){
 return r.context==Context::Journal&&!r.pause?(r.journalZoom>0?uint64_t(1)<<6:r.journalZoom<0?uint64_t(1)<<7:0):0;
}
inline bool guardContext(Context c){return c==Context::Unarmed||c==Context::Melee||c==Context::Carrying||c==Context::Firearm;}
inline bool heldGuard(const Result& r){
 return guardContext(r.context)&&(r.context!=Context::Firearm||r.firearmMelee)&&!r.modifier&&!r.pause&&!r.holster&&!r.push&&
  !r.physicalAttacks&&(r.pad.buttons&LB)&&!(r.pad.buttons&X);
}
inline uint64_t combatActions(const Result& r){
 if((!gameplay(r.context)&&r.context!=Context::SeatedFirearm)||r.modifier||r.pause||r.holster||r.push)return 0;
 if(r.reload&&firearmContext(r.context))return uint64_t(1)<<20;
 return (heldGuard(r)?uint64_t(1)<<1:0)|(r.aim?uint64_t(1)<<5:0)|((r.physicalAttacks&1)?uint64_t(1)<<15:0)|((r.physicalAttacks&2)?uint64_t(1):0);
}
// Default profile. Y chords expose the remaining Xbox buttons; Y+left-grip
// exposes unmodified native stick axes and prompt buttons when needed.
class Profile {
 uint64_t session{},yAt{},tapUntil{},pauseUntil{},leftAt{},rightAt{},sprintUntil{};
 bool compatible{},initialized{},hadFocus{},wasY{},usedY{},pauseLatched{},oldLeft{},oldRight{};
 unsigned oldFamily{};
 std::array<uint16_t,2> blocked{};
 std::array<bool,2> gestureBlocked{};
 std::array<ThumbRestGesture,2> cosmetic;
 uint64_t holsterUntil{},pushUntil{};bool holsterSent{true},pushCandidate{},pushSent{true};
 bool whipReserved{};
 bool firearmDropArmed{};
 bool firearmTriggerRearm{},firearmMeleeSelected{true},firearmModeArmed{};
 bool firearmGripArmed{};uint64_t temporaryWeapon{};
 bool traversalFirearm{},traversalMelee{};uint64_t traversalOwner{},traversalUntil{};
 bool continuousGuard{};uint64_t guardOwner{};Context guardKind{Context::Unknown};
 uint64_t itemStrikeAt{};
 bool itemStrikeEdge{};unsigned itemStrikeMask{},itemStrikeDelay{};
 std::array<bool,2> interactHeld{},interactArmed{};
  std::array<uint64_t,2> gripUseUntil{};
 GripCarry carry;ThrowStick throwStick;uint64_t throwActor{};
 Context previousContext{Context::Unknown};
 static uint16_t held(const Hand& h){return (h.lower?1:0)|(h.upper?2:0)|(h.click?4:0)|(h.menu?8:0)|(h.trigger>.1f?16:0)|(h.grip>.1f?32:0)|(std::hypot(h.x,h.y)>.15f?64:0);}
 static Hand sanitize(Hand h){h.x=axis(h.x);h.y=axis(h.y);h.trigger=scalar(h.trigger);h.grip=scalar(h.grip);return h;}
public:
 Result update(Physical input,Context ctx,uint64_t now,unsigned punches=0,float swipe=0,uint64_t pauseWindow=60,float pullDown=0){
  if(ctx!=Context::SeatedFirearm)return updateProfile(input,ctx,now,punches,swipe,pauseWindow,pullDown);
  // Reuse firearm grip/trigger rearming, support hand, Use, reload and menu
  // arbitration. The seat still owns translation; no melee or pitch aliases.
  input.hand[0].x=input.hand[0].y=0;
  if(!input.hand[0].upper){input.hand[1].y=0;input.hand[0].trigger=0;}
  firearmMeleeSelected=false;
  auto out=updateProfile(input,Context::Firearm,now,0,0,pauseWindow,0);
  out.context=ctx;out.motion=out.gestures=false;out.physicalAttacks=0;
  out.firearmMelee=false;out.push=false;
  out.viewTurn=!out.modifier&&!out.pause&&!input.hand[0].upper?out.pad.rx:0;
  out.pad.lx=out.pad.ly=out.pad.rx=out.pad.ry=0;
  if(!out.modifier&&!out.pause){
   // Preserve the seat's native A/B exit actions, not walking jump/crouch.
   const auto buttons=out.pad.buttons;
   out.pad.buttons&=~(A|B|LS|LB);
   if(buttons&A)out.pad.buttons|=B;
   if(buttons&B)out.pad.buttons|=A;
  }
  return out;
 }
private:
 Result updateProfile(Physical input,Context ctx,uint64_t now,unsigned punches,float swipe,uint64_t pauseWindow,float pullDown){
  Result out;out.context=ctx;
  const bool food=input.equippedFood&&(ctx==Context::Normal||ctx==Context::Carrying||ctx==Context::Melee);
  if(food){punches=0;continuousGuard=false;}
  const float physicalLeftTrigger=scalar(input.hand[0].trigger);
  const float physicalRightTrigger=scalar(input.hand[1].trigger);
  const std::array<float,2> physicalGrip{scalar(input.hand[0].grip),scalar(input.hand[1].grip)};
  const std::array<bool,2> physicalTracked{input.hand[0].tracked,input.hand[1].tracked};
  // Hit reactions briefly lose the ordinary projection/context. Remember only
  // an already requested guard across UNKNOWN; never release the general
  // neutral locks (which also protect interaction, charge, and QTE inputs).
  const bool rawFists=input.hand[0].tracked&&input.hand[1].tracked&&
   scalar(input.hand[0].grip)>=.9f&&scalar(input.hand[1].grip)>=.9f;
  if(!input.focused||!rawFists||session!=input.session||guardOwner!=input.actor||
     input.hand[0].upper||input.hand[0].menu||input.hand[1].click||input.rightWhip||
     (!guardContext(ctx)&&ctx!=Context::Unknown)||(continuousGuard&&ctx!=Context::Unknown&&ctx!=guardKind))continuousGuard=false;
  // OBJECT slot14 is also used while reaching for a floor pickup. In that
  // measured state the item is not held yet; slot10 acknowledges carrying.
  const bool pickupReady=ctx==Context::Carrying&&input.pickupReady&&!food;
  const bool carrying=food||(ctx==Context::Carrying&&!pickupReady)||ctx==Context::Melee;
  if(!input.focused||session!=input.session||previousContext!=ctx||input.actor!=throwActor){throwStick={};firearmDropArmed=false;}
  throwActor=input.actor;
  const bool carryAllowed=ctx==Context::Unarmed||ctx==Context::Normal||ctx==Context::Carrying||ctx==Context::Melee;
  if((!carryAllowed&&ctx!=Context::Unknown)||!input.focused||!physicalTracked[0]||!physicalTracked[1]||input.hand[0].upper||input.hand[0].menu||session!=input.session)carry.cancel();
  const bool climbing=ctx==Context::Climbing||ctx==Context::Traversal||ctx==Context::Ledge||ctx==Context::Ladder;
  if(!input.focused||session!=input.session||traversalOwner!=input.actor||input.hand[0].upper||input.hand[0].menu)traversalFirearm=false;
  if(climbing&&previousContext==Context::Firearm){traversalFirearm=true;traversalMelee=firearmMeleeSelected;traversalOwner=input.actor;}
  if(traversalFirearm&&climbing)traversalUntil=now+750;
  if(traversalFirearm&&(now>traversalUntil||(!climbing&&ctx!=Context::Firearm&&ctx!=Context::Unarmed&&ctx!=Context::Unknown)))traversalFirearm=false;
  const bool resumeFirearm=traversalFirearm&&ctx==Context::Firearm&&!climbing;
  if((gameplay(ctx)&&ctx!=Context::Firearm)||session!=input.session){firearmMeleeSelected=true;firearmModeArmed=false;}
  if(ctx!=Context::Firearm){firearmModeArmed=false;firearmGripArmed=false;}
  if(resumeFirearm){firearmMeleeSelected=traversalMelee;traversalFirearm=false;firearmTriggerRearm=true;}
  if(ctx==Context::Firearm&&input.temporaryFirearm&&input.temporaryFirearm!=temporaryWeapon){
   firearmMeleeSelected=false;firearmTriggerRearm=physicalRightTrigger>.1f;
   interactHeld={};gripUseUntil={};carry.cancel();
  }
  temporaryWeapon=ctx==Context::Firearm?input.temporaryFirearm:0;
  bool firearmMelee=ctx==Context::Firearm&&firearmMeleeSelected;
  const bool itemContext=!food&&(ctx==Context::Firearm||ctx==Context::Melee||ctx==Context::Carrying);
  if(!input.focused||!input.hand[1].tracked||input.hand[0].upper||input.hand[0].menu||
     !itemContext||previousContext!=ctx||session!=input.session||(input.hand[0].grip<.9f&&input.hand[1].grip<.9f)||(ctx==Context::Firearm&&!firearmMelee&&input.hand[1].trigger>.1f)){itemStrikeAt=0;itemStrikeEdge=false;itemStrikeMask=0;}
  // Left-stick direction is continuous across native locomotion/QTE ownership.
  // Keep existing neutral locks from focus/tracking/menu/modifier loss, and
  // never carry buttons or artificial turning across the context boundary.
  const bool keepMove=initialized&&session==input.session&&hadFocus&&input.focused&&
   input.hand[0].tracked&&!(blocked[0]&64)&&!wasY&&!input.hand[0].upper&&
   (oldFamily==0||oldFamily==3)&&(family(ctx)==0||family(ctx)==3);
  const bool transition=!initialized||session!=input.session||oldFamily!=family(ctx)||(!hadFocus&&input.focused);
  const bool carryInteract=initialized&&session==input.session&&hadFocus&&input.focused&&
   !input.hand[0].upper&&(oldFamily==0||oldFamily==3)&&(family(ctx)==0||family(ctx)==3);
  if(!input.focused||transition){
   for(unsigned h=0;h<2;++h)blocked[h]=held(input.hand[h]);
   for(unsigned h=0;h<2;++h){
    if(carryInteract&&interactHeld[h]&&input.hand[h].tracked&&input.hand[h].grip>.65f)blocked[h]&=~uint16_t(32);
    else interactHeld[h]=false;
    gripUseUntil[h]=0;
    interactArmed[h]=false;
   }
   if(keepMove)blocked[0]&=~uint16_t(64);
   compatible=wasY=usedY=pauseLatched=oldLeft=oldRight=false;yAt=tapUntil=pauseUntil=leftAt=rightAt=sprintUntil=0;gestureBlocked={true,true};holsterUntil=pushUntil=0;pushCandidate=false;pushSent=true;holsterSent=true;
  }
  // Readiness is a hold, unlike firing/use. Returning from native climb may
  // resume grip, but held triggers and buttons must still require release.
  if(resumeFirearm)for(unsigned h=0;h<2;++h)if(input.hand[h].tracked)blocked[h]&=~uint16_t(32);
  initialized=true;session=input.session;oldFamily=family(ctx);hadFocus=input.focused;
  if(previousContext==Context::Unarmed&&ctx!=Context::Unarmed&&!input.rightWhip)
   for(unsigned h=0;h<2;++h)if(input.hand[h].trigger>.1f)blocked[h]|=16;
  previousContext=ctx;
  if(!input.focused){for(auto& gesture:cosmetic)gesture.update(false,false,0,1);return out;}
  if(wasY&&!input.hand[0].upper&&usedY)for(unsigned h=0;h<2;++h)blocked[h]|=held(input.hand[h]);
  for(unsigned h=0;h<2;++h){auto& hand=input.hand[h];hand=sanitize(hand);
   if(!hand.tracked){blocked[h]=0xffff;hand={};continue;}
   blocked[h]&=held(hand);
   // A fresh physical stroke is its own deliberate input edge after recovery.
   // It may re-enable held grip/trigger, never buttons or movement/QTE inputs.
   if(!transition&&guardContext(ctx)&&(punches&(1u<<h))&&!input.hand[0].upper&&!input.hand[0].menu)
       blocked[h]&=~uint16_t(16|32);
   const auto b=blocked[h];
   if(b&1)hand.lower=false;if(b&2)hand.upper=false;if(b&4)hand.click=false;if(b&8)hand.menu=false;
   if(b&16)hand.trigger=0;if(b&32)hand.grip=0;if(b&64)hand.x=hand.y=0;
  }
  auto& l=input.hand[0];auto& r=input.hand[1];auto& p=out.pad;
  const bool dropAllowed=ctx==Context::Firearm&&!transition&&r.tracked&&
   !(blocked[1]&64)&&!l.upper&&!l.menu&&!l.lower&&!r.click&&!input.rightWhip;
  if(!dropAllowed||input.actor!=throwActor)firearmDropArmed=false;
  else if(std::abs(r.y)<=.25f&&std::abs(r.x)<=.25f)firearmDropArmed=true;
  const bool dropUp=dropAllowed&&firearmDropArmed&&r.y>=.75f&&std::abs(r.x)<=.45f;
  if(dropUp)firearmDropArmed=false;
  const bool throwControls=carrying&&!transition&&physicalTracked[0]&&physicalTracked[1]&&
   !(blocked[1]&64)&&!l.upper&&!l.menu&&!l.lower&&!r.click&&!input.rightWhip;
  const bool throwDown=throwControls&&r.y<=-.65f&&std::abs(r.x)<=.45f;
  const std::array<bool,2> throwMode{throwDown&&physicalLeftTrigger<=.1f,
                                    throwDown&&physicalRightTrigger<=.1f};
  const bool flickThrow=throwStick.update(throwControls&&physicalLeftTrigger<=.1f&&physicalRightTrigger<=.1f,r.x,r.y,now);
  if(ctx==Context::Firearm){
   bool readyPress=false;
   if(!r.tracked||l.upper||l.menu||(blocked[1]&32))firearmGripArmed=false;
   else if(r.grip<=.65f)firearmGripArmed=true;
   else if(r.grip>=.9f&&firearmGripArmed){firearmGripArmed=false;readyPress=physicalRightTrigger<=.1f;}
   if(readyPress&&!(input.focusFresh&&input.pickupTarget)){
    firearmMeleeSelected=false;itemStrikeAt=0;itemStrikeMask=0;itemStrikeEdge=false;
    continuousGuard=false;
    if(r.trigger>.1f)firearmTriggerRearm=true;
   }
   if(!l.tracked||l.upper||l.menu||l.lower||r.click||(blocked[0]&16))firearmModeArmed=false;
   else if(l.trigger<=.1f)firearmModeArmed=true;
   else if(l.trigger>=.5f&&firearmModeArmed){
    firearmMeleeSelected=!firearmMeleeSelected;firearmModeArmed=false;
    itemStrikeAt=0;itemStrikeMask=0;itemStrikeEdge=false;
   }
  }
  firearmMelee=ctx==Context::Firearm&&firearmMeleeSelected;out.firearmMelee=firearmMelee;
  if(r.trigger<=.1f)firearmTriggerRearm=false;
  else if(firearmMelee)firearmTriggerRearm=true;

  std::array<bool,2> cosmeticReserved{};
  for(unsigned h=0;h<2;++h){const auto& hand=input.hand[h];
   const auto gesture=cosmetic[h].update(!transition&&ctx==Context::Unarmed&&hand.tracked&&
    !l.upper&&!l.menu&&!r.click&&!(h==1&&input.rightWhip),hand.thumbRest,hand.grip,hand.trigger);
   out.fingerGesture[h]=gesture.extension;cosmeticReserved[h]=ctx==Context::Unarmed&&gesture.reserved;
   // Cosmetic finger pose never consumes a physical strike or charge.
  }
  if(l.upper||l.menu){interactHeld={};interactArmed={};gripUseUntil={};}
  const bool whip=input.rightWhip&&gameplay(ctx)&&!l.upper;
  if(whipReserved&&!whip&&!l.upper&&r.trigger>.1f){blocked[1]|=16;r.trigger=0;}
  whipReserved=whip;
  const bool y=l.upper;if(y)holsterSent=true;
  if(!y)compatible=false;else if(l.grip>=.9f)compatible=true;
  if(y&&!wasY){yAt=now;usedY=false;}
  const bool chord=y&&(l.lower||l.click||l.grip>=.9f||r.lower||r.upper||r.click||l.trigger>.15f||r.trigger>.15f||std::hypot(r.x,r.y)>.25f);
  out.modifier=y&&(usedY||chord||now-yAt>=250);
  if(out.modifier)usedY=true;
  if(!y&&wasY&&!usedY&&now-yAt<250)tapUntil=now+90;
  wasY=y;
  if(now<tapUntil)p.buttons|=Back;
  // Both click edges must be fresh and close together. Delay sprint until
  // the chord window expires, including a short tap pending arbitration.
  if(!l.click&&!r.click)pauseLatched=false;
  if(l.click&&!oldLeft)leftAt=now;
  if(r.click&&!oldRight){rightAt=now;holsterSent=false;pushSent=false;
   pushCandidate=ctx==Context::Unarmed&&!y&&!input.rightWhip&&l.tracked&&r.tracked&&l.grip>=.9f&&r.grip>=.9f;}
  if(!y&&l.click&&r.click&&!pauseLatched&&leftAt&&rightAt&&
     (leftAt>rightAt?leftAt-rightAt:rightAt-leftAt)<=pauseWindow){pauseLatched=true;pauseUntil=now+90;sprintUntil=0;}
  if(!l.click&&oldLeft&&!y&&!pauseLatched&&now-leftAt<=pauseWindow)sprintUntil=now+90;
  const bool rightReleased=!r.click&&oldRight;
  oldLeft=l.click;oldRight=r.click;
  if(l.menu||now<pauseUntil){carry.cancel();itemStrikeAt=0;itemStrikeEdge=false;itemStrikeMask=0;holsterUntil=pushUntil=0;pushCandidate=false;pushSent=true;holsterSent=true;interactHeld={};interactArmed={};p={};p.buttons=Start;out.pause=true;return out;}
  if(ctx==Context::BoatDriver||ctx==Context::BoatMap){
   // Driver owns steering/throttle; native look and equipment chords must
   // not rotate the body independently or equip an unusable weapon.
   p={};out.modifier=false;
   if(now<tapUntil)p.buttons|=Back;
   if(!y){p.lx=l.x;p.ly=l.y;p.lt=l.trigger;p.rt=r.trigger;
    if(ctx==Context::BoatMap){
     if(l.lower)p.buttons|=X;
     if(l.grip>.5f)p.buttons|=LB;if(r.grip>.5f)p.buttons|=RB;
    }else if(l.lower||l.grip>=.9f||r.grip>=.9f)p.buttons|=X;
    if(r.lower)p.buttons|=A;if(r.upper)p.buttons|=B;
   }
   return out;
  }
  if((gameplay(ctx)||ctx==Context::Photo)&&!y&&!pauseLatched&&!pushCandidate&&!holsterSent&&rightAt&&
     (rightReleased||(r.click&&now-rightAt>pauseWindow))){holsterUntil=now+200;holsterSent=true;}
  if(ctx==Context::Unarmed&&!y&&!pauseLatched&&pushCandidate&&!pushSent&&rightAt&&
     (rightReleased||(r.click&&now-rightAt>pauseWindow))){pushUntil=now+120;pushSent=true;holsterSent=true;holsterUntil=0;}
  if(ctx!=Context::Unarmed||input.rightWhip){pushUntil=0;pushCandidate=false;}
  if((!gameplay(ctx)&&ctx!=Context::Photo)||y||pauseLatched){holsterUntil=pushUntil=0;pushCandidate=false;}
  if(pushCandidate&&(r.click||rightReleased||now<pushUntil)){
   carry.cancel();itemStrikeAt=0;itemStrikeEdge=false;itemStrikeMask=0;continuousGuard=false;
   firearmTriggerRearm=true;interactHeld={};interactArmed={};gestureBlocked={true,true};
   out.push=true;out.motion=motionContext(ctx);p={};p.lx=l.x;p.ly=l.y;
   if(now<pushUntil)p.buttons=RS;
   return out;
  }
  if(dropUp&&!y&&!pauseLatched){holsterUntil=now+200;holsterSent=true;}
  if(now<holsterUntil){
   carry.cancel();
   out.holster=true;p={};p.buttons=Y;p.lx=l.x;p.ly=l.y;
   itemStrikeAt=0;itemStrikeEdge=false;itemStrikeMask=0;
   interactHeld={};interactArmed={};gestureBlocked={true,true};return out;
  }
  out.motion=motionContext(ctx)&&!y&&!pauseLatched;
  out.gestures=ctx==Context::Unarmed&&!y&&!pauseLatched;
  p.lx=l.x;p.ly=l.y;
  if(ctx==Context::Journal){
   // A modal native journal owns navigation. Never leak either stick into
   // locomotion/look, including the compatibility modifier page.
   p.lx=p.ly=p.rx=p.ry=0;out.motion=false;
   if(l.lower)p.buttons|=X;
   if(r.lower)p.buttons|=A;if(r.upper)p.buttons|=B;
   if(l.grip>.5f)p.buttons|=LB;if(r.grip>.5f)p.buttons|=RB;
   p.lt=l.trigger; // Native journal zoom/inspect is LT, not the bumper zoom steps.
   if(std::abs(l.y)>=std::abs(l.x)){if(l.y>.5f)p.buttons|=Up;if(l.y<-.5f)p.buttons|=Down;}
   else {if(l.x>.5f)p.buttons|=Right;if(l.x<-.5f)p.buttons|=Left;}
   out.journalZoom=r.y>.5f?1:r.y<-.5f?-1:0;
   return out;
  }
  if(out.modifier){
   // Momentary compatibility page: Y + left grip. Both native stick axes
   // and prompt A/B are reachable even while the VR camera owns normal look.
   // Releasing Y leaves the page and blocks held actions until neutral.
   if(compatible){
    p={};p.lx=l.x;p.ly=l.y;p.rx=r.x;p.ry=r.y;p.lt=l.trigger;p.rt=r.trigger;
    if(l.lower)p.buttons|=X;if(r.lower)p.buttons|=A;if(r.upper)p.buttons|=B;
    if(l.click)p.buttons|=LS;if(r.click)p.buttons|=RS;return out;
   }
   if(l.lower)p.buttons|=X; // Confirm the native quick-item prompt while Y remains held.
   if(r.lower)p.rt=1; // native RT's _weapreload contextual path
   if(r.upper)p.buttons|=Left; // native quick inventory left slot
   if(r.trigger>.15f)p.buttons|=RB; // _whip/_clinchgrab
   if(l.trigger>.15f)p.buttons|=LB; // _block
   if(r.click)p.buttons|=RS;
   if(l.click)p.buttons|=Y;
   if(r.x<-.5f)p.buttons|=Left;if(r.x>.5f)p.buttons|=Right;
   if(r.y<-.5f)p.buttons|=Down;if(r.y>.5f)p.buttons|=Up;
   p.lx=p.ly=0;return out;
  }
  if(y){p.lx=p.ly=0;return out;}
  // Hold semantics are forwarded, never synthesized as an auto-repeat macro.
  if(l.lower)p.buttons|=X;
  const bool movementButtons=motionContext(ctx)||ctx==Context::WhipAttached||ctx==Context::Ledge||ctx==Context::WhipClimb||ctx==Context::WhipSwing||ctx==Context::Ladder||ctx==Context::RopeSlide;
  if(r.lower)p.buttons|=movementButtons?B:A;
  if(r.upper)p.buttons|=movementButtons?A:B;
  if(!pauseLatched&&!y&&((l.click&&now-leftAt>pauseWindow)||now<sprintUntil))p.buttons|=LS;
  p.rx=r.x;p.ry=throwControls?0:r.y;
  if(viewTurnContext(ctx)){out.viewTurn=r.x;p.rx=p.ry=0;} // VR reference turns independently; native left stick owns hanging.
  if(ctx==Context::Seated){
   // Use must remain reachable while the seat owns translation. Preserve
   // native hold/release semantics, without enabling attacks or movement.
   if(l.grip>=.9f||r.grip>=.9f)p.buttons|=X;
   p.lx=p.ly=0;return out;
  }
  if(ctx==Context::WhipPrompt||ctx==Context::Ledge){
   // This verified traversal continuation prompts for native RB. Do not also
   // emit RT (attack), and keep native pitch/yaw out of the headset view.
   p.lt=l.trigger;p.rt=0;
   if(r.trigger>.15f)p.buttons|=RB;
   return out;
  }
  if(ctx==Context::WhipClimb){
   // Either hand may hold the native LT climb action; never emit attack/yank.
   p.lt=std::max(l.trigger,r.trigger);p.rt=0;
   return out;
  }
  if(ctx==Context::Blow){
   // Only the native air-effect state enables this chord. Emit its two action
   // bits separately from XInput so LT cannot also aim/prime a throw. Holding
   // does not synthesize repeated presses; native edge/cooldown rules apply.
   out.blow=l.tracked&&r.tracked&&l.trigger>=.5f&&r.trigger>=.5f;
   return out;
  }
  if(ctx==Context::Photo){
   // Photo equipment uses native LB/RB zoom, not generic grip-to-Use.
   // Keep LT viewfinder and RT shutter, and the normal VR locomotion route.
   p.lt=l.trigger;p.rt=r.trigger;
   if(l.grip>.5f)p.buttons|=LB;
   if(r.grip>.5f)p.buttons|=RB;
   return out;
  }
  if(ctx==Context::WhipAttached||(ctx==Context::Whip&&input.rightWhip&&input.whipAttached)){
   // Y + both triggers previously emitted LB|RB. In the native attached
   // state reserve RT for that same yank chord, without a simultaneous RT
   // attack. Climbing/swinging and unknown scripted states keep their routes.
   p.lt=l.trigger;p.rt=0;
   if(r.trigger>.15f)p.buttons|=LB|RB;
   return out;
  }
  if(ctx==Context::Map){
   p.lt=l.trigger; // Same native zoom hold as the raised journal.
   // Native default.cfg uses LB/RB for _tab_prev_ui/_tab_next_ui.
   // Lowered Map retains VR locomotion/turning with the normal A/B mapping.
   // Grips navigate tabs; left trigger retains native zoom/detail.
   if(l.grip>.5f)p.buttons|=LB;
   if(r.grip>.5f)p.buttons|=RB;
   if(r.click&&!pauseLatched)p.buttons|=RS;
   return out;
  }
  if(ctx!=Context::Menu){
   // Empty-hand triggers only charge a physical punch in hand_runtime. They
   // must never start a native hold/release attack by themselves.
   if(ctx==Context::Firearm){
    // Native LT is aim, RT changes meaning outside aim. Grip owns readiness;
    // an ungripped trigger must not leak a native pistol melee/shot on regrip.
    const bool readyGrip=r.grip>=.9f;
    if(!readyGrip&&r.trigger>.1f){blocked[1]|=16;r.trigger=0;}
    out.aim=readyGrip&&!firearmMelee;
    p.rt=out.aim&&!firearmTriggerRearm?r.trigger:0.f;
   }else if(ctx!=Context::Unarmed&&!itemContext){p.lt=l.trigger;p.rt=whip?0:r.trigger;}
   // In an active GRIP puzzle, another Use press can detach/cancel the
   // interaction. Keep manual X and an already accepted hold below, but do
   // not turn a fresh squeeze for a direction gesture into another Use.
   if(ctx!=Context::Grip&&ctx!=Context::Unarmed&&!itemContext&&(l.grip>=.9f||(!whip&&r.grip>=.9f)))p.buttons|=X;
  }
  for(unsigned h=0;h<2;++h){const auto& hand=input.hand[h];
   const bool allowed=hand.tracked&&!y&&!pauseLatched&&ctx!=Context::Menu&&ctx!=Context::Inspection&&!(h==1&&whip)&&!r.click&&!cosmeticReserved[h];
   if(!allowed||hand.grip<=.65f){interactHeld[h]=false;gripUseUntil[h]=0;}
   if(!allowed)interactArmed[h]=false;
   else if(hand.grip<=.65f)interactArmed[h]=true;
   else if(hand.grip>=.9f&&interactArmed[h]){
    interactArmed[h]=false;
    if((ctx==Context::Unarmed||ctx==Context::Normal||pickupReady||(h==1&&ctx==Context::Firearm&&input.focusFresh&&input.pickupTarget))&&hand.trigger<=.1f){
     // A fresh grip also acts as a short physical X press. Do not require a
     // near-hand point or a published focus result BEFORE asking Motor to use
     // its current gaze target. Hold-capable accepted interactions retain X.
     gripUseUntil[h]=now+120;if(ctx!=Context::Firearm)carry.pickup(h,now);
     interactHeld[h]=ctx!=Context::Firearm&&((input.nearUsableHands&(1u<<h))||(input.focusFresh&&input.useTarget));
    }
   }
   if(allowed&&now<gripUseUntil[h])p.buttons|=X;
   if(interactHeld[h]){p.buttons|=X;punches&=~(1u<<h);(h?p.rt:p.lt)=0;}
  }
  if(ctx==Context::Carrying&&l.lower)carry.cancel(); // manual X has priority
  const auto held=ctx==Context::Unknown?CarryResult{}:carry.update({carryAllowed&&!y&&!pauseLatched&&!whip&&!l.lower,carrying,input.focused,input.focusFresh,
      input.session,input.actor,input.placeTarget,input.releaseUseTarget,physicalTracked,physicalGrip,input.speed,input.motionTick,throwMode,flickThrow},now);
  if(carrying){
   // Pickup's held X must finish before release can produce a new Place press.
   interactHeld={};gripUseUntil={};if(!l.lower)p.buttons&=~X;
   if(held.neutral||held.action!=CarryAction::None){
    p.buttons&=~(X|Y);p.lt=p.rt=0;punches=0;itemStrikeAt=0;itemStrikeEdge=false;itemStrikeMask=0;
    if(held.action==CarryAction::Place)p.buttons|=X;
    if(held.action==CarryAction::Drop)p.buttons|=Y;
    out.carryAction=held.action;out.throwToken=held.throwToken;
   }
  }
  // A deliberate pickup/Use owns this tick over readiness, reload and attacks.
  if(ctx==Context::Firearm&&(p.buttons&X)){
   out.aim=false;out.reload=false;out.physicalAttacks=0;p.lt=p.rt=0;p.buttons&=~LB;
   itemStrikeAt=0;itemStrikeMask=0;itemStrikeEdge=false;continuousGuard=false;
   if(physicalRightTrigger>.1f)firearmTriggerRearm=true;
   return out;
  }
  // Left grip requests reload; X retains native use/grip with priority.
  // Reload wins over aim, shots, guard and physical strikes.
  if(ctx==Context::Firearm&&!input.supportGrip&&l.grip>=.9f&&!l.lower&&!y&&!pauseLatched){
   out.reload=true;out.aim=false;out.physicalAttacks=0;p.buttons&=~(X|LB);p.lt=p.rt=0;
   itemStrikeAt=0;itemStrikeMask=0;itemStrikeEdge=false;continuousGuard=false;
   firearmTriggerRearm=true;return out;
  }
  // Menu trigger/wheel remain owned by the confirmed ray adapter; no second
  // XInput confirm or scroll event is generated here.
  if(ctx==Context::Menu)p.rx=p.ry=0;
  if(ctx==Context::Swimming)p.ry=0;
  for(unsigned h=0;h<2;++h){
   if(!(punches&(1u<<h)))gestureBlocked[h]=false;
   if(out.gestures&&!r.click&&!gestureBlocked[h]&&!(h==1&&whip)&&(punches&(1u<<h)))out.physicalAttacks|=1u<<h;
  }
  // Holding stick down alone is not a throw. The carry release decision above
  // cancels the queued stroke only when the hand actually lets go.
  if(itemContext&&!y&&!pauseLatched&&!r.click){
   unsigned mask=input.supportGrip?0:punches;
   for(unsigned h=0;h<2;++h)if(!input.hand[h].tracked||input.hand[h].grip<.9f||gestureBlocked[h]){mask&=~(1u<<h);itemStrikeMask&=~(1u<<h);}
   if(itemStrikeAt&&!itemStrikeMask)itemStrikeAt=0;
   if(ctx==Context::Firearm&&!firearmMelee&&r.trigger>.1f)mask=0;
   const bool edge=mask!=0;
   if(edge&&!itemStrikeEdge&&!itemStrikeAt){
    itemStrikeAt=now;itemStrikeMask=mask;
    const bool triggerStroke=((mask&1)&&physicalGrip[0]>=.9f&&l.trigger>.1f)||
                             ((mask&2)&&physicalGrip[1]>=.9f&&r.trigger>.1f);
    // Trigger-held swings cannot throw. Do not spend their native charge
    // credit waiting for a grip-release throw decision (especially heavy items).
    itemStrikeDelay=carrying&&throwDown&&!triggerStroke?320:0;
   }
   itemStrikeEdge=edge;
   if(itemStrikeAt){
    const auto elapsed=now-itemStrikeAt;
    const unsigned strikeDelay=itemStrikeDelay; // latched at the physical swing
    if(elapsed>=strikeDelay&&!mask){itemStrikeAt=0;itemStrikeMask=0;}
    else if(elapsed<strikeDelay+250){
     out.aim=false;p.lt=p.rt=0;
     if(elapsed>=strikeDelay&&elapsed<strikeDelay+200)out.physicalAttacks=itemStrikeMask;
    }else {itemStrikeAt=0;itemStrikeMask=0;}
   }
  }
  if(ctx==Context::Grip&&!y&&std::hypot(p.lx,p.ly)<.15f&&!(blocked[0]&64)){
   p.lx=axis(swipe);p.ly=p.lx==0?std::min(0.f,axis(pullDown)):0;
  }
  if(input.supportGrip){out.physicalAttacks=0;continuousGuard=false;return out;}
  // Two closed fists hold native block. Preserve motion strikes; release block
  // on their pulse so the guard chord cannot swallow the physical attack.
  const bool fistsReady=l.tracked&&r.tracked&&l.grip>=.9f&&r.grip>=.9f;
  if(!food&&guardContext(ctx)&&(ctx!=Context::Firearm||firearmMelee)&&!y&&!pauseLatched&&!r.click&&!whip&&fistsReady&&input.actor){
   continuousGuard=true;guardOwner=input.actor;guardKind=ctx;
  }
  if(!food&&guardContext(ctx)&&(ctx!=Context::Firearm||firearmMelee)&&!y&&!pauseLatched&&!r.click&&!whip&&
     (fistsReady||continuousGuard)&&!out.physicalAttacks&&!(p.buttons&X))
   p.buttons|=LB;
  return out;
 }
};
}
