#include "material_history.hpp"
#include "transparent_sort.hpp"
#include <cstdio>

int main() {
    using namespace circle::motor;
    unsigned failures{};
    auto check=[&](bool ok,const char* message) {
        if (!ok) { ++failures; std::fprintf(stderr,"FAIL: %s\n",message); }
    };
    MaterialHistoryBinding binding;
    MaterialHistoryKey left{0x100,0x200,0x300,12,1,21,4,0};
    auto right=left; right.backend++; right.eye=1;
    check(binding.take(left)==0,"no history without selection");
    binding.note(left,0x400);
    check(binding.take(left)==0x400,"left job receives selected left image");
    check(binding.take(left)==0,"binding consumed exactly once");
    binding.note(left,0x400);
    check(binding.take(right)==0,"left history cannot leak into right job");
    check(binding.take(left)==0,"mismatch discards stale selection");
    binding.note(right,0x500);
    check(binding.take(right)==0x500,"right job receives selected right image");
    for (unsigned change=0;change<8;++change) {
        auto next=left;
        switch(change) {
        case 0: ++next.view; break;
        case 1: ++next.command; break;
        case 2: ++next.device; break;
        case 3: ++next.tick; break;
        case 4: ++next.generation; break; // resize/context replacement
        case 5: ++next.backend; break;
        case 6: ++next.eyeSequence; break; // repeated simulation token
        case 7: ++next.eye; break;
        }
        binding.note(left,0x400);
        check(binding.take(next)==0,"all job and lifetime identity fields must match");
    }
    binding.note(left,0);
    check(binding.take(left)==0,"missing image retains native binding");
    auto menu=left;menu.tick=0;
    binding.note(menu,0x400);
    check(binding.take(menu)==0,"untagged menu cannot borrow gameplay history");
    binding.note(left,0x400);binding.clear();
    check(binding.take(left)==0,"retirement or disabled correction clears pending selection");
    TransparentSortScope sort;
    check(!sort.matches(0x100,12,21,0,0),"no sorting override before a validated optical latch");
    sort.note(0x100,12,21,0,true);
    check(sort.matches(0x100,12,21,0,0),"current left primary view gets stable-order fallback");
    check(!sort.matches(0x100,12,21,1,0),"opposite eye cannot borrow sorting scope");
    check(!sort.matches(0x100,12,22,0,0),"reused view in a later render is not sufficient");
    check(!sort.matches(0x100,13,21,0,0),"reused frame with a different simulation token rejected");
    check(!sort.matches(0x200,12,21,0,0),"secondary view pointer rejected");
    check(!sort.matches(0x100,12,21,0,1),"reflection/shadow view index retains native sorting");
    sort.note(0x100,12,22,1,true);
    check(sort.matches(0x100,12,22,1,0),"same native view can be renewed for right eye");
    check(!sort.matches(0x100,12,21,0,0),"left scope expires at the right optical latch");
    sort.note(0x100,13,23,0,false);
    check(!sort.matches(0x100,13,23,0,0),"cinema/mono/native view clears fallback");
    for(unsigned invalid=0;invalid<3;++invalid){
        sort.note(invalid==0?0:0x100,invalid==1?0:12,21,invalid==2?2:0,true);
        check(!sort.matches(0x100,12,21,0,0),"invalid latch clears prior eligibility");
    }
    return failures?1:0;
}
