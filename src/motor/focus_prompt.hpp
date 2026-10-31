#pragma once
#include <cstdint>
namespace circle::motor::interaction {
struct FocusActions {uint64_t useTarget{},placeTarget{},otherTarget{};bool fresh{};uint64_t pickupTarget{};};
inline FocusActions decodeFocus(unsigned prompt,uint64_t entity,uint64_t usable){
 FocusActions out;
 if(prompt&~0x7eu)return out;
 const auto valid=[](uint64_t id){return id&&id!=~uint64_t{};};
 out.fresh=true;
 if((prompt&(8|32|64))&&valid(entity))out.useTarget=entity;
 if((prompt&2)&&valid(usable))out.useTarget=usable;
 if(prompt&32)out.pickupTarget=out.useTarget;
 if((prompt&8)&&valid(entity))out.placeTarget=entity;
 // Museum placement uses ordinary USABLE, not GRIP COMBINE. While carrying,
 // forward its native X action; Motor still decides whether placement is valid.
 // A PICKUP prompt on another floor object must not replace the held item.
 if(!out.placeTarget&&(prompt&2)&&!(prompt&32)&&valid(usable))out.placeTarget=usable;
 if(!out.placeTarget&&(prompt&(2|32|64)))out.otherTarget=out.useTarget;
 // A prompt without its resolved entity is an incomplete observation, not
 // proof that it is safe to drop the carried object into empty space.
 if((prompt&(2|8|32|64))&&!out.useTarget)out.fresh=false;
 return out;
}
class PickupStage {
 uint64_t owner{};bool active{};
public:
 bool update(uint64_t actor,unsigned slot,const FocusActions& focus){
  if(owner!=actor||slot!=14)active=false;
  owner=actor;
  if(actor&&slot==14&&focus.fresh&&focus.pickupTarget)active=true;
  return active;
 }
};
}
