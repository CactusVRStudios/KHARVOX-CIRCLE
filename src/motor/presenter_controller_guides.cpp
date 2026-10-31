#include "vkcore/presenter_impl.hpp"
#include "motor/controller_guides.hpp"
#include <wincodec.h>

namespace circlevr::vkcore {
namespace {
bool readGuide(unsigned resourceId, std::vector<std::uint8_t>& pixels,
               std::uint32_t& width, std::uint32_t& height) {
    HMODULE module{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&readGuide),&module))return false;
    const auto resource=FindResourceW(module,MAKEINTRESOURCEW(resourceId),MAKEINTRESOURCEW(10));
    const auto size=resource?SizeofResource(module,resource):0;
    const auto data=resource?LockResource(LoadResource(module,resource)):nullptr;
    if(!data||!size)return false;
    // The XR worker may already belong to a COM apartment. Only balance our own
    // successful initialization, and release WIC objects before uninitializing.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool ok = false;
    {
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        UINT w{}, h{};
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)),size)) &&
            SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
                WICDecodeMetadataCacheOnLoad, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame)) &&
            SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
            SUCCEEDED(converter->GetSize(&w, &h)) && w && h && w <= 4096 && h <= 4096) {
            pixels.resize(std::size_t(w) * h * 4);
            ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4,
                static_cast<UINT>(pixels.size()), pixels.data()));
            if (ok) { width = w; height = h; }
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return ok;
}
}

void XrPresenter::Impl::createControllerGuides() {
    const char* labels[] = {"controller interaction guide", "controller combat guide"};
    // Like ARGENT, upload static compositor images during session setup only.
    // Opening/closing Pause performs no disk access, decoding, or GPU upload.
    for (unsigned i = 0; i < controllerGuides.size(); ++i) {
        auto& guide = controllerGuides[i];
        std::vector<std::uint8_t> pixels;
        guide.ready = readGuide(301+i, pixels, guide.width, guide.height) &&
            guide.width <= maxSwapchainWidth && guide.height <= maxSwapchainHeight &&
            createStaticImage(guide.swapchain, pixels, guide.width, guide.height, labels[i]);
        CIRCLE_VR_LOG("menu-guides: %s %s (%ux%u)", labels[i],
            guide.ready ? "ready" : "unavailable", guide.width, guide.height);
    }
}

bool XrPresenter::Impl::fillControllerGuides(const XrCompositionLayerQuad& menu,
                                           std::array<XrCompositionLayerQuad, 2>& quads) {
    if (!circle::motor::controllerGuideContext(hybrid.state, shownHasView && shownView.inspection) ||
        !menu.space || !menu.subImage.swapchain || !(menu.size.width > 0) || !(menu.size.height > 0) ||
        !controllerGuides[0].ready || !controllerGuides[1].ready) return false;
    const Pose pose{{menu.pose.orientation.x, menu.pose.orientation.y, menu.pose.orientation.z, menu.pose.orientation.w},
                    {menu.pose.position.x, menu.pose.position.y, menu.pose.position.z}};
    for (unsigned i = 0; i < quads.size(); ++i) {
        const auto& guide = controllerGuides[i];
        auto& q = quads[i];
        q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        q.space = menu.space;
        q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        q.subImage.swapchain = guide.swapchain;
        q.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(guide.width), static_cast<int32_t>(guide.height)}};
        const float height = menu.size.height * .85f;
        q.size = {height * float(guide.width) / float(guide.height), height};
        const auto placed = circle::motor::controllerGuidePose(pose, menu.size.width, q.size.width, i == 0);
        q.pose = {{placed.orientation.x, placed.orientation.y, placed.orientation.z, placed.orientation.w},
                  {placed.position.x, placed.position.y, placed.position.z}};
    }
    return true;
}

void XrPresenter::Impl::destroyControllerGuides() {
    for (auto& guide : controllerGuides) {
        xrSwapchains.destroy(xr, guide.swapchain);
        guide = {};
    }
}
}
