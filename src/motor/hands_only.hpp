#pragma once
#include <string_view>
#include <initializer_list>
#include <array>
#include <cstdint>

namespace circle::motor {
constexpr bool heldEffectSuffix(std::string_view name){
    return name=="-emitter"||name=="-fireblob"||name=="-flame";
}
// Native restored objects receive independent terminal allocation suffixes:
// parent ...weapon6_S_27D5, child ...weapon6-flame_S_27D2 (live r303).
// Strip only the bounded, complete uppercase hex suffix, never an actor/item ID.
constexpr std::string_view restoredInstanceBase(std::string_view name){
    const auto at=name.rfind("_S_");
    if(at==name.npos||!at||name.size()-at-3<1||name.size()-at-3>8)return name;
    for(char c:name.substr(at+3))if(!((c>='0'&&c<='9')||(c>='A'&&c<='F')))return name;
    return name.substr(0,at);
}
constexpr std::string_view exactHeldEffectName(std::string_view parentName,std::string_view childName){
    if(parentName.empty()||parentName=="<unnamed>"||parentName.size()>230||!childName.starts_with(parentName))return {};
    const auto suffix=childName.substr(parentName.size());
    return heldEffectSuffix(suffix)?suffix:std::string_view{};
}
constexpr std::string_view heldEffectName(std::string_view parentName,std::string_view childName){
    if(const auto exact=exactHeldEffectName(parentName,childName);!exact.empty())return exact;
    return exactHeldEffectName(restoredInstanceBase(parentName),restoredInstanceBase(childName));
}
constexpr bool namedHeldEffect(std::string_view parentName,std::string_view childName){
    return !heldEffectName(parentName,childName).empty();
}
constexpr bool namedHeldEffectCandidate(std::string_view childName){
    childName=restoredInstanceBase(childName);
    const auto at=childName.find_last_of('-');
    return at!=childName.npos&&heldEffectSuffix(childName.substr(at));
}
// Called only AFTER live owner/world/allocation checks. A normalized stem is
// not a unique allocation ID; ambiguous candidates stay native. Direct native
// bindings and exact unmodified names take priority over restored-name matches.
struct HeldEffectParentChoice {
    uintptr_t parent{};unsigned strength{};bool ambiguous{};
    bool observe(uintptr_t candidate,bool direct){
        if(!candidate)return false;
        const unsigned priority=direct?2u:1u;
        if(priority>strength){parent=candidate;strength=priority;ambiguous=false;return true;}
        if(priority==strength&&candidate!=parent)ambiguous=true;
        return false;
    }
    bool valid()const{return parent&&!ambiguous;}
};
constexpr bool rigidLighterFlame(std::string_view parentAsset,std::string_view childAsset,
    std::string_view parentName,std::string_view childName){
    return parentAsset=="md6def/gameplay/interactables/grip/onehand/cube_small/indy_lighter_01a/indy_lighter_01a.md6"&&
        childAsset=="props/lighter/particle_prop_lighter_01.prt"&&
        heldEffectName(parentName,childName)=="-flame";
}
// Observed native torch child names include the complete unique parent instance
// name. Asset/proximity alone cannot distinguish a held torch from world fires.
constexpr bool heldTorchEffect(std::string_view parentAsset,std::string_view parentName,
    std::string_view childAsset,std::string_view childName){
    if(parentName.empty()||parentName=="<unnamed>"||parentName.size()>230||
        parentAsset.find("/torch_set_01a/")==std::string_view::npos)return false;
    const bool emitter=childAsset=="props/torch/particle_prop_torch_01.prt";
    const bool blob=childAsset=="models/gameplay/interactables/grip/onehand/cylinder_medium/torch_set_01a/torch_wood_01a_blob.idasset";
    if(!emitter&&!blob)return false;
    return heldEffectName(parentName,childName)==(emitter?"-emitter":"-fireblob");
}
// Live library candle: animated Alembic wax shares the candle model origin,
// but uses an independently named scene entity and no referenceModel.
constexpr bool candleWaxAsset(std::string_view asset){
    return asset=="vfx/alembic/maps/ch02_vatican/se02/modelasset_vatican_candlewax01.idasset";
}
constexpr bool heldCandleWax(std::string_view parentAsset,std::string_view parentName,
    std::string_view childAsset,std::string_view childName){
    return parentAsset=="models/lamps/standing/standing_candles_generic_set_01a/s_candles_generic_01a.idasset"&&
        restoredInstanceBase(parentName)=="gp_library_indys_candle"&&candleWaxAsset(childAsset)&&
        restoredInstanceBase(childName)=="gp_library_gameplay_fx_alembic_1";
}
constexpr bool candleEmitterAsset(std::string_view asset){
    return asset=="props/lighter/particle_prop_lighter_01_candlestick_nosound.prt";
}
// Vatican census: Indy, Antonio and two world instances share these assets.
// This identifies a compatible child, NOT ownership. Callers must additionally
// prove a currently held local first-person parent in the same render world.
constexpr bool heldCandleEffect(std::string_view parentAsset,std::string_view parentName,
    std::string_view childAsset,std::string_view childName){
    return parentAsset=="models/lamps/standing/standing_candles_generic_set_01a/s_candles_generic_01a.idasset"&&
        candleEmitterAsset(childAsset)&&heldEffectName(parentName,childName)=="-emitter";
}
enum class BodyEquipment {None,Whip,Pistol};
constexpr BodyEquipment bodyEquipmentAsset(std::string_view name){
    if(name=="md6def/weapons/whip01/whip_01a_3m.md6")return BodyEquipment::Whip;
    if(name=="md6def/weapons/relicpistol02/relicpistol02.md6")return BodyEquipment::Pistol;
    return BodyEquipment::None;
}
// Exact observed local assets only. Unknown/stale weapon context is visible;
// never hide an equipped weapon or infer ownership from proximity alone.
constexpr bool hideBodyEquipment(BodyEquipment item,unsigned slot){
    const bool known=slot==1||slot==2||slot==3||slot==4||slot==8||slot==10||slot==14||slot==18;
    if(!known)return false;
    return item==BodyEquipment::Whip?slot!=18:
        item==BodyEquipment::Pistol?slot!=2&&slot!=3&&slot!=4:false;
}
using HandsOnlyViewRecord=std::array<unsigned char,48>;
// Shared render correction for native first-person equipment. Slot freshness is
// checked by weaponSlot(owner); eligibility must not depend on an item list.
// Empty hands and a pending pickup have no acknowledged held-item ownership.
constexpr bool handAttachmentAsset(std::string_view asset,unsigned slot,bool firstPerson){
    if(!firstPerson||slot==0||slot==1||slot==14||slot>=255)return false;
    const auto equipment=bodyEquipmentAsset(asset);
    if(equipment==BodyEquipment::Whip&&slot!=18)return false;
    if(equipment==BodyEquipment::Pistol&&slot!=2&&slot!=3&&slot!=4)return false;
    if(asset.starts_with("md6def/characters/")||asset.starts_with("models/characters/")||asset.starts_with("models/wear/"))return false;
    return asset.starts_with("md6def/")||asset.starts_with("models/");
}
// Native GPU culling reads the same view-ID record as CPU culling. Work on a
// private upload copy so simulation, reflections and later cinema frames retain
// the authored record. Rebuild from native data on EVERY ring upload (including
// exit), rather than retaining a hidden record from an earlier frame.
constexpr HandsOnlyViewRecord handsOnlyViewRecord(HandsOnlyViewRecord native,bool hide,uint16_t viewId,bool primaryZero=false){
    // Main third-person camera uses ID0. Zero means disabled suppression,
    // so use a private surface-only allow mask for the verified ledge view.
    if(hide&&primaryZero&&viewId==0){native[42]=0xff;native[43]=0xff;}
    if(hide&&viewId){native[44]=static_cast<unsigned char>(viewId);native[45]=static_cast<unsigned char>(viewId>>8);}
    return native;
}
// Keep unknown models, held items and the skinned body asset: the latter
// contains the two hand meshes, despite its misleading name.
constexpr bool handsOnlyBodyAsset(std::string_view name) {
    if(name=="md6def/characters/standard/indiana_jones/indiana_wear_head.md6")return true;
    if(name=="indiana_wear_eyebrows.hairmodel"||name=="indiana_wear_eyelashes.hairmodel"||name=="indiana_wear_head_beard.hairmodel"||name=="indiana_wear_head.hairmodel")return true;
    // Live Himalaya traversal: jacket/hat fur and beard are separate hair
    // draws, not parts of the clothing idasset. Keep hand hair and unknowns.
    if(name=="indiana_nepal_jacket.hairmodel"||name=="indiana_nepal_hat.hairmodel"||name=="indiana_beard_nepal.hairmodel")return true;
    if(!name.starts_with("models/wear/indiana_jones/")||!name.ends_with(".idasset"))return false;
    const auto leaf=name.substr(name.find_last_of('/')+1);
    if(!leaf.starts_with("indiana_"))return false;
    for(auto part:{"_upperbody_","_lowerbody_","_footwear_","_headwear_","_harness_","_holster_"})
        if(leaf.find(part)!=std::string_view::npos)return true;
    return false;
}
constexpr bool handsOnlyContext(bool enabled,bool adapter,bool stopped,bool gameplay,bool cinema,
    bool valid,bool animated,bool localSpace,int movementType,int reason,unsigned mode) {
    return enabled&&adapter&&!stopped&&gameplay&&!cinema&&valid&&!animated&&!localSpace&&
        movementType==0&&reason==0&&(mode==0||mode==1||mode==2||mode==9||mode==7||mode==8||mode==20);
}
// Streamed traversal models can be born without the first-person flag.
// Require the native player instance namespace as well as an exact body asset;
// NPCs may wear the very same Indiana clothing.
constexpr bool traversalBodyInstance(std::string_view asset,std::string_view instance){
    if(!handsOnlyBodyAsset(asset)||!instance.starts_with("actor_player_"))return false;
    instance.remove_prefix(13);
    size_t digits=0;while(digits<instance.size()&&instance[digits]>='0'&&instance[digits]<='9')++digits;
    if(!digits)return false;
    const auto suffix=instance.substr(digits);
    return suffix==":head"||suffix.starts_with(":wear[")||suffix.starts_with(":hair_");
}
constexpr bool playerViewAssetHidden(std::string_view name,bool handsOnly,unsigned slot){
    return (handsOnly&&handsOnlyBodyAsset(name))||hideBodyEquipment(bodyEquipmentAsset(name),slot);
}
// Stationary airplane-gun control only. Visibility must not depend on the
// optional Hands Only setting or on a successful camera/IK update this frame.
constexpr bool cockpitPlayerHidden(bool adapter,bool stopped,bool gameplay,bool cinema,bool fresh,
    bool animated,bool localSpace,int type,int reason,unsigned mode,unsigned slot){
    return adapter&&!stopped&&gameplay&&!cinema&&fresh&&!animated&&!localSpace&&
        type==5&&reason==0&&mode==27&&slot==22;
}
constexpr bool cockpitPlayerAsset(std::string_view name){
    return handsOnlyBodyAsset(name)||
        name=="md6def/characters/standard/indiana_jones/indiana_wear_body.md6"||
        name=="indiana_hand_left.hairmodel"||name=="indiana_hand_right.hairmodel"||
        (name.starts_with("models/wear/indiana_jones/")&&name.ends_with(".idasset"));
}
constexpr bool cockpitPlayerInstance(std::string_view asset,std::string_view instance){
    if(!cockpitPlayerAsset(asset))return false;
    instance=restoredInstanceBase(instance);
    if(!instance.starts_with("actor_player_"))return false;
    instance.remove_prefix(13);
    size_t digits=0;while(digits<instance.size()&&instance[digits]>='0'&&instance[digits]<='9')++digits;
    if(!digits)return false;
    const auto suffix=instance.substr(digits);
    return suffix.empty()||suffix==":head"||suffix.starts_with(":wear[")||suffix.starts_with(":hair_");
}
constexpr bool cockpitViewAssetHidden(std::string_view name){
    return cockpitPlayerAsset(name)||bodyEquipmentAsset(name)!=BodyEquipment::None;
}
}
