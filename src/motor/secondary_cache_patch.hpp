#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <unordered_map>
namespace circle::motor::secondary_cache {
inline std::vector<std::uint32_t> patchHistoryDistance(std::span<const std::uint32_t> input) {
    if(input.size()<5||input[0]!=119734787u)return {};
    std::unordered_map<std::uint32_t,std::uint32_t> floats;
    std::size_t firstFunction=0, subtraction=0;unsigned count=0;
    for(std::size_t i=5;i<input.size();){
        const auto n=input[i]>>16,op=input[i]&65535;
        if(!n||i+n>input.size())return {};
        if(op==43&&n==4&&input[i+3]==0x3f800000)floats[input[i+2]]=input[i+1];
        if(op==54&&!firstFunction)firstFunction=i;
        if(op==131&&n==5&&floats.contains(input[i+4])&&floats.at(input[i+4])==input[i+1]){
            subtraction=i;++count;
        }
        i+=n;
    }
    // Audited modules have exactly one scalar subtraction with RHS float1:
    // previous secondary-cache validity. Never rewrite every constant1.
    if(count!=1||!firstFunction||subtraction<firstFunction)return {};
    std::vector<std::uint32_t> out(input.begin(),input.end());
    const auto id=out[3]++;
    const auto type=out[subtraction+1];
    out[subtraction+4]=id;
    out.insert(out.begin()+firstFunction,{(4u<<16)|43u,type,id,0x40000000u});
    return out;
}
}
