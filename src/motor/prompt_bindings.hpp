#pragma once
#include "default_input.hpp"
#include <string>
#include <string_view>
namespace circle::motor::prompts {
using input::Context;
inline std::string_view label(std::string_view action,Context c,bool firearmMelee=false){
 const bool motion=input::motionContext(c)||c==Context::WhipAttached||c==Context::Ledge||c==Context::WhipClimb||c==Context::WhipSwing||c==Context::Ladder||c==Context::RopeSlide;
 if(action=="_grip"||action=="_use")return "L lower button";
 if(action=="_jump")return motion?"R upper button":"R lower button";
 if(action=="_crouch")return motion?"R lower button":"R upper button";
 if(action=="_ui_menu_accept")return motion?"R upper button":"R lower button";
 if(action=="_ui_menu_cancel")return motion?"R lower button":"R upper button";
 if(action=="_quickinvup")return "Hold L upper + R stick up";
 if(action=="_quickinvdown")return "Hold L upper + R stick down";
 if(action=="_quickinvleft")return "Hold L upper + R stick left";
 if(action=="_quickinvright"||action=="_keyitemequip")return "Hold L upper + R stick right";
 if(action=="_sprint")return "L stick click";
 if(action=="_inventory")return "L upper button (tap)";
 if(action=="_startmenu")return "L menu / both stick clicks";
 if(action=="_weapfists")return "R stick click";
 if(action=="_push"&&c==Context::Unarmed)return "Both grips + R stick click";
 if(action=="_attack1"){
  if(c==Context::Blow)return "Both triggers";
  if(input::firearmContext(c)&&!firearmMelee)return "R grip + R trigger";
  if(c==Context::Unarmed||c==Context::Melee||firearmMelee)return "Grip + swing hand";
  if(c==Context::Photo||c==Context::Normal||c==Context::Inspection)return "R trigger";
 }
 if(action=="_aim"){
  if(input::firearmContext(c)&&!firearmMelee)return "R grip";
  if(c==Context::Photo||c==Context::Inspection||c==Context::Map||c==Context::Journal)return "L trigger";
 }
 if(action=="_weapreload"&&input::firearmContext(c))return "L grip";
 if(action=="_whip"&&(c==Context::WhipPrompt||c==Context::Ledge||c==Context::WhipAttached))return "R trigger";
 if(action=="_zoomin"&&(c==Context::Photo||c==Context::Journal))return c==Context::Photo?"R grip":"R stick up";
 if(action=="_zoomout"&&(c==Context::Photo||c==Context::Journal))return c==Context::Photo?"L grip":"R stick down";
 if(action=="_ui_journal_zoom"||action=="_ui_journal_inspect")return "L trigger";
 if(action=="_ui_menu_tab_next")return "R grip";
 if(action=="_ui_menu_tab_prev")return "L grip";
 if(action=="_moveforward"||action=="_moveForward")return "L stick forward";
 if(action=="_moveback"||action=="_moveBack")return "L stick back";
 if(action=="_moveleft"||action=="_moveLeft")return "L stick left";
 if(action=="_moveright"||action=="_moveRight")return "L stick right";
 if(action=="_ui_movement_all")return "L stick";
 if(action=="_ui_movement_updown")return "L stick up/down";
 if(action=="_ui_movement_leftright")return "L stick left/right";
 return {};
}
inline std::string replace(std::string_view source,Context context,bool firearmMelee=false){
 auto id=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';};
 std::string out;out.reserve(source.size());
 for(size_t i=0;i<source.size();){
  if(source[i]=='_'&&(i==0||!id(source[i-1])||(i>=2&&source[i-2]=='^'))){
   size_t end=i+1;while(end<source.size()&&id(source[end]))++end;
   auto text=label(source.substr(i,end-i),context,firearmMelee);
   if(!text.empty()){out+='[';out+=text;out+=']';}else out.append(source.substr(i,end-i));
   i=end;
  }else out+=source[i++];
 }
 return out;
}
void publish(Context,bool firearmMelee,bool enabled);
void install();
}
