#pragma once
#include <string>
#include <array>
#include <cstdint>

namespace circle::motor::experimental {
bool enabled();
bool liveEnabled();
std::uint64_t selectedViewSequence();
void traceFrame(const char* stage, std::uint32_t image=0);
void stopLive();
void recenterLive();
// Runtime LOCAL-space recenter: wait for a tracked pose at/after changeTime.
void recenterAt(std::int64_t changeTime);
bool liveActive();
bool pauseMenuActive();
// Separate, code-fingerprinted observer. Does not open the donor engine-write gates.
bool installObservation(std::string& error);
void pollObservation();
struct EyeRig {
    // Exact xrLocateViews angles: left, right, up, down (radians).
    std::array<std::array<float,4>,2> fovs{};
    std::array<std::array<float,3>,2> positions{};
    std::array<std::array<float,4>,2> orientations{};
    std::int64_t displayTime=0;
    std::uint64_t ticks=0;
    std::int64_t locatedQpc=0;
    bool valid=false;
    bool tracked=false;
};
void publishRig(const EyeRig& rig);
struct CinemaPanel {
    std::array<float,3> position{};
    std::array<float,4> orientation{0,0,0,1};
    float width=0;
    bool valid=false;
};
void publishCinemaPanel(const CinemaPanel& panel);
struct LaserRay {std::array<float,3> from{},to{};uint64_t owner{},weapon{},life{},tick{};bool valid{},hit{};};
struct LiveEye {
    LaserRay laser{};
    std::array<float,16> projection{};
    bool immersive=false;
    bool cinema=false;
    bool monoCinema=false;
    bool inspection=false; // Journal/book/object quad, latched with this rendered frame.
    std::array<float,4> cinemaFov{};
    std::array<float,2> cinemaCrop{0,1};
    std::array<float,4> runtimeFov{};
    bool runtimeFovCrop=false;
    std::uint64_t sequence=0,ticks=0,epoch=0,frameToken=0;
    std::int64_t copyQpc=0;
    std::uintptr_t frame=0,destination=0;
    int eye=-1;
    EyeRig rig{};
    std::array<float,12> base{},camera{};
    float fovX=0,fovY=0;
    float photoZoomScale=1;
    bool valid=false;
    bool paused=false;
};
bool buildSequentialEyes(std::uintptr_t nativeView,std::uint64_t token,
    std::array<std::array<unsigned char,0x9a0>,2>& views,std::array<LiveEye,2>& eyes,bool cinema=false,bool monoCinema=false);
void acquireLive(std::uint64_t swapchain,std::uint32_t image);
LiveEye takeLive(std::uint64_t swapchain,std::uint32_t image);
}
