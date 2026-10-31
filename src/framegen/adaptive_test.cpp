#include "adaptive_api.hpp"
#include "pacing_policy.hpp"
#include "policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <deque>
using namespace circle::fg;
void check(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
int main(){
 check(!CircleAdaptiveFgGetApi(1)&&!CircleAdaptiveFgGetApi(2)&&!CircleAdaptiveFgGetApi(3)&&!CircleAdaptiveFgGetApi(4)&&!CircleAdaptiveFgGetApi(999),"reject old ABI");
 const auto api=CircleAdaptiveFgGetApi(AdaptiveAbi);check(api&&api->size==sizeof(AdaptiveApi),"DLL ABI");
 for(double hz:{72.,90.,120.})for(unsigned tenth=10;tenth<=40;++tenth){
  auto p=api->create();check(p!=nullptr,"create");
  const double period=1000/hz,interval=period*tenth*.1;
  AdaptiveSample s{1000,period,16.,3.,interval,4,0,1};api->sample(p,&s);
  unsigned sum=0;
  for(unsigned n=0;n<1000;++n){const auto count=api->plan(p,interval,period,1);sum+=count;check(count>=1&&count<=4,"output ceiling");check(std::abs(double(sum)-(n+1)*tenth*.1)<=1.25001,"bounded causal fractional timing residual");}
  check(std::abs(double(sum)-tenth*100)<=1,"output ratio within one deferred slot, including 1x through 2x");api->destroy(p);
 }
 auto p=api->create();AdaptiveSample s{1000,1000./90,16.7,3.,1000./60,4,0,1};
 auto d=api->sample(p,&s);check(d.tenths==15,"60 real at 90Hz needs 1.5x");
 unsigned real=0,synthetic=0;
 for(unsigned i=0;i<600;++i){s.nowMs+=s.realIntervalMs;s.renderMs=i%2?80:1;s.generatedMs=i%2?30:1;
  d=api->sample(p,&s);auto count=api->plan(p,s.realIntervalMs,s.periodMs,1);++real;synthetic+=count-1;
  check(d.tenths==15,"cost spikes cannot throttle actual real cadence");
 }
 check(real==600&&synthetic==300,"preserve 60 real and fill 30 synthetic per second");
 // Genuine 40FPS workload still needs 90 output slots, even though our own
 // producer waits have already been removed at matched publication boundaries.
 s.pendingPairs=1;s.realIntervalMs=1000./40;api->sample(p,&s);
 unsigned filled=0;api->plan(p,0,s.periodMs,0);
 for(unsigned i=0;i<400;++i)filled+=api->plan(p,s.realIntervalMs,s.periodMs,1);
 check(std::abs(double(filled)-900)<=1,"40 real FPS fill 90Hz without inventing a higher rendering capacity");
 check(std::abs(unblockedInterval(33.333,16.666)-16.667)<.0001,"same-publication wait subtraction preserves real work");
 check(unblockedInterval(20,-1)==0&&unblockedInterval(20,30)==0,"invalid/backward wait counters cannot manufacture demand");
 s.pendingPairs=3;api->sample(p,&s);
 for(unsigned i=0;i<200;++i)check(api->plan(p,80,s.periodMs,1)==1,"full queue drains real frames; no wait-induced 4x ratchet");
 s.pendingPairs=1;s.realIntervalMs=1000./60;api->sample(p,&s);
 auto freshPlan=api->create();api->sample(freshPlan,&s);
 for(unsigned i=0;i<8;++i)check(api->plan(p,s.realIntervalMs,s.periodMs,1)==api->plan(freshPlan,s.realIntervalMs,s.periodMs,1),"queue drain discards interpolation debt and resumes like clean history");
 api->destroy(freshPlan);
 api->plan(p,0,s.periodMs,0);filled=0;
 for(unsigned i=0;i<600;++i)filled+=api->plan(p,i%2?1000./30-5:5,s.periodMs,1);
 check(std::abs(double(filled)-900)<=1,"publication jitter credits fast real pairs instead of overgenerating");
 // Restore the reported interval after the deliberately slow trace.
 for(unsigned i=0;i<200;++i)api->sample(p,&s);
 // Output loss is diagnosed, not converted into a larger real-frame divisor.
 for(unsigned i=0;i<200;++i){s.nowMs+=s.periodMs;api->output(p,s.nowMs,s.periodMs,0,s.nowMs);d=api->sample(p,&s);}
 check(d.tenths==15,"missed callbacks cannot manufacture demand by slowing producer");
 check(api->plan(p,16.7,s.periodMs,0)==1,"history gap yields real frame and resets residual");
 check(api->plan(p,std::numeric_limits<double>::quiet_NaN(),s.periodMs,1)==1,"invalid interval real fallback");
 s.maximum=2;s.realIntervalMs=80;api->sample(p,&s);check(api->plan(p,80,s.periodMs,1)==2,"backend ceiling");
 api->suspend(p,s.nowMs);check(api->plan(p,1,s.periodMs,1)==1,"faster-than-refresh real frames are never duplicated");
 api->destroy(p);
 // Closed-loop producer + three-pair FIFO + display clock. This catches
 // controllers that fill nominal ratios but throttle real throughput through
 // their own queue, including recovery after a stalled display consumer.
 for(double hz:{72.,90.,120.})for(double native:{40.,50.,60.,144.}){
  p=api->create();const double period=1000/hz,work=1000/std::min(native,hz);
  struct Publication {double at,held;};
  std::deque<Publication> queue;double producer=work,display=period,previous=0,previousHeld=0,totalHeld=0,blockedAt=0;
  unsigned remaining=0,reals=0,outputs=0,peak=0;bool blocked=false,pressure=false;
  const auto prepare=[&]{
   if(remaining||queue.empty())return;
   const auto front=queue.front();
   const double interval=unblockedInterval(front.at-previous,front.held-previousHeld);
   AdaptiveSample input{display,period,work,4.,interval,4,pressure?3u:unsigned(queue.size()),1};
   pressure=false;api->sample(p,&input);remaining=api->plan(p,interval,period,previous!=0);
   previous=front.at;previousHeld=front.held;
  };
  while(display<=20000){
   if(producer<=display){
    queue.push_back({producer,totalHeld});peak=std::max(peak,unsigned(queue.size()));
    blocked=queue.size()>=3;if(blocked){blockedAt=producer;pressure=true;}producer=blocked?1e20:producer+work;
   }else{
    // Runtime stall while rendering keeps going until the bounded queue fills.
    if(display>=5000&&display<5080){display+=period;continue;}
    prepare();
    if(remaining){++outputs;if(!--remaining){++reals;queue.pop_front();if(blocked){blocked=false;totalHeld+=display-blockedAt;producer=display+work;}
     // Production prefetch plans immediately AFTER retirement, before the
     // newly unblocked producer can publish: a full queue now looks like two.
     prepare();
    }}
    display+=period;
   }
  }
  printf("FIFO %.0fHz native %.0f: real %.2f output %.2f peak %u\n",hz,native,reals/20.,outputs/20.,peak);
  check(peak<=3,"bounded queue during runtime stalls");
  check(reals/19.92>=std::min(native,hz)*.97,"FIFO feedback preserves available real throughput");
  check(outputs/19.92>=hz*.97,"FIFO feedback fills display demand after a stall");
  api->destroy(p);
 }
 // One controller survives runtime refresh switches; it must discard the
 // old fractional debt and output-loss window in each new clock domain.
 p=api->create();
 for(double hz:{72.,120.,90.,72.}){
  s={1000,1000./hz,16.7,3.,1000./60,4,0,1};api->sample(p,&s);
  unsigned outputs=0;
  for(unsigned i=0;i<600;++i)outputs+=api->plan(p,s.realIntervalMs,s.periodMs,1);
  check(std::abs(double(outputs)-hz*10)<=1,"72/90/120Hz switch fills display demand within one deferred slot");
 }
 api->destroy(p);
 // Newer ready inputs must not replace the adjacent pair needed by DLSS.
 std::array<PublishedPair,4> slots={PublishedPair{14,14},{12,12},{13,13},{11,11}};
 check((oldestUnconsumed(slots,10)>>2)==11,"FIFO selects adjacent pair instead of newest");
 check(protectPublished(11,10,true)&&protectPublished(14,10,true),"all unread pairs protected");
 for(unsigned retired=11;retired<=14;++retired){
  check((oldestUnconsumed(slots,retired-1)>>2)==retired,"retirement preserves ordered neighbors");
  check(!protectPublished(retired,retired,true),"consumed slot is reusable");
 }
 check(!oldestUnconsumed(slots,14),"no stale pair replay");
 slots[3].value=20;check((oldestUnconsumed(slots,10)>>2)==12,"overwritten publication rejected");
 check(!protectPublished(14,10,false),"real-only legacy ring behavior unchanged");
 // The real producer clock depends only on refresh, never the displayed FG ratio.
 ProducerClock clock;double t=1000;clock.deadline(t,1000./90,1000./90,true);
 for(unsigned i=0;i<100;++i){t+=1000./60;check(clock.deadline(t,1000./90,1000./90,true)<=t+.001,"60FPS producer is not reduced to 45FPS");}
 // Producer intervals alternating around 2x have exactly 2x average demand.
 // The former floor quantizer alternated 1/3 despite continuous history.
 for(double hz:{72.,90.,120.}){
  p=api->create();const double period=1000/hz;
  s={1000,period,period*2,3,period*2,4,0,1};api->sample(p,&s);
  unsigned sum=0,changes=0,lastCount=0,oldChanges=0,oldLast=0;double oldDebt=0;
  for(unsigned i=0;i<1000;++i){
   const double ratio=i%2?2.2:1.8;
   const auto count=api->plan(p,ratio*period,period,1);sum+=count;
   if(lastCount&&count!=lastCount)++changes;lastCount=count;
   oldDebt+=ratio-1;const auto oldGenerated=unsigned(std::floor(std::max(0.,oldDebt)+1e-7));oldDebt-=oldGenerated;
   const auto oldCount=oldGenerated+1;if(oldLast&&oldCount!=oldLast)++oldChanges;oldLast=oldCount;
   check(i==0||count==2,"small paired timing jitter retains 2x after initial real slot");
  }
  check(sum==1999&&changes==1&&oldChanges==999,"jitter hysteresis preserves total output while eliminating unnecessary count changes");
  // A sustained workload change must overcome hysteresis, never wait for a
  // multi-second cooldown. Preserve measured demand, including 1x-2x.
  for(double ratio:{3.,1.5,2.4,1.,4.,2.}){
   unsigned total=0;
   for(unsigned i=0;i<100;++i)total+=api->plan(p,ratio*period,period,1);
   check(std::abs(double(total)-ratio*100)<=1,"sustained changes retain real-frame priority and bounded output debt");
  }
  s.maximum=2;api->sample(p,&s);
  for(unsigned i=0;i<20;++i)check(api->plan(p,period*4,period,1)<=2,"hysteresis cannot hold a plan above new backend ceiling");
  api->destroy(p);
 }
 puts("PASS: fresh-first DLL, 1.0-4.0 ratios, bounded cadence hysteresis, real cadence, gap filling, output loss, boundaries and refresh-only producer clock");
}
