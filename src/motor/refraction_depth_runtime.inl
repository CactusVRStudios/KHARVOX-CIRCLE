// Included inside refraction's anonymous namespace. Native entry lists, GPU
// depth images and draw indices are never written by this path.
struct SortOwner {std::uintptr_t options{};TransparentSortScope scope;};
std::mutex sortOwnerMutex;
std::array<SortOwner,64> sortOwners{};
unsigned sortOwnerCursor{};
struct DepthBatch {
    RefractionDepthCache cache;
    RefractionDepthOwner owner;
    std::uintptr_t stack{};
    float farPlane{};
    bool grouping{},finished{};
    unsigned captured{},recovered{},invalid{},reversed{};
    float minimum{},maximum{};
};
thread_local DepthBatch depthBatch;
template<class T>bool depthRead(std::uintptr_t p,T& value){
    SIZE_T n{};return p&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&value,sizeof(value),&n)&&n==sizeof(value);
}
void beginDepth(safetyhook::Context& r){
    auto& b=depthBatch;b.cache.clear();b.owner={};b.grouping=b.finished=false;
    b.captured=b.recovered=b.invalid=b.reversed=0;b.minimum=b.maximum=0;
    // Steam C4890B / GOG C316EB: R13 has just captured the exact context.
    const auto context=r.r13;
    if(!context||r.rdx!=context||!diagnostics::transparentSortFallback())return;
    const auto options=get<std::uintptr_t>(context+8),view=get<std::uintptr_t>(context+0x10);
    const auto backend=get<unsigned>(context+0xf0);
    const auto tag=circlevr::vkcore::seqTagForBackendFrame(backend+1u);
    if(!options||!view||!tag||!tag->tick||!circlevr::stereo_seq::drawsEyeView(*tag)||
       get<unsigned char>(options+0x15)!=1||get<int>(view+0x18f34)!=0)return;
    const unsigned eye=tag->eye==circlevr::stereo_seq::Eye::Right?1u:0u;
    bool owned=false;
    {
        std::lock_guard lock(sortOwnerMutex);
        for(const auto& s:sortOwners)if(s.options==options&&s.scope.matches(view,tag->tick,tag->renderFrame,eye,0)){owned=true;break;}
    }
    if(!owned){
        static std::atomic<unsigned> rejected{};if(++rejected<=8)
            circlevr::vkcore::logf("motor-refraction-depth: no validated stable-order owner; unchanged eye %u backend %u render %u tick %llu options %p view %p",eye,backend,tag->renderFrame,tag->tick,(void*)options,(void*)view);
        return;
    }
    std::array<int,2> range{};
    const auto ranges=get<std::uintptr_t>(context+0xe0),entries=get<std::uintptr_t>(context+0xe8);
    const float farPlane=get<float>(options+0x78);
    if(!ranges||!depthRead(ranges+0x20,range)||range[0]<0||range[1]<range[0]||
       unsigned(range[1])>=RefractionDepthCache::maxSurfaces||!std::isfinite(farPlane)||farPlane<=0)return;
    const RefractionDepthOwner owner{context,view,entries,tag->tick,backend,eye};
    try{if(!b.cache.begin(owner,unsigned(range[1])+1))return;}catch(...){b.cache.clear();return;}
    b.owner=owner;b.stack=r.rbp;b.farPlane=farPlane;b.grouping=true;
}
bool depthDraw(const safetyhook::Context& r){
    const auto& b=depthBatch;
    return b.grouping&&r.r13==b.owner.context&&r.rbp==b.stack&&r.r14==b.owner.entries&&
        r.rbx<RefractionDepthCache::maxSurfaces&&r.rdi==r.rbx*32;
}
void captureDepth(safetyhook::Context& r){
    if(!depthDraw(r))return;
    // Before the native bounds-projection helper: R8 is the material's actual
    // local bounds (including its override), RBP+10 is the freshly assembled
    // model-view-projection W row. Same geometry and eye as the native pass.
    auto& b=depthBatch;
    std::array<float,6> bounds{};std::array<float,4> wRow{};std::uint64_t key{};
    if(!depthRead(r.r8,bounds)||!depthRead(r.rbp+0x10,wRow)||!depthRead(r.r14+r.rdi,key)){++b.invalid;return;}
    const auto depth=refractionViewDepth(bounds,wRow,b.farPlane);
    if(!depth||!b.cache.put(unsigned(r.rbx),key,*depth)){++b.invalid;return;}
    if(!b.captured)b.minimum=b.maximum=*depth;
    else {b.minimum=std::min(b.minimum,*depth);b.maximum=std::max(b.maximum,*depth);}
    ++b.captured;
}
void useDepth(safetyhook::Context& r){
    if(!depthDraw(r))return;
    auto& b=depthBatch;std::uint64_t key{};
    if(!depthRead(r.r14+r.rdi,key))return;
    if(const auto depth=b.cache.get(b.owner,unsigned(r.rbx),key)){
        // Immediately AFTER far - packedKeyDepth, BEFORE the grouping code
        // reads XMM6. No change to native order, bounds tests or sort keys.
        r.xmm6.f32[0]=*depth;++b.recovered;
    }
}
void depthSeparation(safetyhook::Context& r){
    if(!depthDraw(r))return;
    const auto delta=r.xmm7.f32[0];
    if(std::isfinite(delta)){
        if(delta<0)++depthBatch.reversed;
        r.xmm7.f32[0]=refractionDepthSeparation(delta);
    }
}
void finishDepth(safetyhook::Context& r){
    auto& b=depthBatch;
    if(!b.grouping||r.rdi!=b.owner.context)return;
    b.grouping=false;b.finished=true;
    static std::atomic<unsigned> counts[2]{};const auto n=++counts[b.owner.eye];
    if(n<=8||n%1200==0)
        circlevr::vkcore::logf("motor-refraction-depth: eye %u backend %u tick %llu context %p view %p stableOrder 1 captured %u recovered %u invalid %u depth %.4f..%.4f far %.1f reversedGaps %u groups %u; native draw list and GPU depth unchanged; sample %u",
            b.owner.eye,b.owner.backend,b.owner.tick,(void*)b.owner.context,(void*)b.owner.view,b.captured,b.recovered,b.invalid,b.minimum,b.maximum,b.farPlane,b.reversed,unsigned(r.rax),n);
}
void nearestDepth(safetyhook::Context& r){
    auto& b=depthBatch;
    // A second consumer chooses the nearest special refraction/mask surface.
    // It must agree with layer grouping instead of seeing the old far=8192.
    if(!b.finished||r.rdi!=b.owner.context||r.rdx>=RefractionDepthCache::maxSurfaces||
       r.r8!=b.owner.entries+r.rdx*32||get<unsigned>(r.rdi+0xf0)!=b.owner.backend||
       get<std::uintptr_t>(r.rdi+0x10)!=b.owner.view||get<std::uintptr_t>(r.rdi+0xe8)!=b.owner.entries)return;
    std::uint64_t key{};if(!depthRead(r.r8,key))return;
    if(const auto depth=b.cache.get(b.owner,unsigned(r.rdx),key)){
        r.xmm0.f32[0]=*depth;
        static std::atomic<unsigned> counts[2]{};const auto n=++counts[b.owner.eye];
        if(n<=4||n%12000==0)circlevr::vkcore::logf("motor-refraction-depth: nearest consumer eye %u backend %u draw %u depth %.4f sample %u",b.owner.eye,b.owner.backend,unsigned(r.rdx),*depth,n);
    }
}
