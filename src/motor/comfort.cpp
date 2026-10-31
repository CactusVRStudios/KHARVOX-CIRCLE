#include "comfort.hpp"
#include "build_profile.hpp"
#include "vkcore/log.hpp"
#include <windows.h>
#include <cstring>
#include <mutex>

namespace circle::motor::comfort {
namespace {
bool active(){
    static const bool vr=[] {char mode[32]{};GetEnvironmentVariableA("KHARVOX_CIRCLE_MODE",mode,sizeof(mode));return !std::strcmp(mode,"cinema");}();
    return vr&&build::supported();
}
bool read(uintptr_t at,void* data,size_t size){SIZE_T got{};return at&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),data,size,&got)&&got==size;}
struct Numeric {int integer;float floating;};
static_assert(sizeof(Numeric)==8);
}
void enforcePostEffects(){
    if(!active())return;
    static std::mutex mutex;std::lock_guard lock(mutex);
    struct Control {uintptr_t rva;const char* name;};
    static constexpr Control controls[]={{0x44d84e0,"r_motionblur"},{0x44d3f30,"r_chromaticAberration"},
        {0x4568e50,"g_showcrosshair"},{0x4589480,"g_setting_motion_crosshair"}};
    static bool confirmed[sizeof(controls)/sizeof(controls[0])]{};
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    for(unsigned index=0;index<sizeof(controls)/sizeof(controls[0]);++index){
        const auto& c=controls[index];const auto offset=build::rva(c.rva);if(!offset)continue;
        uintptr_t internal{},namePointer{};char name[64]{};
        if(!read(base+offset,&internal,sizeof(internal))||!internal||
           !read(base+offset+0x30,&namePointer,sizeof(namePointer))||
           !read(namePointer,name,std::strlen(c.name)+1)||std::strcmp(name,c.name))continue;
        Numeric before{},after{};
        if(!read(internal+8,&before,sizeof(before)))continue;
        const bool changed=before.integer!=0||before.floating!=0.f;
        if(changed){SIZE_T got{};if(!WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(internal+8),&after,sizeof(after),&got)||got!=sizeof(after))continue;}
        Numeric verify{};
        if(!read(internal+8,&verify,sizeof(verify))||verify.integer!=0||verify.floating!=0.f)continue;
        if(changed||!confirmed[index]){
            circlevr::vkcore::logf("motor-vr-comfort: %s forced off; native numeric lanes 0/0 verified; %s",c.name,changed?"reapplied":"initial check");
            confirmed[index]=true;
        }
    }
}
}
