#pragma once
#include "motor/model_pair.hpp"
#include <string_view>

namespace circle::motor {
inline bool pairableNpcHair(std::string_view asset,std::string_view instance,bool firstPerson){
    return !firstPerson&&asset.ends_with(".hairmodel")&&
        asset.find("indiana")==std::string_view::npos&&
        instance.find("actor_player")==std::string_view::npos&&
        (instance.starts_with("npc_")||instance.find(":hair_")!=std::string_view::npos);
}
// Only current placement is shared. Eye projection and temporal history remain native.
struct HairPairPlacement {
    ModelPairPlacement saved{};
    bool select(bool right,uintptr_t model,uintptr_t committed,uintptr_t manager,
                uintptr_t view,unsigned index,uint64_t token,unsigned frame,
                const std::array<float,15>& native,std::array<float,15>& selected){
        if(!model||!committed||!manager||!view||!token||!ModelPairPlacement::finite(native)){
            if(!right)saved={};
            return false;
        }
        if(!right){
            // Observe every native left update. Both hair consumers must not
            // roll a later placement back to the first one seen this frame.
            saved={model,committed,manager,view,token,index,frame,native};
            selected=native;return true;
        }
        if(!saved.matches(model,committed,manager,view,index,token,frame))return false;
        selected=saved.placement;return true;
    }
};
}
