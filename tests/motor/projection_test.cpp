#include "xr_math/stereo_view.hpp"
#include "stereo_seq/bin_tiles.hpp"
#include "cinema_projection.hpp"
#include "hud_projection.hpp"
#include "packed_projection.hpp"
#include <limits>
#include <cmath>
#include <cstdio>
int main(){
    using namespace circlevr::xr_math;
    int failures=0;auto check=[&](bool ok,const char* why){if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}};
    // Native finite 0..1 depth convention; asymmetric optical frustum.
    EngineMatrix native{};native[0]=1;native[5]=1.8f;native[10]=-1.00001f;native[11]=-0.03f;native[14]=-1;
    const Fov eye{-0.9f,0.75f,0.85f,-0.8f};
    const auto p=engineProjection(eye,native);check(p.has_value(),"valid asymmetric frustum");
    if(p){
        for(int i=8;i<16;++i)check((*p)[i]==native[i],"native near/far and depth rows preserved");
        check(std::fabs((*p)[0]*std::tan(eye.angleLeft)-(*p)[2]+1)<1e-5,"left optical edge maps to -1");
        check(std::fabs((*p)[0]*std::tan(eye.angleRight)-(*p)[2]-1)<1e-5,"right optical edge maps to +1");
        check(std::fabs((*p)[5]*std::tan(eye.angleUp)-(*p)[6]-1)<1e-5,"upper optical edge maps to +1");
        check(std::fabs((*p)[5]*std::tan(eye.angleDown)-(*p)[6]+1)<1e-5,"lower optical edge maps to -1");
        // Motor CEF810 uses ceil(renderDimension / 32) bins. Check the
        // donor grid reaches the actual optical edges at portrait resolution.
        const auto bins=circlevr::stereo_seq::binTileParams(*p,2496,2688);
        check(bins.has_value(),"native portrait light/decal grid is valid");
        if(bins){
            check(std::fabs(bins->left-std::tan(eye.angleLeft))<1e-5,"bin grid starts at left eye edge");
            check(std::fabs(bins->top+std::tan(eye.angleUp))<1e-5,"bin grid starts at upper eye edge");
            check(std::fabs(bins->left+78*bins->width-std::tan(eye.angleRight))<1e-5,"last tile reaches right eye edge");
            check(std::fabs(bins->top+84*bins->height+std::tan(eye.angleDown))<1e-5,"last tile reaches lower eye edge");
        }
    }
    check(!engineProjection({0,0,0,0},native),"degenerate runtime FOV rejected");
    // Independent project/unproject round trip across both eyes, UV edges,
    // native forward/reverse depth, and symmetric fallback optics. The old
    // packed vector loses the optical centre; it must fail this same test.
    for(bool reverse:{false,true})for(unsigned e=0;e<3;++e){
        EngineMatrix source=native;
        source[10]=reverse?.00001f:-1.00001f;source[11]=reverse?.03f:-.03f;
        Fov f=e==2?Fov{-.9f,.9f,.85f,-.85f}:
            e==0?Fov{-.95f,.75f,.85f,-.8f}:Fov{-.75f,.95f,.8f,-.85f};
        const auto q=engineProjection(f,source);
        EngineMatrix inv{};
        inv[0]=1/(*q)[0];inv[3]=(*q)[2]/(*q)[0];
        inv[5]=1/(*q)[5];inv[7]=(*q)[6]/(*q)[5];
        inv[11]=-1;inv[14]=1/(*q)[11];inv[15]=(*q)[10]/(*q)[11];
        const auto original=inv;const auto packed=circle::motor::packedInverseProjectionXY(inv);
        check(packed.has_value(),"packed inverse supports both depth conventions");
        check(inv==original,"packing never mutates inverse or depth parameters");
        if(!packed)continue;
        for(float u:{0.f,.2f,.5f,1.f})for(float v:{0.f,.3f,1.f})for(float depth:{.2f,.8f}){
            const float w=inv[14]*depth+inv[15];
            const float x=(u*(*packed)[0]+(*packed)[2])/w;
            const float y=(v*(*packed)[1]+(*packed)[3])/w,z=inv[11]/w;
            const float clipW=(*q)[14]*z;
            const float outU=.5f*(((*q)[0]*x+(*q)[2]*z)/clipW+1);
            const float outV=.5f*(1-((*q)[5]*y+(*q)[6]*z)/clipW);
            check(std::abs(outU-u)<1.e-5f&&std::abs(outV-v)<1.e-5f,"packed reconstruction projects onto original eye pixel");
            check(std::abs(((*q)[10]*z+(*q)[11])/clipW-depth)<1.e-5f,"packed reconstruction preserves native depth");
        }
        if(e!=2)check(std::abs((*packed)[2]+inv[0])>.01f,"asymmetric regression would fail with original symmetric packing");
        else check((*packed)[2]==-inv[0]&&(*packed)[3]==inv[5],"symmetric optics remain numerically unchanged");
        inv[1]=.2f;check(!circle::motor::packedInverseProjectionXY(inv),"unsupported skew rejected");
        inv=original;inv[3]=std::numeric_limits<float>::quiet_NaN();
        check(!circle::motor::packedInverseProjectionXY(inv),"nonfinite centre rejected");
    }
    check(!circle::motor::packedInverseProjectionXY({}),"empty projection rejected");
    native[14]=0;check(!engineProjection(eye,native),"unvalidated native depth layout rejected");
    // A point on the authored screen plane must have identical position in
    // both pictures; nearer geometry must have genuine binocular disparity.
    const float tx=.8f,ty=.45f,width=3.f,convergence=width/(2*tx);
    float screen[2]{},nearby[2]{};
    native[14]=-1;
    for(unsigned e=0;e<2;++e){
        const float x=e?.032f:-.032f;
        const auto f=circle::motor::cinemaFrustum(tx,ty,width,x,0);
        check(f.has_value(),"cinema frustum valid");
        const auto p=engineProjection({(*f)[0],(*f)[1],(*f)[2],(*f)[3]},native);
        screen[e]=(*p)[0]*(-x)/convergence-(*p)[2];
        nearby[e]=(*p)[0]*(-x)/(convergence*.5f)-(*p)[2];
    }
    check(std::fabs(screen[0])<1e-5&&std::fabs(screen[1])<1e-5,"screen-plane point does not jump between eyes");
    check(nearby[0]>0&&nearby[1]<0,"near object has opposing binocular disparity");
    check(!circle::motor::cinemaFrustum(tx,ty,0,0,0),"invalid panel rejected");
    // Rotate and translate the viewer in all axes; film camera offsets and
    // zero-parallax plane must remain unchanged, including head roll at 180deg.
    for(int i=0;i<100;++i){
        const float a=i*.11f,b=i*.17f;
        const std::array<float,3> half{.032f*std::cos(a)*std::cos(b),.032f*std::sin(b),.032f*std::sin(a)*std::cos(b)};
        std::array<std::array<float,3>,2> eyes{};
        for(unsigned k=0;k<3;++k){eyes[0][k]=float(k)+i*.01f-half[k];eyes[1][k]=float(k)+i*.01f+half[k];}
        auto fixed=circle::motor::cinemaEyeOffsets(eyes,false);
        check(fixed&&std::fabs((*fixed)[0]+.032f)<1.e-6f&&std::fabs((*fixed)[1]-.032f)<1.e-6f,"head pose cannot pan tilt roll or shift cinematic cameras");
        auto mono=circle::motor::cinemaEyeOffsets(eyes,true);
        check(mono&&(*mono)[0]==0&&(*mono)[1]==0,"inspection stays flat despite head movement");
    }
    check(!circle::motor::cinemaEyeOffsets({},false),"invalid stereo eye separation rejected");
    // The same HUD pixel must resolve to the same point on a 2m plane,
    // despite opposing asymmetric lens frusta and a nonzero IPD.
    for(unsigned e=0;e<2;++e){
        const std::array<float,4> f=e?std::array<float,4>{-.75f,.95f,.85f,-.8f}:std::array<float,4>{-.95f,.75f,.85f,-.8f};
        const float ex=e?.032f:-.032f;const auto v=circle::motor::hudViewport(f,ex);
        check(v.has_value(),"HUD eye viewport valid");
        if(v)for(float x:{0.f,.5f,1.f}){
            const float u=(*v)[0]+x*(*v)[2];
            const float worldX=ex+2*(std::tan(f[0])+u*(std::tan(f[1])-std::tan(f[0])));
            check(std::fabs(worldX-2*.65f*(2*x-1))<1e-5,"HUD pixels converge on shared plane");
            const float top=std::tan(f[2])-(*v)[1]*(std::tan(f[2])-std::tan(f[3]));
            check(std::fabs(top-.65f*9/16)<1e-5,"HUD aspect remains 16:9");
        }
    }
    check(!circle::motor::hudViewport({0,0,0,0},0),"degenerate HUD frustum rejected");
    check(!circle::motor::hudViewport({-.8f,.8f,.8f,-.8f},1),"invalid HUD eye offset rejected");
    constexpr float rad=3.14159265358979323846f/180.f;
    check(circle::motor::binocularHudMaterial("hud_binoculars_fla.material_4"),"binocular SWF material recognized");
    check(circle::motor::binocularHudMaterial("textures/guis/hud/binoculars/hud_binoculars_background.png$bc7$streamed$mtlkind=ui"),"binocular background recognized");
    check(!circle::motor::binocularHudMaterial("models/props/binoculars.idasset"),"world binocular prop does not unlock HUD");
    check(!circle::motor::binocularHudMaterial("hud_journal_fla.material_4"),"journal retains its HUD policy");
    check(circle::motor::freshBinocularHud(1000,1100,true,false),"active viewfinder follows head");
    check(!circle::motor::freshBinocularHud(1000,1250,true,false),"closed viewfinder expires");
    check(!circle::motor::freshBinocularHud(1000,1100,true,true),"pause retains menu policy");
    check(!circle::motor::freshBinocularHud(1000,1100,false,false),"cinema ownership retained");
    check(!circle::motor::freshBinocularHud(0,10,true,false)&&!circle::motor::freshBinocularHud(1000,999,true,false),"missing or invalid timestamps rejected");
    using circle::motor::followHudYaw;
    check(followHudYaw(0,9*rad,.1)==0,"HUD ignores motion inside deadzone");
    check(followHudYaw(179*rad,-179*rad,.1)==179*rad,"HUD deadzone wraps across 180 degrees");
    const float step=followHudYaw(0,30*rad,.1);
    check(step>0&&step<20*rad,"HUD smoothly follows only excess angle");
    float a=0,b=0;
    for(int i=0;i<90;++i)a=followHudYaw(a,30*rad,1./90);
    for(int i=0;i<72;++i)b=followHudYaw(b,30*rad,1./72);
    check(std::fabs(a-b)<1e-5,"HUD following independent of refresh rate");
    check(std::fabs(followHudYaw(0,-30*rad,.1)+step)<1e-6,"HUD follows both directions symmetrically");
    return failures?1:0;
}
