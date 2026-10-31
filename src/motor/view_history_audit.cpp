#include "motor/view_history_audit.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>

namespace circle::motor::view_audit {
namespace {
constexpr unsigned perStage = 512;
struct Record {
    // stage, eye, frame, eyeSeq, simulation token, view, model, QPC
    std::array<std::uint64_t,8> key{};
    std::array<unsigned char,0x980> view{};
    // Hair setup inputs only: model +120 and +8A0, 64 bytes each.
    // Do not interpret these as completed shader output or GPU data.
    std::array<unsigned char,128> model{};
    std::array<char,128> asset{};
};
static_assert(sizeof(Record)==2752);
std::atomic<bool> enabled{};
std::mutex mutex;
std::vector<Record> records;
std::array<unsigned,3> counts{};
ULONGLONG deadline{};
unsigned session{};
bool copy(std::uintptr_t at,void* dst,std::size_t size){
    SIZE_T n{};
    return at&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),
                               dst,size,&n)&&n==size;
}
}
void sample(Stage stage,unsigned eye,std::uint32_t frame,std::uint32_t eyeSequence,
            std::uint64_t token,std::uintptr_t view,std::uintptr_t model){
    if(!enabled.load(std::memory_order_relaxed)||eye>1||!token||!view)return;
    const auto index=static_cast<unsigned>(stage);
    if(index>=counts.size())return;
    std::lock_guard lock(mutex);
    if(!enabled||counts[index]>=perStage)return;
    Record row{};
    if(!copy(view+0x18790,row.view.data(),row.view.size()))return;
    LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);
    row.key={index,eye,frame,eyeSequence,token,view,model,
             static_cast<std::uint64_t>(qpc.QuadPart)};
    if(model){
        std::uintptr_t name{};
        if(!copy(model+0x120,row.model.data(),64)||
           !copy(model+0x8a0,row.model.data()+64,64))return;
        if(copy(model+0x90,&name,sizeof(name)))copy(name,row.asset.data(),row.asset.size()-1);
    }
    records.push_back(row);
    ++counts[index];
}
void poll(){
    if(!circlevr::vkcore::loggingEnabled())return;
    const auto now=GetTickCount64();
    const auto request=circlevr::vkcore::logDirectory()+L"\\view-history-audit.request";
    const bool requested=GetFileAttributesW(request.c_str())!=INVALID_FILE_ATTRIBUTES;
    std::vector<Record> done;
    unsigned completed{};
    {
        std::lock_guard lock(mutex);
        const bool full=counts[0]>=perStage&&counts[1]>=perStage&&counts[2]>=perStage;
        if(enabled&&(now>=deadline||full)){
            enabled=false;
            done.swap(records);
            completed=session;
        } else if(!enabled&&requested&&DeleteFileW(request.c_str())){
            records.clear();records.reserve(perStage*counts.size());counts={};
            deadline=now+20000;++session;enabled=true;
            circlevr::vkcore::logf("motor-view-audit: started %u; 20 seconds, at most 512 records per boundary; observation only",session);
        }
    }
    if(completed){
        const auto path=circlevr::vkcore::logDirectory()+L"\\view-history-audit-"+std::to_wstring(completed)+L".bin";
        FILE* f{};
        bool ok=false;
        if(!circlevr::vkcore::openLogFile(&f,path.c_str(),L"wb")&&f){
            constexpr char magic[8]={'C','V','I','E','W','0','0','1'};
            const std::uint32_t size=sizeof(Record),count=static_cast<std::uint32_t>(done.size());
            ok=fwrite(magic,1,8,f)==8&&fwrite(&size,4,1,f)==1&&fwrite(&count,4,1,f)==1&&
               fwrite(done.data(),sizeof(Record),done.size(),f)==done.size();
            if(fclose(f)!=0)ok=false;
        }
        circlevr::vkcore::logf("motor-view-audit: completed %u records %zu saved %d; CPU inputs only, GPU consumption remains unproven",
                         completed,done.size(),ok?1:0);
    }
}
}
