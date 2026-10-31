#include "argent/CinewindowPosePolicy.h"
#include "cinema_policy.hpp"
#include <cstdio>
#include <limits>

int main(){
    unsigned errors{};
    const auto check=[&](bool ok,const char* why){if(!ok){++errors;std::fprintf(stderr,"FAIL: %s\n",why);}};
    const auto near=[](float a,float b){return std::fabs(a-b)<.0001f;};
    kharvox::CinewindowCaptureReadiness readiness;
    constexpr int64_t start=1000000000;
    check(!readiness.ready(start,true,false),"VALID without TRACKED cannot anchor startup floor pose");
    check(!readiness.ready(start,true,true),"first tracked frame waits");
    check(!readiness.ready(start+249000000,true,true),"settling window not complete");
    check(readiness.ready(start+250000000,true,true),"tracked 250 ms captures independent of refresh rate");
    check(!readiness.ready(start+260000000,false,true),"hidden session clears settling");
    check(!readiness.ready(start+300000000,true,true),"return from hidden begins new settling");
    check(readiness.ready(start+550000000,true,true),"visible session can capture without input focus");
    readiness.referenceChanged(start+2000000000);
    check(!readiness.ready(start+1000000000,true,true),"future LOCAL change blocks capture in old coordinates");
    check(!readiness.ready(start+2000000000,true,true),"effective LOCAL change restarts settling");
    check(readiness.ready(start+2250000000,true,true),"capture in new LOCAL coordinates");
    check(!readiness.ready(start+2300000000,true,false),"lost tracking restarts settling");

    circle::motor::CinemaEntry entry;
    kharvox::CinewindowAnchor anchor;
    // Startup -> gameplay -> turned/translated roomscale -> film, then hold.
    check(entry.update(true),"initial screen enters cinema");
    check(kharvox::captureCinewindowAnchor(anchor,0,0,0,1,0,.3f,0),"initial pose");
    auto first=kharvox::fixedCinewindowLayerPose(anchor,2.5f);
    check(near(first.positionY,.3f)&&near(first.positionZ,-2.5f),"LOCAL height is relative; no assumed floor height");
    check(!entry.update(false),"gameplay exit does not capture");
    check(entry.update(true),"actual presented cinematic after gameplay recaptures");
    // 180-degree yaw with 60-degree roll: screen remains upright, in front,
    // at the user's new position/height rather than at the startup anchor.
    check(kharvox::captureCinewindowAnchor(anchor,.5f,.8660254f,0,0,3,1.7f,4),"turned and rolled pose");
    const auto turned=kharvox::fixedCinewindowLayerPose(anchor,2.5f);
    check(near(turned.positionX,3)&&near(turned.positionY,1.7f)&&near(turned.positionZ,6.5f),"roomscale position plus forward distance at head height");
    check(turned.orientationX==0&&turned.orientationZ==0,"screen never rolls or pitches");
    for(int i=0;i<90;++i)check(!entry.update(true),"same film/menu remains fixed, no per-frame follow");
    check(!entry.update(false)&&entry.update(true),"second film/pause entry recenters again");
    check(kharvox::captureCinewindowAnchor(anchor,.5f,0,0,.8660254f,-1,-.2f,2),"60-degree head pitch");
    auto pitched=kharvox::fixedCinewindowLayerPose(anchor,2.5f);
    check(near(pitched.positionY,-.2f)&&near(pitched.positionZ,-.5f),"pitch does not move screen below/above eye line");
    check(!kharvox::captureCinewindowAnchor(anchor,.70710678f,0,0,.70710678f,0,0,0),"vertical gaze waits rather than using unrelated LOCAL forward");
    check(!kharvox::captureCinewindowAnchor(anchor,0,0,0,0,0,0,0),"zero quaternion rejected");
    check(!kharvox::captureCinewindowAnchor(anchor,0,0,0,1,0,std::numeric_limits<float>::quiet_NaN(),0),"nonfinite pose rejected");
    return errors?1:0;
}
