#include "bridge.hpp"
#include "fidelityfx.hpp"
#include "pacing.hpp"
#include <windows.h>
#include <mutex>
#include <chrono>
#include "vkcore/log.hpp"
namespace circle::fg {
namespace {std::mutex mutex;CaptureCallback callback;Status current;uint64_t lastSerial{};double lastTime{};uint64_t lastReal{},lastGenerated{};}
bool requested(){static bool on=[] {char s[8]{};return GetEnvironmentVariableA("CIRCLE_VR_DLSS_FG",s,8)==1&&s[0]=='1';}();return on||fidelityRequested();}
void setCapture(CaptureCallback cb){std::lock_guard lock(mutex);callback=std::move(cb);}
void capture(const Capture& input){CaptureCallback cb;{std::lock_guard lock(mutex);cb=callback;}if(cb){auto tagged=input;tagged.producerHoldMs=producerHoldMs();cb(tagged);}}
void state(const char* value,const char* reason,bool available){std::lock_guard lock(mutex);
 if(current.state!=value||current.reason!=reason){static uint64_t n=0;if(++n<=64||n%600==0)circlevr::vkcore::logf("vr-fg: state %s available %d reason %s",value,available,reason);}
 current.state=value;current.reason=reason;current.available=available;}
Status status(){std::lock_guard lock(mutex);return current;}
void configure(double multiplier,const char* backend){std::lock_guard lock(mutex);current.multiplier=multiplier;current.backend=backend;}
void countCapture(){std::lock_guard lock(mutex);++current.captures;}
void evaluated(bool interpolated,bool gap){std::lock_guard lock(mutex);++current.evaluations;if(!interpolated)++current.warmups;if(gap)++current.historyGaps;}
void submitted(bool generated,uint64_t serial,bool success,bool image,double gpuMs,double ageMs){
 if(!requested()||!success||!image)return;
 std::lock_guard lock(mutex);
 if(serial==lastSerial)++current.repeated;
 else {lastSerial=serial;if(generated)++current.generatedSubmitted;else ++current.realSubmitted;}
 current.gpuMs=gpuMs;current.imageAgeMs=ageMs;
 double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
 if(lastTime&&now-lastTime>=1){current.realRate=(current.realSubmitted-lastReal)/(now-lastTime);current.generatedRate=(current.generatedSubmitted-lastGenerated)/(now-lastTime);lastTime=0;}
 if(!lastTime){lastTime=now;lastReal=current.realSubmitted;lastGenerated=current.generatedSubmitted;}
 static double lastReport{};
 if(now-lastReport>=10){lastReport=now;
  circlevr::vkcore::logf("vr-fg: cadence real %.2f/s generated %.2f/s totals real %llu generated %llu repeat %llu evaluations %llu warmup %llu gaps %llu producerInterval %.3f ms GPU %.3f ms poseAge %.3f ms",
   current.realRate,current.generatedRate,current.realSubmitted,current.generatedSubmitted,current.repeated,
   current.evaluations,current.warmups,current.historyGaps,pacingIntervalMs(),current.gpuMs,current.imageAgeMs);
 }
}
}
