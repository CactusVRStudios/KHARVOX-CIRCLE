#pragma once
#include <cstdint>

namespace circle::motor {
// MD6's two-slot auxiliary geometry history. The native moving branch toggles
// once per builder invocation, while a stereo pair represents one game update.
struct NpcSkinPair {
    uintptr_t pose{};
    uint64_t token{};
    unsigned render{};
    int selected{-1};
    bool reusable(uintptr_t p,uint64_t t,unsigned f,int current,bool right)const{
        return pose&&pose==p&&token&&token==t&&selected>=0&&selected<2&&
            current==selected&&(f==render||(right&&f-render==1));
    }
    void observeLeft(uintptr_t p,uint64_t t,unsigned f,int current,bool moving){
        if(!p||!t||current<0||current>1){*this={};return;}
        *this={p,t,f,moving?1-current:current};
    }
};
}
