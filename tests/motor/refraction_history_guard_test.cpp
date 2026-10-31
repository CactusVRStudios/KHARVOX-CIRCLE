#include "refraction_history_guard.hpp"
#include <cstdio>

int main(){
    using namespace circle::motor;
    using S=RefractionHistoryStatus;
    unsigned failures{};
    auto check=[&](bool ok,const char* why){if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}};
    RefractionHistoryGuard h;
    check(h.observe(0,2,{100,10,5,0,600,640},200)==S::Unknown,"uninitialized history is not safe");
    check(h.observe(2,0,{200,11,5,1,600,640},100)==S::OtherEye,"right must not blend left from previous native render");
    check(h.observe(0,2,{100,12,6,0,600,640},200)==S::OtherEye,"left must not blend right despite stable parity");
    check(h.observe(2,0,{200,13,6,1,600,640},100)==S::OtherEye,"peer ownership persists over repeated stereo pairs");
    h.clear();
    h.observe(0,2,{100,10,5,0,600,640},200);
    check(h.observe(2,0,{200,11,6,0,600,640},100)==S::SameEye,"consecutive mono-eye producer can retain history");
    check(h.observe(0,2,{100,13,7,0,600,640},200)==S::SameEye,"per-eye cadence allows a peer render between own frames");
    check(h.observe(2,0,{200,15,8,0,600,640},100)==S::SameEye,"repeated valid same-eye cadence");
    check(h.observe(0,2,{100,20,9,0,600,640},200)==S::Gap,"stale render after absent refraction rejected");
    check(h.observe(2,0,{200,21,9,0,600,640},100)==S::Gap,"duplicate eye sequence cannot impersonate prior frame");
    check(h.observe(0,2,{100,22,10,0,800,900},200)==S::Resized,"resolution change rejects incompatible history");
    check(h.observe(2,0,{200,23,11,0,800,900},101)==S::Unknown,"reused slot with different allocation rejected");
    h.clear();
    h.observe(0,2,{100,10,5,0,600,640},200);
    check(h.observe(3,1,{400,11,5,1,600,640},300)==S::Unknown,"update one cannot borrow update zero ownership");
    check(h.observe(2,0,{200,12,6,0,600,640},100)==S::SameEye,"independent update does not corrupt earlier slot");
    h.clear();
    check(h.observe(0,2,{100,14,7,0,600,640},200)==S::Unknown,"context retirement or untagged view invalidates ownership");
    check(h.observe(0,0,{100,15,8,0,600,640},100)==S::Unknown,"read-write alias never retained");
    check(h.observe(2,0,{200,16,9,0,600,640},100)==S::Unknown,"invalid mapping clears previous ownership");
    h.clear();
    h.observe(0,2,{100,0xffffffffu,0xffffffffu,0,600,640},200);
    check(h.observe(2,0,{200,0,0,0,600,640},100)==S::SameEye,"unsigned counter wrap preserves valid history");
    return failures?1:0;
}
