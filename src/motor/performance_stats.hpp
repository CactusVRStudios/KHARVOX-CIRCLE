#pragma once
#include <cstdint>

namespace circle::motor {
// Worker-thread telemetry. An accepted xrEndFrame is not proof of physical
// scanout. Count fresh game images by transport identity, not XR loop ticks.
struct PerformanceStats {
    struct Rates { double produced=0, fresh=0, submissions=0, repeats=0; bool valid=false; double output=0; } rates;
    double start=-1, lastFresh=-1;
    std::uint64_t producedBase=0, lastImage=0, fresh=0, submissions=0, repeated=0, output=0;
    void reset(double now, std::uint64_t produced) {
        *this={};start=now;producedBase=produced;
    }
    void sample(double now,std::uint64_t produced,bool accepted,bool gameImage,std::uint64_t image,bool generated=false) {
        if(start<0 || now<start || produced<producedBase)reset(now,produced);
        if(accepted && gameImage && image) {
            ++submissions;
            if(image!=lastImage){++output;lastImage=image;if(!generated){++fresh;lastFresh=now;}}
            else ++repeated;
        }
        const double dt=now-start;
        if(dt>=1.0) {
            rates={double(produced-producedBase)/dt,double(fresh)/dt,double(submissions)/dt,
                submissions?100.0*double(repeated)/double(submissions):0.0,true,double(output)/dt};
            start=now;producedBase=produced;fresh=submissions=repeated=output=0;
        }
    }
    double imageAgeMs(double now)const{return lastFresh<0?-1:1000.0*(now-lastFresh);}
};
}
