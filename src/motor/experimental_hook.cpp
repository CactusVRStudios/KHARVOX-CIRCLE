#include "laser_sight.hpp"
#include "motor/diagnostics.hpp"
#include "motor/build_profile.hpp"
#include "motor/experimental.hpp"
#include "motor/cinema_projection.hpp"
#include "motor/locomotion_runtime.hpp"
#include "motor/mounted_gun_runtime.hpp"
#include "motor/boat_runtime.hpp"
#include "motor/hand_runtime.hpp"
#include "motor/interaction_runtime.hpp"
#include "motor/interaction_policy.hpp"
#include "motor/sequential.hpp"
#include "motor/adapter.hpp"
#include "motor/native_state.hpp"
#include "vkcore/log.hpp"
#include "vkcore/bug_capture.hpp"
#include <safetyhook.hpp>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <cmath>

namespace circle::motor::experimental {
namespace {
constexpr std::size_t kViewBytes=0x9a0;
struct Sample {
    std::uint64_t sequence=0,ticks=0;
    std::uintptr_t source=0,owner=0,component=0,task=0,frame=0,viewOffset=0;
    std::array<unsigned char,kViewBytes> view{};
};
std::atomic_flag g_sampleLock=ATOMIC_FLAG_INIT;
Sample g_sample;
std::atomic<std::uint64_t> g_calls{0},g_reads{0},g_invalid{0};
safetyhook::MidHook* g_observer=nullptr;
safetyhook::MidHook* g_afterCopy=nullptr;
safetyhook::MidHook* g_hideGui=nullptr;
enum class Probe { None, Yaw, Pitch, Roll, Position, Left, Right };
std::atomic<Probe> g_probe{Probe::None};
std::atomic<std::uint64_t> g_valid{0},g_overrides{0},g_restored{0},g_mismatch{0};
std::atomic<std::uintptr_t> g_base{0};
EyeRig g_rig,g_pairRig;
CinemaPanel g_cinemaPanel;
EyeRig g_liveReference;
LiveEye g_liveLatest,g_livePrevious;
LiveEye g_renderPrevious;
std::atomic<bool> g_liveStopped{false},g_recenter{true};
std::atomic<std::int64_t> g_spaceRecenterTime{0};
std::uint64_t g_liveEpoch=0,g_liveNumber=0,g_acquiredSequence=0;
struct Acquired {std::uint64_t swapchain=0;std::uint32_t image=0;LiveEye view,front;};
std::array<Acquired,16> g_acquired;
std::array<std::atomic<std::uint64_t>,5> g_liveRejects{};
std::array<float,12> g_baseCamera{};
std::atomic<bool> g_pairLatched{false},g_nativePaused{false};
std::atomic<std::uintptr_t> g_pauseOwner{0};
std::uintptr_t g_pairOwner=0;
std::array<std::atomic<std::uint64_t>,2> g_eyeCopies{};
std::array<std::array<float,12>,2> g_eyeCameras{};
struct PrivateView {
    alignas(16) std::array<unsigned char,kViewBytes> bytes{};
    std::uintptr_t original=0,destination=0;
    std::array<unsigned char,48> originalCamera{};
    bool active=false;
    int eye=-1;
    LiveEye live;
};
thread_local PrivateView g_private;
std::once_flag g_once;
bool g_installed=false;
std::string g_error;
bool read(std::uintptr_t address,void* bytes,std::size_t size) {
    SIZE_T n=0;
    return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),bytes,size,&n) && n==size;
}
float field(const std::array<unsigned char,kViewBytes>& view,std::size_t offset) {
    float f=0;std::memcpy(&f,view.data()+offset,4);return f;
}
bool cameraValid(const Sample& sample) {
    std::uintptr_t vtable=0;
    if(!read(sample.owner,&vtable,8) || vtable!=g_base+build::rva(0x36e6ec0))return false;
    const float fovX=field(sample.view,0x64),fovY=field(sample.view,0x68);
    if(!(fovX>10 && fovX<170 && fovY>10 && fovY<170))return false;
    for(unsigned i=0;i<3;++i)if(!std::isfinite(field(sample.view,0xa8+i*4)))return false;
    float basis[9]{};std::memcpy(basis,sample.view.data()+0xb4,sizeof(basis));
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col) {
        float dot=0;
        for(unsigned k=0;k<3;++k)dot+=basis[row*3+k]*basis[col*3+k];
        if(!std::isfinite(dot) || std::fabs(dot-(row==col ? 1.0f : 0.0f))>0.02f)return false;
    }
    const float determinant=basis[0]*(basis[4]*basis[8]-basis[5]*basis[7])-
        basis[1]*(basis[3]*basis[8]-basis[5]*basis[6])+basis[2]*(basis[3]*basis[7]-basis[4]*basis[6]);
    return determinant>0.95f && determinant<1.05f;
}
void adjustView(Probe probe) {
    float basis[9]{};std::memcpy(basis,g_private.bytes.data()+0xb4,sizeof(basis));
    if(probe==Probe::Position) {
        float origin[3]{};std::memcpy(origin,g_private.bytes.data()+0xa8,sizeof(origin));
        for(unsigned i=0;i<3;++i)origin[i]+=basis[3+i]*0.03f; // diagnostic game units; scale is not asserted
        std::memcpy(g_private.bytes.data()+0xa8,origin,sizeof(origin));return;
    }
    float axis[3]={0,0,1};
    if(probe==Probe::Pitch)std::memcpy(axis,basis+3,sizeof(axis));
    if(probe==Probe::Roll)std::memcpy(axis,basis,sizeof(axis));
    constexpr float radians=5.0f*3.14159265358979323846f/180.0f;
    const float c=std::cos(radians),s=std::sin(radians);
    for(unsigned row=0;row<3;++row) {
        const float* v=basis+row*3;
        const float dot=v[0]*axis[0]+v[1]*axis[1]+v[2]*axis[2];
        const float cross[3]={axis[1]*v[2]-axis[2]*v[1],axis[2]*v[0]-axis[0]*v[2],axis[0]*v[1]-axis[1]*v[0]};
        float rotated[3]{};
        for(unsigned i=0;i<3;++i)rotated[i]=v[i]*c+cross[i]*s+axis[i]*dot*(1-c);
        std::memcpy(g_private.bytes.data()+0xb4+row*12,rotated,sizeof(rotated));
    }
}
bool nativePause(const Sample& sample) {
    std::uintptr_t input=0;
    std::array<unsigned char,0x514> bytes{};
    if(!read(sample.owner+0x42188,&input,8) || !read(input,bytes.data(),bytes.size()) || bytes[0x48a]!=1)return false;
    float latest[12]{};std::memcpy(latest,bytes.data()+0x4e4,sizeof(latest));
    // Independently observed live Input owner: its latest view matches the selected output.
    for(unsigned i=0;i<12;++i) {
        if(!std::isfinite(latest[i]))return false;
        // The renderer feeds the copied eye view back into latestView. Correlate
        // ownership before latching; subsequent eyes rely on the native pause flag.
        if(!g_pairLatched.load() && std::fabs(latest[i]-field(sample.view,0xa8+i*4))>0.02f)return false;
    }
    return bytes[0x48]==1 && bytes[0x488]<=1;
}
void eyeView(Probe probe) {
    // Both eye cameras use one latched base view and one actual OpenXR eye rig.
    std::memcpy(g_private.bytes.data()+0xa8,g_baseCamera.data(),48);
    const auto eye=probe==Probe::Left ? 0U : 1U;
    const auto& l=g_pairRig.positions[0];const auto& r=g_pairRig.positions[1];
    float delta[3]{};for(unsigned k=0;k<3;++k)delta[k]=g_pairRig.positions[eye][k]-(g_private.live.valid ? (g_liveReference.positions[0][k]+g_liveReference.positions[1][k])*0.5f : (l[k]+r[k])*0.5f);
    // Native camera owns body translation in every same-frame stereo state,
    // including interactions that temporarily suspend roomscale locomotion.
    if(sequential::requested())for(unsigned k=0;k<3;++k)delta[k]=g_pairRig.positions[eye][k]-(l[k]+r[k])*.5f;
    const auto q=locomotion::uprightReference(g_private.live.valid ? g_liveReference.orientations[0] : g_pairRig.orientations[0]);
    const float u[3]={-q[0],-q[1],-q[2]};
    const float cross[3]={u[1]*delta[2]-u[2]*delta[1],u[2]*delta[0]-u[0]*delta[2],u[0]*delta[1]-u[1]*delta[0]};
    const float cross2[3]={u[1]*cross[2]-u[2]*cross[1],u[2]*cross[0]-u[0]*cross[2],u[0]*cross[1]-u[1]*cross[0]};
    for(unsigned k=0;k<3;++k)delta[k]+=2*(q[3]*cross[k]+cross2[k]);
    float origin[3]={g_baseCamera[0],g_baseCamera[1],g_baseCamera[2]};
    for(unsigned k=0;k<3;++k)origin[k]+=-delta[2]*g_baseCamera[3+k]-delta[0]*g_baseCamera[6+k]+delta[1]*g_baseCamera[9+k];
    if(sequential::requested())origin[2]=g_baseCamera[2]+g_pairRig.positions[eye][1]-(l[1]+r[1])*.5f;
    std::memcpy(g_private.bytes.data()+0xa8,origin,12); // experimental world scale: 1 game unit / metre
    // Rotate each actual XR eye orientation relative to the captured head basis.
    const auto rotate=[](const std::array<float,4>& quat,const std::array<float,3>& v) {
        const std::array<float,3> c={quat[1]*v[2]-quat[2]*v[1],quat[2]*v[0]-quat[0]*v[2],quat[0]*v[1]-quat[1]*v[0]};
        const std::array<float,3> c2={quat[1]*c[2]-quat[2]*c[1],quat[2]*c[0]-quat[0]*c[2],quat[0]*c[1]-quat[1]*c[0]};
        std::array<float,3> out{};for(unsigned k=0;k<3;++k)out[k]=v[k]+2*(quat[3]*c[k]+c2[k]);return out;
    };
    const std::array<float,4> inverse={-q[0],-q[1],-q[2],q[3]};
    const std::array<std::array<float,3>,3> xrAxes={{{0,0,-1},{-1,0,0},{0,1,0}}};
    for(unsigned row=0;row<3;++row) {
        const auto local=rotate(inverse,rotate(g_pairRig.orientations[eye],xrAxes[row]));
        float axis[3]{};
        for(unsigned k=0;k<3;++k)axis[k]=-local[2]*g_baseCamera[3+k]-local[0]*g_baseCamera[6+k]+local[1]*g_baseCamera[9+k];
        std::memcpy(g_private.bytes.data()+0xb4+row*12,axis,12);
    }
    std::memcpy(g_eyeCameras[eye].data(),g_private.bytes.data()+0xa8,48);
}
void afterCopy(safetyhook::Context& ctx) {
    traceFrame("copy");
    if(!g_private.active)return;
    std::array<unsigned char,48> unchanged{},copied{};
    if(read(g_private.original+0xa8,unchanged.data(),unchanged.size()) && unchanged==g_private.originalCamera &&
       read(g_private.destination+0xa8,copied.data(),copied.size()) &&
       !std::memcmp(copied.data(),g_private.bytes.data()+0xa8,copied.size())) {
        ++g_restored;
        if(g_private.eye>=0)++g_eyeCopies[g_private.eye];
        if(g_private.live.valid && !g_sampleLock.test_and_set(std::memory_order_acquire)) {
            g_renderPrevious=g_liveLatest;g_liveLatest=g_private.live;g_livePrevious=g_private.live;
            g_sampleLock.clear(std::memory_order_release);
        }
    }
    else {++g_mismatch;g_probe.store(Probe::None);stopLive();}
    // The original memory was never changed. The temporary source argument ends here.
    ctx.rdx=g_private.original;g_private.active=false;
}
void observe(safetyhook::Context& ctx) {
    const auto sequence=++g_calls;
    // Exact measured boundary: RDX is selected renderView_t; R13 owns the component;
    // R14 is the job packet. Snapshot first; independently gated probes can redirect
    // this copy's source argument to a private view. Original view memory stays intact.
    if(g_sampleLock.test_and_set(std::memory_order_acquire))return;
    Sample next;
    next.sequence=sequence;next.ticks=GetTickCount64();next.source=ctx.rdx;
    next.owner=ctx.r13;next.component=ctx.rdi;next.task=ctx.r14;next.viewOffset=ctx.rbp;
    const bool ok=read(ctx.rdx,next.view.data(),next.view.size()) &&
        read(ctx.r14+0x10,&next.frame,sizeof(next.frame));
    if(ok) {
        g_sample=next;++g_reads;
        const bool valid=cameraValid(next);
        if(valid)g_pauseOwner.store(next.owner);
        if(valid)++g_valid;
        g_private.live={};
        if(liveActive() && valid && g_afterCopy && g_valid.load()>=8 && g_rig.valid && next.ticks-g_rig.ticks<150 && !g_mismatch.load()) {
            std::array<float,12> base{};std::memcpy(base.data(),next.view.data()+0xa8,48);
            bool previousAdjusted=g_livePrevious.valid,previousBase=g_livePrevious.valid;
            for(unsigned k=0;k<12;++k) {
                previousAdjusted=previousAdjusted && std::fabs(base[k]-g_livePrevious.camera[k])<0.00002f;
                previousBase=previousBase && std::fabs(base[k]-g_livePrevious.base[k])<0.00002f;
            }
            if(previousAdjusted && !previousBase) {
                stopLive();circlevr::vkcore::logf("live: camera feedback detected; cinema fallback latched");
            } else {
                if(g_recenter.exchange(false)) {g_liveReference=g_rig;++g_liveEpoch;}
                g_baseCamera=base;g_pairRig=g_rig;
                auto& tag=g_private.live;
                tag.valid=true;tag.sequence=sequence;tag.ticks=next.ticks;tag.epoch=g_liveEpoch;
                LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);tag.copyQpc=stamp.QuadPart;
                std::uintptr_t input=0;unsigned char pause=0;
                tag.paused=read(next.owner+0x42188,&input,8) && read(input+0x48a,&pause,1) && pause==1;
                tag.eye=static_cast<int>((g_liveNumber++)&1);tag.rig=g_rig;tag.base=base;
                tag.frame=next.frame;tag.destination=next.frame+next.viewOffset+0x50;
                tag.fovX=field(next.view,0x64);tag.fovY=field(next.view,0x68);
                tag.valid=read(next.frame+0x18,&tag.frameToken,8) && tag.frameToken!=0;
                if(tag.valid) {
                    g_private.bytes=next.view;g_private.original=ctx.rdx;g_private.destination=tag.destination;
                    std::memcpy(g_private.originalCamera.data(),base.data(),48);
                    eyeView(tag.eye==0 ? Probe::Left : Probe::Right);
                    std::memcpy(tag.camera.data(),g_private.bytes.data()+0xa8,48);
                    g_private.active=true;g_private.eye=tag.eye;ctx.rdx=reinterpret_cast<std::uintptr_t>(g_private.bytes.data());++g_overrides;
                }
            }
            g_sampleLock.clear(std::memory_order_release);return;
        }
        const auto probe=g_probe.load(std::memory_order_relaxed);
        const bool eye=probe==Probe::Left || probe==Probe::Right;
        const bool paused=valid && nativePause(next);
        g_nativePaused.store(paused);
        if(!paused)g_pairLatched.store(false);
        if(eye && paused && !g_pairLatched.load() && g_rig.valid) {
            std::memcpy(g_baseCamera.data(),next.view.data()+0xa8,48);
            g_pairRig=g_rig;g_pairOwner=next.owner;g_pairLatched.store(true);
        }
        const bool eyeAllowed=!eye || (paused && g_pairLatched.load() && next.owner==g_pairOwner);
        if(valid && eyeAllowed && g_afterCopy && g_valid.load()>=8 && probe!=Probe::None && !g_mismatch.load()) {
            g_private.bytes=next.view;g_private.original=ctx.rdx;
            g_private.destination=next.frame+ctx.rbp+0x50;
            std::memcpy(g_private.originalCamera.data(),next.view.data()+0xa8,g_private.originalCamera.size());
            if(eye)eyeView(probe);else adjustView(probe);
            g_private.active=true;
            g_private.eye=eye ? (probe==Probe::Left ? 0 : 1) : -1;
            ctx.rdx=reinterpret_cast<std::uintptr_t>(g_private.bytes.data());
            ++g_overrides;
        }
    } else ++g_invalid;
    g_sampleLock.clear(std::memory_order_release);
}
void hidePausedGui(safetyhook::Context& ctx) {
    const auto operation=g_probe.load();
    if((operation==Probe::Left || operation==Probe::Right) && g_nativePaused.load() && g_pairLatched.load())
        ctx.rax=(ctx.rax & ~std::uintptr_t(255))|1; // native r_skipGuis result, one render-settings copy only
}
}
bool enabled() {
    wchar_t value[8]{};
    return ((GetEnvironmentVariableW(L"CIRCLE_EXPERIMENTAL_STEREO",value,8)==1 && value[0]==L'1') || liveEnabled() || sequential::requested()) && isTargetProcess();
}
bool liveEnabled() {
    static const bool yes=[] {wchar_t value[8]{};return !sequential::requested() && GetEnvironmentVariableW(L"CIRCLE_DIAGNOSTIC_AER",value,8)==1 && value[0]==L'1' && isTargetProcess();}();
    return yes;
}
bool liveActive() {return liveEnabled() && !g_liveStopped.load();}
std::uint64_t selectedViewSequence() {return g_calls.load();}
void publishCinemaPanel(const CinemaPanel& panel){
    if(g_sampleLock.test_and_set(std::memory_order_acquire))return;
    g_cinemaPanel=panel;g_sampleLock.clear(std::memory_order_release);
}
bool buildSequentialEyes(std::uintptr_t nativeView,std::uint64_t token,
    std::array<std::array<unsigned char,0x9a0>,2>& views,std::array<LiveEye,2>& eyes,bool cinema,bool monoCinema) {
    if(g_liveStopped || g_mismatch || g_sampleLock.test_and_set(std::memory_order_acquire))return false;
    const auto unlock=[] {g_sampleLock.clear(std::memory_order_release);};
    std::array<unsigned char,0x9a0> native{};std::uintptr_t input=0;std::array<unsigned char,3> flags{};
    std::array<float,12> ledge{};const bool climbing=!cinema&&sequential::ledgeCamera(ledge);
    static bool wasClimbing=false;static EyeRig climbReference;
    if(!climbing&&wasClimbing){locomotion::resetCameraAnchor();wasClimbing=false;}

    if(!g_rig.valid || GetTickCount64()-g_rig.ticks>=100 || GetTickCount64()-g_sample.ticks>=100 ||
       !read(nativeView,native.data(),native.size()) || std::memcmp(native.data()+0xa8,g_sample.view.data()+0xa8,48) ||
       (!cinema && !climbing && (!read(g_sample.owner+0x42188,&input,8) || !read(input+0x488,flags.data(),3) || flags[0]!=1 || flags[1] || flags[2]))) {unlock();return false;}
    static bool positionalCinemaReturn=false;
    if(!cinema&&positionalCinemaReturn){locomotion::resetCameraAnchor();positionalCinemaReturn=false;}
    if(cinema){
        positionalCinemaReturn=true;
        // Fixed stereoscopic film cameras: only eye separation comes from XR.
        // Tracking controls the spectator's view of the panel, never the cut.
        const auto& panel=g_cinemaPanel;
        const float tx=std::tan(field(native,0x64)*0.00872664626f);
        const float ty=cinemaVerticalTangent(tx);
        if(!panel.valid||!g_rig.tracked||panel.width<=0||!std::isfinite(tx+ty)||tx<=0||ty<=0){unlock();return false;}
        std::array<float,12> camera{};std::memcpy(camera.data(),native.data()+0xa8,48);
        for(float v:camera)if(!std::isfinite(v)){unlock();return false;}
        const auto offsets=cinemaEyeOffsets(g_rig.positions,monoCinema);
        if(!offsets){unlock();return false;}
        for(unsigned e=0;e<2;++e){
            // Inspection is a flat authored image: no eye or head-dependent
            // camera offset. Keep the paired transport for stable transitions.
            const float x=(*offsets)[e];
            views[e]=native;auto& out=eyes[e];out={};out.valid=true;out.cinema=true;out.monoCinema=monoCinema;out.eye=e;
            out.sequence=out.frameToken=token;out.rig=g_rig;out.ticks=GetTickCount64();out.base=out.camera=camera;
            for(unsigned k=0;k<3;++k)out.camera[k]+=-x*camera[6+k];
            std::memcpy(views[e].data()+0xa8,out.camera.data(),48);
            const auto frustum=cinemaFrustum(tx,ty,panel.width,x,0);
            if(!frustum){unlock();return false;}out.cinemaFov=*frustum;
            if(!monoCinema){
                const auto stable=stableCinemaProjection(tx,ty,panel.width,x);
                if(!stable){unlock();return false;}
                out.cinemaFov=stable->fov;out.cinemaCrop=stable->crop;
            }
            out.fovX=field(native,0x64);out.fovY=2*std::atan(ty)*57.2957795f;
        }
        static uint64_t lastCinemaLog{};const auto now=GetTickCount64();
        if(now-lastCinemaLog>2000){lastCinemaLog=now;circlevr::vkcore::logf("motor-cinema-fixed: mono %d eye offsets %.6f %.6f; authored camera retained, no tracked camera shift",monoCinema,(*offsets)[0],(*offsets)[1]);}
        unlock();return true;
    }
    if(climbing){
        const auto climbMotion=locomotion::update(g_rig,ledge,false,false);
        if(!wasClimbing){climbReference=g_rig;wasClimbing=true;}
        // Preserve the established world heading through native body turns.
        // Positional tracking still uses the entry anchor for the climb.
        if(climbMotion.referenceValid)climbReference.orientations=g_liveReference.orientations;
        // Render-only override: native ledge physics, animation and input own
        // the body. Never publish controller IK or a hand-model rebase here.
        g_pairRig=g_rig;g_baseCamera=ledge;
        if(climbMotion.referenceValid)locomotion::levelGameplayCamera(g_baseCamera,climbMotion.nativeYaw-climbMotion.referenceCompensation);
        const auto reference=g_liveReference;g_liveReference=climbReference;
        const auto saved=g_private;
        for(unsigned eye=0;eye<2;++eye){
            g_private={};g_private.bytes=native;g_private.live.valid=true;
            eyeView(eye==0?Probe::Left:Probe::Right);
            views[eye]=g_private.bytes;auto& out=eyes[eye];out={};out.valid=true;out.eye=eye;
            out.sequence=out.frameToken=token;out.epoch=g_liveEpoch;out.ticks=GetTickCount64();out.rig=g_pairRig;out.base=g_baseCamera;
            std::memcpy(out.camera.data(),views[eye].data()+0xa8,48);out.fovX=field(native,0x64);out.fovY=field(native,0x68);
            LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);out.copyQpc=qpc.QuadPart;
        }
        g_private=saved;g_liveReference=reference;
        // Ledge rendering returns before ordinary view publication. Target
        // queries need these exact compensated cameras, but no controller IK.
        interaction::publishView(eyes,climbMotion);
        static uint64_t last{};const auto now=GetTickCount64();if(now-last>1000){last=now;circlevr::vkcore::logf("motor-ledge-vr: native FPS anchor %.4f %.4f %.4f; native limbs/input retained",ledge[0],ledge[1],ledge[2]);}
        unlock();return true;
    }
    bool recentered=g_recenter.exchange(false);
    if(recentered){g_liveReference=g_rig;++g_liveEpoch;}
    g_pairRig=g_rig;std::memcpy(g_baseCamera.data(),native.data()+0xa8,48);
    auto motion=locomotion::update(g_pairRig,g_baseCamera,recentered);
    if(motion.passengerEntryRecenter){
        // Locomotion and stereo must adopt the SAME entry pose, before either
        // eye or the interaction ray is built. Do not defer to the next pair.
        if(!recentered){g_liveReference=g_pairRig;++g_liveEpoch;}
        recentered=true;hands::resetInteractionTracking();
    }
    turret::View mountedView;
    const bool mounted=turret::view(g_pairRig,g_baseCamera,recentered,mountedView);
    const bool vehicle=mounted||boat::view(g_pairRig,g_baseCamera,recentered,mountedView);
    const auto ordinaryReference=g_liveReference;
    if(vehicle){
        g_baseCamera=turret::matrix(mountedView.camera);
        motion={};motion.mounted=mounted;motion.referenceValid=true;motion.epoch=mountedView.epoch;
        motion.mountedRoot=turret::matrix(mountedView.mount);
        const auto& q=mountedView.reference;
        motion.referenceYaw=locomotion::trackingYaw({q.x,q.y,q.z,q.w});
        motion.nativeYaw=std::atan2(g_baseCamera[4],g_baseCamera[3])*57.295779513f;
        motion.physics=locomotion::latestNative().position;
        for(auto& orientation:g_liveReference.orientations)orientation={q.x,q.y,q.z,q.w};
    }
    // The player yaw owns the gameplay frame, including while airborne.
    // Native jump/crawl camera-joint rotations must not redirect headset aim.
    if(!vehicle&&(motion.referenceValid||motion.gravityAligned))locomotion::levelGameplayCamera(g_baseCamera,motion.nativeYaw);
    const auto nativeCamera=g_baseCamera;
    const auto placementPhysics=motion.physics;const auto placementYaw=motion.nativeYaw;
    const auto stabilizeCamera=[&] {
        const auto p=motion.cameraAnchor.place({nativeCamera[0],nativeCamera[1],nativeCamera[2]},placementPhysics,placementYaw);
        g_baseCamera[0]=p.x;g_baseCamera[1]=p.y;g_baseCamera[2]=p.z;
    };
    stabilizeCamera();
    hands::publishTargets(g_pairRig,g_baseCamera,motion);
    // Match the native arm simulation without rolling the current headset pose
    // back to its older IK sample. Rebase the model independently below.
    const bool handsMatched=hands::appliedViewFrame(g_pairRig,motion);
    if(handsMatched&&!vehicle)
        locomotion::levelGameplayCamera(g_baseCamera,motion.nativeYaw);
    stabilizeCamera(); // Same anchor snapshot as the applied controller skeleton.
    hands::publishRenderAnchor(token,g_baseCamera,handsMatched,motion);
    static interaction::PhotoZoom photoZoom;
    const bool photo=hands::weaponSlot(locomotion::latestNative().entity)==17;
    const float zoomScale=photoZoom.update(field(native,0x64),photo);
    static uint64_t photoLogged{};const auto photoNow=GetTickCount64();
    if(photo&&photoNow-photoLogged>1000){photoLogged=photoNow;circlevr::vkcore::logf("motor-photo-render: native FOV %.3f baseline %.3f tangent scale %.4f",field(native,0x64),photoZoom.baseline,zoomScale);}
    const auto saved=g_private;
    for(unsigned eye=0;eye<2;++eye){
        g_private={};g_private.bytes=native;g_private.live.valid=true;
        eyeView(eye==0?Probe::Left:Probe::Right);
        if(motion.referenceValid&&!vehicle){
            // Remove the native yaw already accepted by the visible body.
            // Both eyes share this correction; retain their real pitch/roll.
            for(unsigned axis=0;axis<3;++axis){
                locomotion::Vec v;std::memcpy(&v,g_private.bytes.data()+0xb4+axis*12,sizeof(v));
                v=locomotion::rotateWorldYaw(v,-motion.referenceCompensation);
                std::memcpy(g_private.bytes.data()+0xb4+axis*12,&v,sizeof(v));
            }
            // Native body position already contains accepted roomscale. Render
            // only the actual eye offset here;
            // never add the unaccepted horizontal tracking residual again.
            const auto& l=g_pairRig.positions[0];const auto& r=g_pairRig.positions[1];
            locomotion::Vec offset{g_pairRig.positions[eye][0]-(l[0]+r[0])*.5f,0,g_pairRig.positions[eye][2]-(l[2]+r[2])*.5f};
            float yaw=std::atan2(g_baseCamera[4],g_baseCamera[3]);
            locomotion::GroundFrame frame{{std::cos(yaw),std::sin(yaw),0},{std::sin(yaw),-std::cos(yaw),0},{0,0,1},1,true};
            auto delta=frame.trackingToWorld(offset,motion.referenceYaw);
            // User requirement: native body camera owns height, including
            // crouch/traversal animations; physical HMD height adds nothing.
            float origin[3]={g_baseCamera[0]+delta.x,g_baseCamera[1]+delta.y,g_baseCamera[2]+g_pairRig.positions[eye][1]-(l[1]+r[1])*.5f};
            std::memcpy(g_private.bytes.data()+0xa8,origin,sizeof(origin));
        }
        views[eye]=g_private.bytes;
        auto& out=eyes[eye];out={};out.valid=true;out.eye=static_cast<int>(eye);out.sequence=token;out.frameToken=token;
        out.epoch=g_liveEpoch;out.ticks=GetTickCount64();out.rig=g_pairRig;out.base=g_baseCamera;out.photoZoomScale=zoomScale;
        std::memcpy(out.camera.data(),views[eye].data()+0xa8,48);out.fovX=field(native,0x64);out.fovY=field(native,0x68);
        LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);out.copyQpc=qpc.QuadPart;
    }
    interaction::publishView(eyes,motion);
    laser::publish(eyes);
    if(vehicle)g_liveReference=ordinaryReference;
    g_private=saved;unlock();return true;
}
void stopLive() {g_liveStopped.store(true);}
bool pauseMenuActive(){
    const auto owner=g_pauseOwner.load();std::uintptr_t input=0;
    unsigned char active=0;std::array<unsigned char,3> flags{};
    return owner&&read(owner+0x42188,&input,sizeof(input))&&input&&
        read(input+0x48,&active,1)&&active==1&&read(input+0x488,flags.data(),flags.size())&&
        flags[0]<=1&&flags[1]<=1&&flags[2]==1;
}
void recenterLive() {g_recenter.store(true);hands::resetInteractionTracking();}
void recenterAt(std::int64_t changeTime) {
    g_spaceRecenterTime.store(changeTime > 0 ? changeTime : 1, std::memory_order_release);
}
bool stillOwned(const LiveEye& view) {
    std::uint64_t token=0;std::array<float,12> camera{};
    if(!view.valid || GetTickCount64()-view.ticks>=200) {++g_liveRejects[0];return false;}
    if(!read(view.frame+0x18,&token,8) || token!=view.frameToken) {++g_liveRejects[1];return false;}
    if(!read(view.destination+0xa8,camera.data(),48) || camera!=view.camera) {++g_liveRejects[2];return false;}
    return true;
}
void acquireLive(std::uint64_t swapchain,std::uint32_t image) {
    if(!liveActive() || image>=g_acquired.size())return;
    if(g_sampleLock.test_and_set(std::memory_order_acquire)) {++g_liveRejects[3];return;}
    auto& acquired=g_acquired[image];acquired={swapchain,image,{},{}};
    if(g_liveLatest.sequence!=g_acquiredSequence && stillOwned(g_liveLatest)) {
        // Motor's frontend copy is one image ahead of the desktop backend. The
        // first live capture exposed reversed disparity when tagging that newer
        // camera. Require an uninterrupted pair of completed native copies; keep
        // the current frontend seal separate from the rendered camera's metadata.
        if(g_renderPrevious.valid && g_renderPrevious.sequence+1==g_liveLatest.sequence &&
           g_renderPrevious.epoch==g_liveLatest.epoch && g_liveLatest.ticks-g_renderPrevious.ticks<150) {
            acquired.view=g_renderPrevious;acquired.front=g_liveLatest;
        }
        g_acquiredSequence=g_liveLatest.sequence;
    }
    g_sampleLock.clear(std::memory_order_release);
}
LiveEye takeLive(std::uint64_t swapchain,std::uint32_t image) {
    LiveEye result;
    if(!liveActive() || image>=g_acquired.size())return result;
    if(g_sampleLock.test_and_set(std::memory_order_acquire)) {++g_liveRejects[4];return result;}
    auto& acquired=g_acquired[image];
    if(acquired.swapchain==swapchain && acquired.view.valid && stillOwned(acquired.front))result=acquired.view;
    acquired.view.valid=false;
    g_sampleLock.clear(std::memory_order_release);return result;
}
void traceFrame(const char* stage,std::uint32_t image) {
    if(!liveEnabled() || !g_base)return;
    static std::atomic<unsigned> count{0};
    if(count.fetch_add(1)>4000)return;
    std::array<std::uint64_t,6> words{};
    read(g_base+build::rva(0x4c52390),words.data(),sizeof(words));
    circlevr::vkcore::logf("live-trace: %s thread %lu image %u camera %llu frame %llu %llu %llx %llx %llx %llx",stage,GetCurrentThreadId(),image,g_calls.load(),words[0],words[1],words[2],words[3],words[4],words[5]);
}
void publishRig(const EyeRig& rig) {
    // Scripted inputs must not depend on the gameplay/IK render branch.
    hands::publishInteractionTracking(rig);
    if(g_sampleLock.test_and_set(std::memory_order_acquire))return;
    g_rig=rig;
    auto changeTime=g_spaceRecenterTime.load(std::memory_order_acquire);
    if(changeTime&&rig.valid&&rig.tracked&&rig.displayTime>=changeTime&&
       g_spaceRecenterTime.compare_exchange_strong(changeTime,0)) {
        // Publish the new-space pose and the reset together. The render hook
        // must never rebase onto the last pose from the previous space.
        recenterLive();
        circlevr::vkcore::logf("motor-runtime-recenter: LOCAL change %lld applied at tracked pose %lld; resetting shared camera/locomotion reference",changeTime,rig.displayTime);
    }
    g_sampleLock.clear(std::memory_order_release);
}
bool installObservation(std::string& error) {
    if(!enabled()) {error="CIRCLE_EXPERIMENTAL_STEREO is not explicitly 1";return false;}
    std::call_once(g_once,[] {
        NativeStateObserver state;
        const auto observation=state.sample();
        if(!observation.nativeStateValid) {g_error=state.validation();return;}
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        g_base=base;
        // Selected native view -> destination frame slot -> measured view-copy function.
        constexpr unsigned char expected[]={0x49,0x8b,0x4e,0x10,0x48,0x83,0xc1,0x50,
            0x48,0x03,0xcd,0xe8,0xa0,0xf9,0x6d,0xff,0xeb,0x67};
        std::array<unsigned char,sizeof(expected)> actual{};
        constexpr unsigned char copyExpected[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x0f,0x10,0x02};
        std::array<unsigned char,sizeof(copyExpected)> copyActual{};
        constexpr unsigned char joinExpected[]={0x49,0x8b,0x46,0x10,0x08,0x9c,0x28,0xb2,0x00,0x00,0x00};
        std::array<unsigned char,sizeof(joinExpected)> joinActual{};
        if(!build::matches(0xf624a0,expected,sizeof(expected)) ||
           !build::matches(0x641e50,copyExpected,sizeof(copyExpected)) ||
           !build::matches(0xf62519,joinExpected,sizeof(joinExpected))) {
            g_error="Motor selected-view/copy instruction fingerprint differs";return;
        }
        HMODULE self=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
             reinterpret_cast<LPCWSTR>(&installObservation),&self)) {g_error="cannot pin experimental observer DLL";return;}
        // F624B0 is a 2-byte jump followed by another branch's F624B2 landing:
        // patching it would overwrite that landing. F62519 is the common block entry.
        auto after=safetyhook::MidHook::create(reinterpret_cast<void*>(base+build::rva(0xf62519)),afterCopy);
        if(!after) {g_error="cannot install copy-completion boundary";return;}
        g_afterCopy=new safetyhook::MidHook(std::move(*after));
        auto made=safetyhook::MidHook::create(reinterpret_cast<void*>(base+build::rva(0xf624a0)),observe);
        if(!made) {g_error="SafetyHook could not install the independently validated read-only observation";return;}
        g_observer=new safetyhook::MidHook(std::move(*made));
        // Native r_skipGuis proxy -> compare integer -> setne AL -> render flags.
        // Full C1E5C0..C1F5EF decode has no branch into this 6-byte instruction.
        constexpr unsigned char guiExpected[]={0x88,0x83,0x5d,0x42,0x4d,0x00,0x48,0x8b,0x05,0xc1,0x5e,0x8a,0x03};
        std::array<unsigned char,sizeof(guiExpected)> guiActual{};
        if(build::matches(0xc1e7b2,guiExpected,sizeof(guiExpected))) {
            auto gui=safetyhook::MidHook::create(reinterpret_cast<void*>(base+build::rva(0xc1e7b2)),hidePausedGui);
            if(gui)g_hideGui=new safetyhook::MidHook(std::move(*gui));
        }
        g_installed=true;
        circlevr::vkcore::logf("experiment: read-only selected-view observer installed at Motor RVA F624A0; production camera gates remain closed");
    });
    error=g_error;return g_installed;
}
void pollObservation() {
    if(!g_installed)return;
    static ULONGLONG last=0;
    static std::uint64_t shown=0;
    static unsigned captures=0;
    const auto now=GetTickCount64();
    if(now-last<1000)return;
    last=now;
    diagnostics::poll();
    if(liveEnabled())circlevr::vkcore::logf("live: active %d; rejected invalid/stale %llu token %llu camera %llu acquire_busy %llu present_busy %llu",liveActive()?1:0,
        g_liveRejects[0].load(),g_liveRejects[1].load(),g_liveRejects[2].load(),g_liveRejects[3].load(),g_liveRejects[4].load());
    const auto control=circlevr::vkcore::logDirectory()+L"\\experiment-camera.txt";
    FILE* controlFile=nullptr;
    if(!_wfopen_s(&controlFile,control.c_str(),L"rb") && controlFile) {
        char command[24]{};fgets(command,sizeof(command),controlFile);fclose(controlFile);
        Probe wanted=Probe::None;
        if(!std::strncmp(command,"yaw",3))wanted=Probe::Yaw;
        if(!std::strncmp(command,"pitch",5))wanted=Probe::Pitch;
        if(!std::strncmp(command,"roll",4))wanted=Probe::Roll;
        if(!std::strncmp(command,"position",8))wanted=Probe::Position;
        if(!std::strncmp(command,"left",4))wanted=Probe::Left;
        if(!std::strncmp(command,"right",5))wanted=Probe::Right;
        if(wanted!=g_probe.load() && !g_mismatch.load()) {
            if(wanted!=Probe::Left && wanted!=Probe::Right)g_pairLatched.store(false);
            g_probe.store(wanted);
            circlevr::vkcore::logf("experiment: private-camera probe %d; 5 degree rotation or 0.03 game-unit position; original camera memory untouched",static_cast<int>(wanted));
        }
    }
    const auto request=circlevr::vkcore::logDirectory()+L"\\experiment-capture.request";
    if(GetFileAttributesW(request.c_str())!=INVALID_FILE_ATTRIBUTES && DeleteFileW(request.c_str()))
        circlevr::vkcore::bug_capture::request(); // existing synchronized Vulkan capture; mono until a real pair exists
    if(!diagnostics::enabled())return;
    if(g_sampleLock.test_and_set(std::memory_order_acquire))return;
    const Sample sample=g_sample;
    const auto pairOwner=g_pairOwner;
    const auto pairRig=g_pairRig;
    const auto baseCamera=g_baseCamera;
    const auto eyeCameras=g_eyeCameras;
    const bool rigValid=g_rig.valid;
    g_sampleLock.clear(std::memory_order_release);
    if(sample.sequence==shown) {
        if(!shown) circlevr::vkcore::logf("experiment: awaiting selected-view execution; calls %llu successful reads %llu invalid %llu",
            g_calls.load(),g_reads.load(),g_invalid.load());
        return;
    }
    shown=sample.sequence;
    circlevr::vkcore::logf("experiment: view call %llu source %p owner %p component %p packet %p frame %p slot +%llx age %llu ms; successful reads %llu",
        sample.sequence,reinterpret_cast<void*>(sample.source),reinterpret_cast<void*>(sample.owner),
        reinterpret_cast<void*>(sample.component),reinterpret_cast<void*>(sample.task),reinterpret_cast<void*>(sample.frame),
        static_cast<unsigned long long>(sample.viewOffset),now-sample.ticks,g_reads.load());
    circlevr::vkcore::logf("experiment: camera xyz %.6f %.6f %.6f fov %.3f %.3f; valid %llu private overrides %llu original preserved/copy complete %llu mismatch %llu",
        field(sample.view,0xa8),field(sample.view,0xac),field(sample.view,0xb0),field(sample.view,0x64),field(sample.view,0x68),
        g_valid.load(),g_overrides.load(),g_restored.load(),g_mismatch.load());
    if(g_probe.load()==Probe::Left || g_probe.load()==Probe::Right) {
        circlevr::vkcore::logf("experiment: frozen-eye gate native pause %s rig %s owner %s latched %s",
            g_nativePaused.load() ? "yes" : "no",rigValid ? "valid" : "missing",
            sample.owner==pairOwner ? "same" : "different",g_pairLatched.load() ? "yes" : "no");
        if(g_pairLatched.load() && g_nativePaused.load()) {
            const auto path=circlevr::vkcore::logDirectory()+L"\\native-pair-rig.json";
            FILE* rigFile=nullptr;
            if(!circlevr::vkcore::openLogFile(&rigFile,path.c_str(),L"wb") && rigFile) {
                fprintf(rigFile,"{\"native_pause\":true,\"latched_owner\":\"0x%llx\",\"game_units_per_metre\":1,\"scale_status\":\"experimental, not physically calibrated\",\"fov_x\":%.6f,\"fov_y\":%.6f,\"display_time\":%lld,\"eye_positions\":[",
                    static_cast<unsigned long long>(pairOwner),field(sample.view,0x64),field(sample.view,0x68),pairRig.displayTime);
                for(unsigned eye=0;eye<2;++eye)fprintf(rigFile,"%s[%.9f,%.9f,%.9f]",eye ? "," : "",pairRig.positions[eye][0],pairRig.positions[eye][1],pairRig.positions[eye][2]);
                fprintf(rigFile,"],\"eye_orientations\":[");
                for(unsigned eye=0;eye<2;++eye)fprintf(rigFile,"%s[%.9f,%.9f,%.9f,%.9f]",eye ? "," : "",pairRig.orientations[eye][0],pairRig.orientations[eye][1],pairRig.orientations[eye][2],pairRig.orientations[eye][3]);
                fprintf(rigFile,"],\"base_camera\":[");
                for(unsigned k=0;k<12;++k)fprintf(rigFile,"%s%.9f",k ? "," : "",baseCamera[k]);
                fprintf(rigFile,"],\"eye_cameras\":[");
                for(unsigned eye=0;eye<2;++eye) {
                    fprintf(rigFile,"%s[",eye ? "," : "");
                    for(unsigned k=0;k<12;++k)fprintf(rigFile,"%s%.9f",k ? "," : "",eyeCameras[eye][k]);
                    fprintf(rigFile,"]");
                }
                fprintf(rigFile,"],\"verified_eye_copies\":[%llu,%llu],\"mismatches\":%llu}\n",g_eyeCopies[0].load(),g_eyeCopies[1].load(),g_mismatch.load());fclose(rigFile);
            }
        }
    }
    if(captures>=32)return;
    const auto directory=circlevr::vkcore::logDirectory()+L"\\experimental-views";
    CreateDirectoryW(directory.c_str(),nullptr);
    wchar_t name[80]{};
    swprintf_s(name,L"\\view-%02u-%llu.bin",captures,sample.sequence);
    FILE* file=nullptr;
    if(circlevr::vkcore::openLogFile(&file,(directory+name).c_str(),L"wb") || !file)return;
    fwrite(sample.view.data(),1,sample.view.size(),file);fclose(file);
    swprintf_s(name,L"\\view-%02u-%llu.json",captures,sample.sequence);
    if(!circlevr::vkcore::openLogFile(&file,(directory+name).c_str(),L"wb") && file) {
        fprintf(file,"{\"sequence\":%llu,\"ticks\":%llu,\"source\":\"0x%llx\",\"owner\":\"0x%llx\",\"component\":\"0x%llx\",\"packet\":\"0x%llx\",\"frame\":\"0x%llx\",\"slot_offset\":%llu,\"bytes\":%zu,\"camera_write\":false}\n",
            sample.sequence,sample.ticks,static_cast<unsigned long long>(sample.source),static_cast<unsigned long long>(sample.owner),
            static_cast<unsigned long long>(sample.component),static_cast<unsigned long long>(sample.task),
            static_cast<unsigned long long>(sample.frame),static_cast<unsigned long long>(sample.viewOffset),sample.view.size());
        fclose(file);
    }
    ++captures;
}
}
