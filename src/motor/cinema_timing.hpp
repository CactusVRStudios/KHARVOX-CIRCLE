#pragma once
#include <cstdint>
namespace circle::motor::cinema_timing {
void install();
void scene(bool eligible);
void displayPeriod(std::int64_t ns,double reportedHz=0);
constexpr bool adaptiveCinema(bool fresh,bool paused,int mode,float speed){
    return fresh&&!paused&&mode>=0&&mode<=2&&mode!=2&&speed==1.0f;
}
constexpr unsigned targetRate(unsigned native,unsigned refresh){
    return refresh>=60&&refresh<=240&&(native>=60)?refresh:native;
}
}
