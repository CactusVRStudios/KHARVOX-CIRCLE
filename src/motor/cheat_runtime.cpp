#include "cheat_runtime.hpp"
#include "cheat_policy.hpp"
#include "locomotion_probe.hpp"
#include "build_profile.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <safetyhook.hpp>
#include <atomic>
#include <array>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <vector>
namespace circle::motor::cheats {
namespace {
bool flag(const char* name){char b[8]{};return GetEnvironmentVariableA(name,b,sizeof(b))==1&&b[0]=='1';}
const Options& options(){static const Options o=[] {
 Options v;v.invulnerable=flag("CIRCLE_CHEAT_INVULNERABLE");v.stealth=flag("CIRCLE_CHEAT_STEALTH");
 v.stamina=flag("CIRCLE_CHEAT_STAMINA");v.oxygen=flag("CIRCLE_CHEAT_OXYGEN");v.ammo=flag("CIRCLE_CHEAT_AMMO");v.damageBoost=flag("CIRCLE_CHEAT_DAMAGE");
 char b[32]{};auto length=GetEnvironmentVariableA("CIRCLE_CHEAT_DAMAGE_MULTIPLIER",b,sizeof(b));
 if(length&&length<sizeof(b)){char* end{};const float m=strtof(b,&end);if(end!=b&&!*end&&std::isfinite(m)&&m>=1&&m<=99)v.multiplier=m;else v.damageBoost=false;}
 return v;
 }();return o;}
bool read(uintptr_t p,void* out,size_t size){SIZE_T got{};return p&&ReadProcessMemory(GetCurrentProcess(),(void*)p,out,size,&got)&&got==size;}
template<class T>T at(uintptr_t p){T v{};read(p,&v,sizeof(v));return v;}
template<class T>bool put(uintptr_t p,T value){SIZE_T wrote{};return p&&WriteProcessMemory(GetCurrentProcess(),(void*)p,&value,sizeof(value),&wrote)&&wrote==sizeof(value);}
uint64_t local(){const auto n=locomotion::latestNative();const auto now=GetTickCount64();return n.valid&&now>=n.tick&&now-n.tick<250?n.entity:0;}
std::atomic<uint64_t> damageChanges{},staminaChanges{},ammoChanges{},visionSkips{},soundSkips{},lastReport{};
void report(){const auto now=GetTickCount64();auto last=lastReport.load();if(now-last<5000||!lastReport.compare_exchange_strong(last,now))return;
 circlevr::vkcore::logf("motor-cheats: applied damage %llu stamina %llu ammo %llu; filtered vision %llu sound %llu",damageChanges.load(),staminaChanges.load(),ammoChanges.load(),visionSkips.load(),soundSkips.load());
}
void onDamage(safetyhook::Context& r){
 // idCalculateDamageJob: after calculating one idTrackedDamage, before publishing it.
 // Never change shared damage declarations or other entities' resource components.
 const auto id=local();if(!id)return;
 std::array<unsigned char,0xa0> event{};if(!read(r.rbx,event.data(),event.size()))return;
 uint64_t victim{},attacker{};float before{};
 std::memcpy(&victim,event.data(),8);std::memcpy(&attacker,event.data()+8,8);std::memcpy(&before,event.data()+0x3c,4);
 const auto after=damage(before,id,victim,attacker,options());
 if(after!=before&&std::isfinite(after)&&put(r.rbx+0x3c,after))++damageChanges;
 if(options().invulnerable&&player(id,victim)){
  put(r.rbx+0x98,0.f);put(r.rbx+0x9c,0.f);
 }
 report();
}
void oxygen(){
 if(!options().oxygen)return;
 static uint64_t last{};const auto now=GetTickCount64();if(now-last<2000)return;last=now;
 const auto offset=build::rva(0x4552ec0);if(!offset)return;
 const auto wrapper=(uintptr_t)GetModuleHandleW(nullptr)+offset;
 constexpr char expected[]="g_oxygenBreatheUnderwater";char name[sizeof(expected)]{};
 if(!read(at<uintptr_t>(wrapper+0x30),name,sizeof(name))||memcmp(name,expected,sizeof(name)))return;
 const auto value=at<uintptr_t>(wrapper);int i{};float f{};
 if(!read(value+8,&i,4)||!read(value+12,&f,4)||(i!=0&&i!=1)||!std::isfinite(f)||(f!=0&&f!=1))return;
 if(i==0&&put(value+8,1)&&put(value+12,1.f))circlevr::vkcore::logf("motor-cheats: native underwater breathing enabled");
}
void onStamina(safetyhook::Context& r){
 // idStaminaPlayerUpdateJob, after its update/optional auto-depletion.
 const auto id=local();if(!player(id,at<uint64_t>(r.r14)))return;
 if(options().stamina&&at<unsigned char>(r.rdi+0x78)==1){
  int current{},capacity{};
  if(read(r.rdi+8,&capacity,4)&&read(r.rdi+12,&current,4)&&refill(current,capacity)&&put(r.rdi+12,capacity))++staminaChanges;
 }
 oxygen();report();
}
void onVision(safetyhook::Context& r){
 // Omit only the local player's transient perception row. The next entity
 // reuses this slot; the native count, capacity and other targets stay intact.
 if(player(local(),at<uint64_t>(r.rbp+0x48))){r.rip=(uintptr_t)GetModuleHandleW(nullptr)+build::rva(0x11dc05a);++visionSkips;}
 report();
}
void onSound(safetyhook::Context& r){
 // Native hearing event: R8 is the emitter ID, R9 the listening entity ID.
 // Skip player-originated events without disabling NPC hearing globally.
 if(player(local(),r.r8)){r.rip=(uintptr_t)GetModuleHandleW(nullptr)+build::rva(0x11992d9);++soundSkips;}
 report();
}
struct Site {uintptr_t rva;const char* bytes;size_t size;void(*callback)(safetyhook::Context&);};
bool group(const char* name,std::initializer_list<Site> sites){
 for(const auto& h:sites)if(!build::matches(h.rva,h.bytes,h.size)){circlevr::vkcore::logf("motor-cheats: %s DISABLED, signature mismatch %llx",name,h.rva);return false;}
 std::vector<safetyhook::MidHook> pending;
 for(const auto& h:sites)if(h.callback){auto hook=safetyhook::MidHook::create((void*)((uintptr_t)GetModuleHandleW(nullptr)+build::rva(h.rva)),h.callback);if(!hook){circlevr::vkcore::logf("motor-cheats: %s DISABLED, hook failed",name);return false;}pending.push_back(std::move(*hook));}
 for(auto& h:pending)new safetyhook::MidHook(std::move(h));
 circlevr::vkcore::logf("motor-cheats: %s hooks installed",name);return true;
}
}
bool ammoEnabled(){return options().ammo;}
void weapon(uint64_t owner,uint64_t entity,uintptr_t loadout,unsigned slot,uintptr_t attachment,uintptr_t active){
 if(!ammoEnabled()||!player(local(),owner)||slot>=28||!entity||entity==UINT64_MAX||!active||!attachment||
    at<uint64_t>(loadout+0x28+slot*8)!=entity||at<uint64_t>(attachment)!=owner||at<unsigned char>(active+0x366)!=1)return;
 const auto count=at<int>(active+0x18c);if(count<1||count>3)return;
 for(int i=0;i<count;++i){const auto a=active+0x190+i*0x30;int current{},capacity{};
  if(!at<uintptr_t>(a)||!read(a+0x20,&current,4)||!read(a+0x24,&capacity,4)||!refill(current,capacity))continue;
  // Reserve only: keep clip/reload state and native firing restrictions intact.
  if(put(a+0x20,capacity))++ammoChanges;
 }
 report();
}
void install(){static std::once_flag once;std::call_once(once,[]{const auto& o=options();
 circlevr::vkcore::logf("motor-cheats: requested invulnerable %d stealth %d stamina %d oxygen %d ammo %d damage %d multiplier %.1f; restart applies settings",o.invulnerable,o.stealth,o.stamina,o.oxygen,o.ammo,o.damageBoost,o.multiplier);
 if(o.invulnerable||o.damageBoost)group("damage",{{0x13e2497,"\x48\x81\xc3\xa0\x00\x00\x00\x48\x3b\xde\x75\xad",12,onDamage}});
 if(o.stamina||o.oxygen)group("survival",{{0x18dd77f,"\x48\x8b\x46\x20\x48\x89\x9d\x40\x01\x00\x00",11,onStamina}});
 if(o.stealth)group("stealth",{
  {0x11dc034,"\x49\x63\xc4\x41\xff\xc4\x0f\x11\x45\x30",10,onVision},
  {0x11dc05a,"\x4a\x8b\x44\xdb\x20\x48\x63\xd1",8,nullptr},
  {0x1199250,"\x4d\x3b\xc1\x0f\x84\x80\x00\x00\x00",9,onSound},
  {0x11992d9,"\x41\xff\xc7\x48\x83\xc6\x30\x44\x3b\x7a\x08",11,nullptr}});
 });}
}
