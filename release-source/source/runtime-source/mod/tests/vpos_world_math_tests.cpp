// SPDX-License-Identifier: GPL-3.0-only
#include "t5/vpos_world_math.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
using namespace bovr::t5;
namespace {
unsigned checks{},failures{};
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
using MatrixD=std::array<double,16>;
using VectorD=std::array<double,4>;
MatrixD doubles(const VPosMatrix& m){MatrixD d{};std::copy(m.begin(),m.end(),d.begin());return d;}
MatrixD invert(MatrixD m){
    // Independent general inverse-projection oracle, rather than a second
    // copy of the VPOS coefficient recipe under test.
    std::array<std::array<double,8>,4> a{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){a[r][c]=m[r*4+c];a[r][4+c]=r==c?1.:0.;}
    for(unsigned c=0;c<4;++c){
        unsigned pivot=c;
        for(unsigned r=c+1;r<4;++r)if(std::fabs(a[r][c])>std::fabs(a[pivot][c]))pivot=r;
        if(std::fabs(a[pivot][c])<1.e-12)return {};
        std::swap(a[c],a[pivot]);const double s=a[c][c];for(double& f:a[c])f/=s;
        for(unsigned r=0;r<4;++r)if(r!=c){const double q=a[r][c];for(unsigned j=0;j<8;++j)a[r][j]-=q*a[c][j];}
    }
    MatrixD out{};for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)out[r*4+c]=a[r][4+c];return out;
}
VectorD transform(VectorD v,const MatrixD& m){
    VectorD out{};for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)out[c]+=v[r]*m[4*r+c];return out;
}
VPosMatrix identity(){VPosMatrix m{};for(unsigned i=0;i<4;++i)m[i*5]=1.f;return m;}
VPosMatrix perspective(float x=2.f,float y=4.f,float ox=0.f,float oy=0.f){
    VPosMatrix m{};m[0]=x;m[5]=y;m[8]=ox;m[9]=oy;m[10]=1.f;m[11]=1.f;m[14]=-.25f;return m;
}
VPosMatrix rotated(){
    // Includes native-like negative handedness and a nonzero origin. The
    // direction reconstruction must exclude translation through w=0.
    constexpr float c=.6f,s=.8f;
    return {c,s,0,0, 0,0,1,0, s,-c,0,0, 12500,-971,341,1};
}
VPosWorldInput fixture(){return {1600,900,{800,100,800,600},perspective(),identity()};}
VectorD evaluate(const VPosWorldConstants& c,double x,double y){
    VectorD out{};for(unsigned i=0;i<4;++i)out[i]=x*c.x[i]+y*c.y[i]+c.one[i];return out;
}
bool near(double a,double b,double tolerance=2.e-5){return std::fabs(a-b)<=tolerance*std::max({1.,std::fabs(a),std::fabs(b)});}
bool close(VectorD a,VectorD b){for(unsigned i=0;i<4;++i)if(!near(a[i],b[i]))return false;return true;}
VectorD oracle(const VPosWorldInput& in,double x,double y){
    const auto& v=in.viewport;
    const double nx=2.*(x-v.x)/v.width-1.,ny=1.-2.*(y-v.y)/v.height;
    auto camera=transform({nx,ny,.5,1.},invert(doubles(in.projection)));
    // Perspective inverse is homogeneous; its xyz ratios give the ray.
    const double z=camera[2];camera={-camera[0]/z,-camera[1]/z,-1.,0.};
    return transform(camera,doubles(in.inverse_view));
}
void pinned_and_packed(){
    auto in=fixture();VPosWorldConstants c{};
    check(vpos_world_constants(in,&c),"valid offset native perspective is admitted");
    check(near(c.x[0],-.00125)&&near(c.y[1],1./1200.)&&near(c.one[0],1.5)&&near(c.one[1],-1./3.)&&c.one[2]==-1.f,
          "pinned negative-ray signs and viewport-origin constants");
    check(close(evaluate(c,800,100),{.5,-.25,-1,0}),"absolute top-left integer pixel has no half-pixel shift");
    check(close(evaluate(c,1200,400),{0,0,-1,0}),"offset viewport optical center is forward");
    in={128,64,{0,0,128,64},perspective(),identity()};
    check(vpos_world_constants(in,&c),"full symmetric native recipe admitted by pure math");
    check(c.x[0]==-.0078125f&&c.y[1]==.0078125f&&c.one[0]==.5f&&c.one[1]==-.25f&&c.one[2]==-1.f,
          "native full-target dyadic fixture retains exact coefficients");
    const std::array<std::array<unsigned,2>,4> resolutions{{{1280,720},{2560,1440},{3840,2160},{5120,2880}}};
    for(const auto& size:resolutions)for(unsigned eye=0;eye<2;++eye){
        const unsigned width=size[0]/2;
        in={size[0],size[1],{static_cast<int>(eye*width),0,static_cast<int>(width),static_cast<int>(size[1])},perspective(),identity()};
        check(vpos_world_constants(in,&c),"each supported internal resolution and ordinary eye admitted");
        const double x=eye*width+width*.5,y=size[1]*.5;
        check(close(evaluate(c,x,y),{0,0,-1,0}),"both packed-eye centers reconstruct their camera forward ray");
        // Demonstrate the old full-target recipe reproduces opposite erroneous
        // lateral rays. This check fails if the original bug is reinstated.
        auto legacy=in;legacy.viewport={0,0,static_cast<int>(size[0]),static_cast<int>(size[1])};
        VPosWorldConstants old{};check(vpos_world_constants(legacy,&old),"legacy full-target fixture computes");
        check(std::fabs(evaluate(old,x,y)[0])>.24,"old packed-target camera ray is wrong at either eye center");
    }
}
void inverse_projection_proof(){
    const std::array<VPosViewport,4> viewports{{{0,0,1920,2160},{1920,0,1920,2160},{201,127,777,444},{0,0,3840,2160}}};
    for(const auto& v:viewports)for(unsigned eye=0;eye<2;++eye)for(bool rotation:{false,true}){
        // Physical XR-like asymmetric tangent limits exercise the general
        // row-vector projection, independently of the current symmetric/crop
        // compositor implementation.
        const double l=eye?-.839099631:-1.376381920,r=eye?1.376381920:.839099631;
        const double b=-1.110612515,t=1.035530314;
        auto p=perspective(static_cast<float>(2./(r-l)),static_cast<float>(2./(t-b)),
                           static_cast<float>(-(r+l)/(r-l)),static_cast<float>(-(t+b)/(t-b)));
        auto in=VPosWorldInput{3840,2160,v,p,rotation?rotated():identity()};
        VPosWorldConstants c{};check(vpos_world_constants(in,&c),"asymmetric projection with translated or full viewport admits");
        for(double u:{0.,.137,.5,.819,1.})for(double w:{0.,.211,.5,1.}){
            const double x=v.x+u*v.width,y=v.y+w*v.height;
            check(close(evaluate(c,x,y),oracle(in,x,y)),"VPOS world ray equals independently inverted projection at every sample");
        }
        auto translated=in;translated.inverse_view[12]-=237.f;translated.inverse_view[13]+=521.f;translated.inverse_view[14]-=1120.f;
        VPosWorldConstants moved{};check(vpos_world_constants(translated,&moved)&&c==moved,
                                      "shared-world translation cannot change camera direction constants");
    }
}
void refusals(){
    const auto original=fixture();VPosWorldConstants c{{1,2,3,4},{5,6,7,8},{9,10,11,12}};VPosWorldStatus why{};
    auto rejects=[&](VPosWorldInput in,VPosWorldStatus expected,const char* label){
        c.x.fill(99);check(!vpos_world_constants(in,&c,&why)&&why==expected&&c==VPosWorldConstants{},label);
    };
    auto in=original;in.viewport.x=-1;rejects(in,VPosWorldStatus::viewport,"negative viewport origin rejected without coefficients");
    in=original;in.viewport.width=0;rejects(in,VPosWorldStatus::viewport,"zero viewport rejected");
    in=original;in.viewport.x=std::numeric_limits<int>::max();rejects(in,VPosWorldStatus::viewport,"overflow-sized origin rejected");
    in=original;in.viewport.height=901;rejects(in,VPosWorldStatus::viewport,"out-of-target viewport rejected");
    in=original;in.target_width=16385;rejects(in,VPosWorldStatus::viewport,"unbounded target rejected");
    for(unsigned index:{0u,8u,14u}){in=original;in.projection[index]=std::numeric_limits<float>::quiet_NaN();rejects(in,VPosWorldStatus::nonfinite,"nonfinite projection rejected");}
    in=original;in.inverse_view[12]=std::numeric_limits<float>::infinity();rejects(in,VPosWorldStatus::nonfinite,"nonfinite camera rejected");
    in=original;in.projection[0]=0;rejects(in,VPosWorldStatus::projection,"singular perspective rejected");
    in=original;in.projection[11]=-1;rejects(in,VPosWorldStatus::projection,"different clip-W convention refused");
    in=original;in.projection[15]=1;rejects(in,VPosWorldStatus::projection,"orthographic camera refused");
    in=original;in.projection[1]=.1f;rejects(in,VPosWorldStatus::projection,"unsupported projection skew refused");
    in=original;in.inverse_view[0]=2;rejects(in,VPosWorldStatus::inverse_view,"nonrigid camera refused");
    in=original;in.inverse_view[3]=1;rejects(in,VPosWorldStatus::inverse_view,"nonaffine camera refused");
    check(!vpos_world_constants(original,nullptr,&why)&&why==VPosWorldStatus::output,"null output refused");
}
void policy(){
    VPosCameraPolicy p{0,2,1,0,3,true,true,false,false};
    check(vpos_camera_correction_allowed(p),"proved ordinary left scene camera admitted");
    p.eye=1;check(vpos_camera_correction_allowed(p),"proved ordinary right scene camera admitted");
    auto reject=[&](VPosCameraPolicy q,const char* label){check(!vpos_camera_correction_allowed(q),label);};
    auto q=p;q.eye=2;reject(q,"third/auxiliary eye refused");
    q=p;q.view_count=3;reject(q,"auxiliary view layout remains outside initial correction");
    q=p;q.source_camera_matches=false;reject(q,"unmatched local camera source refused");
    q=p;q.ordinary_eye_owned=false;reject(q,"missing owner-thread/eye identity refused");
    q=p;q.fullscreen=true;reject(q,"fullscreen camera refused");
    q=p;q.extra_view=true;reject(q,"native extra view refused");
    for(unsigned mode:{0u,2u,3u}){q=p;q.view_mode=mode;reject(q,"non-3D source mode refused");}
    q=p;q.viewport_behavior=1;reject(q,"FULL viewport behavior not silently changed");
    for(unsigned target:{2u,4u,6u,10u,14u,15u,22u,23u,25u}){q=p;q.target=target;reject(q,"shadow/reflection/postfx/unproven target remains untouched");}
}
void gpu_viewport_transform(){
    for(unsigned width:{2560u,3840u,5120u})for(bool rotate:{false,true}){
        VPosWorldInput full{width,2880,{0,0,static_cast<int>(width),2880},perspective(),rotate?rotated():identity()};
        VPosWorldConstants native{},transformed{};
        check(vpos_world_constants(full,&native),"native full-target fixture");
        check(vpos_viewport_constants(native,width,2880,full.viewport,&transformed)&&native==transformed,
              "GPU full-viewport transform exactly preserves native constants");
        for(unsigned eye=0;eye<2;++eye){
            auto packed=full;packed.viewport={static_cast<int>(eye*width/2),127,static_cast<int>(width/2),2444};
            check(vpos_viewport_constants(native,width,2880,packed.viewport,&transformed),"GPU packed transform admits");
            for(double u:{0.,.14,.5,.83,1.})for(double v:{0.,.29,.5,1.}){
                const double x=packed.viewport.x+u*packed.viewport.width,y=packed.viewport.y+v*packed.viewport.height;
                check(close(evaluate(transformed,x,y),oracle(packed,x,y)),
                      "actual-vector GPU transform matches independent inverse-projection ray");
            }
        }
    }
    auto fixture_native=VPosWorldConstants{};VPosWorldConstants out{};
    check(!vpos_viewport_constants(fixture_native,3840,2160,{-1,0,1920,2160},&out),"negative GPU viewport refused");
    check(!vpos_viewport_constants(fixture_native,3840,2160,{1920,0,1921,2160},&out),"out of bounds GPU viewport refused");
    check(!vpos_viewport_constants(fixture_native,3840,0,{0,0,1920,2160},&out),"zero GPU target refused");
    fixture_native.x[0]=std::numeric_limits<float>::quiet_NaN();
    check(!vpos_viewport_constants(fixture_native,3840,2160,{0,0,1920,2160},&out)&&out==VPosWorldConstants{},"nonfinite GPU vectors refused and output cleared");
}
}
int main(){pinned_and_packed();inverse_projection_proof();refusals();policy();gpu_viewport_transform();
    std::printf("vpos world math: %u checks, %u failures\n",checks,failures);return failures?1:0;}
