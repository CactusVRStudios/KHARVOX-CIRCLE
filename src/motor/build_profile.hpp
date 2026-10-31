#pragma once
#include <cstdint>
#include <cstddef>

namespace circle::motor::build {
enum class Id { Unknown, Gog170, Steam170 };
struct Profile { Id id; const char* name; const char* sha256; uint32_t timestamp, imageSize; };
// Exact file identity, not product version or store folder name.
const Profile* identify(const char* sha256,uint32_t timestamp,uint32_t imageSize);
const Profile* current();
bool supported();
uintptr_t resolve(Id id,uintptr_t canonical);
uintptr_t rva(uintptr_t canonical);
// Exact return-address entries only (no guessed relocation delta).
uintptr_t canonical(uintptr_t actual);
bool matches(uintptr_t canonical,const void* referenceBytes,size_t count);
}
