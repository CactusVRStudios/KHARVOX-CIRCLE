#include "stereo_seq/eye_pairing.hpp"
#include "stereo_seq/eye_tags.hpp"
#include "stereo_seq/render_idle.hpp"
#include "object_ring.hpp"
#include "joint_pair.hpp"
#include "npc_skin_pair.hpp"
#include "npc_model_policy.hpp"
#include "secondary_animation.hpp"
#include "real_frame_submission.hpp"
#include "model_pair.hpp"
#include "world_model_pair.hpp"
#include "hand_hair.hpp"
#include "hair_projection.hpp"
#include "effect_history.hpp"
#include <limits>
#include <cstdio>
using namespace circlevr::stereo_seq;
int main() {
    int errors=0;
    auto check=[&](bool ok,const char* name){if(!ok){std::fprintf(stderr,"FAIL: %s\n",name);++errors;}};
    {
        // Wax compute jobs must use the candle origin (no lighter nozzle offset)
        // and a matching inverse at arbitrary position and wrist rotation.
        const std::array<float,15> pose{4,-7,2,0,1,0,-1,0,0,0,0,1,1,1,1};
        std::array<float,16> f{},v{};
        check(circle::motor::heldWaxMatrices(pose,f,v),"wax compute matrix pair");
        check(f[3]==4&&f[7]==-7&&f[11]==2,"wax retains candle origin");
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
            float sum=0;for(unsigned k=0;k<4;++k)sum+=f[r*4+k]*v[k*4+c];
            check(std::abs(sum-(r==c?1.f:0.f))<.00001f,"wax forward inverse roundtrip");
        }
        auto bad=pose;bad[3]=2;const auto saved=f;
        check(!circle::motor::heldWaxMatrices(bad,f,v)&&f==saved,"invalid wax basis leaves job intact");
    }


    {
        using namespace circle::motor;
        using namespace circle::motor::hands;
        const std::array<float,15> parent{10,20,30,1,0,0,0,1,0,0,0,1,1,1,1};
        auto emitter=parent;emitter[2]+=.46634f;
        TorchEmitterLocal binding;
        check(!binding.observe(1,parent,emitter)&&!binding.observe(1,parent,emitter),"torch calibration requires distinct simulation samples");
        check(binding.observe(2,parent,emitter)&&std::abs(binding.local[2]-.46634f)<1e-5f,"held torch instance captures measured height");
        auto stale=emitter;stale[0]+=2;
        check(binding.observe(3,parent,stale)&&std::abs(binding.local[2]-.46634f)<1e-5f,"stale native world pose cannot replace confirmed local anchor");
        TorchEmitterLocal torn;
        check(!torn.observe(1,parent,stale)&&!torn.observe(2,parent,stale),"misaligned parent child snapshots cannot calibrate torch");
        auto tilted=emitter;tilted[3]=0;tilted[4]=1;tilted[6]=-1;tilted[7]=0;
        check(!torn.observe(3,parent,tilted),"different native update rotations rejected");
        const auto first=binding.local;binding={};emitter[2]=parent[2]+.56634f;
        check(!binding.observe(4,parent,emitter)&&binding.observe(5,parent,emitter)&&
            std::abs(binding.local[2]-first[2]-.1f)<1e-5f,"re-equipped different torch acquires its own height");
        HeldFlameEmitterLocal candle;
        emitter=parent;emitter[2]+=.410f;
        check(!candle.observe(6,parent,emitter)&&candle.observe(7,parent,emitter)&&
            std::abs(candle.local[2]-.410f)<1e-5f,"candlestick acquires its own measured offset, not lighter/torch height");
        HeldFlameEmitterLocal dormant;
        auto dormantEmitter=parent;dormantEmitter[0]-=3.67886f;dormantEmitter[1]-=.20469f;dormantEmitter[2]-=2.41611f;
        check(!dormant.observe(8,parent,dormantEmitter)&&!dormant.observe(9,parent,dormantEmitter),"observed dormant library emitter mismatch must not become a calibrated offset");
        for(float height:{.410f,.46634f,.56634f})for(unsigned step=0;step<80;++step){
            const auto q=normalized(Quat{.01f*step,.005f*step,.02f*step,1});
            const auto x=rotate(q,{1,0,0}),y=rotate(q,{0,1,0}),z=rotate(q,{0,0,1});
            const Vec origin{10+step*.04f,20-step*.02f,30};
            std::array<float,16> matrix{x.x,y.x,z.x,origin.x,x.y,y.y,z.y,origin.y,x.z,y.z,z.z,origin.z,0,0,0,1};
            std::array<float,15> local{0,0,height,1,0,0,0,1,0,0,0,1,1,1,1},placed{};
            check(rigidParticlePlacement(matrix,local,{1,1,1},placed),"torch pose accepts simultaneous artificial translation and hand/stick turn");
            const auto expected=origin+rotate(q,{0,0,height});
            check(length(Vec{placed[0],placed[1],placed[2]}-expected)<1e-5f,"torch flame stays at authored tip in current rendered parent frame");
            const auto gpu=gpuEmitterRotation(placed);
            check(gpu&&length(rotate(Quat{-gpu->x,-gpu->y,-gpu->z,gpu->w},{0,0,1})-z)<1e-5f,"torch GPU rotation uses verified native quaternion convention");
            local[2]=0;
            check(rigidParticlePlacement(matrix,local,{1,1,1},placed)&&
                length(Vec{placed[0],placed[1],placed[2]}-origin)<1e-5f,"fireblob follows exact torch model origin without accumulating another delta");
        }
    }
    {
        // Read-only native BE0680 queue capture, independent of our encoder.
        const std::array<float,15> captured{0,0,0,
            .998442113f,.042245843f,.036450088f,
            -.032644317f,.972064376f,-.232433274f,
            -.045251168f,.230881289f,.971929073f,1,1,1};
        const auto q=circle::motor::gpuEmitterRotation(captured);
        check(q&&std::abs(q->x-.116671197f)<1e-6f&&std::abs(q->y-.020573892f)<1e-6f&&
            std::abs(q->z+.018858731f)<1e-6f&&std::abs(q->w-.992778361f)<1e-6f,
            "GPU quaternion matches captured native emitter convention");
        using namespace circle::motor::hands;
        for(auto axis:std::array<Vec,4>{Vec{1,0,0},Vec{0,1,0},Vec{0,0,1},unit(Vec{1,2,3})})
        for(float angle:std::array<float,6>{-2.9f,-1.57f,0.f,1.57f,2.9f,3.14f}){
            const float s=std::sin(angle/2);Quat hand{axis.x*s,axis.y*s,axis.z*s,std::cos(angle/2)};
            const auto x=rotate(hand,{1,0,0}),y=rotate(hand,{0,1,0}),z=rotate(hand,{0,0,1});
            std::array<float,16> matrix{x.x,y.x,z.x,10,x.y,y.y,z.y,20,x.z,y.z,z.z,30,0,0,0,1};
            std::array<float,15> pose{};
            check(circle::motor::lighterParticlePlacement(matrix,{1,1,1},pose),"hand rotation produces rigid nozzle placement");
            const auto encoded=circle::motor::gpuEmitterRotation(pose);
            check(bool(encoded),"all rotation branches encode for GPU");
            if(!encoded)continue;
            const Quat decoded{-encoded->x,-encoded->y,-encoded->z,encoded->w};
            const Vec point{.013f,.007f,.025f};
            const auto actual=Vec{pose[0],pose[1],pose[2]}+rotate(decoded,point);
            const auto expected=Vec{10,20,30}+rotate(hand,Vec{.008f,0,.050f}+point);
            check(length(actual-expected)<1e-5f,"nozzle and noncentral flame point rotate together about lighter in pitch roll and yaw");
        }
        auto invalid=captured;invalid[3]=2;
        check(!circle::motor::gpuEmitterRotation(invalid),"nonrigid emitter basis rejected");
    }
    {
        // Captured native descriptor: its first qword is a link, not capacity.
        std::array<unsigned char,0x78> raw{0xb0,0x6f,0xa5,0xac,0x4e,0x01,0x00,0x00,0x00,0x80,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xd0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x50,0x16,0xc5,0x4b,0x4b,0x01,0x00,0x00,0x00,0x08,0xf4,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0xb6,0x83,0x4d,0x01,0x00,0x00,0x00,0x80,0x06,0x00,0x00,0x00,0x00,0x00,0xd7,0x7f,0x00,0x00,0x2e,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x0f,0x00,0xff,0x01,0x00,0x08,0xb6,0x83,0x4d,0x01,0x00,0x00,0x00,0x80,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x01,0x00,0x03,0x00,0xd0,0xc6,0x22,0x6c,0x4b,0x01,0x00,0x00};
        auto b=circle::motor::GpuEmitterBuffer::decode(raw);
        check(b.capacity==0x68000&&b.contains(61)&&b.contains(2047)&&!b.contains(2048)&&!b.contains(-1),"native emitter descriptor capacity and exact last-record boundary");
        raw[0]^=0x70;
        check(circle::motor::GpuEmitterBuffer::decode(raw).capacity==0x68000,"allocation-link changes do not invalidate buffer capacity");
        raw[0x14]=0;
        check(!circle::motor::GpuEmitterBuffer::decode(raw).mapped,"unexpected record stride rejects emitter buffer");
        raw[0x14]=0xd0;
        for(unsigned i=0x50;i<0x58;++i)raw[i]=0;
        check(!circle::motor::GpuEmitterBuffer::decode(raw).contains(0),"unmapped descriptor cannot expose records");
    }
    {
        std::array<float,15> flame{};
        for(unsigned step=0;step<100;++step){
            const float yaw=step*.05f,c=std::cos(yaw),v=std::sin(yaw);
            const float x=step*.1f,y=step*.07f;
            std::array<float,16> rendered{c,-v,0,x,v,c,0,y,0,0,1,2,0,0,0,1};
            check(circle::motor::lighterParticlePlacement(rendered,{.5f,2.f,1.f},flame),"moving rendered lighter matrix accepted");
            check(std::abs(flame[0]-(x+.008f*c))<1e-5f&&std::abs(flame[1]-(y+.008f*v))<1e-5f&&std::abs(flame[2]-2.05f)<1e-5f,"GPU emitter follows same rendered parent during translation and yaw");
            check(flame[3]==c&&flame[4]==v&&flame[12]==.5f&&flame[13]==2.f,"particle rotation stays unscaled with authored size retained");
        }
        const auto saved=flame;
        std::array<float,16> bad{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0};
        check(!circle::motor::lighterParticlePlacement(bad,{1,1,1},flame)&&flame==saved,"invalid affine cannot replace particle pose");
        bad[15]=1;bad[0]=2;
        check(!circle::motor::lighterParticlePlacement(bad,{1,1,1},flame)&&flame==saved,"nonrigid parent cannot contaminate particle history");
    }
    {
        circle::motor::LighterParticleHistory h;
        std::array<float,15> pose{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
        check(h.advance(1,2,3,4,5,10,100,1000,pose)&&h.previous==pose,"new emitter starts without inherited motion");
        const auto first=pose;pose[0]=1;
        check(!h.advance(1,2,3,4,5,11,110,1010,pose)&&h.previous==first&&h.current==pose,"emitter translation history is corrected-to-corrected");
        auto right=pose;right[0]=2;
        check(!h.advance(1,2,3,4,5,11,110,1011,right)&&h.current==right&&h.previous==first,"late same-timestamp parent update refreshes current without advancing previous");
        auto lateTurn=right;lateTurn[3]=0;lateTurn[4]=1;lateTurn[6]=-1;lateTurn[7]=0;
        check(!h.advance(1,2,3,4,5,11,110,1012,lateTurn)&&h.current==lateTurn&&h.previous==first,"same-timestamp yaw refreshes emitter basis without a second history step");
        check(!h.advance(1,2,3,4,5,11,110,1013,lateTurn)&&h.current==lateTurn&&h.previous==first,"identical repeated render is idempotent");
        auto rotated=pose;rotated[3]=0;rotated[4]=1;rotated[6]=-1;rotated[7]=0;
        h.advance(1,2,3,4,5,12,120,1020,rotated);
        check(h.previous==lateTurn&&h.current==rotated,"next timestamp inherits the latest rendered pose, not the first stale pose");
        check(h.advance(1,2,6,4,5,13,130,1030,pose)&&h.previous==pose,"recreated GPU runtime resets history");
        check(h.advance(1,7,6,4,5,14,140,1040,rotated)&&h.previous==rotated,"recycled committed model resets history");
        check(h.advance(1,7,6,8,5,15,150,1050,pose)&&h.previous==pose,"different held parent resets history");
        check(h.advance(1,7,6,8,5,16,160,1500,rotated)&&h.previous==rotated,"holster gap does not create a velocity spike");
        check(h.advance(1,7,6,8,5,17,90,1510,pose)&&h.previous==pose,"time rewind discards old motion");
        check(h.advance(1,7,6,8,5,18,100,1520,rotated,123)&&h.previous==rotated,"recycled GPU stage IDs reset same-runtime history");
    }
    {
        using namespace circle::motor;
        for(const auto type:{WorldModelKind::Static,WorldModelKind::Skinned,WorldModelKind::Deform,WorldModelKind::Breakable,WorldModelKind::GeomCache})
            check(pairableWorldModel("arbitrary/new/path.mesh","world_instance",false,type),"supported type independent of asset path");
        check(worldModelKind(".?AVidRenderModelSkinned@@")==WorldModelKind::Skinned&&worldModelKind(".?AVidRenderModelStatic@@")==WorldModelKind::Static,"native RTTI classes");
        check(pairableWorldModel("md6def/animals/dog/dog.md6","dog",false,WorldModelKind::Skinned),"actual dog root admitted");
        check(pairableWorldModel("models/vehicles/military/italian_truck_01/italian_truck_01a.idasset","truck",false,WorldModelKind::Static),"actual truck root admitted");
        for(const auto name:{".?AVidRenderModelParticle@@",".?AVidRenderModelHair@@",".?AVidRenderModelGui@@",".?AVidRenderModelUnknown@@"})
            check(!pairableWorldModel("models/setdressing/door.idasset","door",false,worldModelKind(name)),"unsupported procedural type remains native despite name");
        check(!pairableWorldModel("models/wear/gina/body.idasset","npc_gina",false,WorldModelKind::Deform)&&
              !pairableWorldModel("item","grip-actor_player_11",false,WorldModelKind::Static)&&
              !pairableWorldModel("item","held",true,WorldModelKind::Skinned),"specialized player and wear paths excluded");
        std::array<float,15> a{1,2,3,1,0,0,0,1,0,0,0,1,1,1,1},b=a,out{};
        // Door rotation about a hinge plus translation and animated scale.
        b[0]=2;b[3]=0;b[4]=1;b[6]=-1;b[7]=0;b[12]=1.05f;
        WorldModelPair p;
        check(!p.select(true,1,2,3,4,5,11,b,out),"right without left remains native");
        check(p.select(false,1,2,3,4,5,10,a,out)&&out==a,"left affine captured");
        check(p.select(false,1,2,3,4,5,10,b,out)&&out==b,"later left rebuild retains native affine");
        check(p.select(true,1,2,3,4,5,11,a,out)&&out==b,"right shares final left origin rotation and scale");
        check(!p.select(true,1,2,3,4,6,11,b,out)&&!p.select(true,1,2,3,4,5,12,b,out),"stale token/frame rejected");
        check(!p.select(true,9,2,3,4,5,11,b,out)&&!p.select(true,1,9,3,4,5,11,b,out)&&
              !p.select(true,1,2,9,4,5,11,b,out)&&!p.select(true,1,2,3,9,5,11,b,out),"recycled identity rejected");
        check(p.select(false,1,2,3,4,6,12,b,out)&&out==b&&p.select(true,1,2,3,4,6,13,b,out)&&out==b,"next pair advances normally");
        auto invalid=b;invalid[12]=0;
        check(!p.select(false,1,2,3,4,7,14,invalid,out),"singular scale rejected");
        invalid=b;invalid[4]=std::numeric_limits<float>::quiet_NaN();
        check(!p.select(false,1,2,3,4,7,14,invalid,out),"invalid rotation rejected");
        p.select(false,1,2,3,4,8,16,a,out);
        check(!p.select(false,1,2,3,4,8,16,invalid,out)&&
              !p.select(true,1,2,3,4,8,17,b,out),"failed later left update invalidates earlier same-pair pose");
        // A door root is rebuilt after an attachment has received the newer
        // parent pose. Replaying the first left root would separate the parts.
        WorldModelPair door,plate;
        auto closed=a,opened=a,attached=a;
        opened[3]=0;opened[4]=1;opened[6]=-1;opened[7]=0;
        attached=opened;attached[1]+=.75f; // authored offset along rotated local X
        door.select(false,10,20,3,40,9,18,closed,out);
        plate.select(false,11,21,3,41,9,18,attached,out);
        std::array<float,15> leftDoor{},rightDoor{},rightPlate{};
        door.select(false,10,20,3,40,9,18,opened,leftDoor);
        door.select(true,10,20,3,40,9,19,closed,rightDoor);
        plate.select(true,11,21,3,41,9,19,closed,rightPlate);
        check(leftDoor==opened&&rightDoor==opened&&rightPlate==attached&&
              rightPlate[0]-rightDoor[0]==.75f*rightDoor[3]&&
              rightPlate[1]-rightDoor[1]==.75f*rightDoor[4],
              "door rebuild preserves attachment local offset in both eyes");
        // Live Vatican library door plate uses (1,-1,1). Rejecting it while
        // pairing its unmirrored door splits the two parts between eye times.
        for(unsigned axis=0;axis<3;++axis){
            auto mirrored=attached;mirrored[12+axis]=-1;
            auto advanced=mirrored;advanced[0]+=.15f;advanced[3]=1;advanced[4]=0;
            WorldModelPair reflected;
            check(reflected.select(false,11,21,3,41,10,20,mirrored,out)&&
                  reflected.select(true,11,21,3,41,10,21,advanced,out)&&out==mirrored,
                  "mirrored door attachment retains exact left affine including signed scale");
            check(reflected.select(false,11,21,3,41,11,22,advanced,out)&&
                  reflected.select(true,11,21,3,41,11,23,mirrored,out)&&out==advanced,
                  "mirrored attachment advances on next pair without freezing movement");
            auto singular=mirrored;singular[12+axis]=0;
            check(!reflected.select(false,11,21,3,41,12,24,singular,out)&&
                  !reflected.select(true,11,21,3,41,12,25,advanced,out),
                  "zero scale still invalidates mirrored attachment pair");
        }
    }
    {
        circle::motor::NpcSkinPair skin;
        check(!skin.reusable(1,9,20,0,true),"uninitialized skin history remains native");
        skin.observeLeft(1,9,20,0,true);
        check(skin.selected==1&&skin.reusable(1,9,21,1,true),"right retains the geometry selected by left");
        check(skin.reusable(1,9,20,1,false),"same-render duplicate must not advance geometry twice");
        check(!skin.reusable(1,9,21,0,true),"externally changed geometry slot is not overwritten");
        check(!skin.reusable(2,9,21,1,true)&&!skin.reusable(1,10,21,1,true),"new pose allocation or simulation invalidates reuse");
        check(!skin.reusable(1,9,22,1,true)&&!skin.reusable(1,9,21,1,false),"only adjacent right render can reuse left");
        skin.observeLeft(1,10,22,1,true);check(skin.selected==0,"next simulation advances geometry normally");
        skin.observeLeft(1,11,24,0,false);check(skin.selected==0,"nonmoving native slot retained");
        skin.observeLeft(1,12,26,2,true);check(!skin.pose,"invalid slot clears history");
    }
    {
        circle::motor::RealFrameSubmission f;
        check(!f.fresh(0)&&f.wait(true,true,false,0,0),"startup waits for a real image");
        check(!f.wait(false,true,false,0,0),"default submission unchanged");
        check(f.fresh(1)&&!f.wait(true,true,false,1,5),"new completed pair immediately eligible");
        f.accept(1,true,false);check(f.fresh(1),"failed submission can retry");
        f.accept(1,false,true);check(f.fresh(1),"empty frame does not consume image");
        f.accept(1,true,true);check(!f.fresh(1)&&f.fresh(2),"successful pair not repeated");
        check(f.wait(true,true,false,1,79)&&!f.wait(true,true,false,1,80),"bounded stall heartbeat");
        check(!f.wait(true,false,false,1,0)&&!f.wait(true,true,true,1,0),"stop and resize cancel wait");
    }
    {
        using circle::motor::secondaryAnimationFlags;
        // The verified JNE must take the native skip path, without disturbing
        // unrelated machine flags or changing unowned renders/comparison mode.
        constexpr uintptr_t flags=0x2d7;
        check(!(secondaryAnimationFlags(flags,true)&0x40),"VR secondary deformation takes native skip branch");
        check((secondaryAnimationFlags(flags,true)^flags)==0x40,"unrelated CPU flags preserved");
        check(secondaryAnimationFlags(flags,false)==flags,"unowned native rendering unchanged");
        check(secondaryAnimationFlags(flags,true,true)==flags,"live native comparison restores original branch");
        check(secondaryAnimationFlags(flags&~uintptr_t{0x40},true)==(flags&~uintptr_t{0x40}),"native skip already selected is unchanged");
    }
    {
        // Parent translates/turns after IK: every bound child retains its local
        // pose (rope root, emitter, secondary mesh), without mutating originals.
        const std::array<float,15> parent{10,20,30,1,0,0,0,1,0,0,0,1,1,1,1};
        const std::array<float,15> moved{11,22,33,0,1,0,-1,0,0,0,0,1,1,1,1};
        const std::array<float,15> child{10.5f,20,31,1,0,0,0,1,0,0,0,1,.5f,.5f,.5f};
        auto result=child;
        check(circle::motor::rebaseAttachedPlacement(parent,moved,result),"bound child rebased");
        check(std::abs(result[0]-11)<1e-5f&&std::abs(result[1]-22.5f)<1e-5f&&std::abs(result[2]-34)<1e-5f,"child keeps parent-relative offset across movement and turn");
        check(result[3]==0&&result[4]==1&&result[6]==-1&&result[12]==.5f,"child orientation follows parent; authored scale retained");
        auto restored=result;
        check(circle::motor::rebaseAttachedPlacement(moved,parent,restored),"inverse parent delta accepted");
        for(unsigned i=0;i<15;++i)check(std::abs(restored[i]-child[i])<1e-5f,"parent correction reversible without accumulated drift");
        auto invalid=parent;invalid[3]=2;result=child;
        check(!circle::motor::rebaseAttachedPlacement(invalid,moved,result)&&result==child,"invalid basis leaves native child intact");
        invalid=parent;invalid[12]=2;
        check(!circle::motor::rebaseAttachedPlacement(invalid,moved,result)&&result==child,"nonrigid parent rejected");

    }
    {
        const std::array<float,15> parent{11,22,33,0,1,0,-1,0,0,0,0,1,1,1,1};
        std::array<float,15> flame{-100,400,-200,1,0,0,0,1,0,0,0,1,.5f,.5f,.5f};
        check(circle::motor::placeRigidLighterFlame(parent,flame),"stale lighter origin repaired");
        check(std::abs(flame[0]-11)<1e-5f&&std::abs(flame[1]-22.008f)<1e-5f&&std::abs(flame[2]-33.05f)<1e-5f,"lighter offset rotates with current parent");
        check(flame[3]==0&&flame[4]==1&&flame[12]==.5f,"lighter axes and scale retained");
        const auto once=flame;
        check(circle::motor::placeRigidLighterFlame(parent,flame)&&flame==once,"lighter correction never accumulates");
        auto next=parent;next[0]+=8;
        check(circle::motor::placeRigidLighterFlame(next,flame)&&std::abs(flame[0]-19)<1e-5f,"lighter follows movement despite frozen emitter");
        auto invalid=parent;invalid[3]=2;const auto before=flame;
        check(!circle::motor::placeRigidLighterFlame(invalid,flame)&&flame==before,"invalid lighter parent leaves input intact");
    }
    {
        const std::array<float,16> parent{0,-1,0,11,1,0,0,22,0,0,1,33,0,0,0,1};
        std::array<float,16> current{},inverse{};
        check(circle::motor::lighterUploadMatrices(parent,{.5f,2.f,1.f},current,inverse),"lighter upload from actual parent affine");
        check(std::abs(current[3]-11)<1e-5f&&std::abs(current[7]-22.008f)<1e-5f&&std::abs(current[11]-33.05f)<1e-5f,"upload offset uses parent rotation");
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){float v=0;
            for(unsigned k=0;k<4;++k)v+=current[r*4+k]*inverse[k*4+c];
            check(std::abs(v-(r==c?1.f:0.f))<1e-4f,"uploaded forward/inverse agree with nonuniform particle scale");}
        auto next=parent;next[3]+=5;
        check(circle::motor::lighterUploadMatrices(next,{1,1,1},current,inverse)&&current[3]==16,"upload refresh does not need a new child pose");
        next[0]=2;const auto saved=current;
        check(!circle::motor::lighterUploadMatrices(next,{1,1,1},current,inverse)&&current==saved,"invalid parent upload cannot replace output");
    }
    {
        using circle::motor::scatterStereoEye;
        RenderTag tag{Eye::Left,123,true,true,9,100,1};
        check(scatterStereoEye(true,false,true,99,tag)==0,"scattering fallback accepts captured left pass");
        tag.eye=Eye::Right;tag.backendFrame=101;
        check(scatterStereoEye(true,false,true,100,tag)==1,"scattering fallback accepts captured right pass");
        check(!scatterStereoEye(true,false,true,99,tag),"different backend render cannot reuse stale eye tag");
        check(!scatterStereoEye(false,false,true,100,tag)&&!scatterStereoEye(true,true,true,100,tag),"inactive or failed adapter preserves native scattering");
        check(!scatterStereoEye(true,false,false,100,tag),"secondary view keeps its native history");
        check(!scatterStereoEye(true,false,true,100,std::nullopt),"untagged pass keeps its native history");
        tag.viewApplied=false;
        check(!scatterStereoEye(true,false,true,100,tag),"unmodified camera keeps native history");
        tag.viewApplied=true;tag.eye=Eye::Mono;
        check(!scatterStereoEye(true,false,true,100,tag),"mono/menu fallback keeps native history");
        tag.eye=Eye::Left;tag.tick=0;
        check(!scatterStereoEye(true,false,true,100,tag),"unknown simulation never forces a stereo effect");
        // Water's reset would remove its essential temporal reconstruction;
        // the unsuccessful live comparison is intentionally NOT adopted.
    }
    const float hairCutoff=.0001f;
    // Projected area falls with distance squared: an asset at the old cutoff
    // now remains eligible at twice that distance, but still has a finite cap.
    for(float distance:{1.f,1.5f,2.f,2.01f,4.f}){
        const float area=hairCutoff/(distance*distance);
        const auto compared=circle::motor::hairVisibilityArea(area);
        check(compared&&(*compared>=hairCutoff)==(distance<=2),"hair cutoff extends visibility to twice the old distance");
    }
    check(circle::motor::hairVisibilityArea(0)==0.f,"zero projected area stays zero");
    check(!circle::motor::hairVisibilityArea(-1)&&!circle::motor::hairVisibilityArea(std::numeric_limits<float>::infinity())&&
        !circle::motor::hairVisibilityArea(std::numeric_limits<float>::max())&&
        !circle::motor::hairVisibilityArea(std::numeric_limits<float>::quiet_NaN()),"invalid or overflowing area never overrides native visibility");
    // Projection of a finite segment is the difference of two projected
    // endpoints, not the endpoint's distance from the centre of the image.
    std::array<float,16> hp{1.1f,0,0,0, 0,1.2f,0,0, 0,0,-1,-.06f, 0,0,-1,0};
    const auto symmetric=circle::motor::hairWidthNumerators(hp,.0001f);
    check(symmetric&&(*symmetric)[0]==hp[0]*.0001f&&(*symmetric)[1]==hp[5]*.0001f,"symmetric native hair width retained");
    for(float x:{-.14f,.14f})for(float y:{-.08f,.08f}){
        hp[2]=x;hp[6]=y;
        const auto corrected=circle::motor::hairWidthNumerators(hp,.0001f);
        check(corrected==symmetric,"both asymmetric eyes retain the same strand width, independent of optical centre");
        auto ndc=[&](unsigned row,double w){return ((hp[row*4]+hp[row*4+1])*w+hp[row*4+2]+hp[row*4+3])/hp[14];};
        if(corrected)for(unsigned row=0;row<2;++row)
            check(std::fabs((*corrected)[row]/hp[14]-(ndc(row,.0001)-ndc(row,0)))<1.e-10,"corrected width matches independently projected endpoint difference");
        check(std::fabs(ndc(0,.0001))>100*std::fabs((*symmetric)[0]),"native point projection reproduces massive off-axis width inflation");
    }
    hp[2]=hp[6]=0;hp[0]*=2;
    const auto wider=circle::motor::hairWidthNumerators(hp,.0002f);
    check(wider&&std::fabs((*wider)[0]-4*(*symmetric)[0])<1.e-9f,"real strand width and focal scale still affect extent");
    check(!circle::motor::hairWidthNumerators(hp,-1)&&!circle::motor::hairWidthNumerators(hp,std::numeric_limits<float>::infinity()),"invalid native widths leave native path intact");
    hp[12]=.1f;check(!circle::motor::hairWidthNumerators(hp,.01f),"oblique denominator not silently rewritten");
    hp[12]=0;hp[15]=1;check(!circle::motor::hairWidthNumerators(hp,.01f),"orthographic view rejected");
    hp[15]=0;hp[2]=std::numeric_limits<float>::quiet_NaN();
    check(!circle::motor::hairWidthNumerators(hp,.01f),"invalid projection rejected");
    for(const auto name:{"indiana_wear_head_beard.hairmodel","satipo_wear_beard.hairmodel"}){
        check(circle::motor::suppressFacialHair(true,false,true,name),"observed separate beard suppressed");
        check(!circle::motor::suppressFacialHair(false,false,true,name)&&!circle::motor::suppressFacialHair(true,true,true,name)&&!circle::motor::suppressFacialHair(true,false,false,name),"beard filter requires active adapter and explicit option");
    }
    for(const auto name:{"indiana_wear_head.hairmodel","satipo_wear_head.hairmodel","satipo_wear_eyebrows.hairmodel","satipo_wear_eyelashes.hairmodel","tarantula_hair.hairmodel","npc_beard.hairmodel_extra","bearded_head.hairmodel","npc.md6",""})
        check(!circle::motor::suppressFacialHair(true,false,true,name),"scalp eyebrows lashes animals unknown and lookalikes retained");
    check(circle::motor::isHandHair("indiana_hand_left.hairmodel")&&circle::motor::isHandHair("indiana_hand_right.hairmodel"),"both local hand hair assets selected");
    for(const auto name:{"indiana_wear_head.hairmodel","indiana_wear_head_beard.hairmodel","indiana_wear_eyelashes.hairmodel","npc_indiana_hand_left.hairmodel","indiana_hand_left.hairmodel_extra","md6def/characters/standard/indiana_jones/indiana_wear_body.md6",""})
        check(!circle::motor::isHandHair(name),"head hair, NPCs, body skin and unknown assets retained");
    circle::motor::ModelPairPlacement mp{100,200,300,400,9000001,5,80,{}};
    mp.placement[12]=mp.placement[13]=mp.placement[14]=1;
    check(mp.matches(100,200,300,400,5,9000001,81),"current player placement belongs to consecutive right render");
    check(!mp.matches(100,200,300,400,5,9000002,81)&&!mp.matches(100,200,300,400,5,9000001,82),"model pose cannot leak to another tick or later render");
    check(!mp.matches(100,201,300,400,5,9000001,81)&&!mp.matches(100,200,301,400,5,9000001,81)&&!mp.matches(100,200,300,401,5,9000001,81)&&!mp.matches(100,200,300,400,6,9000001,81),"recycled committed model manager owner and index are rejected");
    check(circle::motor::ModelPairPlacement::finite(mp.placement),"valid affine placement accepted");
    mp.placement[12]=0;
    check(!circle::motor::ModelPairPlacement::finite(mp.placement),"singular placement cannot enter pair cache");
    // World NPCs have no local-player owner: identity still includes manager,
    // committed model, index and exact consecutive pair, not just an asset name.
    circle::motor::ModelPairPlacement npc{700,800,900,0,12345,42,98,{}};
    check(npc.matches(700,800,900,0,42,12345,99),"NPC pair does not require player ownership");
    check(!npc.matches(701,800,900,0,42,12345,99)&&!npc.matches(700,801,900,0,42,12345,99)&&
          !npc.matches(700,800,901,0,42,12345,99)&&!npc.matches(700,800,900,0,43,12345,99),"NPC reuse rejects recycled model, manager and index");
    check(!npc.matches(700,800,900,0,42,12346,99)&&!npc.matches(700,800,900,0,42,12345,101),"NPC never freezes the next simulation pose");
    {
        using circle::motor::npcPlacementKind;
        using circle::motor::npcWearOwner;
        constexpr auto ginaWear="gp_ai_sanctuary_story_event_3_characters_gina_gina_companion_1:wear[UPPER_BODY]";
        constexpr auto ginaAsset="models/wear/gina/adventurer/gina_adventurer_upperbody_01.idasset";
        check(npcWearOwner(ginaWear)=="gp_ai_sanctuary_story_event_3_characters_gina_gina_companion_1",
              "wear ownership retains the complete native character name");
        check(npcPlacementKind(ginaAsset,ginaWear,false)==circle::motor::NpcPlacementKind::None&&
              npcPlacementKind(ginaAsset,ginaWear,false,true)==circle::motor::NpcPlacementKind::WearPart,
              "scripted Gina clothing requires an independently registered character owner");
        check(npcPlacementKind(ginaAsset,ginaWear,true,true)==circle::motor::NpcPlacementKind::None&&
              npcPlacementKind(ginaAsset,"actor_player_11:wear[UPPER_BODY]",false,true)==circle::motor::NpcPlacementKind::None,
              "registered wear never authorizes first person or player models");
        check(npcWearOwner("gina:wear[]").empty()&&npcWearOwner(":wear[UPPER_BODY]").empty()&&
              npcWearOwner("gina:wear[UPPER_BODY").empty()&&
              npcPlacementKind(ginaAsset,"unrelated_prop",false,true)==circle::motor::NpcPlacementKind::None,
              "malformed and unrelated instance names do not gain wear ownership");
        using Kind=circle::motor::NpcPlacementKind;
        check(npcPlacementKind("md6def/characters/standard/barranca/barranca_wear_base.md6","npc_barranca_spawn_1",false)==Kind::Character,
            "existing Barranca MD6 correction retained");
        for(const auto asset:{"models/wear/satipo/guide/satipo_wear_base.idasset",
            "models/wear/peruvian_porter/adventurer/peruvian_porter_wear_base.idasset",
            "models/wear/satipo/guide/satipo_guide_upperbody_01.idasset",
            "models/wear/satipo/guide/satipo_guide_footwear_01.idasset",
            "satipo_wear_head.hairmodel"}){
            check(npcPlacementKind(asset,"npc_satipo_spawn_1:wear[FOOTWEAR]",false)==Kind::WearPart,
                "native NPC instance admits observed wear bodies and attached parts");
            check(npcPlacementKind(asset,"actor_player_11",false)==Kind::None&&
                  npcPlacementKind(asset,"npc_satipo_spawn_1",true)==Kind::None,
                "local player identity and first-person flag independently exclude correction");
            check(npcPlacementKind(asset,"",false)==Kind::None&&
                  npcPlacementKind(asset,"prop_npc_satipo",false)==Kind::None,
                "wear without native NPC instance stays unchanged");
        }
        for(const auto asset:{"models/wear/indiana_jones/adventurer/indiana_footwear.idasset",
            "indiana_wear_head.hairmodel","models/props/bottle.idasset","models/wear/test.idasset_extra",""}){
            check(npcPlacementKind(asset,"npc_satipo_spawn_1",false)==Kind::None,
                "player assets props unknown paths and suffix lookalikes excluded");
        }
        // Each part keeps its OWN left placement, including authored offsets and
        // scale. Shared stereo timing does not collapse clothing onto body origin.
        std::array<float,15> body{1,2,3,1,0,0,0,1,0,0,0,1,1,1,1};
        auto shoe=body;shoe[2]=2.8f;shoe[12]=.9f;
        circle::motor::ModelPairPlacement bodyPair{1,2,3,0,99,4,100,body};
        circle::motor::ModelPairPlacement shoePair{5,6,3,0,99,7,100,shoe};
        body[0]=9;shoe[2]=8;
        check(bodyPair.matches(1,2,3,0,4,99,101)&&shoePair.matches(5,6,3,0,7,99,101)&&
              bodyPair.placement[0]==1&&shoePair.placement[2]==2.8f&&shoePair.placement[12]==.9f,
              "native writes after left capture do not alter paired body or shoe source");
        check(!shoePair.matches(1,2,3,0,4,99,101)&&!shoePair.matches(5,6,3,0,7,100,103),
              "shoe placement cannot cross model or simulation identity");
        shoePair={5,6,3,0,100,7,102,shoe};
        check(shoePair.matches(5,6,3,0,7,100,103)&&shoePair.placement[2]==8,
              "new left render advances NPC animation normally");
    }
    circle::motor::JointPairRecord jp{100,500,20,1,12,6,{}};
    for(auto call:circle::motor::jointCommitCallsites){
        check(circle::motor::jointCommitCaller(call+5),"all verified native joint consumers enter the pair bridge");
        check(!circle::motor::jointCommitCaller(call)&&!circle::motor::jointCommitCaller(call+6),"unknown return addresses retain native joint selection");
    }
    check(jp.matches(100,500,21,1,jp.descriptor),"same pair and unchanged joint source can reuse draw allocation");
    check(jp.sameRender(100,500,20,1,jp.descriptor),"multiple consumers in one render reuse current AND previous joints");
    check(!jp.sameRender(100,501,20,1,jp.descriptor)&&!jp.sameRender(100,500,21,1,jp.descriptor),"same-render cache rejects changed simulation and render");
    check(!jp.matches(100,501,21,1,jp.descriptor)&&!jp.matches(100,500,22,1,jp.descriptor),"joint reuse rejects different tick and nonconsecutive render");
    auto changed=jp.descriptor;changed[20]=1;
    check(!jp.matches(100,500,21,1,changed)&&!jp.matches(100,500,21,2,jp.descriptor)&&!jp.matches(101,500,21,1,jp.descriptor),"changed source descriptor slot or object forces native path");
    check(!jp.sameRender(100,500,20,1,changed)&&!jp.sameRender(100,500,20,2,jp.descriptor)&&!jp.sameRender(101,500,20,1,jp.descriptor),"same-render reuse rejects changed pose allocation and identity");
    EyeTagQueue tags;EyePairing pair;
    tags.rebase(100);
    RenderTag left;left.eye=Eye::Left;left.tick=42;left.viewApplied=true;left.renderFrame=81;
    RenderTag right=left;right.eye=Eye::Right;right.renderFrame=82;
    check(tags.push(left)&&tags.push(right),"two renderer passes can share one simulation token");
    check(pair.onPresent(tags.pop(101)).action==PairAction::StartPair,"left GPU image starts pending pair");
    check(pair.onPresent(tags.pop(102)).action==PairAction::CompletePair,"right with same token completes pair");
    tags.push(left);right.tick=43;tags.push(right);
    pair.onPresent(tags.pop(103));
    check(pair.onPresent(tags.pop(104)).action==PairAction::Drop&&!pair.pending(),"different simulation tokens never form a stereo pair");
    right.tick=42;right.viewApplied=false;tags.push(left);tags.push(right);
    pair.onPresent(tags.pop(105));
    check(pair.onPresent(tags.pop(106)).action==PairAction::Drop,"unverified right camera cannot publish");
    tags.push(left);tags.push(right);
    check(!tags.pop(108).tagged&&!tags.synced(),"missing backend present invalidates ownership");
    tags.rebase(200);right.viewApplied=true;tags.push(right);
    check(pair.onPresent(tags.pop(201)).action==PairAction::Drop,"right without retained left is dropped");
    FrameEndInput frame;frame.presents=true;frame.synced=true;frame.leftApplied=false;
    check(!planFrameEnd(frame).stereo,"ordinary cinema frame never starts native re-entry");
    frame.leftApplied=true;frame.presents=false;
    check(!planFrameEnd(frame).stereo,"skipped backend cannot start a pair");
    circle::motor::ObjectRing objects;
    {
        circle::motor::ObjectRing observed,plain;
        circle::motor::ObjectRing::Observation o;
        for(unsigned frame:{10u,11u,11u,14u,12u,15u}){
            const auto measured=observed.picks(Eye::Left,8000,frame,&o);
            const auto baseline=plain.picks(Eye::Left,8000,frame);
            check(measured.current==baseline.current&&measured.previous==baseline.previous,"observation never changes ring allocation");
            if(frame==14||frame==12||frame==15)check(o.first&&!o.inOrder,"out of order first requests detected");
        }
        observed.picks(Eye::Right,9000,15,&o);
        check(!o.first&&o.tagMismatch,"conflicting ownership on cached render is observable without reallocating");
        circle::motor::ObjectRing wrap;
        wrap.picks(Eye::Left,1,0xffffffffu,&o);
        wrap.picks(Eye::Right,1,0,&o);
        check(o.first&&o.inOrder,"native counter wrap remains ordered");
    }
    unsigned lastCurrent=99,lastPrevious=99,previousRight=99;
    for(unsigned tick=1;tick<100;++tick){
        const uint64_t timestamp=9000000+tick*27183;
        const auto l=objects.picks(Eye::Left,timestamp,tick*2);
        const auto r=objects.picks(Eye::Right,timestamp,tick*2+1);
        check(l.current%3!=lastCurrent&&l.current%3!=lastPrevious,"left never overwrites prior render read/write slots");
        check(r.current%3!=l.current%3&&r.current%3!=(l.previous+2)%3,"right never overwrites left read/write slots");
        if(tick>1)check((r.previous+2)%3==previousRight,"right history comes from preceding game update despite timestamp tokens");
        const auto repeat=objects.picks(Eye::Left,timestamp,tick*2);
        check(repeat.current==l.current&&repeat.previous==l.previous,"late repeated binding preserves original allocation");
        previousRight=r.current%3;lastCurrent=r.current%3;lastPrevious=(r.previous+2)%3;
    }
    return errors?1:0;
}
