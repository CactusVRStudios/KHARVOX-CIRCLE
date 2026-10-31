#include "motor/hair_pair.hpp"
#include <cassert>
#include <limits>
using namespace circle::motor;
int main(){
    std::array<float,15> left{1,2,3,1,0,0,0,1,0,0,0,1,1,1,1},moving=left,out{};
    moving[0]+=0.04f;moving[4]=0.1f;
    HairPairPlacement p;
    assert(!p.select(true,1,2,3,4,5,6,11,moving,out));
    assert(p.select(false,1,2,3,4,5,6,10,left,out)&&out==left);
    assert(p.select(false,1,2,3,4,5,6,10,moving,out)&&out==moving);
    assert(p.select(true,1,2,3,4,5,6,11,left,out)&&out==moving);
    assert(moving[0]!=left[0]);
    assert(!p.select(true,1,2,3,4,5,7,11,moving,out));
    assert(!p.select(true,1,2,3,4,5,6,12,moving,out));
    assert(!p.select(true,1,2,3,9,5,6,11,moving,out));
    assert(!p.select(true,1,9,3,4,5,6,11,moving,out));
    assert(!p.select(true,1,2,9,4,5,6,11,moving,out));
    assert(!p.select(true,1,2,3,4,9,6,11,moving,out));
    auto invalid=moving;invalid[0]=std::numeric_limits<float>::quiet_NaN();
    assert(!p.select(true,1,2,3,4,5,6,11,invalid,out));
    assert(p.select(false,1,2,3,4,5,7,12,moving,out)&&out==moving);
    assert(p.select(true,1,2,3,4,5,7,13,moving,out)&&out==moving);
    assert(!p.select(false,1,2,3,4,5,7,12,invalid,out));
    assert(!p.select(true,1,2,3,4,5,7,13,moving,out));
    assert(pairableNpcHair("gina_hair.hairmodel","gp_ai_gina:hair_0",false));
    assert(pairableNpcHair("other.hairmodel","npc_guard",false));
    assert(!pairableNpcHair("indiana_hand.hairmodel","npc_guard",false));
    assert(!pairableNpcHair("gina_hair.hairmodel","actor_player:hair_0",false));
    assert(!pairableNpcHair("gina_hair.hairmodel","gp_ai_gina:hair_0",true));
    assert(!pairableNpcHair("world.hairmodel","unknown",false));
}
