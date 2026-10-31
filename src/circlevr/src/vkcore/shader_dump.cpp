// Shader and draw dump: configuration, the background writer, the device registry and the
// creation-time hooks (shader modules, pipelines, layouts, descriptor sets). See shader_dump.hpp.

#include "vkcore/shader_dump.hpp"

#include "vkcore/log.hpp"
#include "vkcore/shader_dump_impl.hpp"
#include "vkcore/vrs_nv.hpp"
#include "motor/secondary_cache.hpp"
#include "motor/hair_motion_patch.hpp"
#include "motor/sequential.hpp"
#include "vkcore/seq_hooks.hpp"
#include <vector>

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace circlevr::vkcore::shader_dump {

namespace {

using dump::handleText;
using dump::handleValue;
std::mutex cacheMutex;
struct CacheVariant {VkDevice device{};VkPipeline pipeline{};};
struct CacheModule {VkDevice device{};VkShaderModule module{};};
std::unordered_map<VkShaderModule,CacheModule> cacheModules;
std::unordered_map<VkPipeline,CacheVariant> cachePipelines;
std::unordered_map<VkCommandBuffer,std::uint32_t> cacheCommands;

// Descriptor writes are logged until this many lines, so a game that rewrites buffer descriptors every
// frame cannot fill the disk.
constexpr std::uint32_t kMaxSetLines = 200000;

struct Config {
    bool on = false;
    std::wstring dir;
};

const Config& config() {
    static const Config value = [] {
        Config c;
        std::wstring dir;
        if (loggingEnabled() && readEnv(L"KHARVOX_CIRCLE_DUMP_SHADERS", dir) && !dir.empty()) {
            c.on = true;
            c.dir = dir;
        }
        return c;
    }();
    return value;
}

// ---------------------------------------------------------------------------------------------------
// Writer: one thread appends queued lines to the jsonl files, so the game's threads only format.

class Writer {
public:
    void start(const std::wstring& dir) {
        static const wchar_t* const names[] = {L"\\pipelines.jsonl", L"\\layouts.jsonl", L"\\sets.jsonl",
                                               L"\\drawlog.jsonl"};
        for (int i = 0; i < 4; ++i) {
            files_[i] = circlevr::vkcore::openLogFileShared((dir + names[i]).c_str(), L"wb", _SH_DENYWR);
        }
        std::thread([this] { run(); }).detach();
    }

    void push(Stream stream, std::string lines) {
        {
            std::lock_guard lock(mutex_);
            queue_.emplace_back(stream, std::move(lines));
        }
        wake_.notify_one();
    }

private:
    void run() {
        for (;;) {
            std::deque<std::pair<Stream, std::string>> batch;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return !queue_.empty(); });
                batch.swap(queue_);
            }
            for (auto& [stream, text] : batch) {
                if (std::FILE* f = files_[static_cast<int>(stream)]) {
                    std::fwrite(text.data(), 1, text.size(), f);
                }
            }
            for (std::FILE* f : files_) {
                if (f) {
                    std::fflush(f);
                }
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::pair<Stream, std::string>> queue_;
    std::FILE* files_[4] = {};
};

Writer& writer() {
    static Writer& w = *new Writer; // never destroyed: the thread outlives static destruction
    return w;
}

// ---------------------------------------------------------------------------------------------------
// Registry

std::shared_mutex& g_devicesMutex = *new std::shared_mutex;
auto& g_devices = *new std::unordered_map<void*, DumpDevice*>;

std::mutex& g_modulesMutex = *new std::mutex;
auto& g_moduleHashes = *new std::unordered_map<std::uint64_t, std::uint64_t>; // module handle -> hash
auto& g_writtenHashes = *new std::unordered_set<std::uint64_t>;
std::atomic<std::uint32_t> g_setLines{0};

void* keyOf(void* dispatchable) {
    return *static_cast<void**>(dispatchable);
}

// Hashes SPIR-V code and writes it once as modules/<hash>.spv.
std::uint64_t storeModule(const std::uint32_t* code, std::size_t size) {
    const std::uint64_t hash = dump::fnv1a64(code, size);
    {
        std::lock_guard lock(g_modulesMutex);
        if (!g_writtenHashes.insert(hash).second) {
            return hash;
        }
    }
    const std::string name = dump::hex64(hash);
    const std::wstring path =
        config().dir + L"\\modules\\" + std::wstring(name.begin(), name.end()) + L".spv";
    if (std::FILE* f = circlevr::vkcore::openLogFileShared(path.c_str(), L"wb", _SH_DENYWR)) {
        std::fwrite(code, 1, size, f);
        std::fclose(f);
    } else {
        CIRCLE_VR_LOG("shader dump: cannot write %ls", path.c_str());
    }
    return hash;
}

std::uint64_t moduleHash(VkShaderModule module) {
    std::lock_guard lock(g_modulesMutex);
    const auto it = g_moduleHashes.find(handleValue(module));
    return it == g_moduleHashes.end() ? 0 : it->second;
}

// ---------------------------------------------------------------------------------------------------
// Creation-time hooks

VKAPI_ATTR VkResult VKAPI_CALL CreateShaderModule(VkDevice device,
                                                  const VkShaderModuleCreateInfo* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkShaderModule* pModule) {
    DumpDevice* d = deviceOf(device);
    std::vector<std::uint32_t> hairCode;
    if(d->isGame&&circle::motor::sequential::requested()&&pCreateInfo->pCode){
        const auto hash=dump::fnv1a64(pCreateInfo->pCode,pCreateInfo->codeSize);
        hairCode=circle::motor::hair_motion::patch({pCreateInfo->pCode,pCreateInfo->codeSize/4},hash);
    }
    auto shaderInfo=*pCreateInfo;
    if(!hairCode.empty()){shaderInfo.pCode=hairCode.data();shaderInfo.codeSize=hairCode.size()*4;}
    VkResult result = d->CreateShaderModule(device, &shaderInfo, pAllocator, pModule);
    if(!hairCode.empty()){
        if(result==VK_SUCCESS)logf("motor-hair-motion: native matrix reprojection enabled; opposite-eye strand history bypassed (fragment %llx)",dump::fnv1a64(pCreateInfo->pCode,pCreateInfo->codeSize));
        else {logf("motor-hair-motion: patched module rejected; using native shader");result=d->CreateShaderModule(device,pCreateInfo,pAllocator,pModule);}
    }
    if (enabled() && result == VK_SUCCESS && d->isGame && pCreateInfo->pCode) {
        const std::uint64_t hash = storeModule(pCreateInfo->pCode, pCreateInfo->codeSize);
        std::lock_guard lock(g_modulesMutex);
        g_moduleHashes[handleValue(*pModule)] = hash;
        auto code=circle::motor::secondary_cache::patch(
            {pCreateInfo->pCode,pCreateInfo->codeSize/sizeof(std::uint32_t)});
        if(!code.empty()){
            auto info=*pCreateInfo;info.pCode=code.data();info.codeSize=code.size()*4;
            VkShaderModule variant{};
            if(d->CreateShaderModule(device,&info,nullptr,&variant)==VK_SUCCESS){
                std::lock_guard cacheLock(cacheMutex);cacheModules[*pModule]={device,variant};
                logf("motor-secondary-cache: motion shader variant created original %llx",hash);
            }else circle::motor::secondary_cache::shaderFailed();
        }
    }
    return result;
}
VKAPI_ATTR void VKAPI_CALL DestroyShaderModule(VkDevice device,VkShaderModule module,const VkAllocationCallbacks* allocator){
    auto* d=deviceOf(device);
    {std::lock_guard lock(cacheMutex);auto it=cacheModules.find(module);
     if(it!=cacheModules.end()){d->DestroyShaderModule(device,it->second.module,nullptr);cacheModules.erase(it);}}
    d->DestroyShaderModule(device,module,allocator);
}
VKAPI_ATTR void VKAPI_CALL DestroyPipeline(VkDevice device,VkPipeline pipeline,const VkAllocationCallbacks* allocator){
    auto* d=deviceOf(device);
    {std::lock_guard lock(cacheMutex);auto it=cachePipelines.find(pipeline);
     if(it!=cachePipelines.end()){d->DestroyPipeline(device,it->second.pipeline,nullptr);cachePipelines.erase(it);}}
    d->DestroyPipeline(device,pipeline,allocator);
}

// One stage of a pipeline as JSON: {"stage":bits,"module":"<hash>","entry":"main",...}.
void appendStage(std::string& out, const VkPipelineShaderStageCreateInfo& stage) {
    out += "{\"stage\":" + std::to_string(stage.stage) + ",\"module\":";
    std::string module;
    if (stage.module != VK_NULL_HANDLE) {
        const std::uint64_t hash = moduleHash(stage.module);
        module = hash ? dump::hex64(hash) : "unknown:" + handleText(handleValue(stage.module));
    } else if (const auto* inlineInfo = dump::findInChain<VkShaderModuleCreateInfo>(
                   stage.pNext, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
               inlineInfo && inlineInfo->pCode) {
        module = dump::hex64(storeModule(inlineInfo->pCode, inlineInfo->codeSize));
    } else if (const auto* id = dump::findInChain<VkPipelineShaderStageModuleIdentifierCreateInfoEXT>(
                   stage.pNext, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT);
               id && id->pIdentifier) {
        module = "id:" + dump::hexBytes(id->pIdentifier, id->identifierSize);
    } else {
        module = "none";
    }
    dump::appendJsonString(out, module);
    out += ",\"entry\":";
    dump::appendJsonString(out, stage.pName ? stage.pName : "");
    if (const VkSpecializationInfo* spec = stage.pSpecializationInfo) {
        out += ",\"spec\":{\"entries\":[";
        for (std::uint32_t i = 0; i < spec->mapEntryCount; ++i) {
            const VkSpecializationMapEntry& e = spec->pMapEntries[i];
            out += (i ? ",[" : "[") + std::to_string(e.constantID) + "," + std::to_string(e.offset) + "," +
                   std::to_string(e.size) + "]";
        }
        out += "],\"data\":\"" + (spec->pData ? dump::hexBytes(spec->pData, spec->dataSize) : "") + "\"}";
    }
    out += "}";
}

void appendLibraries(std::string& out, const void* pNext) {
    const auto* libs = dump::findInChain<VkPipelineLibraryCreateInfoKHR>(
        pNext, VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR);
    if (!libs || libs->libraryCount == 0) {
        return;
    }
    out += ",\"libraries\":[";
    for (std::uint32_t i = 0; i < libs->libraryCount; ++i) {
        out += (i ? ",\"" : "\"") + handleText(handleValue(libs->pLibraries[i])) + "\"";
    }
    out += "]";
}

template <typename Info>
void logPipelines(const char* kind, std::uint32_t count, const Info* infos, const VkPipeline* pipelines) {
    std::string lines;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (pipelines[i] == VK_NULL_HANDLE) {
            continue;
        }
        const Info& info = infos[i];
        lines += "{\"pipeline\":\"" + handleText(handleValue(pipelines[i])) + "\",\"kind\":\"" + kind +
                 "\",\"layout\":\"" + handleText(handleValue(info.layout)) +
                 "\",\"flags\":" + std::to_string(info.flags) + ",\"stages\":[";
        if constexpr (std::is_same_v<Info, VkComputePipelineCreateInfo>) {
            appendStage(lines, info.stage);
        } else {
            for (std::uint32_t s = 0; s < info.stageCount; ++s) {
                if (s) {
                    lines += ",";
                }
                appendStage(lines, info.pStages[s]);
            }
        }
        lines += "]";
        if constexpr (std::is_same_v<Info, VkGraphicsPipelineCreateInfo>) {
            // Creation-time values are separate from dynamic command state.
            // Never treat an ignored static value as the effective draw state.
            lines+=",\"rasterStatic\":{";
            if(const auto* r=info.pRasterizationState){
                lines+="\"depthClampEnable\":"+std::to_string(r->depthClampEnable)+
                    ",\"rasterizerDiscardEnable\":"+std::to_string(r->rasterizerDiscardEnable)+
                    ",\"polygonMode\":"+std::to_string(r->polygonMode)+",\"cullMode\":"+std::to_string(r->cullMode)+
                    ",\"frontFace\":"+std::to_string(r->frontFace)+",\"depthBiasEnable\":"+std::to_string(r->depthBiasEnable)+
                    ",\"depthBias\":["+std::to_string(r->depthBiasConstantFactor)+","+std::to_string(r->depthBiasClamp)+","+std::to_string(r->depthBiasSlopeFactor)+"],\"lineWidth\":"+std::to_string(r->lineWidth)+",";
            }
            if(const auto* z=info.pDepthStencilState){
                lines+="\"depthTestEnable\":"+std::to_string(z->depthTestEnable)+",\"depthWriteEnable\":"+std::to_string(z->depthWriteEnable)+
                    ",\"depthCompareOp\":"+std::to_string(z->depthCompareOp)+",\"depthBoundsTestEnable\":"+std::to_string(z->depthBoundsTestEnable)+
                    ",\"depthBounds\":["+std::to_string(z->minDepthBounds)+","+std::to_string(z->maxDepthBounds)+"],\"stencilTestEnable\":"+std::to_string(z->stencilTestEnable)+",";
            }
            lines+="\"dynamicStates\":[";
            if(const auto* dy=info.pDynamicState)for(unsigned j=0;j<dy->dynamicStateCount;++j)lines+=(j?",":"")+std::to_string(dy->pDynamicStates[j]);
            lines+="]";
            if(const auto* v=info.pViewportState){
                const auto dynamic=[&](VkDynamicState state){return info.pDynamicState&&std::find(info.pDynamicState->pDynamicStates,info.pDynamicState->pDynamicStates+info.pDynamicState->dynamicStateCount,state)!=info.pDynamicState->pDynamicStates+info.pDynamicState->dynamicStateCount;};
                lines+=",\"viewportCount\":"+std::to_string(v->viewportCount)+",\"scissorCount\":"+std::to_string(v->scissorCount);
                lines+=",\"viewports\":{";
                if(v->pViewports&&!dynamic(VK_DYNAMIC_STATE_VIEWPORT)&&!dynamic(VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT))for(unsigned j=0;j<v->viewportCount;++j){const auto& x=v->pViewports[j];lines+=(j?",":"")+std::string("\"")+std::to_string(j)+"\":["+std::to_string(x.x)+","+std::to_string(x.y)+","+std::to_string(x.width)+","+std::to_string(x.height)+","+std::to_string(x.minDepth)+","+std::to_string(x.maxDepth)+"]";}
                lines+="},\"scissors\":{";
                if(v->pScissors&&!dynamic(VK_DYNAMIC_STATE_SCISSOR)&&!dynamic(VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT))for(unsigned j=0;j<v->scissorCount;++j){const auto& x=v->pScissors[j];lines+=(j?",":"")+std::string("\"")+std::to_string(j)+"\":["+std::to_string(x.offset.x)+","+std::to_string(x.offset.y)+","+std::to_string(x.extent.width)+","+std::to_string(x.extent.height)+"]";}
                lines+="}";
            }
            if(const auto* ms=info.pMultisampleState){
                lines+=",\"rasterizationSamples\":"+std::to_string(ms->rasterizationSamples)+",\"sampleShadingEnable\":"+std::to_string(ms->sampleShadingEnable)+",\"alphaToCoverageEnable\":"+std::to_string(ms->alphaToCoverageEnable)+",\"alphaToOneEnable\":"+std::to_string(ms->alphaToOneEnable);
            }
            if(const auto* blend=info.pColorBlendState){
                lines+=",\"colorBlendAttachments\":[";
                for(unsigned j=0;j<blend->attachmentCount;++j){const auto& b=blend->pAttachments[j];lines+=(j?",":"")+std::string("[")+std::to_string(b.blendEnable)+","+std::to_string(b.srcColorBlendFactor)+","+std::to_string(b.dstColorBlendFactor)+","+std::to_string(b.colorBlendOp)+","+std::to_string(b.srcAlphaBlendFactor)+","+std::to_string(b.dstAlphaBlendFactor)+","+std::to_string(b.alphaBlendOp)+","+std::to_string(b.colorWriteMask)+"]";}
                lines+="]";
            }
            lines+="},\"pipelineExtensions\":[";
            bool comma=false;for(auto ext=static_cast<const VkBaseInStructure*>(info.pNext);ext;ext=ext->pNext){if(comma)lines+=",";comma=true;lines+=std::to_string(ext->sType);}
            lines+="]";
            if(info.pVertexInputState){
                const auto& v=*info.pVertexInputState;
                lines+=",\"vertexBindings\":[";
                for(std::uint32_t b=0;b<v.vertexBindingDescriptionCount;++b){
                    const auto& x=v.pVertexBindingDescriptions[b];
                    lines+=(b?",":"")+std::string("[")+std::to_string(x.binding)+","+
                        std::to_string(x.stride)+","+std::to_string(x.inputRate)+"]";
                }
                lines+="],\"vertexAttributes\":[";
                for(std::uint32_t a=0;a<v.vertexAttributeDescriptionCount;++a){
                    const auto& x=v.pVertexAttributeDescriptions[a];
                    lines+=(a?",":"")+std::string("[")+std::to_string(x.location)+","+
                        std::to_string(x.binding)+","+std::to_string(x.format)+","+std::to_string(x.offset)+"]";
                }
                lines+="]";
            }
        }
        appendLibraries(lines, info.pNext);
        lines += "}\n";
    }
    write(Stream::Pipelines, std::move(lines));
}

VKAPI_ATTR VkResult VKAPI_CALL CreateGraphicsPipelines(VkDevice device,
                                                       VkPipelineCache cache,
                                                       std::uint32_t count,
                                                       const VkGraphicsPipelineCreateInfo* pInfos,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkPipeline* pPipelines) {
    DumpDevice* d = deviceOf(device);
    const VkResult result = d->CreateGraphicsPipelines(device, cache, count, pInfos, pAllocator, pPipelines);
    if (result >= VK_SUCCESS && d->isGame) {
        logPipelines("graphics", count, pInfos, pPipelines);
        for(unsigned i=0;i<count;++i){
            if(!pPipelines[i])continue;
            auto info=pInfos[i];
            std::vector<VkPipelineShaderStageCreateInfo> stages(info.pStages,info.pStages+info.stageCount);
            bool changed=false;
            std::lock_guard lock(cacheMutex);
            for(auto& stage:stages){
                auto it=cacheModules.find(stage.module);
                if(stage.stage==VK_SHADER_STAGE_VERTEX_BIT&&it!=cacheModules.end()){
                    stage.module=it->second.module;changed=true;
                }
            }
            if(!changed)continue;
            info.pStages=stages.data();
            info.flags&=~VK_PIPELINE_CREATE_DERIVATIVE_BIT;
            info.basePipelineHandle=VK_NULL_HANDLE;info.basePipelineIndex=-1;
            VkPipeline variant{};
            if(d->CreateGraphicsPipelines(device,cache,1,&info,nullptr,&variant)==VK_SUCCESS){
                cachePipelines[pPipelines[i]]={device,variant};
                circle::motor::secondary_cache::shaderReady();
            }else{
                if(variant)d->DestroyPipeline(device,variant,nullptr);
                circle::motor::secondary_cache::shaderFailed();
                logf("motor-secondary-cache: shader variant creation failed; correction disabled");
            }
        }
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateComputePipelines(VkDevice device,
                                                      VkPipelineCache cache,
                                                      std::uint32_t count,
                                                      const VkComputePipelineCreateInfo* pInfos,
                                                      const VkAllocationCallbacks* pAllocator,
                                                      VkPipeline* pPipelines) {
    DumpDevice* d = deviceOf(device);
    const VkResult result = d->CreateComputePipelines(device, cache, count, pInfos, pAllocator, pPipelines);
    if (result >= VK_SUCCESS && d->isGame) {
        logPipelines("compute", count, pInfos, pPipelines);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL CreatePipelineLayout(VkDevice device,
                                                    const VkPipelineLayoutCreateInfo* pInfo,
                                                    const VkAllocationCallbacks* pAllocator,
                                                    VkPipelineLayout* pLayout) {
    DumpDevice* d = deviceOf(device);
    const VkResult result = d->CreatePipelineLayout(device, pInfo, pAllocator, pLayout);
    if (result == VK_SUCCESS && d->isGame) {
        std::string line =
            "{\"pipelineLayout\":\"" + handleText(handleValue(*pLayout)) + "\",\"setLayouts\":[";
        for (std::uint32_t i = 0; i < pInfo->setLayoutCount; ++i) {
            line += (i ? ",\"" : "\"") + handleText(handleValue(pInfo->pSetLayouts[i])) + "\"";
        }
        line += "],\"pushRanges\":[";
        for (std::uint32_t i = 0; i < pInfo->pushConstantRangeCount; ++i) {
            const VkPushConstantRange& r = pInfo->pPushConstantRanges[i];
            line += (i ? ",[" : "[") + std::to_string(r.stageFlags) + "," + std::to_string(r.offset) + "," +
                    std::to_string(r.size) + "]";
        }
        line += "]}\n";
        write(Stream::Layouts, std::move(line));
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorSetLayout(VkDevice device,
                                                         const VkDescriptorSetLayoutCreateInfo* pInfo,
                                                         const VkAllocationCallbacks* pAllocator,
                                                         VkDescriptorSetLayout* pLayout) {
    DumpDevice* d = deviceOf(device);
    const VkResult result = d->CreateDescriptorSetLayout(device, pInfo, pAllocator, pLayout);
    if (result == VK_SUCCESS && d->isGame) {
        std::string line = "{\"setLayout\":\"" + handleText(handleValue(*pLayout)) +
                           "\",\"flags\":" + std::to_string(pInfo->flags) + ",\"bindings\":[";
        for (std::uint32_t i = 0; i < pInfo->bindingCount; ++i) {
            const VkDescriptorSetLayoutBinding& b = pInfo->pBindings[i];
            line += (i ? ",[" : "[") + std::to_string(b.binding) + "," + std::to_string(b.descriptorType) +
                    "," + std::to_string(b.descriptorCount) + "," + std::to_string(b.stageFlags) + "]";
        }
        line += "]}\n";
        write(Stream::Layouts, std::move(line));
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL AllocateDescriptorSets(VkDevice device,
                                                      const VkDescriptorSetAllocateInfo* pInfo,
                                                      VkDescriptorSet* pSets) {
    DumpDevice* d = deviceOf(device);
    const VkResult result = d->AllocateDescriptorSets(device, pInfo, pSets);
    if (result == VK_SUCCESS && d->isGame && g_setLines.load(std::memory_order_relaxed) < kMaxSetLines) {
        std::string lines;
        for (std::uint32_t i = 0; i < pInfo->descriptorSetCount; ++i) {
            lines += "{\"set\":\"" + handleText(handleValue(pSets[i])) + "\",\"layout\":\"" +
                     handleText(handleValue(pInfo->pSetLayouts[i])) + "\"}\n";
        }
        g_setLines.fetch_add(pInfo->descriptorSetCount, std::memory_order_relaxed);
        write(Stream::Sets, std::move(lines));
    }
    return result;
}

bool isBufferDescriptor(VkDescriptorType type) {
    return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
           type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSets(VkDevice device,
                                                std::uint32_t writeCount,
                                                const VkWriteDescriptorSet* pWrites,
                                                std::uint32_t copyCount,
                                                const VkCopyDescriptorSet* pCopies) {
    DumpDevice* d = deviceOf(device);
    d->UpdateDescriptorSets(device, writeCount, pWrites, copyCount, pCopies);
    if (!d->isGame || g_setLines.load(std::memory_order_relaxed) >= kMaxSetLines) {
        return;
    }
    std::string lines;
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < writeCount; ++i) {
        const VkWriteDescriptorSet& w = pWrites[i];
        if (!isBufferDescriptor(w.descriptorType) || !w.pBufferInfo) {
            continue;
        }
        lines += "{\"write\":\"" + handleText(handleValue(w.dstSet)) +
                 "\",\"binding\":" + std::to_string(w.dstBinding) +
                 ",\"element\":" + std::to_string(w.dstArrayElement) +
                 ",\"type\":" + std::to_string(w.descriptorType) + ",\"ranges\":[";
        for (std::uint32_t b = 0; b < w.descriptorCount; ++b) {
            const VkDescriptorBufferInfo& info = w.pBufferInfo[b];
            lines += (b ? ",[\"" : "[\"") + handleText(handleValue(info.buffer)) + "\"," +
                     std::to_string(info.offset) + "," + std::to_string(info.range) + "]";
        }
        lines += "]}\n";
        ++n;
    }
    if (n) {
        g_setLines.fetch_add(n, std::memory_order_relaxed);
        write(Stream::Sets, std::move(lines));
    }
}

PFN_vkVoidFunction findCreateHook(const char* name) {
#define CIRCLE_VR_DUMP_HOOK(fn)                                                                                    \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                  \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                    \
    }
    CIRCLE_VR_DUMP_HOOK(CreateShaderModule)
    CIRCLE_VR_DUMP_HOOK(DestroyShaderModule)
    CIRCLE_VR_DUMP_HOOK(DestroyPipeline)
    CIRCLE_VR_DUMP_HOOK(CreateGraphicsPipelines)
    CIRCLE_VR_DUMP_HOOK(CreateComputePipelines)
    CIRCLE_VR_DUMP_HOOK(CreatePipelineLayout)
    CIRCLE_VR_DUMP_HOOK(CreateDescriptorSetLayout)
    CIRCLE_VR_DUMP_HOOK(AllocateDescriptorSets)
    CIRCLE_VR_DUMP_HOOK(UpdateDescriptorSets)
#undef CIRCLE_VR_DUMP_HOOK
    return nullptr;
}

} // namespace

void cacheCommandBegin(VkCommandBuffer cb){
    if(!circle::motor::secondary_cache::requested())return;
    const auto frame=seqBackendCounter();
    std::lock_guard lock(cacheMutex);
    if(frame)cacheCommands[cb]=*frame+1;else cacheCommands.erase(cb);
}
VkPipeline cacheMotionPipeline(VkCommandBuffer cb,VkPipeline original){
    if(!circle::motor::secondary_cache::ready())return original;
    const auto now=seqBackendCounter();
    std::lock_guard lock(cacheMutex);
    auto job=cacheCommands.find(cb);auto pipeline=cachePipelines.find(original);
    if(!now||job==cacheCommands.end()||job->second!=*now+1||pipeline==cachePipelines.end()||
       !circle::motor::secondary_cache::usesHistory(job->second))return original;
    static std::atomic<unsigned> count{};const auto n=++count;
    if(n<=8||n%1800==0)logf("motor-secondary-cache: GPU previous distance 2 backend %u count %u",job->second,n);
    return pipeline->second.pipeline;
}

bool enabled() {
    return config().on;
}

PFN_vkVoidFunction findHook(const char* name) {
    if (!enabled()) {
        if(circle::motor::sequential::requested() && std::strcmp(name,"vkCreateShaderModule")==0)
            return reinterpret_cast<PFN_vkVoidFunction>(CreateShaderModule);
        return nullptr;
    }
    if (PFN_vkVoidFunction hook = findCreateHook(name)) {
        return hook;
    }
    return findRecordHook(name);
}

DumpDevice* deviceOf(void* dispatchable) {
    std::shared_lock lock(g_devicesMutex);
    const auto it = g_devices.find(keyOf(dispatchable));
    return it == g_devices.end() ? nullptr : it->second;
}

void write(Stream stream, std::string lines) {
    if (!lines.empty()) {
        writer().push(stream, std::move(lines));
    }
}

void onDeviceCreated(VkDevice device, PFN_vkGetDeviceProcAddr nextGetDeviceProcAddr, bool isGame) {
    if(isGame)circle::motor::sequential::installSecondaryCacheResources();
    if (!enabled() && !circle::motor::sequential::requested()) {
        return;
    }
    auto* d = new DumpDevice; // freed with the device; the loader keeps no pointer to it
    d->device = device;
    d->isGame = isGame;
    // The VRS experiment's hooks sit between this one and the next layer where both hook a function.
    const auto next = [&](const char* name) {
        const PFN_vkVoidFunction vrs = vrs_nv::findHook(name);
        return vrs ? vrs : nextGetDeviceProcAddr(device, name);
    };
#define CIRCLE_VR_DUMP_LOAD_FN(name) d->name = reinterpret_cast<PFN_vk##name>(next("vk" #name));
    CIRCLE_VR_DUMP_FUNCTIONS(CIRCLE_VR_DUMP_LOAD_FN)
#undef CIRCLE_VR_DUMP_LOAD_FN
    if (!d->CmdDrawIndirectCount) {
        d->CmdDrawIndirectCount = reinterpret_cast<PFN_vkCmdDrawIndirectCount>(
            nextGetDeviceProcAddr(device, "vkCmdDrawIndirectCountKHR"));
    }
    if (!d->CmdDrawIndexedIndirectCount) {
        d->CmdDrawIndexedIndirectCount = reinterpret_cast<PFN_vkCmdDrawIndexedIndirectCount>(
            nextGetDeviceProcAddr(device, "vkCmdDrawIndexedIndirectCountKHR"));
    }

    static std::once_flag started;
    if (isGame && enabled()) {
        std::call_once(started, [] {
            const std::wstring& dir = config().dir;
            CreateDirectoryW(dir.c_str(), nullptr);
            CreateDirectoryW((dir + L"\\modules").c_str(), nullptr);
            writer().start(dir);
            std::wstring text;
            const std::uint32_t skip =
                dump::parseCount(readEnv(L"KHARVOX_CIRCLE_DUMP_SKIP_FRAMES", text) ? text : L"", 0);
            const std::uint32_t frames =
                dump::parseCount(readEnv(L"KHARVOX_CIRCLE_DUMP_FRAMES", text) ? text : L"", 60);
            startRecording(skip, frames);
            CIRCLE_VR_LOG("shader dump: writing to %ls, draw log for presents [%u, %u)", dir.c_str(), skip,
                    skip + frames);
        });
        d->isGame = true;
    }
    std::unique_lock lock(g_devicesMutex);
    g_devices[keyOf(device)] = d;
}

void onDeviceDestroyed(VkDevice device) {
    DumpDevice* d = nullptr;
    {
        std::unique_lock lock(g_devicesMutex);
        const auto it = g_devices.find(keyOf(device));
        if (it == g_devices.end()) {
            return;
        }
        d = it->second;
        g_devices.erase(it);
    }
    {
        std::lock_guard lock(cacheMutex);
        for(auto it=cachePipelines.begin();it!=cachePipelines.end();){
            if(it->second.device==device){d->DestroyPipeline(device,it->second.pipeline,nullptr);it=cachePipelines.erase(it);}
            else ++it;
        }
        for(auto it=cacheModules.begin();it!=cacheModules.end();){
            if(it->second.device==device){d->DestroyShaderModule(device,it->second.module,nullptr);it=cacheModules.erase(it);}
            else ++it;
        }
        cacheCommands.clear();
    }
    delete d;
}

} // namespace circlevr::vkcore::shader_dump
