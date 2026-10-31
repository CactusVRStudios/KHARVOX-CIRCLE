#include "particle_pair.hpp"
#include <cstdlib>
#include <iostream>

static void require(bool v) { if (!v) { std::cerr << "Particle pair invariant failed\n"; std::exit(1); } }
int main() {
    using namespace circle::motor;
    ParticlePair p{10,20,30,1000,54,70,2,true,true};
    require(p.rightMatches(10,20,30,1000,55,70,2));
    require(!p.rightMatches(10,20,30,1000,57,70,2)); // repeated token, another pair
    require(!p.rightMatches(10,20,30,1000,54,70,2)); // duplicate left
    require(!p.rightMatches(10,21,30,1000,55,70,2)); // ring replaced on load
    require(!p.rightMatches(11,20,30,1000,55,70,2)); // another render world
    require(!p.rightMatches(10,20,31,1000,55,70,2)); // light pool replaced
    require(!p.rightMatches(10,20,30,1001,55,70,2));
    require(!p.rightMatches(10,20,30,1000,55,71,2)); // ring advanced unexpectedly
    require(!p.rightMatches(10,20,30,1000,55,70,0));
    require(p.generatedHere(70));
    require(!p.generatedHere(69)); // not seen by left: native bind/generation required
    p.enabled = false; require(!p.rightMatches(10,20,30,1000,55,70,2));
    require(!p.generatedHere(70));
    p.enabled = true; p.left = false; require(!p.rightMatches(10,20,30,1000,55,70,2));
    p.left = true; p.render = UINT32_MAX;
    require(p.rightMatches(10,20,30,1000,0,70,2));
    // L draws previous slot; L and optional GPU-staged R generation append to
    // current. Neither may overwrite the slot consumed by either eye.
    for (int slot=0; slot<3; ++slot) {
        require(previousParticleSlot(slot) != slot);
        require(previousParticleSlot((slot+1)%3) == slot);
    }
    require(previousParticleSlot(-1) == -1 && previousParticleSlot(3) == -1);
    require(particleGeneration(true,true,false)==ParticleGeneration::ReuseCpu);
    require(particleGeneration(true,true,true)==ParticleGeneration::BindGpuAndGenerate);
    require(particleGeneration(true,false,false)==ParticleGeneration::Native);
    require(particleGeneration(true,false,true)==ParticleGeneration::Native);
    require(particleGeneration(false,true,false)==ParticleGeneration::Native);
    require(particleGeneration(false,true,true)==ParticleGeneration::Native);
    std::cout << "Particle pair identity, native fallback and ring lifetime policy passed\n";
}
