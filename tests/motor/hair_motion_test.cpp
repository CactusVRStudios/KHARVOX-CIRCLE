#include "hair_motion_patch.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <cstring>
using namespace circle::motor::hair_motion;
int main(int argc,char** argv){
    for(const auto& spec:auditedModules){
    std::vector<std::uint32_t> in={0x07230203,0x10000,0,4000,0,
        (3u<<16)|42u,56,spec.zero,(4u<<16)|61u,56,spec.condition,spec.pointer,
        (4u<<16)|250u,spec.condition,spec.yes,spec.no};
    auto out=patch(in,spec.hash);assert(out.size()==in.size());
    unsigned changes=0;for(unsigned i=0;i<in.size();++i)changes+=out[i]!=in[i];
    assert(changes==1&&out[out.size()-3]==spec.zero&&in[in.size()-3]==spec.condition);
    assert(patch(in,0).empty());auto bad=in;bad.back()=3700;assert(patch(bad,spec.hash).empty());
    bad=in;bad.push_back(0);assert(patch(bad,spec.hash).empty());
    bad=in;bad.insert(bad.end(),in.end()-4,in.end());assert(patch(bad,spec.hash).empty());
    }
    if(argc>1){
        std::ifstream f(argv[1],std::ios::binary);std::vector<char> b((std::istreambuf_iterator<char>(f)),{});
        assert(!b.empty()&&b.size()%4==0);std::uint64_t hash=14695981039346656037ull;
        for(unsigned char c:b){hash^=c;hash*=1099511628211ull;}
        std::vector<std::uint32_t>w(b.size()/4);std::memcpy(w.data(),b.data(),b.size());auto p=patch(w,hash);assert(!p.empty());
        if(argc>2){std::ofstream o(argv[2],std::ios::binary);o.write(reinterpret_cast<const char*>(p.data()),p.size()*4);}
    }
}
