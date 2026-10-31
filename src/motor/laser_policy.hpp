#pragma once
#include "firearm_policy.hpp"
#include <span>
#include <cstring>
namespace circle::motor::laser {
// Reflected idLoadoutAttachmentComponent: world muzzle at256, model-local at264.
inline constexpr uintptr_t worldMuzzleOffset=0x100;
inline std::array<float,15> muzzlePlacement(hands::Pose p){
 const auto x=hands::rotate(p.orientation,{1,0,0}),y=hands::rotate(p.orientation,{0,1,0}),z=hands::rotate(p.orientation,{0,0,1});
 return {p.position.x,p.position.y,p.position.z,x.x,x.y,x.z,y.x,y.y,y.z,z.x,z.y,z.z,1,1,1};
}
// Reflected Motor 1.0.17 layouts: idActiveLoadoutWeapon and equippedWeaponState_t.
inline bool nativeReady(std::span<const unsigned char> active,std::span<const unsigned char> state,uint64_t gameTime){
 if(active.size()<0x368||state.size()<0x30)return false;
 const auto field=[]<class T>(std::span<const unsigned char> bytes,size_t offset){T v{};std::memcpy(&v,bytes.data()+offset,sizeof(v));return v;};
 if(active[0x366]!=1||active[0x362]||state[0x29]>1||field.template operator()<int64_t>(state,0)>static_cast<int64_t>(gameTime))return false;
 const int mode=field.template operator()<int>(active,0x184),modes=field.template operator()<int>(active,0x188),ammos=field.template operator()<int>(active,0x18c);
 if(mode<0||mode>=modes||modes>3||ammos<1||ammos>3)return false;
 const auto firing=0x220+mode*0x68;
 const auto decl=field.template operator()<uint64_t>(active,firing+0x28);
 const int cost=field.template operator()<int>(active,firing+0x1c);if(!decl||cost<1)return false;
 for(int i=0;i<ammos;++i){const auto ammo=0x190+i*0x30;
  if(field.template operator()<uint64_t>(active,ammo)==decl)return field.template operator()<int>(active,ammo+0x14)>=cost;}
 return false;
}
inline hands::Vec localPoint(hands::Vec world,hands::Pose camera,hands::Pose head){
 const auto v=hands::rotate(hands::inverse(camera).orientation,world-camera.position);
 return head.position+hands::rotate(head.orientation,{-v.y,v.z,-v.x});
}
}
