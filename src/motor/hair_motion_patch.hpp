#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace circle::motor::hair_motion {
inline constexpr std::uint64_t auditedFragment=0xa39cad8addc3c423ull;
struct AuditedModule { std::uint64_t hash; std::uint32_t zero,condition,pointer,yes,no; };
inline constexpr AuditedModule auditedModules[]={
    {auditedFragment,458,3688,3687,3689,3699},
    {0x58effbcec00dc285ull,477,3763,3762,3764,3774},
    {0x914b3f91e69fc307ull,477,3763,3762,3764,3774}
};
// Exact audited modules: select its native model/camera reprojection instead of
// strand screen-space history, which currently contains the opposite eye.
// This intentionally loses independent strand deformation velocity.
inline std::vector<std::uint32_t> patch(std::span<const std::uint32_t> in,std::uint64_t hash){
    const AuditedModule* spec=nullptr;
    for(const auto& candidate:auditedModules)if(candidate.hash==hash)spec=&candidate;
    if(!spec||in.size()<5||in[0]!=119734787u)return {};
    bool falseConstant=false,conditionLoad=false;std::size_t branch=0;unsigned matches=0;
    for(std::size_t i=5;i<in.size();){
        const auto n=in[i]>>16,op=in[i]&65535;
        if(!n||i+n>in.size())return {};
        if(op==42&&n==3&&in[i+1]==56&&in[i+2]==spec->zero)falseConstant=true;
        if(op==61&&n==4&&in[i+1]==56&&in[i+2]==spec->condition&&in[i+3]==spec->pointer)conditionLoad=true;
        if(op==250&&n==4&&in[i+1]==spec->condition&&in[i+2]==spec->yes&&in[i+3]==spec->no){branch=i;++matches;}
        i+=n;
    }
    if(!falseConstant||!conditionLoad||matches!=1)return {};
    std::vector<std::uint32_t> out(in.begin(),in.end());out[branch+1]=spec->zero;
    return out;
}
}
