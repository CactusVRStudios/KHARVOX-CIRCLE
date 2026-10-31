#include "motor/prompt_bindings.hpp"
#include <iostream>
using namespace circle::motor;
int main(){int errors=0;auto check=[&](bool yes,const char* what){if(!yes){std::cerr<<what<<"\n";++errors;}};
 check(prompts::replace("Press _grip",input::Context::Normal)=="Press [L lower button]","Use mapping");
 check(prompts::replace("_push",input::Context::Unarmed)=="[Both grips + R stick click]","Push chord");
 check(prompts::replace("_push",input::Context::Firearm)=="_push","No unarmed push outside combat");
 check(prompts::replace("_attack1",input::Context::Firearm)=="[R grip + R trigger]","Firearm readiness");
 check(prompts::replace("_attack1",input::Context::Firearm,true)=="[Grip + swing hand]","Firearm melee mode");
 check(prompts::replace("_jump",input::Context::Normal)=="[R upper button]","Gameplay swap");
 check(prompts::replace("_ui_menu_accept",input::Context::Menu)=="[R lower button]","Menu confirm");
 check(prompts::replace("_attack1",input::Context::Blow)=="[Both triggers]","Blow context");
 check(prompts::replace("foo_grip _gripper _ui_currency_vat #str_pickup_title_mysterynote",input::Context::Normal)=="foo_grip _gripper _ui_currency_vat #str_pickup_title_mysterynote","Preserve identifiers and unknown tokens");
 check(prompts::replace("^a_grip / _grip.",input::Context::Normal)=="^a[L lower button] / [L lower button].","Color markup and punctuation");
 return errors?1:0;}
