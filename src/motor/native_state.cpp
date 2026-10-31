#include "motor/build_profile.hpp"
#include "motor/native_state.hpp"
#include "motor/adapter.hpp"
#include "motor/sequential.hpp"
#include "motor/experimental.hpp"
#include <array>
#include <cstring>

namespace circle::motor {
namespace {
constexpr std::uintptr_t kGameSystem = 0x4c6c4c0;
constexpr std::uintptr_t kVtable = 0x36e4bd0;
bool read(std::uintptr_t address, void* output, std::size_t bytes) {
    SIZE_T count = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), output,
                             bytes, &count) && count == bytes;
}
template<std::size_t N> bool matches(std::uintptr_t address, const unsigned char (&expected)[N]) {
    std::array<unsigned char,N> actual{};
    return read(address, actual.data(), N) && !std::memcmp(actual.data(), expected, N);
}
}
void NativeStateObserver::validate() {
    attempted_ = true;
    if (!isTargetProcess()) { reason_ = "not TheGreatCircle.exe"; return; }
    base_ = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 pe{};
    if (!read(base_, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !read(base_ + dos.e_lfanew, &pe, sizeof(pe)) || pe.Signature != IMAGE_NT_SIGNATURE ||
        pe.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        !build::supported()) {
        reason_ = "unsupported Motor PE fingerprint; native observer disabled"; return;
    }
    // Measured Motor SetState: native state read at +0x54, enum table, and state write.
    // This only verifies code. It does not patch, call or hook it.
    constexpr unsigned char prologue[] = {0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,
        0x48,0x63,0xda,0x4c,0x8d,0x0d,0xf4,0xa9,0x53,0x03,0x48,0x8b,0xf9,0x48,0x63,0x49,0x54};
    constexpr unsigned char write[] = {0x89,0x5f,0x54,0x48,0x8b,0x5c,0x24,0x38};
    if (!build::matches(0xf1e590,prologue,sizeof(prologue)) || !build::matches(0xf1e5fa,write,sizeof(write))) {
        reason_ = "Motor SetState code mismatch; native observer disabled"; return;
    }
    const char* enumNames[] = {"idGameSystemLocal::GAME_STATE_MAIN_MENU",
        "idGameSystemLocal::GAME_STATE_LOADING", "idGameSystemLocal::GAME_STATE_INGAME"};
    for (std::size_t i=0;i<3;++i) {
        std::uintptr_t pointer = 0;
        char text[80]{};
        if (!read(base_+build::rva(0x4458f98)+i*8,&pointer,sizeof(pointer)) ||
            pointer<base_ || pointer>=base_+pe.OptionalHeader.SizeOfImage ||
            !read(pointer,text,std::strlen(enumNames[i])+1) || std::strcmp(text,enumNames[i])) {
            reason_ = "Motor state enum mismatch; native observer disabled"; return;
        }
    }
    validated_ = true;
    reason_ = build::current()->name;
}
Observation NativeStateObserver::sample() {
    if (!attempted_) validate();
    Observation o;
    if (!validated_) return o;
    std::uintptr_t vtable=0;
    int state=-1;
    if (read(base_+build::rva(kGameSystem),&vtable,sizeof(vtable)) && vtable==base_+build::rva(kVtable) &&
        read(base_+build::rva(kGameSystem)+0x54,&state,sizeof(state)) && state>=0 && state<=2) {
        o.nativeState=state; o.nativeStateValid=true;
        o.pauseValidated=true;o.paused=experimental::pauseMenuActive();
        o.menuValidated=true;o.menuActive=sequential::menuActive();o.mainMenu=sequential::nativeMainMenuActive();
    }
    return o;
}
}
