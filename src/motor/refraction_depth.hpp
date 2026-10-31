#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace circle::motor {
// Row 3 of the native model-view-projection is homogeneous W, i.e. view
// distance for Motor's perspective projection. Do NOT divide by W or use
// projected Z: reversed depth and asymmetric eye optics do not change W.
inline std::optional<float> refractionViewDepth(const std::array<float,6>& bounds,
                                               const std::array<float,4>& wRow,float farPlane){
    if(!std::isfinite(farPlane)||farPlane<=0)return {};
    double depth=wRow[3],normal=0;
    for(unsigned i=0;i<4;++i)if(!std::isfinite(wRow[i]))return {};
    for(unsigned i=0;i<3;++i){
        if(!std::isfinite(bounds[i])||!std::isfinite(bounds[i+3])||bounds[i]>bounds[i+3])return {};
        depth+=(double(bounds[i])+double(bounds[i+3]))*.5*double(wRow[i]);
        normal+=double(wRow[i])*double(wRow[i]);
    }
    // An identity/orthographic fallback cannot supply perspective distance.
    if(normal<1.e-12||!std::isfinite(depth))return {};
    // A bound may straddle the eye. Keep that surface and its draw position;
    // distance is zero there, never a reason to discard geometry.
    return float(std::clamp(depth,0.0,double(farPlane)));
}
inline float refractionDepthSeparation(float signedDelta){return std::abs(signedDelta);}

struct RefractionDepthOwner {
    std::uintptr_t context{},view{},entries{};
    std::uint64_t tick{};
    std::uint32_t backend{};
    unsigned eye{};
    bool operator==(const RefractionDepthOwner&)const=default;
};
// Invocation-local geometry, not a previous-frame history. Never transfer a
// depth between eyes, backend frames, recycled lists or reused draw indices.
class RefractionDepthCache {
    struct Sample {std::uint64_t key{};float depth{};bool valid{};};
    RefractionDepthOwner owner_{};
    std::vector<Sample> samples_;
public:
    static constexpr unsigned maxSurfaces=0x5000; // native transparent-list cap
    void clear(){owner_={};samples_.clear();}
    bool begin(RefractionDepthOwner owner,unsigned count){
        clear();
        if(!owner.context||!owner.view||!owner.entries||!owner.tick||owner.eye>1||!count||count>maxSurfaces)return false;
        owner_=owner;samples_.resize(count);return true;
    }
    bool put(unsigned index,std::uint64_t key,float depth){
        if(index>=samples_.size()||!std::isfinite(depth)||depth<0)return false;
        samples_[index]={key,depth,true};return true;
    }
    std::optional<float> get(const RefractionDepthOwner& owner,unsigned index,std::uint64_t key)const{
        if(owner!=owner_||index>=samples_.size())return {};
        const auto& s=samples_[index];
        return s.valid&&s.key==key?std::optional<float>(s.depth):std::nullopt;
    }
};
}
