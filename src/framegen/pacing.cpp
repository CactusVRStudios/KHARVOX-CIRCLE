#include "pacing.hpp"
#include "pacing_policy.hpp"
#include "bridge.hpp"
#include "options.hpp"
#include "auto_policy.hpp"
#include "stable_auto_policy.hpp"
#include "adaptive_client.hpp"
#include "policy.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <deque>
namespace circle::fg {
namespace {
using Clock=std::chrono::steady_clock;
double nowMs(){return std::chrono::duration<double,std::milli>(Clock::now().time_since_epoch()).count();}
struct Pacer {
 std::mutex mutex,producer;
 std::condition_variable changed;
 bool ready{},visible{};
 unsigned multiplier=2;
 double heartbeat{},base{},interval{},holdMs{},holdTotalMs{},holdStartedMs{};
 bool queuePressure{};
 uint64_t revision{};
 AdaptivePacing adaptive;
 ProducerClock schedule;
 AutoFramegen automatic;
 StableAutoFramegen stable;
 AdaptiveClient fractional;AdaptiveDecision decision{10};unsigned planned=1;std::deque<uint64_t> pending;bool fractionalOn{};
 double display{},gpu[2]{},gpuAt[2]{},cpu{},cpuAt{},producerReturned{},fgPerFrame=2.;
 bool gpuComplete[2]{};
};
// No static destructor under the DLL loader lock. Waits are bounded and the
// heartbeat releases pacing if the XR worker stops unexpectedly.
Pacer& p=*new Pacer;
}
void displayPulse(int64_t ns,bool visible){
 if(!requested())return;
 std::lock_guard lock(p.mutex);
 const double base=ns>=4000000&&ns<=40000000?ns*(p.fractionalOn?1:p.multiplier)*1e-6:0;
 p.display=ns>=4000000&&ns<=40000000?ns*1e-6:0;
 if(p.base!=base||p.visible!=visible){++p.revision;p.adaptive={};p.interval=base;}
 p.base=base;p.visible=visible;p.heartbeat=nowMs();
 if(!visible){p.automatic.suspend(p.heartbeat);p.stable.suspend(p.heartbeat);if(p.fractionalOn)p.fractional.api->suspend(p.fractional.instance,p.heartbeat);}
 p.changed.notify_all();
}
void pacingMultiplier(unsigned multiplier){std::lock_guard lock(p.mutex);p.multiplier=std::clamp(multiplier,1u,4u);}
void autoGpuSample(unsigned eye,double ms,bool complete){
 if((!automaticMultiplier()&&!fractionalMultiplier())||eye>1||!std::isfinite(ms)||ms<=0||ms>250)return;
 std::lock_guard lock(p.mutex);p.gpu[eye]=p.gpu[eye]?p.gpu[eye]*.8+ms*.2:ms;p.gpuAt[eye]=nowMs();
 p.gpuComplete[eye]=complete;
}
unsigned autoMultiplier(unsigned maximum,double fgMs,unsigned generated){
 std::lock_guard lock(p.mutex);const auto now=nowMs();
 if(generated&&std::isfinite(fgMs)&&fgMs>0)p.fgPerFrame=p.fgPerFrame*.9+.1*fgMs/generated;
 const bool gpuFresh=p.gpuAt[0]&&p.gpuAt[1]&&now-p.gpuAt[0]<=2000&&now-p.gpuAt[1]<=2000;
 const bool fresh=p.visible&&p.cpuAt&&now-p.cpuAt<=1000;
 const bool complete=gpuFresh&&p.gpuComplete[0]&&p.gpuComplete[1];
 if(stableAutomaticMultiplier()){
  if(!fresh){p.stable.suspend(now);return p.stable.selected;}
  const double render=automaticWorkload(p.cpu,fresh,p.gpu[0]+p.gpu[1],gpuFresh);
  const auto before=p.stable.selected;
  const auto chosen=p.stable.choose(now,p.display,render,p.fgPerFrame,maximum);
  static unsigned event{};static double report{};
  if(event!=p.stable.events||now-report>=2000){event=p.stable.events;report=now;
   circlevr::vkcore::logf("vr-fg-auto-integer: %ux -> %ux target %.2f Hz delivery %.1f%% outputAge %.0f ms overload %.0f/3000 ms recovery %.0f/20000 ms stable %.0f/3000 ms trialHealthy %.0f/1500 ms trialFrom %ux cooldown %.0f ms work %.3f ms FG/frame %.3f ms GPUfresh %d complete %d ceiling %u reason %s",
    before,chosen,p.display>0?1000/p.display:0,p.stable.delivery*100,p.stable.outputAt?now-p.stable.outputAt:-1.,p.stable.overload,p.stable.reserve,p.stable.stableOutput,p.stable.trialHealthy,p.stable.trialFrom,std::max(0.,p.stable.cooldown-now),render,p.fgPerFrame,gpuFresh,complete,maximum,p.stable.reason);
  }
  return chosen;
 }
 static double report{};
 if(now-report>=5000){report=now;circlevr::vkcore::logf("vr-fg-auto: measurement fresh %d GPU fresh %d complete %d L/R %.3f/%.3f ms producer work %.3f ms display %.3f ms selected %ux",fresh,gpuFresh,complete,p.gpu[0],p.gpu[1],p.cpu,p.display,p.automatic.selected);}
 if(!fresh){p.automatic.suspend(now);return p.automatic.selected;}
 // Partial GPU samples remain lower bounds. Use the measured producer work
 // with one policy reserve; validate lower stages against actual XR output.
 const double render=automaticWorkload(p.cpu,fresh,p.gpu[0]+p.gpu[1],gpuFresh);
 const auto before=p.automatic.selected;
 const auto chosen=p.automatic.choose(now,p.display,render,p.fgPerFrame,maximum);
 static unsigned event{};static double decisionLog{};
 if(event!=p.automatic.events||now-decisionLog>=1000){event=p.automatic.events;decisionLog=now;
  circlevr::vkcore::logf("vr-fg-auto-decision: selected %ux next %ux reserve %.0f/5000 ms trialFrom %ux trialAge %.0f ms outputs %u missed %u cooldown %.0f ms workload %.3f ms FG/frame %.3f ms reason %s",chosen,chosen>1?chosen-1:1,p.automatic.reserve,p.automatic.trialFrom,p.automatic.trialFrom?now-p.automatic.trialStart:0,p.automatic.samples,p.automatic.misses,std::max(0.,p.automatic.cooldown-now),render,p.fgPerFrame,p.automatic.reason);
 }
 if(chosen!=before)circlevr::vkcore::logf("vr-fg-auto: %ux -> %ux GPU pair %.3f ms producer work %.3f ms FG/frame %.3f ms display %.3f ms max %u",before,chosen,p.gpu[0]+p.gpu[1],p.cpu,p.fgPerFrame,p.display,maximum);
 return chosen;
}
double producerHoldMs(){std::lock_guard lock(p.mutex);return p.holdMs;}
bool fractionalPacingActive(){std::lock_guard lock(p.mutex);return p.fractionalOn;}
double displayPeriodMs(){std::lock_guard lock(p.mutex);return p.display;}
double fractionalFactor(){std::lock_guard lock(p.mutex);return p.decision.tenths*.1;}
unsigned fractionalSample(unsigned maximum,double fgMs,unsigned generated,double sourceInterval,bool continuous,double sourceWaitMs){
 std::lock_guard lock(p.mutex);const double now=nowMs();
 const bool first=!p.fractional.attempted;
 p.fractionalOn=p.fractional.load();
 if(first)circlevr::vkcore::logf("vr-fg-fractional: CircleAdaptiveFG.dll ABI %u %s",AdaptiveAbi,p.fractionalOn?"loaded":"unavailable: real-frame fallback");
 if(!p.fractionalOn){p.multiplier=1;return 1;}
 if(generated&&std::isfinite(fgMs)&&fgMs>0)p.fgPerFrame=p.fgPerFrame*.9+.1*fgMs/generated;
 const bool gpuFresh=p.gpuAt[0]&&p.gpuAt[1]&&now-p.gpuAt[0]<=2000&&now-p.gpuAt[1]<=2000;
 const bool fresh=p.visible&&p.cpuAt&&now-p.cpuAt<=1000;
 const double render=automaticWorkload(p.cpu,fresh,p.gpu[0]+p.gpu[1],gpuFresh);
 // The publication and cumulative hold counters belong to the SAME producer
 // boundary. Captured-eye holds and XR pose timestamps are not paired clocks.
 const double workInterval=unblockedInterval(sourceInterval,sourceWaitMs);
 const unsigned pressure=p.queuePressure?std::max(3u,unsigned(p.pending.size())):unsigned(p.pending.size());
 p.queuePressure=false;
 AdaptiveSample sample{now,p.display,render,p.fgPerFrame,continuous?workInterval:0.,maximum,pressure,unsigned(p.visible)};
 p.decision=p.fractional.api->sample(p.fractional.instance,&sample);
 p.planned=p.fractional.api->plan(p.fractional.instance,workInterval,p.display,continuous);
 static unsigned planCounts[4]{},planPairs{},planChanges{},lastPlan{},pressureDrains{};
 ++planCounts[std::clamp(p.planned,1u,4u)-1];++planPairs;
 if(continuous&&lastPlan&&lastPlan!=p.planned)++planChanges;
 lastPlan=continuous?p.planned:0;
 if(pressure>=3)++pressureDrains;
 static double logAt{};
 if(now-logAt>=1000){logAt=now;
  circlevr::vkcore::logf("vr-fg-auto-stability: pairs %u countChanges %u plans 1x/2x/3x/4x %u/%u/%u/%u pressureDrains %u; bounded phase hysteresis, no real-render throttle",
   planPairs,planChanges,planCounts[0],planCounts[1],planCounts[2],planCounts[3],pressureDrains);
  planPairs=planChanges=pressureDrains=0;for(auto& n:planCounts)n=0;
  circlevr::vkcore::logf("vr-fg-auto-fresh: measured %.1fx outputsThisPair %u target %.2f Hz publication %.3f ms ownHold %.3f ms unblocked %.3f ms CPU %.3f ms GPU pair %.3f ms GPUfresh %d work %.3f ms FG/output %.3f ms telemetryFresh %d pending %zu pressure %u outputs %u missed %u reason %s",
   p.decision.tenths*.1,p.planned,p.display>0?1000/p.display:0,sourceInterval,sourceWaitMs,workInterval,p.cpu,p.gpu[0]+p.gpu[1],gpuFresh,render,p.fgPerFrame,fresh,p.pending.size(),pressure,p.decision.samples,p.decision.misses,p.decision.reason);
 }
 return p.planned;
}
bool preserveRealPairs(){std::lock_guard lock(p.mutex);return p.fractionalOn&&pacingLive(p.ready,p.visible,nowMs(),p.heartbeat);}
ProducerStamp pairPublished(uint64_t value){
 if(!fractionalMultiplier())return {nowMs(),0};
 std::lock_guard lock(p.mutex);
 if(p.pending.empty()||p.pending.back()<value)p.pending.push_back(value);
 if(p.pending.size()>8)p.pending.pop_front();
 const double stamp=nowMs();
 return {stamp,p.holdTotalMs+(p.holdStartedMs?std::max(0.,stamp-p.holdStartedMs):0.)};
}
void pairRetired(uint64_t value){if(!fractionalMultiplier())return;std::lock_guard lock(p.mutex);while(!p.pending.empty()&&p.pending.front()<=value)p.pending.pop_front();p.changed.notify_all();}

void autoOutput(uint64_t serial,bool accepted,int64_t displayTimeNs){
 if(!automaticMultiplier()&&!fractionalMultiplier())return;
 std::lock_guard lock(p.mutex);static uint64_t previous{};
 const bool fresh=accepted&&serial&&serial!=previous;
 if(accepted&&serial)previous=serial;
 if(p.visible&&p.ready){p.automatic.output(nowMs(),p.display,fresh);if(stableAutomaticMultiplier())p.stable.output(nowMs(),p.display,fresh,displayTimeNs*1e-6);if(p.fractionalOn)p.fractional.api->output(p.fractional.instance,nowMs(),p.display,fresh,displayTimeNs*1e-6);}
}
void pacingReady(bool ready){
 if(!requested())return;
 std::lock_guard lock(p.mutex);
 if(!ready){p.pending.clear();p.queuePressure=false;p.automatic.suspend(nowMs());p.stable.suspend(nowMs());if(p.fractionalOn)p.fractional.api->suspend(p.fractional.instance,nowMs());}
 if(p.ready!=ready){p.ready=ready;++p.revision;p.adaptive={};p.interval=p.base;p.changed.notify_all();}
}
void pacingPair(bool missed){
 if(!requested())return;
 std::lock_guard lock(p.mutex);
 if(!p.fractionalOn&&p.ready&&p.base>0)p.interval=p.adaptive.observe(nowMs(),p.base,missed);
}
double pacingIntervalMs(){std::lock_guard lock(p.mutex);return pacingLive(p.ready,p.visible,nowMs(),p.heartbeat)?p.interval:0;}
bool paceProducer(){
 if(!requested())return false;
 std::lock_guard producer(p.producer);
 std::unique_lock lock(p.mutex);
 const double now=nowMs();
 if((automaticMultiplier()||fractionalMultiplier())&&p.visible&&p.producerReturned){
  const double work=now-p.producerReturned;
  if(work>0&&work<250){p.cpu=p.cpu?p.cpu*.9+work*.1:work;p.cpuAt=now;}
 }
 const bool live=pacingLive(p.ready,p.visible,now,p.heartbeat)&&p.base>0;
 static uint64_t revision=0;
 if(revision!=p.revision){revision=p.revision;p.schedule={};}
 // Auto only caps the real producer at headset refresh. Its measured FG
 // ratio never enters this clock. Backpressure protects a bounded FIFO.
 if(p.fractionalOn&&live)p.interval=p.display;
 const double due=p.schedule.deadline(now,std::max(p.base,p.interval),p.base,live);
 if(!live){p.holdMs=0;p.producerReturned=now;return false;}
 p.holdStartedMs=now;
 const auto until=Clock::now()+std::chrono::duration<double,std::milli>(std::clamp(due-now,0.,100.));
 p.changed.wait_until(lock,until,[&]{return revision!=p.revision||!p.ready||!p.visible;});
 if(p.fractionalOn){
  // Latch the actual producer blockage: after retirement, prefetch already
  // sees only two pairs and must not mistake that for a never-blocked queue.
  if(p.pending.size()>=3)p.queuePressure=true;
  const auto queueUntil=Clock::now()+std::chrono::milliseconds(100);
  // Four ring slots: up to three complete pairs, leaving one slot for the
  // next stereo capture. The planner drains a full queue without synthetic work.
  p.changed.wait_until(lock,queueUntil,[&]{return p.pending.size()<3||!p.ready||!p.visible||!pacingLive(p.ready,p.visible,nowMs(),p.heartbeat);});
 }
 p.producerReturned=nowMs();p.holdMs=p.producerReturned-now;
 p.holdTotalMs+=p.holdMs;
 p.holdStartedMs=0;
 return true;
}
}
