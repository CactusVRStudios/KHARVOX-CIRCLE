// Motor equivalents of CircleVR keep_prev_hooks / moved_flag_hooks.
// Only active together with the per-eye TAA images and camera histories.
struct TemporalModelStamp {uintptr_t manager{};unsigned index{},frame{};};
std::mutex temporalModelMutex;
std::unordered_map<uintptr_t,TemporalModelStamp> temporalModelStamps;
uint64_t temporalModelToken{};
struct TemporalPreviousSave {uintptr_t at{};std::array<unsigned char,64> bytes{};};
thread_local TemporalPreviousSave temporalPreviousSave;
void temporalModelBefore(safetyhook::Context& r){
    temporalPreviousSave.at=0;
    if(!adapterInstalled||stopped||!inGameplay||!temporal::active())return;
    const auto eye=circlevr::vkcore::seqChainEye();
    const auto token=field<uint64_t>(base+build::rva(0x4c523a8));
    const auto frame=field<unsigned>(base+build::rva(0x4a71950));
    const auto manager=field<uintptr_t>(r.rdi+0x28);
    const auto index=field<unsigned>(r.rdi+0x30);
    if(!token||!manager||index>=1u<<20||r.r10!=index*64ull||
       r.r11!=field<uintptr_t>(manager+0xf0))return;
    std::lock_guard lock(temporalModelMutex);
    if(temporalModelToken!=token){temporalModelStamps.clear();temporalModelToken=token;}
    if(eye==circlevr::stereo_seq::Eye::Left){
        if(temporalModelStamps.size()<1u<<20)temporalModelStamps[r.rdi]={manager,index,frame};
        return;
    }
    const auto i=temporalModelStamps.find(r.rdi);
    if(eye!=circlevr::stereo_seq::Eye::Right||i==temporalModelStamps.end()||
       i->second.manager!=manager||i->second.index!=index||frame-i->second.frame!=1)return;
    const auto at=r.r11+r.r10;
    if(read(at,temporalPreviousSave.bytes.data(),64))temporalPreviousSave.at=at;
}
void temporalModelAfter(){
    if(!temporalPreviousSave.at)return;
    // Restore before native bit0x10 decides whether this model has motion.
    // The native no-motion branch can still initialize previous=current.
    std::memcpy(reinterpret_cast<void*>(temporalPreviousSave.at),temporalPreviousSave.bytes.data(),64);
    temporalPreviousSave.at=0;
    static std::atomic<unsigned> count{};const auto n=++count;
    if(n<=8||n%2400==0)circlevr::vkcore::logf("motor-temporal-model: previous model matrix retained for right eye count %u",n);
}
void temporalMovedStatus(safetyhook::Context& r){
    observeWorldModelSlice(r.rsi); // validated native world at 83F068
    if(!adapterInstalled||stopped||!inGameplay||!temporal::active())return;
    // Before test cl,cl in 83ED50. Status1 is cleanup only: it clears the
    // moved flag and rebuilds surfaces without velocities. Defer to next L.
    if(circlevr::vkcore::seqChainEye()!=circlevr::stereo_seq::Eye::Right||(r.rcx&255)!=1)return;
    r.rcx&=~uintptr_t{255};
    static std::atomic<unsigned> count{};const auto n=++count;
    if(n<=8||n%2400==0)circlevr::vkcore::logf("motor-temporal-model: right moved cleanup deferred count %u",n);
}
