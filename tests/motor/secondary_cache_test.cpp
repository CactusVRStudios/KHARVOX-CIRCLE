#include "secondary_cache_patch.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <cstring>
using namespace circle::motor::secondary_cache;
int main(int argc,char** argv){
    // Minimal instruction stream for structural surgery, not an executable shader.
    std::vector<std::uint32_t> input={119734787u,0x10000,0,20,0,
        (4u<<16)|43u,1,2,0x3f800000,
        (5u<<16)|54u,1,3,0,4,
        (5u<<16)|131u,1,5,6,2};
    const auto original=input;const auto result=patchHistoryDistance(input);
    assert(input==original&&result.size()==input.size()+4&&result[3]==21);
    assert(result[9]==((4u<<16)|43u)&&result[11]==20&&result[12]==0x40000000);
    assert(result.back()==20);
    auto bad=input;bad.back()=7;assert(patchHistoryDistance(bad).empty());
    bad=input;bad.push_back(0);assert(patchHistoryDistance(bad).empty());
    bad=input;bad.insert(bad.end(),input.end()-5,input.end());assert(patchHistoryDistance(bad).empty());
    // Three buffers retain the previous same-eye state until its consumer.
    std::uint32_t slots[3]={};
    for(unsigned frame=10;frame<100;++frame){
        const auto current=frame%3,previous=(frame%3+1)%3;
        assert(current!=previous);
        if(frame>=12)assert(slots[previous]==frame-2);
        slots[current]=frame;
    }
    for(int n=1;n<argc;++n){
        std::ifstream file(argv[n],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
        assert(!bytes.empty()&&bytes.size()%4==0);
        std::vector<std::uint32_t> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());
        const auto patched=patchHistoryDistance(words);assert(!patched.empty());
    }
}
