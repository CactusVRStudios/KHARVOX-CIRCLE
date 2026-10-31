#pragma once
namespace circle::motor {
// Raw-input backends may ignore absolute motion. Alternating relative pulses
// guarantee motion delivery without accumulating desktop cursor displacement.
struct MenuMotionPulse {
 bool phase{};
 int next(){phase=!phase;return phase?1:-1;}
};
constexpr bool menuRayContext(bool mainMenu,bool pauseMenu,bool immersiveGameplay,bool nativeCursor){
 // The title screen's child pages hide its root widget while retaining the
 // native UI cursor. The title scene itself reports native INGAME (2).
 return mainMenu||pauseMenu||(!immersiveGameplay&&nativeCursor);
}
struct PauseChord {
 bool latched{};unsigned long long until{};
 bool update(bool left,bool right,bool focused,unsigned long long now){
  if(!focused){latched=left||right;until=0;return false;}
  if(!left&&!right)latched=false;
  if(left&&right&&!latched){latched=true;until=now+120;}
  return now<until;
 }
};
inline float imageCoordinate(float panelCoordinate,float offset,float extent,float imageExtent){
 return imageExtent>0?(offset+panelCoordinate*extent)/imageExtent:0;
}
// A held trigger entering a menu or re-entering the panel cannot click.
struct MenuClick {
 bool armed{},down{};
 int update(bool allowed,bool hit,float trigger){
  if(!allowed||!hit){armed=false;if(down){down=false;return -1;}return 0;}
  if(trigger<.25f){armed=true;if(down){down=false;return -1;}}
  if(trigger>.65f&&armed&&!down){armed=false;down=true;return 1;}
  return 0;
 }
};
}
