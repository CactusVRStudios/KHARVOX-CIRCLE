// Included inside gpu_audit's anonymous namespace. Explicit development mode only.
struct AuditImage { VkImageCreateInfo info{}; std::uint64_t generation{}; };
std::unordered_map<VkImage,AuditImage> auditImages;
std::vector<Snapshot> imageSpares;
std::array<bool,2> imageBefore{},imageAfter{};
std::array<VkCommandBuffer,2> imageCommands{};
std::array<std::uint64_t,2> imageCommandGenerations{};
float imageCenterX=.5f,imageCenterY=.5f;
struct LightingTarget { unsigned set{},binding{},element{}; };
std::vector<LightingTarget> lightingTargets;
std::uint64_t lightingPipeline{};
std::uint64_t lightingAuxiliary{};
std::array<bool,2> auxiliaryBefore{},auxiliaryAfter{};
std::array<VkCommandBuffer,2> auxiliaryCommands{};
std::array<std::uint64_t,2> auxiliaryGenerations{};
std::array<bool,2> lightingBefore{},lightingAfter{};
std::array<VkCommandBuffer,2> lightingCommands{};
std::array<std::uint64_t,2> lightingGenerations{};
bool rasterImageMode{};
struct RasterImageBinding {
    LightingTarget target{};VkDescriptorSet set{};std::uint64_t version{};
    Binding descriptor{};ImageViewRecord view{};
};
struct RasterImageSelection {
    VkCommandBuffer command{};std::uint64_t generation{};unsigned pass{};
    std::vector<RasterImageBinding> bindings;
    std::optional<Binding> constants;
};
std::array<RasterImageSelection,2> rasterImageSelections{};
unsigned imageBytes(unsigned f){
    switch(f){
    case VK_FORMAT_BC4_UNORM_BLOCK:return 1; // Block byte count is calculated separately below.
    case VK_FORMAT_R8_UNORM:case VK_FORMAT_R8_SNORM:case VK_FORMAT_R8_UINT:return 1;
    case VK_FORMAT_R8G8_UNORM:case VK_FORMAT_R8G8_SNORM:
    case VK_FORMAT_R16_UNORM:case VK_FORMAT_R16_SNORM:case VK_FORMAT_R16_UINT:case VK_FORMAT_R16_SFLOAT:return 2;
    case VK_FORMAT_R8G8B8A8_SNORM:case VK_FORMAT_R16G16_UNORM:case VK_FORMAT_R16G16_SNORM:
    case VK_FORMAT_R16G16_UINT:case VK_FORMAT_R32_UINT:return 4;
    case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:case VK_FORMAT_A2B10G10R10_UNORM_PACK32:case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_D24_UNORM_S8_UINT:case VK_FORMAT_D32_SFLOAT_S8_UINT:
    case VK_FORMAT_R16G16_SFLOAT:case VK_FORMAT_R32_SFLOAT:case VK_FORMAT_D32_SFLOAT:return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT:case VK_FORMAT_R32G32_SFLOAT:return 8;
    default:return 0;
    }
}
VKAPI_ATTR VkResult VKAPI_CALL CreateImage(VkDevice v,const VkImageCreateInfo* ci,const VkAllocationCallbacks* a,VkImage* out){
    auto* d=dev(v);auto info=*ci;
    const bool eligible=temporalEnabled()&&d->game&&imageBytes(ci->format)&&
        (ci->imageType==VK_IMAGE_TYPE_2D||ci->imageType==VK_IMAGE_TYPE_3D)&&
        ci->arrayLayers==1&&ci->samples==VK_SAMPLE_COUNT_1_BIT&&
        (ci->usage&VK_IMAGE_USAGE_SAMPLED_BIT)&&!(ci->flags&(VK_IMAGE_CREATE_SPARSE_BINDING_BIT|VK_IMAGE_CREATE_PROTECTED_BIT));
    if(eligible)info.usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const auto r=d->CreateImage(v,&info,a,out);
    if(r==VK_SUCCESS&&eligible){std::lock_guard lock(mutex);info.pNext=nullptr;info.pQueueFamilyIndices=nullptr;
        auditImages[*out]={info,++resourceGeneration};}
    return r;
}
VKAPI_ATTR void VKAPI_CALL DestroyImage(VkDevice v,VkImage image,const VkAllocationCallbacks* a){
    {std::lock_guard lock(mutex);auditImages.erase(image);}
    dev(v)->DestroyImage(v,image,a);
}
VKAPI_ATTR void VKAPI_CALL CmdPipelineBarrier(VkCommandBuffer cb,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,
    uint32_t mc,const VkMemoryBarrier* m,uint32_t bc,const VkBufferMemoryBarrier* b,uint32_t ic,const VkImageMemoryBarrier* images){
    if(temporalEnabled()){
        std::lock_guard lock(mutex);auto& c=commands[cb];
        for(unsigned i=0;i<ic;++i)if(auditImages.contains(images[i].image)&&images[i].subresourceRange.baseMipLevel==0&&
            images[i].subresourceRange.baseArrayLayer==0)c.imageLayouts[images[i].image]=images[i].newLayout;
    }
    dev(cb)->CmdPipelineBarrier(cb,src,dst,flags,mc,m,bc,b,ic,images);
}
void temporalRequest(const std::wstring& dir){
    if(active||pendingCopies||!temporalEnabled())return;
    const auto path=dir+L"\\temporal-images.request";
    FILE* f{};if(_wfopen_s(&f,path.c_str(),L"r")||!f)return;
    float x{},y{};const bool valid=fscanf_s(f,"%f %f",&x,&y)==2&&imageCrop(512,512,x,y).width;
    fclose(f);DeleteFileW(path.c_str());
    lightingTargets.clear();lightingPipeline=0;rasterImageSelections={};
    const auto rasterPath=dir+L"\\raster-images.enabled";
    rasterImageMode=GetFileAttributesW(rasterPath.c_str())!=INVALID_FILE_ATTRIBUTES;
    if(rasterImageMode)DeleteFileW(rasterPath.c_str());
    const auto lightingPath=dir+L"\\lighting-images.targets";
    FILE* lf{};bool targetsValid=true;
    if(!_wfopen_s(&lf,lightingPath.c_str(),L"r")&&lf){
        unsigned long long pipeline{};unsigned count{};
        targetsValid=fscanf_s(lf,"%llx %u",&pipeline,&count)==2&&pipeline&&count>0&&count<=8;
        for(unsigned i=0;targetsValid&&i<count;++i){LightingTarget t;
            targetsValid=fscanf_s(lf,"%u %u %u",&t.set,&t.binding,&t.element)==3&&t.set<8&&t.element<4096;
            if(targetsValid)lightingTargets.push_back(t);
        }
        fclose(lf);DeleteFileW(lightingPath.c_str());lightingPipeline=pipeline;
    }
    if(!valid||!targetsValid||(rasterImageMode&&!lightingPipeline)||imageSpares.size()<8+(rasterImageMode?2:4)*lightingTargets.size()){
        lightingTargets.clear();lightingPipeline=0;
        circlevr::vkcore::logf("temporal-images: request rejected (invalid request or insufficient retained capture slots)");return;
    }
    lightingAuxiliary=0;auxiliaryBefore={};auxiliaryAfter={};auxiliaryCommands={};auxiliaryGenerations={};
    const auto auxiliaryPath=dir+L"\\lighting-auxiliary.pipeline";
    FILE* af{};if(!_wfopen_s(&af,auxiliaryPath.c_str(),L"r")&&af){
        unsigned long long value{};if(fscanf_s(af,"%llx",&value)==1)lightingAuxiliary=value;
        fclose(af);DeleteFileW(auxiliaryPath.c_str());
    }
    lightingBefore={};lightingAfter={};lightingCommands={};lightingGenerations={};
    ++session;records=0;sessionTextBytes=0;ownershipBytes=0;addressBytes=0;truncated=false;
    capturePair={};sampledDraws={};requestedPipeline=0;requestedPass=0;
    const auto requestTag=currentTag();minimumBackend=requestTag?requestTag->backendFrame+2:0;
    physicalRanges=false;captureViewConstants=false;captureBeforePass=false;
    imageBefore={};imageAfter={};imageCommands={};imageCommandGenerations={};
    imageCenterX=x;imageCenterY=y;temporalActive=true;inventoryOnly=true;requestedRanges.clear();
    deadline=GetTickCount64()+10000;active=true;
    emit(head("temporal_start")+",\"centerX\":"+std::to_string(x)+",\"centerY\":"+std::to_string(y)+
        ",\"lightingPipeline\":"+h(reinterpret_cast<VkPipeline>(lightingPipeline))+",\"lightingTargetCount\":"+std::to_string(lightingTargets.size())+
        ",\"rasterImages\":"+(rasterImageMode?"true":"false")+
        ",\"auxiliaryPipeline\":"+h(reinterpret_cast<VkPipeline>(lightingAuxiliary))+
        ",\"maxCrop\":512,\"stages\":\"depth/motion/input before Streamline, output after Streamline\"");
    circlevr::vkcore::logf("temporal-images: bounded stereo capture %u armed at %.3f %.3f",session,x,y);
}
template<size_t N> std::string jsonFloats(const std::array<float,N>& values){
    std::string s="[";for(size_t i=0;i<N;++i){if(i)s+=",";char value[48]{};std::snprintf(value,sizeof(value),"%.9g",values[i]);s+=std::isfinite(values[i])?value:"null";}return s+"]";
}
void imageCopy(Device* d,VkCommandBuffer cb,Cmd& command,const circle::fg::Capture& c,const circle::fg::Resource& r,const char* kind,bool after,bool dispatchDescriptor=false){
    auto line=head("temporal_image")+tagText(c.tag)+",\"kind\":"+quote(kind)+",\"after\":"+(after?"true":"false")+
        ",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(command.generation)+",\"image\":"+h(r.image)+",\"tagFormat\":"+std::to_string(r.format)+",\"tagUsage\":"+std::to_string(r.usage);
    auto skip=[&](const char* why){emit(line+",\"status\":"+quote(why));};
    const auto image=reinterpret_cast<VkImage>(r.image);auto it=auditImages.find(image);
    if(it==auditImages.end()){skip("image-not-prepared-or-format-unsupported");return;}
    const auto& resource=it->second;const auto& info=resource.info;
    if(info.extent.width!=r.width||info.extent.height!=r.height||info.format!=VkFormat(r.format)||r.top||r.left||
        (r.extentWidth&&r.extentWidth!=r.width)||(r.extentHeight&&r.extentHeight!=r.height)){
        skip("resource-or-subrect-mismatch");return;
    }
    // Streamline may transition its output. Require a command-local observed
    // final layout after evaluation; never guess from the input tag alone.
    auto observed=command.imageLayouts.find(image);
    if(after&&!dispatchDescriptor&&observed==command.imageLayouts.end()){skip("output-layout-not-observed");return;}
    const auto layout=after&&!dispatchDescriptor?observed->second:VkImageLayout(r.layout);
    if((!after||dispatchDescriptor)&&observed!=command.imageLayouts.end()&&observed->second!=layout){skip("tag-layout-mismatch");return;}
    if(layout!=VK_IMAGE_LAYOUT_GENERAL&&layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&
       layout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL&&layout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL){skip("layout-unsupported");return;}
    const bool volume=info.imageType==VK_IMAGE_TYPE_3D;
    const bool bc4=info.format==VK_FORMAT_BC4_UNORM_BLOCK;
    const auto crop=(volume||bc4)?ImageCrop{0,0,r.width,r.height}:imageCrop(r.width,r.height,imageCenterX,imageCenterY);
    const unsigned depth=volume?info.extent.depth:1;
    const auto bytes=bc4?std::uint64_t((crop.width+3)/4)*((crop.height+3)/4)*depth*8:
        std::uint64_t(crop.width)*crop.height*depth*imageBytes(r.format);
    if(!crop.width||imageSpares.empty()){skip("crop-or-capacity-invalid");return;}
    auto spare=std::find_if(imageSpares.begin(),imageSpares.end(),[&](const auto& s){return s.device==d&&s.capacity>=bytes;});
    if(spare==imageSpares.end()){skip("capture-slots-exhausted");return;}
    auto s=*spare;imageSpares.erase(spare);s.cb=cb;s.commandGeneration=command.generation;s.id=++snapshotId;s.session=session;
    s.size=bytes;
    const bool combined=r.format==VK_FORMAT_D24_UNORM_S8_UINT||r.format==VK_FORMAT_D32_SFLOAT_S8_UINT;
    const VkImageAspectFlags aspect=(r.format==VK_FORMAT_D32_SFLOAT||combined)?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
    auto& vk=d->d->vk;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.oldLayout=layout;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=image;barrier.subresourceRange={VkImageAspectFlags(combined?(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT):aspect),0,1,0,1};
    vk.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkBufferImageCopy region{};region.imageSubresource={aspect,0,0,1};region.imageOffset={int(crop.x),int(crop.y),0};region.imageExtent={crop.width,crop.height,depth};
    vk.CmdCopyImageToBuffer(cb,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,s.dst,1,&region);
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.newLayout=layout;
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vk.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,1,&barrier);
    vk.CmdSetEvent(cb,s.event,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    snapshots.push_back(s);++pendingCopies;command.temporalCaptured=true;
    emit(line+",\"status\":\"recorded\",\"id\":"+std::to_string(s.id)+",\"generation\":"+std::to_string(resource.generation)+
        ",\"format\":"+std::to_string(r.format)+",\"layout\":"+std::to_string(layout)+",\"width\":"+std::to_string(r.width)+
        ",\"height\":"+std::to_string(r.height)+",\"x\":"+std::to_string(crop.x)+",\"y\":"+std::to_string(crop.y)+
        ",\"cropWidth\":"+std::to_string(crop.width)+",\"cropHeight\":"+std::to_string(crop.height)+",\"cropDepth\":"+std::to_string(depth)+",\"bytes\":"+std::to_string(s.size));
}
// Immutable GPU snapshot of the selected dispatch's constants, never a CPU
// read of a ring buffer which the game may already have reused.
void lightingConstantCopy(Device* d,VkCommandBuffer cb,Cmd& cmd,const Binding& b,const char* kind,bool after){
    auto line=head("lighting_constants")+tagText(cmd.tag)+",\"kind\":"+quote(kind)+
        ",\"after\":"+(after?"true":"false")+",\"pipeline\":"+h(rasterImageMode?reinterpret_cast<VkPipeline>(lightingPipeline):cmd.compute)+
        ",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(cmd.generation)+
        ","+bindingText(b);
    auto reject=[&](const char* why){emit(line+",\"status\":"+quote(why));};
    const auto src=buffers.find(b.buffer);
    if(src==buffers.end()||src->second.generation!=b.generation||!b.size||b.size>4096||
       b.offset%4||b.size%4||b.offset>src->second.size||b.size>src->second.size-b.offset||
       !(src->second.usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)||
       (src->second.flags&(VK_BUFFER_CREATE_SPARSE_BINDING_BIT|VK_BUFFER_CREATE_PROTECTED_BIT))){reject("source-unproven");return;}
    const auto spare=std::find_if(spareSnapshots.begin(),spareSnapshots.end(),[&](const auto& s){return s.device==d&&s.capacity>=b.size;});
    if(spare==spareSnapshots.end()){reject("constant-slots-exhausted");return;}
    auto s=*spare;spareSnapshots.erase(spare);s.cb=cb;s.commandGeneration=cmd.generation;
    s.id=++snapshotId;s.session=session;s.source=b.buffer;s.sourceGeneration=b.generation;s.size=b.size;
    auto& vk=d->d->vk;
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vk.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    VkBufferCopy copy{b.offset,0,b.size};d->copy(cb,b.buffer,s.dst,1,&copy);
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_READ_BIT;
    vk.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    vk.CmdSetEvent(cb,s.event,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    snapshots.push_back(s);++pendingCopies;cmd.temporalCaptured=true;
    emit(line+",\"status\":\"recorded\",\"id\":"+std::to_string(s.id)+",\"bytes\":"+std::to_string(s.size));
}
void lightingConstants(Device* d,VkCommandBuffer cb,Cmd& cmd,bool after){
    for(const auto& [setNumber,boundSet]:cmd.sets[1]){
        const auto set=sets.find(boundSet.set);
        if(set==sets.end()||set->second.unsupported||set->second.version!=boundSet.version)continue;
        for(const auto& [key,b]:set->second.entries){
            const auto sampler=auditSamplers.find(b.sampler);if(sampler==auditSamplers.end())continue;
            const auto& s=sampler->second.info;
            emit(head("lighting_sampler")+tagText(cmd.tag)+",\"pipeline\":"+h(cmd.compute)+
                ",\"after\":"+(after?"true":"false")+",\"set\":"+std::to_string(setNumber)+",\"binding\":"+std::to_string(key.first)+
                ",\"element\":"+std::to_string(key.second)+",\"sampler\":"+h(b.sampler)+
                ",\"minFilter\":"+std::to_string(s.minFilter)+",\"magFilter\":"+std::to_string(s.magFilter)+
                ",\"addressU\":"+std::to_string(s.addressModeU)+",\"addressV\":"+std::to_string(s.addressModeV)+
                ",\"addressW\":"+std::to_string(s.addressModeW)+",\"unnormalized\":"+std::to_string(s.unnormalizedCoordinates)+
                ",\"anisotropy\":"+std::to_string(s.anisotropyEnable)+",\"reduction\":"+std::to_string(sampler->second.reduction));
        }
    }
    const auto bound=cmd.sets[1].find(0);
    if(bound!=cmd.sets[1].end()){
        const auto set=sets.find(bound->second.set);
        if(set!=sets.end()&&!set->second.unsupported&&set->second.version==bound->second.version){
            const auto entry=set->second.entries.find({0,0});
            if(entry!=set->second.entries.end()&&entry->second.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER){
                cmd.usedSets.emplace(bound->second.set,set->second.version);watchedSets.insert(bound->second.set);
                lightingConstantCopy(d,cb,cmd,entry->second,"s0-b0",after);
            }
        }
    }
    // Captured bytes are evidence only. The tool verifies the selected SPIR-V
    // layout before interpreting this bounded 88-byte buffer-reference prefix.
    if(std::all_of(cmd.computePushValid.begin(),cmd.computePushValid.begin()+8,[](auto v){return v!=0;})){
        std::uint64_t address{};std::memcpy(&address,cmd.computePush.data(),8);
        std::vector<Binding> candidates;
        for(const auto& [buffer,info]:buffers)if(info.address&&address>=info.address&&
            address-info.address<=info.size&&88<=info.size-(address-info.address)){
            Binding b{};b.buffer=buffer;b.generation=info.generation;b.offset=address-info.address;b.size=88;candidates.push_back(b);
        }
        if(candidates.size()==1)lightingConstantCopy(d,cb,cmd,candidates.front(),"push-reference-prefix",after);
    }
    emit(head("lighting_push")+tagText(cmd.tag)+",\"after\":"+(after?"true":"false")+
        ",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(cmd.generation)+
        ",\"bytes\":"+quote(hexBytes(cmd.computePush.data(),cmd.computePush.size()).c_str())+
        ",\"valid\":"+quote(hexBytes(cmd.computePushValid.data(),cmd.computePushValid.size()).c_str()));
}
// Capture only explicitly selected descriptors at the first matching dispatch
// in each eye. These copies use the same GPU completion and submit validation
// as the DLSS snapshots; an emitted request is never evidence of completion.
void lightingDispatch(VkCommandBuffer cb,bool after){
    if(!active||!temporalActive||rasterImageMode)return;
    std::lock_guard lock(mutex);
    if(!lightingPipeline||lightingTargets.empty())return;
    auto it=commands.find(cb);auto* d=dev(cb);
    if(!d||!d->game||it==commands.end())return;
    auto& cmd=it->second;auto current=currentTag();if(!current)return;const auto& tag=*current;
    const bool auxiliary=lightingAuxiliary&&handleValue(cmd.compute)==lightingAuxiliary;
    if(!circlevr::stereo_seq::drawsEyeView(tag)||!same(cmd.tag,tag)||cmd.scope||cmd.unknownInherited||
       cmd.level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY||!(cmd.usage&VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)||
       (cmd.usage&VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)||(!auxiliary&&handleValue(cmd.compute)!=lightingPipeline))return;
    auto& beforeFlags=auxiliary?auxiliaryBefore:lightingBefore;auto& afterFlags=auxiliary?auxiliaryAfter:lightingAfter;
    auto& commandIds=auxiliary?auxiliaryCommands:lightingCommands;auto& generations=auxiliary?auxiliaryGenerations:lightingGenerations;
    const unsigned eye=circlevr::stereo_seq::eyeIndex(tag.eye);
    if(eye>1||tag.backendFrame<=minimumBackend||!capturePair.accept(tag.tick,tag.backendFrame,eye))return;
    if(after){
        if(!beforeFlags[eye]||afterFlags[eye]||commandIds[eye]!=cb||generations[eye]!=cmd.generation)return;
        afterFlags[eye]=true;
    }else{
        if(beforeFlags[eye])return;
        beforeFlags[eye]=true;commandIds[eye]=cb;generations[eye]=cmd.generation;
    }
    lightingConstants(d,cb,cmd,after);
    circle::fg::Capture capture{};capture.tag=tag;capture.command=cb;
    const std::vector<LightingTarget> auxiliaryTargets{{0,4,0},{1,3,0},{1,4,0}};
    for(const auto& target:auxiliary?auxiliaryTargets:lightingTargets){
        const auto kind=std::string(auxiliary?"aux-lighting-s":"lighting-s")+std::to_string(target.set)+"-b"+std::to_string(target.binding)+"-e"+std::to_string(target.element)+(after?"-after":"-before");
        auto line=head("lighting_binding")+tagText(tag)+",\"kind\":"+quote(kind.c_str())+",\"pipeline\":"+h(cmd.compute)+
            ",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(cmd.generation);
        auto reject=[&](const char* why){emit(line+",\"status\":"+quote(why));};
        auto bound=cmd.sets[1].find(target.set);
        if(bound==cmd.sets[1].end()){reject("set-not-bound");continue;}
        auto set=sets.find(bound->second.set);
        if(set==sets.end()||set->second.unsupported||set->second.imageRegistryTruncated||set->second.version!=bound->second.version){reject("set-not-proven");continue;}
        auto entry=set->second.entries.find({target.binding,target.element});
        if(entry==set->second.entries.end()){reject("binding-missing");continue;}
        const auto& b=entry->second;auto view=imageViews.find(b.image);
        if(view==imageViews.end()||view->second.generation!=b.generation){reject("view-not-live");continue;}
        const auto& v=view->second;auto image=auditImages.find(v.image);auto layout=cmd.imageLayouts.find(v.image);
        if(image==auditImages.end()){reject("image-not-prepared");continue;}
        const auto& info=image->second.info;
        const auto levels=v.range.levelCount==VK_REMAINING_MIP_LEVELS?info.mipLevels:v.range.levelCount;
        const auto layers=v.range.layerCount==VK_REMAINING_ARRAY_LAYERS?info.arrayLayers:v.range.layerCount;
        if((layout!=cmd.imageLayouts.end()&&layout->second!=b.imageLayout)||
           v.range.baseMipLevel||v.range.baseArrayLayer||levels!=1||layers!=1||v.format!=info.format){reject("image-layout-or-subresource-unproven");continue;}
        cmd.usedSets.emplace(bound->second.set,set->second.version);watchedSets.insert(bound->second.set);
        // A live bound image descriptor declares the layout required by this
        // dispatch. Unlike DLSS output, this boundary has an explicit Vulkan
        // layout contract. Any observed contradictory transition rejects it.
        emit(line+",\"status\":\"resolved\",\"layoutEvidence\":\"dispatch-descriptor-contract\",\"image\":"+h(v.image)+",\"setVersion\":"+std::to_string(set->second.version));
        circle::fg::Resource resource{};resource.image=handleValue(v.image);resource.layout=b.imageLayout;
        resource.width=info.extent.width;resource.height=info.extent.height;resource.format=info.format;resource.usage=info.usage;
        imageCopy(d,cb,cmd,capture,resource,kind.c_str(),after,true);
    }
    if(imageAfter[0]&&imageAfter[1]&&lightingAfter[0]&&lightingAfter[1]&&
       (!lightingAuxiliary||(auxiliaryAfter[0]&&auxiliaryAfter[1]))){
        active=false;temporalActive=false;emit(head("stop")+",\"reason\":\"lighting-and-temporal-pair-recorded\"");
    }
}
// Resolve sampled graphics inputs at the actual draw, then copy only after its
// render pass ends. Never insert transfer commands into an active render pass.
// This measures the inputs after the containing pass, not an individual pixel
// invocation; consumers must retain that distinction when interpreting results.
void rasterImageDraw(VkCommandBuffer cb){
    if(!active||!temporalActive||!rasterImageMode)return;
    std::lock_guard lock(mutex);
    auto it=commands.find(cb);auto* d=dev(cb);const auto tag=currentTag();
    if(!d||!d->game||it==commands.end()||!tag)return;
    auto& cmd=it->second;
    ++cmd.drawNumber;
    if(!circlevr::stereo_seq::drawsEyeView(*tag)||!same(cmd.tag,*tag)||cmd.mismatch||!cmd.scope||
       cmd.unknownInherited||!cmd.attachmentsKnown||cmd.level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY||
       !(cmd.usage&VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)||(cmd.usage&VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)||
       handleValue(cmd.graphics)!=lightingPipeline||tag->backendFrame<=minimumBackend)return;
    const auto eye=circlevr::stereo_seq::eyeIndex(tag->eye);
    if(eye>1||lightingBefore[eye]||!capturePair.accept(tag->tick,tag->backendFrame,eye))return;
    RasterImageSelection selected;selected.command=cb;selected.generation=cmd.generation;selected.pass=cmd.pass;
    for(const auto& target:lightingTargets){
        const auto bound=cmd.sets[0].find(target.set);if(bound==cmd.sets[0].end())return;
        const auto set=sets.find(bound->second.set);
        // Unrelated bindless images may be truncated in the metadata registry.
        // Each requested ordinary descriptor still needs exact live ownership.
        if(set==sets.end()||set->second.unsupported||set->second.version!=bound->second.version)return;
        const auto b=set->second.entries.find({target.binding,target.element});
        if(b==set->second.entries.end()||b->second.type!=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)return;
        const auto view=imageViews.find(b->second.image);
        if(view==imageViews.end()||view->second.generation!=b->second.generation)return;
        const auto& v=view->second;const auto image=auditImages.find(v.image);
        // An attachment can transition implicitly at pass end even if its
        // descriptor is read-only. Only non-attachments have the proven layout.
        if(cmd.attachmentImages.contains(v.image))return;
        if(image==auditImages.end()||v.format!=image->second.info.format||v.range.baseMipLevel||v.range.baseArrayLayer||
           (v.range.levelCount!=1&&!(v.range.levelCount==VK_REMAINING_MIP_LEVELS&&image->second.info.mipLevels==1))||
           (v.range.layerCount!=1&&!(v.range.layerCount==VK_REMAINING_ARRAY_LAYERS&&image->second.info.arrayLayers==1)))return;
        // Restrict to read-only sampled inputs; in particular do not copy a
        // simultaneously writable color/depth attachment through this path.
        if(b->second.imageLayout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&
           b->second.imageLayout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)return;
        const auto layout=cmd.imageLayouts.find(v.image);
        if(layout!=cmd.imageLayouts.end()&&layout->second!=b->second.imageLayout)return;
        selected.bindings.push_back({target,bound->second.set,set->second.version,b->second,v});
    }
    const auto bound=cmd.sets[0].find(0);
    if(bound!=cmd.sets[0].end()){
        const auto set=sets.find(bound->second.set);
        if(set!=sets.end()&&!set->second.unsupported&&set->second.version==bound->second.version){
            const auto b=set->second.entries.find({0,0});
            if(b!=set->second.entries.end()&&b->second.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER&&b->second.size<=4096)
                selected.constants=b->second;
        }
    }
    for(const auto& b:selected.bindings){cmd.usedSets.emplace(b.set,b.version);watchedSets.insert(b.set);}
    lightingBefore[eye]=true;lightingCommands[eye]=cb;lightingGenerations[eye]=cmd.generation;
    rasterImageSelections[eye]=std::move(selected);
    emit(head("raster_image_draw")+tagText(*tag)+",\"pipeline\":"+h(cmd.graphics)+",\"cb\":"+h(cb)+
        ",\"commandGeneration\":"+std::to_string(cmd.generation)+",\"pass\":"+std::to_string(cmd.pass)+
        ",\"drawNumber\":"+std::to_string(cmd.drawNumber)+",\"attachments\":"+cmd.attachments+",\"raster\":"+rasterText(cmd));
}
void rasterImageExit(VkCommandBuffer cb,Cmd& cmd){ // audit mutex held
    if(!active||!temporalActive||!rasterImageMode||cmd.scope||!cmd.tag||!same(cmd.tag,currentTag()))return;
    const auto eye=circlevr::stereo_seq::eyeIndex(cmd.tag->eye);if(eye>1||lightingAfter[eye])return;
    const auto& selected=rasterImageSelections[eye];
    if(selected.command!=cb||selected.generation!=cmd.generation||selected.pass!=cmd.pass)return;
    lightingAfter[eye]=true;
    circle::fg::Capture capture{};capture.tag=*cmd.tag;capture.command=cb;
    for(const auto& b:selected.bindings){
        const auto kind="raster-s"+std::to_string(b.target.set)+"-b"+std::to_string(b.target.binding)+"-e"+std::to_string(b.target.element)+"-after";
        auto line=head("lighting_binding")+tagText(cmd.tag)+",\"kind\":"+quote(kind.c_str())+",\"pipeline\":"+h(reinterpret_cast<VkPipeline>(lightingPipeline))+
            ",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(cmd.generation)+",\"pass\":"+std::to_string(cmd.pass)+
            ",\"captureBoundary\":\"render-pass-end\"";
        const auto set=sets.find(b.set);const auto view=imageViews.find(b.descriptor.image);const auto image=auditImages.find(b.view.image);
        const auto layout=cmd.imageLayouts.find(b.view.image);
        if(set==sets.end()||set->second.version!=b.version||view==imageViews.end()||view->second.generation!=b.descriptor.generation||
           image==auditImages.end()||!cmd.attachmentsKnown||cmd.attachmentImages.contains(b.view.image)||
           (layout!=cmd.imageLayouts.end()&&layout->second!=b.descriptor.imageLayout)){
            emit(line+",\"status\":\"resource-changed-during-pass\"");continue;
        }
        const auto& info=image->second.info;
        emit(line+",\"status\":\"resolved\",\"layoutEvidence\":\"sampled-read-only-descriptor\",\"image\":"+h(b.view.image)+",\"setVersion\":"+std::to_string(b.version));
        circle::fg::Resource r{};r.image=handleValue(b.view.image);r.layout=b.descriptor.imageLayout;
        r.width=info.extent.width;r.height=info.extent.height;r.format=info.format;r.usage=info.usage;
        imageCopy(dev(cb),cb,cmd,capture,r,kind.c_str(),true,true);
    }
    if(selected.constants)lightingConstantCopy(dev(cb),cb,cmd,*selected.constants,"raster-s0-b0",true);
    if(imageAfter[0]&&imageAfter[1]&&lightingAfter[0]&&lightingAfter[1]){
        active=false;temporalActive=false;emit(head("stop")+",\"reason\":\"raster-and-temporal-pair-recorded\"");
    }
}
void temporalCapture(const circle::fg::Capture& c,const circle::fg::Resource& input,const circle::fg::Resource& output,bool after,int result){
    if(!active||!temporalActive||!c.command||!circlevr::stereo_seq::drawsEyeView(c.tag))return;
    const auto cb=static_cast<VkCommandBuffer>(c.command);auto* d=dev(cb);auto it=commands.find(cb);
    const unsigned eye=circlevr::stereo_seq::eyeIndex(c.tag.eye);
    if(!d||!d->game||it==commands.end()||eye>1)return;
    auto& cmd=it->second;
    if(cmd.scope||cmd.unknownInherited||cmd.level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY||
       !(cmd.usage&VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)||(cmd.usage&VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)||
       !same(cmd.tag,c.tag)||!same(cmd.tag,currentTag()))return;
    if(c.tag.backendFrame<=minimumBackend||!capturePair.accept(c.tag.tick,c.tag.backendFrame,eye))return;
    if(!after){
        if(imageBefore[eye])return;imageBefore[eye]=true;imageCommands[eye]=cb;imageCommandGenerations[eye]=cmd.generation;
        emit(head("temporal_camera")+tagText(c.tag)+",\"jitter\":"+jsonFloats(c.camera.jitter)+",\"motionScale\":"+jsonFloats(c.camera.motionScale)+
            ",\"clipToPrevious\":"+jsonFloats(c.camera.clipToPrev)+",\"viewToClip\":"+jsonFloats(c.camera.viewToClip)+
            ",\"clipToView\":"+jsonFloats(c.camera.clipToView)+",\"position\":"+jsonFloats(c.camera.position)+
            ",\"cameraMotion\":"+std::to_string(c.camera.cameraMotion)+",\"jittered\":"+std::to_string(c.camera.jittered)+
            ",\"invertedDepth\":"+std::to_string(c.camera.inverted)+",\"reset\":"+std::to_string(c.camera.reset));
        imageCopy(d,cb,cmd,c,c.depth,"depth",false);imageCopy(d,cb,cmd,c,c.motion,"motion",false);imageCopy(d,cb,cmd,c,input,"input",false);
    }else{
        if(!imageBefore[eye]||imageAfter[eye]||imageCommands[eye]!=cb||imageCommandGenerations[eye]!=cmd.generation)return;
        imageAfter[eye]=true;
        emit(head("temporal_evaluate")+tagText(c.tag)+",\"result\":"+std::to_string(result));
        if(result==0)imageCopy(d,cb,cmd,c,output,"output",true);
        if(eye==1&&(!lightingPipeline||(lightingAfter[0]&&lightingAfter[1]))&&
           (!lightingAuxiliary||(auxiliaryAfter[0]&&auxiliaryAfter[1]))){active=false;temporalActive=false;emit(head("stop")+",\"reason\":\"temporal-pair-recorded\"");}
    }
}
