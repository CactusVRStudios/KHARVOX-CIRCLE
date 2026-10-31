// Nearby non-player character render ownership and opt-in L/R upload audit.
// Reuses the verified affine-builder boundary; simulation poses stay native.
struct NpcPairAudit {
    uintptr_t model{},committed{},manager{};unsigned index{};
    char name[160]{},instance[256]{};
    NpcPlacementKind placementKind{NpcPlacementKind::None};
    uint64_t seen{},token{};unsigned frame{};
    int joint{};std::array<float,16> matrix{};bool left{};
    std::array<unsigned char,0x18> skinBuffers{};
    int skinSlot{-1};unsigned skinFlags{};
    std::array<float,16> previousMatrix{};bool previousValid{};
    unsigned blasRevision{};
    ModelPairPlacement placement;
    NpcSkinPair skinPair;
};
std::mutex npcAuditMutex;
std::array<NpcPairAudit,64> npcAuditEntries{};
std::unordered_map<uintptr_t,size_t> npcPlacementLookup;
// Caller holds npcAuditMutex. Match the complete native attachment prefix to
// a live, independently registered character in the same render manager.
bool registeredNpcWearOwner(std::string_view instance,uintptr_t manager){
    const auto owner=npcWearOwner(instance);if(owner.empty())return false;
    for(const auto& e:npcAuditEntries){
        if(!e.model||e.manager!=manager||e.placementKind!=NpcPlacementKind::Character||
           owner!=e.instance||GetTickCount64()-e.seen>5000)continue;
        char asset[160]{},name[256]{};
        if(field<uintptr_t>(e.model+0x88)!=e.committed||field<uintptr_t>(e.committed+0x28)!=manager||
           field<unsigned>(e.committed+0x30)!=e.index||
           !read(field<uintptr_t>(e.model+0x90),asset,sizeof(asset)-1)||
           !read(field<uintptr_t>(e.model+0x98),name,sizeof(name)-1))continue;
        if(owner==name&&!std::strcmp(asset,e.name)&&
           npcPlacementKind(asset,name,(field<unsigned char>(e.model+0x183)&2)!=0)==NpcPlacementKind::Character)return true;
    }
    return false;
}
void npcSkinSelection(safetyhook::Context& r){
    if(!adapterInstalled||stopped||!inGameplay||monoVrEnabled())return;
    const auto eye=circlevr::vkcore::seqChainEye();
    if(eye!=circlevr::stereo_seq::Eye::Left&&eye!=circlevr::stereo_seq::Eye::Right)return;
    const auto model=r.r14,committed=field<uintptr_t>(model+0x88);
    const auto token=field<uint64_t>(base+build::rva(0x4c523a8));
    const auto frame=field<unsigned>(base+build::rva(0x4a71950));
    const int slot=static_cast<int>(r.rax);
    if(slot<0||slot>1||!token)return;
    std::lock_guard lock(npcAuditMutex);
    const auto found=npcPlacementLookup.find(committed);if(found==npcPlacementLookup.end())return;
    auto& e=npcAuditEntries[found->second];
    if(e.model!=model||e.committed!=committed||GetTickCount64()-e.seen>5000||
       field<uintptr_t>(committed+0x28)!=e.manager||field<unsigned>(committed+0x30)!=e.index)return;
    const auto pose=field<uintptr_t>(model+0x490);
    constexpr uintptr_t zf=0x40;
    const bool moving=(r.rflags&zf)==0,right=eye==circlevr::stereo_seq::Eye::Right;
    const bool reuse=e.skinPair.reusable(pose,token,frame,slot,right);
    const bool apply=reuse&&moving&&diagnostics::pairedNpcSkin();
    if(diagnostics::npcBufferEvent()){
        std::array<uint64_t,2> handles{};std::array<unsigned,2> offsets{};
        const bool valid=read(model+0x600,handles.data(),sizeof(handles))&&read(model+0x610,offsets.data(),sizeof(offsets));
        circlevr::vkcore::logf("motor-buffer-skin: model %p committed %p pose %p token %llu frame %u eye %u inputSlot %d expectedSlot %d moving %d pairMatch %d applied %d valid %d handle0 %llx handle1 %llx offset0 %u offset1 %u",
            (void*)model,(void*)committed,(void*)pose,token,frame,right?1:0,slot,moving&&!apply?1-slot:slot,moving,reuse,apply,valid,handles[0],handles[1],offsets[0],offsets[1]);
    }
    if(diagnostics::enabled()&&frame%120<=3)
        circlevr::vkcore::logf("motor-npc-skin-select: model %p token %llu frame %u eye %u inputSlot %d moving %d pairMatch %d selectedL %d applied %d asset %s",
            (void*)model,token,frame,right?1:0,slot,moving,reuse,e.skinPair.selected,apply,e.name);
    if(apply){
        // Take the existing JE over the second increment. Both native model
        // state and EAX already contain L's selected slot; no pointer rewrite.
        r.rflags|=zf;
    }
    if(!right&&(!reuse||!diagnostics::pairedNpcSkin()))
        e.skinPair.observeLeft(pose,token,frame,slot,moving);
}
void observeNpcModel(uintptr_t model,uintptr_t committed){
    if(!adapterInstalled||!inGameplay||stopped||!model||!committed)return;
    const auto frame=field<unsigned>(base+build::rva(0x4a71950));
    if(frame%120>3&&!diagnostics::npcBufferActive()&&!gpu_audit::recording())return;
    const auto native=locomotion::latestNative();
    if(!native.valid||GetTickCount64()-native.tick>100)return;
    std::array<float,3> p{};
    if(!read(model+0x120,p.data(),sizeof(p)))return;
    const float x=p[0]-native.position.x,y=p[1]-native.position.y,z=p[2]-native.position.z;
    if(!std::isfinite(x+y+z)||x*x+y*y+z*z>900.f)return;
    char name[160]{},instance[256]{};
    if(!read(field<uintptr_t>(model+0x90),name,sizeof(name)-1)||std::strstr(name,"indiana"))return;
    read(field<uintptr_t>(model+0x98),instance,sizeof(instance)-1);
    if(field<uintptr_t>(model+0x88)!=committed)return;
    const auto manager=field<uintptr_t>(committed+0x28);const auto index=field<int>(committed+0x30);
    if(!manager||index<0||index>=1<<20)return;
    std::lock_guard lock(npcAuditMutex);
    const auto kind=npcPlacementKind(name,instance,(field<unsigned char>(model+0x183)&2)!=0,
        registeredNpcWearOwner(instance,manager));
    if(kind==NpcPlacementKind::None){
        // Keep unknown NPC attachments available for observation, never correction.
        if(!diagnostics::npcBufferActive()&&!gpu_audit::recording())return;
        if(!std::string_view(instance).starts_with("npc_"))return;
    }
    if(field<unsigned char>(model+0x183)&2)return; // never claim player/viewmodel placement
    if(field<uintptr_t>(model+0x88)!=committed)return;
    NpcPairAudit* entry=nullptr;
    for(auto& e:npcAuditEntries)if(e.model==model){entry=&e;break;}
    if(!entry)for(auto& e:npcAuditEntries)if(!e.model||GetTickCount64()-e.seen>5000){entry=&e;break;}
    if(!entry)return;
    if(entry->model!=model||entry->committed!=committed||entry->manager!=manager||entry->index!=unsigned(index)||
       std::strcmp(entry->name,name)||std::strcmp(entry->instance,instance)){
        npcPlacementLookup.erase(entry->committed);
        *entry={};entry->model=model;entry->committed=committed;entry->manager=manager;entry->index=unsigned(index);
        std::memcpy(entry->name,name,sizeof(name));
        std::memcpy(entry->instance,instance,sizeof(instance));
        if(diagnostics::npcBufferEvent())circlevr::vkcore::logf("motor-buffer-model: model %p committed %p manager %p index %u asset %s",
            (void*)model,(void*)committed,(void*)manager,unsigned(index),name);
    }
    entry->placementKind=kind;
    entry->seen=GetTickCount64();
    npcPlacementLookup[committed]=size_t(entry-npcAuditEntries.data());
}
// C5C9C3: R15 supplies origin/axes/scale to the native affine builder and
// its transformed bounds. Hold one placement for the two eye renders, just as
// for the player. Do not rewrite the actor, native source or only its GPU matrix.
void npcPairedPlacement(safetyhook::Context& r){
    if(!adapterInstalled||stopped||!inGameplay||monoVrEnabled())return;
    const auto eye=circlevr::vkcore::seqChainEye();
    if(eye!=circlevr::stereo_seq::Eye::Left&&eye!=circlevr::stereo_seq::Eye::Right)return;
    const auto token=field<uint64_t>(base+build::rva(0x4c523a8));
    const auto frame=field<unsigned>(base+build::rva(0x4a71950));
    if(!token)return;
    std::lock_guard lock(npcAuditMutex);
    const auto found=npcPlacementLookup.find(r.rdi);if(found==npcPlacementLookup.end())return;
    const size_t i=found->second;
    {
        auto& e=npcAuditEntries[i];
        if(!e.model||e.placementKind==NpcPlacementKind::None||e.committed!=r.rdi||GetTickCount64()-e.seen>5000)return;
        const bool extended=e.placementKind==NpcPlacementKind::WearPart;
        if(extended&&!diagnostics::pairedNpcWear()){e.placement={};return;}
        if(field<uintptr_t>(e.model+0x88)!=e.committed||field<uintptr_t>(e.committed+0x28)!=e.manager||
           field<unsigned>(e.committed+0x30)!=e.index||r.r15!=field<uintptr_t>(e.manager+0x108)+e.index*60ull)return;
        // Reject changed owner/asset identity even before the periodic registry
        // refresh. Do not apply an old NPC placement to a recycled player model.
        char asset[160]{},instance[256]{};
        if(!read(field<uintptr_t>(e.model+0x90),asset,sizeof(asset)-1)||
           !read(field<uintptr_t>(e.model+0x98),instance,sizeof(instance)-1)||
           std::strcmp(asset,e.name)||std::strcmp(instance,e.instance)||
           npcPlacementKind(asset,instance,(field<unsigned char>(e.model+0x183)&2)!=0,
               registeredNpcWearOwner(instance,e.manager))!=e.placementKind){
            e.placement={};return;
        }
        std::array<float,15> native{};
        if(!read(r.r15,native.data(),sizeof(native))||!ModelPairPlacement::finite(native)){
            if(eye==circlevr::stereo_seq::Eye::Left)e.placement={};
            return;
        }
        if(eye==circlevr::stereo_seq::Eye::Left){
            gpu_audit::placement(token,frame,0,e.model,"left-saved",0,0);
            e.placement={e.model,e.committed,e.manager,0,token,e.index,frame,native};return;
        }
        if(!e.placement.matches(e.model,e.committed,e.manager,0,e.index,token,frame)){
            gpu_audit::placement(token,frame,1,e.model,"pair-miss",0,0);return;
        }
        float positionDelta=0,axisDelta=0;
        for(unsigned j=0;j<15;++j){const float d=std::fabs(native[j]-e.placement.placement[j]);
            if(j<3)positionDelta=std::fmax(positionDelta,d);else axisDelta=std::fmax(axisDelta,d);}
        thread_local std::array<std::array<float,15>,64> paired;
        gpu_audit::placement(token,frame,1,e.model,"right-aligned",positionDelta,axisDelta);
        paired[i]=e.placement.placement;r.r15=reinterpret_cast<uintptr_t>(paired[i].data());
        r.rdx=r.r15+0xc;r.r8=r.r15+0x30;
        if(diagnostics::npcBufferEvent())
            circlevr::vkcore::logf("motor-buffer-placement: model %p token %llu frame %u eye 1 extended %d leftFrame %u beforePositionDelta %.9g beforeAxisDelta %.9g dx %.9g dy %.9g dz %.9g applied 1",
                (void*)e.model,token,frame,extended,e.placement.frame,positionDelta,axisDelta,
                native[0]-paired[i][0],native[1]-paired[i][1],native[2]-paired[i][2]);
        static unsigned fixes{};
        if(positionDelta>0.0001f||axisDelta>0.0001f){const auto n=++fixes;
            if(n<=8||n%600==0)circlevr::vkcore::logf("motor-npc-placement: right render aligned token %llu frame %u positionDelta %.6f axisDelta %.6f asset %s count %u",token,frame,positionDelta,axisDelta,e.name,n);}
        return;
    }
}
void auditNpcUpload(uintptr_t manager,unsigned frame,int eye,uint64_t token){
    if((!diagnostics::enabled()&&!diagnostics::npcBufferActive()&&!gpu_audit::recording())||!token||eye<0||eye>1)return;
    std::lock_guard lock(npcAuditMutex);
    for(auto& e:npcAuditEntries){
        if(!e.model||e.manager!=manager||GetTickCount64()-e.seen>5000)continue;
        const bool pair=eye==1&&e.left&&e.token==token&&frame-e.frame==1;
        if(!pair&&frame%120>3&&!diagnostics::npcBufferActive()&&!gpu_audit::recording())continue;
        if(field<uintptr_t>(e.model+0x88)!=e.committed||field<uintptr_t>(e.committed+0x28)!=manager||
           field<unsigned>(e.committed+0x30)!=e.index){npcPlacementLookup.erase(e.committed);e={};continue;}
        std::array<float,16> matrix{};int joint{};
        const auto matrices=field<uintptr_t>(manager+0xc0),joints=field<uintptr_t>(manager+0x150);
        if(!matrices||!joints||!read(matrices+e.index*64ull,matrix.data(),sizeof(matrix))||
           !read(joints+e.index*4ull,&joint,sizeof(joint)))continue;
        // Read-only MD6 builder audit. Native 94C730 (Steam94CA70) switches
        // model+4A4 before using the two +600 handles and +610 offsets.
        // Equal joint indices alone do not validate these additional buffers.
        // No source/descriptor contents or model flags are modified here.
        const bool md6=std::strstr(e.name,".md6")!=nullptr;
        std::array<unsigned char,0x18> skinBuffers{};
        const int skinSlot=md6?field<int>(e.model+0x4a4):-1;
        const unsigned skinFlags=md6?field<unsigned char>(e.model+0x5cc):0;
        const bool skinValid=md6&&skinSlot>=0&&skinSlot<2&&
            read(e.model+0x600,skinBuffers.data(),skinBuffers.size());
        std::array<float,16> previousMatrix{};
        const auto previousMatrices=field<uintptr_t>(manager+0xf0);
        const bool previousValid=previousMatrices&&read(previousMatrices+e.index*64ull,previousMatrix.data(),sizeof(previousMatrix));
        if(gpu_audit::recording()){
            const auto bits=field<uint64_t>(field<uintptr_t>(manager+0xa8)+e.index*8ull);
            const unsigned count=unsigned((bits>>33)&4095),offset=unsigned(bits>>45);
            std::array<unsigned char,48*128> surfaces{};
            const bool valid=count<=128&&read(field<uintptr_t>(manager+0x7c8)+offset*48ull,surfaces.data(),count*48ull);
            gpu_audit::npc(token,frame,eye,e.model,e.committed,e.index,joint,e.name,e.instance,bits,
                valid?surfaces.data():nullptr,valid?count:0,matrix.data(),previousValid?previousMatrix.data():nullptr,
                skinValid?skinBuffers.data():nullptr,unsigned(skinBuffers.size()));
        }
        const auto blasRevisions=field<uintptr_t>(manager+0x258);
        const unsigned blasRevision=blasRevisions?field<unsigned>(blasRevisions+e.index*4ull):0;
        if(diagnostics::npcBufferEvent()){
            unsigned matrixHash=2166136261u;
            const auto* bytes=reinterpret_cast<const unsigned char*>(matrix.data());
            for(unsigned j=0;j<sizeof(matrix);++j)matrixHash=(matrixHash^bytes[j])*16777619u;
            std::array<uint64_t,2> handles{};std::array<unsigned,2> offsets{};
            std::memcpy(handles.data(),skinBuffers.data(),16);std::memcpy(offsets.data(),skinBuffers.data()+16,8);
            circlevr::vkcore::logf("motor-buffer-upload: model %p manager %p index %u token %llu frame %u eye %d joint %d matrixHash %08x skinValid %d skinSlot %d handle0 %llx handle1 %llx offset0 %u offset1 %u blas %u",
                (void*)e.model,(void*)manager,e.index,token,frame,eye,joint,matrixHash,skinValid,skinSlot,handles[0],handles[1],offsets[0],offsets[1],blasRevision);
        }
        if(eye==0){e.matrix=matrix;e.joint=joint;e.token=token;e.frame=frame;e.left=true;
            e.skinBuffers=skinBuffers;e.skinSlot=skinValid?skinSlot:-1;e.skinFlags=skinFlags;
            e.previousMatrix=previousMatrix;e.previousValid=previousValid;e.blasRevision=blasRevision;continue;}
        float delta=0.f,positionDelta=0.f,axisDelta=0.f;
        if(pair)for(unsigned i=0;i<16;++i){const auto d=std::fabs(e.matrix[i]-matrix[i]);delta=std::fmax(delta,d);
            if(i==3||i==7||i==11)positionDelta=std::fmax(positionDelta,d);else axisDelta=std::fmax(axisDelta,d);}
        if(pair&&diagnostics::npcBufferEvent()) {
            float previousDelta=-1.f;
            if(previousValid&&e.previousValid) {
                previousDelta=0.f;
                for(unsigned j=0;j<16;++j)
                    previousDelta=std::fmax(previousDelta,std::fabs(previousMatrix[j]-e.previousMatrix[j]));
            }
            circlevr::vkcore::logf("motor-buffer-result: model %p token %llu frame %u eye 1 matrixDelta %.9g positionDelta %.9g axisDelta %.9g jointL %d jointR %d extended %d enabled %d previousValid %d previousMatrixDelta %.9g",
                (void*)e.model,token,frame,delta,positionDelta,axisDelta,e.joint,joint,
                e.placementKind==NpcPlacementKind::WearPart,diagnostics::pairedNpcWear().load(),
                previousValid&&e.previousValid,previousDelta);
        }
        if(!diagnostics::enabled()){e.left=false;continue;}
        circlevr::vkcore::logf("motor-npc-pair-audit: model %p index %u token %llu frame %u paired %d matrixDelta %.9g jointL %d jointR %d positionDelta %.9g axisDelta %.9g asset %s",
            (void*)e.model,e.index,token,frame,pair,delta,pair?e.joint:-1,joint,positionDelta,axisDelta,e.name);
        if(pair&&skinValid&&e.skinSlot>=0){
            std::array<uint64_t,2> handles{};std::array<unsigned,2> offsets{};
            std::memcpy(handles.data(),skinBuffers.data(),16);
            std::memcpy(offsets.data(),skinBuffers.data()+16,8);
            const auto pose=field<uintptr_t>(e.model+0x490);
            float previousDelta=-1.f;
            if(previousValid&&e.previousValid){previousDelta=0;
                for(unsigned i=0;i<16;++i)previousDelta=std::fmax(previousDelta,std::fabs(previousMatrix[i]-e.previousMatrix[i]));}
            circlevr::vkcore::logf("motor-npc-skin-audit: model %p token %llu frame %u slotL %d slotR %d flagsL %x flagsR %x sameBuffers %d handles %llx/%llx offsets %u/%u pose %p previousMatrixDelta %.9g blasRevisionL %u blasRevisionR %u asset %s",
                (void*)e.model,token,frame,e.skinSlot,skinSlot,e.skinFlags,skinFlags,
                e.skinBuffers==skinBuffers,handles[0],handles[1],offsets[0],offsets[1],(void*)pose,
                previousDelta,e.blasRevision,blasRevision,e.name);
        }
        e.left=false;
    }
}
