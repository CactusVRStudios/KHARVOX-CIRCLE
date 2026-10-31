#include "scatter_history.hpp"
#include <cstdlib>
#include <iostream>
using namespace circlevr::stereo_seq;
using namespace circle::motor;
void check(bool ok){if(!ok){std::cerr<<"Motor scatter history failure\n";std::exit(1);}}
void last(MotorScatterState& s,unsigned counter){memcpy(s.data()+0x10,&counter,4);}
unsigned last(const MotorScatterState& s){unsigned x{};memcpy(&x,s.data()+0x10,4);return x;}
int main(){
    const std::array<ScatterPair,2> l{{{(void*)1,(void*)2},{(void*)3,(void*)4}}};
    const std::array<ScatterPair,2> r{{{(void*)5,(void*)6},{(void*)7,(void*)8}}};
    MotorScatterHistory h;h.reset(l,r);check(h.ready());
    MotorScatterState s{};
    auto p=h.before(Eye::Left,100,s);check(p.load&&last(*p.load)==0);s=*p.load;
    auto first=p.slots;auto duplicate=h.before(Eye::Left,100,s);check(duplicate.slots==first&&!duplicate.load);
    // The simulated compute writes its counter and eye-specific camera tail.
    last(s,100);s[0x34]=std::byte{11};
    p=h.before(Eye::Right,101,s);check(p.load&&last(*p.load)==0&&p.slots[1]==r[1]);s=*p.load;last(s,101);s[0x34]=std::byte{22};
    p=h.before(Eye::Left,102,s);check(p.load&&last(*p.load)==0&&(*p.load)[0x34]==std::byte{11});s=*p.load;last(s,102);
    p=h.before(Eye::Right,103,s);check(p.load&&last(*p.load)==0&&(*p.load)[0x34]==std::byte{22});s=*p.load;last(s,103);
    p=h.before(Eye::Left,104,s);check(p.load&&last(*p.load)==103);s=*p.load;last(s,104);
    p=h.before(Eye::Right,105,s);check(p.load&&last(*p.load)==104&&(*p.load)[0x34]==std::byte{22});s=*p.load;last(s,105);
    // Resize invalidates both ping-pong reads, even without an eye switch.
    h.invalidate();p=h.before(Eye::Right,106,s);check(p.load&&last(*p.load)==0);
    // A gap / mono stretch must not consume stale right-eye volume contents.
    s=*p.load;last(s,106);p=h.before(Eye::Mono,120,s);check(p.load&&last(*p.load)==0&&p.restarted);
    s=*p.load;last(s,120);p=h.before(Eye::Right,130,s);check(p.load&&last(*p.load)==0&&p.restarted);
    h.reset(l,r);p=h.before(Eye::Right,1,{});check(p.load&&last(*p.load)==0);
    std::cout<<"Per-eye volume pairing, full Motor state, duplicate setup, clears, resize and gaps PASS\n";
}
