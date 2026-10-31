#pragma once
#include <cstdint>
#include <limits>
#include <cmath>

namespace circle::motor::gpu_audit {
struct ImageCrop { unsigned x{},y{},width{},height{}; };
inline ImageCrop imageCrop(unsigned w,unsigned h,float x,float y) {
    if(!w||!h||w>16384||h>16384||!std::isfinite(x)||!std::isfinite(y)||
       x<0||x>1||y<0||y>1)return {};
    const unsigned cw=w<512?w:512,ch=h<512?h:512;
    const auto start=[](unsigned n,unsigned crop,float center) {
        const int v=int(center*n)-int(crop/2);
        return unsigned(v<0?0:(v>int(n-crop)?int(n-crop):v));
    };
    return {start(w,cw,x),start(h,ch,y),cw,ch};
}
struct PairWindow {
    std::uint64_t token{};
    unsigned leftFrame{};
    bool accept(std::uint64_t t,unsigned frame,unsigned eye) {
        if(!t||eye>1)return false;
        if(!token){if(eye!=0)return false;token=t;leftFrame=frame;}
        return t==token&&frame==leftFrame+eye;
    }
};
constexpr std::uint64_t targetedRangeLimit=64ull*1024;
constexpr bool targetedRange(std::uint64_t offset,std::uint64_t size){
    return size&&size<=targetedRangeLimit&&offset%4==0&&size%4==0&&
        offset<=std::numeric_limits<std::uint64_t>::max()-size;
}
struct Range { std::uint64_t offset{}, size{}; bool valid{}, complete{}; };
// Never wrap an offset, or pretend a bounded sample covers an entire resource.
constexpr Range range(std::uint64_t bufferSize, std::uint64_t offset,
                      std::uint64_t requested, std::uint64_t limit) {
    if(offset>=bufferSize||!limit||offset%4)return {};
    const auto available=bufferSize-offset;
    const auto n=requested==std::numeric_limits<std::uint64_t>::max()?available:requested;
    if(!n||n>available)return {};
    const auto copied=(n<limit?n:limit)&~std::uint64_t{3};
    return {offset,copied,copied!=0,copied==n};
}
struct Budget {
    std::uint64_t used{};
    static constexpr std::uint64_t maximum=768ull*1024*1024;
    bool reserve(std::uint64_t n) {
        if(!n||n>maximum-used)return false;
        used+=n;return true;
    }
};
}
