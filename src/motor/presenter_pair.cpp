#include "vkcore/presenter_impl.hpp"
#include "motor/experimental.hpp"
#include <cmath>

namespace circlevr::vkcore {
bool XrPresenter::Impl::fillNativePair(std::array<XrCompositionLayerProjectionView,2>& views) {
    if(!circle::motor::experimental::enabled())return false;
    // The static milestone must never trap the user on the saved pair. F6 and
    // set-experiment-presentation.ps1 can return to the existing live cinema path.
    if(GetFileAttributesW((logDirectory()+L"\\native-pair.hide").c_str())!=INVALID_FILE_ATTRIBUTES)return false;
    const auto now=GetTickCount64();
    if(!nativePairSwapchain && now-nativePairPoll>=1000) {
        nativePairPoll=now;
        const auto path=logDirectory()+L"\\native-pair.rgba";
        FILE* file=nullptr;
        if(!_wfopen_s(&file,path.c_str(),L"rb") && file) {
            circle::motor::experimental::NativePairHeader header{};
            bool valid=fread(&header,1,sizeof(header),file)==sizeof(header) &&
                !std::memcmp(header.magic,"CIRSTP01",8) && header.paused==1 &&
                header.width>0 && header.height>0 && header.width<=maxSwapchainWidth/2 &&
                header.height<=maxSwapchainHeight && header.fovX>10 && header.fovX<170 &&
                header.fovY>10 && header.fovY<170;
            for(unsigned eye=0;eye<2;++eye) {
                float norm=0;
                for(float q:header.orientations[eye]) {valid=valid && std::isfinite(q);norm+=q*q;}
                valid=valid && std::fabs(norm-1)<0.01f;
                for(float p:header.positions[eye])valid=valid && std::isfinite(p) && std::fabs(p)<1000;
            }
            const auto count=std::uint64_t(header.width)*header.height*8;
            valid=valid && count<=64*1024*1024;
            std::vector<std::uint8_t> pixels;
            if(valid) {
                pixels.resize(static_cast<std::size_t>(count));
                valid=fread(pixels.data(),1,pixels.size(),file)==pixels.size() && fgetc(file)==EOF;
            }
            fclose(file);
            if(valid && createStaticImage(nativePairSwapchain,pixels,header.width*2,header.height,"native frozen Motor L/R pair")) {
                nativePairHeader=header;
                CIRCLE_VR_LOG("experiment: two native eye images uploaded and D3D12 fence completed; %ux%u per eye; actual captured XR poses; static projection",header.width,header.height);
            } else {
                xrSwapchains.destroy(xr,nativePairSwapchain);
                CIRCLE_VR_LOG("experiment: native pair rejected or static upload failed; cinema continues");
            }
        }
    }
    if(!nativePairSwapchain)return false;
    constexpr float degreesToRadians=3.14159265358979323846f/180;
    for(unsigned eye=0;eye<2;++eye) {
        auto& view=views[eye];view={XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        const auto& p=nativePairHeader.positions[eye];const auto& q=nativePairHeader.orientations[eye];
        view.pose.position={p[0],p[1],p[2]};view.pose.orientation={q[0],q[1],q[2],q[3]};
        view.fov={-nativePairHeader.fovX*degreesToRadians/2,nativePairHeader.fovX*degreesToRadians/2,
                  nativePairHeader.fovY*degreesToRadians/2,-nativePairHeader.fovY*degreesToRadians/2};
        view.subImage.swapchain=nativePairSwapchain;
        view.subImage.imageRect={{static_cast<std::int32_t>(eye*nativePairHeader.width),0},
            {static_cast<std::int32_t>(nativePairHeader.width),static_cast<std::int32_t>(nativePairHeader.height)}};
    }
    return true;
}
}
