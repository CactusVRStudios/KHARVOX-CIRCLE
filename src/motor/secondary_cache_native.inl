// Included inside sequential's native-hook namespace.
struct SecondaryCacheResources {
    alignas(16) std::array<unsigned char,0x70> payload{},records{};
    uintptr_t owner{};
    bool payloadTemplate{},recordTemplate{},ready{};
    uintptr_t args5[2]{},args6[2]{};
    unsigned flags[2]{},usage[2]{};
    float priority[2]{};
};
SecondaryCacheResources* secondaryResources{};
void secondaryCacheTemplate(safetyhook::Context& r,unsigned kind){
    if(!secondary_cache::requested())return;
    const uintptr_t owner=r.rsi;
    // Constructor/initialization runs on the native resource lifecycle thread.
    if(!secondaryResources||secondaryResources->owner!=owner||secondaryResources->ready){
        secondaryResources=new SecondaryCacheResources{};
        secondaryResources->owner=owner;
    }
    auto& s=*secondaryResources;auto& dst=kind?s.records:s.payload;
    if(field<uintptr_t>(r.rcx+0x38)!=0){
        circlevr::vkcore::logf("motor-secondary-cache: constructor template %u not fresh",kind);return;
    }
    if(!read(r.rcx,dst.data(),dst.size()))return;
    const auto self=reinterpret_cast<uintptr_t>(dst.data());
    std::memcpy(dst.data()+0x20,&self,sizeof(self));
    s.args5[kind]=field<uintptr_t>(r.rsp+0x20);
    s.args6[kind]=field<uintptr_t>(r.rsp+0x28);
    s.flags[kind]=unsigned(r.r9);s.usage[kind]=field<unsigned>(r.rsp+0x30);
    s.priority[kind]=field<float>(r.rsp+0x38);
    (kind?s.recordTemplate:s.payloadTemplate)=true;
    circlevr::vkcore::logf("motor-secondary-cache: constructor template %u owner %p size %llu",
        kind,reinterpret_cast<void*>(owner),field<std::uint64_t>(r.rcx));
}
void secondaryPayloadTemplate(safetyhook::Context& r){secondaryCacheTemplate(r,0);}
void secondaryRecordTemplate(safetyhook::Context& r){secondaryCacheTemplate(r,1);}
void secondaryCacheCreate(safetyhook::Context& r){
    auto* s=secondaryResources;
    if(!s||s->owner!=r.rsi||s->ready||!s->payloadTemplate||!s->recordTemplate)return;
    using Create=void(*)(void*,uintptr_t,std::uint64_t,unsigned,uintptr_t,uintptr_t,unsigned,float);
    const auto create=reinterpret_cast<Create>(base+build::rva(0xd9e8c0));
    for(unsigned k=0;k<2;++k){
        auto* object=k?s->records.data():s->payload.data();
        const auto size=field<std::uint64_t>(reinterpret_cast<uintptr_t>(object));
        if(!size||size>32*1024*1024){secondary_cache::shaderFailed();return;}
        create(object,0,size,s->flags[k],s->args5[k],s->args6[k],s->usage[k],s->priority[k]);
    }
    // Native upload owns the staging copy just as for the original two records.
    std::vector<unsigned char> zero(524280);
    reinterpret_cast<void(*)(void*,const void*,unsigned,unsigned)>(base+build::rva(0xd9f640))(
        s->records.data(),zero.data(),unsigned(zero.size()),0);
    s->ready=true;
    circlevr::vkcore::logf("motor-secondary-cache: third native payload/record slot initialized owner %p bytes %llu/%llu",
        reinterpret_cast<void*>(s->owner),field<std::uint64_t>(reinterpret_cast<uintptr_t>(s->payload.data())),
        field<std::uint64_t>(reinterpret_cast<uintptr_t>(s->records.data())));
}
thread_local unsigned secondaryJobFrame{};
thread_local std::array<uintptr_t,2> secondaryOriginalPicks{};
thread_local bool secondaryRemapped{};
void secondaryCacheCounter(safetyhook::Context& r){secondaryJobFrame=unsigned(r.rax);}
void secondaryCachePick(safetyhook::Context& r){
    secondaryRemapped=false;
    if(!secondary_cache::ready()||!secondaryResources||!secondaryResources->ready)return;
    const auto tag=circlevr::vkcore::seqTagForBackendFrame(secondaryJobFrame+1);
    if(!tag||!circlevr::stereo_seq::drawsEyeView(*tag))return;
    secondaryOriginalPicks={r.rdi,r.rbx};secondaryRemapped=true;
    r.rdi=secondaryJobFrame%3;
    r.rbx=(secondaryJobFrame%3+1)%3;
    secondary_cache::markHistory(tag->backendFrame);
    static std::atomic<unsigned> counts[2]{};
    const auto eye=circlevr::stereo_seq::eyeIndex(tag->eye);const auto n=++counts[eye];
    if(n<=8||n%1800==0)circlevr::vkcore::logf(
        "motor-secondary-cache: eye %u backend %u native %u current %llu previous %llu token %llu count %u",
        eye,tag->backendFrame,secondaryJobFrame,r.rdi,r.rbx,tag->tick,n);
}
void secondaryCacheRestore(safetyhook::Context& r){
    if(secondaryRemapped){r.rdi=secondaryOriginalPicks[0];r.rbx=secondaryOriginalPicks[1];secondaryRemapped=false;}
}
void secondaryPayloadResult(safetyhook::Context& r){
    if(secondaryResources&&secondaryResources->ready&&r.rdx==2)
        r.rax=reinterpret_cast<uintptr_t>(secondaryResources->payload.data());
}
void secondaryRecordResult(safetyhook::Context& r){
    if(secondaryResources&&secondaryResources->ready&&r.rdx==2)
        r.rax=reinterpret_cast<uintptr_t>(secondaryResources->records.data());
}
