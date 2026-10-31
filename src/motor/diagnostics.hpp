#pragma once
#include <windows.h>
#include <atomic>
#include <cstdio>
#include "vkcore/log.hpp"
#include "motor/view_history_audit.hpp"
#include "motor/gpu_binding_audit.hpp"

namespace circle::motor::diagnostics {
// Only observational work belongs behind this switch. Native-state reads,
// pair/history validation, input ownership and error reporting remain active.
inline std::atomic<bool>& state(){
 static std::atomic<bool> value{[]{char v[8]{};return GetEnvironmentVariableA("CIRCLE_VERBOSE_DIAGNOSTICS",v,8)==1&&v[0]=='1';}()};
 return value;
}
inline bool enabled(){return circlevr::vkcore::loggingEnabled()&&state().load(std::memory_order_relaxed);}
inline std::atomic<bool>& npcBuffers(){static std::atomic<bool> value{false};return value;}
inline std::atomic<unsigned>& npcBufferCount(){static std::atomic<unsigned> value{};return value;}
inline bool npcBufferActive(){return npcBuffers().load(std::memory_order_relaxed)&&npcBufferCount().load(std::memory_order_relaxed)<60000;}
// Dedicated observational trace, bounded per process. No render policy changes.
inline bool npcBufferEvent(){
 return npcBufferActive()&&npcBufferCount().fetch_add(1)<60000;
}
inline std::atomic<bool>& cameraStabilization(){static std::atomic<bool> value{true};return value;}
inline std::atomic<bool>& nativeSecondaryAnimation(){static std::atomic<bool> value{false};return value;}
inline std::atomic<bool>& nativeGiSamplePhase(){static std::atomic<bool> value{false};return value;}
// r166 remains opt-in until the native selection-boundary measurement and
// same-scene headset comparison establish that retaining L's slot is useful.
inline std::atomic<bool>& pairedNpcSkin(){static std::atomic<bool> value{false};return value;}
// r181: extend the existing current-pair placement to native NPC wear parts.
// Live rollback only; never changes the older MD6 correction or skin allocator.
inline std::atomic<bool>& pairedWorldModels(){static std::atomic<bool> value{true};return value;}
inline std::atomic<bool>& pairedNpcWear(){static std::atomic<bool> value{true};return value;}
inline std::atomic<bool>& pairedParticles(){static std::atomic<bool> value{true};return value;}
inline std::atomic<bool>& materialEyeHistory(){static std::atomic<bool> value{true};return value;}
inline std::atomic<bool>& refractionHistoryGuard(){static std::atomic<bool> value{true};return value;}
inline std::atomic<bool>& transparentSortFallback(){static std::atomic<bool> value{true};return value;}
inline std::atomic<bool>& packedReflectionInverse(){static std::atomic<bool> value{true};return value;}
// r169: the packed inverse fixes the tested reflection path, but asymmetric
// optics still lose ceiling shadows in the right eye. Centered stereo plus
// that correction is headset-verified twice. Keep it as the safe default until
// the remaining asymmetric shader consumers are identified and validated.
inline std::atomic<bool>& symmetricOptics(){static std::atomic<bool> value{true};return value;}
// Called by the existing once-per-second observation poll, never per object.
// Allows a same-scene A/B comparison without another DLL/game restart.
inline void poll(){
 if(!circlevr::vkcore::loggingEnabled())return;
 {
  const auto path=circlevr::vkcore::logDirectory()+L"\\refraction-history-guard.txt";
  FILE* f=nullptr;if(!_wfopen_s(&f,path.c_str(),L"rb")&&f){const int c=fgetc(f);fclose(f);
   if((c=='0'||c=='1')&&refractionHistoryGuard().exchange(c=='1')!=(c=='1'))
    circlevr::vkcore::logf("motor-refraction-comparison: history ownership guard %s; native cvars unchanged",c=='1'?"ON":"OFF");}
 }
 {
  const auto path=circlevr::vkcore::logDirectory()+L"\\transparent-sort-fallback.txt";
  FILE* f=nullptr;if(!_wfopen_s(&f,path.c_str(),L"rb")&&f){const int c=fgetc(f);fclose(f);
   if((c=='0'||c=='1')&&transparentSortFallback().exchange(c=='1')!=(c=='1'))
    circlevr::vkcore::logf("motor-transparent-sort-comparison: VR view fallback %s; native cvar unchanged",c=='1'?"ON":"OFF");}
 }
 {
  const auto path=circlevr::vkcore::logDirectory()+L"\\world-model-pairing.txt";
  FILE* f=nullptr;if(!_wfopen_s(&f,path.c_str(),L"rb")&&f){const int c=fgetc(f);fclose(f);
   if((c=='0'||c=='1')&&pairedWorldModels().exchange(c=='1')!=(c=='1'))
    circlevr::vkcore::logf("motor-world-comparison: requested %s; latched at next left affine pair",c=='1'?"ON":"OFF");}
 }
 view_audit::poll();
 gpu_audit::poll();
 {
  const auto path=circlevr::vkcore::logDirectory()+L"\\material-eye-history.txt";
  FILE* f=nullptr;if(!_wfopen_s(&f,path.c_str(),L"rb")&&f){const int c=fgetc(f);fclose(f);
   if((c=='0'||c=='1')&&materialEyeHistory().exchange(c=='1')!=(c=='1'))
    circlevr::vkcore::logf("motor-material-history-comparison: per-eye distortion source %s",c=='1'?"ON":"OFF");}
 }
 {
  const auto path=circlevr::vkcore::logDirectory()+L"\\particle-pair-sync.txt";
  FILE* f=nullptr;if(!_wfopen_s(&f,path.c_str(),L"rb")&&f){const int c=fgetc(f);fclose(f);
   if((c=='0'||c=='1')&&pairedParticles().exchange(c=='1')!=(c=='1'))
    circlevr::vkcore::logf("motor-particle-comparison: paired CPU geometry %s; takes effect at next left prepare",c=='1'?"ON":"OFF");}
 }
 {
  const auto path=circlevr::vkcore::logDirectory()+L"\\npc-wear-pairing.txt";
  FILE* f=nullptr;if(!_wfopen_s(&f,path.c_str(),L"rb")&&f){const int c=fgetc(f);fclose(f);
   if((c=='0'||c=='1')&&pairedNpcWear().exchange(c=='1')!=(c=='1'))
    circlevr::vkcore::logf("motor-npc-wear-pairing: %s; existing MD6 placement unchanged",c=='1'?"ON":"OFF");}
 }
 {
  const auto path=circlevr::vkcore::logDirectory()+L"\\npc-buffer-audit.txt";
  FILE* f=nullptr;if(!_wfopen_s(&f,path.c_str(),L"rb")&&f){const int c=fgetc(f);fclose(f);
   if((c=='0'||c=='1')&&npcBuffers().exchange(c=='1')!=(c=='1'))
    circlevr::vkcore::logf("motor-npc-buffer-trace: %s; CPU observations only; maximum 60000 events per process",c=='1'?"ON":"OFF");}
 }
 {
  const auto path=circlevr::vkcore::logDirectory()+L"\\packed-reflection-inverse.txt";
  FILE* f=nullptr;if(!_wfopen_s(&f,path.c_str(),L"rb")&&f){const int c=fgetc(f);fclose(f);
   if((c=='0'||c=='1')&&packedReflectionInverse().exchange(c=='1')!=(c=='1'))
    circlevr::vkcore::logf("motor-packed-reflection: optical centre correction %s",c=='1'?"ON":"OFF");}
 }
 {
  const auto path=circlevr::vkcore::logDirectory()+L"\\paired-npc-skin.txt";
  FILE* f=nullptr;if(!_wfopen_s(&f,path.c_str(),L"rb")&&f){const int c=fgetc(f);fclose(f);
   if((c=='0'||c=='1')&&pairedNpcSkin().exchange(c=='1')!=(c=='1'))
    circlevr::vkcore::logf("motor-npc-skin-comparison: paired auxiliary geometry %s",c=='1'?"ON":"OFF");}
 }
 {
  const auto path=circlevr::vkcore::logDirectory()+L"\\symmetric-optics.txt";
  FILE* f=nullptr;if(!_wfopen_s(&f,path.c_str(),L"rb")&&f){const int c=fgetc(f);fclose(f);
   if((c=='0'||c=='1')&&symmetricOptics().exchange(c=='1')!=(c=='1'))
    circlevr::vkcore::logf("motor-optics-comparison: symmetric stereo %s",c=='1'?"ON":"OFF");}
 }
 {
  const auto path=circlevr::vkcore::logDirectory()+L"\\native-gi-sample-phase.txt";
  FILE* f=nullptr;if(!_wfopen_s(&f,path.c_str(),L"rb")&&f){const int c=fgetc(f);fclose(f);
   if((c=='0'||c=='1')&&nativeGiSamplePhase().exchange(c=='1')!=(c=='1'))
    circlevr::vkcore::logf("motor-gi-sample-phase: native comparison %s",c=='1'?"ON":"OFF");}
 }
 {
  const auto path=circlevr::vkcore::logDirectory()+L"\\native-secondary-animation.txt";
  FILE* f=nullptr;if(!_wfopen_s(&f,path.c_str(),L"rb")&&f){const int c=fgetc(f);fclose(f);
   if((c=='0'||c=='1')&&nativeSecondaryAnimation().exchange(c=='1')!=(c=='1'))
    circlevr::vkcore::logf("motor-secondary-animation: native comparison %s",c=='1'?"ON":"OFF");}
 }
 {
  const auto path=circlevr::vkcore::logDirectory()+L"\\camera-stabilization.txt";
  FILE* f=nullptr;if(!_wfopen_s(&f,path.c_str(),L"rb")&&f){const int c=fgetc(f);fclose(f);
   if((c=='0'||c=='1')&&cameraStabilization().exchange(c=='1')!=(c=='1'))
    circlevr::vkcore::logf("motor-camera-anchor: stabilization %s",c=='1'?"ON":"OFF");}
 }
 const auto path=circlevr::vkcore::logDirectory()+L"\\verbose-diagnostics.txt";
 FILE* file=nullptr;
 if(_wfopen_s(&file,path.c_str(),L"rb")||!file)return;
 const int command=fgetc(file);fclose(file);
 if(command!='0'&&command!='1')return;
 const bool next=command=='1';
 if(state().exchange(next)!=next)circlevr::vkcore::logf("motor-diagnostics: verbose %s; operational stereo/input checks remain active",next?"ON":"OFF");
}
}
