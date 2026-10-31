#include "temporal_target.hpp"
#include <cstdio>
int main(){
    using namespace circle::motor;
    unsigned failures{};auto check=[&](bool ok,const char* msg){if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",msg);}};
    for(int width:{2496,1248}){
        TemporalTargetSize own{width,2688,width,2688};
        // Measured in the failing cinema: equal allocations, unequal active
        // heights. Exercise both ping-pong slots, opaque and high targets.
        for(int height:{1404,2688,1404,2688})for(unsigned slot=0;slot<3;++slot){
            const TemporalTargetSize native{width,2688,width,height};
            const auto region=temporalActiveSize(native,own);
            check(region&&(*region)[0]==width&&(*region)[1]==height,"cinema/gameplay rectangle follows native frame");
            own[2]=(*region)[0];own[3]=(*region)[1];
            check(own[0]==width&&own[1]==2688,"allocation dimensions stay intact");
        }
        check(!temporalActiveSize({width,2688,width,0},own),"zero region rejected");
        check(!temporalActiveSize({width,2688,width,2689},own),"out-of-allocation region rejected");
        check(!temporalActiveSize({width,2688,width+1,1404},own),"oversize width rejected");
        check(!temporalActiveSize({width,1404,width,1404},own),"storage resize must finish before reuse");
        check(!temporalActiveSize({},own),"missing native target rejected");
    }
    return failures?1:0;
}
