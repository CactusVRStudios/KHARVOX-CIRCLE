// Discover exact native type through MSVC RTTI, relative to the verified game
// image. No store-specific vtable RVA or asset-prefix allowlist is required.
WorldModelKind nativeWorldModelKind(uintptr_t model){
    const auto vt=field<uintptr_t>(model);
    static std::mutex typeMutex;static std::unordered_map<uintptr_t,WorldModelKind> types;
    std::lock_guard lock(typeMutex);
    const auto found=types.find(vt);if(found!=types.end())return found->second;
    const auto nt=field<unsigned>(base+0x3c);
    const auto size=field<unsigned>(base+nt+0x50);
    if(!base||nt>0x1000||field<unsigned>(base+nt)!=0x4550||size<4096||vt<base+8||vt>=base+size)return WorldModelKind::Unsupported;
    const auto col=field<uintptr_t>(vt-8);std::array<unsigned,6> locator{};
    WorldModelKind kind=WorldModelKind::Unsupported;
    if(col>=base&&col+sizeof(locator)<=base+size&&read(col,locator.data(),sizeof(locator))&&
       locator[0]==1&&col-base==locator[5]&&locator[3]<size&&size-locator[3]>=144){
        char name[128]{};
        if(read(base+locator[3]+16,name,sizeof(name)-1))kind=worldModelKind(name);
    }
    if(types.size()<256)types.emplace(vt,kind);
    return kind;
}
// Uses the existing native model-status and affine-builder boundaries.
struct WorldModelEntry {
    uintptr_t model{},committed{},manager{};unsigned index{};uint64_t seen{};
    std::array<char,160> asset{};std::array<char,256> instance{};WorldModelKind kind{};WorldModelPair pair;bool rightObserved{},pairEnabled{true};
};
std::mutex worldModelMutex;
std::array<WorldModelEntry,16384> worldModels;
std::unordered_map<uintptr_t,size_t> worldModelLookup;
void observeWorldModel(uintptr_t model,uintptr_t committed){
    if(!adapterInstalled||stopped||!inGameplay||monoVrEnabled()||!model||!committed)return;
    const auto kind=nativeWorldModelKind(model);if(kind==WorldModelKind::Unsupported)return;
    std::array<char,160> asset{};std::array<char,256> instance{};
    if(!read(field<uintptr_t>(model+0x90),asset.data(),asset.size()-1)||
       !read(field<uintptr_t>(model+0x98),instance.data(),instance.size()-1)||
       !pairableWorldModel(asset.data(),instance.data(),(field<unsigned char>(model+0x183)&2)!=0,kind)||
       field<uintptr_t>(model+0x88)!=committed)return;
    const auto manager=field<uintptr_t>(committed+0x28);const auto index=field<unsigned>(committed+0x30);
    if(!manager||index>=1u<<20)return;
    const auto now=GetTickCount64();std::lock_guard lock(worldModelMutex);
    WorldModelEntry* entry=nullptr;
    const auto found=worldModelLookup.find(committed);
    if(found!=worldModelLookup.end())entry=&worldModels[found->second];
    if(!entry)for(auto& e:worldModels)if(!e.model||now-e.seen>5000){entry=&e;break;}
    if(!entry){
        static unsigned rejected{};const auto n=++rejected;
        if(n<=8||n%1000==0)circlevr::vkcore::logf("motor-world-registry: full capacity %u rejected %u asset %s instance %s",unsigned(worldModels.size()),n,asset.data(),instance.data());
        return;
    } // Never evict an active stereo pair.
    if(entry->kind!=kind||entry->model!=model||entry->committed!=committed||entry->manager!=manager||entry->index!=index||
       std::strcmp(entry->asset.data(),asset.data())||std::strcmp(entry->instance.data(),instance.data())){
        worldModelLookup.erase(entry->committed);*entry={};
        entry->model=model;entry->committed=committed;entry->manager=manager;entry->index=index;
        entry->asset=asset;entry->instance=instance;entry->kind=kind;
        circlevr::vkcore::logf("motor-world-type: registered kind %u model %p asset %s instance %s",unsigned(kind),(void*)model,asset.data(),instance.data());
        if(worldDoorAsset(asset.data()))circlevr::vkcore::logf("motor-world-registry: door registered model %p committed %p manager %p index %u asset %s instance %s",(void*)model,(void*)committed,(void*)manager,index,asset.data(),instance.data());
    }
    entry->seen=now;worldModelLookup[committed]=size_t(entry-worldModels.data());
}
// The native dirty-model loop need not visit resting doors at all. Traverse
// a bounded slice of its owning world's live model table once per render.
void observeWorldModelSlice(uintptr_t world){
    if(!adapterInstalled||stopped||!inGameplay||monoVrEnabled()||!world)return;
    static std::mutex scanMutex;static uintptr_t priorWorld{};static unsigned priorFrame{},cursor{};
    const auto frame=field<unsigned>(base+build::rva(0x4a71950));
    std::unique_lock scan(scanMutex,std::try_to_lock);if(!scan.owns_lock())return;
    if(priorWorld==world&&priorFrame==frame)return;
    if(priorWorld!=world){priorWorld=world;cursor=0;}
    priorFrame=frame;
    const auto table=field<uintptr_t>(world+0x588);const auto count=field<unsigned>(world+0x600);
    if(!table||!count||count>1u<<20)return;
    if(cursor>=count)cursor=0;
    const auto end=std::min(count,cursor+1024u);
    for(;cursor<end;++cursor){
        const auto model=field<uintptr_t>(table+cursor*8ull);
        if(model&&field<uintptr_t>(model+0x108)==world)
            observeWorldModel(model,field<uintptr_t>(model+0x88));
    }
}
void worldModelPlacement(safetyhook::Context& r){
    if(!adapterInstalled||stopped||!inGameplay||monoVrEnabled())return;
    const auto eye=circlevr::vkcore::seqChainEye();
    if(eye!=circlevr::stereo_seq::Eye::Left&&eye!=circlevr::stereo_seq::Eye::Right)return;
    const auto token=field<uint64_t>(base+build::rva(0x4c523a8));
    const auto frame=field<unsigned>(base+build::rva(0x4a71950));
    std::lock_guard lock(worldModelMutex);
    const auto found=worldModelLookup.find(r.rdi);if(found==worldModelLookup.end())return;
    auto& e=worldModels[found->second];
    if(GetTickCount64()-e.seen>5000||field<uintptr_t>(e.model+0x88)!=e.committed||
       field<uintptr_t>(e.committed+0x28)!=e.manager||field<unsigned>(e.committed+0x30)!=e.index||
       r.r15!=field<uintptr_t>(e.manager+0x108)+e.index*60ull)return;
    const auto kind=nativeWorldModelKind(e.model);
    if(kind!=e.kind){e.pair={};return;}
    char asset[160]{},instance[256]{};
    if(!read(field<uintptr_t>(e.model+0x90),asset,sizeof(asset)-1)||
       !read(field<uintptr_t>(e.model+0x98),instance,sizeof(instance)-1)||
       std::strcmp(asset,e.asset.data())||std::strcmp(instance,e.instance.data())||
       !pairableWorldModel(asset,instance,(field<unsigned char>(e.model+0x183)&2)!=0,kind)){e.pair={};return;}
    std::array<float,15> native{},selected{};
    const bool right=eye==circlevr::stereo_seq::Eye::Right;
    if(!read(r.r15,native.data(),sizeof(native))){if(!right)e.pair={};return;}
    const bool revisedLeft=!right&&e.pair.saved.token==token&&e.pair.saved.frame==frame&&
        e.pair.saved.placement!=native&&ModelPairPlacement::finite(native);
    if(!right&&(e.pair.saved.token!=token||e.pair.saved.frame!=frame))
        e.pairEnabled=diagnostics::pairedWorldModels().load();
    if(!e.pair.select(right,e.model,e.committed,e.manager,e.index,token,frame,native,selected))return;
    if(revisedLeft){
        static unsigned revisions{};const auto n=++revisions;
        if(n<=16||n%600==0)circlevr::vkcore::logf("motor-world-pair: native left rebuild retained model %p token %llu frame %u asset %s instance %s count %u",(void*)e.model,token,frame,asset,instance,n);
    }
    if(right&&!e.rightObserved){
        e.rightObserved=true;
        circlevr::vkcore::logf("motor-world-type: right-pair kind %u model %p token %llu frame %u equal %u asset %s instance %s",unsigned(e.kind),(void*)e.model,token,frame,unsigned(native==selected),asset,instance);
    }
    if(right&&std::strstr(asset,"propeller")){
        static unsigned observations{};const auto n=++observations;
        if(n<=32||n%600==0)circlevr::vkcore::logf("motor-world-comparison: propeller model %p token %llu frame %u enabled %u nativeEqual %u asset %s",(void*)e.model,token,frame,unsigned(e.pairEnabled),unsigned(native==selected),asset);
    }
    if(!e.pairEnabled||native==selected)return;
    // Full affine source: origin, rotation AND scale. Bounds and inverse use
    // the same source. Never write actor/model state or freeze the next pair.
    thread_local std::array<float,15> source;
    source=selected;r.r15=reinterpret_cast<uintptr_t>(source.data());r.rdx=r.r15+12;r.r8=r.r15+48;
    static unsigned fixes{};const auto n=++fixes;
    if(n<=8||n%600==0){float delta=0;for(unsigned i=0;i<15;++i)delta=std::fmax(delta,std::fabs(native[i]-selected[i]));
        circlevr::vkcore::logf("motor-world-pair: aligned eye %u token %llu frame %u model %p affineDelta %.9g asset %s instance %s count %u",
            right?1:0,token,frame,(void*)e.model,delta,asset,instance,n);}
}
