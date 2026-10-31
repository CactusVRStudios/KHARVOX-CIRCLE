#pragma once
#include "adaptive_api.hpp"
#include <algorithm>
#include <cmath>
namespace circle::fg {
// Real producer cadence is an INPUT, never a throttle setpoint. The residual
// assigns only missing display slots to interpolation, with a hard 4x ceiling.
struct FractionalFramegen {
 unsigned maximum=4,selected=10,events{},samples{},misses{},outputCalls{},explicitMisses{},pendingPairs{};
 double residual{},realMs{},last{},window{},firstDisplay{},previousDisplay{},periodMs{};
 unsigned previousPlan{};
 const char* reason="waiting for real frames";
 void clearOutput(){samples=misses=outputCalls=explicitMisses=0;firstDisplay=previousDisplay=0;}
 bool refresh(double period){
  if(!std::isfinite(period)||period<4||period>40)return false;
  if(std::abs(periodMs-period)>.05){periodMs=period;residual=realMs=0;previousPlan=0;clearOutput();++events;}
  return true;
 }
 void suspend(double now){residual=realMs=0;previousPlan=0;last=window=now;clearOutput();selected=10;reason="history boundary";}
 AdaptiveDecision decision(double)const{return {selected,0,samples,misses,events,0,0,reason};}
 AdaptiveDecision sample(const AdaptiveSample& s){
  maximum=std::clamp(s.maximum,1u,4u);
  pendingPairs=s.pendingPairs;
  refresh(s.periodMs);
  if(!std::isfinite(s.nowMs))return decision(last);
  if(s.nowMs<last)suspend(s.nowMs);last=s.nowMs;
  if(window&&s.nowMs-window>=2000){window=s.nowMs;clearOutput();}
  if(!window)window=s.nowMs;
  if(s.fresh&&std::isfinite(s.realIntervalMs)&&s.realIntervalMs>0&&s.realIntervalMs<=250&&
     std::isfinite(s.periodMs)&&s.periodMs>=4&&s.periodMs<=40){
   realMs=realMs?realMs*.85+s.realIntervalMs*.15:s.realIntervalMs;
   const auto next=unsigned(std::lround(std::clamp(realMs/s.periodMs,1.,double(maximum))*10));
   if(next!=selected){selected=next;++events;}
   reason=realMs>s.periodMs*maximum?"4x/backend ceiling: render rate too low":"fill measured gaps; real frames first";
  }
  // Delivery deficits are diagnostics, NOT permission to slow the producer.
  if(samples>=90&&misses*100>samples*8)reason="output deficit; preserving real cadence";
  if(pendingPairs>=3)reason="real queue full: drain real frames before interpolation";
  return decision(s.nowMs);
 }
 unsigned plan(double interval,double period,bool continuous){
  if(!refresh(period)||!continuous||!std::isfinite(interval)||interval<=0||interval>250||
     !std::isfinite(period)||period<4||period>40){residual=0;previousPlan=0;return 1;}
  // The interval excludes holds paired at real publication boundaries. Pressure
  // includes a producer blockage latched before retirement, even if the queue
  // has already fallen from three to two when prefetch runs. Discard drain debt.
  if(pendingPairs>=3){residual=0;previousPlan=0;return 1;}
  const double ratio=std::clamp(interval/period,0.,double(maximum));
  residual+=ratio-1;
  unsigned generated=std::min(maximum-1,unsigned(std::floor(std::max(0.,residual)+1e-7)));
  // Causal hysteresis: keep a count while the accumulated unfilled demand
  // stays within 1.25 display intervals. Never borrow future display slots:
  // centered rounding can block the producer even with a shallow queue.
  // A ready second real pair disables retention to prioritize queue latency.
  const auto fits=[&](unsigned count){
   const double debt=residual-double(count-1);
   return count&&count<=maximum&&debt>=-1e-7&&debt<=1.25;
  };
  if(pendingPairs<2){
   if(previousPlan&&fits(previousPlan))generated=previousPlan-1;
   else {
    const auto near=unsigned(std::clamp(std::floor(ratio+.5),1.,double(maximum)));
    if(fits(near))generated=near-1;
   }
  }
  // A short publication burst already supplied a real display slot. Credit it
  // against the next long interval instead of manufacturing extra interpolation.
  residual=std::clamp(residual-generated,-3.,1.25);
  previousPlan=generated+1;return previousPlan;
 }
 void output(double now,double period,bool fresh,double display){
  if(!std::isfinite(now)||!std::isfinite(period)||period<4||period>40||!std::isfinite(display)||display<=0)return;
  refresh(period);
  if(previousDisplay&&display<=previousDisplay)clearOutput();
  if(!outputCalls)firstDisplay=display;
  previousDisplay=display;++outputCalls;if(!fresh)++explicitMisses;
  const auto expected=unsigned(std::min(100000.,std::floor(std::max(0.,display-firstDisplay)/period+.5)+1));
  const unsigned absent=expected>outputCalls?expected-outputCalls:0;
  samples=outputCalls+absent;misses=explicitMisses+absent;
 }
};
}
