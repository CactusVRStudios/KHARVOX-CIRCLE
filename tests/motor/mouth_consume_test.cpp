#include "mouth_consume.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
using namespace circle::motor::hands;
int main(){
 for(unsigned category=0;category<19;++category)assert(edibleCategory(category)==(category==1||category==2));
 const Pose head{{1,2,3},{0,0,0,1}};const Vec mouth=head.position+Vec{0,-.10f,-.10f};
 assert(mouthReach(head,mouth)<.0001f);
 const Pose turn{{-7,3,12},{0,.70710678f,0,.70710678f}};
 assert(mouthReach(compose(turn,head),compose(turn,{mouth,{}}).position)<.0001f);
 assert(mouthReach(head,head.position+Vec{0,-.1f,.2f})>1.55f);
 assert(mouthReach({{}, {0,0,0,0}}, {})<0);
 assert(mouthReach(head,{std::numeric_limits<float>::quiet_NaN(),0,0})<0);
 MouthConsume state;uint64_t now=1000,pose=1,item=10,owner=1,session=1,life=1;
 auto step=[&](float reach,bool safe=true,bool manual=false){now+=11;++pose;return state.update(owner,item,session,life,now,pose,reach,safe,manual);};
 // Loading at the face never consumes; withdrawing then entering needs only
 // one new sample (11 ms here), not a 150 ms arming wait or 250 ms dwell.
 for(int i=0;i<40;++i)assert(!step(.5f));
 assert(!step(2));assert(step(.5f));
 for(int i=0;i<54;++i)assert(step(.5f));
 for(int i=0;i<100;++i)assert(!step(.5f));
 // Hysteresis band cannot rearm. A real withdrawal can immediately rearm.
 assert(!step(1.2f));assert(!step(.5f));assert(!step(2));assert(step(.5f));
 assert(!step(2));assert(!step(2));assert(step(.5f));
 // Manual RT takes priority; release at the mouth cannot trigger another bite.
 assert(!step(.5f,true,true));for(int i=0;i<60;++i)assert(!step(.5f));
 assert(!step(2));assert(step(.5f));
 assert(!step(.5f,false));for(int i=0;i<60;++i)assert(!step(.5f));
 for(int change=0;change<4;++change){
  assert(!step(2));
  if(change==0)++item;if(change==1)++owner;if(change==2)++session;if(change==3)++life;
  for(int i=0;i<20;++i)assert(!step(.5f));
 }
 assert(!step(2));now+=200;
 for(int i=0;i<20;++i)assert(!step(.5f)); // No action across a stall.
 assert(!step(2));pose=1;
 for(int i=0;i<20;++i)assert(!step(.5f)); // XR time discontinuity.
 assert(!step(2));
 now+=11;assert(!state.update(owner,item,session,life,now,pose,.5f,true,false));
 assert(step(.5f)); // Reused pose did not trigger; a new pose does.
 // Equipping away from face arms without extra timer or extra outside sample.
 ++item;assert(!step(2));assert(step(.5f));
}
