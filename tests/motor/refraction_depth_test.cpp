#include "refraction_depth.hpp"
#include <cstdio>
#include <limits>

int main(){
    using namespace circle::motor;
    unsigned failures{};
    auto check=[&](bool ok,const char* why){if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}};
    const std::array<float,6> box{-1,-2,-3,1,2,3};
    check(refractionViewDepth(box,{0,0,1,12},8192)==12,"physical distance survives zeroed sort-depth bits");
    check(refractionViewDepth({2,0,0,4,2,2},{1,0,0,5},8192)==8,"rotated model/eye uses W row and bound centre");
    check(refractionViewDepth(box,{0,0,1,-2},8192)==0,"bounds crossing/behind eye are retained at zero distance");
    check(refractionViewDepth(box,{0,0,1,9000},8192)==8192,"far range is bounded");
    check(!refractionViewDepth(box,{0,0,0,1},8192),"identity fallback cannot masquerade as perspective depth");
    check(!refractionViewDepth({2,0,0,1,2,2},{1,0,0,5},8192),"invalid bounds rejected");
    check(!refractionViewDepth(box,{0,0,1,std::numeric_limits<float>::quiet_NaN()},8192),"nonfinite transform rejected");
    check(!refractionViewDepth(box,{0,0,1,3},0),"invalid far plane rejected");
    check(refractionDepthSeparation(2-12)==10&&refractionDepthSeparation(12-2)==10,
          "stable draw order must split distant refraction layers in either depth direction");
    check(refractionDepthSeparation(3-4)<7,"nearby surfaces may still share a native source");
    RefractionDepthCache cache;
    RefractionDepthOwner left{1,2,3,100,10,0};
    check(cache.begin(left,4),"valid captured view begins batch");
    constexpr auto key=0x8a00000000800001ull;
    check(!cache.get(left,0,key),"uncaptured geometry never invents a depth");
    check(cache.put(0,key,12)&&cache.get(left,0,key)==12,"same draw gets physical depth without sorting list");
    auto right=left;right.eye=1;
    check(!cache.get(right,0,key),"peer-eye depth rejected");
    auto next=left;++next.backend;
    check(!cache.get(next,0,key),"recycled context in later backend rejected");
    next=left;++next.tick;
    check(!cache.get(next,0,key),"reused backend with new simulation tick rejected");
    next=left;++next.entries;
    check(!cache.get(next,0,key),"different draw list rejected");
    check(!cache.get(left,0,key+1),"reused index with changed key rejected");
    check(!cache.put(4,key,1),"native list bounds enforced");
    check(cache.begin(right,4)&&!cache.get(right,0,key),"new invocation erases previous samples");
    check(!cache.begin(left,RefractionDepthCache::maxSurfaces+1),"corrupt native count rejected");
    check(!cache.get(left,0,key),"rejected begin invalidates previous owner");
    return failures?1:0;
}
