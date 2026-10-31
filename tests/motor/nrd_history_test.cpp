#include "nrd_history.hpp"
#include <cstdlib>
#include <iostream>
using namespace circle::motor;
void check(bool ok){if(!ok){std::cerr<<"NRD eye history failure\n";std::exit(1);}}
NrdSettings frame(unsigned char mark){NrdSettings s{};s[0]=mark;s[0x80]=mark+1;s[0x14c]=mark+2;s[0x15c]=mark+3;s[0x164]=mark+4;s[0x1a0]=mark+5;return s;}
int main(){
    NrdEyeHistory l,r;
    auto a=frame(10),b=frame(40);
    check(l.prepare(a,100,true,false));check(r.prepare(b,101,true,false));
    check(a[0x40]==10&&b[0x40]==40&&a[0x1a4]==2&&b[0x1a4]==2);
    auto c=frame(11),d=frame(41);
    check(!l.prepare(c,102,true,false));check(!r.prepare(d,103,true,false));
    check(c[0x40]==10&&d[0x40]==40&&d[0xc0]==41&&d[0x154]==42&&d[0x160]==43&&d[0x168]==44);
    check(d[0x1a0]==46); // tracing/checkerboard counter is untouched
    auto duplicate=frame(99);check(!r.prepare(duplicate,103,true,false));check(duplicate==d);
    auto gap=frame(55);check(r.prepare(gap,111,true,false));check(gap[0x40]==55);
    auto mono=frame(60);check(l.prepare(mono,112,false,false));
    auto next=frame(61);check(!l.prepare(next,113,false,false));check(next[0x40]==60);
    auto cut=frame(70);check(l.prepare(cut,114,false,true));check(cut[0x40]==70&&cut[0x1a4]==2);
    auto reentry=frame(80);check(l.prepare(reentry,116,true,false));
    auto wrap=frame(90);l.prepare(wrap,0xffffffffu,true,false);wrap=frame(91);check(!l.prepare(wrap,1,true,false));
}
