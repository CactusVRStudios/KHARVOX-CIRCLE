#pragma once
#include "hand_pose.hpp"
#include <array>
#include <cstring>

namespace circle::motor::hands {
// Validated idLoadoutAttachmentComponent world muzzle, not a physics/model
// pointer. Retain the handle generation as well as the attachment identity.
struct NativeMuzzleSource {
    uintptr_t pool{},attachment{};
    uint64_t handle{UINT64_MAX},owner{};
};
template<class Read> bool readNativeMuzzle(const NativeMuzzleSource& source,Read&& read,Pose& result){
    if(!source.pool||!source.attachment||!source.owner||source.handle==UINT64_MAX)return false;
    const auto get=[&]<class T>(uintptr_t address,T& value){return read(address,&value,sizeof(value));};
    uintptr_t data{},verifyData{};uint64_t count{},verifyCount{},owner{},handle{};
    std::array<unsigned char,2> hidden{};
    if(!get(source.attachment,owner)||owner!=source.owner||
       !get(source.attachment+0x100,handle)||handle!=source.handle||
       !get(source.attachment+0x201,hidden)||hidden[0]||hidden[1]||
       !get(source.pool,data)||!get(source.pool+8,count)||!data||count>10000000)return false;
    const uint64_t index=handle&((uint64_t{1}<<24)-1),offset=(handle>>40)&0x3ff;
    if(index+offset>=count||data>UINTPTR_MAX-count*32)return false;
    uint16_t generation{},verifyGeneration{};
    if(!get(data+index*32+12,generation)||generation!=uint16_t(handle>>24))return false;
    std::array<unsigned char,32> bytes{},verify{};
    if(!get(data+(index+offset)*32,bytes)||!get(data+(index+offset)*32,verify)||bytes!=verify||
       !get(source.pool,verifyData)||data!=verifyData||!get(source.pool+8,verifyCount)||count!=verifyCount||
       !get(data+index*32+12,verifyGeneration)||generation!=verifyGeneration||
       !get(source.attachment+0x100,handle)||handle!=source.handle||
       !get(source.attachment,owner)||owner!=source.owner)return false;
    Pose pose;
    std::memcpy(&pose.position,bytes.data(),sizeof(Vec));
    std::memcpy(&pose.orientation,bytes.data()+16,sizeof(Quat));
    if(!valid(pose))return false;
    result=pose;return true;
}
}
