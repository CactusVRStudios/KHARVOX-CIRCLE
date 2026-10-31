#include "adaptive_policy.hpp"
#include <new>
namespace {
using namespace circle::fg;
void* create(){return new(std::nothrow) FractionalFramegen;}
void destroy(void* p){delete static_cast<FractionalFramegen*>(p);}
AdaptiveDecision sample(void* p,const AdaptiveSample* s){return p&&s?static_cast<FractionalFramegen*>(p)->sample(*s):AdaptiveDecision{};}
void output(void* p,double now,double period,uint32_t fresh,double display){if(p)static_cast<FractionalFramegen*>(p)->output(now,period,fresh!=0,display);}
uint32_t plan(void* p,double interval,double period,uint32_t continuous){return p?static_cast<FractionalFramegen*>(p)->plan(interval,period,continuous!=0):1;}
void suspend(void* p,double now){if(p)static_cast<FractionalFramegen*>(p)->suspend(now);}
const AdaptiveApi api{sizeof(AdaptiveApi),AdaptiveAbi,create,destroy,sample,output,plan,suspend};
}
extern "C" __declspec(dllexport) const circle::fg::AdaptiveApi* CircleAdaptiveFgGetApi(uint32_t version){return version==circle::fg::AdaptiveAbi?&api:nullptr;}
