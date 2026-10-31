#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <algorithm>
namespace circle::fg {
struct Identity {
 uint64_t pair{}, epoch{};
 std::array<uint32_t,2> eyeSeq{},backend{};
 uint32_t width{},height{};
 bool complete{},reset{},gameplay{}; // gameplay means an eligible rendered scene (including cinematics)
 double sourceMs{}; // monotonic complete stereo publication time, not pose time
 unsigned scene{}; // 0 gameplay, 1 stereo cinema, 2 mono cinema, 3 journal/inspection quad
 double sourceWaitTotalMs{}; // cumulative own holds at the same publication boundary
};
inline double unblockedInterval(double elapsed,double ownHold){
 if(!std::isfinite(elapsed)||!std::isfinite(ownHold)||elapsed<=0||ownHold<0)return 0;
 return std::max(0.,elapsed-ownHold);
}
// Motor's backend advances once PER EYE, simulation tick once PER PAIR.
// Missing input pairs are not repaired by pairing unrelated eye histories.
inline bool consecutive(const Identity& a,const Identity& b){
 return a.complete&&b.complete&&a.gameplay&&b.gameplay&&!b.reset&&a.epoch==b.epoch&&a.scene==b.scene&&
  b.pair>a.pair&&a.width==b.width&&a.height==b.height&&
  b.eyeSeq[0]==a.eyeSeq[0]+1&&b.eyeSeq[1]==a.eyeSeq[1]+1&&
  b.backend[0]==a.backend[0]+2&&b.backend[1]==a.backend[1]+2;
}
inline bool sceneEligible(bool hasView,bool eyes,bool menu,bool inspection,bool cinema=false,bool mono=false){
 // Inspections are rendered scenes, not native pause-menu images. Only the
 // validated mono quad can opt in; depth/MV pair completeness is checked later.
 return hasView&&eyes&&!menu&&(!inspection||(cinema&&mono));
}
inline unsigned sceneKind(bool cinema,bool mono,bool inspection){
 return inspection?3u:cinema?(mono?2u:1u):0u;
}
struct PublishedPair {uint64_t published{},value{};};
inline bool protectPublished(uint64_t published,uint64_t consumed,bool ordered){return ordered&&published>consumed;}
template<size_t N> uint64_t oldestUnconsumed(const std::array<PublishedPair,N>& slots,uint64_t consumed){
 static_assert(N<=4);uint64_t oldest=0;
 for(unsigned i=0;i<N;++i){const auto& s=slots[i];if(s.published>consumed&&s.published==s.value&&(!oldest||s.published<(oldest>>2)))oldest=(s.published<<2)|i;}
 return oldest;
}
enum class Output { Real, Generated };
// A generated image commits an obligation to show its exact endpoint next.
// Failed xrEndFrame does not acknowledge either output. Never admit a newer
// pair while an endpoint is owed; even if its producer has raced ahead.
struct Cadence {
 uint64_t pending{},lastReal{},lastGenerated{};
 unsigned acceptedGenerated{};
 Identity input{};
 // Motor simulation tokens are not a process-lifetime monotonic counter:
 // focus/menu/load transitions can rewind them. Scope output watermarks to
 // the same continuous history as NGX. Never discard an owed real endpoint.
 bool beginInput(const Identity& next){
  if(pending)return false;
  const bool boundary=!consecutive(input,next);
  if(boundary){lastReal=0;lastGenerated=0;}
  input=next;
  return boundary;
 }
 bool canGenerate(uint64_t end)const{return !pending&&end>lastReal&&end>lastGenerated;}
 void accept(Output kind,uint64_t pair,bool success,unsigned generatedIndex=1,unsigned generatedCount=1){
  if(!success)return;
  if(kind==Output::Generated){
   if(!pending&&generatedIndex==1){pending=pair;lastGenerated=pair;acceptedGenerated=1;}
   else if(pending==pair&&generatedIndex==acceptedGenerated+1&&generatedIndex<=generatedCount)acceptedGenerated=generatedIndex;
  }
  else if(!pending||(pending==pair&&acceptedGenerated>=generatedCount)){lastReal=pair;pending=0;acceptedGenerated=0;}
 }
 void reset(){*this={};}
};
// A consumer lease is reusable only after both APIs' GPU work completed and
// all CPU owners (pending endpoint included) released it.
inline bool reusable(unsigned owners,uint64_t produced,uint64_t consumed,uint64_t vkDone,uint64_t dxDone){
 return owners==1&&produced&&vkDone>=produced&&dxDone>=consumed;
}
}
