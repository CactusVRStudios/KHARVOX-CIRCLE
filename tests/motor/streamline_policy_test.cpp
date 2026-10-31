#include "streamline_policy.hpp"
#include "frame_generation.hpp"
#include "cinema_timing.hpp"
#include <cstdio>
int main(){
    circle::motor::StreamlineHistory h;int errors=0;
    auto check=[&](bool b,const char* why){if(!b){++errors;std::fprintf(stderr,"FAIL: %s\n",why);}};
    using namespace circle::motor;
    check(cinema_timing::adaptiveCinema(true,false,1,1.f),"cinema can select native adaptive clock");
    check(!cinema_timing::adaptiveCinema(true,true,1,1.f)&&!cinema_timing::adaptiveCinema(false,false,1,1.f),"pause and stale cinema cannot override clock");
    check(!cinema_timing::adaptiveCinema(true,false,1,.5f)&&!cinema_timing::adaptiveCinema(true,false,3,1.f),"time scaling and unknown mode untouched");
    check(cinema_timing::targetRate(1000,72)==72&&cinema_timing::targetRate(60,90)==90&&cinema_timing::targetRate(1000,0)==1000,"cinema follows headset refresh instead of legacy 60Hz bound");
    check(cinema_timing::targetRate(30,90)==30&&cinema_timing::targetRate(60,0)==60,"low special clock and unavailable refresh retained");
    StreamlineTransitionRecovery recovery;
    for(unsigned n=0;n<3;++n){
        recovery.transient(100+n,10+n);
        check(!recovery.retry(100+n,20+n),"same native token cannot retry");
        check(!recovery.retry(200+n,10+n),"same simulation cannot retry");
        check(recovery.retry(200+n,20+n),"new token and simulation recover transient duplicate");
        check(!recovery.retry(300+n,30+n),"recovery is consumed once");
    }
    recovery.transient(400,40);check(!recovery.retry(500,50),"persistent duplicate fails closed after three retries");
    for(int n=0;n<240;++n)recovery.success();
    recovery.transient(600,60);check(recovery.retry(700,70),"stable rendering rearms later scene transition");
    recovery.transient(800,80);recovery.cancel();check(!recovery.retry(900,90),"explicit AA off cancels recovery");
    for(uintptr_t flags=0;flags<0x1000;++flags){
        const auto off=frame_generation::nativeOffFlags(flags);
        check(frame_generation::gateFlags(flags,true)==flags,"Mono FG leaves native eligibility and options flags untouched");
        check(frame_generation::gateFlags(flags,false)==off,"Stereo retains native FG off path");
        check(bool(off&0x80)!=bool(off&0x800),"FG guard takes native signed-less off branch");
        check((off&~uintptr_t(0x880))==(flags&~uintptr_t(0x880)),"FG guard preserves unrelated CPU flags");
    }
    // Reproduce flat-cinema size failure, then resume on a new simulation/token.
    StreamlineTransitionRecovery resizeRecovery;
    StreamlineResizeGrace resizeGrace;
    for(unsigned attempt=0;attempt<4;++attempt){
        for(unsigned pass=0;pass<4;++pass)check(resizeGrace.defer(),"resize grace before fallback");
        check(!resizeGrace.defer(),"fifth size failure enters fallback");
        resizeRecovery.transient(1000+attempt,2000+attempt);
        check(!resizeRecovery.retry(1000+attempt,3000+attempt),"no size retry on failed token");
        const bool retried=resizeRecovery.retry(4000+attempt,3000+attempt);
        check(retried==(attempt<3),"size recovery bounded to three attempts");
        if(retried)resizeGrace.succeeded();
    }
    StreamlineResizeGrace grace;
    for(int i=0;i<4;++i)check(grace.defer(),"bounded resize transition retains DLSS selection");
    check(!grace.defer(),"persistent invalid dimensions still fail closed");
    grace.succeeded();check(grace.defer(),"successful evaluation rearms subsequent resolution change");
    std::array<StreamlineResizeGrace,2> eyes;
    for(int i=0;i<4;++i){check(eyes[1].defer(),"right-eye resize grace");eyes[0].succeeded();}
    check(!eyes[1].defer(),"left success cannot hide persistent right-eye failure");
    StreamlineConstantKey left{123,10,20,0},right{123,10,21,1};
    check(!reusableStreamlineConstants(true,left,right),"same native frame token must not suppress right-eye constants");
    check(reusableStreamlineConstants(true,left,left),"same eye/frame/token constants can be reused");
    check(!reusableStreamlineConstants(false,left,left),"failed API submission is never reusable");
    check(!reusableStreamlineConstants(true,left,{123,10,22,0}),"repeated simulation still updates new render frame");
    check(!reusableStreamlineConstants(true,left,{124,10,20,0}),"token replacement requires submission");
    check(!reusableStreamlineConstants(true,left,{123,11,20,0}),"new simulation cannot reuse wrapped backend record");
    check(!reusableStreamlineConstants(true,{0,10,20,0},{0,10,20,0}),"null token cannot establish ownership");
    check(supportedStreamlinePass(0,1)&&supportedStreamlinePass(rrFeature,1)&&supportedStreamlinePass(rrFeature,2),"SR and both RR passes supported");
    check(!supportedStreamlinePass(0,0)&&!supportedStreamlinePass(0,2)&&!supportedStreamlinePass(1000,1)&&!supportedStreamlinePass(rrFeature,3),"FG and unowned native views are not intercepted");
    check(eyeViewport(1,0)==1&&eyeViewport(1,1)==3&&eyeViewport(2,0)==2&&eyeViewport(2,1)==4,"all four native/eye views have distinct identities");
    check(streamlineResourceSlot(0,1)!=streamlineResourceSlot(rrFeature,1)&&streamlineResourceSlot(rrFeature,1)!=streamlineResourceSlot(rrFeature,2),"feature lifetimes cannot alias at shared viewport 3");
    std::array<StreamlineHistory,2> passes;
    for(auto& pass:passes){pass.reset(0,1,2,100,200,false);pass.reset(1,1,3,100,200,false);}
    passes[1].invalidate();
    check(!passes[0].reset(0,2,4,100,200,false)&&!passes[0].reset(1,2,5,100,200,false),"secondary resize preserves main histories");
    check(passes[1].reset(0,2,4,100,200,false)&&passes[1].reset(1,2,5,100,200,false),"secondary release resets both secondary eyes");
    check(h.reset(0,10,20,100,200,false)&&h.reset(1,10,21,100,200,false),"first L/R initialize independently");
    check(!h.reset(0,11,22,100,200,false)&&!h.reset(1,11,23,100,200,false),"continuous pair retains histories");
    check(h.reset(0,12,24,100,200,true)&&h.reset(1,12,25,100,200,false),"native camera cut resets both eyes");
    check(!h.reset(0,13,26,100,200,false)&&!h.reset(1,13,27,100,200,false),"cut reset does not repeat");
    check(h.reset(0,14,28,50,100,false)&&h.reset(1,14,29,50,100,false),"quality/size change resets both");
    check(h.reset(1,15,49,50,100,false),"menu gap resets despite consecutive eye sequence");
    h.invalidate();check(h.reset(0,15,30,50,100,false)&&h.reset(1,16,51,50,100,false),"resource release invalidates both contexts");
    return errors?1:0;
}
