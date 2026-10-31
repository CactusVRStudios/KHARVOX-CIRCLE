#include "vkcore/presenter_impl.hpp"
#include "vkcore/fence_wait.hpp"
#include "motor/temporal.hpp"
#include "motor/sequential.hpp"

#include <algorithm>
#include <string>

namespace circlevr::vkcore {
namespace {
constexpr UINT width=1024,height=416,pitch=width*4;
bool enabled(){static const bool on=[] {char value[8]{};return GetEnvironmentVariableA("CIRCLE_PERFORMANCE_OVERLAY",value,8)==1&&value[0]=='1';}();return on;}

// A single opaque texture keeps text identical in both eyes and consumes one
// composition layer. GDI is used only every 500 ms, on the XR worker.
std::vector<std::uint8_t> panelPixels(const circle::motor::PerformanceStats& stats,
                                     double now,double poseAge,bool gameImage,bool cinema,double refreshHz,bool steamvr,
                                     UINT& contentWidth,UINT& contentHeight){
    std::vector<std::uint8_t> pixels;
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)return pixels;
    BITMAPINFO bmi{};bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=width;bmi.bmiHeader.biHeight=-static_cast<LONG>(height);
    bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap){DeleteDC(dc);return pixels;}
    auto oldBitmap=SelectObject(dc,bitmap);
    HFONT font=CreateFontW(-25,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,ANSI_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");
    auto oldFont=font?SelectObject(dc,font):nullptr;
    RECT area{0,0,width,height};HBRUSH background=CreateSolidBrush(RGB(12,19,25));
    FillRect(dc,&area,background);DeleteObject(background);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(222,235,238));
    char lines[16][128]{};
    const bool mono=circle::motor::sequential::monoVrEnabled();
    const bool menus=circle::motor::sequential::menuActive();
    sprintf_s(lines[0],"KHARVOX PERFORMANCE | %s | %s",menus?"MONO MENU":mono?"MONO VR":"STEREO",!gameImage?"WAITING":menus?"MENU":cinema?"CINEMA":"GAMEPLAY");
    char hz[24]{};
    if(refreshHz>0)sprintf_s(hz,"%.0f",refreshHz);else sprintf_s(hz,"--");
    if(stats.rates.valid){
        sprintf_s(lines[1],"%s    %6.1f / %s",steamvr?"Submitted FPS / Target Hz":"Game FPS / Requested Hz",stats.rates.output,hz);
        sprintf_s(lines[2],"Fresh game images   %6.1f FPS",stats.rates.fresh);
    }else sprintf_s(lines[1],"Measuring (1 second window)...");
    if(stats.rates.valid&&stats.rates.produced>0)
        sprintf_s(lines[3],"Game pair interval ~%6.1f ms",1000.0/stats.rates.produced);
    else sprintf_s(lines[3],"Game pair interval       --");
    if(gameImage&&poseAge>=0)sprintf_s(lines[4],"Pose sample age     %6.1f ms",poseAge);
    else sprintf_s(lines[4],"Pose sample age          --");
    if(gameImage&&stats.imageAgeMs(now)>=0)sprintf_s(lines[5],"Last new image age  %6.1f ms",stats.imageAgeMs(now));
    else sprintf_s(lines[5],"Last new image age       --");
    const auto aa=circle::motor::temporal::upscalingStatus();
    const char* actual=aa.actual==0?"Off":aa.actual==1?"Native TAA":aa.actual==2?(aa.stereoVerified?(mono?"DLSS SR (mono verified)":"DLSS SR (L/R verified)"):(mono?"DLSS (mono pending)":"DLSS (L/R pending)")):aa.actual==3?(aa.stereoVerified?"AMD FSR (eyes verified)":"AMD FSR (eyes pending)"):"Unknown";
    sprintf_s(lines[6],"Upscaling active:    %s",actual);
    if(!steamvr)sprintf_s(lines[7],"Game FPS includes FG; excludes runtime reprojection");
    if(circle::fg::requested()){
        auto fg=circle::fg::status();
        sprintf_s(lines[8],"VR FG %.1fx %s: %s | available: %s",fg.multiplier,fg.backend,fg.state,fg.available?"yes":"no");
        sprintf_s(lines[9],"Real pairs %.1f/s | Generated XR pairs %.1f/s",fg.realRate,fg.generatedRate);
        sprintf_s(lines[10],"FG GPU %.2f ms | Output pose age %.1f ms",fg.gpuMs,fg.imageAgeMs);
        sprintf_s(lines[11],"%.110s",fg.reason.c_str());
    }
    contentWidth=36;int row=0;
    for(int i=0;i<12;++i){
        const int count=static_cast<int>(strlen(lines[i]));if(!count)continue;
        SIZE extent{};
        if(GetTextExtentPoint32A(dc,lines[i],count,&extent))
            contentWidth=std::max(contentWidth,std::min(width,UINT(extent.cx+36)));
        SetTextColor(dc,i==0?RGB(105,220,194):i==11?RGB(150,169,180):RGB(222,235,238));
        RECT line{18,12+row*33,LONG(width)-18,12+(row+1)*33};
        DrawTextA(dc,lines[i],count,&line,DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
        ++row;
    }
    contentHeight=std::min(height,UINT(24+row*33));
    GdiFlush();pixels.resize(width*height*4);
    const auto* bgra=static_cast<const std::uint8_t*>(bits);
    for(size_t i=0;i<pixels.size();i+=4){pixels[i]=bgra[i+2];pixels[i+1]=bgra[i+1];pixels[i+2]=bgra[i];pixels[i+3]=255;}
    if(font){SelectObject(dc,oldFont);DeleteObject(font);}SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);
    return pixels;
}
}

bool XrPresenter::Impl::fillPerformanceQuad(XrCompositionLayerQuad& quad,double now,bool gameImage){
    if(!enabled()||performanceFailed||!viewSpace)return false;
    const auto fail=[&](const char* reason){performanceFailed=true;CIRCLE_VR_LOG("performance-overlay: disabled: %s",reason);return false;};
    if(!performanceSwapchain){
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        info.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;info.sampleCount=1;info.width=width;info.height=height;
        info.faceCount=info.arraySize=info.mipCount=1;
        XrSwapchain made=XR_NULL_HANDLE;
        if(XR_FAILED(xr.xrCreateSwapchain(session,&info,&made)))return fail("swapchain creation");
        if(!xrSwapchains.admit(xr,made,width,height,"performance panel",performanceImages,swapchain_book::Fit::AtLeast))return fail("swapchain images");
        performanceSwapchain=made;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=pitch*height;
        desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(d3dDevice->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&performanceUpload)))||
           FAILED(d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&performanceAllocator)))||
           FAILED(d3dDevice->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,performanceAllocator.Get(),nullptr,IID_PPV_ARGS(&performanceList)))||
           FAILED(d3dDevice->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&performanceFence))))return fail("upload resources");
        if(FAILED(performanceList->Close()))return fail("initial command list");
        CIRCLE_VR_LOG("performance-overlay: enabled, persistent %ux%u panel, 1s rates, 500ms refresh",width,height);
    }
    if(performancePending){
        const auto completed=performanceFence->GetCompletedValue();
        if(completed==UINT64_MAX)return fail("device removed");
        if(completed>=performanceFenceValue){
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            if(!runtimeReleaseJoin())return fail("runtime release join");
            if(XR_FAILED(xr.xrReleaseSwapchainImage(performanceSwapchain,&release)))return fail("release");
            performanceAcquired=performancePending=false;performanceReady=true;
            performanceContentWidth=performancePendingWidth;performanceContentHeight=performancePendingHeight;
        }
    }
    if(!performancePending&&now>=performanceNextUpdate){
        if(!performanceAcquired){
            if(XR_FAILED(xr.xrAcquireSwapchainImage(performanceSwapchain,nullptr,&performanceIndex)))return fail("acquire");
            performanceAcquired=true;
        }
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=0;
        const auto result=xr.xrWaitSwapchainImage(performanceSwapchain,&wait);
        if(XR_FAILED(result))return fail("wait");
        if(result==XR_SUCCESS){
            if(!runtimeAcquireJoin())return fail("runtime acquire join");
            if(performanceIndex>=performanceImages.size())return fail("image index");
            const double poseAge=shownHasView&&shownView.locatedQpc?std::max(0.0,(now-qpcSeconds(shownView.locatedQpc))*1000.0):-1;
            const auto pixels=panelPixels(performanceStats,now,poseAge,gameImage,shownHasView&&shownView.cutscene,refresh.reportedHz(),steamVrRuntime,performancePendingWidth,performancePendingHeight);
            if(pixels.empty())return fail("text rasterization");
            void* mapped=nullptr;if(FAILED(performanceUpload->Map(0,nullptr,&mapped)))return fail("map");
            memcpy(mapped,pixels.data(),pixels.size());performanceUpload->Unmap(0,nullptr);
            if(FAILED(performanceAllocator->Reset())||FAILED(performanceList->Reset(performanceAllocator.Get(),nullptr)))return fail("reset");
            auto* target=performanceImages[performanceIndex].Get();
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition={target,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST};
            performanceList->ResourceBarrier(1,&barrier);
            D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=target;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=performanceUpload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,width,height,1,pitch};
            performanceList->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);performanceList->ResourceBarrier(1,&barrier);
            if(FAILED(performanceList->Close()))return fail("close");
            ID3D12CommandList* lists[]={performanceList.Get()};d3dQueue->ExecuteCommandLists(1,lists);
            // Set pending before Signal: a failed signal must still retain resources.
            performancePending=true;
            if(FAILED(d3dQueue->Signal(performanceFence.Get(),++performanceFenceValue)))return fail("signal");
            performanceNextUpdate=now+0.5;
            if(performanceStats.rates.valid)CIRCLE_VR_LOG("performance-overlay: output %.1f produced %.1f fresh %.1f submissions %.1f repeats %.1f%% pose_age %.1fms image_age %.1fms",performanceStats.rates.output,performanceStats.rates.produced,performanceStats.rates.fresh,performanceStats.rates.submissions,performanceStats.rates.repeats,poseAge,performanceStats.imageAgeMs(now));
        }
    }
    if(!performanceReady)return false;
    quad.type=XR_TYPE_COMPOSITION_LAYER_QUAD;quad.space=viewSpace;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain=performanceSwapchain;
    quad.subImage.imageRect={{0,0},{int32_t(performanceContentWidth),int32_t(performanceContentHeight)}};
    constexpr float metresPerPixel=.62f/width;
    quad.size={metresPerPixel*performanceContentWidth,metresPerPixel*performanceContentHeight};
    // Keep the top/left edge and text scale fixed as unused space is cropped.
    quad.pose={{0,0,0,1},{-.67f+quad.size.width*.5f,-.30f+metresPerPixel*height*.5f-quad.size.height*.5f,-1.2f}};
    return true;
}

void XrPresenter::Impl::destroyPerformanceOverlay(){
    // Bounded teardown only. A failed GPU must not leave resources freed while
    // queued commands can still access them; keep those objects alive instead.
    if(performancePending&&performanceFence&& !waitFence(performanceFence.Get(),performanceFenceValue,copyEvent,2000)){
        if(performanceUpload)performanceUpload.Detach();
        if(performanceAllocator)performanceAllocator.Detach();
        if(performanceList)performanceList.Detach();
        performanceFence.Detach();
        for(auto& image:performanceImages)image.Detach();
        CIRCLE_VR_LOG("performance-overlay: GPU timeout on shutdown; retaining in-flight resources");
    }
    xrSwapchains.destroy(xr,performanceSwapchain);performanceImages.clear();
    performanceUpload.Reset();performanceAllocator.Reset();performanceList.Reset();performanceFence.Reset();
    performanceFenceValue=0;performanceReady=performanceAcquired=performancePending=performanceFailed=false;
    performanceNextUpdate=0;performanceStats={};
    performanceContentWidth=performancePendingWidth=width;performanceContentHeight=performancePendingHeight=height;
}
} // namespace circlevr::vkcore
