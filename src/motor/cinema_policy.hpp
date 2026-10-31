#pragma once
namespace circle::motor {
// Observe the image actually being presented, not the legacy native-state
// classifier (which cannot identify the sequential renderer's camera owner).
struct CinemaEntry {
    bool previous{};
    bool update(bool cinema) {
        const bool entered=cinema&&!previous;
        previous=cinema;
        return entered;
    }
};
// Item inspection always presents one center-camera image to both eyes.
// Film stereo is opt-in; immersive gameplay never becomes a flat panel here.
constexpr bool monoCinemaView(bool cinema,bool inspection,bool stereoRequested){
    return cinema && (inspection || !stereoRequested);
}
// All loaded VR scenes share the full render surface, including flat films
// and inspections. Only projection and XR layer layout change. Switching the
// native active band can violate DLSS input bounds and permanently select TAA.
constexpr bool fixedCinemaSurface(bool loaded){return loaded;}
}
