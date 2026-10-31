// Experimental Motor alternate-eye transport. Normal native simulation/rendering is never re-entered.
#include "vkcore/presenter_impl.hpp"
#include <cmath>

namespace circlevr::vkcore {
namespace {
void setEye(ViewRecord& view,const circle::motor::experimental::LiveEye& eye) {
    auto& out=view.eyes[eye.eye];
    const auto& p=eye.rig.positions[eye.eye];const auto& q=eye.rig.orientations[eye.eye];
    out.pose.position={p[0],p[1],p[2]};out.pose.orientation={q[0],q[1],q[2],q[3]};
    constexpr float halfRadians=3.14159265358979323846f/360.0f;
    out.fov={-eye.fovX*halfRadians,eye.fovX*halfRadians,eye.fovY*halfRadians,-eye.fovY*halfRadians};
}
}
VkSemaphore XrPresenter::Impl::copyLive(VkQueue queue,std::uint32_t family,SwapchainState& sc,
    std::uint32_t image,const VkPresentInfoKHR* info,FamilyCommands& fc,std::uint64_t completed) {
    using namespace circle::motor::experimental;
    const auto fresh=takeLive(reinterpret_cast<std::uint64_t>(gameSwapchain),image);
    capture.poll(dev,completed);
    // A held half remains owned by the existing timeline and ring state machine.
    const bool pair=fresh.valid && liveHeld.valid && pendingSlot<kRingSize &&
        fresh.eye!=liveHeld.eye && fresh.epoch==liveHeld.epoch &&
        fresh.sequence==liveHeld.sequence+1 && fresh.ticks-liveHeld.ticks<150;
    if(!pair && pendingSlot<kRingSize)releasePendingSlot();
    if(!pair)liveHeld={};
    // One missed camera tag must not alternate projection/quad layers at game-frame frequency.
    // Keep the released stereo image briefly; sustained loss still gets a fresh mono image.
    if(!fresh.valid && liveActive() && liveLastPairTicks && GetTickCount64()-liveLastPairTicks<150)
        return VK_NULL_HANDLE;
    const auto slot=pair ? pendingSlot : acquireFreeSlot(fc,completed);
    if(slot>=kRingSize) {++framesDropped;return VK_NULL_HANDLE;}
    const auto carry=pair ? acquireFreeSlot(fc,completed) : kRingSize;
    CopyTarget target{};
    target.firstEye=fresh.valid ? static_cast<unsigned>(fresh.eye) : 0;
    target.eyeCount=fresh.valid ? 1 : 2;
    target.keepOther=pair;target.carrySlot=carry;target.toWindow=true;
    const auto kind=fresh.valid ? (fresh.eye==0 ? stereo_seq::PresentKind::EyeL : stereo_seq::PresentKind::EyeR) : stereo_seq::PresentKind::Mono;
    target.kind=kind;
    VkBuffer buffer=VK_NULL_HANDLE;
    if(fresh.valid) {
        if(fresh.eye==0)buffer=pairCaptureBuffer(sc,completed,pairsPublished.load(),fresh.sequence);
        else if(pair)buffer=capture.bufferFor(dev,1,0,sc.format,sc.extent,completed);
        else capture.cancel();
        if(buffer && capture.once()) {
            char line[256]{};
            snprintf(line,sizeof(line),"Motor native eye %d: camera %llu, frame token %llu, copy QPC %lld, XR time %lld, paused %d; temporal offset, NOT same-state.\n",
                fresh.eye,fresh.sequence,fresh.frameToken,fresh.copyQpc,fresh.rig.displayTime,fresh.paused?1:0);
            capture.once()->sidecar+=line;
        }
    } else capture.cancel();
    const auto value=submitCopy(queue,family,info,sc,image,fc,slot,target,buffer);
    if(!value) {
        ring[slot].state.store(kSlotFree);if(carry<kRingSize)ring[carry].state.store(kSlotFree);
        pendingSlot=kRingSize;liveHeld={};capture.cancel();++framesDropped;return VK_NULL_HANDLE;
    }
    if(fresh.valid && fresh.eye==0)captureCopied(buffer,value,false);
    else if(pair && fresh.eye==1) {if(buffer)capture.copySubmitted(value);capture.submitted(value,fresh.sequence);}
    if(pair) {
        auto& out=ring[slot];out.view={};out.view.seq=fresh.sequence;
        out.view.poseTime=fresh.rig.displayTime;out.view.locatedQpc=std::min(fresh.rig.locatedQpc,liveHeld.rig.locatedQpc);
        out.view.stereo=true;out.view.showEyes=true;out.hasView=true;
        setEye(out.view,fresh);setEye(out.view,liveHeld);
        out.eyes.eye0=fresh.eye==0 ? fresh.sequence : liveHeld.sequence;
        out.eyes.eye1=fresh.eye==1 ? fresh.sequence : liveHeld.sequence;
        out.eyes.eye1View=out.eyes.eye1;
        out.view.pose=out.view.eyes[fresh.eye].pose;out.view.fov=out.view.eyes[fresh.eye].fov;
        out.view.gameOriginValid=true;out.view.gameOrigin={fresh.base[0],fresh.base[1],fresh.base[2]};
        publishSlot(slot,value);pairsPublished.fetch_add(1);
        liveLastPairTicks=GetTickCount64();
        pendingSlot=carry;liveHeld=carry<kRingSize ? fresh : LiveEye{};
    } else if(fresh.valid) {pendingSlot=slot;liveHeld=fresh;}
    else {ring[slot].hasView=false;publishSlot(slot,value);pendingSlot=kRingSize;}
    // Bounded metadata; all actual pixels stay in the donor GPU transport. Flush once per second.
    static FILE* proof=nullptr;static ULONGLONG flushed=0;
    if(!proof) {
        proof=circlevr::vkcore::openLogFileShared((logDirectory()+L"\\live-frames.csv").c_str(),L"wb",_SH_DENYWR);
        if(proof)fprintf(proof,"present,camera,eye,ticks,pose_time,pose_qpc,frame_token,epoch,pair,timeline,base_x,base_y,base_z,cam_x,cam_y,cam_z,fx,fy,fz,lx,ly,lz,ux,uy,uz,qx,qy,qz,qw,copy_qpc,paused\n");
    }
    if(proof) {
        const auto& q=fresh.rig.orientations[fresh.eye==1 ? 1 : 0];
        fprintf(proof,"%llu,%llu,%d,%llu,%lld,%lld,%llu,%llu,%d,%llu",static_cast<unsigned long long>(gamePresents.load()),fresh.sequence,fresh.eye,fresh.ticks,fresh.rig.displayTime,fresh.rig.locatedQpc,fresh.frameToken,fresh.epoch,pair ? 1 : 0,value);
        for(unsigned k=0;k<3;++k)fprintf(proof,",%.9f",fresh.base[k]);
        for(float f:fresh.camera)fprintf(proof,",%.9f",f);
        for(float f:q)fprintf(proof,",%.9f",f);
        fprintf(proof,",%lld,%d\n",fresh.copyQpc,fresh.paused ? 1 : 0);
        const auto now=GetTickCount64();if(now-flushed>=1000) {fflush(proof);flushed=now;}
    }
    logCopyStats(sc,family);
    return sc.presentSemaphores[image];
}
}
