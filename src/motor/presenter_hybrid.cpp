// Small Motor controller integration; the donor transport and XR lifetime remain unchanged.
#include "vkcore/presenter_impl.hpp"
#include "vkcore/keep_active.hpp"
#include "motor/experimental.hpp"
#include <cstdlib>

namespace circlevr::vkcore {
void XrPresenter::Impl::updateHybrid() {
    using namespace circle::motor;
    const auto observation = motorState.sample();
    if (!hybridLogged) {
        hybridLogged=true;
        wchar_t mask[16]{};
        if (GetEnvironmentVariableW(L"KHARVOX_CIRCLE_HOTKEY_MASK",mask,16))
            hotkeyMask=static_cast<unsigned>(std::wcstoul(mask,nullptr,10)) & 15U;
        CIRCLE_VR_LOG("hybrid: %s; hotkey mask %u (F6/F7/F8/F9); cinema 3.2 m wide at 2.5 m; native stereo remains gated",
                motorState.validation(),hotkeyMask);
    }
    const bool changed=hybrid.update(observation,motorStereo);
    if (changed || observation.nativeState!=loggedNativeState) {
        CIRCLE_VR_LOG("hybrid: native state %d valid %s -> %s / %s; camera ownership %s",
                observation.nativeState, observation.nativeStateValid ? "yes" : "no",
                name(hybrid.state),name(hybrid.mode),observation.cameraValidated ? "validated" : "unresolved");
        loggedNativeState=observation.nativeState;
    }
    DWORD frontPid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&frontPid);
    const bool foreground=gameWindow()!=nullptr && frontPid==GetCurrentProcessId();
    for (unsigned i=0;i<4;++i) {
        const bool down=(GetAsyncKeyState(VK_F6+static_cast<int>(i)) & 0x8000)!=0;
        const bool pressed=down && !hybridKeyDown[i] && foreground && (hotkeyMask & (1U<<i));
        hybridKeyDown[i]=down;
        if (!pressed) continue;
        switch(i) {
        case 0:
            hybrid.forceCinema();
            experimental::stopLive();
            if(experimental::enabled()) {
                FILE* marker=nullptr;
                if(!circlevr::vkcore::openLogFile(&marker,(logDirectory()+L"\\native-pair.hide").c_str(),L"wb") && marker) {
                    fclose(marker);CIRCLE_VR_LOG("experiment: static pair hidden; existing live cinema resumes");
                }
            }
            CIRCLE_VR_LOG("hybrid: F6 cinema requested");
            break;
        case 1:
            hybrid.requestStereo();
            CIRCLE_VR_LOG("hybrid: F7 stereo request %s: %s",hybrid.stereoBlocker(motorStereo) ? "denied" : "ready",
                    hybrid.stereoBlocker(motorStereo) ? hybrid.stereoBlocker(motorStereo) : "all Motor capabilities validated");
            break;
        case 2: replaceScreen.store(true,std::memory_order_release); experimental::recenterLive(); CIRCLE_VR_LOG("hybrid: F8 recenter requested; next tracked headset pose recenters live camera and screen"); break;
        case 3: hybrid.toggleAutomatic(); CIRCLE_VR_LOG("hybrid: F9 automatic detection %s",hybrid.automatic ? "enabled" : "disabled"); break;
        }
    }
    // Manual requests can select only a mode allowed by the same engine/capability gates.
    if (hybrid.update(observation,motorStereo))
        CIRCLE_VR_LOG("hybrid: manual selection -> %s / %s",name(hybrid.state),name(hybrid.mode));
}

bool XrPresenter::Impl::createLoadingImage() {
    constexpr unsigned width=512,height=256;
    // A quiet stable loading card, used only when the native loading state has no fresh game image.
    const char* letters[7][7]={
        {"10000","10000","10000","10000","10000","10000","11111"},
        {"01110","10001","10001","10001","10001","10001","01110"},
        {"01110","10001","10001","11111","10001","10001","10001"},
        {"11110","10001","10001","10001","10001","10001","11110"},
        {"11111","00100","00100","00100","00100","00100","11111"},
        {"10001","11001","11001","10101","10011","10011","10001"},
        {"01111","10000","10000","10111","10001","10001","01110"}};
    std::vector<std::uint8_t> pixels(width*height*4);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        bool text=false;
        constexpr unsigned scale=6,startX=133,startY=106;
        if(x>=startX && x<startX+7*6*scale && y>=startY && y<startY+7*scale) {
            const unsigned gx=(x-startX)/scale,gy=(y-startY)/scale;
            text=gx%6<5 && letters[gx/6][gy][gx%6]=='1';
        }
        const unsigned offset=(y*width+x)*4;
        pixels[offset]=static_cast<std::uint8_t>(text ? 210 : 14);
        pixels[offset+1]=static_cast<std::uint8_t>(text ? 215 : 18);
        pixels[offset+2]=static_cast<std::uint8_t>(text ? 225 : 24);
        pixels[offset+3]=255;
    }
    loadingImageReady=createStaticImage(loadingSwapchain,pixels,width,height,"Motor stable loading card");
    if (!loadingImageReady) CIRCLE_VR_LOG("hybrid: loading card unavailable; stale loading images are omitted while XR synchronization continues");
    return loadingImageReady;
}
}
