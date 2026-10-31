#include "menu_pointer_policy.hpp"
#include "controller_guides.hpp"
#include "features/menu/panel_pointer.hpp"
#include "ui_layer/ui_settings.hpp"
#include <cstdio>
#include <cmath>
int main(){
 int errors=0;auto check=[&](bool ok,const char* name){if(!ok){++errors;std::printf("FAIL %s\n",name);}};
 circle::motor::MenuClick click;
 circle::motor::MenuMotionPulse pulse;int displacement{};
 for(int i=0;i<1000;++i){const int delta=pulse.next();check(delta==1||delta==-1,"menu heartbeat always produces relative motion");displacement+=delta;check(displacement>=0&&displacement<=1,"menu heartbeat cannot accumulate desktop drift");}
 using circle::motor::GameState;
 using circle::motor::controllerGuideContext;
 check(controllerGuideContext(GameState::MainMenu,false)&&controllerGuideContext(GameState::PauseMenu,false)&&
       controllerGuideContext(GameState::Menu,false),"controller guides stay on main/pause menus and their child pages");
 for(auto state:{GameState::Gameplay,GameState::Loading,GameState::Cutscene,GameState::BootIntro,GameState::Unknown})
  check(!controllerGuideContext(state,false),"controller guides never overlay non-menu scenes");
 check(!controllerGuideContext(GameState::Menu,true)&&controllerGuideContext(GameState::PauseMenu,true),
       "journal inspection hides guides; opening pause over inspection still shows them");
 // Recenter rotates the entire layout; hinged inner edges retain their gap.
 const circlevr::Pose tilted{circlevr::Quat::fromAxisAngle({0,0,1},1.5707963268f),{1,2,-3}};
 for(bool left:{true,false}){
  const auto guide=circle::motor::controllerGuidePose(tilted,1.8f,2.f,left);
  const auto inner=guide.position+rotate(guide.orientation,circlevr::Vec3{left?1.f:-1.f,0,0});
  const auto expected=tilted.position+rotate(tilted.orientation,circlevr::Vec3{left?-.95f:.95f,0,0});
  check(std::abs(inner.x-expected.x)<.0001f&&std::abs(inner.y-expected.y)<.0001f&&std::abs(inner.z-expected.z)<.0001f,
        "side guide inner edge retains 5 cm menu gap after recenter");
  check(guide.position.z>tilted.position.z,"side guides angle toward player");
 }
 check(circle::motor::menuRayContext(false,false,false,true),"title submenu retains native cursor routing with hidden root");
 check(!circle::motor::menuRayContext(false,false,false,false),"cinematic without UI cursor cannot gain ray input");
 check(!circle::motor::menuRayContext(false,false,true,true),"immersive gameplay cannot gain menu ray from cursor alone");
 check(circle::motor::menuRayContext(true,false,false,false)&&circle::motor::menuRayContext(false,true,false,true),"main and pause menus retain existing routing");
 const auto cinema=circlevr::ui_layer::wideContentRect(2496,2688);
 check(cinema.width==2496&&cinema.height==1404&&cinema.y==642,"portrait eye surface yields centered 16 by 9 cinema without stretching");
 circle::motor::PauseChord chord;
 check(!chord.update(true,false,true,1000)&&chord.update(true,true,true,1010),"both stick clicks pause");
 check(!chord.update(true,true,true,1200)&&!chord.update(true,true,true,1300),"held chord cannot repeatedly toggle pause");
 chord.update(false,false,true,1400);check(chord.update(true,true,true,1500),"release rearms pause chord");
 check(!chord.update(true,true,false,1510)&&!chord.update(true,true,true,1520),"focus loss clears chord pulse");
 check(std::abs(circle::motor::imageCoordinate(.5f,642,1404,2688)-.5f)<.0001f,"cropped panel center maps to full image center");
 check(std::abs(circle::motor::imageCoordinate(0,642,1404,2688)-642.f/2688)<.0001f,"top of cropped menu is not top of native image");
 check(click.update(true,true,1)==0,"held trigger entering menu cannot click");
 check(click.update(true,true,0)==0&&click.update(true,true,1)==1,"fresh trigger presses once");
 check(click.update(true,true,1)==0,"held trigger cannot repeat");
 check(click.update(false,false,1)==-1,"focus tracking or menu loss releases");
 check(click.update(true,true,1)==0,"resume must release before click");
 click.update(true,true,0);click.update(true,true,1);check(click.update(true,false,1)==-1,"leaving panel releases drag");
 circlevr::menu::Panel panel{{{0,0,0,1},{0,0,-2}},3.2f,1.8f};
 auto hit=circlevr::menu::intersectPanel(panel,circlevr::Vec3{},circlevr::Vec3{0,0,-1});
 check(hit&&std::abs(hit->u-.5f)<.0001f&&std::abs(hit->v-.5f)<.0001f,"ray hits displayed quad center");
 check(!circlevr::menu::intersectPanel(panel,circlevr::Vec3{},circlevr::Vec3{0,0,1}),"backwards ray cannot click");
 auto pixel=circlevr::menu::cursorPixel(1,1,1280,720);check(pixel.x==1279&&pixel.y==719,"client cursor edges clamp");
 auto nativePixel=circlevr::menu::cursorPixel(.5f,circle::motor::imageCoordinate(0,642,1404,2688),2496,2688);
 check(std::abs(nativePixel.x-1248)<=1&&std::abs(nativePixel.y-642)<=1,"native cursor uses render pixels and GUI crop, not the 1280x720 mirror");
 // A view-space GUI panel must be transformed with the head before testing
 // a LOCAL-space controller ray; the background plane is not its geometry.
 circlevr::Pose head{{0,.70710678f,0,.70710678f},{1,1.6f,2}};
 circlevr::menu::Panel turned{circlevr::compose(head,panel.pose),panel.width,panel.height};
 auto turnedHit=circlevr::menu::intersectPanel(turned,head);
 check(turnedHit&&std::abs(turnedHit->u-.5f)<.0001f&&std::abs(turnedHit->v-.5f)<.0001f,"head-relative GUI and local ray share the same coordinate space");
 const float croppedTop=circle::motor::imageCoordinate(0,642,1404,2688);
 const float croppedBottom=circle::motor::imageCoordinate(1,642,1404,2688);
 check(croppedTop<.5f&&croppedBottom>.5f&&std::abs(croppedTop+croppedBottom-1)<.0001f,"GUI crop maps symmetrically through its own image extent");
 return errors?1:0;
}
