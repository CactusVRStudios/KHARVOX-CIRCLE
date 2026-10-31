#include "vkcore/game_code.hpp"
#include "motor/adapter.hpp"
#include "motor/sequential.hpp"

#include "engine/eternal/resolver/pattern.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

namespace circlevr::vkcore {

namespace {
bool readGameText(GameText& text) {
    auto* module = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (!module) {
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".text", 6) == 0) {
            text.base = module;
            text.bytes = resolver::ByteSpan(module + section->VirtualAddress, section->Misc.VirtualSize);
            text.timestamp = nt->FileHeader.TimeDateStamp;
            return true;
        }
    }
    return false;
}
}
bool findGameText(GameText& text) {
    if (!circle::motor::engineLayoutValidated()) { text = {}; return false; }
    return readGameText(text);
}
bool findRenderSizeText(GameText& text) {
    if(circle::motor::sequential::requested()){
        if(!circle::motor::sequential::allowsRenderSize()){text={};return false;}
        return readGameText(text);
    }
    return findGameText(text);
}

const std::byte*
findUniqueInText(const GameText& text, const char* tag, const char* name, const char* signature) {
    auto pattern = resolver::Pattern::parse(signature);
    if (!pattern) {
        CIRCLE_VR_LOG("%s: %s signature does not parse", tag, name);
        return nullptr;
    }
    const auto matches = resolver::findAll(text.bytes, *pattern);
    if (matches.size() != 1) {
        CIRCLE_VR_LOG("%s: %s signature matched %zu time(s), expected 1", tag, name, matches.size());
        return nullptr;
    }
    const std::byte* at = text.bytes.data() + matches.front();
    CIRCLE_VR_LOG("%s: %s at RVA 0x%X", tag, name, rvaOf(text, at));
    return at;
}

const std::byte* relativeTarget(const std::byte* instruction) {
    const auto opcode = std::to_integer<std::uint8_t>(instruction[0]);
    if (opcode != 0xE8 && opcode != 0xE9) {
        return nullptr;
    }
    return instruction + 5 + readI32(instruction + 1);
}

const std::byte* ripTarget(const std::byte* disp, const std::byte* instructionEnd) {
    return instructionEnd + readI32(disp);
}

} // namespace circlevr::vkcore
