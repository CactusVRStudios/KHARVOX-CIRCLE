#pragma once
#include <algorithm>
#include <cmath>

namespace circle::motor::input {
struct ThumbRestOutput {float extension{};bool reserved{};};
// One instance per hand. Arm with trigger released, never reinterpret an
// already charging punch. A lifted thumb cannot turn a held cosmetic trigger
// back into charge until the trigger has returned to neutral.
class ThumbRestGesture {
 bool armed{},reserved{},tail{};
public:
 ThumbRestOutput update(bool eligible,bool touch,float grip,float trigger){
  const bool neutral=std::isfinite(trigger)&&trigger<=.1f;
  if(neutral)tail=false;
  const bool chord=eligible&&touch&&std::isfinite(grip)&&grip>=.9f&&std::isfinite(trigger);
  if(!chord){
   if(reserved&&!neutral)tail=true;
   armed=reserved=false;
   return {0,tail};
  }
  reserved=!neutral;
  if(neutral)armed=true;
  return {armed&&!tail?std::clamp(trigger,0.f,1.f):0.f,reserved||tail};
 }
};
}
