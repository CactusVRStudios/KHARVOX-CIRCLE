#include "stable_auto_policy.hpp"
#include <cstdio>
#include <cstdlib>
using namespace circle::fg;
void check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAILED: %s\n",message);std::exit(1);}}
struct Run {
 StableAutoFramegen policy;
 double period,now=1000,phase{};
 unsigned maximum=4;
 explicit Run(double hz):period(1000/hz){}
 void step(double work,double rate=1.,bool report=true,double fg=.1){
  now+=period;phase+=rate;const bool fresh=phase>=1;if(fresh)phase-=1;
  if(report)policy.output(now,period,fresh,now+100000);
  policy.choose(now,period,work*period,fg*period,maximum);
  check(policy.selected>=2&&policy.selected<=maximum,"only supported integer stages 2..4");
 }
 void run(double seconds,double work,double rate=1.,bool report=true,double fg=.1){
  const auto end=now+seconds*1000;while(now<end)step(work,rate,report,fg);
 }
};
int main(){
 for(double hz:{72.,90.,120.}){
  Run mild(hz);mild.policy.selected=4;
  mild.run(2,2.5,1.,true,.22);
  const auto credit=mild.policy.stableOutput;
  mild.run(1,2.5,.93,true,.22);
  check(mild.policy.stableOutput>credit*.5,"short mild dip retains most recovery progress");
  mild.run(6,2.5,1.,true,.22);
  check(mild.policy.selected==3&&!mild.policy.trialFrom,"recovery survives measured 93 percent output dip");
  Run stable(hz);stable.policy.selected=4;
  stable.run(2,2.5,1.,true,.22);
  check(stable.policy.selected==4&&!stable.policy.trialFrom,"stable output does not trigger frequent probes");
  stable.run(6,2.5,1.,true,.22);
  check(stable.policy.selected==3&&!stable.policy.trialFrom,"actual healthy lower output overrides conservative higher-stage estimate");
  stable.run(20,2.5,1.,true,.22);
  check(stable.policy.selected==2&&!stable.policy.trialFrom,"stable delivery permits stepwise recovery to x2");
  const auto stableEvents=stable.policy.events;
  stable.run(120,2.5,1.,true,.22);
  check(stable.policy.events==stableEvents,"integer auto never probes below x2");

  Run failedStable(hz);failedStable.policy.selected=4;
  while(!failedStable.policy.trialFrom&&failedStable.now<60000)failedStable.step(2.5,1.,true,.22);
  check(failedStable.policy.selected==3&&failedStable.policy.trialFrom==4,"bounded output-only probe starts");
  failedStable.run(4,2.5,.75,true,.22);
  check(failedStable.policy.selected==4&&!failedStable.policy.trialFrom,"output-only probe rolls back on observed deficit");
  const auto failedEvents=failedStable.policy.events;
  failedStable.run(18,2.5,1.,true,.22);
  check(failedStable.policy.events==failedEvents,"failed probe requires cooldown plus new stable observation");

  Run interrupted(hz);interrupted.policy.selected=4;
  interrupted.run(2,2.5);interrupted.run(3,2.5,.8);interrupted.run(1,2.5);
  check(interrupted.policy.selected==4,"output interruption resets long stable-probe countdown");
  interrupted.policy.suspend(interrupted.now);interrupted.now+=60000;interrupted.run(2,2.5);
  check(interrupted.policy.selected==4,"pause time cannot complete stable-probe countdown");
  Run r(hz);r.run(10,1.2);
  check(r.policy.selected==2,"start at x2 and never select real-only");
  for(int i=0;i<8;++i){r.run(1,4.5,.5);r.run(3,1.2);}
  check(r.policy.events==0,"brief severe dips cannot change integer mode");
  r.run(12,3.0,.85);
  check(r.policy.selected==2,"moderate output shortage does not justify a mode switch");
  r.run(12,3.0,1.);
  check(r.policy.selected==2,"estimated workload alone cannot force upshift");
  r.run(12,1.2,.6);
  check(r.policy.selected==2,"runtime output shortage alone cannot force upshift");
  r.run(6,3.0,.65);
  check(r.policy.selected==3,"sustained severe workload and delivery shortage steps up once");
  const double raised=r.policy.changed;
  while(r.now<raised+9000)r.step(4.5,.6);
  check(r.policy.selected==3,"at least ten seconds between upshifts");
  r.run(5,4.5,.6);
  check(r.policy.selected==4,"persistent overload can reach ceiling after hold");
  r.run(2,.8);
  check(r.policy.selected==4,"short recovery cannot downshift");
  r.run(6,.8);
  check(r.policy.selected==3&&!r.policy.trialFrom,"sustained headroom verifies only next lower stage");
  r.run(20,.8);
  check(r.policy.selected==2&&!r.policy.trialFrom,"long recovery returns stepwise to x2");
  r.run(30,.5);
  check(r.policy.selected==2,"healthy scene never goes below x2");

  Run rollback(hz);rollback.policy.selected=4;
  for(int i=0;i<int(hz*30)&&!rollback.policy.trialFrom;++i)rollback.step(.8);
  check(rollback.policy.selected==3&&rollback.policy.trialFrom==4,"recovery creates verifiable trial");
  rollback.run(4,3.2,.5);
  check(rollback.policy.selected==4&&!rollback.policy.trialFrom,"failed actual output restores former stage");
  const auto event=rollback.policy.events;
  rollback.run(15,.8);
  check(rollback.policy.selected==4&&rollback.policy.events==event,"failed decrease blocks repeated probes for twenty seconds");
  rollback.run(13,.8);
  check(rollback.policy.selected<4,"recovery may retry after backoff and new sustained evidence");

  Run missing(hz);missing.run(20,5,.5,false);
  check(missing.policy.selected==2,"no output evidence cannot upshift");
  missing.policy.selected=4;
  for(int i=0;i<int(hz*30)&&!missing.policy.trialFrom;++i)missing.step(.8);
  missing.run(9,.8,1.,false);
  check(missing.policy.selected==4&&!missing.policy.trialFrom,"unverified recovery times out safely");

  Run amd(hz);amd.maximum=2;amd.run(30,8,.3);
  check(amd.policy.selected==2&&amd.policy.events==0,"backend cap keeps AMD at x2");
  Run costly(hz);costly.run(30,1,.5,true,3);
  check(costly.policy.selected==2,"more expensive generated frames must not justify an unhelpful increase");

  Run paused(hz);paused.run(3,3.0,.6);paused.policy.suspend(paused.now);
  paused.now+=60000;paused.run(2,3.0,.6);
  check(paused.policy.selected==2,"pause does not contribute time or stale output to a switch");
  paused.policy.choose(paused.now,paused.period,NAN,1,4);
  check(paused.policy.overload==0&&paused.policy.outputAt==0,"invalid workload discards accumulated evidence");
  paused.policy.choose(paused.now,1000./144,3*paused.period,1,4);
  check(paused.policy.selected==2&&paused.policy.overload==0,"refresh change requires new evidence");
 }
 std::puts("PASS: integer Auto hysteresis, recovery, output evidence, caps and 72/90/120 Hz");
}


