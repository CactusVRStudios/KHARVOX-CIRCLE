#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace circle::motor {
// NRD 4.11 CommonSettings layout, checked against C175E0's writes.
using NrdSettings=std::array<unsigned char,0x1ac>;
struct NrdEyeHistory {
    NrdSettings previous{},applied{};
    std::uint32_t backend=0;
    bool valid=false,stereo=false;
    bool prepare(NrdSettings& s,std::uint32_t frame,bool isStereo,bool reset){
        if(valid&&frame==backend&&stereo==isStereo){s=applied;return s[0x1a4]!=0;}
        const bool restart=reset||!valid||stereo!=isStereo||frame-backend!=(isStereo?2u:1u);
        const auto current=s;
        const auto& p=restart?current:previous;
        std::memcpy(s.data()+0x40,p.data(),0x40);
        std::memcpy(s.data()+0xc0,p.data()+0x80,0x40);
        std::memcpy(s.data()+0x154,p.data()+0x14c,8);
        std::memcpy(s.data()+0x160,p.data()+0x15c,4);
        std::memcpy(s.data()+0x168,p.data()+0x164,4);
        if(restart)s[0x1a4]=2; // CLEAR_AND_RESTART, includes GPU history clear
        // Preserve native frameIndex: water's packed checkerboard inputs are
        // generated using that parity. Changing only the denoiser would desync it.
        previous=current;applied=s;backend=frame;stereo=isStereo;valid=true;
        return restart;
    }
};
}
