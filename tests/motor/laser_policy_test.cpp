#include "laser_policy.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace circle::motor;
void check(bool b,const char* msg){if(!b){std::fprintf(stderr,"%s\n",msg);std::exit(1);}}
int main(){
 std::array<unsigned char,0x368> a{};std::array<unsigned char,0x30> s{};
 auto put=[&]<class T>(size_t offset,T value){std::memcpy(a.data()+offset,&value,sizeof(value));};
 a[0x366]=1;put(0x188,3);put(0x18c,3);
 for(int i=0;i<3;++i){put(0x190+i*0x30,uint64_t(90+i));put(0x190+i*0x30+0x14,i);put(0x220+i*0x68+0x28,uint64_t(90+i));put(0x220+i*0x68+0x1c,1);}
 check(!laser::nativeReady(a,s,100),"empty active ammo must not borrow another mode's rounds");
 put(0x184,1);check(laser::nativeReady(a,s,100),"one round in selected mode is ready before first shot");
 put(0x220+0x68+0x1c,2);check(!laser::nativeReady(a,s,100),"insufficient ammo for mode cost");put(0x220+0x68+0x1c,1);
 for(unsigned state:{2,3,4,255}){s[0x29]=state;check(!laser::nativeReady(a,s,100),"reload/external/unknown states off");}
 s[0x29]=1;check(laser::nativeReady(a,s,100),"fired state ready after cooldown");
 uint64_t next=101;std::memcpy(s.data(),&next,8);check(!laser::nativeReady(a,s,100),"cooldown off");check(laser::nativeReady(a,s,101),"cooldown finished");
 int64_t unset=-1;std::memcpy(s.data(),&unset,8);check(laser::nativeReady(a,s,100),"unset signed next-fire time permits first shot");
 a[0x362]=1;check(!laser::nativeReady(a,s,200),"manual action off");a[0x362]=0;
 a[0x366]=0;check(!laser::nativeReady(a,s,200),"uninitialized off");a[0x366]=1;
 for(int mode:{-1,3,200}){put(0x184,mode);check(!laser::nativeReady(a,s,200),"invalid mode off");}
 put(0x184,1);put(0x18c,4);check(!laser::nativeReady(a,s,200),"bad ammo count off");put(0x18c,3);
 check(!laser::nativeReady(std::span(a).first(100),s,200),"truncated read off");
 using namespace hands;
 const Pose camera{{12,-4,2},{}},head{{.2f,1.7f,.4f},{}};
 const Vec world{15,-4.3f,2.1f};const Vec expected{.5f,1.8f,-2.6f};
 check(length(laser::localPoint(world,camera,head)-expected)<.0001f,"native forward/left/up to XR right/up/back");
 const Quat turn{0,0,std::sin(.6f),std::cos(.6f)};const Pose moved{{9,18,4},turn};
 const auto transformed=moved.position+rotate(turn,world-camera.position);
 check(length(laser::localPoint(transformed,moved,head)-expected)<.0001f,"stick yaw and world translation do not displace laser in hand");
 Pose looked=head;looked.orientation={0,std::sin(.4f),0,std::cos(.4f)};
 check(length(laser::localPoint(world,camera,looked)-(head.position+rotate(looked.orientation,expected-head.position)))<.0001f,"head rotation uses shown pose without extra yaw");
 check(laser::worldMuzzleOffset==256,"use reflected world muzzle rather than physics query or local muzzle");
 for(float angle:{0.f,.4f,-1.f}){
  Pose source{{12,-4,2},{0,0,std::sin(angle/2),std::cos(angle/2)}};
  Pose target{{12.1f,-3.9f,2.05f},{0,0,std::sin((angle+.1f)/2),std::cos((angle+.1f)/2)}};
  Pose weapon{{12.3f,-4,2.2f},source.orientation},local{{.2f,0,.09f},{}};
  auto native=compose(weapon,local);
  auto direct=renderHandAttachment(source,target,native);
  auto viaModel=compose(renderHandAttachment(source,target,weapon),local);
  check(length(direct.position-viaModel.position)<.0001f,"world muzzle receives identical render correction to visible model plus local muzzle");
  auto placement=laser::muzzlePlacement(native);
  auto q=rotationBasis({placement[3],placement[4],placement[5]},{placement[6],placement[7],placement[8]},{placement[9],placement[10],placement[11]});
  check(q&&length(rotate(*q,{1,0,0})-rotate(native.orientation,{1,0,0}))<.0001f,"world muzzle placement retains barrel direction");
 }
 std::puts("Laser readiness and coordinate regression checks passed.");
}
