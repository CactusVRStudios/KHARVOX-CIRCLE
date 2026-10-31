struct HairPlacementEntry {
    HairPairPlacement pair;
    std::array<char,160> asset{};
    std::array<char,256> instance{};
};
std::mutex hairPlacementMutex;
std::array<HairPlacementEntry,64> hairPlacements;
bool pairedHairSource(uintptr_t model,uintptr_t view,std::array<float,15>& selected){
    if(!adapterInstalled||stopped||!inGameplay||!immersive()||monoVrEnabled()||
       view!=projectedNativeView.load()||field<uintptr_t>(model)!=base+build::rva(0x3649f60))return false;
    const auto eye=circlevr::vkcore::seqChainEye();
    if(eye!=circlevr::stereo_seq::Eye::Left&&eye!=circlevr::stereo_seq::Eye::Right)return false;
    const auto token=field<uint64_t>(base+build::rva(0x4c523a8));
    const auto frame=field<unsigned>(base+build::rva(0x4a71950));
    std::array<char,160> asset{};std::array<char,256> instance{};
    if(!read(field<uintptr_t>(model+0x90),asset.data(),asset.size()-1)||
       !read(field<uintptr_t>(model+0x98),instance.data(),instance.size()-1)||
       !pairableNpcHair(asset.data(),instance.data(),(field<unsigned char>(model+0x183)&2)!=0))return false;
    const auto committed=field<uintptr_t>(model+0x88);
    const auto manager=field<uintptr_t>(committed+0x28);
    const auto index=field<unsigned>(committed+0x30);
    std::array<float,15> native{};
    if(!committed||!manager||index>=1u<<20)return false;
    std::lock_guard lock(hairPlacementMutex);
    HairPlacementEntry* entry=nullptr;
    for(auto& e:hairPlacements)if(e.pair.saved.model==model){entry=&e;break;}
    if(!entry)for(auto& e:hairPlacements)if(!e.pair.saved.model||e.pair.saved.token!=token){entry=&e;break;}
    if(!entry)return false;
    if(std::strcmp(entry->asset.data(),asset.data())||std::strcmp(entry->instance.data(),instance.data())){
        entry->pair={};entry->asset=asset;entry->instance=instance;
    }
    const bool right=eye==circlevr::stereo_seq::Eye::Right;
    if(!read(model+0x120,native.data(),sizeof(native))){if(!right)entry->pair={};return false;}
    if(!entry->pair.select(right,model,committed,manager,view,index,token,frame,native,selected)){
        static unsigned misses{};
        if(right&&++misses<=8)circlevr::vkcore::logf("motor-hair-pair: pair miss token %llu frame %u leftToken %llu leftFrame %u asset %s",
            token,frame,entry->pair.saved.token,entry->pair.saved.frame,asset.data());
        return false;
    }
    if(!right){static unsigned saves{};if(++saves<=4)circlevr::vkcore::logf(
        "motor-hair-pair: left source saved token %llu frame %u model %p asset %s",token,frame,(void*)model,asset.data());}
    if(right){
        float delta=0;for(unsigned i=0;i<3;++i)delta=std::fmax(delta,std::fabs(native[i]-selected[i]));
        static unsigned count{};const auto n=++count;
        if(n<=8||(delta>0.0001f&&n%120==0))circlevr::vkcore::logf(
            "motor-hair-pair: right source aligned token %llu frame %u model %p positionDelta %.9g asset %s count %u",
            token,frame,(void*)model,delta,asset.data(),n);
    }
    return true;
}
struct HairOperandSource {
    alignas(16) std::array<unsigned char,0x160> bytes{};
    uintptr_t model{},stack{};
};
thread_local HairOperandSource hairCurrentOperand,hairHistoryOperand;
void hairCurrentPlacementBegin(safetyhook::Context& r){
    // 89A953..89AAB7 is a straight-line block with only model+120..14C reads.
    // Redirect RDI for that block; never modify the shared model or its owner.
    if(hairCurrentOperand.model)return;
    std::array<float,15> placement{};
    if(!pairedHairSource(r.rdi,r.rsi,placement))return;
    std::memcpy(hairCurrentOperand.bytes.data()+0x120,placement.data(),sizeof(placement));
    hairCurrentOperand.model=r.rdi;hairCurrentOperand.stack=r.rsp;
    r.rdi=reinterpret_cast<uintptr_t>(hairCurrentOperand.bytes.data());
}
void hairCurrentPlacementEnd(safetyhook::Context& r){
    if(hairCurrentOperand.model&&hairCurrentOperand.stack==r.rsp&&
       r.rdi==reinterpret_cast<uintptr_t>(hairCurrentOperand.bytes.data())){
        r.rdi=hairCurrentOperand.model;hairCurrentOperand.model=0;
    }
}
void hairHistoryCurrentPlacement(safetyhook::Context& r){
    // 899FC2 reads the current origin/axes for the history transform builder.
    // RCX is replaced by a stack argument at 89A01D, before the next call.
    // Previous matrices and each eye's current/previous view stay untouched.
    std::array<float,15> placement{};
    if(!pairedHairSource(r.rcx,r.r11,placement))return;
    std::memcpy(hairHistoryOperand.bytes.data()+0x120,placement.data(),sizeof(placement));
    r.rcx=reinterpret_cast<uintptr_t>(hairHistoryOperand.bytes.data());
}
