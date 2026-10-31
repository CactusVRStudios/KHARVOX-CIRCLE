#pragma once
#include "model_pair.hpp"
#include <string_view>

namespace circle::motor {
// Only the leaf name is inspected: a story directory mentioning a door must
// not enroll its camera, pendant or character models.
inline bool worldDoorAsset(std::string_view asset){
    const auto leaf=asset.substr(asset.find_last_of('/')+1);
    const bool named=leaf.find("door")!=std::string_view::npos||leaf.find("gate")!=std::string_view::npos||leaf.find("hatch")!=std::string_view::npos;
    return (asset.starts_with("models/")&&asset.ends_with(".idasset")&&named)||
        (asset.starts_with("md6def/setdressing/doors/")&&asset.ends_with(".md6"))||
        (asset.starts_with("md6def/animation/story/")&&named&&
            (asset.ends_with(".md6")||asset.ends_with(".idasset")));
}
enum class WorldModelKind { Unsupported, Static, Skinned, Deform, Breakable, GeomCache };
inline WorldModelKind worldModelKind(std::string_view rtti){
    if(rtti==".?AVidRenderModelStatic@@")return WorldModelKind::Static;
    if(rtti==".?AVidRenderModelSkinned@@")return WorldModelKind::Skinned;
    if(rtti==".?AVidRenderModelDeform@@")return WorldModelKind::Deform;
    if(rtti==".?AVidRenderModelBreakable@@")return WorldModelKind::Breakable;
    if(rtti==".?AVidRenderModelGeomCache@@")return WorldModelKind::GeomCache;
    return WorldModelKind::Unsupported;
}
inline bool pairableWorldModel(std::string_view asset,std::string_view instance,bool firstPerson,WorldModelKind kind){
    // Names only protect specialized player/attachment ownership; never select
    // a world category. A supported RTTI type AND the native affine consumer
    // are required before any transform can be redirected.
    return kind!=WorldModelKind::Unsupported&&!firstPerson&&
        instance.find("actor_player")==std::string_view::npos&&
        instance.find(":wear[")==std::string_view::npos&&instance.find(":hair_")==std::string_view::npos&&
        asset.find("indiana")==std::string_view::npos&&!asset.starts_with("models/wear/");
}
struct WorldModelPair {
    ModelPairPlacement saved{};
    bool select(bool right,uintptr_t model,uintptr_t committed,uintptr_t manager,unsigned index,
                uint64_t token,unsigned frame,const std::array<float,15>& native,std::array<float,15>& out){
        if(!model||!committed||!manager||!token||!ModelPairPlacement::finite(native)){
            // A failed left update must not leave an earlier pose eligible for
            // the right eye of this same pair.
            if(!right)saved={};
            return false;
        }
        if(!right){
            // Observe native left updates, never roll them back. A parent can
            // be rebuilt before and after its attachments are updated. Holding
            // its FIRST pose here would undo that rebuild while a child keeps
            // the newer pose. The adjacent right eye needs the final left pose.
            saved={model,committed,manager,0,token,index,frame,native};
            out=native;return true;
        }
        if(!saved.matches(model,committed,manager,0,index,token,frame))return false;
        out=saved.placement;return true;
    }
};
}
