#include "motor/gi_history.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
using namespace circle::motor;
int main(){
    GiSampleSequence sequence;
    unsigned visited[2]{},nativeVisited[2]{};
    for(unsigned pair=0;pair<16;++pair){
        for(unsigned eye=0;eye<2;++eye){
            const auto counter=101+pair*2+eye; // left can have odd parity
            const auto phase=sequence.before(1000+pair,true,counter);
            assert(phase==pair);
            assert(sequence.before(1000+pair,true,counter)==phase); // duplicate setup
            visited[eye]|=1u<<(phase&15u);nativeVisited[eye]|=1u<<(counter&15u);
        }
    }
    assert(visited[0]==0xffff&&visited[1]==0xffff);
    assert(nativeVisited[0]==0xaaaa&&nativeVisited[1]==0x5555);
    assert(sequence.before(0,false,164)==164); // mono keeps native sampling
    assert(sequence.before(2000,true,165)==0);
    assert(sequence.before(2000,true,166)==0);
    assert(sequence.before(2010,true,179)==1); // sparse pair ticks, no parity assumption
    assert(sequence.before(1,true,180)==0); // scene clock restart
    sequence.reset();assert(sequence.before(5,true,0xffffffffu)==0);
    assert(sequence.before(5,true,0)==0); // native counter wrap between eyes
    GiHistory h;GiCameraHistory left{},right{};left[0]=std::byte{11};right[0]=std::byte{22};
    auto p=h.before(0,true,100,{},0);assert(p.restart&&p.cut==2&&p.writeBank==0);
    p=h.before(1,true,101,left,1);assert(p.restart&&p.eye==1);
    p=h.before(0,true,102,right,1);assert(!p.restart&&p.camera==left&&p.writeBank==1&&p.cut==1);
    auto repeated=h.before(0,true,102,{},0);assert(repeated.camera==left&&repeated.writeBank==1);
    p=h.before(1,true,103,left,0);assert(!p.restart&&p.camera==right&&p.writeBank==1);
    p=h.before(0,true,104,right,0);assert(!p.restart&&p.writeBank==0&&p.cut==0);
    p=h.before(0,true,108,left,0);assert(p.restart&&p.cut==2);
    p=h.before(0,false,109,left,0);assert(p.restart);
    p=h.before(0,false,110,left,0);assert(!p.restart&&p.writeBank==1);
    p=h.before(1,true,111,left,0);assert(p.restart);
    h.reset();p=h.before(0,true,0xfffffffeu,left,0);assert(p.restart);
    p=h.before(1,true,0xffffffffu,left,1);assert(p.restart);
    p=h.before(0,true,0,right,1);assert(!p.restart&&p.camera==left);
    for(unsigned parity=0;parity<2;++parity)for(unsigned bank=0;bank<2;++bank){
        assert(GiHistory::bankForSlot(parity,parity,bank)==bank);
        assert(GiHistory::bankForSlot(parity^1u,parity,bank)==(bank^1u));
    }
}
