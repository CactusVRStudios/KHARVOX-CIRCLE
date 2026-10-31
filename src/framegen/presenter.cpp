#include "vkcore/presenter_impl.hpp"
#include "motor/sequential.hpp"
#include "motor/frame_generation.hpp"
#include <filesystem>
#include <tlhelp32.h>
namespace circlevr::vkcore {
namespace {
bool externalFg(){
 HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());if(h==INVALID_HANDLE_VALUE)return true;
 MODULEENTRY32W e{};e.dwSize=sizeof(e);bool found=false;
 if(Module32FirstW(h,&e))do{std::wstring n=e.szModule;std::transform(n.begin(),n.end(),n.begin(),::towlower);
  if(n.find(L"xrframebridge")!=n.npos||n.find(L"openxr-fg")!=n.npos||n.find(L"ofxr")!=n.npos)found=true;
 }while(Module32NextW(h,&e));CloseHandle(h);return found;
}
XrPosef interpolatePose(const XrPosef& a,const XrPosef& b,float t){
 XrPosef p{};p.position={a.position.x+(b.position.x-a.position.x)*t,a.position.y+(b.position.y-a.position.y)*t,a.position.z+(b.position.z-a.position.z)*t};
 float dot=a.orientation.x*b.orientation.x+a.orientation.y*b.orientation.y+a.orientation.z*b.orientation.z+a.orientation.w*b.orientation.w;
 const float sign=dot<0?-1.f:1.f;dot=std::clamp(std::abs(dot),0.f,1.f);
 float x=1-t,y=t;if(dot<.9995f){float angle=std::acos(dot),den=std::sin(angle);x=std::sin((1-t)*angle)/den;y=std::sin(t*angle)/den;}
 auto& q=p.orientation;q={x*a.orientation.x+sign*y*b.orientation.x,x*a.orientation.y+sign*y*b.orientation.y,x*a.orientation.z+sign*y*b.orientation.z,x*a.orientation.w+sign*y*b.orientation.w};
 float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);if(n>1e-6f){q.x/=n;q.y/=n;q.z/=n;q.w/=n;}else q=b.orientation;return p;
}
void generatedView(ViewRecord& out,const ViewRecord& a,const ViewRecord& b,unsigned index,unsigned count){
 const float t=float(index+1)/float(count+1);out=b;
 for(unsigned e=0;e<2;++e)out.eyes[e].pose=interpolatePose(a.eyes[e].pose,b.eyes[e].pose,t);
 out.pose=interpolatePose(a.pose,b.pose,t);
 out.poseTime=a.poseTime+(b.poseTime-a.poseTime)*(index+1)/(count+1);
 out.locatedQpc=a.locatedQpc+(b.locatedQpc-a.locatedQpc)*(index+1)/(count+1);
}
bool sameProjection(const ViewRecord& a,const ViewRecord& b){
 if(a.runtimeFovCrop!=b.runtimeFovCrop)return false;
 if(a.runtimeFovCrop)for(unsigned e=0;e<2;++e)for(unsigned k=0;k<4;++k)
  if(std::abs(a.runtimeFov[e][k]-b.runtimeFov[e][k])>.0001f)return false;
 for(unsigned e=0;e<2;++e)for(unsigned k=0;k<2;++k)
  if(std::abs(a.cinemaCrop[e][k]-b.cinemaCrop[e][k])>.0001f)return false;
 for(unsigned e=0;e<2;++e){const float* x=&a.eyes[e].fov.angleLeft;const float* y=&b.eyes[e].fov.angleLeft;for(int j=0;j<4;++j)if(std::abs(x[j]-y[j])>.0001f)return false;}
 return true;
}
}
void XrPresenter::Impl::initializeFramegen(){
 if(!circle::fg::requested())return;
 if(fgTransport){fgPrepared={};fgGenerator.reset();fgCadence.reset();fgEndpointSlot=UINT32_MAX;fgPreviousValid=fgAwaitAck=fgGenerated=false;return;}
 circle::fg::state("requested","Checking runtime capabilities");
 if(!circle::motor::frame_generation::guardInstalled()){circle::fg::state("fallback","Native desktop FG guard unavailable");return;}
 if(circle::motor::sequential::monoVrEnabled()){circle::fg::state("fallback","Integrated VR FG requires stereo mode");return;}
 if(externalFg()){circle::fg::state("fallback","External FG layer detected or module inventory unavailable");return;}
 wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
 auto directory=std::filesystem::path(path).parent_path()/L"streamline";
 if(!fgGenerator.initialize(d3dDevice.Get(),d3dQueue.Get(),directory.c_str()))return;
 fgTransport=std::make_shared<circle::fg::Transport>(dev,d3dDevice.Get(),sharedFence.Get(),copyFence.Get());fgTransport->connect();
 CIRCLE_VR_LOG("vr-fg: integrated per-eye frame generation requested; fresh input only; generated then pinned real endpoint; real-only preference %d retained",settings.onlySubmitRealFrames);
}
void XrPresenter::Impl::prefetchFramegen(){
 using namespace circle::fg;
 if(!retireGpuCopy())return;
 // Only the XR worker records commands. Start the next adjacent pair before
 // xrWaitFrame, after the previous real endpoint has actually been accepted.
 // No XR swapchain image is acquired or released by this preparation step.
 if(!fgTransport||!preserveRealPairs()||fgPrepared.slot!=UINT32_MAX||
    fgEndpointSlot!=UINT32_MAX||fgAwaitAck||heldCopy.stalled||
    resizeRequested.load()||stop.load()||!ringReady.load()||
    copyFence->GetCompletedValue()<copyFenceValue)return;
 uint32_t index{};uint64_t value{};
 if(!takeRenderedSlot(index,value,qpcNow(),true))return;
 auto& slot=ring[index];
 if(FAILED(d3dAllocator->Reset())||FAILED(d3dList->Reset(d3dAllocator.Get(),nullptr))){slot.state.store(kSlotFree);return;}
 ViewRecord view=slot.view;
 const bool generated=prepareFramegen(slot,index,value,view);
 if(FAILED(d3dList->Close())){
  slot.state.store(kSlotFree);fgEndpointSlot=UINT32_MAX;
  fgGenerator.reset();fgCadence.reset();fgPreviousValid=false;return;
 }
 d3dQueue->Wait(sharedFence.Get(),value);
 ID3D12CommandList* lists[]={d3dList.Get()};d3dQueue->ExecuteCommandLists(1,lists);
 d3dQueue->Signal(copyFence.Get(),++copyFenceValue);
 for(auto& input:slot.fgInputs)if(input)input->consumed=copyFenceValue;
 fgPrepared={index,value,copyFenceValue,view,generated};
}
bool XrPresenter::Impl::prepareFramegen(RingSlot& slot,uint32_t index,uint64_t value,ViewRecord& outputView){
 using namespace circle::fg;
 fgCopyGenerated=false;
 if(!fgTransport)return false;
 fgGenerator.pollTiming();
 // The real endpoint was pinned when its midpoint was recorded. Do not run
 // the generator again for that same pair.
 if(fgEndpointSlot!=UINT32_MAX){
  if(fgOutputIndex<fgGenerator.count()){
   generatedView(outputView,fgStartView,fgEndpointView,fgOutputIndex,fgGenerator.count());
   fgCopyGenerated=true;
  }
  return fgCopyGenerated;
 }
 Identity id{};id.sourceMs=slot.publishedMs;id.pair=slot.view.seq;id.epoch=slot.fgEpoch;id.width=eyeExtent.width;id.height=eyeExtent.height;
 id.sourceWaitTotalMs=slot.producerWaitTotalMs;
 id.gameplay=sceneEligible(slot.hasView,slot.view.showEyes,
  menuUp.load()||circle::motor::sequential::menuActive(),slot.view.inspection,slot.view.cutscene,slot.view.monoCinema);
 id.scene=sceneKind(slot.view.cutscene,slot.view.monoCinema,slot.view.inspection);
 id.complete=slot.fgInputs[0]&&slot.fgInputs[1];
 if(id.complete){for(unsigned e=0;e<2;++e){auto& c=slot.fgInputs[e]->capture;id.eyeSeq[e]=c.tag.eyeSeq;id.backend[e]=c.tag.backendFrame;
  id.complete=id.complete&&c.tag.tick==id.pair&&unsigned(c.tag.eye)==e+1&&circlevr::stereo_seq::drawsEyeView(c.tag);
  id.reset=id.reset||c.camera.reset;}}
 if(!fgPreviousValid||!sameProjection(fgPreviousView,slot.view)||
    fgPreviousView.cutscene!=slot.view.cutscene||fgPreviousView.monoCinema!=slot.view.monoCinema||
    fgPreviousView.inspection!=slot.view.inspection)id.reset=true;
 if(shownHasView&&qpcSeconds(qpcNow()-shownView.locatedQpc)>.25)id.reset=true;
 const auto oldReal=fgCadence.lastReal,oldGenerated=fgCadence.lastGenerated;
 if(fgCadence.beginInput(id)&&(oldGenerated||(id.gameplay&&oldReal))){
  static unsigned boundaries=0;
  if(++boundaries<=16||boundaries%600==0)CIRCLE_VR_LOG("vr-fg: output history boundary real %llu generated %llu -> input %llu epoch %llu complete %d eligible %d reset %d scene %u",
   oldReal,oldGenerated,id.pair,id.epoch,id.complete,id.gameplay,id.reset,id.scene);
 }
 bool made=fgGenerator.record(d3dList.Get(),slot.resource.Get(),slot.fgInputs,id);
 if(fractionalPacingActive()){
  static int64_t reportQpc{};const auto stamp=qpcNow();
  if(!reportQpc||qpcSeconds(stamp-reportQpc)>=1){reportQpc=stamp;
   const double sourceMs=fgPreviousValid?id.sourceMs-fgPreviousSourceMs:0;
   const auto planned=fgGenerator.count()+1;
   CIRCLE_VR_LOG("vr-fg-fractional-pair: pair %llu plan %ux generated %u interpolated %d publicationDelta %.3f ms covered %.3f ms producerHoldDiagnostic %.3f ms epoch %llu reset %d scene %u complete %d eligible %d",
    id.pair,planned,fgGenerator.count(),made,sourceMs,planned*displayPeriodMs(),slot.fgInputs[0]?slot.fgInputs[0]->capture.producerHoldMs:0.,id.epoch,id.reset,id.scene,id.complete,id.gameplay);
  }
 }
 if(made&&fgCadence.canGenerate(id.pair)){
  fgEndpointSlot=index;fgEndpointValue=value;fgEndpointView=slot.view;
  fgStartView=fgPreviousView;fgOutputIndex=0;
  generatedView(outputView,fgStartView,fgEndpointView,fgOutputIndex,fgGenerator.count());
  fgCopyGenerated=true;
 }
 fgPreviousView=slot.view;fgPreviousSourceMs=id.sourceMs;fgPreviousValid=id.gameplay&&id.complete;
 return fgCopyGenerated;
}
void XrPresenter::Impl::acknowledgeFramegen(bool success,bool image,int64_t displayTimeNs){
 if(!circle::fg::requested())return;
 circle::fg::autoOutput(performanceImageSerial,success&&image,displayTimeNs);
 auto before=fgCadence.lastGenerated;
 if(fgAwaitAck)fgCadence.accept(fgGenerated?circle::fg::Output::Generated:circle::fg::Output::Real,shownView.seq,success&&image,fgOutputIndex+1,fgGenerator.count());
 if(success&&image&&fgAwaitAck){
  fgAwaitAck=false;
  if(!fgGenerated)circle::fg::pairRetired(fgCompletedValue);
  if(fgGenerated){++fgOutputIndex;circle::fg::state("active","Generated stereo pair reached xrEndFrame",true);
   if(before!=fgCadence.lastGenerated) {static unsigned n=0;if(++n<=8||n%120==0)CIRCLE_VR_LOG("vr-fg: XR generated pair %llu accepted, endpoint pinned slot %u; GPU %.3f ms",shownView.seq,fgEndpointSlot,fgGenerator.gpuMs);}}
  else if(fgEndpointSlot!=UINT32_MAX){fgEndpointSlot=UINT32_MAX;}
 }
 circle::fg::submitted(fgGenerated,performanceImageSerial,success,image,fgGenerator.gpuMs,shownHasView?qpcSeconds(qpcNow()-shownView.locatedQpc)*1000:0);
}
}
