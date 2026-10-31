// Motor adaptation of CircleVR fx_sync_hooks / fx_gpu_stages (repository
// third-party notices apply). Motor inlines GPU-stage binding and does not
// stamp-gate its particle bind. Do NOT skip that native GPU binding: it fills
// the per-render draw and light-atlas lists, even when CPU surfaces are reused.
std::mutex particlePairMutex;
struct ParticleRingState { ParticlePair pair{}; unsigned pass{}; bool reused{}; };
std::array<ParticleRingState,16> particleRings{};
struct ParticlePrepare { uintptr_t world{},ring{},pool{}; uint64_t token{}; unsigned frame{}; bool left{},enabled{}; };
thread_local ParticlePrepare particlePrepare;
struct ParticleBind { uintptr_t model{},world{},ring{}; unsigned frame{}; bool reused{}; };
thread_local ParticleBind particleBind;
std::atomic<uint64_t> particleAdvances[2]{},particleKeeps{},particleCpuBindings{},particleGpuBindings{},
    particleGenerations[2]{},particleGenerationKeeps{},effectGenerationKeeps{},particlePools{},particleLights{};
std::atomic<uint64_t> particleReportTime{};

bool particleActive() {
    return adapterInstalled && !stopped && inGameplay && !monoVrEnabled();
}
// The GPU emitter queue bypasses object current/inverse matrix uploads.
// Give native generation a private current pose and a coherent previous pose;
// leave the native model/physics poses and shared particle declarations intact.
std::mutex lighterParticleMutex;
// Reserve the original eight entries for the lighter; shot bursts must not
// evict a persistent flame's history. Muzzles use 24; torches and candles each have eight dedicated slots.
std::array<LighterParticleHistory,48> lighterParticleHistory{};
unsigned lighterParticleNext{},muzzleParticleNext{},torchParticleNext{},candleParticleNext{};
struct LighterParticleGeneration {
    uintptr_t model{},runtime{};
    std::array<float,15> current{},previous{};
};
thread_local LighterParticleGeneration lighterParticleGeneration;
void lighterParticleBegin(safetyhook::Context& r){
    lighterParticleGeneration={};
    if(!particleActive()||inCinema)return;
    char asset[160]{};
    if(!read(field<uintptr_t>(r.rbx+0x90),asset,sizeof(asset)-1))return;
    const bool muzzle=hands::muzzleParticleAsset(asset);
    const bool torch=std::string_view(asset)=="props/torch/particle_prop_torch_01.prt";
    const bool candle=candleEmitterAsset(asset);
    if(!muzzle&&!torch&&!candle&&std::string_view(asset)!="props/lighter/particle_prop_lighter_01.prt")return;
    PlayerModelAudit child;
    const auto runtime=field<uintptr_t>(r.rbx+0x650);
    if(!runtime)return;
    std::array<float,15> pose{};
    if(!read(r.rdi,pose.data(),sizeof(pose)))return;
    // Match the immutable native spawn pose, not an already interpolated or
    // corrected model-table copy. This also avoids a second render delta.
    if(muzzle&&!read(r.rbx+0x120,pose.data(),sizeof(pose)))return;
    const auto native=pose;
    const auto entries=field<uintptr_t>(runtime+0x10);
    const int count=field<int>(runtime+0x18);
    if(!entries||count<1||count>64)return;
    // The same runtime can retain its address across stage destruction/reuse.
    uint64_t allocation=entries;
    for(int i=0;i<count;++i)allocation=(allocation^field<unsigned>(entries+i*0x38+4))*1099511628211ull;
    const auto token=field<uint64_t>(base+build::rva(0x4c523a8));
    bool corrected=false;
    std::array<float,15> reconstructed{};
    {
        std::lock_guard auditLock(modelAuditMutex);
        corrected=attachedEffectModel(r.rbx,child)&&(child.rigidFlame||child.rigidTorch||child.rigidCandle||child.muzzleFlash)&&
            attachedRenderPlacement(child,token,pose);
        reconstructed=pose;
        // The above validates the live VR anchor, but its simulation-source
        // reconstruction trails the actual held-object matrix during movement.
        // Consume the parent's already built render affine in this generation.
        PlayerModelAudit parent;
        std::array<float,16> matrix{},verify{};
        if(corrected&&(child.rigidFlame||child.rigidTorch||child.rigidCandle)){
            corrected=playerModel(child.attachedParent,parent)&&parent.carried&&
                parent.owner==child.owner&&parent.manager==child.manager;
            if(corrected){
                const auto table=field<uintptr_t>(parent.manager+0xc0);
                const auto address=table+parent.index*64ull;
                bool currentParent=false;
                const auto frame=field<unsigned>(base+build::rva(0x4a71950));
                for(const auto& placement:playerPlacements)
                    if(placement.model==parent.model&&placement.committed==parent.committed&&
                       placement.manager==parent.manager&&placement.owner==parent.owner&&
                       placement.index==parent.index&&placement.token==token&&
                       (placement.frame==frame||frame-placement.frame==1))currentParent=true;
                // Before the parent rebuild (usually the first eye), the affine
                // table can still belong to the preceding pair. Keep the fresh
                // anchor reconstruction until a matching parent placement exists.
                if(currentParent)corrected=table&&read(address,matrix.data(),sizeof(matrix))&&
                    read(address,verify.data(),sizeof(verify))&&matrix==verify&&
                    field<uintptr_t>(parent.model+0x88)==parent.committed&&
                    heldParticlePlacement(child,parent,matrix,{native[12],native[13],native[14]},pose);
            }
        }
    }
    std::lock_guard lock(lighterParticleMutex);
    auto histories=std::span(lighterParticleHistory).subspan(muzzle?8:torch?32:candle?40:0,muzzle?24:8);
    auto* history=&histories[0];
    bool found=false;
    for(auto& h:histories)if(h.model==r.rbx){history=&h;found=true;break;}
    if(!corrected){
        static std::atomic<unsigned> rejected[4]{};const auto n=++rejected[muzzle?0:torch?1:candle?3:2];
        if(n<=20||n%1000==0)circlevr::vkcore::logf("motor-%s-particle-rejected: model %p runtime %p count %u (ownership/current-anchor validation)",muzzle?"muzzle":torch?"torch":candle?"candle":"lighter",(void*)r.rbx,(void*)runtime,n);
        // Tracking/ownership fallback must not differentiate a native pose
        // against the last VR emitter pose and inject a false velocity.
        if(found){
            std::memcpy(reinterpret_cast<void*>(r.rbx+0x4cc),native.data(),12);
            std::memcpy(reinterpret_cast<void*>(r.rbx+0x4e4),native.data()+3,36);
            *history={};
        }
        return;
    }
    if(!found)history=&histories[(muzzle?muzzleParticleNext++:torch?torchParticleNext++:candle?candleParticleNext++:lighterParticleNext++)%histories.size()];
    if((r.rsi&0xff)||(field<unsigned char>(r.rbx+0x184)&0x20))*history={};
    const bool reset=history->advance(r.rbx,child.committed,runtime,child.attachedParent,
        child.owner,token,field<int>(r.rbx+0x5d0),GetTickCount64(),pose,allocation);
    lighterParticleGeneration={r.rbx,runtime,history->current,history->previous};
    std::memcpy(reinterpret_cast<void*>(r.rbx+0x4cc),history->previous.data(),12);
    r.rdi=reinterpret_cast<uintptr_t>(lighterParticleGeneration.current.data());
    static std::atomic<uint64_t> last{};const auto now=GetTickCount64();auto before=last.load();
    if(now-before>=1000&&last.compare_exchange_strong(before,now)){
        circlevr::vkcore::logf("motor-%s-particle-history: model %p runtime %p token %llu reset %u position %.5f %.5f %.5f",muzzle?"muzzle":torch?"torch":candle?"candle":"lighter",(void*)r.rbx,(void*)runtime,token,unsigned(reset),history->current[0],history->current[1],history->current[2]);
        if(!muzzle&&!torch&&!candle)circlevr::vkcore::logf("motor-lighter-render-source: parent %p frame %u matrix %.5f %.5f %.5f reconstructionDelta %.5f %.5f %.5f",(void*)child.attachedParent,field<unsigned>(base+build::rva(0x4a71950)),pose[0],pose[1],pose[2],pose[0]-reconstructed[0],pose[1]-reconstructed[1],pose[2]-reconstructed[2]);
    }
}
void lighterParticlePreviousAxes(safetyhook::Context& r){
    if(lighterParticleGeneration.model==r.rbx)
        r.rax=reinterpret_cast<uintptr_t>(lighterParticleGeneration.previous.data()+3);
}
void lighterParticleAngularAxes(safetyhook::Context& r){
    if(lighterParticleGeneration.model==r.rbx)
        r.rax=reinterpret_cast<uintptr_t>(lighterParticleGeneration.current.data()+5);
}
void lighterParticleCurrentAxes(safetyhook::Context& r){
    if(lighterParticleGeneration.model==r.rbx)
        r.rax=reinterpret_cast<uintptr_t>(lighterParticleGeneration.current.data()+3);
}
void lighterParticleGpuUpdate(safetyhook::Context& r){
    const auto& g=lighterParticleGeneration;
    if(!g.model||g.model!=r.rbx||g.runtime!=r.rcx||r.rdx!=r.rbp+0xc0)return;
    // Native BE1550 copies these to the GPU queue as position/quaternion.
    std::memcpy(reinterpret_cast<void*>(r.rdx+4),g.current.data(),48);
}
// Backend descriptor binding, after native simulation/upload chose its parity.
// Refresh only owned lighter/muzzle rigid emitter fields in that mapped slot.
void lighterParticleDrawBind(safetyhook::Context& r){
    if(!particleActive()||inCinema)return;
    const auto manager=r.rcx;
    const int parity=field<int>(manager+0x18078);
    if(parity<0||parity>1)return;
    const auto descriptor=manager+0x180a0+parity*0x78;
    std::array<unsigned char,0x78> raw{};
    if(!read(descriptor,raw.data(),raw.size()))return;
    const auto buffer=GpuEmitterBuffer::decode(raw);
    static std::atomic<unsigned> calls{};const auto call=++calls;
    const bool report=call<=4||call%600==0;
    if(report)circlevr::vkcore::logf("motor-lighter-draw-descriptor: call %u parity %d descriptor %p mapped %p capacity %llu valid %u",call,parity,(void*)descriptor,(void*)buffer.mapped,buffer.capacity,unsigned(buffer.mapped!=0));
    if(!buffer.mapped)return;
    std::array<LighterParticleHistory,48> candidates;
    {std::lock_guard lock(lighterParticleMutex);candidates=lighterParticleHistory;}
    const auto now=GetTickCount64();
    unsigned written=0;
    std::lock_guard lock(modelAuditMutex);
    for(const auto& h:candidates){
        if(!h.model||now<h.seen||now-h.seen>100||field<uintptr_t>(h.runtime+8)!=manager)continue;
        PlayerModelAudit child,parent;
        if(!attachedEffectModel(h.model,child)||(!child.rigidFlame&&!child.rigidTorch&&!child.rigidCandle&&!child.muzzleFlash)||child.committed!=h.committed||
           child.owner!=h.owner||child.attachedParent!=h.parent||field<uintptr_t>(h.model+0x650)!=h.runtime)continue;
        std::array<float,16> matrix{},verify{};std::array<float,15> pose{};
        if(child.muzzleFlash){
            if(!read(h.model+0x120,pose.data(),sizeof(pose))||
               !hands::renderMuzzleFlash(h.owner,field<uint64_t>(base+build::rva(0x4c523a8)),pose))continue;
        }else{
            if(!playerModel(h.parent,parent)||parent.owner!=h.owner||parent.manager!=child.manager)continue;
            const auto source=field<uintptr_t>(parent.manager+0xc0)+parent.index*64ull;
            if(!read(source,matrix.data(),sizeof(matrix))||!read(source,verify.data(),sizeof(verify))||matrix!=verify||
               !heldParticlePlacement(child,parent,matrix,{1,1,1},pose))continue;
        }
        const auto rotation=gpuEmitterRotation(pose);
        if(!rotation)continue;
        const std::array<float,4> q{rotation->x,rotation->y,rotation->z,rotation->w};
        const auto entries=field<uintptr_t>(h.runtime+0x10);const int count=field<int>(h.runtime+0x18);
        if(!entries||count<1||count>64)continue;
        for(int stage=0;stage<count;++stage){
            const int id=field<int>(entries+stage*0x38+4);
            if(!buffer.contains(id))continue;
            const auto target=buffer.mapped+uint64_t(id)*0xd0;
            std::array<float,3> before{};if(!read(target,before.data(),sizeof(before)))continue;
            // Match the native step's mapped destination and preserve every
            // lifetime, velocity, flag and allocation field surrounding these.
            std::memcpy(reinterpret_cast<void*>(target),pose.data(),12);
            std::memcpy(reinterpret_cast<void*>(target+0x20),q.data(),16);
            ++written;
            static std::atomic<unsigned> samples[4]{};
            if(stage==0&&samples[child.muzzleFlash?1:child.rigidTorch?2:child.rigidCandle?3:0].fetch_add(1)<1200)
                circlevr::vkcore::logf("motor-%s-draw-anchor: backendEye %u parity %d descriptor %p instance %d old %.5f %.5f %.5f new %.5f %.5f %.5f",child.muzzleFlash?"muzzle":child.rigidTorch?"torch":child.rigidCandle?"candle":"lighter",unsigned(circlevr::vkcore::seqFrameEndEye()),parity,(void*)descriptor,id,before[0],before[1],before[2],pose[0],pose[1],pose[2]);
        }
    }
    if(report)circlevr::vkcore::logf("motor-lighter-draw-result: call %u parity %d records %u",call,parity,written);
}
bool particleReusedWorld(uintptr_t world,uintptr_t ring,unsigned frame,ParticlePair* pair=nullptr) {
    std::lock_guard lock(particlePairMutex);
    for(const auto& s:particleRings)
        if(s.reused && s.pass==frame && s.pair.world==world && s.pair.ring==ring) {
            if(pair)*pair=s.pair;
            return true;
        }
    return false;
}
bool particleReusedModel(uintptr_t model,uintptr_t ring,size_t stampOffset) {
    if(!particleActive() || !model || !ring)return false;
    ParticlePair p;
    return particleReusedWorld(field<uintptr_t>(model+0x108),ring,
        field<unsigned>(base+build::rva(0x4a71950)),&p) &&
        p.generatedHere(field<unsigned>(model+stampOffset)) &&
        field<unsigned>(ring+0x480)==p.ringFrame;
}
void particleReport(){
    const auto now=GetTickCount64();auto before=particleReportTime.load();
    if(now-before<10000 || !particleReportTime.compare_exchange_strong(before,now))return;
    circlevr::vkcore::logf("motor-particle-pair: advances L/R %llu/%llu ringReuses %llu CPU-bind-reuses %llu GPU-native-stages %llu generations L/R %llu/%llu CPU-gen-reuses %llu effect-reuses %llu pools %llu lights %llu",
        particleAdvances[0].load(),particleAdvances[1].load(),particleKeeps.load(),particleCpuBindings.load(),
        particleGpuBindings.load(),particleGenerations[0].load(),particleGenerations[1].load(),
        particleGenerationKeeps.load(),effectGenerationKeeps.load(),particlePools.load(),particleLights.load());
}
void particlePrepareBefore(safetyhook::Context& r) {
    particlePrepare={};
    const auto world=r.rbx,ring=r.rcx;
    const auto pool=field<uintptr_t>(world+0x98);
    const auto frame=field<unsigned>(base+build::rva(0x4a71950));
    const auto token=field<uint64_t>(base+build::rva(0x4c523a8));
    const bool left=circlevr::vkcore::seqChainEye()==circlevr::stereo_seq::Eye::Left;
    const bool enabled=particleActive() && diagnostics::pairedParticles();
    particlePrepare={world,ring,pool,token,frame,left,enabled};
    const auto ringFrame=field<unsigned>(ring+0x480);
    const auto slot=field<int>(ring+0x7b0);
    {
        std::lock_guard lock(particlePairMutex);
        for(auto& s:particleRings)if(s.pair.world==world) {
            // Freeze the choice for the entire pair, including a live toggle.
            if(!left && particleActive() && s.pair.rightMatches(world,ring,pool,token,frame,ringFrame,slot)) {
                const auto previous=previousParticleSlot(slot);
                const auto vertices=ring+0x488+previous*0x70;
                const auto auxiliary=ring+0x648+previous*0x70;
                const auto mapped=field<uintptr_t>(vertices+0x50);
                const auto offset=field<int>(vertices+8);
                if(!mapped || offset<0)break; // native advance, no partial change
                // Native frame-middle closes the previous descriptors. Reopen
                // exactly those descriptors; current fills/atlas remain intact.
                *reinterpret_cast<uint8_t*>(vertices+0x16)|=1;
                *reinterpret_cast<uint8_t*>(auxiliary+0x16)|=1;
                *reinterpret_cast<uintptr_t*>(ring+0x7a8)=mapped+offset;
                s.pass=frame;s.reused=true;
                particlePrepare.enabled=true;
                r.rip=base+build::rva(0x83e26a);
                ++particleKeeps;
                break;
            }
            s.reused=false;
            break;
        }
    }
    if(r.rip!=base+build::rva(0x83e26a))++particleAdvances[left?0:1];
    particleReport();
}
void particlePrepareAfter(safetyhook::Context& r) {
    const auto note=particlePrepare;
    if(note.world!=r.rbx || note.ring!=field<uintptr_t>(r.rbx+0x78))return;
    std::lock_guard lock(particlePairMutex);
    auto* entry=&particleRings[0];
    for(auto& s:particleRings) {
        if(s.pair.world==note.world){entry=&s;break;}
        if(!s.pair.world || s.pass<entry->pass)entry=&s;
    }
    if(!note.left && entry->reused && entry->pass==note.frame && entry->pair.world==note.world)return;
    *entry={{note.world,note.ring,note.pool,note.token,note.frame,
        field<unsigned>(note.ring+0x480),field<int>(note.ring+0x7b0),note.left,note.enabled},note.frame,false};
}
void particlePoolReset(safetyhook::Context& r) {
    if(particleActive() && particleReusedWorld(r.rbx,field<uintptr_t>(r.rbx+0x78),
        field<unsigned>(base+build::rva(0x4a71950)))) {
        r.rip=base+build::rva(0x83e3bb);++particlePools;
    }
}
void particleLightBind(safetyhook::Context& r) {
    if(particleReusedModel(r.rcx,field<uintptr_t>(r.r15),0x5f8)) {
        r.rip=base+build::rva(0x8a1f3d);++particleLights;
    }
}
void particleBindStart(safetyhook::Context& r) {
    const auto ring=field<uintptr_t>(r.r8);
    particleBind={r.rcx,field<uintptr_t>(r.rcx+0x108),ring,
        field<unsigned>(base+build::rva(0x4a71950)),particleReusedModel(r.rcx,ring,0x5f8)};
}
bool particleBinding(uintptr_t model) {
    return particleBind.reused && particleBind.model==model &&
        particleBind.frame==field<unsigned>(base+build::rva(0x4a71950));
}
void particleBindClear(safetyhook::Context& r) {
    if(particleBinding(r.r14))r.rip=base+build::rva(0x8a242c);
}
void particleBindCpu(safetyhook::Context& r) {
    if(particleBinding(r.r14)){r.rip=base+build::rva(0x8a275e);++particleCpuBindings;}
}
void particleBindGpu(safetyhook::Context& r) {
    if(particleBinding(r.r14))++particleGpuBindings; // native draw/atlas append runs
    if(!particleActive())return;
    char asset[160]{};
    if(!read(field<uintptr_t>(r.r14+0x90),asset,sizeof(asset)-1))return;
    const bool muzzle=hands::muzzleParticleAsset(asset);
    const bool torch=std::string_view(asset)=="props/torch/particle_prop_torch_01.prt";
    const bool candle=candleEmitterAsset(asset);
    if(!muzzle&&!torch&&!candle&&std::string_view(asset)!="props/lighter/particle_prop_lighter_01.prt")return;
    const auto runtime=field<uintptr_t>(r.r14+0x650),manager=field<uintptr_t>(runtime+8);
    const int stage=int(r.r9),count=field<int>(runtime+0x18);
    if(!runtime||!manager||stage<0||stage>=count||count>64)return;
    const auto entry=field<uintptr_t>(runtime+0x10)+stage*0x38;
    const int parity=field<int>(manager+0x18078);
    if(parity<0||parity>1)return;
    static std::atomic<unsigned> rows[4]{};
    if(rows[muzzle?1:torch?2:candle?3:0].fetch_add(1)<1200)
        circlevr::vkcore::logf("motor-%s-gpu-binding: frame %u eye %u model %p runtime %p manager %p stage %d instance %d parity %d descriptor %p drawPayload %p",muzzle?"muzzle":torch?"torch":candle?"candle":"lighter",field<unsigned>(base+build::rva(0x4a71950)),unsigned(circlevr::vkcore::seqChainEye()),(void*)r.r14,(void*)runtime,(void*)manager,stage,field<int>(entry+4),parity,(void*)(manager+0x180a0+parity*0x78),(void*)(field<uintptr_t>(runtime+0x728)+stage*0x218));
}
bool particleHasGpu(uintptr_t model) {
    const auto records=field<uintptr_t>(model+0x600);const auto count=field<int>(model+0x608);
    if(count<0 || count>4096 || (count && !records))return true; // unknown: retain generation
    for(int i=0;i<count;++i)if(field<uintptr_t>(records+i*0x50+8))return true;
    const auto runtime=field<uintptr_t>(model+0x650);
    if(!runtime)return false;
    const auto entries=field<uintptr_t>(runtime+0x10);const auto n=field<int>(runtime+0x18);
    if(n<0 || n>4096 || (n && !entries))return true;
    for(int i=0;i<n;++i)if(field<int>(entries+i*0x38+4)!=-1)return true;
    return false;
}
void particleGenerate(safetyhook::Context& r) {
    const auto ring=field<uintptr_t>(r.r8);
    const bool reuse=particleReusedModel(r.rcx,ring,0x5f8);
    const auto action=particleGeneration(reuse,reuse,reuse && particleHasGpu(r.rcx));
    if(action==ParticleGeneration::ReuseCpu) {
        r.rip=base+build::rva(0x8a1f56);++particleGenerationKeeps;
    } else ++particleGenerations[circlevr::vkcore::seqChainEye()==circlevr::stereo_seq::Eye::Right?1:0];
    // Bounded identity evidence from the actual native particle path, distinct
    // from the NPC mesh audit. No per-frame vertex/image readbacks.
    static std::atomic<unsigned> samples{};
    if(particleActive() && reuse && samples.fetch_add(1)<32) {
        char asset[160]{},instance[160]{};
        read(field<uintptr_t>(r.rcx+0x90),asset,sizeof(asset)-1);
        read(field<uintptr_t>(r.rcx+0x98),instance,sizeof(instance)-1);
        circlevr::vkcore::logf("motor-particle-owner: model %p asset %s instance %s world %p ring %p ringFrame %u stamp %u slot %d records %d action %s",
            reinterpret_cast<void*>(r.rcx),asset,instance,reinterpret_cast<void*>(field<uintptr_t>(r.rcx+0x108)),
            reinterpret_cast<void*>(ring),field<unsigned>(ring+0x480),field<unsigned>(r.rcx+0x5f8),
            field<int>(ring+0x7b0),field<int>(r.rcx+0x608),
            action==ParticleGeneration::ReuseCpu?"reuse-CPU":"native-GPU-bind-and-generate");
    }
    particleBind={};
}
void effectGenerate(safetyhook::Context& r) {
    const auto ring=field<uintptr_t>(field<uintptr_t>(r.rbp+0x18));
    if(particleReusedModel(r.r13,ring,0x44d8)) {
        r.rip=base+build::rva(0x89f21c);++effectGenerationKeeps;
    }
}
