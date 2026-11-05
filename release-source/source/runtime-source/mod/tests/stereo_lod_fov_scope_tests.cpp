// SPDX-License-Identifier: GPL-3.0-only
// Real production scope, isolated registered-dvar and native-frame fixtures.
#include "../src/t5/stereo_lod_fov_scope.cpp"
#include <cstdio>
#include <cstdarg>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
namespace {std::vector<std::string> logs;}
namespace bovr {void trace(const char* event,const char* format,...) noexcept {
    char text[2048]{};va_list a;va_start(a,format);vsnprintf(text,sizeof text,format,a);va_end(a);
    try{logs.emplace_back(std::string(event)+" "+text);}catch(...){}
}}
using namespace bovr::t5;
namespace {
unsigned checks{},failures{},calls{},mode{},current_eye{};bool owned=true;
std::uintptr_t base{},buffer{},refdef{},view{},scale{},bias{},threshold{},lockpvs{};
__declspec(align(16)) unsigned char expected_fp[512]{},actual_fp[512]{};
void check(bool okay,const char* why){++checks;if(!okay){++failures;printf("FAIL %s\n",why);}}
template<class T>void store(std::uintptr_t p,const T& v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof v);}
bool owner(void*) noexcept{return owned;}
float load(std::uintptr_t p){float f{};std::memcpy(&f,reinterpret_cast<void*>(p),4);return f;}
void dvar(std::uintptr_t p,const char* name,unsigned type,float value,float minimum,float maximum){
    std::array<unsigned char,0x70> bytes{};store(p,bytes);store(p,static_cast<unsigned>(p+0x100));
    std::memcpy(reinterpret_cast<void*>(p+0x100),name,std::strlen(name)+1);store(p+0x10,type);store(p+0xc,0x80u);
    store(p+0x18,value);store(p+0x28,value);store(p+0x38,value);store(p+0x58,minimum);store(p+0x5c,maximum);
}
void fixture(unsigned eye=0,float scale_value=1.f,float bias_value=0.f){
    owned=true;poisoned=false;logs.clear();mode=0;current_eye=eye;
    reports=0;eye_reports[0]=0;eye_reports[1]=0;
    buffer=base+first;refdef=base+0x20000;view=base+views_first+eye*0x38e0;
    scale=base+0x10000;bias=base+0x10200;threshold=base+0x10400;lockpvs=base+0x10600;
    // Captured retail registration differs from the donor source: the
    // persistent scale domain is [1,4], and bias is [-1000,0].
    dvar(scale,"r_lodScaleRigid",1,scale_value,1.f,4.f);
    dvar(bias,"r_lodBiasRigid",1,bias_value,-1000.f,0.f);
    dvar(threshold,"r_fovScaleThresholdRigid",1,2.4f,1.f,100.f);dvar(lockpvs,"r_lockPvs",0,0.f,0.f,1.f);
    store(base+scale_slot,static_cast<unsigned>(scale));store(base+bias_slot,static_cast<unsigned>(bias));
    store(base+threshold_slot,static_cast<unsigned>(threshold));store(base+0x371fd24,static_cast<unsigned>(lockpvs));
    store(base+0x373708c,static_cast<unsigned>(buffer));store(buffer+0x15d7a0,17u);
    store(buffer+0x16cc3c,eye);store(buffer+0x16cbe0,eye);store(buffer+0x16cc38,0u);
    store(buffer+0x16cc40,static_cast<unsigned>(base+views_first));store(refdef+0x183a4,0u);store(refdef+0x14,1.3901109f);
    const std::array<float,3> origin{10.f,20.f,30.f};const std::array<int,4> vp{int(eye)*2560,0,2560,2880};
    store(refdef+0x20,origin);store(refdef,vp);store(view+0x100,origin);store(view+0x140,vp);store(view+0x188,0u);store(view+0x3510,static_cast<unsigned>(buffer));
    store(base+0x356ecf8,origin);store(base+0x356ecf8+0xc,1.0123f);store(base+0x356ecf8+0x10,0.f);store(base+0x356ecf8+0x14,1.0123f);
    const auto world=base+0x40000,instances=base+0x41000,model=base+0x42000,vis=base+0x43000,lods=base+0x44000;
    store(base+0x356a24c,static_cast<unsigned>(world));store(world+0x344,10u);store(world+0x3a4,static_cast<unsigned>(instances));
    store(world+0x370,static_cast<unsigned>(vis));store(world+0x390,static_cast<unsigned>(lods));store(instances+2*0x4c+0x38,static_cast<unsigned>(model));
    store(vis+2,static_cast<unsigned char>(1));store(lods,3u<<4);
    configured_base=base;enabled=true;probe=true;selected_count=1;selected[0]=2;
}
bool start(StereoLodFovScope& s){return s.begin(base,buffer,17,refdef,123,current_eye,{nullptr,&owner});}
void native_body(void*){
    ++calls;
    if(mode==1)throw std::runtime_error("native exception");
    if(mode==2)RaiseException(0x12345678,0,0,nullptr);
    if(mode==5){StereoLodFovScope nested;check(!start(nested),"nested scope refuses authority");}
    const float f=multiply(load(refdef+0x14),native_factor),old=load(base+0x356ecf8+0x14);
    const float applied=f>=old||divide(old,f)>load(threshold+0x18)?f:old;
    const std::array<float,3> actual{multiply(load(scale+0x18),applied),multiply(load(bias+0x18),applied),applied};
    store(base+0x356ecf8+0xc,actual);store(buffer+0x16cc3c,current_eye+1);store(buffer+0x16cbe0,current_eye+1);store(buffer+0x16cc38,current_eye);
    if(mode==3)store(scale+0x18,8.f);
    if(mode==4)store(buffer+0x15d7a0,18u);
    if(mode==6)store(scale+0x10,5u);
    if(mode==7)store(base+0x356ecf8+0xc,6.f);
    if(mode==8)store(buffer+0x16cc38,1u-current_eye);
    SetLastError(0x12344321);
    __asm {fninit
        fld1
        pcmpeqd xmm0,xmm0
        pcmpeqd xmm1,xmm1
        pcmpeqd xmm2,xmm2
        fxsave expected_fp}
}
bool catch_seh(StereoLodFovScope* s){__try{invoke_stereo_lod_fov_native(s,&native_body,nullptr);}__except(EXCEPTION_EXECUTE_HANDLER){return true;}return false;}
int lod(float adjusted,float raw){constexpr float distances[]{300,650,900,1700};for(int i=0;i<4;++i)if(adjusted<distances[i])return i;for(int i=0;i<4;++i)if(raw<distances[i])return i;return -1;}
bool contains(const char* s){for(const auto& line:logs)if(line.find(s)!=std::string::npos)return true;return false;}
}
int main(){
    StereoLodFovMath m{};
    check(stereo_lod_fov_math(1.3901109f,1.0123f,2.4f,1.f,0.f,&m)&&m.applied>2.9f&&std::abs(m.expected_scale-1.f)<1e-7f&&m.expected_bias==0,"captured VR FOV compensated");
    check(stereo_lod_fov_math(1.f,3.f,2.4f,1.f,0.f,&m)&&m.applied==3.f,"hysteresis keeps prior larger factor");
    check(stereo_lod_fov_math(.4f,3.f,2.4f,1.f,0.f,&m)&&m.applied<1.f,"hysteresis threshold crossing adopts smaller factor");
    for(float scale_value:{0.f,.25f,1.f,2.f})for(float bias_value:{-25.f,-0.f,0.f,30.f})for(float tangent:{.5f,1.f,1.3901109f,2.f}){
        check(stereo_lod_fov_math(tangent,3.f,2.4f,scale_value,bias_value,&m),"valid user settings and FOV math");
        check(std::abs(m.expected_scale-scale_value)<=3e-7f&&std::abs(m.expected_bias-bias_value)<=4e-6f,"original user scale and bias retained to float precision");
    }
    const auto nan=(std::numeric_limits<float>::quiet_NaN)(),inf=(std::numeric_limits<float>::infinity)();
    check(!stereo_lod_fov_math(0,1,2.4f,1,0,&m)&&!stereo_lod_fov_math(-1,1,2.4f,1,0,&m),"nonpositive FOV rejected");
    check(!stereo_lod_fov_math(nan,1,2.4f,1,0,&m)&&!stereo_lod_fov_math(1,inf,2.4f,1,0,&m)&&!stereo_lod_fov_math(1,1,0,1,0,&m)&&!stereo_lod_fov_math(1,1,2.4f,-1,0,&m),"invalid native math rejected");
    check(!stereo_lod_fov_math(1,1,2.4f,1,inf,&m)&&!stereo_lod_fov_math(1,1,2.4f,1,0,nullptr),"nonfinite preference and missing output rejected");
    check(lod(2.94519f*578.f,578.f)==1&&lod(2.94519f*577.f,577.f)==3,"native wide FOV reverses authored detail at captured boundary");
    stereo_lod_fov_math(1.3901109f,1.0123f,2.4f,1,0,&m);int previous=4;
    for(int distance=1699;distance>=0;--distance){const auto selected_lod=lod(float(distance)*m.expected_scale,float(distance));check(selected_lod<=previous,"compensated default LOD remains monotonic approaching");previous=selected_lod;}
    check(lod(1701.f*m.expected_scale,1701.f)==-1,"native far cutoff remains outside authored maximum");
    base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4300000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!base){puts("fixture allocation failed");return 2;}
    for(unsigned eye=0;eye<2;++eye){fixture(eye,1.f,-0.f);std::array<unsigned char,0x70> before_s{},before_b{};get(scale,&before_s);get(bias,&before_b);
        StereoLodFovScope s;check(start(s),"owned registered per-eye scope admitted");check(load(scale+0x18)<.35f,"temporary FOV scale applied only within native call");
        const auto before_calls=calls;invoke_stereo_lod_fov_native(&s,&native_body,nullptr);_fxsave(actual_fp);const auto error=GetLastError();
        check(calls==before_calls+1,"original called once");check(!std::memcmp(expected_fp,actual_fp,288)&&error==0x12344321,"native FP SSE and LastError survive scope cleanup");
        std::array<unsigned char,0x70> after_s{},after_b{};get(scale,&after_s);get(bias,&after_b);
        check(s.safe()&&!active_scope&&before_s==after_s&&before_b==after_b,"all dvar bytes and negative zero preference restored");
        check(contains("packed_valid=1")&&contains("hooks_added=0"),"qualified post-eye table evidence captured without return claim");
    }
    // Native buffer recycling clears view counts, not the last selected index.
    // Exercise multiple actual pairs without resetting that index between them.
    for(unsigned stale_index:{1u,2u,3u}){
        fixture();store(buffer+0x16cc38,stale_index);
        std::array<unsigned char,0x70> original_scale{},original_bias{};get(scale,&original_scale);get(bias,&original_bias);
        reports=238;eye_reports[0]=119;eye_reports[1]=119;
        const auto before_calls=calls;
        for(unsigned pair=0;pair<6;++pair){
            const auto frame=17u+pair;store(buffer+0x15d7a0,frame);
            store(buffer+0x16cc3c,0u);store(buffer+0x16cbe0,0u);
            unsigned carried_index{};get(buffer+0x16cc38,&carried_index);
            check(carried_index==(pair?1u:stale_index),"recycled buffer retains prior selected index while empty");
            for(unsigned eye=0;eye<2;++eye){
                current_eye=eye;view=base+views_first+eye*0x38e0;
                const std::array<int,4> vp{int(eye)*2560,0,2560,2880};const std::array<float,3> origin{10.f,20.f,30.f};
                store(refdef,vp);store(view+0x140,vp);store(view+0x100,origin);store(view+0x188,0u);store(view+0x3510,static_cast<unsigned>(buffer));
                StereoLodFovScope s;
                check(s.begin(base,buffer,frame,refdef,123u+pair,eye,{nullptr,&owner}),"both eyes admitted on every recycled pair");
                check(load(scale+0x18)<.35f,"recycled left and right use the same temporary compensation");
                invoke_stereo_lod_fov_native(&s,&native_body,nullptr);
                std::array<unsigned char,0x70> restored_scale{},restored_bias{};get(scale,&restored_scale);get(bias,&restored_bias);
                check(s.safe()&&restored_scale==original_scale&&restored_bias==original_bias,"recycled pair native result validated and exact preferences restored");
                unsigned selected_index{};get(buffer+0x16cc38,&selected_index);
                check(selected_index==eye,"native selects the exact completed eye");
            }
        }
        check(calls==before_calls+12,"repeated stereo pairs call original exactly once per eye");
        unsigned periodic_left{},periodic_right{};
        for(const auto& line:logs)if(line.find("t5_stereo_lod_fov ")==0&&line.find("eye_sample=120 ")!=std::string::npos){
            periodic_left+=line.find("eye=0 admitted=1 actual_valid=1")!=std::string::npos;
            periodic_right+=line.find("eye=1 admitted=1 actual_valid=1")!=std::string::npos;
        }
        check(periodic_left==1&&periodic_right==1,"periodic diagnostics sample each eye without global parity aliasing");
    }
    for(unsigned wrong_index:{1u,2u,3u}){
        fixture(1);store(buffer+0x16cc38,wrong_index);StereoLodFovScope s;
        check(!start(s)&&s.safe()&&load(scale+0x18)==1.f,"pre-right existing-view selected index remains strictly zero");
    }
    for(unsigned eye=0;eye<2;++eye){fixture(eye);StereoLodFovScope s;check(start(s),"post-native wrong-index case admitted");mode=8;
        invoke_stereo_lod_fov_native(&s,&native_body,nullptr);
        check(!s.safe()&&load(scale+0x18)==1.f&&contains("reason=post_native_authority"),"completed-view wrong index refuses publication and restores preferences");
    }
    for(unsigned fault=0;fault<11;++fault){fixture();StereoLodFovScope s;
        switch(fault){case 0:store(buffer+0x15d7a0,18u);break;case 1:store(refdef+0x14,nan);break;case 2:store(scale+0x10,5u);break;
        case 3:store(scale+0x18,-1.f);break;case 4:store(scale+0x5c,.1f);break;case 5:store(lockpvs+0x18,1u);break;
        case 6:store(base+0x356ecf8+0x14,inf);break;case 7:owned=false;break;case 8:store(threshold+0x18,.5f);break;
        case 9:store(buffer+0x16cc3c,1u);break;case 10:store(scale+0x100,'x');break;}
        const float prior=load(scale+0x18);check(!start(s),"invalid owner/dvar/layout refuses before mutation");check(bits(load(scale+0x18))==bits(prior)&&s.safe(),"failed admission never changes preferences");
    }
    for(unsigned fault=0;fault<4;++fault){fixture();reports=0;StereoLodFovScope s;
        if(fault<3)store(scale+0x18,fault==0?.5f:fault==1?0.f:5.f);else store(bias+0x18,1.f);
        const auto original_scale=bits(load(scale+0x18)),original_bias=bits(load(bias+0x18));
        check(!start(s),"out-of-range original retail preference rejected");s.finish();
        check(contains("admitted=0 actual_valid=0")&&contains("reason=begin_original_preference_domain"),"refused scope never labels zero defaults as native results");
        check(bits(load(scale+0x18))==original_scale&&bits(load(bias+0x18))==original_bias,"invalid original preferences remain untouched");
    }
    {fixture();reports=0;StereoLodFovScope s;check(start(s),"retail scale minimum does not reject derived reciprocal");
        check(load(scale+0x18)<load(scale+0x58)&&load(scale+0x58)==1.f&&load(scale+0x5c)==4.f,"temporary below UI minimum leaves registered domain unchanged");
        invoke_stereo_lod_fov_native(&s,&native_body,nullptr);
        // This fixture's reciprocal round trip is exactly one float ULP below
        // one (0x3f7fffff), not the decimal string "1". Production still checks
        // the actual native result bit-for-bit against its predicted SSE math.
        check(s.safe()&&contains("admitted=1 actual_valid=1")&&
            bits(load(base+0x356ecf8+0xc))==0x3f7fffffu&&load(base+0x356ecf8+0x10)==0.f,
            "admitted result reports observed native compensation including float rounding");
    }
    for(unsigned failure=1;failure<=7;++failure){fixture();StereoLodFovScope s;check(start(s),"exception/conflict case admitted");mode=failure;const auto before_calls=calls;bool caught=false;
        if(failure==2)caught=catch_seh(&s);else try{invoke_stereo_lod_fov_native(&s,&native_body,nullptr);}catch(const std::runtime_error&){caught=true;}
        check(calls==before_calls+1&&!s.safe(),"native failure/conflict refuses publication without duplicate call");
        check(failure>2||caught,"native exception propagated");
        check(failure==3?load(scale+0x18)==8.f:failure==6?load(scale+0x18)!=1.f:load(scale+0x18)==1.f,"restoration never overwrites a conflicting foreign write");
        check(bits(load(bias+0x18))==bits(0.f),"independent owned bias restored despite other conflict");check(!active_scope,"failure releases scope TLS");
    }
    {fixture();StereoLodFovScope s;check(start(s),"foreign thread retirement starts");bool result=true;std::thread other([&]{result=s.finish();});other.join();check(!result&&load(scale+0x18)!=1.f,"foreign thread cannot restore");check(!s.finish()&&load(scale+0x18)==1.f,"owner restores after foreign-thread refusal");}
    {fixture();enabled=false;StereoLodFovScope s;check(start(s)&&load(scale+0x18)==1.f,"probe-only path never changes dvars");invoke_stereo_lod_fov_native(&s,&native_body,nullptr);check(s.safe()&&load(scale+0x18)==1.f,"probe-only native result remains unchanged");}
    SetEnvironmentVariableW(L"BOVR_STEREO_LOD_FOV_COMPENSATION",nullptr);SetEnvironmentVariableW(L"BOVR_STEREO_LOD_PROBE",nullptr);
    check(configure_stereo_lod_fov_scope(nullptr)&&!stereo_lod_fov_scope_enabled(),"default OFF performs no native configuration or patch");
    VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
