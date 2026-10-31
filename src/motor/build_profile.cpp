#include "build_profile.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <vector>
#include <algorithm>
#include "build_profiles.generated.hpp"

namespace circle::motor::build {
const Profile* identify(const char* hash,uint32_t stamp,uint32_t size){
    if(!hash)return nullptr;
    for(const auto& p:profiles)if(p.timestamp==stamp&&p.imageSize==size&&!_stricmp(hash,p.sha256))return &p;
    return nullptr;
}
const Profile* current(){
    static const Profile* profile=[]()->const Profile*{
        const auto base=reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
        const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if(!base||dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||dos->e_lfanew>0x100000)return nullptr;
        const auto pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64)return nullptr;
        bool candidate=false;
        for(const auto& p:profiles)candidate|=p.timestamp==pe->FileHeader.TimeDateStamp&&p.imageSize==pe->OptionalHeader.SizeOfImage;
        if(!candidate)return nullptr;
        wchar_t path[32768]{};if(!GetModuleFileNameW(nullptr,path,32768))return nullptr;
        HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return nullptr;
        BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};DWORD objectSize{},got{};
        bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
        if(ok)ok=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&got,0)>=0;
        std::vector<unsigned char> object(objectSize),buffer(1024*1024);
        if(ok)ok=BCryptCreateHash(alg,&hash,object.data(),objectSize,nullptr,0,0)>=0;
        while(ok){DWORD n{};if(!ReadFile(file,buffer.data(),DWORD(buffer.size()),&n,nullptr)){ok=false;break;}if(!n)break;ok=BCryptHashData(hash,buffer.data(),n,0)>=0;}
        std::array<unsigned char,32> digest{};
        if(ok)ok=BCryptFinishHash(hash,digest.data(),DWORD(digest.size()),0)>=0;
        if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);CloseHandle(file);
        if(!ok)return nullptr;
        char hex[65]{};constexpr char alphabet[]="0123456789abcdef";
        for(unsigned i=0;i<32;++i){hex[i*2]=alphabet[digest[i]>>4];hex[i*2+1]=alphabet[digest[i]&15];}
        return identify(hex,pe->FileHeader.TimeDateStamp,pe->OptionalHeader.SizeOfImage);
    }();
    return profile;
}
bool supported(){return current()!=nullptr;}
const Address* entry(uintptr_t canonical){
    const auto i=std::lower_bound(std::begin(addresses),std::end(addresses),canonical,[](const Address& a,uintptr_t r){return a.reference<r;});
    return i!=std::end(addresses)&&i->reference==canonical?i:nullptr;
}
uintptr_t resolve(Id id,uintptr_t canonical){
    const auto a=entry(canonical);if(!a)return 0;
    return id==Id::Gog170?a->reference:id==Id::Steam170?a->steam:0;
}
uintptr_t rva(uintptr_t canonical){const auto p=current();return p?resolve(p->id,canonical):0;}
uintptr_t canonical(uintptr_t actual){
    const auto p=current();if(!p)return 0;
    for(const auto& a:addresses)if((p->id==Id::Gog170?a.reference:a.steam)==actual)return a.reference;
    // Joint selectors report the return address immediately after a checked call.
    for(const auto& a:addresses)if(a.code&&a.gogBytes[0]==0xe8&&(p->id==Id::Gog170?a.reference:a.steam)+5==actual)return a.reference+5;
    return 0;
}
bool matches(uintptr_t canonical,const void* referenceBytes,size_t count){
    const auto p=current();const auto a=entry(canonical);
    if(!p||!a||!a->code||!referenceBytes||count>32||std::memcmp(a->gogBytes,referenceBytes,count))return false;
    std::array<unsigned char,32> actual{};SIZE_T got{};
    const auto at=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+resolve(p->id,canonical);
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),actual.data(),count,&got)&&got==count&&
        !std::memcmp(actual.data(),p->id==Id::Gog170?a->gogBytes:a->steamBytes,count);
}
}
