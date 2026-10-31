#include "vkcore/presenter_impl.hpp"
#include "vkcore/keep_active.hpp"
#include "motor/controller_tracking.hpp"
#include "motor/hand_runtime.hpp"
#include "motor/menu_pointer_policy.hpp"
#include "motor/sequential.hpp"
#include <algorithm>

namespace circlevr::vkcore {
bool XrPresenter::Impl::updateMotorMenu(XrTime time,const XrCompositionLayerQuad* panel){
 circle::motor::hands::maintainComfortControls();
 static circle::motor::MenuClick click;
 static circle::motor::MenuMotionPulse motionPulse;
 static uint64_t lastWheel{};
 menuPointer={};
 const auto input=circle::motor::hands::latest();
 const HWND window=gameWindow();DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);
 const bool nativeCursor=circle::motor::locomotion::nativeMenuCursorActive();
 static const bool desktopSimulator=[] {char b[8]{};return GetEnvironmentVariableA("CIRCLE_SIMULATOR_DESKTOP_INPUT",b,8)==1&&b[0]=='1';}();
 bool allowed=!desktopSimulator&&panel&&window&&pid==GetCurrentProcessId()&&input.focused&&
  circle::motor::menuRayContext(hybrid.state==circle::motor::GameState::MainMenu,
   circle::motor::sequential::menuActive(),circle::motor::sequential::projectionAllowed(),nativeCursor);
 XrPosef localPanel=panel?panel->pose:XrPosef{};
 if(allowed&&panel->space!=localSpace){
  XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
  constexpr XrSpaceLocationFlags required=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
  if(XR_FAILED(xr.xrLocateSpace(panel->space,localSpace,time,&location))||(location.locationFlags&required)!=required)allowed=false;
  else {
   const auto& a=location.pose;const auto& b=panel->pose;
   const Pose combined=compose(Pose{{a.orientation.x,a.orientation.y,a.orientation.z,a.orientation.w},{a.position.x,a.position.y,a.position.z}},Pose{{b.orientation.x,b.orientation.y,b.orientation.z,b.orientation.w},{b.position.x,b.position.y,b.position.z}});
   localPanel={{combined.orientation.x,combined.orientation.y,combined.orientation.z,combined.orientation.w},{combined.position.x,combined.position.y,combined.position.z}};
  }
 }
 const auto& hand=input.hands[1];
 bool hitPanel=false;std::optional<menu::PanelHit> hit;
 if(allowed&&hand.aimTracked){
  const auto pose=[](XrPosef p){return Pose{{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},{p.position.x,p.position.y,p.position.z}};};
  const menu::Panel target{pose(localPanel),panel->size.width,panel->size.height};
  const Pose aim=pose(hand.aim);hit=menu::intersectPanel(target,aim);
  menuPointer.visible=true;menuPointer.from=aim.position;
  const auto head=locateHead(time);menuPointer.eye=head?head->position:aim.position;
  menuPointer.to=hit?hit->point:aim.position+rotate(aim.orientation,Vec3{0,0,-2});
  if(hit){
   hitPanel=true;menuPointer.hit=true;menuPointer.dot=menu::dotPose(target,hit->u,hit->v,.003f);menuPointer.dotSide=.012f;
   const auto& rect=panel->subImage.imageRect;
   const auto imageExtent=panel->subImage.swapchain==uiSwapchain?uiExtent:eyeExtent;
   const float u=circle::motor::imageCoordinate(hit->u,float(rect.offset.x),float(rect.extent.width),float(imageExtent.width));
   const float v=circle::motor::imageCoordinate(hit->v,float(rect.offset.y),float(rect.extent.height),float(imageExtent.height));
   const auto pixel=menu::cursorPixel(u,v,imageExtent.width,imageExtent.height);
   hitPanel=circle::motor::locomotion::menuCursorTarget(pixel.x,pixel.y,true);
   // Motor's hover dispatch needs a real motion event, not only new cursor
   // storage. Move on every ray sample, before queuing any button edge.
   RECT client{};POINT screen{};
   if(hitPanel&&GetClientRect(window,&client)&&client.right>0&&client.bottom>0){
    screen.x=std::clamp(int(u*client.right),0,int(client.right-1));
    screen.y=std::clamp(int(v*client.bottom),0,int(client.bottom-1));
    if(ClientToScreen(window,&screen)){
     const int vx=GetSystemMetrics(SM_XVIRTUALSCREEN),vy=GetSystemMetrics(SM_YVIRTUALSCREEN);
     const int vw=GetSystemMetrics(SM_CXVIRTUALSCREEN),vh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
     if(vw>1&&vh>1){INPUT move{};move.type=INPUT_MOUSE;
      move.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
      move.mi.dx=LONG(std::clamp(double(screen.x-vx)*65535/(vw-1),0.0,65535.0));
      move.mi.dy=LONG(std::clamp(double(screen.y-vy)*65535/(vh-1),0.0,65535.0));
      // Absolute positioning alone can stop generating native SE_MOUSE
      // events after an input/focus change. Keep the desktop position, then
      // wake raw mouse dispatch with one relative pixel. The game-thread
      // cursor hooks overwrite its accumulated XY with the exact ray target
      // before UI dispatch. Neither event carries a button or wheel action.
      INPUT pulse{};pulse.type=INPUT_MOUSE;
      pulse.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_MOVE_NOCOALESCE;
      pulse.mi.dx=motionPulse.next();
      INPUT moves[2]={move,pulse};
      hitPanel=SendInput(2,moves,sizeof(INPUT))==2;
     }else hitPanel=false;
    }else hitPanel=false;
   }else hitPanel=false;
  }
 }
 if(!hitPanel)circle::motor::locomotion::menuCursorTarget(0,0,false);
 const int edge=click.update(allowed&&hand.aimTracked&&!input.hands[0].upper,hitPanel,hand.trigger);
 static uint64_t diagnostic{};const uint64_t tick=GetTickCount64();
 if(panel&&tick-diagnostic>2000){diagnostic=tick;CIRCLE_VR_LOG("motor-menu-ray-state: menu %d foreground %d focused %d aim %d allowed %d hit %d crop %d,%d %dx%d image %ux%u",circle::motor::sequential::menuActive(),pid==GetCurrentProcessId(),input.focused,hand.aimTracked,allowed,hitPanel,panel->subImage.imageRect.offset.x,panel->subImage.imageRect.offset.y,panel->subImage.imageRect.extent.width,panel->subImage.imageRect.extent.height,eyeExtent.width,eyeExtent.height);}
 if(edge){INPUT event{};event.type=INPUT_MOUSE;event.mi.dwFlags=edge>0?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP;
  const bool sent=SendInput(1,&event,sizeof(event))==1;
  CIRCLE_VR_LOG("motor-menu-ray: %s %s",edge>0?"press":"release",sent?"delivered":"failed");
 }
 const uint64_t now=GetTickCount64();
 if(allowed&&hitPanel&&!input.hands[0].upper&&std::abs(hand.stick.y)>.6f&&now-lastWheel>150){
  INPUT wheel{};wheel.type=INPUT_MOUSE;wheel.mi.dwFlags=MOUSEEVENTF_WHEEL;wheel.mi.mouseData=DWORD(hand.stick.y>0?WHEEL_DELTA:-WHEEL_DELTA);
  SendInput(1,&wheel,sizeof(wheel));lastWheel=now;
 }
 return menuPointer.visible;
}
}
