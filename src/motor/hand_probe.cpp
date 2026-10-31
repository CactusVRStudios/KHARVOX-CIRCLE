#include "motor/diagnostics.hpp"
#include "motor/build_profile.hpp"
#include "locomotion_probe.hpp"
#include "hand_pose.hpp"
#include "hand_runtime.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <safetyhook.hpp>
#include <array>
#include <atomic>
#include <cstring>
#include <cstdio>
namespace circle::motor::locomotion {
namespace {
bool read(uintptr_t p,void* out,size_t size){SIZE_T n{};return p&&ReadProcessMemory(GetCurrentProcess(),(void*)p,out,size,&n)&&n==size;}
template<class T>T at(uintptr_t p){T out{};read(p,&out,sizeof(out));return out;}
void finishRig(safetyhook::Context& r){hands::applyTrackedArms(r.rdi,r.rsi);}
// Read only after Motor's arm solver returned. 961AF0 establishes the cache
// bit layout and 48-byte transform stride; never invoke its lazy evaluation.
void observeSolvedRig(safetyhook::Context& r){
 if(!diagnostics::enabled())return;
 static std::atomic<uint64_t> last{};const auto now=GetTickCount64();auto old=last.load();
 if(now-old<2000||!last.compare_exchange_strong(old,now))return;
 const auto base=(uintptr_t)GetModuleHandleW(nullptr);
 if(at<uintptr_t>(r.rdi)!=base+build::rva(0x3653ca8))return;
 const auto native=latestNative();const auto origin=at<Vec>(r.rdi+0xc20);
 if(!native.valid||now-native.tick>100||!finite(origin)||length(origin-native.position)>.01f)return;
 const auto count=at<uint64_t>(r.rsi+8),bits=at<uintptr_t>(r.rsi),joints=at<uintptr_t>(r.rsi+0x38);
 if(!count||count>4096||!bits||!joints)return;
 std::vector<std::array<float,12>> localRaw(size_t(count),std::array<float,12>{});
 std::vector<short> parents(size_t(count),-1);
 if(!read(at<uintptr_t>(r.rsi+0x30),localRaw.data(),size_t(count)*48)||!read(at<uintptr_t>(r.rsi+0x18),parents.data(),size_t(count)*2))return;
 std::vector<hands::Pose> localPoses;localPoses.reserve(size_t(count));
 for(const auto& v:localRaw)localPoses.push_back({{v[4],v[5],v[6]},{v[0],v[1],v[2],v[3]}});
 std::vector<std::array<float,12>> globalRaw(size_t(count),std::array<float,12>{});
 std::vector<uint64_t> bitWords(size_t((count+63)/64),0);
 if(!read(joints,globalRaw.data(),size_t(count)*48)||!read(bits,bitWords.data(),bitWords.size()*8))return;
 std::vector<hands::Pose> globalPoses;globalPoses.reserve(size_t(count));std::vector<unsigned char> validBits(size_t(count),0);
 for(size_t i=0;i<count;++i){const auto& v=globalRaw[i];globalPoses.push_back({{v[4],v[5],v[6]},{v[0],v[1],v[2],v[3]}});validBits[i]=(bitWords[i/64]>>(i%64))&1;}
 for(unsigned hand=0;hand<2;++hand){
  const auto params=r.rdi+0x720+hand*0xa4;
  if(at<unsigned char>(params+0x5d)!=1)continue;
  for(unsigned slot=0;slot<8;++slot){
   const auto id=at<short>(params+0x64+slot*2);if(id<0||uint64_t(id)>=count)continue;
   const bool cached=(at<uint64_t>(bits+(id/64)*8)&(uint64_t(1)<<(id%64)))!=0;
   std::array<float,12> transform{};
   const auto resolved=hands::modelPose(localPoses,parents,unsigned(id),globalPoses,validBits);
   if(!resolved)continue;
   float error=-1;
   if(cached&&read(joints+size_t(id)*48,transform.data(),48))error=length(resolved->position-Vec{transform[4],transform[5],transform[6]});
   const auto& p=resolved->position;const auto& q=resolved->orientation;
   circlevr::vkcore::logf("motor-arm-solved: rig %p hand %u slot %u joint %d parent %d count %llu pos %.5f %.5f %.5f quat %.5f %.5f %.5f %.5f cached %d error %.7f",(void*)r.rdi,hand,slot,id,parents[id],count,p.x,p.y,p.z,q.x,q.y,q.z,q.w,cached,error);
  }
 }
}
void observeRig(safetyhook::Context& r){
 if(!diagnostics::enabled())return;
 static std::atomic<uint64_t> last{};auto now=GetTickCount64(),old=last.load();
 if(now-old<2000||!last.compare_exchange_strong(old,now))return;
 const auto base=(uintptr_t)GetModuleHandleW(nullptr);
 if(at<uintptr_t>(r.rdi)!=base+build::rva(0x3653ca8))return;
 auto position=at<Vec>(r.rdi+0xc20);const auto native=latestNative();
 circlevr::vkcore::logf("motor-arm-probe: rig %p context %p world %.4f %.4f %.4f physics %.4f %.4f %.4f decl %p",(void*)r.rdi,(void*)r.rsi,position.x,position.y,position.z,native.position.x,native.position.y,native.position.z,(void*)at<uintptr_t>(r.rdi+0x718));
 for(unsigned h=0;h<2;++h){const auto p=r.rdi+0x720+h*0xa4;std::array<short,8> ids{};if(!read(p+0x64,ids.data(),sizeof(ids)))continue;auto target=at<Vec>(p);
  circlevr::vkcore::logf("motor-arm-probe: slot %u initialized %u bones %d %d %d %d %d %d %d %d lengths %.4f %.4f %.4f blend %.3f target %.3f %.3f %.3f",h,at<unsigned char>(p+0x5d),ids[0],ids[1],ids[2],ids[3],ids[4],ids[5],ids[6],ids[7],at<float>(p+0x74),at<float>(p+0x78),at<float>(p+0x7c),at<float>(p+0x80),target.x,target.y,target.z);
 }
}
void observeHand(safetyhook::Context& r){
 const auto contextEntity=at<uint64_t>(at<uintptr_t>(r.rbp+0x18));
 const auto owned=latestNative();
 if(owned.valid&&owned.entity==contextEntity){
  const auto loadout=at<uintptr_t>(r.rbp+0x38),anim=at<uintptr_t>(r.rbp+0x48);
  unsigned char slot=0,dead=1;
  if(read(loadout+0x10c,&slot,1)&&read(anim+6,&dead,1))hands::observeCombatContext(contextEntity,slot,dead!=0,loadout);
 }
 if(!diagnostics::enabled())return;
 static std::atomic<uint64_t> last{};auto now=GetTickCount64(),old=last.load();
 if(now-old<2000||!last.compare_exchange_strong(old,now))return;
 std::array<unsigned char,216> bytes{};if(!read(r.rsi,bytes.data(),bytes.size()))return;
 auto field=[&]<class T>(size_t offset){T v{};std::memcpy(&v,bytes.data()+offset,sizeof(v));return v;};
 const auto entity=at<uint64_t>(at<uintptr_t>(r.rbp+0x18));
 const auto player=latestNative();auto a=field.operator()<Vec>(0x24),b=field.operator()<Vec>(0x30);
 circlevr::vkcore::logf("motor-hand-probe: candidate entity %llx physics owner %llx equal %d component %p query %p enabled %u modifying %u targets %.3f %.3f %.3f / %.3f %.3f %.3f handles %llx %llx",
 entity,player.entity,entity==player.entity,(void*)r.rsi,(void*)r.r15,bytes[0],bytes[0x20],a.x,a.y,a.z,b.x,b.y,b.z,field.operator()<uint64_t>(0xc0),field.operator()<uint64_t>(0xc8));
 static unsigned dumps{};if(dumps++>=6)return;
 for(unsigned offset=0x18;offset<=0x78;offset+=0x10){auto pointer=at<uintptr_t>(r.rbp+offset);std::array<unsigned char,96> raw{};if(!read(pointer,raw.data(),raw.size()))continue;
  char hex[193]{};for(unsigned i=0;i<raw.size();++i)sprintf_s(hex+2*i,3,"%02x",raw[i]);
  circlevr::vkcore::logf("motor-hand-probe: slot %x ptr %p bytes %s",offset,(void*)pointer,hex);
 }
}
}
// Invoked only from the fingerprint-validated Motor observation installer.
void installHandProbe(){
 const auto base=(uintptr_t)GetModuleHandleW(nullptr);
 constexpr unsigned char finalBytes[]={0x48,0x81,0xc4,0x98,0x01,0,0};
 std::array<unsigned char,sizeof(finalBytes)> finalActual{};
 if(build::matches(0x984ee6,finalBytes,sizeof(finalBytes))){
  auto finalHook=safetyhook::MidHook::create((void*)(base+build::rva(0x984ee6)),finishRig);
  if(finalHook){new safetyhook::MidHook(std::move(*finalHook));circlevr::vkcore::logf("motor-hand-ik: native body-IK exit 984EE6 installed; requires CIRCLE_HAND_IK=1 and owned tracked gameplay");}
 }
 constexpr unsigned char expected[]={0xf2,0x0f,0x10,0x46,0x24};
 std::array<unsigned char,sizeof(expected)> bytes{};
 if(!build::matches(0x13d58f4,expected,sizeof(expected)))return;
 auto hook=safetyhook::MidHook::create((void*)(base+build::rva(0x13d58f4)),observeHand);
 if(hook){new safetyhook::MidHook(std::move(*hook));circlevr::vkcore::logf("motor-hand-probe: read-only target observer 13D58F4 installed; no bone writes");}
 constexpr unsigned char rigBytes[]={0x48,0x8b,0x87,0xb8,0,0,0};
 std::array<unsigned char,sizeof(rigBytes)> actual{};
 if(build::matches(0x9847c9,rigBytes,sizeof(rigBytes))){
  auto rigHook=safetyhook::MidHook::create((void*)(base+build::rva(0x9847c9)),observeRig);
  if(rigHook){new safetyhook::MidHook(std::move(*rigHook));circlevr::vkcore::logf("motor-arm-probe: read-only rig evaluator observer installed");}
 }
 constexpr unsigned char solvedBytes[]={0x80,0xbf,0x78,0x0b,0,0,0};
 std::array<unsigned char,sizeof(solvedBytes)> solvedActual{};
 if(build::matches(0x984d87,solvedBytes,sizeof(solvedBytes))){
  auto solvedHook=safetyhook::MidHook::create((void*)(base+build::rva(0x984d87)),observeSolvedRig);
  if(solvedHook){new safetyhook::MidHook(std::move(*solvedHook));circlevr::vkcore::logf("motor-arm-solved: read-only post-arm observer 984D87 installed; cache-valid transforms only");}
 }
}
}
