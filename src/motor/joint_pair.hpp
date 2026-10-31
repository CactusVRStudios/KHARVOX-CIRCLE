#pragma once
#include <array>
#include <cstdint>

namespace circle::motor {
// Native 9517F0 consumers, each verified to publish its current selection to
// the committed model manager's +150 joint-index array.
inline constexpr std::array<uintptr_t,6> jointCommitCallsites{
    0x87055a,0x899a6f,0x9477e8,0x94a166,0x94c99b,0x950cdf};
constexpr bool jointCommitCaller(uintptr_t returnRva){
    for(auto call:jointCommitCallsites)if(returnRva==call+5)return true;
    return false;
}
struct JointPairRecord {
    uintptr_t source{};
    uint64_t token{};
    unsigned render{};
    int slot{-1},current{},previous{};
    std::array<unsigned char,0x70> descriptor{};
    bool sameRender(uintptr_t p,uint64_t t,unsigned f,int s,const std::array<unsigned char,0x70>& d)const{
        return source&&source==p&&token&&token==t&&f==render&&slot==s&&descriptor==d;
    }
    bool matches(uintptr_t p,uint64_t t,unsigned f,int s,const std::array<unsigned char,0x70>& d)const{
        return source&&source==p&&token&&token==t&&f-render==1&&slot==s&&descriptor==d;
    }
};
}
