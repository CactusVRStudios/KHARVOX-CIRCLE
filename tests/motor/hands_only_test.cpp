#include "hands_only.hpp"
#include <cstdlib>
#include <iostream>
using namespace circle::motor;
void require(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
    for(auto part:{"body","cylinder","cylinder_hinge","trigger","plunger","cocking_handle"}){
        const auto asset=std::string("models/weapons/relicrevo01/relicrevo01_")+part+".lwo";
        require(handAttachmentAsset(asset,3,true),"Revolver parts share the hand render frame");
    }
    for(unsigned slot:{2u,3u,4u}){
        require(handAttachmentAsset("md6def/weapons/relicpistol02/relicpistol02.md6",slot,true),"Equipped pistol lacks hand render correction");
        require(!handAttachmentAsset("md6def/weapons/whip01/whip_01a_3m.md6",slot,true),"Holstered whip must not receive pistol correction");
    }
    require(handAttachmentAsset("md6def/weapons/whip01/whip_01a_3m.md6",18,true),"Equipped whip lacks hand render correction");
    require(!handAttachmentAsset("md6def/weapons/relicpistol02/relicpistol02.md6",18,true),"Holstered pistol must not receive whip correction");
    for(unsigned slot:{8u,10u})require(handAttachmentAsset("models/props/bottle.idasset",slot,true),"Held melee/carried object must retain correction");
    require(handAttachmentAsset("models/props/journal.idasset",16,true),"Held journal must receive render anchor correction");
    require(!handAttachmentAsset("md6def/weapons/relicpistol02/relicpistol02.md6",16,true)&&!handAttachmentAsset("md6def/weapons/whip01/whip_01a_3m.md6",16,true),"Journal cannot register holstered equipment");
    require(handAttachmentAsset("models/props/camera.idasset",17,true),"Photo camera receives shared hand anchor");
    require(!handAttachmentAsset("md6def/weapons/relicpistol02/relicpistol02.md6",17,true)&&!handAttachmentAsset("md6def/weapons/whip01/whip_01a_3m.md6",17,true),"Camera excludes holstered equipment");
    for(unsigned slot:{0u,1u,14u,255u})require(!handAttachmentAsset("models/props/bottle.idasset",slot,true),"Unsupported or pending object incorrectly registered");
    for(unsigned slot:{2u,3u,4u,8u,10u,16u,17u,18u}){
        require(!handAttachmentAsset("md6def/weapons/relicpistol02/relicpistol02.md6",slot,false),"World weapon must remain unchanged");
        require(!handAttachmentAsset("md6def/characters/guard.md6",slot,true),"Character must not become an attachment");
        require(!handAttachmentAsset("models/wear/coat.idasset",slot,true),"Clothing must not become an attachment");
    }
    // Measured Cane asset in slot23, plus slot-independent equipment fixtures.
    for(unsigned slot:{5u,6u,7u,9u,11u,12u,13u,15u,19u,20u,21u,22u,23u,24u}){
        require(handAttachmentAsset("models/gameplay/interactables/grip/onehand/cylinder_medium/bonk_cane_01a/bonk_cane_01a_wood.idasset",slot,true),"Native first-person attachment must not require per-item slot allowlist");
        require(handAttachmentAsset("md6def/props/new_held_item.md6",slot,true),"New equipment model must share anchor");
        require(!handAttachmentAsset("models/props/item.idasset",slot,false),"Dropped/world item loses anchor");
        require(!handAttachmentAsset("models/characters/npc.idasset",slot,true),"NPC excluded from generic attachment path");
        require(!handAttachmentAsset("models/wear/coat.idasset",slot,true),"Clothing excluded from generic attachment path");
        require(!handAttachmentAsset("md6def/weapons/whip01/whip_01a_3m.md6",slot,true),"Generic equipment does not move holstered whip");
        require(!handAttachmentAsset("md6def/weapons/relicpistol02/relicpistol02.md6",slot,true),"Generic equipment does not move holstered pistol");
    }
    const auto whip=bodyEquipmentAsset("md6def/weapons/whip01/whip_01a_3m.md6");
    const auto pistol=bodyEquipmentAsset("md6def/weapons/relicpistol02/relicpistol02.md6");
    require(whip==BodyEquipment::Whip&&pistol==BodyEquipment::Pistol,"Observed equipment assets not recognized");
    for(auto name:{"_whip","md6def/weapons/unknown.md6","md6def/characters/standard/indiana_jones/indiana_wear_body.md6"})
        require(bodyEquipmentAsset(name)==BodyEquipment::None,"Unknown or non-equipment model hidden");
    // Draw/stow and equip transitions must work on both ring slots, regardless
    // of whether clothing is hidden by the independent Hands Only option.
    for(bool handsOnly:{false,true}){
        require(playerViewAssetHidden("models/wear/indiana_jones/adventurer/indiana_adventurer_lowerbody_01.idasset",handsOnly,1)==handsOnly,"Equipment rule changed full-body clothing");
        for(unsigned slot:{1u,18u,1u,2u,3u,4u,0u,255u}){
            HandsOnlyViewRecord native{};
            for(unsigned ring=0;ring<2;++ring){
                auto w=handsOnlyViewRecord(native,playerViewAssetHidden("md6def/weapons/whip01/whip_01a_3m.md6",handsOnly,slot),1);
                auto p=handsOnlyViewRecord(native,playerViewAssetHidden("md6def/weapons/relicpistol02/relicpistol02.md6",handsOnly,slot),1);
                require(w[44]==(slot==1||slot==2||slot==3||slot==4),"Whip draw/stow visibility wrong");
                require(p[44]==(slot==1||slot==18),"Pistol equip/stow visibility wrong");
            }
        }
    }
    HandsOnlyViewRecord authored{};
    for(unsigned i=0;i<authored.size();++i)authored[i]=static_cast<unsigned char>(i+3);
    std::array<HandsOnlyViewRecord,2> gpu{authored,authored};
    for(unsigned frame=0;frame<8;++frame){
        const bool hide=frame<4;
        auto& slot=gpu[frame%2];slot=handsOnlyViewRecord(authored,hide,0x1234);
        for(unsigned i=0;i<authored.size();++i)
            require(slot[i]==(hide&&i==44?0x34:hide&&i==45?0x12:authored[i]),"GPU override modified bounds, other masks or failed to restore");
    }
    require(gpu[0]==authored&&gpu[1]==authored,"Cinema exit left hidden ring data");
    require(handsOnlyViewRecord(authored,true,0)==authored,"Unknown view ID modified native visibility");
    const auto zeroView=handsOnlyViewRecord(authored,true,0,true);
    for(unsigned i=0;i<authored.size();++i)
        require(zeroView[i]==(i==42||i==43?0xff:authored[i]),"Ledge ID0 only changes surface allow mask, preserves bounds and shadows");
    require(handsOnlyViewRecord(authored,false,0,true)==authored,"Ledge exit restores original surface mask");
    for(auto slot:{"upperbody","lowerbody","footwear","headwear","harness","holster"})
        require(handsOnlyBodyAsset(std::string("models/wear/indiana_jones/adventurer/indiana_adventurer_")+slot+"_01.idasset"),"Body slot not hidden");
    require(handsOnlyBodyAsset("md6def/characters/standard/indiana_jones/indiana_wear_head.md6"),"Head not hidden");
    for(auto name:{"indiana_wear_eyebrows.hairmodel","indiana_wear_eyelashes.hairmodel","indiana_wear_head_beard.hairmodel","indiana_wear_head.hairmodel"})
        require(handsOnlyBodyAsset(name),"Local head hair must not remain inside first-person camera");
    for(auto name:{"md6def/characters/standard/indiana_jones/indiana_wear_body.md6",
        "models/wear/npc/indiana_adventurer_upperbody_01.idasset","models/weapons/indiana_whip.idasset",
        "models/wear/indiana_jones/adventurer/indiana_book.idasset","unknown", ""})
        require(!handsOnlyBodyAsset(name),"Hands, NPC, prop or unknown asset hidden");
    auto context=[](bool enabled=true,bool adapter=true,bool stopped=false,bool gameplay=true,bool cinema=false,
        bool valid=true,bool animated=false,bool local=false,int type=0,int reason=0,unsigned mode=0){
        return handsOnlyContext(enabled,adapter,stopped,gameplay,cinema,valid,animated,local,type,reason,mode);};
    require(context(),"Gameplay disabled");
    require(!context(false)&&!context(true,false)&&!context(true,true,true)&&!context(true,true,false,false),"Disabled adapter/context hidden");
    require(!context(true,true,false,true,true)&&!context(true,true,false,true,false,false),"Cinema or invalid state hidden");
    require(!context(true,true,false,true,false,true,true)&&!context(true,true,false,true,false,true,false,true),"Scripted animation hidden");
    require(!context(true,true,false,true,false,true,false,false,7)&&!context(true,true,false,true,false,true,false,false,0,1),"Traversal hidden");
    for(unsigned mode=0;mode<256;++mode)
        require(context(true,true,false,true,false,true,false,false,0,0,mode)==(mode==0||mode==1||mode==2||mode==9||mode==7||mode==8||mode==20),"Owned ground/water visibility or native scripted visibility incorrect");
    for(unsigned mode:{7u,8u,20u}){
        require(!context(false,true,false,true,false,true,false,false,0,0,mode),"Water hides body despite disabled Hands Only");
        require(!context(true,true,false,true,false,true,true,false,0,0,mode),"Scripted swim animation hidden");
        require(!context(true,true,false,true,true,true,false,false,0,0,mode),"Water cinema hides body");
    }
    const std::string_view torch="models/gameplay/destructibles/gameplay/interactables/grip/onehand/cylinder_medium/torch_set_01a/torch_wood_01a_breakable.idasset";
    const std::string_view flame="props/torch/particle_prop_torch_01.prt";
    const std::string_view blob="models/gameplay/interactables/grip/onehand/cylinder_medium/torch_set_01a/torch_wood_01a_blob.idasset";
    require(heldTorchEffect(torch,"spawned_torch_5",flame,"spawned_torch_5-emitter"),"Held emitter ownership missing");
    require(heldTorchEffect(torch,"spawned_torch_5",blob,"spawned_torch_5-fireblob"),"Held fire geometry ownership missing");
    require(!heldTorchEffect(torch,"spawned_torch_5",flame,"spawned_torch_50-emitter"),"Other torch instance accepted");
    require(!heldTorchEffect(torch,"spawned_torch_5",flame,"spawned_torch_5-emitter-extra"),"Partial child name accepted");
    require(!heldTorchEffect(torch,"",flame,"-emitter")&&!heldTorchEffect(torch,"<unnamed>",flame,"<unnamed>-emitter"),"Anonymous effect accepted");
    require(!heldTorchEffect(torch,"spawned_torch_5","props/braziers/fire.prt","spawned_torch_5-emitter"),"Unrelated particle accepted");
    require(!heldTorchEffect("models/npc.idasset","spawned_torch_5",flame,"spawned_torch_5-emitter"),"Non-torch parent accepted");
    require(namedHeldEffect("held_lantern_41","held_lantern_41-emitter"),"shared effect binding must not require a torch asset list");
    require(!namedHeldEffect("held_lantern_41","held_lantern_410-emitter"),"similar instance prefix is not ownership");
    require(!namedHeldEffect("held_lantern_41","held_lantern_41-impact"),"detached impact is not a held child");
    constexpr std::string_view lighter="item/lighter01-actor_player_11_weapon6";
    require(namedHeldEffect(lighter,"item/lighter01-actor_player_11_weapon6-flame"),"live lighter flame must follow its held parent");
    constexpr std::string_view lighterAsset="md6def/gameplay/interactables/grip/onehand/cube_small/indy_lighter_01a/indy_lighter_01a.md6";
    constexpr std::string_view lighterFx="props/lighter/particle_prop_lighter_01.prt";
    require(rigidLighterFlame(lighterAsset,lighterFx,lighter,"item/lighter01-actor_player_11_weapon6-flame"),"rigid lighter identity");
    require(!rigidLighterFlame(lighterAsset,lighterFx,lighter,"item/lighter01-actor_player_12_weapon6-flame"),"foreign lighter excluded");
    require(!rigidLighterFlame("models/other.idasset",lighterFx,lighter,"item/lighter01-actor_player_11_weapon6-flame"),"other held asset cannot bypass distance gate");
    require(!rigidLighterFlame(lighterAsset,flame,lighter,"item/lighter01-actor_player_11_weapon6-flame"),"other particle cannot use lighter anchor");
    require(heldEffectSuffix("-flame"),"lighter flame must pass attachment prefilter");
    require(!namedHeldEffect(lighter,"item/lighter01-actor_player_11_weapon60-flame"),"other lighter instance excluded");
    require(!namedHeldEffect(lighter,"script_village_asmaa_attachments_weapon_lighter01_1-flame"),"NPC lighter flame excluded");
    require(!namedHeldEffect(lighter,"item/lighter01-actor_player_11_weapon6-flame-extra"),"partial lighter child excluded");
    require(!namedHeldEffect(lighter,"<unnamed>")&&!namedHeldEffect("", "-flame"),"unowned lighter particles excluded");
    // Exact live r303 restored allocation names. Both IDs differ; neither is
    // the player/weapon identity. The old prefilter and parent prefix rejected it.
    constexpr std::string_view restoredParent="item/lighter01-actor_player_11_weapon6_S_27D5";
    constexpr std::string_view restoredFlame="item/lighter01-actor_player_11_weapon6-flame_S_27D2";
    require(namedHeldEffectCandidate(restoredFlame),"restored flame passes native child prefilter");
    require(rigidLighterFlame(lighterAsset,lighterFx,restoredParent,restoredFlame),"restored lighter reaches original rigid GPU fix");
    require(heldEffectName(restoredParent,restoredFlame)=="-flame","different allocation suffix lengths cannot corrupt effect type");
    require(exactHeldEffectName(restoredParent,restoredFlame).empty(),"restored binding must use ambiguity checks");
    require(namedHeldEffect(restoredParent,"item/lighter01-actor_player_11_weapon6_S_27D5-flame"),"new child can retain parent's full restored name");
    require(namedHeldEffect(lighter,restoredFlame)&&namedHeldEffect(restoredParent,"item/lighter01-actor_player_11_weapon6-flame"),"mixed restored/native names retain full actor and slot identity");
    for(auto other:{"item/lighter01-actor_player_12_weapon6-flame_S_27D2","item/lighter01-actor_player_11_weapon60-flame_S_27D2","script_village_asmaa_attachments_weapon_lighter01_1-flame_S_27D2","item/lighter01-actor_player_11_weapon6-flame-extra_S_27D2"})
        require(!namedHeldEffect(restoredParent,other),"restored actor/item/suffix identity remains exact");
    for(auto malformed:{"x_S_","x_S_27D2tail","x_S_-123","x_S_123456789","x_S_1Z2","_S_123"})
        require(restoredInstanceBase(malformed)==malformed,"malformed restoration suffix is not stripped");
    require(!namedHeldEffectCandidate("item/lighter01-actor_player_11_weapon6-flame_S_27D2-extra"),"nonterminal allocation suffix is not normalized");
    require(!rigidLighterFlame("models/other.idasset",lighterFx,restoredParent,restoredFlame),"restoration cannot bypass exact lighter asset gate");
    require(!namedHeldEffect("<unnamed>_S_1","<unnamed>-flame_S_2"),"restoration cannot make anonymous instances eligible");
    require(heldTorchEffect(torch,"spawned_torch_5_S_10",flame,"spawned_torch_5-emitter_S_20"),"restored torch uses shared ownership fix");
    require(!heldTorchEffect(torch,"spawned_torch_5_S_10",flame,"spawned_torch_50-emitter_S_20"),"restored world torch still requires exact instance stem");
    HeldEffectParentChoice choice;
    constexpr std::string_view candle="models/lamps/standing/standing_candles_generic_set_01a/s_candles_generic_01a.idasset";
    constexpr std::string_view candleFx="props/lighter/particle_prop_lighter_01_candlestick_nosound.prt";
    require(heldCandleEffect(candle,"gp_library_indys_candle",candleFx,"gp_library_indys_candle-emitter"),"observed library candle/emitter compatible");
    constexpr auto wax="vfx/alembic/maps/ch02_vatican/se02/modelasset_vatican_candlewax01.idasset";
    require(heldCandleWax(candle,"gp_library_indys_candle",wax,"gp_library_gameplay_fx_alembic_1"),"observed Indy wax attachment");
    require(heldCandleWax(candle,"gp_library_indys_candle_S_123",wax,"gp_library_gameplay_fx_alembic_1_S_AB"),"restored wax attachment");
    require(!heldCandleWax(candle,"gp_library_antonios_candle",wax,"gp_library_gameplay_fx_alembic_1"),"NPC candle excluded");
    require(!heldCandleWax(candle,"gp_library_indys_candle",wax,"gp_library_gameplay_fx_alembic_2"),"other scene wax excluded");
    require(!heldCandleWax(candle,"gp_library_indys_candle",candleFx,"gp_library_gameplay_fx_alembic_1"),"flame not classified as wax");

    require(!heldCandleEffect(candle,"gp_library_indys_candle",candleFx,"gp_library_antonios_candle-emitter"),"Antonio emitter cannot bind to Indy candle");
    require(!heldCandleEffect(candle,"gp_library_indys_candle",candleFx,"gp_library_indys_candle2-emitter"),"similar candle instance is not ownership");
    require(!heldCandleEffect(candle,"gp_library_indys_candle","environment/fire/particle_candle_flame_large_01.prt","gp_library_indys_candle-emitter"),"world candle particle excluded");
    require(!heldCandleEffect(candle,"gp_library_indys_candle",lighterFx,"gp_library_indys_candle-emitter"),"lighter nozzle path not reused for candle");
    require(!heldCandleEffect(torch,"gp_library_indys_candle",candleFx,"gp_library_indys_candle-emitter"),"other parent asset excluded");
    require(heldCandleEffect(candle,"gp_library_indys_candle_S_123",candleFx,"gp_library_indys_candle-emitter_S_456"),"restored candle instance retains complete identity");
    require(!heldCandleEffect(candle,"<unnamed>",candleFx,"<unnamed>-emitter"),"anonymous candle excluded");
    require(!handAttachmentAsset(candle,6,false)&&handAttachmentAsset(candle,6,true),"world/NPC candle cannot enter held-parent path without first-person ownership gate");
    require(choice.observe(1,false)&&choice.valid(),"unique restored parent accepted");
    require(!choice.observe(1,false)&&choice.valid(),"duplicate registry observation is not another parent");
    require(!choice.observe(2,false)&&!choice.valid(),"two live restored parent candidates rejected");
    require(choice.observe(3,true)&&choice.valid()&&choice.parent==3,"direct native binding resolves weaker ambiguity");
    require(!choice.observe(4,false)&&choice.valid()&&choice.parent==3,"weaker restored match cannot replace direct binding");
    require(!choice.observe(4,true)&&!choice.valid(),"conflicting direct parents rejected");
    const auto clothing="models/wear/indiana_jones/adventurer/indiana_adventurer_upperbody_02.idasset";
    for(auto fur:{"indiana_nepal_jacket.hairmodel","indiana_nepal_hat.hairmodel","indiana_beard_nepal.hairmodel"}){
        require(traversalBodyInstance(fur,"actor_player_11:hair_4"),"Himalaya traversal fur remains visible");
        require(traversalBodyInstance(fur,"actor_player_11:hair_4_S_548E"),"Restored Himalaya fur rejected");
        require(playerViewAssetHidden(fur,true,18),"Himalaya fur absent from shared visibility policy");
        require(!playerViewAssetHidden(fur,false,18),"Himalaya fur hidden outside body suppression");
        require(!traversalBodyInstance(fur,"actor_npc_11:hair_4"),"NPC Himalaya fur hidden");
    }
    require(!handsOnlyBodyAsset("models/wear/indiana_jones/nepal/indiana_nepal_gloves.idasset"),"Himalaya gloves hidden");
    require(!handsOnlyBodyAsset("unknown_nepal.hairmodel"),"Unknown hair blanket hidden");
    require(traversalBodyInstance(clothing,"actor_player_11:wear[UPPER_BODY]_S_B41"),"Streamed traversal clothing rejected");
    require(traversalBodyInstance("indiana_wear_head.hairmodel","actor_player_11:hair_0_S_548E"),"Streamed head hair rejected");
    require(!traversalBodyInstance(clothing,"gp_zeppelin_gameplay_animated_moe:wear[UPPER_BODY]"),"NPC wearing player clothing hidden");
    require(!traversalBodyInstance(clothing,"actor_player_:wear[UPPER_BODY]")&&!traversalBodyInstance(clothing,"actor_player_11_fake:wear[UPPER_BODY]"),"Invalid player namespace accepted");
    require(!traversalBodyInstance("indiana_hand_left.hairmodel","actor_player_11:hair_4_S_6CBE"),"Hand hair hidden");
    require(!traversalBodyInstance("md6def/characters/standard/indiana_jones/indiana_wear_body.md6","actor_player_11"),"Hand mesh body hidden");
    require(cockpitPlayerHidden(true,false,true,false,true,false,false,5,0,27,22),"mounted cockpit must hide player independently of Hands Only setting");
    require(!cockpitPlayerHidden(true,false,true,true,true,false,false,5,0,27,22),"cinema must restore player");
    require(!cockpitPlayerHidden(true,false,true,false,false,false,false,5,0,27,22),"stale context cannot hide player");
    require(!cockpitPlayerHidden(true,false,true,false,true,false,false,0,0,0,22),"walking cannot hide player");
    require(!cockpitPlayerHidden(true,false,true,false,true,false,false,5,0,27,1),"leaving mounted gun restores player");
    require(!cockpitPlayerHidden(true,false,true,false,true,true,false,5,0,27,22),"scripted cinematic animation excluded");
    require(!cockpitPlayerHidden(false,false,true,false,true,false,false,5,0,27,22)&&!cockpitPlayerHidden(true,true,true,false,true,false,false,5,0,27,22),"inactive adapter cannot hide models");
    for(auto asset:{"md6def/characters/standard/indiana_jones/indiana_wear_body.md6","indiana_hand_left.hairmodel","indiana_hand_right.hairmodel","models/wear/indiana_jones/nepal/indiana_nepal_gloves.idasset"}){
        require(cockpitPlayerAsset(asset)&&cockpitViewAssetHidden(asset),"cockpit must hide hands, gloves and body too");
        require(!handsOnlyBodyAsset(asset),"ordinary Hands Only must still retain hands");
        require(cockpitPlayerInstance(asset,"actor_player_11:wear[GLOVES]_S_58A"),"restored local cockpit model recognized");
        require(!cockpitPlayerInstance(asset,"actor_npc_11:wear[GLOVES]")&&!cockpitPlayerInstance(asset,"actor_player_11_fake"),"same-asset NPC or partial namespace excluded");
    }
    require(cockpitPlayerInstance("md6def/characters/standard/indiana_jones/indiana_wear_body.md6","actor_player_11"),"root player model included");
    for(auto asset:{"md6def/weapons/airplane_machinegun.md6","models/vehicles/airplane.idasset","models/props/cockpit.idasset","md6def/characters/npc.md6"})
        require(!cockpitViewAssetHidden(asset)&&!cockpitPlayerInstance(asset,"actor_player_11"),"gun, airplane and NPC stay visible");
    require(cockpitViewAssetHidden("md6def/weapons/whip01/whip_01a_3m.md6")&&cockpitViewAssetHidden("md6def/weapons/relicpistol02/relicpistol02.md6"),"registered body equipment hidden in cockpit");
    HandsOnlyViewRecord visible{};visible[7]=42;
    require(handsOnlyViewRecord(visible,true,1)!=visible&&handsOnlyViewRecord(visible,true,2)!=visible,"both eye view IDs suppressed");
    require(handsOnlyViewRecord(visible,true,0,true)!=visible,"primary view zero suppression supported");
    require(handsOnlyViewRecord(visible,false,1)==visible,"exit rebuilds original visibility without retained hidden mask");
    std::cout<<"Hands Only asset and context policy passed\n";
}
