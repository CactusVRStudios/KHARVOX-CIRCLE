#pragma once
#include <algorithm>
#include <cmath>
namespace circle::fg {
inline double automaticWorkload(double cpu,bool cpuFresh,double gpu,bool gpuFresh){
 if(!cpuFresh||!std::isfinite(cpu)||cpu<=0)return 0;
 return gpuFresh&&std::isfinite(gpu)&&gpu>0?std::max(cpu,gpu):cpu;
}
// Select at stereo input boundaries only. Times exclude our producer sleep.
// Slow upgrades avoid oscillation; recovery waits longer than overload relief.
struct AutoFramegen {
 unsigned selected=1,candidate=1;double since{},refresh{};
 double reserve{},last{},trialStart{},cooldown{},lastOutput{};
 unsigned trialFrom{},samples{},misses{},events{};
 const char* reason="measuring";
 void suspend(double now){reserve=0;last=since=now;if(trialFrom){trialStart=now;samples=misses=0;lastOutput=0;}}
 void output(double now,double period,bool fresh){
  if(!trialFrom||now-trialStart<750||period<=0)return;
  ++samples;if(!fresh)++misses;
  if(lastOutput&&now-lastOutput>period*1.5){
   const unsigned skipped=unsigned(std::min(100.,std::floor((now-lastOutput)/period)-1));
   samples+=skipped;misses+=skipped;
  }
  lastOutput=now;
 }
 unsigned choose(double now,double period,double render,double perGenerated,unsigned maximum){
  if(!std::isfinite(now)||!std::isfinite(period)||period<4||period>40||
     !std::isfinite(render)||render<=0||!std::isfinite(perGenerated)||perGenerated<0)return selected;
  maximum=std::clamp(maximum,1u,4u);
  if(std::abs(refresh-period)>.05){refresh=period;candidate=selected;suspend(now);}
  const double dt=last&&now>=last?std::min(100.,now-last):0;last=now;
  if(trialFrom){
   const bool bad=samples>=20&&misses>=3&&misses*10>samples;
   const bool timeout=now-trialStart>=6000;
   if(bad||timeout){selected=std::min(trialFrom,maximum);trialFrom=0;cooldown=now+10000;reserve=0;candidate=selected;since=now;reason=bad?"rollback: missed output deadlines":"rollback: insufficient output evidence";++events;return selected;}
   if(now-trialStart>=3000&&samples>=60&&misses*100<=samples*3){trialFrom=0;reserve=0;reason="lower stage confirmed";++events;}
   else return selected;
  }
  unsigned wanted=maximum;
  for(unsigned m=1;m<=maximum;++m)
   if((render+perGenerated*(m-1))*1.12<=period*m){wanted=m;break;}
  // Overload still switches up promptly. Recovery tests only the NEXT lower
  // stage, so alternating ideal targets (2/3 while at 4) cannot restart it.
  if(wanted>selected){
   reserve=std::max(0.,reserve-dt*2);
   if(wanted!=candidate||now<since){candidate=wanted;since=now;}
   if(now-since>=1000){selected=candidate;reason="upshift: workload";++events;reserve=0;}
  }else{
   candidate=selected;since=now;
   const unsigned next=selected>1?selected-1:1;
   const bool fits=selected>1&&(render+perGenerated*(next-1))*1.12<=period*next;
   reserve=now<cooldown?0:std::clamp(reserve+(fits?dt:-dt*2),0.,5000.);
   if(reserve>=5000){trialFrom=selected;selected=next;trialStart=now;samples=misses=0;lastOutput=0;reserve=0;reason="probing next lower stage";++events;}
  }
  selected=std::min(selected,maximum);return selected;
 }
};
}
