#pragma once
#include <algorithm>
#include <cmath>

namespace circle::fg {
// Circle VR adaptive pacing feedback.
// Copyright (c) 2026 dariulone. Full notice in licenses/frame-generation-MIT.txt.
struct AdaptivePacing {
 double interval{},base{},windowStart{},lastMiss{};
 unsigned misses{};
 double observe(double now,double minimum,bool missed){
  if(!std::isfinite(now)||!std::isfinite(minimum)||minimum<=0)return interval;
  if(!interval||base!=minimum||now<windowStart||now-windowStart>2000){
   interval=base=minimum;windowStart=lastMiss=now;misses=0;
  }
  if(missed){++misses;lastMiss=now;}
  if(now-windowStart>=1000){
   if(misses>=2)interval=std::min(base*2,interval*1.04);
   else if(!misses&&now-lastMiss>=5000)interval=std::max(base,interval-base*.005);
   windowStart=now;misses=0;
  }
  return interval;
 }
};
// One schedule for the entire producer, never one per native worker or eye.
// Advance on a fixed grid rather than adding the hook's cost each time.
struct ProducerClock {
 double last{},base{};
 double deadline(double now,double interval,double refreshBase,bool enabled){
  if(!enabled||!std::isfinite(now)||interval<=0||!std::isfinite(interval)){
   *this={};return now;
  }
  if(!last||base!=refreshBase||now<last-interval||now>last+interval*1.5){
   last=now;base=refreshBase;return now;
  }
  last+=interval;return std::max(now,last);
 }
};
inline bool pacingLive(bool ready,bool visible,double now,double heartbeat){
 return ready&&visible&&heartbeat>0&&now>=heartbeat&&now-heartbeat<250;
}
}
