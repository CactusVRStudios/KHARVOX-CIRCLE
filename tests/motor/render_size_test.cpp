#include "features/render_size/render_size.hpp"
#include <cstdio>
#include <cmath>
int main(){
    using namespace circlevr::render_size;
    int failed=0;auto check=[&](bool ok,const char* name){if(!ok){++failed;std::fprintf(stderr,"FAIL %s\n",name);}};
    ViewLimits limits{{2496,2688},{16384,16384},{16384,16384},2};
    Request request;request.mode=Mode::Auto;request.uncappedRecommendation=true;
    check(selectSize(request,limits)==Extent{2496,2688},"100 percent is the full recommendation, no donor budget");
    request.scale=1.5f;check(selectSize(request,limits)==Extent{3744,4032},"150 percent scales both dimensions");
    request.scale=0.5f;check(selectSize(request,limits)==Extent{1248,1344},"50 percent scales both dimensions");
    request.scale=2;limits.maxSwapchain={4096,4096};auto capped=selectSize(request,limits);
    check(capped && capped->width<=2048 && capped->height<=4096,"packed two-eye hardware width limit");
    check(capped && capped->width%8==0 && capped->height%8==0,"native dimension alignment");
    check(capped && std::fabs(float(capped->width)/capped->height-2496.f/2688)<0.01f,"uniform limit fitting");
    check(!parseRenderScale(L"nan") && !parseRenderScale(L"2.01") && !parseRenderScale(L"0.49"),"invalid scales rejected");
    check(!selectSize(request,std::nullopt),"auto waits for runtime dimensions");
    return failed?1:0;
}
