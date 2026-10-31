#include "motor/gpu_binding_audit.hpp"
#include "motor/gpu_audit_policy.hpp"
#include "framegen/bridge.hpp"
#include "vkcore/dispatch.hpp"
#include "vkcore/dump_format.hpp"
#include "vkcore/log.hpp"
#include "vkcore/seq_hooks.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace circle::motor::gpu_audit {
namespace {
using circlevr::vkcore::dump::handleValue;
using circlevr::vkcore::dump::handleText;
using circlevr::vkcore::dump::hexBytes;
using Tag=circlevr::stereo_seq::RenderTag;
template<class T> std::string h(T v){
    if constexpr(std::is_pointer_v<T>)return "\""+handleText(handleValue(v))+"\"";
    else return "\""+handleText(static_cast<std::uint64_t>(v))+"\"";
}
std::string quote(const char* v){std::string s;circlevr::vkcore::dump::appendJsonString(s,v?v:"");return s;}
std::uint64_t qpc(){LARGE_INTEGER n;QueryPerformanceCounter(&n);return n.QuadPart;}
std::mutex mutex;
std::atomic<bool> active{};
std::atomic<bool> temporalActive{};
unsigned session{}, records{};
ULONGLONG deadline{};
std::string lines;
std::size_t sessionTextBytes{};
bool truncated{};
std::size_t ownershipBytes{};
std::size_t addressBytes{};
std::atomic<bool> writerBusy{};
std::atomic_flag polling=ATOMIC_FLAG_INIT;
std::uint64_t serial{};
constexpr unsigned maxRecords=16384;
// Diagnostic-only immutable destinations; never recycle memory referenced by
// an already recorded command buffer. Allow several targeted stereo audits.
constexpr unsigned snapshotCapacity=16384;
constexpr std::uint64_t maxRange=targetedRangeLimit;
PairWindow capturePair;
bool inventoryOnly{};
std::uint64_t requestedPipeline{};
unsigned requestedPass{}; // Zero retains the legacy all-pass inventory.
bool physicalRanges{};
bool captureViewConstants{};
unsigned captureMvpIndexPlusOne{};
bool captureBeforePass{};
bool captureSkinningInputs{};
unsigned skinningBatch{};
bool inventoryNpcConsumers{};
bool pairedNpcConsumers{};
unsigned captureWidth{},captureHeight{},captureAttachmentSignature{},minimumBackend{};
std::set<VkDescriptorSet> loggedDescriptorUpdates;
std::array<std::array<unsigned,2>,2> sampledDraws{};
std::array<unsigned,2> rasterDraws{};
std::set<VkDescriptorSet> watchedSets;
std::set<std::tuple<unsigned,VkBuffer,std::uint64_t,VkDeviceSize,VkDeviceSize,unsigned,unsigned,std::uint64_t,std::uint64_t,unsigned>> capturedRanges;
// Buffers/events stay allocated until device idle/teardown. Never recycle a
// destination while a recorded or resubmitted command buffer can reference it.
Budget budget;
std::array<std::uint64_t,2> eyeBytes{};
// Keep compute captures from consuming the graphics/geometry allowance.
std::array<std::array<std::uint64_t,4>,2> phaseBytes{};
std::array<std::array<unsigned,4>,2> phaseCopies{};
std::map<std::uint64_t,std::vector<Range>> requestedRanges;
std::map<std::uint64_t,std::uint64_t> requestedGenerations;
void emit(std::string s) { // mutex held
    if(!temporalActive&&(s.starts_with("{\"event\":\"begin\"")||s.starts_with("{\"event\":\"push\"")||s.starts_with("{\"event\":\"render_attachments\"")))return;
    if(inventoryOnly&&s.starts_with("{\"event\":\"descriptor_update\""))return;
    // Address inventories can contain thousands of small allocations. They
    // must not consume the draw budget before either eye is recorded.
    if(s.starts_with("{\"event\":\"buffer_address\"")){
        if(addressBytes+s.size()+2>16ull*1024*1024)return;
        addressBytes+=s.size()+2;lines+=std::move(s)+"}\n";return;
    }
    const bool ownership=s.starts_with("{\"event\":\"npc\"")||s.starts_with("{\"event\":\"joint_selection\"");
    if(ownership){
        if(ownershipBytes+s.size()>4ull*1024*1024)return;
        ownershipBytes+=s.size();
    }
    const bool completion=s.starts_with("{\"event\":\"gpu_complete\"")||s.starts_with("{\"event\":\"submit")||
        s.starts_with("{\"event\":\"stop\"")||ownership;
    if(!completion&&(records>=maxRecords||sessionTextBytes+s.size()>16ull*1024*1024)){
        if(!truncated){lines+="{\"event\":\"truncated\",\"session\":"+std::to_string(session)+"}\n";truncated=true;}
        return;
    }
    if(!completion){++records;sessionTextBytes+=s.size()+2;}
    lines+=std::move(s)+"}\n";
}
std::string head(const char* event){
    return "{\"event\":"+quote(event)+",\"session\":"+std::to_string(session)+
           ",\"serial\":"+std::to_string(++serial)+",\"qpc\":"+std::to_string(qpc());
}
std::string tagText(const std::optional<Tag>& t) {
    if(!t)return ",\"eye\":-1,\"token\":0,\"backend\":0,\"eyeSeq\":0";
    return ",\"eye\":"+std::to_string(circlevr::stereo_seq::eyeIndex(t->eye))+
        ",\"token\":"+std::to_string(t->tick)+",\"backend\":"+std::to_string(t->backendFrame)+
        ",\"eyeSeq\":"+std::to_string(t->eyeSeq);
}
std::optional<Tag> currentTag(){
    const auto frame=circlevr::vkcore::seqBackendCounter();
    auto t=frame?circlevr::vkcore::seqTagForBackendFrame(*frame+1):std::optional<Tag>{};
    return t&&circlevr::stereo_seq::drawsEyeView(*t)?t:std::optional<Tag>{};
}
bool same(const std::optional<Tag>& a,const std::optional<Tag>& b){
    return a&&b&&a->tick==b->tick&&a->eye==b->eye&&a->backendFrame==b->backendFrame;
}
#define AUDIT_FUNCTIONS(X) \
 X(CreateImage) X(DestroyImage) X(CreateImageView) X(DestroyImageView) X(CreateSampler) X(DestroySampler) X(CreateFramebuffer) X(DestroyFramebuffer) X(CmdPipelineBarrier) X(CreateBuffer) X(DestroyBuffer) X(CreateBufferView) X(DestroyBufferView) X(GetBufferDeviceAddress) \
 X(AllocateCommandBuffers) X(FreeCommandBuffers) X(BeginCommandBuffer) X(ResetCommandBuffer) X(ResetCommandPool) X(DestroyCommandPool) \
 X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(AllocateDescriptorSets) X(FreeDescriptorSets) X(ResetDescriptorPool) X(DestroyDescriptorPool) \
 X(UpdateDescriptorSets) X(CmdBindDescriptorSets) X(CmdBindPipeline) X(CmdPushConstants) \
 X(CmdSetViewport) X(CmdSetScissor) X(CmdSetDepthBias) X(CmdSetDepthBounds) X(CmdSetLineWidth) \
 X(CmdSetViewportWithCount) X(CmdSetScissorWithCount) X(CmdSetCullMode) X(CmdSetFrontFace) \
 X(CmdSetDepthTestEnable) X(CmdSetDepthWriteEnable) X(CmdSetDepthCompareOp) X(CmdSetDepthBoundsTestEnable) \
 X(CmdSetDepthBiasEnable) X(CmdSetRasterizerDiscardEnable) X(CmdSetStencilTestEnable) \
 X(CmdBindVertexBuffers) X(CmdBindVertexBuffers2) X(CmdBindIndexBuffer) X(CmdBeginRenderPass) X(CmdEndRenderPass) \
 X(UpdateDescriptorSetWithTemplate) X(CmdPushDescriptorSetKHR) \
 X(CmdBeginRenderPass2) X(CmdEndRenderPass2) X(CmdBeginRendering) X(CmdEndRendering) \
 X(CmdDraw) X(CmdDrawIndexed) X(CmdDrawIndirect) X(CmdDrawIndexedIndirect) \
 X(CmdDrawIndirectCount) X(CmdDrawIndexedIndirectCount) X(CmdDispatch) X(CmdDispatchIndirect) \
 X(CmdExecuteCommands) X(QueueSubmit) X(QueueSubmit2)
struct Device {
    circlevr::vkcore::DeviceData* d{};
    bool game{};
#define FN(n) PFN_vk##n n{};
    AUDIT_FUNCTIONS(FN)
#undef FN
    PFN_vkCmdCopyBuffer copy{};
};
std::unordered_map<void*,std::unique_ptr<Device>> devices;
std::mutex deviceMutex;
Device* dev(void* dispatchable){
    std::lock_guard lock(deviceMutex);
    auto it=devices.find(*static_cast<void**>(dispatchable));
    return it==devices.end()?nullptr:it->second.get();
}
struct Buffer {VkDeviceSize size{};VkBufferUsageFlags usage{};std::uint64_t generation{};VkBufferCreateFlags flags{};VkDeviceAddress address{};};
struct ImageViewRecord {VkImage image{};VkFormat format{};VkImageSubresourceRange range{};std::uint64_t generation{};};
std::unordered_map<VkImageView,ImageViewRecord> imageViews;
struct AuditSampler {VkSamplerCreateInfo info{};unsigned reduction{};};
std::unordered_map<VkSampler,AuditSampler> auditSamplers;
struct AttachmentRecord {VkImageView view{};std::uint64_t generation{};};
std::unordered_map<VkFramebuffer,std::vector<AttachmentRecord>> framebuffers;
struct Binding {
    VkDescriptorType type{};VkBuffer buffer{};VkDeviceSize offset{},size{};std::uint64_t generation{};
    VkImageView image{};VkSampler sampler{};VkImageLayout imageLayout{};
};
struct Set {
    VkDescriptorPool pool{};VkDescriptorSetLayout layout{};
    std::uint64_t version{};bool unsupported{};
    bool imageRegistryTruncated{};
    std::map<std::pair<unsigned,unsigned>,Binding> entries;
};
struct BoundSet {VkDescriptorSet set{};std::uint64_t version{};std::vector<unsigned> dynamic;VkPipelineLayout layout{};};
struct Cmd {
    std::uint64_t generation{};VkCommandPool pool{};VkCommandBufferLevel level{};
    VkCommandBufferUsageFlags usage{};
    std::optional<Tag> tag;bool scope{},mismatch{},unknownInherited{};
    unsigned pass{},drawNumber{},renderWidth{},renderHeight{},attachmentSignature{};
    VkPipeline graphics{},compute{};
    std::map<std::string,std::string> raster;
    std::map<unsigned,std::string> viewports,scissors;
    std::string attachments="null";
    bool attachmentsKnown{};
    std::set<VkImage> attachmentImages;
    std::array<std::map<unsigned,BoundSet>,2> sets;
    std::map<unsigned,Binding> vertices;Binding index;
    std::map<unsigned,VkDeviceSize> strides;
    VkIndexType indexType{};
    std::vector<Binding> passBuffers;
    std::vector<Binding> dispatchBuffers;
    std::array<unsigned char,256> computePush{},computePushValid{};
    std::vector<std::pair<VkCommandBuffer,std::uint64_t>> children;
    std::set<std::pair<VkDescriptorSet,std::uint64_t>> usedSets;
    unsigned submitted{};
    bool temporalCaptured{};
    std::unordered_map<VkImage,VkImageLayout> imageLayouts;
};
struct Snapshot {
    Device* device{};VkBuffer dst{};VkDeviceMemory memory{};void* mapped{};bool coherent{};
    VkEvent event{};VkCommandBuffer cb{};std::uint64_t commandGeneration{},id{};
    VkBuffer source{};std::uint64_t sourceGeneration{},size{};bool written{};
    unsigned session{};
    VkDeviceSize capacity{};
};
std::unordered_map<VkBuffer,Buffer> buffers;
std::unordered_map<VkBufferView,Binding> views;
std::unordered_map<VkDescriptorSetLayout,std::vector<VkDescriptorSetLayoutBinding>> layouts;
std::unordered_map<VkDescriptorSet,Set> sets;
std::unordered_map<VkCommandBuffer,Cmd> commands;
std::unordered_map<VkDescriptorPool,std::unordered_set<VkDescriptorSet>> poolSets;
std::unordered_map<VkCommandPool,std::unordered_set<VkCommandBuffer>> poolCommands;
std::vector<Snapshot> snapshots;
std::vector<Snapshot> spareSnapshots;
Snapshot allocateSnapshot(Device* d,VkDeviceSize capacity=maxRange){
    Snapshot s{};s.device=d;s.capacity=capacity;
            auto& vk=d->d->vk;const auto device=d->d->device;
            VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=capacity;ci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bool ok=vk.CreateBuffer(device,&ci,nullptr,&s.dst)==VK_SUCCESS;
            VkMemoryRequirements req{};VkPhysicalDeviceMemoryProperties props{};
            unsigned type=UINT32_MAX;
            if(ok){
                vk.GetBufferMemoryRequirements(device,s.dst,&req);
                d->d->instance->vk.GetPhysicalDeviceMemoryProperties(d->d->physicalDevice,&props);
                for(unsigned t=0;t<props.memoryTypeCount;++t)
                    if((req.memoryTypeBits&(1u<<t))&&(props.memoryTypes[t].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)){
                        if(type==UINT32_MAX||(props.memoryTypes[t].propertyFlags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT))type=t;
                    }
                ok=type!=UINT32_MAX;
            }
            if(ok){
                s.coherent=(props.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
                VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;
                ok=vk.AllocateMemory(device,&ai,nullptr,&s.memory)==VK_SUCCESS&&
                   vk.BindBufferMemory(device,s.dst,s.memory,0)==VK_SUCCESS&&
                   vk.MapMemory(device,s.memory,0,VK_WHOLE_SIZE,0,&s.mapped)==VK_SUCCESS;
            }
            VkEventCreateInfo ei{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
            if(ok)ok=vk.CreateEvent(device,&ei,nullptr,&s.event)==VK_SUCCESS;
            if(!ok){
                if(s.mapped)vk.UnmapMemory(device,s.memory);
                if(s.dst)vk.DestroyBuffer(device,s.dst,nullptr);
                if(s.memory)vk.FreeMemory(device,s.memory,nullptr);
                return {};
            }
    return s;
}
// De-duplicate only at a single copy boundary. Never reuse a previous eye or
// previous pass's data as evidence for the current GPU content.
std::uint64_t resourceGeneration{},commandGeneration{},snapshotId{};
unsigned pendingCopies{};
bool isBuffer(VkDescriptorType t){
    return t==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER||t==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER||
           t==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC||t==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}
bool dynamic(VkDescriptorType t){return t==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC||t==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;}
std::string bindingText(const Binding& b){
    auto it=buffers.find(b.buffer);
    const auto address=it!=buffers.end()&&it->second.generation==b.generation?it->second.address:0;
    return "\"buffer\":"+h(b.buffer)+",\"generation\":"+std::to_string(b.generation)+
        ",\"offset\":"+std::to_string(b.offset)+",\"size\":"+std::to_string(b.size)+
        ",\"type\":"+std::to_string(b.type)+",\"deviceAddress\":"+h(address);
}
bool usable(Device* d,Cmd& c){
    if(!d||!d->game||!active||!c.tag)return false;
    const auto now=currentTag();
    if(!same(c.tag,now))c.mismatch=true;
    if(c.mismatch)return false;
    return c.level==VK_COMMAND_BUFFER_LEVEL_PRIMARY&&!c.unknownInherited&&
        (c.usage&VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)!=0&&
        !(c.usage&VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
}
void copyBuffers(Device* d,VkCommandBuffer cb,Cmd& c,const std::vector<Binding>& input,const char* phase){
    if(inventoryOnly||requestedRanges.empty()||input.empty()||c.scope)return;
    if(!usable(d,c)){
        if(active)emit(head("copy_skipped")+",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(c.generation)+tagText(c.tag)+
            ",\"reason\":\"requires-unchanged-eye-primary-one-time-command-buffer\"");
        return;
    }
    const unsigned eye=circlevr::stereo_seq::eyeIndex(c.tag->eye);
    if(eye>1)return;
    const unsigned phaseIndex=std::strcmp(phase,"before_dispatch")==0?0:(std::strcmp(phase,"after_dispatch")==0?3:(std::strcmp(phase,"before_render_pass")==0?2:1));
    const bool dispatchPhase=phaseIndex==0||phaseIndex==3;
    std::set<std::tuple<VkBuffer,VkDeviceSize,VkDeviceSize>> unique;
    for(const auto& b:input){
        auto it=buffers.find(b.buffer);
        if(it==buffers.end()||it->second.generation!=b.generation)continue;
        const auto& src=it->second;
        if(!requestedGenerations.contains(handleValue(b.buffer))||
            requestedGenerations.at(handleValue(b.buffer))!=src.generation)continue;
        std::vector<Range> ranges;
        auto requested=requestedRanges.find(handleValue(b.buffer));
        if(!requestedRanges.empty()){
            if(requested==requestedRanges.end())continue;
            // Require the entire selected range to be bound. Do not turn one
            // target into many partial copies across different draw bindings.
            auto bound=range(src.size,b.offset,b.size,src.size);
            if(!bound.valid)continue;
            for(auto r:requested->second){
                if(r.offset>=bound.offset&&r.offset+r.size<=bound.offset+bound.size)
                    ranges.push_back(range(src.size,r.offset,r.size,maxRange));
            }
        }else continue; // No broad/full-buffer readbacks, even without a filter.
        for(auto r:ranges){
            if(!r.valid||!unique.emplace(b.buffer,r.offset,r.size).second)continue;
            if(capturedRanges.contains({eye,b.buffer,b.generation,r.offset,r.size,phaseIndex,c.pass,handleValue(cb),c.generation,dispatchPhase?c.drawNumber:0}))continue;
            auto line=head("gpu_copy")+tagText(c.tag)+",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(c.generation)+
                ",\"pass\":"+std::to_string(c.pass)+",\"drawNumber\":"+std::to_string(c.drawNumber)+
                ",\"phase\":"+quote(phase)+","+bindingText(b)+",\"copyOffset\":"+std::to_string(r.offset)+
                ",\"copyBytes\":"+std::to_string(r.size)+",\"completeBoundRange\":"+
                (r.complete&&r.offset==b.offset&&requestedRanges.empty()?"true":"false");
            if(!(src.usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)||(src.flags&(VK_BUFFER_CREATE_SPARSE_BINDING_BIT|VK_BUFFER_CREATE_PROTECTED_BIT))||
                phaseCopies[eye][phaseIndex]>=(pairedNpcConsumers?384u:64u)||
                phaseBytes[eye][phaseIndex]+r.size>(pairedNpcConsumers?12ull:4ull)*1024*1024||
                eyeBytes[eye]+r.size>Budget::maximum/2||!budget.reserve(r.size)){
                emit(line+",\"status\":\"unsupported-or-budget\"");continue;
            }
            Snapshot s{};s.device=d;s.cb=cb;s.commandGeneration=c.generation;s.id=++snapshotId;
            s.source=b.buffer;s.sourceGeneration=b.generation;s.size=r.size;s.session=session;
            auto spare=std::find_if(spareSnapshots.begin(),spareSnapshots.end(),[&](const Snapshot& p){return p.device==d;});
            if(spare==spareSnapshots.end()){emit(line+",\"status\":\"preallocated-pool-exhausted\"");continue;}
            s.dst=spare->dst;s.memory=spare->memory;s.mapped=spare->mapped;s.coherent=spare->coherent;s.event=spare->event;
            spareSnapshots.erase(spare);
            auto& vk=d->d->vk;
            // Outside render pass only; serialize the transfer with preceding
            // writes and following reuse on THIS queue. No CPU wait/readback here.
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            vk.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
            VkBufferCopy region{r.offset,0,r.size};d->copy(cb,b.buffer,s.dst,1,&region);
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT|VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
            vk.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
            vk.CmdSetEvent(cb,s.event,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            snapshots.push_back(s);++pendingCopies;eyeBytes[eye]+=r.size;
            phaseBytes[eye][phaseIndex]+=r.size;++phaseCopies[eye][phaseIndex];
            capturedRanges.emplace(eye,b.buffer,b.generation,r.offset,r.size,phaseIndex,c.pass,handleValue(cb),c.generation,dispatchPhase?c.drawNumber:0);
            emit(line+",\"id\":"+std::to_string(s.id)+",\"status\":\"recorded\"");
        }
    }
}
std::vector<Binding> bound(Cmd& c,bool compute,std::string& fields){
    std::vector<Binding> result;fields+=",\"bindings\":[";
    bool comma=false,imageRegistryTruncated=false;unsigned omittedImages{},includedImages{};
    for(auto& [number,bound]:c.sets[compute?1:0]){
        auto it=sets.find(bound.set);
        if(it==sets.end()){fields+=(comma?",":"")+std::string("{\"set\":")+h(bound.set)+",\"unknown\":true}";comma=true;continue;}
        const auto& set=it->second;
        imageRegistryTruncated|=set.imageRegistryTruncated;
        unsigned dyn=0;
        for(auto& [key,b]:set.entries){
            // Image descriptors identify the actual motion writer. Keep this
            // metadata bounded; it never schedules an image readback.
            if(!b.buffer&&((!inventoryOnly&&!requestedPipeline)||includedImages>=64)){
                ++omittedImages;continue;
            }
            auto effective=b;bool known=true;
            if(dynamic(b.type)){
                if(dyn>=bound.dynamic.size()||b.offset>UINT64_MAX-bound.dynamic[dyn])known=false;
                else effective.offset+=bound.dynamic[dyn];
                ++dyn;
            }
            // Exact descriptor at the selected draw, not a previous frame's allocator address.
            // Skin shading uses 672 bytes; the executed skin-motion VS uses 304.
            // Both contain the history frame stamp at byte 48. Do not accept
            // arbitrary uniform ranges merely because they occupy binding zero.
            if(captureViewConstants&&!inventoryOnly&&!compute&&known&&!set.unsupported&&
               number==0&&key.first==0&&key.second==0&&(effective.size==672||effective.size==304||effective.size==656)&&
               (effective.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER||effective.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)){
                const auto handle=handleValue(effective.buffer);
                const auto resource=buffers.find(effective.buffer);
                if(resource!=buffers.end()&&resource->second.generation==effective.generation&&
                   targetedRange(effective.offset,effective.size)&&
                   range(resource->second.size,effective.offset,effective.size,maxRange).complete&&
                   (requestedRanges.contains(handle)||requestedRanges.size()<(captureMvpIndexPlusOne?128u:16u))){
                    auto& entries=requestedRanges[handle];
                    if(entries.size()<16&&std::none_of(entries.begin(),entries.end(),[&](const Range& r){return r.offset==effective.offset&&r.size==effective.size;}))
                        entries.push_back({effective.offset,effective.size,true,true});
                    requestedGenerations[handle]=effective.generation;
                }
            }
            // r213: resolve job inputs at the dispatch, including dynamic offsets.
            // r216: index discovered independently in Gina's executed GPU record.
            // Resolve the matrix allocation at this draw; never reuse a prior frame's handle.
            if(captureMvpIndexPlusOne&&!inventoryOnly&&!compute&&known&&!set.unsupported&&
               number==0&&(key.first==15||key.first==18)&&key.second==0&&effective.size==9699328){
                const auto offset=effective.offset+VkDeviceSize(captureMvpIndexPlusOne-1)*64;
                const auto resource=buffers.find(effective.buffer);
                if(resource!=buffers.end()&&resource->second.generation==effective.generation&&
                   offset>=effective.offset&&offset+64<=effective.offset+effective.size&&
                   range(resource->second.size,offset,64,maxRange).complete){
                    const auto handle=handleValue(effective.buffer);auto& entries=requestedRanges[handle];
                    if(std::none_of(entries.begin(),entries.end(),[&](const Range& r){return r.offset==offset&&r.size==64;}))
                        entries.push_back({offset,64,true,true});
                    requestedGenerations[handle]=effective.generation;
                }
            }
            // This mode is selected only after shader-hash verification by the requester.
            if(captureSkinningInputs&&!inventoryOnly&&compute&&known&&!set.unsupported&&key.second==0&&
               ((number==0&&key.first==0)||(number==1&&(key.first==1||key.first==2)))){
                auto resource=buffers.find(effective.buffer);
                const auto bytes=(std::min)(effective.size,VkDeviceSize(512*1024))&~VkDeviceSize(3);
                if(resource!=buffers.end()&&resource->second.generation==effective.generation&&
                   bytes&&effective.offset%4==0&&range(resource->second.size,effective.offset,bytes,512*1024).complete){
                    const auto handle=handleValue(effective.buffer);
                    auto& entries=requestedRanges[handle];
                    for(VkDeviceSize part=0;part<bytes;part+=maxRange){
                        const auto length=(std::min)(VkDeviceSize(maxRange),bytes-part);
                        if(entries.size()<64&&std::none_of(entries.begin(),entries.end(),[&](const Range& r){return r.offset==effective.offset+part&&r.size==length;}))
                            entries.push_back({effective.offset+part,length,true,true});
                    }
                    requestedGenerations[handle]=effective.generation;
                }
            }
            if(compute&&captureSkinningInputs&&!inventoryOnly&&
               !((number==0&&(key.first==0||key.first==9))||(number==1&&(key.first==1||key.first==2||key.first==11))))continue;
            if(effective.buffer&&!inventoryOnly&&!requestedRanges.contains(handleValue(effective.buffer)))continue;
            c.usedSets.emplace(bound.set,set.version);watchedSets.insert(bound.set);
            fields+=(comma?",":"")+std::string("{\"set\":")+h(bound.set)+",\"setNumber\":"+std::to_string(number)+
                ",\"binding\":"+std::to_string(key.first)+",\"element\":"+std::to_string(key.second)+
                ",\"setVersion\":"+std::to_string(set.version)+",\"bindVersion\":"+std::to_string(bound.version)+
                ",\"layout\":"+h(bound.layout)+
                ",\"resolved\":"+(known&&!set.unsupported?"true":"false")+","+bindingText(effective)+"}";
            if(effective.image||effective.sampler){
                ++includedImages;
                fields.pop_back();
                fields+=",\"imageView\":"+h(effective.image)+",\"sampler\":"+h(effective.sampler)+
                    ",\"imageLayout\":"+std::to_string(effective.imageLayout);
                const auto view=imageViews.find(effective.image);
                const bool live=view!=imageViews.end()&&view->second.generation==effective.generation;
                fields+=",\"imageViewLive\":"+std::string(live?"true":"false");
                if(live){const auto& v=view->second;
                    fields+=",\"image\":"+h(v.image)+",\"format\":"+std::to_string(v.format)+
                        ",\"aspect\":"+std::to_string(v.range.aspectMask)+
                        ",\"baseMip\":"+std::to_string(v.range.baseMipLevel)+
                        ",\"baseLayer\":"+std::to_string(v.range.baseArrayLayer);
                }
                fields+="}";
            }
            comma=true;
            if(known&&!set.unsupported&&effective.buffer)result.push_back(effective);
        }
    }
    fields+="],\"omittedImageDescriptors\":"+std::to_string(omittedImages)+
        ",\"imageRegistryTruncated\":"+(imageRegistryTruncated?"true":"false");
    if(!compute){
        fields+=",\"vertices\":[";comma=false;
        for(auto& [slot,b]:c.vertices){if(!inventoryOnly&&!requestedRanges.contains(handleValue(b.buffer)))continue;
            fields+=(comma?",":"")+std::string("{\"slot\":")+std::to_string(slot)+","+bindingText(b);
            auto stride=c.strides.find(slot);if(stride!=c.strides.end())fields+=",\"dynamicStride\":"+std::to_string(stride->second);
            fields+="}";comma=true;result.push_back(b);}
        fields+="],\"index\":{"+bindingText(c.index)+",\"indexType\":"+std::to_string(c.indexType)+"}";
        if(c.index.buffer)result.push_back(c.index);
    }
    return result;
}
void finishDispatch(Device* d,VkCommandBuffer cb){
    std::lock_guard lock(mutex);
    auto& c=commands[cb];
    if(active&&!c.dispatchBuffers.empty())copyBuffers(d,cb,c,c.dispatchBuffers,"after_dispatch");
    c.dispatchBuffers.clear();
}
std::string rasterText(const Cmd& c){
    std::string s="{\"dynamic\":{";bool comma=false;
    for(const auto& [key,value]:c.raster){if(comma)s+=",";comma=true;s+=quote(key.c_str())+":"+value;}
    s+="},\"viewports\":{";comma=false;
    for(const auto& [index,value]:c.viewports){if(comma)s+=",";comma=true;s+=quote(std::to_string(index).c_str())+":"+value;}
    s+="},\"scissors\":{";comma=false;
    for(const auto& [index,value]:c.scissors){if(comma)s+=",";comma=true;s+=quote(std::to_string(index).c_str())+":"+value;}
    return s+"}}";
}
void rasterImageDraw(VkCommandBuffer cb);
void draw(Device* d,VkCommandBuffer cb,const char* name,bool compute,std::string fields,std::vector<Binding> extra={}){
    if(active&&temporalActive){if(!compute)rasterImageDraw(cb);return;}
    if(!active||temporalActive||!d->game)return;
    std::lock_guard lock(mutex);
    auto& c=commands[cb];
    ++c.drawNumber;
    // Same-pair metadata only: never expand the GPU payload/copy budget to
    // every depth/hair draw. Indirect execution and NPC ownership are unknown
    // until their arguments and identity have independently been read back.
    if(!compute&&pairedNpcConsumers&&capturePair.token&&c.tag&&same(c.tag,currentTag())&&
       c.tag->tick==capturePair.token&&c.tag->backendFrame==capturePair.leftFrame+circlevr::stereo_seq::eyeIndex(c.tag->eye)){
        const auto eye=circlevr::stereo_seq::eyeIndex(c.tag->eye);
        if(rasterDraws[eye]++<1024){
            emit(head("raster_draw")+tagText(c.tag)+",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(c.generation)+
                ",\"drawNumber\":"+std::to_string(c.drawNumber)+",\"pass\":"+std::to_string(c.pass)+
                ",\"pipeline\":"+h(c.graphics)+",\"call\":"+quote(name)+",\"commandFlags\":"+std::to_string(c.usage)+
                ",\"primary\":"+(c.level==VK_COMMAND_BUFFER_LEVEL_PRIMARY?"true":"false")+
                ",\"renderWidth\":"+std::to_string(c.renderWidth)+",\"renderHeight\":"+std::to_string(c.renderHeight)+
                ",\"attachmentSignature\":"+std::to_string(c.attachmentSignature)+",\"attachments\":"+c.attachments+
                ",\"raster\":"+rasterText(c)+fields);
        }else if(rasterDraws[eye]==1025)emit(head("raster_truncated")+tagText(c.tag));
    }
    if(compute)c.dispatchBuffers.clear();
    if(requestedPipeline&&handleValue(compute?c.compute:c.graphics)!=requestedPipeline&&!(pairedNpcConsumers&&!compute))return;
    if((inventoryOnly&&inventoryNpcConsumers)||(pairedNpcConsumers&&!compute)){
        if(compute)return;
        const auto boundSet=c.sets[0].find(0);
        if(boundSet==c.sets[0].end())return;
        const auto descriptor=sets.find(boundSet->second.set);
        if(descriptor==sets.end())return;
        if(std::none_of(descriptor->second.entries.begin(),descriptor->second.entries.end(),[](const auto& entry){return entry.second.buffer&&entry.second.size==180393216;}))return;
    }
    if(requestedPass&&(!compute&&c.pass!=requestedPass))return;
    if(pairedNpcConsumers&&!compute&&captureAttachmentSignature&&c.attachmentSignature!=captureAttachmentSignature)return;
    if(pairedNpcConsumers&&!compute&&captureWidth&&captureHeight&&(c.renderWidth!=captureWidth||c.renderHeight!=captureHeight))return;
    if(requestedPass&&compute&&!captureSkinningInputs)return;
    if(compute&&captureSkinningInputs&&!inventoryOnly){
        unsigned batch{};std::memcpy(&batch,c.computePush.data()+8,4);
        if(c.tag&&c.tag->backendFrame>minimumBackend&&same(c.tag,currentTag())){
            auto set=c.sets[1].find(1);
            const auto version=set==c.sets[1].end()?0:sets[set->second.set].version;
            emit(head("skinning_dispatch_boundary")+tagText(c.tag)+",\"cb\":"+h(cb)+
                ",\"commandGeneration\":"+std::to_string(c.generation)+",\"drawNumber\":"+std::to_string(c.drawNumber)+
                ",\"pipeline\":"+h(c.compute)+",\"batch\":"+std::to_string(batch)+
                ",\"jobSet\":"+h(set==c.sets[1].end()?VK_NULL_HANDLE:set->second.set)+
                ",\"jobSetVersion\":"+std::to_string(version)+
                ",\"push\":"+quote(hexBytes(c.computePush.data(),12).c_str())+fields);
        }
        if(batch!=skinningBatch)return;
    }
    const auto now=currentTag();if(!same(c.tag,now))c.mismatch=true;
    if(c.tag&&c.tag->backendFrame<=minimumBackend)return;
    if(c.mismatch||!c.tag||!capturePair.accept(c.tag->tick,c.tag->backendFrame,
        circlevr::stereo_seq::eyeIndex(c.tag->eye)))return;
    const unsigned eye=circlevr::stereo_seq::eyeIndex(c.tag->eye);
    // Metadata-only census must reach late hair/resolve passes. Payload
    // capture retains the smaller draw and copy budgets.
    if(sampledDraws[eye][compute?0:1]>=(inventoryOnly?512u:(pairedNpcConsumers?128u:32u)))return;
    auto ranges=bound(c,compute,fields);ranges.insert(ranges.end(),extra.begin(),extra.end());
    fields+=",\"argumentBindings\":[";
    for(size_t i=0;i<extra.size();++i){if(i)fields+=",";fields+="{"+bindingText(extra[i])+"}";}
    fields+="]";
    if(!inventoryOnly&&std::none_of(ranges.begin(),ranges.end(),[](const Binding& b){
        return requestedRanges.contains(handleValue(b.buffer));}))return;
    // Indirect argument offsets are allocated anew every frame. Resolve them
    // at the selected consumer, never reuse an inventory frame's offsets.
    if(!inventoryOnly&&requestedPipeline&&!captureBeforePass){
        for(const auto& b:extra){
            const auto resource=buffers.find(b.buffer);
            if(resource==buffers.end()||resource->second.generation!=b.generation||
               !b.size||b.size>1024*1024||b.offset%4||!range(resource->second.size,b.offset,b.size,1024*1024).complete)continue;
            const auto key=handleValue(b.buffer);
            auto& entries=requestedRanges[key];
            for(VkDeviceSize part=0;part<b.size;part+=maxRange){
                const auto n=(std::min)(VkDeviceSize(maxRange),b.size-part);
                if(entries.size()<64&&std::none_of(entries.begin(),entries.end(),[&](const Range& r){return r.offset==b.offset+part&&r.size==n;}))
                    entries.push_back({b.offset+part,n,true,true});
            }
            requestedGenerations[key]=b.generation;
        }
        fields+=",\"liveIndirectArguments\":true";
    }
    // Physical-storage pointers are not descriptor bindings. Only include
    // explicitly requested, generation-checked ranges, anchored to a selected
    // pipeline which also binds at least one requested buffer.
    if(physicalRanges&&requestedPipeline&&!compute&&!inventoryOnly){
        for(const auto& [handle,b]:buffers){
            const auto key=handleValue(handle);
            if(!b.address||!requestedRanges.contains(key)||
               requestedGenerations.at(key)!=b.generation)continue;
            if(std::none_of(ranges.begin(),ranges.end(),[&](const Binding& v){return v.buffer==handle;}))
                ranges.push_back({VK_DESCRIPTOR_TYPE_MAX_ENUM,handle,0,b.size,b.generation});
        }
        fields+=",\"explicitPhysicalRanges\":true";
    }
    if(compute&&captureSkinningInputs&&!inventoryOnly){
        std::uint64_t pointer{};
        if(std::all_of(c.computePushValid.begin(),c.computePushValid.begin()+12,[](unsigned char v){return v==1;})){
            std::memcpy(&pointer,c.computePush.data(),8);
            std::vector<Binding> matches;
            for(const auto& [handle,b]:buffers){
                if(b.address&&pointer>=b.address&&pointer-b.address<=b.size&&b.size-(pointer-b.address)>=20)
                    matches.push_back({VK_DESCRIPTOR_TYPE_MAX_ENUM,handle,pointer-b.address,20,b.generation});
            }
            if(matches.size()==1){
                const auto& b=matches.front();const auto key=handleValue(b.buffer);
                auto& entries=requestedRanges[key];
                if(entries.size()<64&&std::none_of(entries.begin(),entries.end(),[&](const Range& r){return r.offset==b.offset&&r.size==20;}))
                    entries.push_back({b.offset,20,true,true});
                requestedGenerations[key]=b.generation;ranges.push_back(b);
                fields+=",\"skinningHeader\":{"+bindingText(b)+"}";
            }else fields+=",\"skinningHeaderUnresolved\":true";
        }else fields+=",\"skinningPushIncomplete\":true";
    }
    fields+=",\"attachmentSignature\":"+std::to_string(c.attachmentSignature);
    if(!compute)fields+=",\"raster\":"+rasterText(c)+",\"attachments\":"+c.attachments;
    fields+=",\"renderWidth\":"+std::to_string(c.renderWidth)+",\"renderHeight\":"+std::to_string(c.renderHeight);
    if(compute){
        fields+=",\"computePush\":"+quote(hexBytes(c.computePush.data(),256).c_str())+
            ",\"computePushValid\":"+quote(hexBytes(c.computePushValid.data(),256).c_str());
    }
    // Explicit ranges on live indirect/count resources may cover the full
    // argument table for consumer discovery, not just this call's small slice.
    for(const auto& b:extra){
        const auto it=buffers.find(b.buffer);
        if(it!=buffers.end()&&requestedGenerations.contains(handleValue(b.buffer))&&
           requestedGenerations.at(handleValue(b.buffer))==it->second.generation)
            ranges.push_back({VK_DESCRIPTOR_TYPE_MAX_ENUM,b.buffer,0,it->second.size,it->second.generation});
    }
    ++sampledDraws[eye][compute?0:1];
    emit(head(name)+",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(c.generation)+tagText(c.tag)+
         ",\"tagEvidence\":\"backend-at-begin\",\"tagMismatch\":"+(c.mismatch?"true":"false")+
         ",\"pipeline\":"+h(compute?c.compute:c.graphics)+",\"insideRenderPass\":"+(c.scope?"true":"false")+
         ",\"pass\":"+std::to_string(c.pass)+",\"drawNumber\":"+std::to_string(c.drawNumber)+
         ",\"commandFlags\":"+std::to_string(c.usage)+fields);
    if(compute&&!c.scope){
        c.dispatchBuffers=ranges;
        copyBuffers(d,cb,c,ranges,"before_dispatch");
    }
    else c.passBuffers.insert(c.passBuffers.end(),ranges.begin(),ranges.end());
}
Binding bufferBinding(VkBuffer b,VkDeviceSize off,VkDeviceSize size,VkDescriptorType type=VK_DESCRIPTOR_TYPE_MAX_ENUM){
    auto i=buffers.find(b);return {type,b,off,size,i==buffers.end()?0:i->second.generation};
}
VKAPI_ATTR VkResult VKAPI_CALL CreateSampler(VkDevice v,const VkSamplerCreateInfo* p,const VkAllocationCallbacks* a,VkSampler* out){
    const auto result=dev(v)->CreateSampler(v,p,a,out);
    if(result==VK_SUCCESS&&temporalEnabled()){
        AuditSampler state{};state.info=*p;state.info.pNext=nullptr;bool supported=true;
        for(auto* next=static_cast<const VkBaseInStructure*>(p->pNext);next;next=next->pNext){
            if(next->sType==VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO)
                state.reduction=unsigned(reinterpret_cast<const VkSamplerReductionModeCreateInfo*>(next)->reductionMode);
            else supported=false;
        }
        if(supported){std::lock_guard lock(mutex);auditSamplers[*out]=state;}
    }
    return result;
}
VKAPI_ATTR void VKAPI_CALL DestroySampler(VkDevice v,VkSampler sampler,const VkAllocationCallbacks* a){
    {std::lock_guard lock(mutex);auditSamplers.erase(sampler);}dev(v)->DestroySampler(v,sampler,a);
}
#include "motor/temporal_image_audit.inl"
VKAPI_ATTR VkResult VKAPI_CALL CreateImageView(VkDevice v,const VkImageViewCreateInfo* p,const VkAllocationCallbacks* a,VkImageView* out){
    const auto result=dev(v)->CreateImageView(v,p,a,out);
    if(result==VK_SUCCESS){std::lock_guard lock(mutex);imageViews[*out]={p->image,p->format,p->subresourceRange,++resourceGeneration};}
    return result;
}
VKAPI_ATTR void VKAPI_CALL DestroyImageView(VkDevice v,VkImageView view,const VkAllocationCallbacks* a){
    {std::lock_guard lock(mutex);imageViews.erase(view);}dev(v)->DestroyImageView(v,view,a);
}
VKAPI_ATTR VkResult VKAPI_CALL CreateFramebuffer(VkDevice v,const VkFramebufferCreateInfo* p,const VkAllocationCallbacks* a,VkFramebuffer* out){
    const auto result=dev(v)->CreateFramebuffer(v,p,a,out);
    if(result==VK_SUCCESS){std::lock_guard lock(mutex);auto& list=framebuffers[*out];list.clear();
        if(!(p->flags&VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT)&&p->attachmentCount<=32)
            for(unsigned i=0;i<p->attachmentCount;++i){const auto view=imageViews.find(p->pAttachments[i]);
                list.push_back({p->pAttachments[i],view==imageViews.end()?0:view->second.generation});}
    }
    return result;
}
VKAPI_ATTR void VKAPI_CALL DestroyFramebuffer(VkDevice v,VkFramebuffer framebuffer,const VkAllocationCallbacks* a){
    {std::lock_guard lock(mutex);framebuffers.erase(framebuffer);}dev(v)->DestroyFramebuffer(v,framebuffer,a);
}
VKAPI_ATTR VkResult VKAPI_CALL CreateBuffer(VkDevice v,const VkBufferCreateInfo* p,const VkAllocationCallbacks* a,VkBuffer* b){
    auto* d=dev(v);auto ci=*p;
    if(d->game&&!(ci.flags&(VK_BUFFER_CREATE_SPARSE_BINDING_BIT|VK_BUFFER_CREATE_PROTECTED_BIT)))ci.usage|=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    auto result=d->CreateBuffer(v,&ci,a,b);
    if(result==VK_SUCCESS){std::lock_guard lock(mutex);buffers[*b]={ci.size,ci.usage,++resourceGeneration,ci.flags};}
    return result;
}
VKAPI_ATTR void VKAPI_CALL DestroyBuffer(VkDevice v,VkBuffer b,const VkAllocationCallbacks* a){
    {std::lock_guard lock(mutex);buffers.erase(b);if(active)emit(head("destroy_buffer")+",\"buffer\":"+h(b));}
    dev(v)->DestroyBuffer(v,b,a);
}
VKAPI_ATTR VkDeviceAddress VKAPI_CALL GetBufferDeviceAddress(VkDevice v,const VkBufferDeviceAddressInfo* p){
    const auto address=dev(v)->GetBufferDeviceAddress(v,p);
    std::lock_guard lock(mutex);
    auto it=buffers.find(p->buffer);
    if(it!=buffers.end()){
        it->second.address=address;
        if(active)emit(head("buffer_address")+",\"buffer\":"+h(p->buffer)+
            ",\"generation\":"+std::to_string(it->second.generation)+
            ",\"address\":"+h(address)+",\"bytes\":"+std::to_string(it->second.size));
    }
    return address;
}
VKAPI_ATTR VkResult VKAPI_CALL CreateBufferView(VkDevice v,const VkBufferViewCreateInfo* p,const VkAllocationCallbacks* a,VkBufferView* out){
    auto r=dev(v)->CreateBufferView(v,p,a,out);if(r==VK_SUCCESS){std::lock_guard lock(mutex);views[*out]=bufferBinding(p->buffer,p->offset,p->range);}return r;
}
VKAPI_ATTR void VKAPI_CALL DestroyBufferView(VkDevice v,VkBufferView b,const VkAllocationCallbacks* a){
    {std::lock_guard lock(mutex);views.erase(b);}dev(v)->DestroyBufferView(v,b,a);
}
VKAPI_ATTR VkResult VKAPI_CALL AllocateCommandBuffers(VkDevice v,const VkCommandBufferAllocateInfo* p,VkCommandBuffer* out){
    auto r=dev(v)->AllocateCommandBuffers(v,p,out);if(r==VK_SUCCESS){std::lock_guard lock(mutex);for(unsigned i=0;i<p->commandBufferCount;++i){auto& c=commands[out[i]];c={};c.pool=p->commandPool;c.level=p->level;poolCommands[p->commandPool].insert(out[i]);}}return r;
}
VKAPI_ATTR void VKAPI_CALL FreeCommandBuffers(VkDevice v,VkCommandPool pool,unsigned n,const VkCommandBuffer* cb){
    {std::lock_guard lock(mutex);for(unsigned i=0;i<n;++i){commands.erase(cb[i]);poolCommands[pool].erase(cb[i]);}}dev(v)->FreeCommandBuffers(v,pool,n,cb);
}
VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer cb,const VkCommandBufferBeginInfo* p){
    auto r=dev(cb)->BeginCommandBuffer(cb,p);if(r==VK_SUCCESS){std::lock_guard lock(mutex);auto& c=commands[cb];auto pool=c.pool;auto level=c.level;c={};c.pool=pool;c.level=level;c.generation=++commandGeneration;c.usage=p->flags;if(active)c.tag=currentTag();c.unknownInherited=(p->flags&VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)!=0;
        if(active)emit(head("begin")+",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(c.generation)+tagText(c.tag)+",\"flags\":"+std::to_string(p->flags)+",\"level\":"+std::to_string(level));}
    return r;
}
VKAPI_ATTR VkResult VKAPI_CALL ResetCommandBuffer(VkCommandBuffer cb,VkCommandBufferResetFlags f){
    auto r=dev(cb)->ResetCommandBuffer(cb,f);if(r==VK_SUCCESS){std::lock_guard lock(mutex);auto& c=commands[cb];auto p=c.pool;auto l=c.level;c={};c.pool=p;c.level=l;}return r;
}
VKAPI_ATTR VkResult VKAPI_CALL ResetCommandPool(VkDevice v,VkCommandPool p,VkCommandPoolResetFlags f){
    auto r=dev(v)->ResetCommandPool(v,p,f);if(r==VK_SUCCESS){std::lock_guard lock(mutex);auto found=poolCommands.find(p);if(found!=poolCommands.end())for(auto cb:found->second){auto& c=commands[cb];auto l=c.level;c={};c.pool=p;c.level=l;}}return r;
}
VKAPI_ATTR void VKAPI_CALL DestroyCommandPool(VkDevice v,VkCommandPool p,const VkAllocationCallbacks* a){
    {std::lock_guard lock(mutex);auto found=poolCommands.find(p);if(found!=poolCommands.end()){for(auto cb:found->second)commands.erase(cb);poolCommands.erase(found);}}
    dev(v)->DestroyCommandPool(v,p,a);
}
VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorSetLayout(VkDevice v,const VkDescriptorSetLayoutCreateInfo* p,const VkAllocationCallbacks* a,VkDescriptorSetLayout* out){
    auto r=dev(v)->CreateDescriptorSetLayout(v,p,a,out);if(r==VK_SUCCESS){std::lock_guard lock(mutex);auto& list=layouts[*out];list.assign(p->pBindings,p->pBindings+p->bindingCount);std::sort(list.begin(),list.end(),[](auto a,auto b){return a.binding<b.binding;});}return r;
}
VKAPI_ATTR void VKAPI_CALL DestroyDescriptorSetLayout(VkDevice v,VkDescriptorSetLayout p,const VkAllocationCallbacks* a){
    {std::lock_guard lock(mutex);layouts.erase(p);}dev(v)->DestroyDescriptorSetLayout(v,p,a);
}
VKAPI_ATTR VkResult VKAPI_CALL AllocateDescriptorSets(VkDevice v,const VkDescriptorSetAllocateInfo* p,VkDescriptorSet* out){
    auto r=dev(v)->AllocateDescriptorSets(v,p,out);if(r==VK_SUCCESS){std::lock_guard lock(mutex);for(unsigned i=0;i<p->descriptorSetCount;++i){auto& s=sets[out[i]];s={};s.pool=p->descriptorPool;s.layout=p->pSetLayouts[i];s.version=++resourceGeneration;poolSets[p->descriptorPool].insert(out[i]);}}return r;
}
VKAPI_ATTR VkResult VKAPI_CALL FreeDescriptorSets(VkDevice v,VkDescriptorPool pool,unsigned n,const VkDescriptorSet* s){
    auto r=dev(v)->FreeDescriptorSets(v,pool,n,s);if(r==VK_SUCCESS){std::lock_guard lock(mutex);for(unsigned i=0;i<n;++i){sets.erase(s[i]);poolSets[pool].erase(s[i]);}}return r;
}
VKAPI_ATTR VkResult VKAPI_CALL ResetDescriptorPool(VkDevice v,VkDescriptorPool pool,VkDescriptorPoolResetFlags f){
    auto r=dev(v)->ResetDescriptorPool(v,pool,f);if(r==VK_SUCCESS){std::lock_guard lock(mutex);auto found=poolSets.find(pool);if(found!=poolSets.end()){for(auto s:found->second)sets.erase(s);found->second.clear();}}return r;
}
VKAPI_ATTR void VKAPI_CALL DestroyDescriptorPool(VkDevice v,VkDescriptorPool pool,const VkAllocationCallbacks* a){
    {std::lock_guard lock(mutex);auto found=poolSets.find(pool);if(found!=poolSets.end()){for(auto s:found->second)sets.erase(s);poolSets.erase(found);}}
    dev(v)->DestroyDescriptorPool(v,pool,a);
}
VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSets(VkDevice v,unsigned n,const VkWriteDescriptorSet* w,unsigned nc,const VkCopyDescriptorSet* copies){
    dev(v)->UpdateDescriptorSets(v,n,w,nc,copies);
    std::lock_guard lock(mutex);
    for(unsigned i=0;i<n;++i){
        auto& s=sets[w[i].dstSet];s.version=++resourceGeneration;
        const auto layout=layouts.find(s.layout);
        const auto binding=layout==layouts.end()?std::vector<VkDescriptorSetLayoutBinding>::const_iterator{}:
            std::find_if(layout->second.cbegin(),layout->second.cend(),[&](auto& b){return b.binding==w[i].dstBinding;});
        if(layout==layouts.end()||binding==layout->second.cend()||
           w[i].dstArrayElement>binding->descriptorCount||w[i].descriptorCount>binding->descriptorCount-w[i].dstArrayElement){s.unsupported=true;continue;}
        const bool imageWrite=w[i].descriptorType==VK_DESCRIPTOR_TYPE_SAMPLER||
            w[i].descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER||
            w[i].descriptorType==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE||w[i].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE||
            w[i].descriptorType==VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        const unsigned tracked=imageWrite?(w[i].dstArrayElement<64?(std::min)(w[i].descriptorCount,64-w[i].dstArrayElement):0):w[i].descriptorCount;
        if(tracked<w[i].descriptorCount)s.imageRegistryTruncated=true;
        for(unsigned j=0;j<tracked;++j){
            auto key=std::pair{w[i].dstBinding,w[i].dstArrayElement+j};s.entries.erase(key);
            if(isBuffer(w[i].descriptorType)&&w[i].pBufferInfo){auto b=w[i].pBufferInfo[j];s.entries[key]=bufferBinding(b.buffer,b.offset,b.range,w[i].descriptorType);}
            else if((w[i].descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||w[i].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)&&w[i].pTexelBufferView){
                auto it=views.find(w[i].pTexelBufferView[j]);if(it!=views.end()){s.entries[key]=it->second;s.entries[key].type=w[i].descriptorType;}else s.unsupported=true;
            }
            else if(w[i].pImageInfo&&(w[i].descriptorType==VK_DESCRIPTOR_TYPE_SAMPLER||
                w[i].descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER||w[i].descriptorType==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE||
                w[i].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE||w[i].descriptorType==VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)){
                Binding b{};b.type=w[i].descriptorType;b.image=w[i].pImageInfo[j].imageView;
                const auto view=imageViews.find(b.image);if(view!=imageViews.end())b.generation=view->second.generation;
                b.sampler=w[i].pImageInfo[j].sampler;b.imageLayout=w[i].pImageInfo[j].imageLayout;s.entries[key]=b;
            }
        }
        if((active||pendingCopies)&&watchedSets.contains(w[i].dstSet)&&loggedDescriptorUpdates.insert(w[i].dstSet).second)emit(head("descriptor_update")+",\"set\":"+h(w[i].dstSet)+",\"version\":"+std::to_string(s.version));
    }
    // Cross-binding descriptor copies are explicitly unsupported, never silently
    // treated as unchanged data. Normal native writes remain fully observed.
    for(unsigned i=0;i<nc;++i){auto& s=sets[copies[i].dstSet];s.version=++resourceGeneration;s.unsupported=true;}
}
VKAPI_ATTR void VKAPI_CALL CmdBindDescriptorSets(VkCommandBuffer cb,VkPipelineBindPoint bp,VkPipelineLayout layout,unsigned first,unsigned count,const VkDescriptorSet* values,unsigned nd,const unsigned* dyn){
    if(!active){dev(cb)->CmdBindDescriptorSets(cb,bp,layout,first,count,values,nd,dyn);return;}
    {std::lock_guard lock(mutex);if(bp==VK_PIPELINE_BIND_POINT_GRAPHICS||bp==VK_PIPELINE_BIND_POINT_COMPUTE){
        unsigned consumed=0;
        // Without reconstructing pipeline-layout compatibility, invalidate sets
        // bound through another layout. Missing evidence is safer than stale binds.
        auto& boundSets=commands[cb].sets[bp==VK_PIPELINE_BIND_POINT_COMPUTE?1:0];
        std::erase_if(boundSets,[&](const auto& item){return item.second.layout!=layout;});
        for(unsigned i=0;i<count;++i){auto& s=sets[values[i]];BoundSet b{values[i],s.version,{},layout};
            auto l=layouts.find(s.layout);if(l!=layouts.end())for(auto& entry:l->second)if(dynamic(entry.descriptorType))
                for(unsigned j=0;j<entry.descriptorCount;++j){if(consumed<nd)b.dynamic.push_back(dyn[consumed]);++consumed;}
            commands[cb].sets[bp==VK_PIPELINE_BIND_POINT_COMPUTE?1:0][first+i]=std::move(b);
        }
    }}
    dev(cb)->CmdBindDescriptorSets(cb,bp,layout,first,count,values,nd,dyn);
}
VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSetWithTemplate(VkDevice v,VkDescriptorSet s,VkDescriptorUpdateTemplate t,const void* data){
    dev(v)->UpdateDescriptorSetWithTemplate(v,s,t,data);
    std::lock_guard lock(mutex);auto& set=sets[s];set.unsupported=true;set.version=++resourceGeneration;
    if(active)emit(head("unsupported_descriptor_template")+",\"set\":"+h(s)+",\"version\":"+std::to_string(set.version));
}
VKAPI_ATTR void VKAPI_CALL CmdPushDescriptorSetKHR(VkCommandBuffer cb,VkPipelineBindPoint bp,VkPipelineLayout layout,unsigned s,unsigned n,const VkWriteDescriptorSet* writes){
    {std::lock_guard lock(mutex);if(bp==VK_PIPELINE_BIND_POINT_GRAPHICS||bp==VK_PIPELINE_BIND_POINT_COMPUTE)commands[cb].sets[bp==VK_PIPELINE_BIND_POINT_COMPUTE?1:0].erase(s);
        if(active)emit(head("unsupported_push_descriptors")+",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(commands[cb].generation)+",\"setNumber\":"+std::to_string(s));}
    dev(cb)->CmdPushDescriptorSetKHR(cb,bp,layout,s,n,writes);
}
VKAPI_ATTR void VKAPI_CALL CmdBindPipeline(VkCommandBuffer cb,VkPipelineBindPoint bp,VkPipeline p){
    if(!active){dev(cb)->CmdBindPipeline(cb,bp,p);return;}
    {std::lock_guard lock(mutex);auto& c=commands[cb];if(bp==VK_PIPELINE_BIND_POINT_COMPUTE)c.compute=p;else if(bp==VK_PIPELINE_BIND_POINT_GRAPHICS)c.graphics=p;}
    dev(cb)->CmdBindPipeline(cb,bp,p);
}
#define RASTER_SCALAR(F,T,K) \
VKAPI_ATTR void VKAPI_CALL F(VkCommandBuffer cb,T value){ \
    if(active){std::lock_guard lock(mutex);commands[cb].raster[K]=std::to_string(value);} \
    dev(cb)->F(cb,value);}
RASTER_SCALAR(CmdSetCullMode,VkCullModeFlags,"cullMode")
RASTER_SCALAR(CmdSetFrontFace,VkFrontFace,"frontFace")
RASTER_SCALAR(CmdSetDepthTestEnable,VkBool32,"depthTestEnable")
RASTER_SCALAR(CmdSetDepthWriteEnable,VkBool32,"depthWriteEnable")
RASTER_SCALAR(CmdSetDepthCompareOp,VkCompareOp,"depthCompareOp")
RASTER_SCALAR(CmdSetDepthBoundsTestEnable,VkBool32,"depthBoundsTestEnable")
RASTER_SCALAR(CmdSetDepthBiasEnable,VkBool32,"depthBiasEnable")
RASTER_SCALAR(CmdSetRasterizerDiscardEnable,VkBool32,"rasterizerDiscardEnable")
RASTER_SCALAR(CmdSetStencilTestEnable,VkBool32,"stencilTestEnable")
RASTER_SCALAR(CmdSetLineWidth,float,"lineWidth")
#undef RASTER_SCALAR
VKAPI_ATTR void VKAPI_CALL CmdSetDepthBias(VkCommandBuffer cb,float constant,float clamp,float slope){
    if(active){std::lock_guard lock(mutex);commands[cb].raster["depthBias"]="["+std::to_string(constant)+","+std::to_string(clamp)+","+std::to_string(slope)+"]";}
    dev(cb)->CmdSetDepthBias(cb,constant,clamp,slope);
}
VKAPI_ATTR void VKAPI_CALL CmdSetDepthBounds(VkCommandBuffer cb,float lo,float hi){
    if(active){std::lock_guard lock(mutex);commands[cb].raster["depthBounds"]="["+std::to_string(lo)+","+std::to_string(hi)+"]";}
    dev(cb)->CmdSetDepthBounds(cb,lo,hi);
}
void rasterViewports(VkCommandBuffer cb,unsigned first,unsigned n,const VkViewport* values,bool count){
    if(!active)return;std::lock_guard lock(mutex);auto& c=commands[cb];
    if(count)c.raster["viewportCount"]=std::to_string(n);
    for(unsigned i=0;i<n&&first+i<32;++i){const auto& v=values[i];c.viewports[first+i]="["+std::to_string(v.x)+","+std::to_string(v.y)+","+std::to_string(v.width)+","+std::to_string(v.height)+","+std::to_string(v.minDepth)+","+std::to_string(v.maxDepth)+"]";}
}
void rasterScissors(VkCommandBuffer cb,unsigned first,unsigned n,const VkRect2D* values,bool count){
    if(!active)return;std::lock_guard lock(mutex);auto& c=commands[cb];
    if(count)c.raster["scissorCount"]=std::to_string(n);
    for(unsigned i=0;i<n&&first+i<32;++i){const auto& v=values[i];c.scissors[first+i]="["+std::to_string(v.offset.x)+","+std::to_string(v.offset.y)+","+std::to_string(v.extent.width)+","+std::to_string(v.extent.height)+"]";}
}
VKAPI_ATTR void VKAPI_CALL CmdSetViewport(VkCommandBuffer cb,unsigned first,unsigned n,const VkViewport* v){rasterViewports(cb,first,n,v,false);dev(cb)->CmdSetViewport(cb,first,n,v);}
VKAPI_ATTR void VKAPI_CALL CmdSetScissor(VkCommandBuffer cb,unsigned first,unsigned n,const VkRect2D* v){rasterScissors(cb,first,n,v,false);dev(cb)->CmdSetScissor(cb,first,n,v);}
VKAPI_ATTR void VKAPI_CALL CmdSetViewportWithCount(VkCommandBuffer cb,unsigned n,const VkViewport* v){rasterViewports(cb,0,n,v,true);dev(cb)->CmdSetViewportWithCount(cb,n,v);}
VKAPI_ATTR void VKAPI_CALL CmdSetScissorWithCount(VkCommandBuffer cb,unsigned n,const VkRect2D* v){rasterScissors(cb,0,n,v,true);dev(cb)->CmdSetScissorWithCount(cb,n,v);}
VKAPI_ATTR void VKAPI_CALL CmdPushConstants(VkCommandBuffer cb,VkPipelineLayout l,VkShaderStageFlags stage,unsigned offset,unsigned n,const void* values){
    if(active){std::lock_guard lock(mutex);auto& c=commands[cb];
        if((stage&VK_SHADER_STAGE_COMPUTE_BIT)&&offset<256&&n<=256-offset){
            std::memcpy(c.computePush.data()+offset,values,n);
            std::fill_n(c.computePushValid.data()+offset,n,1);
        }
        if(!commands[cb].usedSets.empty()&&records<maxRecords)emit(head("push")+",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(commands[cb].generation)+",\"layout\":"+h(l)+",\"stages\":"+std::to_string(stage)+",\"offset\":"+std::to_string(offset)+",\"size\":"+std::to_string(n)+",\"data\":"+quote(hexBytes(values,(std::min)(n,256u)).c_str()));}
    dev(cb)->CmdPushConstants(cb,l,stage,offset,n,values);
}
VKAPI_ATTR void VKAPI_CALL CmdBindVertexBuffers(VkCommandBuffer cb,unsigned first,unsigned n,const VkBuffer* b,const VkDeviceSize* offsets){
    if(!active){dev(cb)->CmdBindVertexBuffers(cb,first,n,b,offsets);return;}
    {std::lock_guard lock(mutex);for(unsigned i=0;i<n;++i)commands[cb].vertices[first+i]=bufferBinding(b[i],offsets[i],VK_WHOLE_SIZE);}
    dev(cb)->CmdBindVertexBuffers(cb,first,n,b,offsets);
}
VKAPI_ATTR void VKAPI_CALL CmdBindVertexBuffers2(VkCommandBuffer cb,unsigned first,unsigned n,const VkBuffer* b,const VkDeviceSize* offsets,const VkDeviceSize* sizes,const VkDeviceSize* strides){
    if(!active){dev(cb)->CmdBindVertexBuffers2(cb,first,n,b,offsets,sizes,strides);return;}
    {std::lock_guard lock(mutex);for(unsigned i=0;i<n;++i){
        commands[cb].vertices[first+i]=bufferBinding(b[i],offsets[i],sizes?sizes[i]:VK_WHOLE_SIZE);
        if(strides)commands[cb].strides[first+i]=strides[i];}}
    dev(cb)->CmdBindVertexBuffers2(cb,first,n,b,offsets,sizes,strides);
}
VKAPI_ATTR void VKAPI_CALL CmdBindIndexBuffer(VkCommandBuffer cb,VkBuffer b,VkDeviceSize o,VkIndexType t){
    if(!active){dev(cb)->CmdBindIndexBuffer(cb,b,o,t);return;}
    {std::lock_guard lock(mutex);commands[cb].index=bufferBinding(b,o,VK_WHOLE_SIZE);commands[cb].indexType=t;}
    dev(cb)->CmdBindIndexBuffer(cb,b,o,t);
}
void enterPass(VkCommandBuffer cb){
    if(!active)return;
    std::lock_guard lock(mutex);auto& c=commands[cb];++c.pass;c.passBuffers.clear();
    // The pipeline is only known at draw time. These are explicitly requested
    // pre-pass bytes, not proof of consumption; match cb/generation/pass to draw.
    if(captureBeforePass&&!inventoryOnly&&requestedPass==c.pass&&!c.scope&&c.tag&&
       same(c.tag,currentTag())&&capturePair.accept(c.tag->tick,c.tag->backendFrame,
           circlevr::stereo_seq::eyeIndex(c.tag->eye))){
        std::vector<Binding> input;
        for(const auto& [buffer,resource]:buffers){
            const auto key=handleValue(buffer);
            if(requestedRanges.contains(key)&&requestedGenerations.contains(key)&&
               requestedGenerations.at(key)==resource.generation)
                input.push_back({VK_DESCRIPTOR_TYPE_MAX_ENUM,buffer,0,resource.size,resource.generation});
        }
        copyBuffers(dev(cb),cb,c,input,"before_render_pass");
    }
    c.scope=true;
}
void exitPass(VkCommandBuffer cb){if(!active)return;std::lock_guard lock(mutex);auto& c=commands[cb];c.scope=false;rasterImageExit(cb,c);copyBuffers(dev(cb),cb,c,c.passBuffers,"after_render_pass");c.passBuffers.clear();}
void attachmentMetadata(VkCommandBuffer cb,const VkRenderPassBeginInfo* p,const VkRenderingInfo* dynamic=nullptr){
    if(!active||(temporalActive&&!rasterImageMode))return;
    std::lock_guard lock(mutex);auto& c=commands[cb];
    c.attachmentsKnown=false;c.attachmentImages.clear();
    if(!c.tag||!same(c.tag,currentTag()))return;
    if(capturePair.token&&(c.tag->tick!=capturePair.token||
        c.tag->backendFrame!=capturePair.leftFrame+circlevr::stereo_seq::eyeIndex(c.tag->eye)))return;
    const auto extent=p?p->renderArea.extent:(dynamic?dynamic->renderArea.extent:VkExtent2D{});
    c.renderWidth=extent.width;c.renderHeight=extent.height;
    std::vector<AttachmentRecord> list;
    bool known=false;
    if(p){
        const auto f=framebuffers.find(p->framebuffer);
        if(f!=framebuffers.end()&&!f->second.empty()){list=f->second;known=true;}
        // Imageless framebuffer attachments belong to this invocation.
        for(auto ext=static_cast<const VkBaseInStructure*>(p->pNext);ext;ext=ext->pNext)
            if(ext->sType==VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO){
                const auto* info=reinterpret_cast<const VkRenderPassAttachmentBeginInfo*>(ext);
                if(info->attachmentCount<=32){list.clear();known=true;
                    for(unsigned i=0;i<info->attachmentCount;++i){const auto view=imageViews.find(info->pAttachments[i]);
                        list.push_back({info->pAttachments[i],view==imageViews.end()?0:view->second.generation});}}
            }
    }else if(dynamic&&dynamic->colorAttachmentCount<=16){
        known=true;
        const auto add=[&](VkImageView v){const auto view=imageViews.find(v);list.push_back({v,view==imageViews.end()?0:view->second.generation});};
        for(unsigned i=0;i<dynamic->colorAttachmentCount;++i){add(dynamic->pColorAttachments[i].imageView);add(dynamic->pColorAttachments[i].resolveImageView);}
        if(dynamic->pDepthAttachment){add(dynamic->pDepthAttachment->imageView);add(dynamic->pDepthAttachment->resolveImageView);}
        if(dynamic->pStencilAttachment){add(dynamic->pStencilAttachment->imageView);add(dynamic->pStencilAttachment->resolveImageView);}
    }
    c.attachmentsKnown=known;
    for(const auto& a:list){
        if(!a.view)continue;
        const auto v=imageViews.find(a.view);
        if(v==imageViews.end()||!a.generation||v->second.generation!=a.generation)c.attachmentsKnown=false;
        else c.attachmentImages.insert(v->second.image);
    }
    c.attachmentSignature=2166136261u;
    for(const auto& a:list){const auto v=imageViews.find(a.view);const auto format=v==imageViews.end()?0u:unsigned(v->second.format);c.attachmentSignature=(c.attachmentSignature^format)*16777619u;}
    auto line=head("render_attachments")+tagText(c.tag)+",\"cb\":"+h(cb)+
        ",\"commandGeneration\":"+std::to_string(c.generation)+",\"pass\":"+std::to_string(c.pass)+
        ",\"known\":"+(known?"true":"false")+",\"dynamic\":"+(dynamic?"true":"false")+
        ",\"renderPass\":"+h(p?p->renderPass:VK_NULL_HANDLE)+",\"attachments\":[";
    for(size_t i=0;i<list.size();++i){if(i)line+=",";const auto& a=list[i];const auto v=imageViews.find(a.view);
        const bool live=v!=imageViews.end()&&a.generation&&v->second.generation==a.generation;
        line+="{\"index\":"+std::to_string(i)+",\"imageView\":"+h(a.view)+",\"generation\":"+std::to_string(a.generation)+",\"live\":"+(live?"true":"false");
        if(live)line+=",\"image\":"+h(v->second.image)+",\"format\":"+std::to_string(v->second.format)+",\"aspectMask\":"+std::to_string(v->second.range.aspectMask);
        line+="}";
    }
    c.attachments="{\"known\":"+std::string(known?"true":"false")+",\"views\":"+line.substr(line.find("\"attachments\":[")+14)+"]}";
    emit(line+"]");
}
VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass(VkCommandBuffer cb,const VkRenderPassBeginInfo* p,VkSubpassContents s){enterPass(cb);attachmentMetadata(cb,p);dev(cb)->CmdBeginRenderPass(cb,p,s);}
VKAPI_ATTR void VKAPI_CALL CmdEndRenderPass(VkCommandBuffer cb){dev(cb)->CmdEndRenderPass(cb);exitPass(cb);}
VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass2(VkCommandBuffer cb,const VkRenderPassBeginInfo* p,const VkSubpassBeginInfo* s){enterPass(cb);attachmentMetadata(cb,p);dev(cb)->CmdBeginRenderPass2(cb,p,s);}
VKAPI_ATTR void VKAPI_CALL CmdEndRenderPass2(VkCommandBuffer cb,const VkSubpassEndInfo* s){dev(cb)->CmdEndRenderPass2(cb,s);exitPass(cb);}
VKAPI_ATTR void VKAPI_CALL CmdBeginRendering(VkCommandBuffer cb,const VkRenderingInfo* p){enterPass(cb);attachmentMetadata(cb,nullptr,p);dev(cb)->CmdBeginRendering(cb,p);}
VKAPI_ATTR void VKAPI_CALL CmdEndRendering(VkCommandBuffer cb){dev(cb)->CmdEndRendering(cb);exitPass(cb);}
VKAPI_ATTR void VKAPI_CALL CmdDraw(VkCommandBuffer cb,unsigned n,unsigned inst,unsigned first,unsigned fi){if(active)draw(dev(cb),cb,"draw",false,",\"count\":"+std::to_string(n)+",\"instances\":"+std::to_string(inst)+",\"firstVertex\":"+std::to_string(first)+",\"firstInstance\":"+std::to_string(fi));dev(cb)->CmdDraw(cb,n,inst,first,fi);}
VKAPI_ATTR void VKAPI_CALL CmdDrawIndexed(VkCommandBuffer cb,unsigned n,unsigned inst,unsigned first,int vo,unsigned fi){if(active)draw(dev(cb),cb,"draw_indexed",false,",\"count\":"+std::to_string(n)+",\"instances\":"+std::to_string(inst)+",\"firstIndex\":"+std::to_string(first)+",\"vertexOffset\":"+std::to_string(vo)+",\"firstInstance\":"+std::to_string(fi));dev(cb)->CmdDrawIndexed(cb,n,inst,first,vo,fi);}
Binding indirect(VkBuffer b,VkDeviceSize o,VkDeviceSize n){std::lock_guard lock(mutex);return bufferBinding(b,o,n);}
#define INDIRECT(F) \
 VKAPI_ATTR void VKAPI_CALL F(VkCommandBuffer cb,VkBuffer b,VkDeviceSize o,unsigned n,unsigned stride){ \
 if(active)draw(dev(cb),cb,#F,false,",\"indirect\":"+h(b)+",\"offset\":"+std::to_string(o)+",\"count\":"+std::to_string(n)+",\"stride\":"+std::to_string(stride),{indirect(b,o,std::uint64_t(n)*stride)});dev(cb)->F(cb,b,o,n,stride);}
INDIRECT(CmdDrawIndirect)
INDIRECT(CmdDrawIndexedIndirect)
#undef INDIRECT
#define INDIRECT_COUNT(F) \
 VKAPI_ATTR void VKAPI_CALL F(VkCommandBuffer cb,VkBuffer b,VkDeviceSize o,VkBuffer count,VkDeviceSize co,unsigned n,unsigned stride){ \
 if(active)draw(dev(cb),cb,#F,false,",\"indirect\":"+h(b)+",\"offset\":"+std::to_string(o)+",\"countBuffer\":"+h(count)+",\"countOffset\":"+std::to_string(co)+",\"maxCount\":"+std::to_string(n)+",\"stride\":"+std::to_string(stride),{indirect(b,o,std::uint64_t(n)*stride),indirect(count,co,4)});dev(cb)->F(cb,b,o,count,co,n,stride);}
INDIRECT_COUNT(CmdDrawIndirectCount)
INDIRECT_COUNT(CmdDrawIndexedIndirectCount)
#undef INDIRECT_COUNT
VKAPI_ATTR void VKAPI_CALL CmdDispatch(VkCommandBuffer cb,unsigned x,unsigned y,unsigned z){if(active)draw(dev(cb),cb,"dispatch",true,",\"groups\":["+std::to_string(x)+","+std::to_string(y)+","+std::to_string(z)+"]");lightingDispatch(cb,false);dev(cb)->CmdDispatch(cb,x,y,z);lightingDispatch(cb,true);if(active)finishDispatch(dev(cb),cb);}
VKAPI_ATTR void VKAPI_CALL CmdDispatchIndirect(VkCommandBuffer cb,VkBuffer b,VkDeviceSize o){if(active)draw(dev(cb),cb,"dispatch_indirect",true,",\"indirect\":"+h(b)+",\"offset\":"+std::to_string(o),{indirect(b,o,12)});lightingDispatch(cb,false);dev(cb)->CmdDispatchIndirect(cb,b,o);lightingDispatch(cb,true);if(active)finishDispatch(dev(cb),cb);}
VKAPI_ATTR void VKAPI_CALL CmdExecuteCommands(VkCommandBuffer cb,unsigned n,const VkCommandBuffer* children){
    if(!active){dev(cb)->CmdExecuteCommands(cb,n,children);return;}
    {std::lock_guard lock(mutex);auto& c=commands[cb];for(unsigned i=0;i<n;++i){auto& child=commands[children[i]];c.children.emplace_back(children[i],child.generation);c.passBuffers.insert(c.passBuffers.end(),child.passBuffers.begin(),child.passBuffers.end());if(active)emit(head("execute")+",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(c.generation)+",\"child\":"+h(children[i])+",\"childGeneration\":"+std::to_string(child.generation));}}
    dev(cb)->CmdExecuteCommands(cb,n,children);
}
void submit(VkQueue q,VkCommandBuffer cb,std::set<VkCommandBuffer>& seen){
    if(!seen.insert(cb).second)return;
    auto& c=commands[cb];bool changed=false;
    for(auto [s,version]:c.usedSets){auto it=sets.find(s);if(it==sets.end()||it->second.version!=version)changed=true;}
    if(active||c.temporalCaptured||!c.usedSets.empty())emit(head("submit")+",\"queue\":"+h(q)+",\"cb\":"+h(cb)+",\"commandGeneration\":"+std::to_string(c.generation)+tagText(c.tag)+
        ",\"submitNumber\":"+std::to_string(++c.submitted)+",\"descriptorChanged\":"+(changed?"true":"false")+
        ",\"tagMismatch\":"+(c.mismatch?"true":"false"));
    for(auto [child,generation]:c.children){auto it=commands.find(child);if(it!=commands.end()&&it->second.generation==generation)submit(q,child,seen);}
}
VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue q,unsigned n,const VkSubmitInfo* p,VkFence f){
    {std::lock_guard lock(mutex);std::set<VkCommandBuffer> seen;for(unsigned i=0;i<n;++i)for(unsigned j=0;j<p[i].commandBufferCount;++j)submit(q,p[i].pCommandBuffers[j],seen);}
    auto r=dev(q)->QueueSubmit(q,n,p,f);if(r!=VK_SUCCESS){std::lock_guard lock(mutex);emit(head("submit_error")+",\"result\":"+std::to_string(r));}return r;
}
VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit2(VkQueue q,unsigned n,const VkSubmitInfo2* p,VkFence f){
    {std::lock_guard lock(mutex);std::set<VkCommandBuffer> seen;for(unsigned i=0;i<n;++i)for(unsigned j=0;j<p[i].commandBufferInfoCount;++j)submit(q,p[i].pCommandBufferInfos[j].commandBuffer,seen);}
    auto r=dev(q)->QueueSubmit2(q,n,p,f);if(r!=VK_SUCCESS){std::lock_guard lock(mutex);emit(head("submit_error")+",\"result\":"+std::to_string(r));}return r;
}
}
bool enabled(){static const bool v=[] {char b[8]{};return GetEnvironmentVariableA("CIRCLE_GPU_BINDING_AUDIT",b,8)==1&&b[0]=='1';}();return circlevr::vkcore::loggingEnabled()&&v;}
bool temporalEnabled(){static const bool on=[] {char b[8]{};return GetEnvironmentVariableA("CIRCLE_TEMPORAL_IMAGE_AUDIT",b,8)==1&&b[0]=='1';}();return on&&enabled();}
void temporalImages(const circle::fg::Capture& c,const circle::fg::Resource& input,
                    const circle::fg::Resource& output,bool after,int result){
    if(!temporalEnabled())return;
    std::lock_guard lock(mutex);temporalCapture(c,input,output,after,result);
}
bool recording(){return active.load(std::memory_order_relaxed);}
PFN_vkVoidFunction findHook(const char* name){
    if(!enabled())return nullptr;
#define HOOK(n) if(!strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(&n);
    AUDIT_FUNCTIONS(HOOK)
#undef HOOK
#define ALIAS(n) if(!strcmp(name,"vk" #n "KHR"))return reinterpret_cast<PFN_vkVoidFunction>(&n);
    ALIAS(CmdBeginRenderPass2) ALIAS(CmdEndRenderPass2) ALIAS(CmdBeginRendering) ALIAS(CmdEndRendering)
    ALIAS(QueueSubmit2) ALIAS(CmdDrawIndirectCount) ALIAS(CmdDrawIndexedIndirectCount)
    ALIAS(GetBufferDeviceAddress)
    ALIAS(UpdateDescriptorSetWithTemplate)
#undef ALIAS
    if(!strcmp(name,"vkCmdBindVertexBuffers2EXT"))return reinterpret_cast<PFN_vkVoidFunction>(&CmdBindVertexBuffers2);
#define RASTER_ALIAS(n) if(!strcmp(name,"vk" #n "EXT"))return reinterpret_cast<PFN_vkVoidFunction>(&n);
    RASTER_ALIAS(CmdSetViewportWithCount) RASTER_ALIAS(CmdSetScissorWithCount) RASTER_ALIAS(CmdSetCullMode) RASTER_ALIAS(CmdSetFrontFace)
    RASTER_ALIAS(CmdSetDepthTestEnable) RASTER_ALIAS(CmdSetDepthWriteEnable) RASTER_ALIAS(CmdSetDepthCompareOp) RASTER_ALIAS(CmdSetDepthBoundsTestEnable)
    RASTER_ALIAS(CmdSetDepthBiasEnable) RASTER_ALIAS(CmdSetRasterizerDiscardEnable) RASTER_ALIAS(CmdSetStencilTestEnable)
#undef RASTER_ALIAS
    return nullptr;
}
void deviceCreated(circlevr::vkcore::DeviceData& data,PFN_vkVoidFunction (*nextHook)(const char*)){
    if(!enabled())return;
    auto d=std::make_unique<Device>();d->d=&data;d->game=data.instance->isGame;
    auto next=[&](const char* n){auto p=nextHook(n);return p?p:data.nextGetDeviceProcAddr(data.device,n);};
#define LOAD(n) d->n=reinterpret_cast<PFN_vk##n>(next("vk" #n));
    AUDIT_FUNCTIONS(LOAD)
#undef LOAD
#define FALLBACK(n) if(!d->n)d->n=reinterpret_cast<PFN_vk##n>(data.nextGetDeviceProcAddr(data.device,"vk" #n "KHR"));
    FALLBACK(CmdBeginRenderPass2) FALLBACK(CmdEndRenderPass2) FALLBACK(CmdBeginRendering) FALLBACK(CmdEndRendering)
    FALLBACK(QueueSubmit2) FALLBACK(CmdDrawIndirectCount) FALLBACK(CmdDrawIndexedIndirectCount)
    FALLBACK(UpdateDescriptorSetWithTemplate)
    FALLBACK(GetBufferDeviceAddress)
#undef FALLBACK
    if(!d->CmdBindVertexBuffers2)d->CmdBindVertexBuffers2=reinterpret_cast<PFN_vkCmdBindVertexBuffers2>(data.nextGetDeviceProcAddr(data.device,"vkCmdBindVertexBuffers2EXT"));
#define RASTER_FALLBACK(n) if(!d->n)d->n=reinterpret_cast<PFN_vk##n>(next("vk" #n "EXT"));
    RASTER_FALLBACK(CmdSetViewportWithCount) RASTER_FALLBACK(CmdSetScissorWithCount) RASTER_FALLBACK(CmdSetCullMode) RASTER_FALLBACK(CmdSetFrontFace)
    RASTER_FALLBACK(CmdSetDepthTestEnable) RASTER_FALLBACK(CmdSetDepthWriteEnable) RASTER_FALLBACK(CmdSetDepthCompareOp) RASTER_FALLBACK(CmdSetDepthBoundsTestEnable)
    RASTER_FALLBACK(CmdSetDepthBiasEnable) RASTER_FALLBACK(CmdSetRasterizerDiscardEnable) RASTER_FALLBACK(CmdSetStencilTestEnable)
#undef RASTER_FALLBACK
    d->copy=reinterpret_cast<PFN_vkCmdCopyBuffer>(data.nextGetDeviceProcAddr(data.device,"vkCmdCopyBuffer"));
    if(d->game){std::lock_guard lock(mutex);for(unsigned i=0;i<snapshotCapacity;++i){
        auto s=allocateSnapshot(d.get());if(!s.dst)break;spareSnapshots.push_back(s);
    }}
    {std::lock_guard lock(deviceMutex);devices[*reinterpret_cast<void**>(data.device)]=std::move(d);}
    if(data.instance->isGame&&temporalEnabled()){
        auto* owner=dev(data.device); std::lock_guard lock(mutex);
        char requested[16]{};
        const auto length=GetEnvironmentVariableA("CIRCLE_TEMPORAL_IMAGE_PAIRS",requested,sizeof(requested));
        unsigned pairs=2;
        if(length&&length<sizeof(requested)){
            unsigned parsed{};char extra{};
            if(sscanf_s(requested,"%u%c",&parsed,&extra,1u)==1&&parsed>=2&&parsed<=32)pairs=parsed;
        }
        for(unsigned i=0;i<pairs*8;++i){auto slot=allocateSnapshot(owner,512ull*512*8);if(!slot.dst)break;imageSpares.push_back(slot);}
        // Dedicated bounded volume slots; ordinary 2D copies use the small slots first.
        for(unsigned i=0;i<48;++i){auto slot=allocateSnapshot(owner,16ull*1024*1024);if(!slot.dst)break;imageSpares.push_back(slot);}
        circlevr::vkcore::logf("temporal-images: reserved %zu immutable slots (%zu complete pairs), requested %u pairs; raster sampled-input capture supported",imageSpares.size(),imageSpares.size()/8,pairs);
    }
    circlevr::vkcore::logf("motor-gpu-audit: targeted capture; preallocated 64 KiB slots, explicit ranges/generations required; inventory performs no copies");
}
void deviceDestroyed(VkDevice v){
    if(!enabled())return;auto* d=dev(v);if(!d)return;
    d->d->vk.DeviceWaitIdle(v);
    std::lock_guard lock(mutex);
    for(auto* list:{&snapshots,&spareSnapshots,&imageSpares})for(auto& s:*list)if(s.device==d){
        if(s.mapped)d->d->vk.UnmapMemory(v,s.memory);
        if(s.event)d->d->vk.DestroyEvent(v,s.event,nullptr);
        if(s.dst)d->d->vk.DestroyBuffer(v,s.dst,nullptr);
        if(s.memory)d->d->vk.FreeMemory(v,s.memory,nullptr);
        s.device=nullptr;s.mapped=nullptr;
    }
    {std::lock_guard deviceLock(deviceMutex);devices.erase(*reinterpret_cast<void**>(v));}
}
void placement(std::uint64_t token,unsigned frame,int eye,std::uintptr_t model,const char* status,float positionDelta,float axisDelta){
    if(!recording())return;std::lock_guard lock(mutex);
    emit(head("npc_placement")+",\"token\":"+std::to_string(token)+",\"frame\":"+std::to_string(frame)+",\"eye\":"+std::to_string(eye)+
        ",\"model\":"+h(model)+",\"status\":"+quote(status)+",\"positionDelta\":"+std::to_string(positionDelta)+",\"axisDelta\":"+std::to_string(axisDelta));
}
void projection(std::uint64_t token,unsigned frame,int eye,const void* bytes,unsigned size){
    if(!recording()||!bytes||size!=0x980||eye<0||eye>1)return;
    std::lock_guard lock(mutex);
    emit(head("cpu_view_projection")+",\"token\":"+std::to_string(token)+",\"frame\":"+std::to_string(frame)+
        ",\"eye\":"+std::to_string(eye)+",\"baseOffset\":100240,\"bytes\":"+quote(hexBytes(bytes,size).c_str()));
}
void npc(std::uint64_t token,unsigned frame,int eye,std::uintptr_t model,std::uintptr_t owner,unsigned index,int joint,
         const char* asset,const char* instance,std::uint64_t flags,const void* bits,unsigned count,
         const float* matrix,const float* previousMatrix,const void* skinBytes,unsigned skinSize){
    if(!recording())return;std::lock_guard lock(mutex);
    emit(head("npc")+ ",\"token\":"+std::to_string(token)+",\"frame\":"+std::to_string(frame)+",\"eye\":"+std::to_string(eye)+
        ",\"model\":"+h(model)+",\"committed\":"+h(owner)+",\"index\":"+std::to_string(index)+",\"joint\":"+std::to_string(joint)+
        ",\"asset\":"+quote(asset)+",\"instance\":"+quote(instance)+",\"renderFlags\":"+h(flags)+",\"surfaceCount\":"+std::to_string(count)+
        ",\"cpuMatrix\":"+quote(hexBytes(matrix,64).c_str())+
        ",\"cpuPreviousMatrix\":"+quote(previousMatrix?hexBytes(previousMatrix,64).c_str():"")+
        ",\"nativeSkinDescriptor\":"+quote(skinBytes?hexBytes(skinBytes,skinSize).c_str():"")+
        ",\"surfaceBits\":"+quote(bits?hexBytes(bits,count*48).c_str():""));
}
void joints(std::uint64_t token,unsigned frame,int eye,std::uintptr_t source,int current,int previous,const void* desc,unsigned size,bool reused,int slot,std::uintptr_t caller){
    if(!recording())return;std::lock_guard lock(mutex);
    emit(head("joint_selection")+",\"token\":"+std::to_string(token)+",\"frame\":"+std::to_string(frame)+",\"eye\":"+std::to_string(eye)+
        ",\"source\":"+h(source)+",\"current\":"+std::to_string(current)+",\"previous\":"+std::to_string(previous)+
        ",\"reused\":"+(reused?"true":"false")+",\"slot\":"+std::to_string(slot)+",\"caller\":"+h(caller)+
        ",\"descriptor\":"+quote(hexBytes(desc,size).c_str()));
}
void poll(){
    if(!enabled()||writerBusy)return;
    if(polling.test_and_set())return;
    struct PollRelease {~PollRelease(){polling.clear();}} release;
    const auto dir=circlevr::vkcore::logDirectory();
    const auto request=dir+L"\\gpu-binding-audit.request";
    const auto inventoryRequest=dir+L"\\gpu-binding-inventory.request";
    struct Payload {std::wstring path;std::vector<unsigned char> bytes;};
    std::vector<Payload> payloads;std::string batch;
    {
        std::lock_guard lock(mutex);
        temporalRequest(dir);
        if(active&&(GetTickCount64()>=deadline||records>=maxRecords||truncated)){
            active=false;temporalActive=false;emit(head("stop")+",\"reason\":\"bounded-window\"");circlevr::vkcore::logf("motor-gpu-audit: capture %u recording stopped; waiting for GPU events, incomplete captures remain explicit",session);
        }
        const bool inventory=GetFileAttributesW(inventoryRequest.c_str())!=INVALID_FILE_ATTRIBUTES;
        if(!active&&((inventory&&DeleteFileW(inventoryRequest.c_str()))||
            (GetFileAttributesW(request.c_str())!=INVALID_FILE_ATTRIBUTES&&DeleteFileW(request.c_str())))){
            temporalActive=false;
            ++session;records=0;sessionTextBytes=0;ownershipBytes=0;addressBytes=0;truncated=false;requestedRanges.clear();
            requestedGenerations.clear();inventoryOnly=inventory;capturePair={};sampledDraws={};rasterDraws={};capturedRanges.clear();
            if(!pendingCopies)watchedSets.clear();
            // Per-request limits; destination slots remain unique and retained
            // for the device lifetime, independently of these counters.
            phaseCopies={};phaseBytes={};eyeBytes={};
            requestedPipeline=0;requestedPass=0;physicalRanges=false;captureViewConstants=false;captureBeforePass=false;captureSkinningInputs=false;inventoryNpcConsumers=false;pairedNpcConsumers=false;captureWidth=0;captureHeight=0;loggedDescriptorUpdates.clear();
            captureMvpIndexPlusOne=0;
            FILE* selection{};
            if(!_wfopen_s(&selection,(dir+L"\\gpu-binding-pipeline.txt").c_str(),L"r")&&selection){
                unsigned long long pipeline{};unsigned physical{},pass{},constants{},before{},skinning{},selectedBatch{},npcConsumers{},paired{},width{},height{},attachmentSignature{};
                if(fscanf_s(selection,"%llx %u %u %u %u %u %u %u %u %u %u %u %u",&pipeline,&physical,&pass,&constants,&before,&skinning,&selectedBatch,&npcConsumers,&paired,&width,&height,&attachmentSignature,&captureMvpIndexPlusOne)>=2){
                    requestedPipeline=pipeline;physicalRanges=physical==1&&pipeline!=0;
                    requestedPass=pipeline?pass:0;
                    captureViewConstants=pipeline&&pass&&constants==1;
                    captureBeforePass=pipeline&&pass&&before==1;
                    captureSkinningInputs=pipeline&&skinning==1;pairedNpcConsumers=pipeline&&skinning==1&&paired==1;skinningBatch=selectedBatch;inventoryNpcConsumers=npcConsumers==1;captureWidth=width;captureHeight=height;captureAttachmentSignature=attachmentSignature;
                }
                fclose(selection);
            }
            // A selected shader can occur after the first 32 draws. Keep the
            // pipeline filter for metadata-only requests as well, otherwise
            // its actual current/previous inputs can never be inventoried.
            if(inventoryOnly)physicalRanges=false;
            FILE* f{};if(!_wfopen_s(&f,(dir+L"\\gpu-binding-ranges.txt").c_str(),L"r")&&f){
                unsigned long long buffer{},off{},size{},generation{};unsigned rangeCount=0;
                char input[256]{},extra{};
                while(fgets(input,sizeof(input),f)){
                    if(sscanf_s(input,"%llx %llu %llu %llu %c",&buffer,&off,&size,&generation,&extra,1u)!=4){
                        requestedRanges.clear();break;
                    }
                    bool found=false;for(const auto& [handle,b]:buffers)
                        if(handleValue(handle)==buffer&&b.generation==generation&&targetedRange(off,size)&&
                           range(b.size,off,size,maxRange).complete){found=true;break;}
                    if(!found||++rangeCount>192){requestedRanges.clear();break;}
                    requestedRanges[buffer].push_back({off,size,true,true});requestedGenerations[buffer]=generation;
                }
                fclose(f);
            }
            std::size_t pairSlots=captureViewConstants?2:0;
            for(const auto& [buffer,ranges]:requestedRanges)pairSlots+=(captureBeforePass?4:2)*ranges.size();
            // Reserve the maximum per-phase stereo allowance, including dispatch post-copies.
            pairSlots=(std::max)(pairSlots,std::size_t(pairedNpcConsumers?2*4*384:2*4*64));
            if(!inventoryOnly&&pairSlots>spareSnapshots.size()){
                emit(head("capture_rejected")+",\"reason\":\"insufficient-stereo-snapshot-slots\",\"required\":"+
                    std::to_string(pairSlots)+",\"available\":"+std::to_string(spareSnapshots.size()));
                circlevr::vkcore::logf("motor-gpu-audit: pair rejected before copies; needs %zu immutable slots, %zu remain",pairSlots,spareSnapshots.size());
            }else if(!inventoryOnly&&requestedRanges.empty()){
                emit(head("capture_rejected")+",\"reason\":\"missing-or-invalid-target-ranges\"");
                circlevr::vkcore::logf("motor-gpu-audit: capture rejected; current-run buffer generation and <=192 aligned ranges of <=64 KiB required");
            }else{
            const auto requestTag=currentTag();minimumBackend=requestTag?requestTag->backendFrame:0;
            deadline=GetTickCount64()+1000;active=true;
            emit(head("start")+",\"inventoryOnly\":"+(inventoryOnly?"true":"false")+
                ",\"requestedPass\":"+std::to_string(requestedPass)+",\"requestedPipeline\":"+h(requestedPipeline)+",\"physicalRanges\":"+(physicalRanges?"true":"false")+
                ",\"rangeLimit\":"+std::to_string(maxRange)+",\"remainingBudget\":"+std::to_string(Budget::maximum-budget.used)+",\"snapshotSlotsRemaining\":"+std::to_string(spareSnapshots.size())+",\"explicitBuffers\":"+std::to_string(requestedRanges.size()));
            for(const auto& [handle,b]:buffers)if(b.address&&(inventoryOnly||requestedRanges.contains(handleValue(handle))))
                emit(head("buffer_address")+",\"buffer\":"+h(handle)+
                    ",\"generation\":"+std::to_string(b.generation)+",\"address\":"+h(b.address)+
                    ",\"bytes\":"+std::to_string(b.size));
            // Registry enumeration is preparation, not part of the capture window.
            deadline=GetTickCount64()+1000;
            circlevr::vkcore::logf("motor-gpu-audit: capture %u started; inventory %d, single pair, <=64 draws per eye, %zu explicit buffers",session,int(inventoryOnly),requestedRanges.size());
            }
        }
        for(auto& s:snapshots){
            if(s.written||!s.device||!s.mapped)continue;
            auto& vk=s.device->d->vk;const auto device=s.device->d->device;
            if(vk.GetEventStatus(device,s.event)!=VK_EVENT_SET)continue;
            if(!s.coherent){VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};r.memory=s.memory;r.size=VK_WHOLE_SIZE;if(vk.InvalidateMappedMemoryRanges(device,1,&r)!=VK_SUCCESS)continue;}
            const auto name=L"gpu-buffer-"+std::to_wstring(s.id)+L".bin";
            Payload p{dir+L"\\"+name,std::vector<unsigned char>(s.size)};
            std::memcpy(p.bytes.data(),s.mapped,s.size);
            const auto hash=circlevr::vkcore::dump::fnv1a64(p.bytes.data(),p.bytes.size());
            emit(head("gpu_complete")+",\"captureSession\":"+std::to_string(s.session)+",\"id\":"+std::to_string(s.id)+
                ",\"bytes\":"+std::to_string(s.size)+",\"hash\":"+h(hash)+",\"file\":"+quote(("gpu-buffer-"+std::to_string(s.id)+".bin").c_str()));
            payloads.push_back(std::move(p));s.written=true;if(pendingCopies)--pendingCopies;
        }
        batch.swap(lines);
    }
    if(batch.empty()&&payloads.empty())return;
    writerBusy=true;
    std::thread([dir,batch=std::move(batch),payloads=std::move(payloads)]{
        bool ok=true;
        for(auto& p:payloads){FILE* f{};if(!circlevr::vkcore::openLogFile(&f,p.path.c_str(),L"wb")&&f){ok&=fwrite(p.bytes.data(),1,p.bytes.size(),f)==p.bytes.size();ok&=fclose(f)==0;}else ok=false;}
        // Secure CRT fopen denies sharing with the live Python/PowerShell
        // reader. That silently lost metadata after successful GPU payloads.
        // There is one writer (writerBusy); permit concurrent read handles.
        const auto path=dir+L"\\gpu-bindings.jsonl";
        HANDLE file=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE){
            size_t offset=0;
            while(offset<batch.size()){
                DWORD written{};const auto bytes=DWORD((std::min)(batch.size()-offset,size_t(16*1024*1024)));
                if(!WriteFile(file,batch.data()+offset,bytes,&written,nullptr)||!written){ok=false;break;}
                offset+=written;
            }
            ok&=CloseHandle(file)!=0;
        }else ok=false;
        if(!ok)circlevr::vkcore::logf("motor-gpu-audit: output write failed; files must be checked before accepting evidence");
        writerBusy=false;
    }).detach();
}
}
