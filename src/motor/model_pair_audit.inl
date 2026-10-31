// Included inside sequential.cpp's private namespace. Motor 1.0.17.0 only.
// Player-scoped current-pair placement and native matrix/upload diagnostics.
struct PlayerModelAudit {
    uintptr_t model{},committed{},manager{};unsigned index{};uint64_t owner{};
    char name[160]{};bool carried{},attachedEffect{},muzzleFlash{},rigidFlame{},rigidTorch{},rigidCandle{};uintptr_t attachedParent{};
};
std::mutex modelAuditMutex;
std::array<PlayerModelAudit,32> modelAuditEntries{};
std::array<ModelPairPlacement,32> playerPlacements{};
unsigned modelAuditNext{};
std::array<PlayerModelAudit,24> traversalVisibilityEntries{};
unsigned traversalVisibilityNext{};
struct NativeAttachmentBinding {uintptr_t parent{},child{},parentCommitted{},childCommitted{};uint64_t owner{},tick{};};
std::array<NativeAttachmentBinding,8> nativeAttachmentBindings{};
unsigned nativeAttachmentNext{};
// Visibility registry is separate. Equipped first-person weapons also enter
// the placement registry and retain their native grip pose through a rigid delta.
std::array<PlayerModelAudit,8> bodyEquipmentEntries{};
unsigned bodyEquipmentNext{};
bool cockpitView(){
    const auto n=locomotion::latestNative();const auto now=GetTickCount64();
    return cockpitPlayerHidden(adapterInstalled,stopped,inGameplay,inCinema,
        n.valid&&now>=n.tick&&now-n.tick<=100,n.animated,n.localSpace,
        n.movementType,n.explicitReason,n.movementMode,hands::weaponSlot(n.entity));
}
// Some scripted models clear firstPerson. Require the player instance namespace
// as well as a body asset, then retain the normal manager/allocation checks.
bool cockpitBodyModel(uintptr_t model,PlayerModelAudit& item){
    if(!model||!cockpitView())return false;
    char asset[160]{},instance[192]{};
    if(!read(field<uintptr_t>(model+0x90),asset,sizeof(asset)-1)||
       !read(field<uintptr_t>(model+0x98),instance,sizeof(instance)-1)||
       !cockpitPlayerInstance(asset,instance))return false;
    const auto n=locomotion::latestNative();std::array<float,3> p{};
    if(!read(model+0x120,p.data(),sizeof(p)))return false;
    const float dx=p[0]-n.position.x,dy=p[1]-n.position.y,dz=p[2]-n.position.z;
    if(!std::isfinite(dx+dy+dz)||dx*dx+dy*dy+dz*dz>16.f)return false;
    item={};std::memcpy(item.name,asset,sizeof(asset));
    item.model=model;item.committed=field<uintptr_t>(model+0x88);
    item.manager=field<uintptr_t>(item.committed+0x28);
    const int index=field<int>(item.committed+0x30);
    if(!item.committed||!item.manager||index<0||index>=1<<20)return false;
    item.index=unsigned(index);item.owner=n.entity;return true;
}
bool bodyEquipmentModel(uintptr_t model,PlayerModelAudit& result){
    if(!adapterInstalled||stopped||!inGameplay||inCinema||!model)return false;
    if(!(field<unsigned char>(model+0x183)&2))return false;
    const auto native=locomotion::latestNative();
    if(!native.valid||GetTickCount64()-native.tick>100)return false;
    result={};
    if(!read(field<uintptr_t>(model+0x90),result.name,sizeof(result.name)-1)||
        bodyEquipmentAsset(result.name)==BodyEquipment::None)return false;
    std::array<float,3> p{};
    if(!read(model+0x120,p.data(),sizeof(p)))return false;
    const float dx=p[0]-native.position.x,dy=p[1]-native.position.y,dz=p[2]-native.position.z;
    if(!std::isfinite(dx+dy+dz)||dx*dx+dy*dy+dz*dz>4.f)return false;
    result.model=model;result.committed=field<uintptr_t>(model+0x88);
    result.manager=field<uintptr_t>(result.committed+0x28);
    const int index=field<int>(result.committed+0x30);
    if(!result.committed||!result.manager||index<0||index>=1<<20)return false;
    result.index=unsigned(index);result.owner=native.entity;return true;
}
void rememberBodyEquipment(uintptr_t model,uintptr_t committed){
    PlayerModelAudit item;
    if(!bodyEquipmentModel(model,item)||item.committed!=committed)return;
    std::lock_guard lock(modelAuditMutex);
    auto* entry=&bodyEquipmentEntries[bodyEquipmentNext++%bodyEquipmentEntries.size()];
    for(auto& e:bodyEquipmentEntries)if(e.model==model){entry=&e;break;}
    if(entry->model!=model)circlevr::vkcore::logf("motor-body-equipment: registered %s model %p index %u owner %llu",item.name,(void*)model,item.index,item.owner);
    *entry=item;
}
bool playerModel(uintptr_t model,PlayerModelAudit& result,bool knownLedgeModel=false){
    if(!adapterInstalled||stopped||!inGameplay||inCinema||!model)return false;
    // firstPerson is idRenderModelParms+63 bit1 (getter 2046C60).
    if(!(field<unsigned char>(model+0x183)&2)&&!knownLedgeModel)return false;
    const auto native=locomotion::latestNative();
    if(!native.valid||GetTickCount64()-native.tick>100)return false;
    std::array<float,3> p{};
    if(!read(model+0x120,p.data(),sizeof(p)))return false;
    const float dx=p[0]-native.position.x,dy=p[1]-native.position.y,dz=p[2]-native.position.z;
    if(!std::isfinite(dx+dy+dz))return false;
    result={};
    if(!read(field<uintptr_t>(model+0x90),result.name,sizeof(result.name)-1))return false;
    const bool player=std::strstr(result.name,"indiana_")||std::strstr(result.name,"indiana_jones/");
    // Shared first-person equipment path, independent of individual item slots.
    // Empty/pending pickup states and known holster equipment remain excluded.
    // Require the native first-person flag and a concrete model asset; world
    // objects, GUI and other actors cannot enter through name/proximity alone.
    const std::string_view asset=result.name;
    result.carried=!player&&handAttachmentAsset(asset,hands::weaponSlot(native.entity),
        (field<unsigned char>(model+0x183)&2)!=0);
    if(!player&&!result.carried)return false;
    if(result.carried&&!hands::carriedModelOriginInRange({dx,dy,dz}))return false;
    if(result.carried&&asset=="models/gameplay/interactables/grip/onehand/cylinder_small/magnifying_glass_01a/magnifying_glass_01a.idasset")
        interaction::observeMagnifier(native.entity);
    if(player&&dx*dx+dy*dy+dz*dz>1.f)return false;
    result.model=model;result.committed=field<uintptr_t>(model+0x88);
    result.manager=field<uintptr_t>(result.committed+0x28);
    const int index=field<int>(result.committed+0x30);
    if(!result.committed||!result.manager||index<0||index>=1<<20)return false;
    result.index=unsigned(index);result.owner=native.entity;return true;
}
void rememberPlayerModel(const PlayerModelAudit& item){
    for(auto& e:modelAuditEntries)if(e.model==item.model){e=item;return;}
    const auto index=modelAuditNext++%modelAuditEntries.size();
    modelAuditEntries[index]=item;playerPlacements[index]={};
    if(item.carried)circlevr::vkcore::logf("motor-carried-model: registered %s model %p owner %llx",item.name,(void*)item.model,item.owner);
}
bool traversalBodyModel(uintptr_t model,PlayerModelAudit& item){
    char asset[160]{},instance[192]{};
    if(!model||!read(field<uintptr_t>(model+0x90),asset,sizeof(asset)-1)||!handsOnlyBodyAsset(asset)||
        !read(field<uintptr_t>(model+0x98),instance,sizeof(instance)-1)||!traversalBodyInstance(asset,instance))return false;
    std::array<float,12> camera{};
    PlayerModelAudit current{};
    if(!ledgeCamera(camera)||!playerModel(model,current,true)||std::strcmp(asset,current.name))return false;
    item=current;return true;
}
void observeWhipRenderBinding(safetyhook::Context& r){
    // idWhipMeshUpdateJob, before it derives local rope vertices from the
    // handle render model. R15=rope, [RBP-48]->model=handle (both stores).
    const auto parent=field<uintptr_t>(field<uintptr_t>(r.rbp-0x48));
    const auto child=r.r15;
    const auto native=locomotion::latestNative();
    if(!native.valid||native.movementType!=0||native.animated||native.localSpace)return;
    PlayerModelAudit item;
    if(!child||child==parent||!playerModel(parent,item)||!item.carried||
        bodyEquipmentAsset(item.name)!=BodyEquipment::Whip)return;
    char name[16]{};
    if(!read(field<uintptr_t>(child+0x90),name,sizeof(name)-1)||std::strcmp(name,"_whip"))return;
    const auto committed=field<uintptr_t>(child+0x88);if(!committed)return;
    std::lock_guard lock(modelAuditMutex);
    rememberPlayerModel(item);
    auto* entry=&nativeAttachmentBindings[nativeAttachmentNext++%nativeAttachmentBindings.size()];
    for(auto& e:nativeAttachmentBindings)if(e.child==child){entry=&e;break;}
    if(entry->child!=child||entry->parent!=parent)
        circlevr::vkcore::logf("motor-held-native-binding: whip handle %p -> rope %p owner %llx",(void*)parent,(void*)child,item.owner);
    *entry={parent,child,item.committed,committed,item.owner,GetTickCount64()};
}
// Called with modelAuditMutex held. Only accept children of a currently valid
// local first-person attachment, with the exact native unique-instance name.
bool attachedEffectModel(uintptr_t model,PlayerModelAudit& result){
    if(!adapterInstalled||stopped||!inGameplay||inCinema||!model)return false;
    char asset[160]{},name[256]{};
    if(!read(field<uintptr_t>(model+0x90),asset,sizeof(asset)-1))return false;
    // Reject actors, clothes and GUI even if a malformed reference is present.
    const std::string_view childAsset=asset;
    if(childAsset.empty()||childAsset.starts_with("indiana_")||childAsset.starts_with("models/wear/")||
        childAsset.starts_with("md6def/characters/")||childAsset.starts_with("models/characters/"))return false;
    if(!read(field<uintptr_t>(model+0x98),name,sizeof(name)-1))return false;
    // Live revolver/SMG/pistol muzzle particles are unnamed, unparented and
    // lack firstPerson. Associate only with a fresh local shot at the same
    // origin/forward axis; never move arbitrary nearby effects or impacts.
    if(hands::muzzleParticleAsset(childAsset)){
        std::array<float,15> placement{};
        const auto native=locomotion::latestNative();
        if(!read(model+0x120,placement.data(),sizeof(placement)))return false;
        auto rotation=hands::rotationBasis({placement[3],placement[4],placement[5]},{placement[6],placement[7],placement[8]},{placement[9],placement[10],placement[11]});
        if(!rotation||!hands::localMuzzleFlash(native.entity,{{placement[0],placement[1],placement[2]},*rotation}))return false;
        result={};result.model=model;result.committed=field<uintptr_t>(model+0x88);
        result.manager=field<uintptr_t>(result.committed+0x28);const int index=field<int>(result.committed+0x30);
        if(!result.committed||!result.manager||index<0||index>=1<<20)return false;
        result.index=unsigned(index);result.owner=native.entity;result.carried=true;result.attachedEffect=true;result.muzzleFlash=true;
        std::memcpy(result.name,asset,sizeof(asset));return true;
    }
    const auto reference=field<uintptr_t>(model+0x1c8); // idRenderModelParms::referenceModel
    // Native lighter flames are named <complete held instance>-flame and
    // have no referenceModel/firstPerson flag. Preserve exact-parent checks below.
    if(!reference&&childAsset!="_whip"&&!namedHeldEffectCandidate(name)&&!candleWaxAsset(childAsset))return false;
    if(childAsset=="_whip"){
        const auto native=locomotion::latestNative();
        if(!native.valid||native.movementType!=0||native.animated||native.localSpace)return false;
    }
    const auto now=GetTickCount64();
    HeldEffectParentChoice choice;
    PlayerModelAudit matched{};
    bool restoredMatch=false;
    for(const auto& parent:modelAuditEntries){
        if(!parent.carried||parent.attachedEffect||!parent.model||parent.model==model)continue;
        PlayerModelAudit fresh;
        if(!playerModel(parent.model,fresh)||!fresh.carried||fresh.owner!=parent.owner||
            fresh.committed!=parent.committed||fresh.manager!=parent.manager||fresh.index!=parent.index)continue;
        bool bound=reference==parent.model;
        for(const auto& b:nativeAttachmentBindings)if(b.parent==parent.model&&b.child==model&&
            b.parentCommitted==fresh.committed&&b.childCommitted==field<uintptr_t>(model+0x88)&&
            b.owner==fresh.owner&&now-b.tick<=100)bound=true;
        char parentName[256]{};
        const bool parentNamed=read(field<uintptr_t>(parent.model+0x98),parentName,sizeof(parentName)-1);
        const bool direct=bound||(parentNamed&&!exactHeldEffectName(parentName,name).empty());
        bound=direct||(parentNamed&&namedHeldEffect(parentName,name));
        const bool rigidFlame=rigidLighterFlame(fresh.name,asset,parentName,name);
        const bool rigidTorch=heldTorchEffect(fresh.name,parentName,asset,name);
        const bool wax=parentNamed&&heldCandleWax(fresh.name,parentName,asset,name);
        // Scene-specific identity, then the same fresh held-owner, allocation,
        // world and ambiguity checks as other attachments. Never proximity only.
        bound=bound||wax;
        const bool rigidCandle=heldCandleEffect(fresh.name,parentName,asset,name)||wax;
        if(!bound||field<uintptr_t>(model+0x108)!=field<uintptr_t>(parent.model+0x108))continue;
        std::array<float,3> p{},q{};
        if(!read(model+0x120,p.data(),sizeof(p))||!read(parent.model+0x120,q.data(),sizeof(q)))continue;
        const float dx=p[0]-q[0],dy=p[1]-q[1],dz=p[2]-q[2];
        // Exact owned lighter identity remains valid when its emitter pose freezes.
        if(!std::isfinite(dx+dy+dz)||(!(rigidFlame||rigidTorch||rigidCandle)&&dx*dx+dy*dy+dz*dz>4.f))continue;
        PlayerModelAudit candidate{};candidate.model=model;candidate.committed=field<uintptr_t>(model+0x88);
        candidate.manager=field<uintptr_t>(candidate.committed+0x28);
        const int index=field<int>(candidate.committed+0x30);
        if(!candidate.committed||!candidate.manager||index<0||index>=1<<20)continue;
        candidate.index=unsigned(index);candidate.owner=fresh.owner;candidate.carried=true;candidate.attachedEffect=true;
        candidate.attachedParent=parent.model;candidate.rigidFlame=rigidFlame;candidate.rigidTorch=rigidTorch;
        candidate.rigidCandle=rigidCandle;
        std::memcpy(candidate.name,asset,sizeof(asset));
        if(choice.observe(parent.model,direct)){matched=candidate;restoredMatch=!direct;}
    }
    if(!choice.valid())return false;
    result=matched;
    if(restoredMatch){
        static uint64_t last{};
        if(now-last>=1000){last=now;circlevr::vkcore::logf("motor-held-effect-restored-binding: child %p parent %p asset %s instance %s owner %llx",(void*)model,(void*)matched.attachedParent,asset,name,matched.owner);}
    }
    return true;
}
// Accessed only under modelAuditMutex. Holster gaps and allocation/owner changes
// invalidate calibration; world torches never pass the held-parent identity gate.
struct TorchLocalBinding {uintptr_t child{},committed{},parent{},parentCommitted{};uint64_t owner{},seen{};HeldFlameEmitterLocal pose{};};
std::array<TorchLocalBinding,16> torchLocalBindings{};unsigned torchLocalNext{},candleLocalNext{};
bool heldFlameLocalPlacement(const PlayerModelAudit& child,const PlayerModelAudit& parent,std::array<float,15>& local){
    if(!child.rigidTorch&&!child.rigidCandle)return false;
    if(child.rigidCandle&&candleWaxAsset(child.name)){
        // Keep animated wax vertices native; only place their model at the
        // corrected candle origin, shared by both eyes and matrix history.
        local={0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};return true;
    }
    if(child.rigidTorch&&std::string_view(child.name)!="props/torch/particle_prop_torch_01.prt"){
        // The separately authored fireblob mesh shares the torch's model origin.
        local={0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};return true;
    }
    const auto now=GetTickCount64();TorchLocalBinding* binding=nullptr;
    auto bindings=std::span(torchLocalBindings).subspan(child.rigidCandle?8:0,8);
    for(auto& b:bindings)if(b.child==child.model){binding=&b;break;}
    if(!binding)binding=&bindings[(child.rigidCandle?candleLocalNext++:torchLocalNext++)%bindings.size()];
    if(binding->child!=child.model||binding->committed!=child.committed||binding->parent!=parent.model||
       binding->parentCommitted!=parent.committed||binding->owner!=child.owner||now<binding->seen||now-binding->seen>250)
        *binding={child.model,child.committed,parent.model,parent.committed,child.owner,now,{}};
    binding->seen=now;
    if(!binding->pose.ready){
        std::array<float,15> p{},c{},verifyP{},verifyC{};
        if(!read(parent.model+0x120,p.data(),sizeof(p))||!read(child.model+0x120,c.data(),sizeof(c))||
           !read(parent.model+0x120,verifyP.data(),sizeof(verifyP))||!read(child.model+0x120,verifyC.data(),sizeof(verifyC))||
           p!=verifyP||c!=verifyC||!binding->pose.observe(field<uint64_t>(base+build::rva(0x4c523a8)),p,c))return false;
        circlevr::vkcore::logf("motor-%s-local-anchor: parent %p emitter %p local %.6f %.6f %.6f",child.rigidCandle?"candle":"torch",(void*)parent.model,(void*)child.model,binding->pose.local[0],binding->pose.local[1],binding->pose.local[2]);
    }
    local=binding->pose.local;return true;
}
bool heldParticlePlacement(const PlayerModelAudit& child,const PlayerModelAudit& parent,
    const std::array<float,16>& matrix,const std::array<float,3>& scale,std::array<float,15>& output){
    if(child.rigidFlame)return lighterParticlePlacement(matrix,scale,output);
    std::array<float,15> local{};
    return heldFlameLocalPlacement(child,parent,local)&&rigidParticlePlacement(matrix,local,scale,output);
}
bool attachedRenderPlacement(const PlayerModelAudit& child,uint64_t token,std::array<float,15>& p){
    if(child.muzzleFlash){
        std::array<float,15> spawn{};
        if(!read(child.model+0x120,spawn.data(),sizeof(spawn))||
           !hands::renderMuzzleFlash(child.owner,token,spawn))return false;
        p=spawn;return true;
    }
    // Verified native children receive the same rigid render-frame delta as
    // their parent. No camera-relative calibration or inferred gear parent.
    PlayerModelAudit parent;
    if(!playerModel(child.attachedParent,parent)||parent.owner!=child.owner||!parent.carried)return false;
    std::array<float,15> native{};
    if(!read(field<uintptr_t>(parent.manager+0x108)+parent.index*60ull,native.data(),sizeof(native)))return false;
    auto corrected=native;
    if(!hands::renderModelPlacement(parent.owner,token,corrected,true))return false;
    if(child.rigidTorch||child.rigidCandle){
        std::array<float,15> local{};
        if(!heldFlameLocalPlacement(child,parent,local))return false;
        constexpr std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
        for(unsigned i=12;i<15;++i)local[i]=p[i];
        if(!rebaseAttachedPlacement(identity,corrected,local))return false;
        p=local;return true;
    }
    if(child.rigidFlame){
        const auto before=p;
        if(!placeRigidLighterFlame(corrected,p))return false;
        static uint64_t last{};const auto now=GetTickCount64();
        if(now-last>=1000){last=now;circlevr::vkcore::logf("motor-lighter-rigid-anchor: token %llu parent %p flame %p correction %.5f %.5f %.5f",token,(void*)parent.model,(void*)child.model,p[0]-before[0],p[1]-before[1],p[2]-before[2]);}
        return true;
    }
    // Other bound effects retain the shared rigid delta.
    return rebaseAttachedPlacement(native,corrected,p);
}
bool handsOnlyEnabled(){
    static const bool enabled=[](){char v[8]{};
        const bool on=GetEnvironmentVariableA("CIRCLE_HANDS_ONLY",v,8)==1&&v[0]=='1';
        circlevr::vkcore::logf("motor-hands-only: %s; private GPU view-ID uploads; native skeleton retained",on?"enabled":"disabled");return on;}();
    return enabled;
}
// GeomCache builds a private compute-job transform, bypassing the object's
// matrix table. Patch that job after native construction, not the simulation.
// The paired inverse must match: the cache uses both during deformation.
void heldWaxComputeTransform(safetyhook::Context& r){
    if(!adapterInstalled||stopped||!inGameplay||inCinema||!r.r14)return;
    const auto eye=circlevr::vkcore::seqChainEye();
    if(eye!=circlevr::stereo_seq::Eye::Left&&eye!=circlevr::stereo_seq::Eye::Right)return;
    const auto token=field<uint64_t>(base+build::rva(0x4c523a8));
    const auto frame=field<unsigned>(base+build::rva(0x4a71950));
    std::lock_guard lock(modelAuditMutex);
    PlayerModelAudit child,parent;
    if(!attachedEffectModel(r.rbx,child)||!candleWaxAsset(child.name)||
       !playerModel(child.attachedParent,parent)||parent.owner!=child.owner||parent.manager!=child.manager)return;
    std::array<float,15> pose{};
    if(!read(parent.model+0x120,pose.data(),sizeof(pose))||
       !hands::renderModelPlacement(parent.owner,token,pose,true))return;
    // As for the flame, prefer the parent's already completed render placement
    // when available in this stereo pair; otherwise use the fresh hand anchor.
    bool currentParent=false;
    for(const auto& p:playerPlacements)
        if(p.model==parent.model&&p.committed==parent.committed&&p.manager==parent.manager&&
           p.owner==parent.owner&&p.index==parent.index&&p.token==token&&
           (p.frame==frame||frame-p.frame==1))currentParent=true;
    std::array<float,16> forward{},inverse{};
    if(currentParent){
        std::array<float,16> matrix{},verify{};
        const auto address=field<uintptr_t>(parent.manager+0xc0)+parent.index*64ull;
        if(!read(address,matrix.data(),64)||!read(address,verify.data(),64)||matrix!=verify||
           !rigidParticlePlacement(matrix,{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1},{1,1,1},pose))return;
    }
    if(!heldWaxMatrices(pose,forward,inverse))return;
    // R14+10 and +50 are the compute job's forward/inverse matrices. All
    // animation indices, interpolation weights and vertex buffers stay native.
    std::memcpy(reinterpret_cast<void*>(r.r14+0x10),forward.data(),64);
    std::memcpy(reinterpret_cast<void*>(r.r14+0x50),inverse.data(),64);
    static std::atomic<unsigned> counts[2]{};
    const unsigned e=eye==circlevr::stereo_seq::Eye::Right?1:0,n=++counts[e];
    if(n<=6||n%600==0)circlevr::vkcore::logf(
        "motor-candle-wax-compute: eye %u token %llu model %p job %p parentCurrent %u position %.5f %.5f %.5f count %u",
        e,token,(void*)r.rbx,(void*)r.r14,unsigned(currentParent),pose[0],pose[1],pose[2],n);
}
// Refresh at dispatch, immediately before native code copies this job into
// the mapped compute inputs. Early commit can precede the candle's final pose.
void heldWaxDispatchTransform(safetyhook::Context& r){
    if(!adapterInstalled||stopped||!inGameplay||inCinema||!r.r12)return;
    const auto model=field<uintptr_t>(r.r12);
    char asset[160]{};
    if(!model||!read(field<uintptr_t>(model+0x90),asset,sizeof(asset)-1)||!candleWaxAsset(asset))return;
    // This job captured its backend frame at entry, before the per-record loop.
    // seqChainEye/global counters may already refer to a different frontend job.
    const auto frame=field<unsigned>(r.rbp-4);
    const auto tag=circlevr::vkcore::seqTagForBackendFrame(frame+1);
    if(!tag||!circlevr::stereo_seq::drawsEyeView(*tag))return;
    std::lock_guard lock(modelAuditMutex);
    PlayerModelAudit child,parent;
    if(!attachedEffectModel(model,child)||!playerModel(child.attachedParent,parent)||
       parent.owner!=child.owner||parent.manager!=child.manager)return;
    bool current=false;
    for(const auto& p:playerPlacements)
        if(p.model==parent.model&&p.committed==parent.committed&&p.manager==parent.manager&&
           p.owner==parent.owner&&p.index==parent.index&&p.token==tag->tick)current=true;
    if(!current)return; // Never substitute an affine from a different simulation pair.
    std::array<float,16> matrix{},verify{},forward{},inverse{},before{};
    const auto address=field<uintptr_t>(parent.manager+0xc0)+parent.index*64ull;
    std::array<float,15> pose{};
    if(!read(address,matrix.data(),64)||!read(address,verify.data(),64)||matrix!=verify||
       !rigidParticlePlacement(matrix,{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1},{1,1,1},pose)||
       !heldWaxMatrices(pose,forward,inverse))return;
    read(r.r12+0x10,before.data(),64);
    std::memcpy(reinterpret_cast<void*>(r.r12+0x10),forward.data(),64);
    std::memcpy(reinterpret_cast<void*>(r.r12+0x50),inverse.data(),64);
    static std::atomic<unsigned> counts[2]{};
    const auto eye=circlevr::stereo_seq::eyeIndex(tag->eye);const auto n=++counts[eye];
    if(n<=6||n%300==0)circlevr::vkcore::logf(
        "motor-candle-wax-dispatch: eye %d frame %u token %llu correction %.5f %.5f %.5f count %u",
        eye,frame,tag->tick,forward[3]-before[3],forward[7]-before[7],forward[11]-before[11],n);
}
void playerBodyGpuVisibility(safetyhook::Context& r){
    std::array<float,12> ledge{};const bool climbing=ledgeCamera(ledge);
    const bool cockpit=cockpitView();
    const bool handsOnly=handsOnlyEnabled()||climbing;
    // CDA8A0 has completed CD1EA0's native sparse uploads. RDI still owns the
    // manager. Clothing (type11) bypasses C4C9D0's CPU-visible model list;
    // override its GPU culling records, using the exact native ring and copier.
    const auto manager=r.rdi;
    const auto bundle=field<uintptr_t>(manager+0x45fa0);
    const auto records=field<uintptr_t>(manager+0x138);
    const auto slot=field<unsigned>(bundle+0xe00);
    if(!bundle||!records||slot>1)return; // native two-buffer layout: stride 70h
    const auto destination=bundle+0xa80+slot*0x70;
    const auto capacity=field<unsigned>(destination);
    const auto native=locomotion::latestNative();
    const auto view=projectedNativeView.load();
    const auto viewId=field<unsigned>(view+0x19008);
    const bool hide=(cockpit||climbing||handsOnlyContext(true,adapterInstalled,stopped,inGameplay,inCinema,
        native.valid&&GetTickCount64()-native.tick<=100,native.animated,native.localSpace,
        native.movementType,native.explicitReason,native.movementMode))&&
        view&&field<int>(view+0x18f34)==0&&(viewId>0||climbing||cockpit)&&viewId<0xffff;
    struct Copy {const void* source;int offset;int size;};
    static_assert(sizeof(Copy)==16);
    std::array<HandsOnlyViewRecord,64> copies{};
    std::array<Copy,64> transfers{};
    unsigned count=0,hidden=0,equipmentHidden=0;
    const auto weaponSlot=hands::weaponSlot(native.entity);
    {
        std::lock_guard lock(modelAuditMutex);
        const auto append=[&](const PlayerModelAudit& m,bool equipment){
            if(count>=copies.size()||!m.model||m.manager!=manager||(!equipment&&!cockpitPlayerAsset(m.name))||m.index>=1u<<20||
                (uint64_t(m.index)+1)*48>capacity)return;
            // Revalidate identity even on exit from gameplay. playerModel() is
            // deliberately gameplay-only and cannot be used to restore cinema.
            if(field<uintptr_t>(m.model+0x88)!=m.committed||
                field<uintptr_t>(m.committed+0x28)!=manager||
                field<unsigned>(m.committed+0x30)!=m.index)return;
            char name[160]{};
            if(!read(field<uintptr_t>(m.model+0x90),name,sizeof(name)-1)||std::strcmp(name,m.name))return;
            HandsOnlyViewRecord original{};
            if(!read(records+m.index*48ull,original.data(),original.size()))return;
            PlayerModelAudit current;
            const bool wanted=(cockpit&&cockpitViewAssetHidden(m.name))||playerViewAssetHidden(m.name,handsOnly,weaponSlot);
            // Ledge animation clears firstPerson on the existing local models.
            // Identity/name/manager/index were revalidated above; only accept
            // this exception for the same previously registered player owner.
            const bool valid=equipment?bodyEquipmentModel(m.model,current):
                ((cockpit&&cockpitBodyModel(m.model,current))||playerModel(m.model,current,climbing&&m.owner==native.entity));
            const bool suppress=hide&&wanted&&m.owner==native.entity&&valid&&
                current.committed==m.committed&&current.index==m.index&&current.manager==manager;
            copies[count]=handsOnlyViewRecord(original,suppress,uint16_t(viewId),climbing||cockpit);
            transfers[count]={copies[count].data(),int(m.index*48),48};
            ++count;hidden+=suppress;equipmentHidden+=equipment&&suppress;
        };
        for(const auto& m:modelAuditEntries)append(m,false);
        for(const auto& m:traversalVisibilityEntries){
            bool present=false;for(const auto& p:modelAuditEntries)present|=p.model==m.model;
            if(!present)append(m,false);
        }
        for(const auto& m:bodyEquipmentEntries)append(m,true);
    }
    if(!count)return;
    // D9F920 copies each source into mapped/staging storage before returning
    // (D9F998 / D9FB54), so these private records have sufficient lifetime.
    using SparseUpload=void(*)(void*,const Copy*,int,bool);
    reinterpret_cast<SparseUpload>(base+build::rva(0xd9f920))(
        reinterpret_cast<void*>(destination),transfers.data(),int(count),true);
    static std::atomic<unsigned> counts[2]{};const auto n=++counts[hidden?1:0];
    if(n<=8||n%1800==0)circlevr::vkcore::logf(
        "motor-hands-only-gpu: slot %u view %u records %u hidden %u equipment %u weapon %u handsOnly %d cockpit %d manager %p count %u; CPU originals retained",
        slot,viewId,count,hidden,equipmentHidden,weaponSlot,handsOnly,cockpit,(void*)manager,n);
}
void playerHandHairVisibility(safetyhook::Context& r){
    // idRenderModelHair vtable[24] (89A400): the native r_skipHair branch,
    // before per-view hair data/particle submission. Its normal exit at 89A626
    // clears model+4A0 to -1 and runs the original epilogue. Only separate beard
    // assets use the optional workaround; head hair and unknown assets remain.
    if(field<uintptr_t>(r.rbx)!=base+build::rva(0x3649f60))return;
    if(cockpitView()){
        PlayerModelAudit current;
        if(cockpitBodyModel(r.rbx,current)||(playerModel(r.rbx,current)&&cockpitPlayerAsset(current.name))){
            r.rflags&=~uintptr_t(0x40);
            static unsigned count{};if(++count<=6||count%1800==0)
                circlevr::vkcore::logf("motor-cockpit-hair: suppressed local %s owner %llx count %u",current.name,current.owner,count);
            return;
        }
    }
    // Hair submits through its own renderer, bypassing the surface visibility
    // upload. Retain local-player identity checks even though ledge animation
    // clears firstPerson. Never apply this to arbitrary nearby NPC hair.
    std::array<float,12> ledge{};
    if(ledgeCamera(ledge)){
        PlayerModelAudit registered{};
        {std::lock_guard lock(modelAuditMutex);
            for(const auto& m:modelAuditEntries)if(m.model==r.rbx){registered=m;break;}}
        if(!registered.model)traversalBodyModel(r.rbx,registered);
        const auto native=locomotion::latestNative();PlayerModelAudit current;
        if(registered.model&&registered.owner==native.entity&&
            playerModel(r.rbx,current,true)&&handsOnlyBodyAsset(current.name)&&
            current.committed==registered.committed&&current.manager==registered.manager&&
            current.index==registered.index&&!std::strcmp(current.name,registered.name)){
            r.rflags&=~uintptr_t(0x40);
            static unsigned count{};
            if(++count<=4||count%1800==0)circlevr::vkcore::logf("motor-ledge-hair: suppressed local %s count %u",current.name,count);
            return;
        }
    }
    static const bool disableFacial=[] {char value[8]{};return GetEnvironmentVariableA("CIRCLE_DISABLE_FACIAL_HAIR",value,8)&&value[0]=='1';}();
    if(adapterInstalled&&!stopped&&disableFacial){
        char name[160]{};read(field<uintptr_t>(r.rbx+0x90),name,sizeof(name)-1);
        const bool suppress=suppressFacialHair(adapterInstalled,stopped,disableFacial,name);
        const auto eye=circlevr::vkcore::seqChainEye();
        const unsigned index=eye==circlevr::stereo_seq::Eye::Right?1:eye==circlevr::stereo_seq::Eye::Left?0:2;
        static std::atomic<unsigned> counts[2][3]{};const auto count=++counts[suppress?1:0][index];
        if(count<=6||count%2400==0)
            circlevr::vkcore::logf("motor-facial-hair: %s eye %u asset %s count %u",suppress?"suppressed beard":"preserved (hand filter follows)",index,name,count);
        if(suppress){r.rflags&=~uintptr_t(0x40);return;}
    }
    PlayerModelAudit item;
    if(!playerModel(r.rbx,item)||!isHandHair(item.name))return;
    r.rflags&=~uintptr_t(0x40); // JNE: take the native disabled-hair path.
    const unsigned hand=std::strcmp(item.name,"indiana_hand_right.hairmodel")==0?1:0;
    const unsigned eye=circlevr::vkcore::seqChainEye()==circlevr::stereo_seq::Eye::Right?1:0;
    static std::atomic<unsigned> counts[2][2]{};
    const unsigned count=++counts[hand][eye];
    if(count<=2||count%1200==0)
        circlevr::vkcore::logf("motor-hand-hair: suppressed %s eye %u owner %llu count %u",item.name,eye,item.owner,count);
}
void savePlayerPlacement(const PlayerModelAudit& m,uint64_t token,unsigned frame,const std::array<float,15>& p){
    for(size_t i=0;i<modelAuditEntries.size();++i)if(modelAuditEntries[i].model==m.model){
        if(!token||!ModelPairPlacement::finite(p)){playerPlacements[i]={};return;}
        playerPlacements[i]={m.model,m.committed,m.manager,m.owner,token,m.index,frame,p};return;
    }
}
void playerModelPlacement(safetyhook::Context& r){
    temporalModelBefore(r);
    npcPairedPlacement(r);
    if(!adapterInstalled||stopped||!inGameplay)return;
    if(inCinema){worldModelPlacement(r);return;}
    const auto eye=circlevr::vkcore::seqChainEye();
    if(eye!=circlevr::stereo_seq::Eye::Left&&eye!=circlevr::stereo_seq::Eye::Right)return;
    const auto token=field<uint64_t>(base+build::rva(0x4c523a8));
    const auto frame=field<unsigned>(base+build::rva(0x4a71950));
    std::lock_guard lock(modelAuditMutex);
    for(size_t i=0;i<modelAuditEntries.size();++i){
        const auto& m=modelAuditEntries[i];
        if(!m.model||m.committed!=r.rdi||field<uintptr_t>(m.model+0x88)!=m.committed)continue;
        if(r.r15!=field<uintptr_t>(m.manager+0x108)+m.index*60ull)return;
        PlayerModelAudit fresh;
        if(m.carried&&(!(m.attachedEffect?attachedEffectModel(m.model,fresh):playerModel(m.model,fresh))||!fresh.carried||fresh.owner!=m.owner||fresh.committed!=m.committed))return;
        if(eye==circlevr::stereo_seq::Eye::Left){
            playerPlacements[i]={}; // Failed later reads cannot retain an earlier left pose.
            std::array<float,15> p{};
            if(read(r.r15,p.data(),sizeof(p))){
                if(m.attachedEffect?attachedRenderPlacement(fresh,token,p):hands::renderModelPlacement(m.owner,token,p,m.carried)){
                    if(m.attachedEffect){static uint64_t last{};const auto now=GetTickCount64();if(now-last>1000){last=now;
                        circlevr::vkcore::logf("motor-held-effect-anchor: applied %s token %llu model %p",m.name,token,(void*)m.model);}}
                    thread_local std::array<std::array<float,15>,32> anchored;
                    anchored[i]=p;r.r15=reinterpret_cast<uintptr_t>(anchored[i].data());
                    r.rdx=r.r15+0xc;r.r8=r.r15+0x30;
                }
                savePlayerPlacement(m,token,frame,p);
            }
        }else if(playerPlacements[i].matches(m.model,m.committed,m.manager,m.owner,m.index,token,frame)){
            // C5C920 uses R15 again for transformed bounds after constructing the
            // matrix/inverse. Redirect all inputs to one private placement, not
            // just the matrix output. Native simulation/deferred data stay intact.
            thread_local std::array<std::array<float,15>,32> threadPlacements;
            auto& pairedPlacement=threadPlacements[i];
            pairedPlacement=playerPlacements[i].placement;
            r.r15=reinterpret_cast<uintptr_t>(pairedPlacement.data());
            r.rdx=r.r15+0xc;r.r8=r.r15+0x30;
            // The displaced instructions set RCX=R15 and R9=RBX next.
            static unsigned applied{};
            if(++applied<=8||applied%600==0)circlevr::vkcore::logf("motor-player-model-pair: right placement held model %p token %llu frame %u count %u",(void*)m.model,token,frame,applied);
        }
        return;
    }
    worldModelPlacement(r); // Existing player/held-item paths above take precedence.
}
void modelAuditRow(const PlayerModelAudit& m,const char* stage,int eye,uint64_t token,unsigned frame,int status){
    if(!diagnostics::enabled())return;
    std::array<float,16> current{},previous{};std::array<float,15> placement{};
    if(!read(field<uintptr_t>(m.manager+0xc0)+m.index*64ull,current.data(),64)||
       !read(field<uintptr_t>(m.manager+0xf0)+m.index*64ull,previous.data(),64)||
       !read(field<uintptr_t>(m.manager+0x108)+m.index*60ull,placement.data(),60))return;
    static FILE* csv=[](){FILE* f=circlevr::vkcore::openLogFileShared((circlevr::vkcore::logDirectory()+L"\\motor-player-models.csv").c_str(),L"w",_SH_DENYWR);
        if(f)fputs("stage,eye,token,frame,model,committed,index,owner,status,flags,px,py,pz,m03,m13,m23,m30,m31,m32,old03,old13,old23,old30,old31,old32,name\n",f);return f;}();
    if(!csv)return;
    const auto flags=field<uint64_t>(field<uintptr_t>(m.manager+0xa8)+m.index*8ull);
    fprintf(csv,"%s,%d,%llu,%u,%llx,%llx,%u,%llu,%d,%llx,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%s\n",
        stage,eye,token,frame,m.model,m.committed,m.index,m.owner,status,flags,
        placement[0],placement[1],placement[2],current[3],current[7],current[11],current[12],current[13],current[14],
        previous[3],previous[7],previous[11],previous[12],previous[13],previous[14],m.name);
    static unsigned rows{};if(++rows%120==0)fflush(csv);
}
// r179 read-only census, separate from the sparse NPC correction registry.
// +108 is an observed shared render binding, NOT a verified actor/entity owner.
void auditCharacterIdentity(uintptr_t model,uintptr_t committed,bool knownPlayer,const PlayerModelAudit& player){
    if(!diagnostics::npcBufferActive()||!model||!committed)return;
    char asset[256]{},instance[256]{};
    if(!read(field<uintptr_t>(model+0x90),asset,sizeof(asset)-1))return;
    read(field<uintptr_t>(model+0x98),instance,sizeof(instance)-1);
    if(!std::strstr(asset,"characters/")&&!std::strstr(asset,"indiana")&&!std::string_view(instance).starts_with("npc_")&&!knownPlayer)return;
    std::array<float,3> position{};
    if(!read(model+0x120,position.data(),sizeof(position)))return;
    read(field<uintptr_t>(model+0x98),instance,sizeof(instance)-1);
    // Whitespace-safe identity tokens for the offline parser.
    for(auto& c:asset)if(c && static_cast<unsigned char>(c)<=32)c='_';
    for(auto& c:instance)if(c && static_cast<unsigned char>(c)<=32)c='_';
    const auto native=locomotion::latestNative();
    const auto now=GetTickCount64();
    const bool fresh=native.valid&&now-native.tick<=100;
    const auto flags=field<unsigned char>(model+0x183);
    const bool playerName=std::strstr(asset,"indiana")!=nullptr;
    const float dx=position[0]-native.position.x,dy=position[1]-native.position.y,dz=position[2]-native.position.z;
    static std::mutex mutex;
    struct Seen {uintptr_t model{},committed{};uint64_t tick{};};
    static std::array<Seen,256> seen{};
    std::lock_guard lock(mutex);
    Seen* slot=nullptr;
    for(auto& e:seen)if(e.model==model&&e.committed==committed){slot=&e;break;}
    if(slot&&now-slot->tick<1000)return;
    if(!slot)for(auto& e:seen)if(!e.model||now-e.tick>5000){slot=&e;break;}
    if(!slot||!diagnostics::npcBufferEvent())return;
    *slot={model,committed,now};
    circlevr::vkcore::logf("motor-buffer-identity: model %p committed %p asset %s instance %s classification %s owner %llx ownerKnown %d renderBindingCandidate %p firstPerson %d playerName %d playerEntity %llx playerFresh %d x %.5f y %.5f z %.5f playerX %.5f playerY %.5f playerZ %.5f distance %.3f frame %u eye %u",
        (void*)model,(void*)committed,asset,instance[0]?instance:"unknown",
        knownPlayer?"player_or_attachment":playerName?"player_asset":(flags&2)?"first_person":"character_candidate",
        knownPlayer?player.owner:0ull,knownPlayer?1:0,(void*)field<uintptr_t>(model+0x108),!!(flags&2),playerName,
        native.entity,fresh,position[0],position[1],position[2],native.position.x,native.position.y,native.position.z,
        fresh?std::sqrt(dx*dx+dy*dy+dz*dz):-1.f,field<unsigned>(base+build::rva(0x4a71950)),unsigned(circlevr::vkcore::seqChainEye()));
}
void playerModelStatus(safetyhook::Context& r){
    observeNpcModel(r.rbx,r.rdi);
    observeWorldModel(r.rbx,r.rdi);
    rememberBodyEquipment(r.rbx,r.rdi);
    // Before cmp cl,1: RBX=model, RDI=committed; native list has loaded both.
    const auto frame=field<unsigned>(base+build::rva(0x4a71950));
    PlayerModelAudit item;const bool ordinary=playerModel(r.rbx,item);
    PlayerModelAudit traversal{};
    const bool visibilityOnly=!ordinary&&(cockpitBodyModel(r.rbx,traversal)||traversalBodyModel(r.rbx,traversal))&&traversal.committed==r.rdi;
    auditCharacterIdentity(r.rbx,r.rdi,ordinary,item);
    std::lock_guard lock(modelAuditMutex);
    if(visibilityOnly){
        auto* entry=&traversalVisibilityEntries[traversalVisibilityNext++%traversalVisibilityEntries.size()];
        for(auto& e:traversalVisibilityEntries)if(e.model==traversal.model){entry=&e;break;}
        if(entry->model!=traversal.model||entry->owner!=traversal.owner)
            circlevr::vkcore::logf("motor-traversal-visibility: registered %s model %p owner %llx",traversal.name,(void*)traversal.model,traversal.owner);
        *entry=traversal;
    }
    PlayerModelAudit attached;
    // Ordinary native gear already has its animated world placement.
    const bool bound=!ordinary&&attachedEffectModel(r.rbx,attached);
    if(bound)item=attached;
    if((!ordinary&&!bound)||item.committed!=r.rdi)return;
    rememberPlayerModel(item);
    if(circlevr::vkcore::seqChainEye()==circlevr::stereo_seq::Eye::Left){
        std::array<float,15> p{};
        if(read(field<uintptr_t>(item.manager+0x108)+item.index*60ull,p.data(),sizeof(p)))
            savePlayerPlacement(item,field<uint64_t>(base+build::rva(0x4c523a8)),frame,p);
    }
    if(!diagnostics::enabled()||frame%120>3)return;
    modelAuditRow(item,"status",circlevr::vkcore::seqChainEye()==circlevr::stereo_seq::Eye::Right?1:0,
        field<uint64_t>(base+build::rva(0x4c523a8)),frame,int(r.rcx&255));
}
void playerModelMatrix(safetyhook::Context& r){
    temporalModelAfter();
    // C5C9EF called the native affine builder; RDI=committed, RBX=current M.
    const auto frame=field<unsigned>(base+build::rva(0x4a71950));
    if(!diagnostics::enabled()||frame%120>3)return;
    std::lock_guard lock(modelAuditMutex);
    for(const auto& m:modelAuditEntries)if(m.committed==r.rdi){
        modelAuditRow(m,"matrix",circlevr::vkcore::seqChainEye()==circlevr::stereo_seq::Eye::Right?1:0,
            field<uint64_t>(base+build::rva(0x4c523a8)),frame,-1);break;
    }
}
void playerModelUpload(uintptr_t manager,unsigned frame,int eye,uint64_t token){
    if(eye>=0&&eye<2){
        std::lock_guard lock(modelAuditMutex);
        for(const auto& m:modelAuditEntries)if(m.model&&m.manager==manager&&field<uintptr_t>(m.model+0x88)==m.committed){
            std::array<float,16> matrix{};
            if(read(field<uintptr_t>(manager+0xc0)+m.index*64ull,matrix.data(),sizeof(matrix)))
                hands::auditUploadedModel(m.owner,token,matrix,unsigned(eye));
        }
    }
    if(!diagnostics::enabled()||frame%120>3)return;
    std::lock_guard lock(modelAuditMutex);
    for(const auto& m:modelAuditEntries)if(m.manager==manager&&field<uintptr_t>(m.model+0x88)==m.committed)
        modelAuditRow(m,"upload",eye,token,frame,-1);
}
