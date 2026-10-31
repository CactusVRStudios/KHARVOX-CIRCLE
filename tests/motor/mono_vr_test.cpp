#include "mono_vr.hpp"
#include "symmetric_optics.hpp"
#include "runtime_optics_crop.hpp"
#include "streamline_policy.hpp"
#include "gi_history.hpp"
#include "nrd_history.hpp"
#include "stereo_seq/eye_tags.hpp"
#include "stereo_seq/stereo_taa.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

void check(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
int main(){
    using namespace circle::motor;
    std::array<experimental::LiveEye,2> eyes{};
    for(unsigned e=0;e<2;++e){
        eyes[e].valid=true;eyes[e].eye=e;eyes[e].sequence=77;
        eyes[e].camera={float(e)*.064f,2,3,1,0,0,0,1,0,0,0,1};
        eyes[e].rig.positions={{{-.032f,1.7f,0},{.032f,1.7f,0}}};
        eyes[e].rig.fovs={{{-.95f,.75f,.9f,-.85f},{-.75f,.95f,.88f,-.9f}}};
        eyes[e].rig.orientations={{{0,0,0,1},{0,0,0,1}}};
    }
    check(nativeRuntimeOptics("Oculus")&&nativeRuntimeOptics("Meta")&&!nativeRuntimeOptics("VirtualDesktopXR")&&!nativeRuntimeOptics("SteamVR/OpenXR")&&!nativeRuntimeOptics(""),"runtime crop only for Meta");
    const std::array<float,4> wide{-.942478f,.942478f,.959931f,-.959931f};
    for(int w:{1688,2248})for(int h:{1816,2416})for(unsigned e=0;e<2;++e){
        const std::array<float,4> native{e?-.698132f:-.942478f,e?.942478f:.698132f,.767945f,-.959931f};
        auto c=runtimeOpticsCrop(wide,native,w,h);check(bool(c),"live Meta FOV crop valid");
        check(c->x>=0&&c->y>=0&&c->x+c->width<=w&&c->y+c->height<=h,"crop stays in its own eye half");
        for(float u:{0.f,.2f,.5f,.8f,1.f}){
            float tx=std::tan(wide[0])+(std::tan(wide[1])-std::tan(wide[0]))*(c->x+u*c->width)/w;
            float ty=std::tan(wide[2])+(std::tan(wide[3])-std::tan(wide[2]))*(c->y+u*c->height)/h;
            check(std::abs(tx-(std::tan(c->fov[0])+(std::tan(c->fov[1])-std::tan(c->fov[0]))*u))<.00001f&&
                  std::abs(ty-(std::tan(c->fov[2])+(std::tan(c->fov[3])-std::tan(c->fov[2]))*u))<.00001f,"submitted crop rays exactly match symmetric render incl pixel rounding");
        }
        for(unsigned k=0;k<4;++k)check(std::abs(c->fov[k]-native[k])<.001f,"crop remains within pixel rounding of native Meta optics");
    }
    check(!runtimeOpticsCrop(wide,{-1.2f,1.2f,1.2f,-1.2f},1688,1816)&&!runtimeOpticsCrop(wide,wide,0,1),"out of bounds/empty crop rejected");
    const auto originalStereo=eyes;
    check(symmetricStereoOptics(eyes),"symmetric stereo diagnostic accepted");
    for(unsigned e=0;e<2;++e){
        check(eyes[e].camera==originalStereo[e].camera&&eyes[e].rig.positions==originalStereo[e].rig.positions&&eyes[e].rig.orientations==originalStereo[e].rig.orientations,"diagnostic retains physical stereo poses");
        check(eyes[e].rig.fovs[0]==std::array<float,4>{-.95f,.95f,.9f,-.9f}&&eyes[e].rig.fovs[1]==eyes[e].rig.fovs[0],"render and submitted FOV enclosing and symmetric");
    }
    eyes=originalStereo;eyes[1].rig.fovs[1][2]=std::numeric_limits<float>::quiet_NaN();
    check(!symmetricStereoOptics(eyes)&&eyes[0].rig.fovs==originalStereo[0].rig.fovs,"invalid optics rejected without partial mutation");
    eyes=originalStereo;eyes[1].cinema=true;
    check(!symmetricStereoOptics(eyes),"cinema untouched");
    eyes=originalStereo;
    check(centerMonoViews(eyes),"valid mono camera");
    check(eyes[0].camera==eyes[1].camera&&std::abs(eyes[0].camera[0]-.032f)<1e-6f,"centre native camera, zero parallax");
    check(eyes[0].rig.positions[0]==eyes[0].rig.positions[1]&&eyes[0].rig.positions[0][0]==0,"centre XR origin");
    check(eyes[0].rig.fovs[0]==eyes[1].rig.fovs[1]&&eyes[0].rig.fovs[0]==std::array<float,4>{-.95f,.95f,.9f,-.9f},"shared enclosing FOV");
    const auto centred=eyes;check(centerMonoViews(eyes)&&eyes[0].camera==centred[0].camera,"idempotent camera");
    eyes[0].rig.fovs[0][0]=std::numeric_limits<float>::quiet_NaN();check(!centerMonoViews(eyes),"reject invalid optics");
    eyes=centred;eyes[1].valid=false;check(!centerMonoViews(eyes),"reject incomplete view");

    // Consecutive LEFT tags are real single renders, not synthetic alternating
    // eyes. Their temporal indices must ping-pong and not reset every frame.
    circlevr::stereo_seq::EyeTagQueue tags;tags.rebase(10);
    TemporalContinuity taa;StreamlineHistory dlss;GiHistory gi;NrdEyeHistory nrd;
    int previousOutput=-1;
    for(unsigned i=0;i<8;++i){
        circlevr::stereo_seq::RenderTag t{};t.eye=circlevr::stereo_seq::Eye::Left;t.tick=100+i;t.renderFrame=20+i;t.viewApplied=true;
        check(tags.push(t),"queue single render");auto match=tags.pop(11+i);check(match.tagged,"one tag per native present");
        t=match.tag;
        const auto output=circlevr::stereo_seq::pickAccumulation(circlevr::stereo_seq::AccumRole::Output,&t,true);
        check(output.pair==0&&output.index!=previousOutput,"one eye history alternates");previousOutput=output.index;
        check(taa.reset(0,t.eyeSeq,t.backendFrame,1)==(i==0),"TAA continuous at mono cadence");
        check(dlss.reset(0,t.eyeSeq,t.backendFrame,2496,2688,false,1)==(i==0),"DLSS continuous at mono cadence");
        check(gi.before(0,false,t.backendFrame,{},0).restart==(i==0),"GI continuous at mono cadence");
        NrdSettings settings{};check(nrd.prepare(settings,t.backendFrame,false,false)==(i==0),"NRD continuous at mono cadence");
    }
    check(taa.reset(0,9,21,1),"missing mono frame resets history");
    check(dlss.reset(0,8,19,1920,1080,false,1),"mono resize resets DLSS");
    check(dlss.reset(0,9,20,1920,1080,true,1),"native cut resets DLSS");
    TemporalContinuity stereo;check(stereo.reset(0,0,10),"stereo first frame");
    check(!stereo.reset(0,1,12),"stereo two-render cadence preserved");
    // Reproduce the observed 2X stream: one native backend tag, then another
    // present with the same backend counter. Extra presents must not publish a
    // quad, consume the next tag, invent a pose or count as fresh XR images.
    using namespace circlevr::stereo_seq;
    EyeTagQueue fgTags;fgTags.rebase(100);
    unsigned xrImages=0,quadImages=0;
    for(unsigned frame=101;frame<109;++frame){
        RenderTag t{};t.eye=(frame==104||frame==105)?Eye::Mono:Eye::Left;
        t.tick=frame;t.renderFrame=frame;t.viewApplied=t.eye==Eye::Left;
        check(fgTags.push(t),"FG native render queued");
        auto native=fgTags.pop(frame);
        const auto action=monoPresentAction(native);
        check(action==(t.eye==Eye::Mono?PairAction::ShowMono:PairAction::CompletePair),"native menu/gameplay transition remains available");
        ++xrImages;if(action==PairAction::ShowMono)++quadImages;
        for(unsigned extra=0;extra<3;++extra){
            check(monoPresentAction(fgTags.pop(frame))==PairAction::Drop,"extra 2X/4X present cannot replace VR with cinema");
            check(fgTags.synced(),"extra presents preserve native tag synchronization");
        }
    }
    check(xrImages==8&&quadImages==2&&fgTags.stats().matched==8&&fgTags.stats().untagged==24,"only native images and explicit menus counted");
    check(monoPresentAction({})==PairAction::Drop,"startup without camera cannot publish guessed view");
    check(monoPresentAction({},true)==PairAction::ShowMono,"confirmed main menu remains visible before first queue base");
    check(monoPresentAction({},false)==PairAction::Drop,"closing main menu cannot leak unknown images into gameplay");
    PresentMatch invalid{};invalid.tagged=true;invalid.tag.eye=Eye::Left;invalid.tag.tick=1;
    check(monoPresentAction(invalid)==PairAction::Drop,"left view without application rejected");
    invalid.tag.viewApplied=true;invalid.tag.tick=0;
    check(monoPresentAction(invalid)==PairAction::Drop,"missing camera token rejected");
    invalid.tag.tick=1;invalid.tag.eye=Eye::Right;
    check(monoPresentAction(invalid)==PairAction::Drop,"unexpected right render cannot start pairing in Mono");
    fgTags.desync(DesyncReason::Requested);
    check(monoPresentAction(fgTags.pop(109))==PairAction::Drop,"desync does not synthesize cinema");
    fgTags.rebase(200);RenderTag menu{};menu.eye=Eye::Mono;
    check(fgTags.push(menu)&&monoPresentAction(fgTags.pop(201))==PairAction::ShowMono,"native menu resumes after resize/rebase");
    std::puts("PASS: Mono camera, shared FOV, real single-render tags, TAA/DLSS/GI/NRD history cadence");
}
