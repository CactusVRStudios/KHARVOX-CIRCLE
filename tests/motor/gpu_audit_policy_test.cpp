#include "gpu_audit_policy.hpp"
#include <cassert>
using namespace circle::motor::gpu_audit;
int main(){
    auto crop=imageCrop(2248,2416,.5f,.5f);
    assert(crop.x==868&&crop.y==952&&crop.width==512&&crop.height==512);
    crop=imageCrop(100,80,1,0);
    assert(crop.x==0&&crop.y==0&&crop.width==100&&crop.height==80);
    assert(!imageCrop(0,80,.5f,.5f).width);
    assert(!imageCrop(100,80,-1,.5f).width);
    assert(!imageCrop(100,80,NAN,.5f).width);
    PairWindow p;
    assert(!p.accept(20,11,1)&&p.token==0);
    assert(p.accept(20,12,0)&&p.accept(20,13,1));
    assert(!p.accept(20,14,0)&&!p.accept(21,13,1)&&!p.accept(20,12,1));
    assert(targetedRange(64,65536)&&!targetedRange(0,65540));
    assert(!targetedRange(3,4)&&!targetedRange(4,3)&&!targetedRange(~std::uint64_t{}-3,8));
    const auto full=range(100,4,48,64);
    assert(full.valid&&full.complete&&full.offset==4&&full.size==48);
    assert(!range(100,101,8,64).valid);
    assert(!range(100,3,8,64).valid);
    assert(!range(100,96,8,64).valid);
    assert(!range(100,0,0,64).valid);
    const auto all=range(100,4,~std::uint64_t{},200);
    assert(all.complete&&all.size==96);
    const auto bounded=range(100,0,100,32);
    assert(bounded.valid&&!bounded.complete&&bounded.size==32);
    const auto odd=range(101,0,101,200);
    assert(odd.valid&&!odd.complete&&odd.size==100);
    assert(!range(100,~std::uint64_t{}-2,10,64).valid);
    Budget b;
    assert(b.reserve(Budget::maximum-4));
    assert(!b.reserve(8)&&b.used==Budget::maximum-4);
    assert(b.reserve(4)&&!b.reserve(4));
}
