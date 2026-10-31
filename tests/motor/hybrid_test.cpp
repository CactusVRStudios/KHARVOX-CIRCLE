#include "motor/hybrid.hpp"
#include <stdexcept>
#include <iostream>
using namespace circle::motor;
void check(bool condition,const char* reason) { if(!condition) throw std::runtime_error(reason); }
int main() {
    HybridController controller;
    Observation o; o.nativeStateValid=true; o.nativeState=2;
    StereoReadiness ready{true,true,true,true,true,true};
    controller.requestStereo(); controller.update(o,ready);
    check(controller.state==GameState::Unknown && controller.mode==RenderMode::CinemaQuad,
          "Manual stereo escaped unknown INGAME camera gate");
    o.controlValidated=o.playerControl=o.cameraValidated=o.gameplayCamera=true;
    auto missing=ready; missing.renderPair=false; controller.update(o,missing);
    check(controller.mode==RenderMode::CinemaQuad,"Unverified render re-entry enabled stereo");
    controller.update(o,ready);
    check(controller.mode==RenderMode::GameplayStereo,"Complete readiness rejected");
    o.cinematicCamera=true; o.gameplayCamera=false; controller.update(o,ready);
    check(controller.state==GameState::Cutscene && controller.mode==RenderMode::CinemaQuad,
          "Cinematic did not stop stereo despite latched F7 request");
    o.cinematicCamera=false; o.gameplayCamera=true; o.pauseValidated=o.paused=true;
    controller.update(o,ready);
    check(controller.state==GameState::PauseMenu && controller.mode==RenderMode::MenuQuad,"Pause gate failed");
    o.paused=false;o.menuValidated=o.menuActive=true;o.cinematicCamera=true;
    controller.update(o,ready);
    check(controller.state==GameState::Menu&&controller.mode==RenderMode::MenuQuad,"submenu must override cutscene and manual stereo");
    o.mainMenu=true;controller.update(o,ready);
    check(controller.state==GameState::MainMenu&&controller.mode==RenderMode::MenuQuad,"title menu within native INGAME gets menu presentation");
    o.menuActive=o.mainMenu=o.cinematicCamera=false;
    o.paused=false; o.nativeState=1; controller.update(o,ready);
    check(controller.mode==RenderMode::LoadingQuad,"Loading allowed projection");
    o.nativeState=2; controller.failStereo("test render failure"); controller.update(o,ready);
    check(controller.mode==RenderMode::SafeMonoFallback,"Stereo failure did not latch mono");
    controller.requestStereo(); controller.toggleAutomatic(); controller.toggleAutomatic(); controller.update(o,ready);
    check(controller.stereoFailed() && controller.mode==RenderMode::SafeMonoFallback,"Hotkey cleared session failure");
    o.nativeStateValid=false; controller.update(o,ready);
    check(controller.state==GameState::Unknown && controller.mode==RenderMode::SafeMonoFallback,"Unreadable engine state enabled stereo");
    check(quadSource(RenderMode::LoadingQuad,true,2000,true)==QuadSource::GameImage,"Fresh loading image rejected");
    check(quadSource(RenderMode::LoadingQuad,true,2001,true)==QuadSource::LoadingCard,"Stale gameplay held through loading");
    check(quadSource(RenderMode::LoadingQuad,true,2001,false)==QuadSource::None,"Unready loading card submitted");
    check(quadSource(RenderMode::CinemaQuad,true,5000,true)==QuadSource::GameImage,"Paused cinema image discarded");
    check(quadSource(RenderMode::LoadingQuad,false,0,true)==QuadSource::LoadingCard,"Loading before first image has no card");
    std::cout<<"PASS: unknown/native camera gates, cutscene/pause/loading transitions and sticky failure. Synthetic safety checks, not runtime camera evidence.\n";
}
