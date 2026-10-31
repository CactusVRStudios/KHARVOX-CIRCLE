#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include "hand_pose.hpp"

namespace circle::motor {
// BE0680 stores the conjugate of the model's local-to-world quaternion in
// emitter record +20. The particle shader consumes this native convention;
// writing the hand quaternion directly reverses controller rotation.
inline std::optional<hands::Quat> gpuEmitterRotation(const std::array<float,15>& pose){
    const auto q=hands::rotationBasis({pose[3],pose[4],pose[5]},
        {pose[6],pose[7],pose[8]},{pose[9],pose[10],pose[11]});
    if(!q)return {};
    return hands::Quat{-q->x,-q->y,-q->z,q->w};
}
// Native GPU emitter descriptor (not the underlying allocation object).
// +0 is a link pointer; +8 capacity, +14 stride, +50 mapped record address.
struct GpuEmitterBuffer {
    uint64_t capacity{};uintptr_t mapped{};
    static GpuEmitterBuffer decode(const std::array<unsigned char,0x78>& raw){
        GpuEmitterBuffer b;uint32_t stride{};
        std::memcpy(&b.capacity,raw.data()+8,8);
        std::memcpy(&stride,raw.data()+0x14,4);
        std::memcpy(&b.mapped,raw.data()+0x50,sizeof(b.mapped));
        if(!b.mapped||stride!=0xd0||b.capacity<0xd0||b.capacity>256ull*1024*1024||
           b.capacity%0xd0||b.mapped>UINTPTR_MAX-b.capacity)return {};
        return b;
    }
    bool contains(int instance)const {
        return mapped&&instance>=0&&(uint64_t(instance)+1)*0xd0<=capacity;
    }
};
// Emitter history belongs to one allocation and simulation timestamp, not an
// eye or a recycled model address. A repeated timestamp must not advance the
// PREVIOUS pose, but must refresh CURRENT: late parent updates can occur inside
// the same simulation/stereo timestamp.
struct LighterParticleHistory {
    uintptr_t model{}, committed{}, runtime{}, parent{};
    uint64_t owner{}, token{}, seen{}, generation{};
    int time{};
    std::array<float,15> current{}, previous{};
    bool advance(uintptr_t m,uintptr_t c,uintptr_t r,uintptr_t p,uint64_t o,
        uint64_t t,int ms,uint64_t now,const std::array<float,15>& pose,uint64_t allocation=0) {
        const bool same=model==m&&committed==c&&runtime==r&&parent==p&&owner==o&&generation==allocation&&model;
        const bool continuous=same&&now>=seen&&now-seen<250&&ms>=time&&int64_t(ms)-time<250;
        if(continuous&&token==t&&time==ms){current=pose;seen=now;return false;}
        previous=continuous&&ms>time?current:pose;
        current=pose;model=m;committed=c;runtime=r;parent=p;owner=o;
        token=t;time=ms;seen=now;generation=allocation;return !continuous;
    }
};
// Native origin, three axis vectors and scale. This is a current-pair pose,
// never temporal history: only the immediately following right render may use it.
struct ModelPairPlacement {
    uintptr_t model{}, committed{}, manager{};
    uint64_t owner{}, token{};
    unsigned index{}, frame{};
    std::array<float,15> placement{};
    bool matches(uintptr_t m,uintptr_t c,uintptr_t g,uint64_t o,unsigned i,uint64_t t,unsigned f)const {
        return model&&model==m&&committed==c&&manager==g&&owner==o&&index==i&&token&&token==t&&f-frame==1;
    }
    static bool finite(const std::array<float,15>& p) {
        for(float v:p)if(!std::isfinite(v))return false;
        // Signed scale is part of the native affine placement. Reflections
        // (e.g. a mirrored door plate) are invertible and must pair as well.
        return p[12]!=0&&p[13]!=0&&p[14]!=0;
    }
};
// Apply exactly the parent's render-only rigid change, retaining the child's
// authored local offset, rotation and scale. No independent filtering/history.
inline bool rebaseAttachedPlacement(const std::array<float,15>& before,
    const std::array<float,15>& after,std::array<float,15>& child){
    if(!ModelPairPlacement::finite(before)||!ModelPairPlacement::finite(after)||!ModelPairPlacement::finite(child))return false;
    for(unsigned a=0;a<3;++a){
        if(std::abs(before[12+a]-1.f)>.001f||std::abs(after[12+a]-1.f)>.001f)return false;
        for(unsigned b=0;b<3;++b){
            float d=0,e=0;for(unsigned k=0;k<3;++k){d+=before[3+3*a+k]*before[3+3*b+k];e+=after[3+3*a+k]*after[3+3*b+k];}
            if(std::abs(d-(a==b?1.f:0.f))>.005f||std::abs(e-(a==b?1.f:0.f))>.005f)return false;
        }
    }
    float rotation[3][3]{};
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
        for(unsigned k=0;k<3;++k)rotation[r][c]+=after[3+3*k+r]*before[3+3*k+c];
    auto result=child;
    for(unsigned r=0;r<3;++r){
        result[r]=after[r];
        for(unsigned c=0;c<3;++c)result[r]+=rotation[r][c]*(child[c]-before[c]);
        for(unsigned a=0;a<3;++a){result[3+3*a+r]=0;
            for(unsigned c=0;c<3;++c)result[3+3*a+r]+=rotation[r][c]*child[3+3*a+c];}
    }
    if(!ModelPairPlacement::finite(result))return false;
    child=result;return true;
}
// Torch instances have different authored emitter heights (live: .46634 and
// .56634 m even for the same mesh). Acquire a coherent local pose, never a
// world-space offset from two render frames. Two distinct samples must agree.
// Also used for the observed candlestick emitter (coherent world instances:
// local Z .410 m). Its own held-instance pose must agree; no borrowed height.
struct HeldFlameEmitterLocal {
    std::array<float,15> local{},candidate{};uint64_t token{};bool ready{},pending{};
    bool observe(uint64_t sample,const std::array<float,15>& parent,const std::array<float,15>& child){
        if(ready)return true;
        constexpr std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
        auto next=child;
        if(!sample||!rebaseAttachedPlacement(parent,identity,next)||
           std::abs(next[0])>.002f||std::abs(next[1])>.002f||next[2]<.1f||next[2]>1.5f){pending=false;return false;}
        for(unsigned i=3;i<12;++i)if(std::abs(next[i]-identity[i])>.002f){pending=false;return false;}
        bool agrees=pending;
        for(unsigned i=0;i<12;++i)agrees&=std::abs(next[i]-candidate[i])<.001f;
        if(agrees&&token!=sample){local=next;ready=true;return true;}
        candidate=next;token=sample;pending=true;return false;
    }
};
using TorchEmitterLocal=HeldFlameEmitterLocal;
inline bool rigidParticlePlacement(const std::array<float,16>& parent,
    const std::array<float,15>& local,const std::array<float,3>& scale,std::array<float,15>& output){
    for(float x:parent)if(!std::isfinite(x))return false;
    if(std::abs(parent[12])+std::abs(parent[13])+std::abs(parent[14])+std::abs(parent[15]-1)>.001f)return false;
    constexpr std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
    const std::array<float,15> pose{parent[3],parent[7],parent[11],parent[0],parent[4],parent[8],
        parent[1],parent[5],parent[9],parent[2],parent[6],parent[10],1,1,1};
    auto placed=local;
    for(unsigned k=0;k<3;++k){if(!std::isfinite(scale[k])||scale[k]<.001f||scale[k]>100)return false;placed[12+k]=scale[k];}
    if(!rebaseAttachedPlacement(identity,pose,placed))return false;
    output=placed;return true;
}
// Measured on the exact indy_lighter_01a / particle_prop_lighter_01 assets:
// coherent native poses give local (0.008, 0, 0.050), with matching axes.
// The emitter's native pose can freeze for seconds; never derive a new local
// offset from that stale world pose or retain a spawn-time world position.
inline bool placeRigidLighterFlame(const std::array<float,15>& parentRender,
    std::array<float,15>& flame){
    if(!ModelPairPlacement::finite(flame))return false;
    constexpr std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
    auto local=identity;local[0]=.008f;local[2]=.050f;
    for(unsigned k=12;k<15;++k)local[k]=flame[k];
    if(!rebaseAttachedPlacement(identity,parentRender,local))return false;
    flame=local;return true;
}
// Use the rendered parent's affine, not a second reconstruction from an older
// simulation placement. Particle axes are unscaled; retain authored scale.
inline bool lighterParticlePlacement(const std::array<float,16>& parent,
    const std::array<float,3>& scale,std::array<float,15>& output){
    for(float x:parent)if(!std::isfinite(x))return false;
    if(std::abs(parent[12])+std::abs(parent[13])+std::abs(parent[14])+std::abs(parent[15]-1)>.001f)return false;
    std::array<float,15> pose{parent[3],parent[7],parent[11],parent[0],parent[4],parent[8],
        parent[1],parent[5],parent[9],parent[2],parent[6],parent[10],1,1,1};
    auto flame=pose;for(unsigned k=0;k<3;++k){if(scale[k]<.001f||scale[k]>100)return false;flame[12+k]=scale[k];}
    if(!placeRigidLighterFlame(pose,flame))return false;
    output=flame;return true;
}
inline bool heldWaxMatrices(const std::array<float,15>& pose,
    std::array<float,16>& forward,std::array<float,16>& inverse){
    if(!ModelPairPlacement::finite(pose))return false;
    constexpr std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
    auto check=pose;
    if(!rebaseAttachedPlacement(identity,pose,check))return false;
    std::array<float,16> f{},v{};f[15]=v[15]=1;
    for(unsigned r=0;r<3;++r){
        f[r*4+3]=pose[r];
        for(unsigned c=0;c<3;++c){f[r*4+c]=pose[3+3*c+r];v[r*4+c]=pose[3+3*r+c];}
        for(unsigned c=0;c<3;++c)v[r*4+3]-=v[r*4+c]*pose[c];
    }
    forward=f;inverse=v;return true;
}
inline bool lighterUploadMatrices(const std::array<float,16>& parent,
    const std::array<float,3>& scale,std::array<float,16>& forward,std::array<float,16>& inverse){
    std::array<float,15> flame{};
    if(!lighterParticlePlacement(parent,scale,flame))return false;
    std::array<float,16> f{},v{};f[15]=v[15]=1;
    for(unsigned r=0;r<3;++r){
        f[r*4+3]=flame[r];
        for(unsigned c=0;c<3;++c){f[r*4+c]=flame[3+3*c+r]*scale[c];v[r*4+c]=flame[3+3*r+c]/scale[r];}
        for(unsigned c=0;c<3;++c)v[r*4+3]-=v[r*4+c]*flame[c];
    }
    forward=f;inverse=v;return true;
}
}
