#include "temporal_continuity.hpp"
#include <cstdio>
int main(){
    circle::motor::TemporalContinuity c;int errors=0;
    auto check=[&](bool yes,const char* why){if(!yes){std::fprintf(stderr,"FAIL: %s\n",why);++errors;}};
    check(c.reset(0,500,1000)&&c.reset(1,300,1001),"first pair must initialize both histories");
    check(!c.reset(0,501,1002)&&!c.reset(1,301,1003),"continuous stereo must retain both histories");
    check(c.reset(0,602,1104),"left detects mono/menu interval");
    check(c.reset(1,302,1105),"right detects menu despite consecutive own-eye sequence");
    check(!c.reset(0,603,1106)&&!c.reset(1,303,1107),"resume settles into continuous histories");
    c.clear();check(c.reset(0,604,1108)&&c.reset(1,304,1109),"resize clears both histories");
    c.clear();c.reset(0,0xffffffffu,0xfffffffeu);
    check(!c.reset(0,0,0),"unsigned counter wrap does not falsely reset a continuous eye");
    return errors?1:0;
}
