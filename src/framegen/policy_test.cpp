#include "policy.hpp"
#include "pacing_policy.hpp"
#include "backend_policy.hpp"
#include "auto_policy.hpp"
#include "display_target.hpp"
#include "adaptive_policy.hpp"
#include "../motor/hud_projection.hpp"
#include <cstdio>
#include <cstdlib>
using namespace circle::fg;
void check(bool b,const char* what){if(!b){fprintf(stderr,"FAILED: %s\n",what);std::exit(1);}}
int main(){
 for(double hz:{72.,90.,120.}){
  const auto physical=std::llround(1e9/hz);
  FractionalFramegen stable;
  unsigned outputs=0;
  for(unsigned i=0;i<600;++i){
   const auto raw=std::llround((1+i%3)*1e9/hz);
   const auto target=displayTargetNs(raw,hz,1000./120);
   check(target==physical,"physical refresh wins over 1x/2x/3x runtime throttling and stale measured base");
   outputs+=stable.plan(1.8*physical*1e-6,target*1e-6,true);
  }
  check(stable.events==1,"SteamVR throttle transitions must not reset interpolation history");
  check(outputs>=1078&&outputs<=1080,"1.8x demand retains fractional display slots despite runtime throttle changes");
  const auto changed=displayTargetNs(physical,72.,1000./120);
  stable.plan(25.,changed*1e-6,true);
  check(stable.periodMs==changed*1e-6,"real headset refresh change updates policy even with shorter old measured base");
 }
 check(displayTargetNs(22222222,0,1000./90)==11111111,"known measured base removes half-rate fallback");
 check(displayTargetNs(13888889,0,1000./90)==13888889,"nonmultiple fallback honors possible real refresh change");
 check(displayTargetNs(22222222,0,0)==22222222,"unknown startup rate is not invented");
 check(displayTargetNs(11111111,NAN,NAN)==11111111,"invalid refresh evidence falls back to runtime");
 check(displayTargetNs(-1,0,0)==0,"invalid periods cannot enable producer pacing");
 {
  AutoFramegen a;a.selected=4;
  // Ideal count alternates between 2 and 3. Both sustain the next stage (3).
  for(unsigned t=100;t<=5200;t+=10)a.choose(t,11.111,(t/100)%2?12:23,2,4);
  check(a.selected==3&&a.trialFrom==4,"changing lower candidates cannot starve step-down probe");
  const auto start=a.trialStart;
  for(double t=start+10;t<=start+3100;t+=10){a.output(t,11.111,true);a.choose(t,11.111,20,2,4);}
  check(a.selected==3&&!a.trialFrom,"fresh output deadlines confirm lower stage");
  for(double t=start+3110;t<start+10000;t+=10){a.output(t,11.111,true);a.choose(t,11.111,12,2,4);}
  check(a.selected==2&&a.trialFrom==3,"next decrease remains a separate trial");
  for(double t=start+10000;t<start+11000;t+=10){a.output(t,11.111,false);a.choose(t,11.111,12,2,4);}
  check(a.selected==3&&!a.trialFrom&&a.cooldown>start+11000,"missed lower-stage outputs roll back with cooldown");
 }
 {
  AutoFramegen a;a.selected=2;
  for(unsigned t=100;t<=5200;t+=10)a.choose(t,11.111,5,2,4);
  check(a.trialFrom==2&&a.selected==1,"real-only is also validated as trial");
  for(unsigned t=5210;t<=12000;t+=10)a.choose(t,11.111,5,2,4);
  check(a.selected==2&&!a.trialFrom,"absence of output evidence cannot confirm a trial");
 }
 { // Replay r296's deadlock: 90 Hz, measured 30 ms producer, no GPU delivery.
  AutoFramegen gaps;gaps.selected=3;gaps.trialFrom=4;gaps.trialStart=100;
  for(unsigned t=900;t<2000;t+=33){gaps.output(t,11.111,true);gaps.choose(t,11.111,10,2,4);}
  check(gaps.selected==4&&!gaps.trialFrom,"slow fresh submissions still miss display deadlines and roll back");
  AutoFramegen paused;paused.selected=3;paused.trialFrom=4;paused.trialStart=100;
  paused.suspend(10000);paused.choose(10010,11.111,10,2,4);
  check(paused.selected==3&&paused.trialFrom==4&&paused.samples==0,"focus interruption restarts evidence window without false timeout");
  AutoFramegen a;
  for(unsigned t=100;t<=2500;t+=100)
   a.choose(t,1000./90,automaticWorkload(30,true,0,false)*1.2,2,4);
  check(a.selected==4,"missing GPU telemetry cannot pin auto at x1 under measured overload");
  check(automaticWorkload(30,true,11,true)==30,"partial GPU lower bound cannot hide producer bottleneck");
  check(automaticWorkload(5,true,20,true)==20,"GPU bottleneck dominates producer time");
  check(automaticWorkload(30,false,11,true)==0,"stale producer does not decide from partial GPU");
  for(unsigned t=2600;t<=35000;t+=10){a.output(t,1000./90,true);a.choose(t,1000./90,automaticWorkload(5,true,0,false),2,4);}
  check(a.selected==1,"telemetry fallback still recovers after sustained real performance improvement");
 }
 for(unsigned target:{1u,2u,3u,4u}){
  AutoFramegen a;const double work=target==1?6.:target==2?12.:target==3?20.:29.;
  for(unsigned t=100;t<2500;t+=100)a.choose(t,1000./90,work,1,4);
  check(a.selected==target,"auto chooses smallest sustainable factor from actual workload");
  a.choose(2500,1000./90,6,1,4);
  check(a.choose(3000,1000./90,6,1,4)==target,"recovery hysteresis");
  for(unsigned t=3010;t<=35000;t+=10){a.output(t,1000./90,true);a.choose(t,1000./90,6,1,4);}
  check(a.selected==1,"sustained verified recovery returns stepwise to real-only");
 }
 {AutoFramegen a;for(unsigned t=100;t<2500;t+=100)a.choose(t,11.111,30,1,2);
  check(a.selected==2,"AMD capability caps auto to x2");
  check(a.choose(2600,NAN,1,1,4)==2,"invalid display timing retains selection");
  check(a.choose(2700,11.111,NAN,1,4)==2,"invalid workload ignored");}
 for(unsigned multiplier:{2u,3u,4u}){
  check(automaticCommunity(0x10de,L"NVIDIA GeForce RTX 2080 Ti",multiplier),"RTX20 auto community");
  check(automaticCommunity(0x10de,L"NVIDIA GeForce RTX 3080 Laptop GPU",multiplier),"RTX30 auto community");
  check(automaticCommunity(0x10de,L"NVIDIA GeForce RTX 4080 SUPER",multiplier),"RTX40 always community");
  check(!automaticCommunity(0x10de,L"NVIDIA GeForce RTX 5090",multiplier),"RTX50 native");
  check(!automaticCommunity(0x1002,L"AMD Radeon RX 7900 XTX",multiplier),"AMD never loads NVIDIA proxy");
  check(!automaticCommunity(0x8086,L"Intel Arc",multiplier),"Intel never loads NVIDIA proxy");
  check(!automaticCommunity(0x10de,L"Unknown GPU",multiplier),"unknown architecture native capability check");
 }
 // Three outputs retain the endpoint until both ordered intermediates succeed.
 Cadence triple;triple.accept(Output::Generated,42,false,1,2);
 check(!triple.pending,"3x failed first frame is retryable");
 triple.accept(Output::Generated,42,true,1,2);
 triple.accept(Output::Generated,42,true,1,2);
 check(triple.acceptedGenerated==1,"3x duplicate first frame does not advance");
 triple.accept(Output::Real,42,true,1,2);
 check(triple.pending==42,"3x cannot skip second intermediate");
 triple.accept(Output::Generated,43,true,2,2);
 check(triple.acceptedGenerated==1,"3x cannot mix endpoint identities");
 triple.accept(Output::Generated,42,false,2,2);
 check(triple.acceptedGenerated==1,"3x failed second frame must retry");
 triple.accept(Output::Generated,42,true,2,2);
 triple.accept(Output::Real,42,false,1,2);
 check(triple.pending==42,"3x failed real output retains endpoint");
 triple.accept(Output::Real,42,true,1,2);
 check(!triple.pending&&!triple.canGenerate(42)&&triple.canGenerate(43),"3x finishes once, then admits next pair");
 // 4x owes three ordered intermediates before releasing the real endpoint.
 Cadence quad;
 quad.accept(Output::Generated,77,true,1,3);
 quad.accept(Output::Generated,77,true,3,3);
 check(quad.acceptedGenerated==1,"4x rejects skipped intermediate");
 quad.accept(Output::Generated,77,true,2,3);
 quad.accept(Output::Real,77,true,1,3);
 check(quad.pending==77,"4x retains endpoint until third intermediate");
 quad.accept(Output::Generated,77,false,3,3);
 check(quad.acceptedGenerated==2,"4x retries failed third intermediate");
 quad.accept(Output::Generated,77,true,3,3);
 quad.accept(Output::Real,77,true,1,3);
 check(!quad.pending&&quad.lastReal==77,"4x completes three intermediates then real");
 Identity a{100,7,{30,29},{90,91},1856,1984,true,false,true},b{101,7,{31,30},{92,93},1856,1984,true,false,true};
 check(consecutive(a,b),"two backends per simulation tick");
 auto c=b;c.eyeSeq[1]++;check(!consecutive(a,c),"unrelated right-eye history rejected");
 c=b;c.backend[1]++;check(!consecutive(a,c),"backend/eye mismatch rejected");
 c=b;c.epoch++;check(!consecutive(a,c),"recenter reset");
 c=b;c.width++;check(!consecutive(a,c),"resize reset");
 c=b;c.reset=true;check(!consecutive(a,c),"camera reset");
 c=b;c.gameplay=false;check(!consecutive(a,c),"cinema/load reset");
 c=b;c.complete=false;check(!consecutive(a,c),"partial pair rejected");
 c=b;c.pair=a.pair;check(!consecutive(a,c),"duplicate simulation rejected");
 for(unsigned scene:{1u,2u,3u}){
  auto cinemaA=a,cinemaB=b;cinemaA.scene=cinemaB.scene=scene;
  check(consecutive(cinemaA,cinemaB),"continuous cinema and journal quad admit interpolation");
  check(!consecutive(a,cinemaB),"gameplay to cinema cannot interpolate");
  cinemaB.scene=scene==1?2:1;
  check(!consecutive(cinemaA,cinemaB),"2D/3D cinema switch resets history");
 }
 check(sceneEligible(true,true,false,false),"complete rendered scene admitted");
 check(!sceneEligible(true,true,true,false)&&!sceneEligible(true,true,false,true)&&!sceneEligible(false,true,false,false),
  "pause menus, unvalidated inspection and missing views cannot interpolate");
 check(sceneEligible(true,true,false,true,true,true),"journal with rendered mono quad may interpolate");
 check(!sceneEligible(true,true,true,true,true,true),"native pause still blocks journal FG");
 check(!sceneEligible(true,true,false,true,true,false)&&!sceneEligible(true,true,false,true,false,true),
  "inspection must be a complete mono cinema view, not an immersive/stereo placeholder");
 check(!sceneEligible(true,false,false,true,true,true),"unrendered menu quad has no scene motion/depth");
 check(sceneKind(false,false,false)==0&&sceneKind(true,false,false)==1&&sceneKind(true,true,false)==2&&sceneKind(true,true,true)==3,
  "journal has its own immutable presentation identity");
 {
  auto journalA=a,journalB=b;journalA.scene=journalB.scene=3;
  Cadence journal;journal.beginInput(journalA);journal.accept(Output::Real,a.pair,true);
  check(!journal.beginInput(journalB),"successive journal pairs retain FG history");
  journal.accept(Output::Generated,b.pair,true,1,2);
  auto game=b;game.scene=0;game.pair++;
  check(!journal.beginInput(game)&&journal.input.scene==3,"closing journal cannot replace a pending generated endpoint");
  journal.accept(Output::Generated,b.pair,true,2,2);journal.accept(Output::Real,b.pair,true,1,2);
  check(journal.beginInput(game)&&!journal.lastGenerated&&!journal.lastReal,"gameplay resumes with a fresh history after journal endpoint");
  auto missing=journalB;missing.complete=false;
  check(!consecutive(journalA,missing),"journal cannot bypass missing depth/motion pair checks");
 }
 Cadence q;check(q.canGenerate(101),"first candidate");
 q.accept(Output::Generated,101,false);check(q.canGenerate(101),"failed end frame does not advance");
 q.accept(Output::Generated,101,true);check(q.pending==101&&!q.canGenerate(102),"pending endpoint precedes newer pair");
 q.accept(Output::Real,102,true);check(q.pending==101,"wrong endpoint cannot discharge obligation");
 q.accept(Output::Real,101,false);check(q.pending==101,"failed endpoint remains pending");
 q.accept(Output::Real,101,true);check(!q.pending&&q.canGenerate(102)&&!q.canGenerate(101),"endpoint accepted exactly once");
 q.reset();check(!q.pending&&!q.lastReal&&!q.lastGenerated,"reset discards cadence");
 // r176 live regression: accepted 956M token, then resumed gameplay at 795M.
 // NGX warmed up, but lastGenerated blocked output until the old clock caught up.
 Identity old=a;old.pair=956496175;
 q.beginInput(old);q.accept(Output::Generated,old.pair,true);
 Identity resumed=b;resumed.pair=795565764;
 check(!q.beginInput(resumed)&&q.pending==old.pair,"focus change cannot abandon pinned endpoint");
 q.accept(Output::Real,old.pair,true);
 check(q.beginInput(resumed)&&!q.lastGenerated&&!q.lastReal,"clock rewind clears output watermarks");
 check(!consecutive(old,resumed),"rewound history first frame cannot interpolate");
 q.accept(Output::Real,resumed.pair,true);
 auto next=resumed;next.pair++;for(int e=0;e<2;++e){next.eyeSeq[e]++;next.backend[e]+=2;}
 check(!q.beginInput(next)&&q.canGenerate(next.pair),"first adjacent pair after rewind generates immediately");
 q.accept(Output::Generated,next.pair,true);q.accept(Output::Real,next.pair,true);
 check(!q.canGenerate(next.pair),"recovery still rejects duplicate generated output");
 auto menu=next;menu.gameplay=false;menu.pair=999999999;
 check(q.beginInput(menu),"menu invalidates output history");q.accept(Output::Real,menu.pair,true);
 check(q.beginInput(resumed),"gameplay return discards unrelated menu token");
 q.accept(Output::Real,resumed.pair,true);q.beginInput(next);
 check(q.canGenerate(next.pair),"large menu token cannot suppress resumed generation");
 for(int change=0;change<4;++change){
  q.reset();q.beginInput(a);q.accept(Output::Generated,a.pair,true);q.accept(Output::Real,a.pair,true);
  auto boundary=b;
  if(change==0)boundary.epoch++;if(change==1)boundary.width++;if(change==2)boundary.reset=true;if(change==3)boundary.complete=false;
  check(q.beginInput(boundary)&&!q.lastGenerated,"epoch resize camera and missing input reset both histories");
 }
 q.reset();
 // Native pause arrives during a pending generated image, before a cinema
 // quad exists. The old anchor selection submitted a null XrSpace, preventing
 // acknowledgement and permanently blocking image acquisition with FG.
 using circle::motor::HudAnchor;using circle::motor::hudAnchor;
 check(hudAnchor(true,false,false)==HudAnchor::Head,"gameplay HUD follows head");
 check(hudAnchor(true,true,false)==HudAnchor::MenuScreen,"early pause uses valid screen fallback");
 q.accept(Output::Generated,200,true);
 check(q.pending==200,"early pause still owes real endpoint");
 check(hudAnchor(true,true,false)==HudAnchor::MenuScreen,"endpoint keeps valid pause HUD");
 q.accept(Output::Real,200,true);
 check(!q.pending,"valid pause submission releases endpoint");
 check(hudAnchor(false,true,true)==HudAnchor::CinemaImage,"arrived pause image supplies cinema anchor");
 check(hudAnchor(false,false,true)==HudAnchor::CinemaImage,"resume pending gameplay keeps last valid cinema anchor");
 check(hudAnchor(true,false,false)==HudAnchor::Head,"new gameplay restores HUD follow");
 check(!reusable(1,10,20,9,20),"Vulkan producer still running");
 check(!reusable(1,10,20,10,19),"D3D consumer still running");
 check(!reusable(2,10,20,10,20),"pending endpoint holds lease");
 check(!reusable(1,0,0,10,20),"unsubmitted recording never reused");
 check(reusable(1,10,20,10,20),"lease reclaimed only after both fences");
 // 2X caps the producer, not the XR loop or either individual eye.
 for(unsigned multiplier:{2u,3u,4u})for(double hz:{72.,90.,120.}){
  ProducerClock clock;const double interval=1000.*multiplier/hz;double now=1000;
  check(clock.deadline(now,interval,interval,true)==now,"pacer starts without extra stale-frame wait");
  for(unsigned i=1;i<=200;++i){
   const double due=clock.deadline(now+5,interval,interval,true);
   check(std::abs(due-(1000+i*interval))<1e-6,"producer holds half-rate grid without cumulative hook cost");now=due;
  }
  const double resumed=now+2000;
  check(clock.deadline(resumed,interval,interval,true)==resumed,"long stall cannot cause catch-up burst");
  check(clock.deadline(resumed+1,interval,interval,false)==resumed+1,"menus/fallback bypass cap");
  check(clock.deadline(resumed+2,interval,interval,true)==resumed+2,"gameplay return reprimes pacer");
  check(clock.deadline(resumed+3,20,20,true)==resumed+3,"refresh-rate change resets deadline");
 }
 AdaptivePacing feedback;const double base=2000./90;
 feedback.observe(1000,base,false);feedback.observe(1200,base,true);
 check(feedback.observe(2000,base,false)==base,"single loading spike does not reduce rate");
 feedback.observe(2200,base,true);feedback.observe(2400,base,true);
 const double backed=feedback.observe(3000,base,false);
 check(backed>base&&backed<=base*2,"repeated missing neighbors back off producer");
 for(double now=4000;now<=8000;now+=1000)feedback.observe(now,base,false);
 check(feedback.interval<backed&&feedback.interval>=base,"stable cadence slowly restores base rate");
 check(!pacingLive(true,true,1300,1000),"stopped XR worker expires producer cap");
 check(!pacingLive(true,false,1100,1000),"focus loss bypasses cap");
 check(!pacingLive(false,true,1100,1000),"missing/failed FG inputs bypass cap");
 check(pacingLive(true,true,1100,1000),"healthy generation enables producer pacing");
 puts("FG pair, output-order, reset and GPU lease policies passed");
}

