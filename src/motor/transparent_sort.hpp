#pragma once
#include <cstdint>

namespace circle::motor {
// Permission is renewed by a successful optical latch, not by a material name
// or the address of a view reused from an older render. Caller owns the lock.
struct TransparentSortScope {
    std::uintptr_t view{};
    std::uint64_t token{};
    std::uint32_t frame{};
    unsigned eye{};
    void note(std::uintptr_t v,std::uint64_t t,std::uint32_t f,unsigned e,bool gameplayStereo){
        *this={};
        if(gameplayStereo&&v&&t&&e<2)*this={v,t,f,e};
    }
    bool matches(std::uintptr_t v,std::uint64_t t,std::uint32_t f,unsigned e,int viewIndex)const{
        return view&&token&&v==view&&t==token&&f==frame&&e==eye&&viewIndex==0;
    }
};
}
