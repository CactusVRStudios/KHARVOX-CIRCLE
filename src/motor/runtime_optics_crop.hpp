#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
namespace circle::motor {
inline bool nativeRuntimeOptics(std::string_view name){return name=="Oculus"||name=="Meta";}
struct OpticsCrop {int x{},y{},width{},height{};std::array<float,4> fov{};};
// Crop in tangent/ray space, never linearly in angles. Return the exact FOV
// of the rounded pixel rectangle so runtime timewarp matches rendered rays.
inline std::optional<OpticsCrop> runtimeOpticsCrop(const std::array<float,4>& render,const std::array<float,4>& wanted,int width,int height){
 if(width<2||height<2)return {};
 std::array<float,4> a{},b{};
 for(unsigned i=0;i<4;++i){
  if(!std::isfinite(render[i])||!std::isfinite(wanted[i])||std::abs(render[i])>=1.57f||std::abs(wanted[i])>=1.57f)return {};
  a[i]=std::tan(render[i]);b[i]=std::tan(wanted[i]);
 }
 if(a[1]<=a[0]||a[2]<=a[3]||b[1]<=b[0]||b[2]<=b[3]||b[0]<a[0]-.00001f||b[1]>a[1]+.00001f||b[2]>a[2]+.00001f||b[3]<a[3]-.00001f)return {};
 auto pixel=[](float x,int limit){return std::clamp(int(std::lround(x)),0,limit);};
 const float dx=a[1]-a[0],dy=a[2]-a[3];
 int x=pixel((b[0]-a[0])/dx*width,width),r=pixel((b[1]-a[0])/dx*width,width);
 int y=pixel((a[2]-b[2])/dy*height,height),d=pixel((a[2]-b[3])/dy*height,height);
 if(r<=x||d<=y)return {};
 return OpticsCrop{x,y,r-x,d-y,{std::atan(a[0]+dx*x/width),std::atan(a[0]+dx*r/width),std::atan(a[2]-dy*y/height),std::atan(a[2]-dy*d/height)}};
}
}
