#pragma once
#include <string_view>

namespace circle::motor {
enum class NpcPlacementKind { None, Character, WearPart };
inline std::string_view npcWearOwner(std::string_view instance) {
    const auto at=instance.find(":wear[");
    if(at==std::string_view::npos||at==0||!instance.ends_with("]")||at+7>=instance.size())return {};
    return instance.substr(0,at);
}

// Asset names alone cannot authorize a player/equipment transform. Native
// npc_ names or an exact attachment prefix of a live registered character
// identify wear bodies and their attachments.
// This is a render-instance classification, not a recovered actor entity ID.
inline NpcPlacementKind npcPlacementKind(std::string_view asset,
    std::string_view instance,bool firstPerson,bool registeredCharacterOwner=false) {
    if(firstPerson||asset.find("indiana")!=std::string_view::npos||
       instance.starts_with("actor_player"))return NpcPlacementKind::None;
    if(asset.starts_with("md6def/characters/"))return NpcPlacementKind::Character;
    const bool namedNpc=instance.starts_with("npc_")&&instance.size()>4;
    const bool ownedWear=registeredCharacterOwner&&!npcWearOwner(instance).empty();
    if(!namedNpc&&!ownedWear)return NpcPlacementKind::None;
    if((asset.starts_with("models/wear/")&&asset.ends_with(".idasset"))||
       asset.ends_with(".hairmodel"))return NpcPlacementKind::WearPart;
    return NpcPlacementKind::None;
}
}
