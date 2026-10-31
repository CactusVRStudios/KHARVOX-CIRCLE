// GOG Motor 1.0.17.0 Streamline SR/RR bridge. Included inside temporal.cpp.
// ABI checked against NVIDIA sl_core_types.h/sl_consts.h and native D01DE0.
// Calls still use Motor's loaded Streamline functions, command buffers,
// resources, frame tokens and error/fallback handling.
struct alignas(8) SlViewport {
    std::uint64_t next{};
    std::array<unsigned char,16> type{};
    std::uint64_t version{};
    std::uint32_t value{},padding{};
};
static_assert(sizeof(SlViewport)==40);
constexpr std::array<unsigned char,16> viewportType{0x35,0x64,0x1b,0x17,0x3c,0x9b,0xc8,0x4f,0x99,0x94,0xfb,0xe5,0x25,0x69,0xaa,0xa4};
constexpr std::array<unsigned char,16> constantsType{0xd7,0x5a,0xd3,0xdc,0x4a,0x4e,0xad,0x4b,0xa9,0x0c,0xe0,0xc4,0x9e,0xb2,0x3a,0xfe};
bool slViewport(std::uintptr_t p,SlViewport& v){
    v=get<SlViewport>(p);
    return p&&v.next==0&&v.type==viewportType&&v.version==1&&v.value>=1&&v.value<=2;
}
std::optional<Tag> slBackendTag(){
    const auto backend=get<std::uintptr_t>(base+build::rva(0x4a729b8));
    return backend?circlevr::vkcore::seqTagForBackendFrame(get<unsigned>(backend+0x1b8)+1):std::optional<Tag>{};
}
bool slStereo(const std::optional<Tag>& t){return t&&t->eye!=Eye::Mono&&t->viewApplied;}
struct SlScope {
    std::optional<Tag> tag;
    std::uintptr_t output{};
    unsigned viewport{1},feature{};
    bool expectedStereo{},options{},tags{},evaluated{},sizeDeferred{};
    circle::fg::Capture fgInput{};
    circle::fg::Resource auditInput{},auditOutput{};
    bool fgInputValid{};
};
thread_local SlScope slScope;
thread_local SlViewport slOptionViewport,slTagViewport,slConstantViewport;
thread_local bool slOptionsPending{},slTagsPending{},slConstantsPending{};
thread_local Tag slConstantsTag;
thread_local unsigned slConstantsNativeViewport{},slOptionsFeature{};
thread_local std::array<unsigned char,224> slRrOptions;
thread_local std::array<unsigned char,456> slConstants;
struct SlMotion {std::array<unsigned char,128> matrices{};unsigned backend{};std::uintptr_t view{};bool valid{};};
thread_local SlMotion slMotion;
struct SlConstantRecord {Tag tag;std::uintptr_t token{};bool valid{};std::array<unsigned char,128> cameraTransforms{};circle::fg::Camera fgCamera{};};
std::array<std::array<SlConstantRecord,16>,2> slRecords{};
std::mutex slMutex;
std::array<StreamlineHistory,2> slHistory;
std::array<SlViewport,3> slRightHandles{};
std::array<bool,3> slRightUsed{};
bool slRrRequested=false;
std::array<StreamlineResizeGrace,2> slResizeGrace;
StreamlineTransitionRecovery slTransition;
void slRetryTransition(std::uint64_t tick){
    if(!streamlineInstalled||!ready)return;
    static const bool requested=[] {char value[32]{};GetEnvironmentVariableA("CIRCLE_UPSCALING",value,sizeof(value));return std::strcmp(value,"dlss-sr")==0;}();
    if(!requested)return;
    const int mode=aa();
    std::lock_guard lock(slMutex);
    if(mode==0){slTransition.cancel();return;}
    if(mode!=1||!slTransition.pending)return;
    const auto backend=get<std::uintptr_t>(base+build::rva(0x4a729b8));
    const auto state=get<std::uintptr_t>(backend+0xaa0);
    const auto token=get<std::uintptr_t>(state+8);
    if(!slTransition.retry(token,tick))return;
    for(auto& h:slHistory)h.invalidate();
    for(auto& grace:slResizeGrace)grace.succeeded();
    for(auto& records:slRecords)records={};
    reinterpret_cast<void(*)(void*,int)>(base+build::rva(0x4be7a0))(reinterpret_cast<void*>(base+build::rva(0x44cccd0)),2);
    circlevr::vkcore::logf("motor-sl: transition recovery %u/3 on new token %p tick %llu; histories reset, DLSS SR requested again",slTransition.attempts,(void*)token,tick);
}
void slSizeMismatch(safetyhook::Context& r){
    if(!streamlineInstalled||aa()!=2||!slScope.tag||slRrRequested)return;
    // D04EC9 is reached only when the actual input dimensions fail the
    // bounds returned by slDLSSGetOptimalSettings for this output/quality.
    // On a resize, Motor skips this pass and retries next frame. Do not turn
    // that transient into a permanent TAA selection in our slEnd adapter.
    {std::lock_guard lock(slMutex);
     slScope.sizeDeferred=slResizeGrace[circlevr::stereo_seq::eyeIndex(slScope.tag->eye)].defer();
     if(!slScope.sizeDeferred){
         const auto backend=get<std::uintptr_t>(base+build::rva(0x4a729b8));
         const auto state=get<std::uintptr_t>(backend+0xaa0);
         slTransition.transient(get<std::uintptr_t>(state+8),slScope.tag->tick);
         circlevr::vkcore::logf("motor-sl: exhausted size grace eye %u; bounded new-frame recovery %d attempt %u/3",
             circlevr::stereo_seq::eyeIndex(slScope.tag->eye),slTransition.pending,slTransition.attempts);
     }
    }
    circlevr::vkcore::logf("motor-sl: input outside DLSS bounds %dx%d output %dx%d allowed %dx%d..%dx%d; resize defer %d",
        get<int>(r.rdi+8),get<int>(r.rdi+12),get<int>(r.rbx+8),get<int>(r.rbx+12),
        get<int>(r.rbp+0x13),get<int>(r.rbp+0x17),get<int>(r.rbp+0x1b),get<int>(r.rbp+0x1f),slScope.sizeDeferred);
}
void slOptionsResult(safetyhook::Context& r){
    // Motor may report success on the bounds-failure path when the output
    // matches a cached size, even though no options were submitted. Skip
    // evaluation through Motor's ordinary error branch for this frame only.
    if(slScope.sizeDeferred)r.rax=0x26;
}
StreamlineConstantKey slKey(const Tag& t,std::uintptr_t token){return {token,t.tick,t.backendFrame,unsigned(circlevr::stereo_seq::eyeIndex(t.eye))};}
void slConstantsGate(safetyhook::Context& r){
    // D0ACF4 normally skips all constants if state.lastToken==state.token.
    // L and R deliberately share a simulation/token, but not camera matrices
    // or viewport histories. Deduplicate by actual render ownership instead.
    if(!streamlineInstalled||aa()!=2||!enabled()||get<int>(r.rbx+0x18f34)!=0)return;
    const auto tag=slBackendTag();if(!slStereo(tag)||!r.rax)return;
    bool reusable=true;
    {std::lock_guard lock(slMutex);
     for(const auto& records:slRecords){const auto& rec=records[tag->backendFrame%records.size()];
      reusable&=reusableStreamlineConstants(rec.valid,slKey(rec.tag,rec.token),slKey(*tag,r.rax));}
    }
    if(reusable)r.rflags|=uintptr_t(0x40);else r.rflags&=~uintptr_t(0x40);
}
using SlEvaluate=int(*)(void*,const void*,unsigned);
SlEvaluate slOriginalEvaluate=nullptr;
void slBegin(safetyhook::Context& r){
    slScope={};
    if(!streamlineInstalled||aa()!=2)return;
    slScope.output=get<std::uintptr_t>(r.r12+0xf0);
    const auto backend=slBackendTag();
    slScope.expectedStereo=slStereo(backend);
    std::optional<Tag> own;
    {std::lock_guard lock(outputMutex);own=outputBook.find(slScope.output);}
    // A target's last tag alone could be stale after a menu. Require the
    // actual job's backend frame as well; never guess from a global eye flag.
    if(slStereo(own)&&backend&&own->backendFrame==backend->backendFrame)
        slScope.tag=own;
    static std::atomic<unsigned> count{};const auto n=++count;
    if(n<=6||n%1200==0)circlevr::vkcore::logf("motor-sl: scope output %p own %d backend %u eye %d expectedStereo %d",(void*)slScope.output,slScope.tag?1:0,backend?backend->backendFrame:0,slScope.tag?circlevr::stereo_seq::eyeIndex(slScope.tag->eye):-1,slScope.expectedStereo);
}
void slEnd(safetyhook::Context&){
    if(slScope.expectedStereo&&!slScope.evaluated&&!slScope.sizeDeferred&&aa()==2){
        // Secondary RR and options/tag errors only log in Motor. Make their
        // fallback as explicit as the primary evaluation error path.
        reinterpret_cast<void(*)(void*,int)>(base+build::rva(0x4be7a0))(reinterpret_cast<void*>(base+build::rva(0x44cccd0)),1);
        circlevr::vkcore::logf("motor-sl: incomplete pass feature %u viewport %u options %d tags %d owned %d; native TAA fallback",slScope.feature,slScope.viewport,slScope.options,slScope.tags,slScope.tag.has_value());
    }
    slScope={};
}
void slSecondaryBegin(safetyhook::Context& r){
    slScope={};
    if(!streamlineInstalled||aa()!=2)return;
    slScope.viewport=2;slScope.feature=rrFeature;slScope.output=r.rbx;
    // This native RR pass has no accumulation selector. Its view is bound by
    // the exact backend-frame constants and frame token, checked at evaluation.
    auto t=slBackendTag();slScope.expectedStereo=slStereo(t);
    if(slScope.expectedStereo)slScope.tag=t;
}
void slSrOnly(safetyhook::Context& r){
    // Respect native hardware/plugin/guide checks in RR mode. SR explicitly
    // selects the native SR branch. Never force an unsupported RR feature.
    if(streamlineInstalled&&aa()==2&&!slRrRequested)r.rax&=~std::uintptr_t{255};
}
void slProjection(safetyhook::Context& r){
    if(!streamlineInstalled||aa()!=2||!slStereo(slBackendTag())||get<int>(r.rbx+0x18f34)!=0)return;
    const auto p=get<std::array<float,16>>(r.rbx+0x18790);
    for(float f:p)if(!std::isfinite(f))return;
    if(p[0]<=0||p[5]<=0||std::fabs(p[14]+1)>0.001f)return;
    // D0ADB2 transposes these rows, then D024F0 computes their inverse.
    // Motor otherwise rebuilds a symmetric desktop projection from FOV.
    // +18790 is CC8C30's unjittered projection; jitter stays separate.
    std::memcpy(r.xmm1.f32,p.data(),16);std::memcpy(r.xmm2.f32,p.data()+4,16);
    std::memcpy(r.xmm0.f32,p.data()+8,16);std::memcpy(r.xmm4.f32,p.data()+12,16);
}
void slSaveMotion(safetyhook::Context& r){
    slMotion={};
    if(!streamlineInstalled||aa()!=2||!enabled())return;
    const auto t=slBackendTag();
    if(!slStereo(t)||get<int>(r.rbx+0x18f34)!=0)return;
    // D0AD13 multiplies this idRenderView's previous VP by current inverse VP,
    // D0AD26 inverts it, and Motor transposes both into Constants +E0/+120.
    // D02DA0 would overwrite them using process-global previous camera data.
    // Keep the native per-view result, which already uses our own-eye history.
    slMotion.matrices=get<std::array<unsigned char,128>>(r.rcx+0xe0);
    auto values=get<std::array<float,32>>(r.rcx+0xe0);
    for(float f:values)if(!std::isfinite(f))return;
    slMotion.backend=t->backendFrame;slMotion.view=r.rbx;slMotion.valid=true;
}
void slConstantsBefore(safetyhook::Context& r){
    slConstantsPending=false;
    if(!streamlineInstalled||aa()!=2||!enabled())return;
    auto t=slBackendTag();
    if(!slStereo(t)||get<int>(r.rbx+0x18f34)!=0||!slViewport(r.r8,slConstantViewport))return;
    if(!slMotion.valid||slMotion.backend!=t->backendFrame||slMotion.view!=r.rbx)return;
    if(get<std::array<unsigned char,16>>(r.rcx+8)!=constantsType||get<std::uint64_t>(r.rcx+24)!=2||get<std::uintptr_t>(r.rcx)!=0)return;
    slConstants=get<std::array<unsigned char,456>>(r.rcx);
    std::memcpy(slConstants.data()+0xe0,slMotion.matrices.data(),128);
    const unsigned eye=circlevr::stereo_seq::eyeIndex(t->eye);
    bool reset=false;
    {std::lock_guard lock(slMutex);
        slConstantsNativeViewport=slConstantViewport.value-1;
        reset=slHistory[slConstantsNativeViewport].reset(eye,t->eyeSeq,t->backendFrame,get<unsigned>(r.rbx+0x18f24),get<unsigned>(r.rbx+0x18f28),slConstants[447]!=0,sequential::monoVrEnabled()?1u:2u);
        const auto state=get<std::uintptr_t>(get<std::uintptr_t>(base+build::rva(0x4a729b8))+0xaa0);
        auto& records=slRecords[slConstantsNativeViewport];
        records[t->backendFrame%records.size()]={*t,r.rdx,false,get<std::array<unsigned char,128>>(state+0x8c)};
    }
    if(reset)slConstants[447]=1;
    {std::lock_guard lock(slMutex);auto& rec=slRecords[slConstantsNativeViewport][t->backendFrame%16];
     std::memcpy(&rec.fgCamera,slConstants.data()+32,sizeof(rec.fgCamera));}
    slConstantViewport.value=eyeViewport(slConstantViewport.value,eye);
    r.rcx=reinterpret_cast<std::uintptr_t>(slConstants.data());
    r.r8=reinterpret_cast<std::uintptr_t>(&slConstantViewport);
    slConstantsTag=*t;slConstantsPending=true;
    static std::atomic<unsigned> counts[2]{};const auto n=++counts[eye];
    if(n<=6||n%600==0){
        float jitter[2]{};std::memcpy(jitter,slConstants.data()+352,8);
        circlevr::vkcore::logf("motor-sl: constants eye %u viewport %u backend %u eyeSeq %u token %p reset %d jitter %.5f %.5f",eye,slConstantViewport.value,t->backendFrame,t->eyeSeq,(void*)r.rdx,reset,jitter[0],jitter[1]);
    }
}
void slConstantsAfter(safetyhook::Context& r){
    if(!slConstantsPending)return;slConstantsPending=false;
    std::lock_guard lock(slMutex);
    auto& records=slRecords[slConstantsNativeViewport];
    auto& rec=records[slConstantsTag.backendFrame%records.size()];
    if(rec.tag.backendFrame==slConstantsTag.backendFrame)rec.valid=static_cast<unsigned>(r.rax)==0;
    if(static_cast<unsigned>(r.rax)==27){
        slTransition.transient(rec.token,rec.tag.tick);
        circlevr::vkcore::logf("motor-sl: duplicate constants viewport %u backend %u token %p tick %llu; bounded new-frame recovery %d",slConstantsNativeViewport+1,rec.tag.backendFrame,(void*)rec.token,rec.tag.tick,slTransition.pending);
    }
    if(static_cast<unsigned>(r.rax)!=0)circlevr::vkcore::logf("motor-sl: constants rejected %u",static_cast<unsigned>(r.rax));
}
void slOptionsBefore(safetyhook::Context& r){
    slOptionsPending=false;
    if(!slScope.tag||!slViewport(r.rcx,slOptionViewport)||slOptionViewport.value!=slScope.viewport)return;
    slOptionsFeature=0;
    slOptionViewport.value=eyeViewport(slOptionViewport.value,circlevr::stereo_seq::eyeIndex(slScope.tag->eye));
    r.rcx=reinterpret_cast<std::uintptr_t>(&slOptionViewport);slOptionsPending=true;
}
void slRrOptionsBefore(safetyhook::Context& r){
    slOptionsPending=false;
    if(!slScope.tag||!slViewport(r.rcx,slOptionViewport)||slOptionViewport.value!=slScope.viewport)return;
    constexpr std::array<unsigned char,16> rrType{4,0x75,0xd8,0x0a,0x4e,0x77,0xf3,0x4b,0x96,0x33,0xa4,0x4d,0x1f,0x7f,0x9c,0xb8};
    if(get<std::array<unsigned char,16>>(r.rdx+8)!=rrType||get<std::uint64_t>(r.rdx+24)!=3||get<std::uintptr_t>(r.rdx)!=0)return;
    const auto& t=*slScope.tag;
    {std::lock_guard lock(slMutex);const auto& records=slRecords[slScope.viewport-1];const auto& rec=records[t.backendFrame%records.size()];
        if(!rec.valid||rec.tag.backendFrame!=t.backendFrame||rec.tag.eye!=t.eye)return;
        slRrOptions=get<std::array<unsigned char,224>>(r.rdx);
        // World/view matrices must belong to this eye's constants, not the
        // latest shared Streamline state overwritten by another view job.
        std::memcpy(slRrOptions.data()+0x40,rec.cameraTransforms.data(),128);
    }
    slScope.feature=rrFeature;slOptionsFeature=rrFeature;
    slOptionViewport.value=eyeViewport(slOptionViewport.value,circlevr::stereo_seq::eyeIndex(t.eye));
    r.rcx=reinterpret_cast<std::uintptr_t>(&slOptionViewport);
    r.rdx=reinterpret_cast<std::uintptr_t>(slRrOptions.data());slOptionsPending=true;
}
void slOptionsAfter(safetyhook::Context& r){
    if(!slOptionsPending)return;slOptionsPending=false;
    slScope.options=static_cast<unsigned>(r.rax)==0;
    if(!slScope.options)circlevr::vkcore::logf("motor-sl: options rejected %u",static_cast<unsigned>(r.rax));
    if(slScope.options&&slScope.tag->eye==Eye::Right){std::lock_guard lock(slMutex);
        const auto slot=streamlineResourceSlot(slOptionsFeature,slScope.viewport);slRightHandles[slot]=slOptionViewport;slRightUsed[slot]=true;
    }
}
void slTagsBefore(safetyhook::Context& r){
    slTagsPending=false;
    if(!slScope.tag||!slViewport(r.rcx,slTagViewport)||slTagViewport.value!=slScope.viewport)return;
    if((circle::fg::requested()||circlevr::vkcore::motionInputCaptureRequested()||gpu_audit::temporalEnabled())&&slScope.viewport==1&&circlevr::stereo_seq::drawsEyeView(*slScope.tag)&&r.r8<=32){
        slScope.auditInput={};slScope.auditOutput={};
        circle::fg::Capture in{};in.tag=*slScope.tag;in.command=reinterpret_cast<void*>(r.r9);
        bool valid=false;
        {std::lock_guard lock(slMutex);const auto& rec=slRecords[0][in.tag.backendFrame%16];
         valid=rec.valid&&rec.tag.backendFrame==in.tag.backendFrame&&rec.tag.eyeSeq==in.tag.eyeSeq;
         in.camera=rec.fgCamera;}
        constexpr std::array<unsigned char,16> tagType{0xad,0x5a,0x6a,0x4c,0x45,0xb4,0x6c,0x49,0x87,0xff,0x1a,0xf3,0x84,0x5b,0xe6,0x53};
        constexpr std::array<unsigned char,16> resourceType{0xcf,0x70,0x9d,0x3a,0x18,0x24,0x72,0x4b,0x83,0x91,0x13,0xf8,0x72,0x1c,0x72,0x61};
        for(unsigned i=0;valid&&i<r.r8;++i){
            auto t=r.rdx+i*64;auto p=get<uintptr_t>(t+32);auto kind=get<unsigned>(t+40);
            if((kind>1&&kind!=3&&kind!=4)||!p||get<std::array<unsigned char,16>>(t+8)!=tagType||get<uint64_t>(t+24)!=1||
               get<std::array<unsigned char,16>>(p+8)!=resourceType||get<uint64_t>(p+24)!=1)continue;
            auto& out=kind==0?in.depth:kind==1?in.motion:kind==3?slScope.auditInput:slScope.auditOutput;
            out.image=get<uint64_t>(p+40);out.layout=get<unsigned>(p+64);out.width=get<unsigned>(p+68);out.height=get<unsigned>(p+72);
            out.format=get<unsigned>(p+76);out.usage=get<unsigned>(p+100);
            out.top=get<unsigned>(t+48);out.left=get<unsigned>(t+52);out.extentWidth=get<unsigned>(t+56);out.extentHeight=get<unsigned>(t+60);
        }
        static std::atomic<unsigned> probes{};if(++probes<=8)circlevr::vkcore::logf("vr-fg: tag audit valid %d eye %u tick %llu backend %u count %llu cmd %p depth %llx motion %llx firstTag %u/%llu resource %p",valid,unsigned(in.tag.eye),in.tag.tick,in.tag.backendFrame,r.r8,in.command,in.depth.image,in.motion.image,get<unsigned>(r.rdx+40),get<uint64_t>(r.rdx+24),(void*)get<uintptr_t>(r.rdx+32));
        // Motor may tag eValidUntilEvaluate resources without a command buffer.
        // Copy the descriptors now; consume them only inside this same pass,
        // immediately before its evaluation on the actual Vulkan command buffer.
        slScope.fgInput=in;
        slScope.fgInputValid=valid&&in.depth.image&&in.motion.image;
    }
    slTagViewport.value=eyeViewport(slTagViewport.value,circlevr::stereo_seq::eyeIndex(slScope.tag->eye));
    r.rcx=reinterpret_cast<std::uintptr_t>(&slTagViewport);slTagsPending=true;
}
void slTagsAfter(safetyhook::Context& r){
    if(!slTagsPending)return;slTagsPending=false;
    slScope.tags=static_cast<unsigned>(r.rax)==0;
    if(!slScope.tags)circlevr::vkcore::logf("motor-sl: tags rejected %u",static_cast<unsigned>(r.rax));
}
int slEvaluate(void* command,const void* viewport,unsigned feature){
    static std::atomic<unsigned> entries{};
    if(++entries<=3)circlevr::vkcore::logf("motor-sl: evaluate entry feature %u AA %d installed %d",feature,aa(),streamlineInstalled.load()?1:0);
    if(!streamlineInstalled||aa()!=2)return slOriginalEvaluate(command,viewport,feature);
    SlViewport v;
    if(!slViewport(reinterpret_cast<std::uintptr_t>(viewport),v)){
        static std::atomic<unsigned> count{};
        if(++count<=3)circlevr::vkcore::logf("motor-sl: unrecognized viewport pointer %p next %llx version %llu id %u typeMatch %d feature %u stereo %d",viewport,v.next,v.version,v.value,v.type==viewportType,feature,slScope.expectedStereo);
        return slScope.expectedStereo?1:slOriginalEvaluate(command,viewport,feature);
    }
    if(!supportedStreamlinePass(feature,v.value))return slOriginalEvaluate(command,viewport,feature);
    if(!slScope.tag){
        if(slScope.expectedStereo){circlevr::vkcore::logf("motor-sl: no current output ownership; requesting native TAA fallback");return 1;}
        const int result=slOriginalEvaluate(command,viewport,feature);
        static std::atomic<unsigned> nativeCounts[3]{};
        const auto n=++nativeCounts[streamlineResourceSlot(feature,v.value)];
        if(result||n<=3||n%2400==0)circlevr::vkcore::logf("motor-sl: native mono evaluate viewport %u feature %u result %d count %u (not stereo validation)",v.value,feature,result,n);
        return result;
    }
    const auto& t=*slScope.tag;
    const auto state=get<std::uintptr_t>(get<std::uintptr_t>(base+build::rva(0x4a729b8))+0xaa0);
    const auto token=get<std::uintptr_t>(state+8);
    bool valid=false;
    {std::lock_guard lock(slMutex);const auto& records=slRecords[v.value-1];const auto& rec=records[t.backendFrame%records.size()];
        valid=reusableStreamlineConstants(rec.valid,slKey(rec.tag,rec.token),slKey(t,token))&&slScope.viewport==v.value&&slScope.feature==feature;
    }
    if(!valid||!slScope.options||!slScope.tags){
        circlevr::vkcore::logf("motor-sl: incomplete pass eye %u backend %u constants %d options %d tags %d; native TAA fallback",circlevr::stereo_seq::eyeIndex(t.eye),t.backendFrame,valid,slScope.options,slScope.tags);
        return 1; // D0C2F0 takes Motor's existing r_antialiasing=1 error path.
    }
    v.value=eyeViewport(v.value,circlevr::stereo_seq::eyeIndex(t.eye));
    const bool temporalAudit=feature==0&&slScope.fgInputValid&&gpu_audit::temporalEnabled();
    if(feature==0&&slScope.fgInputValid){
        // D060E0 (Steam D1D300) passes *(*(command + 8)) as the fifth
        // slEvaluateFeature argument. Transport independently validates that
        // this is a command buffer allocated through our Vulkan layer.
        auto& in=slScope.fgInput;
        in.command=reinterpret_cast<void*>(get<uintptr_t>(get<uintptr_t>(reinterpret_cast<uintptr_t>(command)+8)));
        circlevr::vkcore::noteMotionImage(in.tag,in.motion.image,in.motion.width,in.motion.height,
                                    in.camera.motionScale,in.camera.jitter,in.camera.clipToPrev);
        if(temporalAudit)gpu_audit::temporalImages(in,slScope.auditInput,slScope.auditOutput,false,0);
        circle::fg::capture(in);slScope.fgInputValid=false;
    }
    const int result=slOriginalEvaluate(command,&v,feature);
    if(temporalAudit)gpu_audit::temporalImages(slScope.fgInput,slScope.auditInput,slScope.auditOutput,true,result);
    slScope.evaluated=result==0;
    if(result==0){std::lock_guard lock(slMutex);slResizeGrace[circlevr::stereo_seq::eyeIndex(t.eye)].succeeded();slTransition.success();}
    if(result==0&&feature==0)slLastSuccess[circlevr::stereo_seq::eyeIndex(t.eye)]=GetTickCount64();
    static std::atomic<unsigned> counts[3][2]{};const auto n=++counts[streamlineResourceSlot(feature,slScope.viewport)][circlevr::stereo_seq::eyeIndex(t.eye)];
    if(result||n<=12||n%600==0)circlevr::vkcore::logf("motor-sl: evaluate eye %u viewport %u backend %u token %p output %p result %d count %u feature %u",circlevr::stereo_seq::eyeIndex(t.eye),v.value,t.backendFrame,(void*)token,(void*)slScope.output,result,n,feature);
    return result;
}
void slFree(safetyhook::Context& r){
    if(!streamlineInstalled)return;
    SlViewport native,v;bool release=false;
    if(!slViewport(r.rdx,native))return;
    const auto feature=static_cast<unsigned>(r.rcx);
    if(!supportedStreamlinePass(feature,native.value))return;
    const auto slot=streamlineResourceSlot(feature,native.value);
    {std::lock_guard lock(slMutex);release=slRightUsed[slot];v=slRightHandles[slot];slRightUsed[slot]=false;slHistory[native.value-1].invalidate();slRecords[native.value-1]={};}
    if(!release)return;
    // This hook runs only at Motor's existing GPU-synchronized free boundary.
    const auto fn=get<std::uintptr_t>(base+build::rva(0x4a738c0));
    const int result=reinterpret_cast<int(*)(unsigned,const SlViewport&)>(fn)(feature,v);
    circlevr::vkcore::logf("motor-sl: right resources released with native left; feature %u viewport %u result %d",feature,v.value,result);
}
void installStreamline(){
    wchar_t flag[8]{};
    if(!GetEnvironmentVariableW(L"CIRCLE_EXPERIMENTAL_DLSS",flag,8)||wcscmp(flag,L"1"))return;
    streamlineRequested=true;
    wchar_t rrFlag[8]{};slRrRequested=GetEnvironmentVariableW(L"CIRCLE_EXPERIMENTAL_RR",rrFlag,8)&&wcscmp(rrFlag,L"1")==0;
    struct Point{std::uintptr_t rva;const char* bytes;std::size_t size;void(*fn)(safetyhook::Context&);};
    const Point points[]={
        {0xd0acf4,"\x0f\x84\xce\x05\x00\x00",6,slConstantsGate},
        {0xd04ec9,"\x8b\x4b\x0c\x83\xf9\xff",6,slSizeMismatch},
        {0xd0c274,"\x85\xc0\x74\x11\x48\x8d\x0d",7,slOptionsResult},
        {0xd0c03b,"\x49\x8b\xbc\x24\xf0\x00\x00\x00",8,slBegin},
        {0xd0c368,"\x49\x8b\xc7\x48\x8b\x8c\x24\x80\x00\x00\x00",11,slEnd},
        {0xd0c066,"\x84\xc0\x0f\x84\xeb\x01\x00\x00",8,slSrOnly},
        {0xd0adb2,"\x48\x89\xbc\x24\x40\x03\x00\x00",8,slProjection},
        {0xd0b297,"\xe8\x04\x7b\xff\xff",5,slSaveMotion},
        {0xd0b2a8,"\xff\x15\xfa\x85\xd6\x03",6,slConstantsBefore},
        {0xd0b2ae,"\x48\x8b\x56\x08\x4c\x8d\x46\x40",8,slConstantsAfter},
        {0xd0b2ba,"\xff\x15\xe8\x85\xd6\x03",6,slConstantsBefore},
        {0xd0b2c0,"\x48\x8b\xbc\x24\x40\x03\x00\x00",8,slConstantsAfter},
        {0xd04e9c,"\xff\x15\x66\xea\xd6\x03",6,slOptionsBefore},
        {0xd04ea2,"\x8b\x4b\x0c\x8b\xd0",5,slOptionsAfter},
        {0xd05294,"\xff\x15\x8e\xe6\xd6\x03",6,slRrOptionsBefore},
        {0xd0529a,"\x8b\x4b\x0c\x8b\xd0",5,slOptionsAfter},
        {0xd0c444,"\x44\x8b\x7b\x08\x4b\x8d\x0c\xb6",8,slSecondaryBegin},
        {0xd0c558,"\xff\x15\xca\x73\xd6\x03",6,slRrOptionsBefore},
        {0xd0c55e,"\x4c\x8b\xbc\x24\x80\x01\x00\x00",8,slOptionsAfter},
        {0xd0c640,"\x48\x8b\x4d\x40\x48\x33\xcc",7,slEnd},
        {0xd0587f,"\xff\x15\x1b\xe0\xd6\x03",6,slTagsBefore},
        {0xd05885,"\x4c\x8b\xbc\x24\xd0\x0b\x00\x00",8,slTagsAfter},
        {0xc13abf,"\xff\x15\xfb\xfd\xe5\x03",6,slFree},
        {0xc13adc,"\xff\x15\xde\xfd\xe5\x03",6,slFree},
        {0xc13af9,"\xff\x15\xc1\xfd\xe5\x03",6,slFree},
        {0xc1414a,"\xff\x15\x70\xf7\xe5\x03",6,slFree}};
    for(const auto& p:points)if(!check(p.rva,p.bytes,p.size)){circlevr::vkcore::logf("motor-sl: fingerprint rejected %llx",p.rva);return;}
    if(!check(0xd060e0,"\x48\x89\x5c\x24\x20",5))return;
    // All callbacks remain passive until every hook is installed.
    auto evaluator=safetyhook::InlineHook::create(reinterpret_cast<void*>(base+build::rva(0xd060e0)),reinterpret_cast<void*>(&slEvaluate),safetyhook::InlineHook::StartDisabled);
    if(!evaluator)return;
    slOriginalEvaluate=evaluator->original<SlEvaluate>();
    std::vector<safetyhook::MidHook> hooks;
    for(const auto& p:points){auto hook=safetyhook::MidHook::create(reinterpret_cast<void*>(base+build::rva(p.rva)),p.fn);if(!hook)return;hooks.push_back(std::move(*hook));}
    if(!evaluator->enable())return;
    new safetyhook::InlineHook(std::move(*evaluator));
    for(auto& hook:hooks)new safetyhook::MidHook(std::move(hook));
    streamlineInstalled=true;
    circlevr::vkcore::logf("motor-sl: SR/RR port installed; main 1/3 secondary RR 2/4; asymmetric constants, per-frame RR camera matrices, paired reset and native release; requested %s; FG not ported",slRrRequested?"RR":"SR");
}
