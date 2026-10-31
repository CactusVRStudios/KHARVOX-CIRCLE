#include "prompt_bindings.hpp"
#include "build_profile.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <safetyhook.hpp>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
namespace circle::motor::prompts { namespace {
std::atomic<unsigned> state{unsigned(Context::Unknown)};
using Assign=void*(*)(void*,const void*);Assign assign{};
bool read(uintptr_t p,void* out,size_t n){SIZE_T got{};return p&&ReadProcessMemory(GetCurrentProcess(),(void*)p,out,n,&got)&&got==n;}
void parse(safetyhook::Context& r){
 const unsigned mode=state.load(std::memory_order_relaxed);if(!(mode&0x200))return;
 try{
  // Native idStr owns the output. Borrow only the source of its copy assignment;
  // no STL buffer escapes this callback and the game's allocator remains in charge.
  alignas(8) unsigned char str[48]{};if(!read(r.rcx,str,sizeof(str)))return;
  uintptr_t data{};int length{};std::memcpy(&data,str+8,8);std::memcpy(&length,str+16,4);
  if(length<=0||length>8192)return;
  std::string source(size_t(length),'\0');if(!read(data,source.data(),source.size())||source.find('_')==source.npos)return;
  auto output=replace(source,Context(mode&0xff),bool(mode&0x100));if(output==source||output.size()>16384)return;
  data=(uintptr_t)output.c_str();length=int(output.size());unsigned capacity=unsigned(length+1);
  std::memcpy(str+8,&data,8);std::memcpy(str+16,&length,4);std::memcpy(str+20,&capacity,4);
  assign((void*)r.rcx,str);
 }catch(...){/* Keep native prompts if allocation fails. */}
}
void invalidate(safetyhook::Context& r){
 // Context changes must re-run the native parser even when the action string
 // stayed identical (for example RT changes from melee to firearm firing).
 thread_local std::unordered_map<uintptr_t,unsigned> seen;
 const auto mode=state.load(std::memory_order_relaxed);
 try{
  auto [it,inserted]=seen.try_emplace(r.rcx,mode);
  if(!inserted&&it->second==mode)return;
  it->second=mode;
  unsigned hash{};if(read(r.rcx+0xa4,&hash,4)){hash^=0x80000000u;SIZE_T got{};WriteProcessMemory(GetCurrentProcess(),(void*)(r.rcx+0xa4),&hash,4,&got);}
  if(seen.size()>4096)seen.clear();
 }catch(...){}
}
}
void publish(Context c,bool melee,bool enabled){state.store(unsigned(c)|(melee?0x100u:0)|(enabled?0x200u:0),std::memory_order_relaxed);}
void install(){static std::once_flag once;std::call_once(once,[]{
 constexpr unsigned char parser[]={0x40,0x55,0x53,0x56,0x41,0x54,0x41,0x56};
 constexpr unsigned char copy[]={0x48,0x3b,0xd1,0x0f,0x84};
 constexpr unsigned char update[]={0x40,0x57,0x48,0x83,0xec,0x30};
 if(!build::matches(0x6dca40,parser,sizeof(parser))||!build::matches(0x5713f0,copy,sizeof(copy))||!build::matches(0x7087c0,update,sizeof(update)))return;
 const auto base=(uintptr_t)GetModuleHandleW(nullptr);assign=(Assign)(base+build::rva(0x5713f0));
 auto p=safetyhook::MidHook::create((void*)(base+build::rva(0x6dca40)),parse,safetyhook::MidHook::StartDisabled);
 auto u=safetyhook::MidHook::create((void*)(base+build::rva(0x7087c0)),invalidate,safetyhook::MidHook::StartDisabled);
 if(!p||!u)return;if(!p->enable())return;if(!u->enable()){(void)p->disable();return;}
 new safetyhook::MidHook(std::move(*p));new safetyhook::MidHook(std::move(*u));
 circlevr::vkcore::logf("motor-vr-prompts: native action-token parser installed; context-aware VR labels; unknown tokens retained");
 });}
}
