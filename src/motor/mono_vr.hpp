#pragma once
#include "experimental.hpp"
#include "stereo_seq/eye_pairing.hpp"
#include <algorithm>
#include <cmath>

namespace circle::motor {
// Additional presents (e.g. native FG) are not native camera records. Never
// reinterpret them as cinema or borrow a previous frame's pose. Explicit
// native Mono tags still carry menu/loading transitions; owned Left tags carry
// both immersive Mono VR and the loaded-scene cinema camera.
inline circlevr::stereo_seq::PairAction monoPresentAction(const circlevr::stereo_seq::PresentMatch& match,bool nativeMainMenu=false) {
    using namespace circlevr::stereo_seq;
    // Before first gameplay the queue has no base. Only a confirmed native
    // main-menu object may use the flat fallback, never a stale FPS sample or
    // the in-level pause menu (which has its own rendered cinema view).
    if(!match.tagged)return nativeMainMenu?PairAction::ShowMono:PairAction::Drop;
    if(match.tag.eye==Eye::Mono)return PairAction::ShowMono;
    if(match.tag.eye==Eye::Left&&match.tag.viewApplied&&match.tag.tick)return PairAction::CompletePair;
    return PairAction::Drop;
}

// One camera, one frustum, one temporal history. Both XR views describe that
// same rendered camera; using the original asymmetric eye FOVs would stretch
// identical pixels differently. Keep the left camera rotation (also works for
// canted eye poses), move its origin to the midpoint, and use it for both eyes.
inline bool centerMonoViews(std::array<experimental::LiveEye,2>& eyes) {
    if(!eyes[0].valid || !eyes[1].valid)return false;
    auto rig=eyes[0].rig;
    auto camera=eyes[0].camera;
    for(unsigned k=0;k<3;++k){
        camera[k]=(eyes[0].camera[k]+eyes[1].camera[k])*.5f;
        rig.positions[0][k]=(rig.positions[0][k]+rig.positions[1][k])*.5f;
    }
    rig.positions[1]=rig.positions[0];
    rig.orientations[1]=rig.orientations[0];
    for(unsigned k=0;k<4;++k){
        const float l=rig.fovs[0][k],r=rig.fovs[1][k];
        if(!std::isfinite(l)||!std::isfinite(r)||std::abs(l)>=1.57f||std::abs(r)>=1.57f)return false;
        rig.fovs[0][k]=(k==0||k==3)?std::min(l,r):std::max(l,r);
    }
    if(rig.fovs[0][0]>=rig.fovs[0][1]||rig.fovs[0][3]>=rig.fovs[0][2])return false;
    rig.fovs[1]=rig.fovs[0];
    for(auto& eye:eyes){eye.camera=camera;eye.rig=rig;}
    return true;
}
}
