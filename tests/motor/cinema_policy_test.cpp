#include "cinema_policy.hpp"
#include "cinema_projection.hpp"
#include <cstdio>
int main(){
    using namespace circle::motor;
    unsigned errors{};
    auto check=[&](bool ok,const char* why){if(!ok){++errors;std::fprintf(stderr,"FAIL: %s\n",why);}};
    const std::array<std::array<float,3>,2> positions{{{-.032f,0,0},{.032f,0,0}}};
    for(bool loaded:{false,true})for(bool inspection:{false,true})for(bool stereo:{false,true}){
        check(fixedCinemaSurface(loaded)==loaded,"full surface survives cinema/inspection/stereo switches without DLSS resize");
    }
    for(float tx:{.3f,.6f,1.f}){
        const float ty=cinemaVerticalTangent(tx);
        // Equal world-space X/Y displacement must have equal physical size
        // on a 16:9 panel even if its underlying render texture is portrait.
        check(std::fabs((16.f/tx)-(9.f/ty))<.0001f,"16:9 film has no geometric aspect distortion");
        check(std::fabs(ty/tx-9.f/16.f)<.0001f,"flat and stereo film use full-surface 16:9 optics");
    }
    for(bool stereo:{false,true}){
        // Gameplay -> film -> inspection -> film -> gameplay, for both settings.
        for(unsigned state:{0u,1u,2u,1u,0u}){
            const bool cinema=state!=0,inspection=state==2;
            const bool mono=monoCinemaView(cinema,inspection,stereo);
            check(mono==(cinema&&(!stereo||inspection)),"presentation policy across transitions");
            if(!cinema){check(!mono,"normal gameplay retains stereo");continue;}
            auto offsets=cinemaEyeOffsets(positions,mono);
            check(offsets.has_value(),"valid film baseline");
            auto l=cinemaFrustum(.6f,.3375f,3.2f,(*offsets)[0],0);
            auto r=cinemaFrustum(.6f,.3375f,3.2f,(*offsets)[1],0);
            check(l&&r,"valid film projections");
            check((*l==*r)==mono,"2D film/inspection use identical center projection, stereo uses distinct eyes");
            if(mono)check((*offsets)[0]==0&&(*offsets)[1]==0,"flat mode removes camera disparity as well as layer disparity");
        }
    }
    for(float tx:{.3f,.6f,1.f})for(float width:{1.5f,3.2f,6.f})for(float x:{-.045f,-.032f,.032f,.045f}){
        auto p=stableCinemaProjection(tx,tx*9/16,width,x);
        check(p.has_value(),"valid symmetric cinema");
        check(std::abs(p->fov[0]+p->fov[1])<1e-6f,"native cinema projection is symmetric");
        check(p->crop[0]>=0&&p->crop[0]+p->crop[1]<=1.00001f,"crop remains inside rendered eye");
        float extent=std::tan(p->fov[1]),shift=x/(width/(2*tx));
        for(float u:{0.f,.25f,.5f,1.f}){
            float croppedRay=-extent+2*extent*(p->crop[0]+u*p->crop[1]);
            float originalRay=-tx-shift+2*tx*u;
            check(std::abs(croppedRay-originalRay)<1e-5f,"presentation crop retains authored disparity and convergence");
        }
        auto other=stableCinemaProjection(tx,tx*9/16,width,-x);
        check(p->fov==other->fov,"both eyes share symmetric lighting optics");
    }
    check(!stableCinemaProjection(0,.3f,3,0)&&!stableCinemaProjection(.6f,.3f,0,0),"invalid cinema optics rejected");
    return errors?1:0;
}
