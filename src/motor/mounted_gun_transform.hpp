#pragma once
#include "hand_pose.hpp"
#include <cstring>

namespace circle::motor::turret {
// idTransformComponent +20 is a generation-tagged world transform handle.
// MountedGunPlayerUpdateJob +168 supplies the same pool used by its native
// seat-placement helper. Read at view construction, not at the earlier job.
struct MountSource {
 uintptr_t record{},component{},pool{};uint64_t gun{},handle{UINT64_MAX};
};
template<class Read> bool readMount(const MountSource& s,Read&& read,hands::Pose& out){
 if(!s.record||!s.component||!s.pool||!s.gun||s.handle==UINT64_MAX)return false;
 const auto get=[&]<class T>(uintptr_t p,T& v){return read(p,&v,sizeof(v));};
 uint64_t gun{},handle{},count{},countAgain{};uintptr_t data{},dataAgain{};
 if(!get(s.record,gun)||gun!=s.gun||!get(s.component+0x20,handle)||handle!=s.handle||
    !get(s.pool,data)||!get(s.pool+8,count)||!data||count>10000000)return false;
 const uint64_t index=handle&((uint64_t{1}<<24)-1),offset=(handle>>40)&0x3ff;
 if(index+offset>=count||data>UINTPTR_MAX-count*32)return false;
 uint16_t generation{},generationAgain{};
 if(!get(data+index*32+12,generation)||generation!=uint16_t(handle>>24))return false;
 std::array<unsigned char,32> bytes{},again{};
 if(!get(data+(index+offset)*32,bytes)||!get(data+(index+offset)*32,again)||bytes!=again||
    !get(s.pool,dataAgain)||dataAgain!=data||!get(s.pool+8,countAgain)||countAgain!=count||
    !get(data+index*32+12,generationAgain)||generationAgain!=generation||
    !get(s.component+0x20,handle)||handle!=s.handle||!get(s.record,gun)||gun!=s.gun)return false;
 hands::Pose pose;
 std::memcpy(&pose.position,bytes.data(),sizeof(hands::Vec));
 std::memcpy(&pose.orientation,bytes.data()+16,sizeof(hands::Quat));
 if(!hands::valid(pose))return false;
 out=pose;return true;
}
// Missing/coherency-failed reads hold the last view-time root. Falling back to
// the older job snapshot would reintroduce the forward/backward correction.
struct RenderMount {
 uint64_t owner{},gun{},life{};hands::Pose pose{};bool valid{};
 void reset(){*this={};}
 bool update(uint64_t who,uint64_t weapon,uint64_t generation,const hands::Pose* current){
  if(owner!=who||gun!=weapon||life!=generation){reset();owner=who;gun=weapon;life=generation;}
  if(current&&hands::valid(*current)){pose=*current;valid=true;}
  return valid;
 }
};
}
