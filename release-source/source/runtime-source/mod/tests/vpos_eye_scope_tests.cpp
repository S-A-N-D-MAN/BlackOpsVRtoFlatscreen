// SPDX-License-Identifier: GPL-3.0-only
// Production reader and ownership path, using private process memory only.
#include "../src/t5/vpos_eye_scope.cpp"
#include <cstdio>
#include <thread>
#include <limits>
#include <type_traits>
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
Validation validate_path(const std::wstring&){return {true,"fixture","fixture"};}
bool validate_loaded_headers(HMODULE,std::string&){return true;}
std::wstring executable_path(){return L"private fixture";}
}
using namespace bovr::t5;
namespace {
unsigned checks{},failures{};
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
template<class T>void set(std::uintptr_t p,const T& value){std::memcpy(reinterpret_cast<void*>(p),&value,sizeof value);}
template<class T>T read(std::uintptr_t p){T value{};std::memcpy(&value,reinterpret_cast<void*>(p),sizeof value);return value;}
struct Fixture {
    std::uintptr_t base{},buffer{},view{},source{},state{};unsigned eye{};bool current{true};
    VPosViewport viewport{};VPosWorldConstants native{};
    Fixture(){base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4240000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));}
    ~Fixture(){if(base)VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);}
    void init(unsigned e){
        eye=e;buffer=base+0x3740100;view=base+0x420f730+e*0x38e0;source=base+0x3cca570;state=base+0x417ee00;
        viewport={static_cast<int>(e*1920),0,1920,2160};
        set(base+0x3a5b5f8,static_cast<std::uint32_t>(buffer));set(buffer+0x15d7a0,123u);set(buffer+0x16cc3c,2u);
        set(buffer+0x16cc38,e);set(buffer+0x16cc40,static_cast<std::uint32_t>(base+0x420f730));set(buffer+0x16cbe0,2u);
        set(view+0x3510,static_cast<std::uint32_t>(buffer));set(view+0x188,0u);set(view+0x18c,0u);set(view+0x3620,0u);set(view+0x38c4,std::uint8_t{});
        set(view+0x140,viewport);set(source+0x1a4c,viewport);set(state+0x1104,viewport);
        set(source+0x1a08,static_cast<std::uint32_t>(view));set(source+0x1680,static_cast<std::uint32_t>(buffer));
        set(source+0x1a34,1u);set(source+0x1a7c,0u);set(source+0x1a0c,0u);set(source+0x1a88,std::uint8_t{});
        set(state+0x1128,std::uint8_t{3});set(state+0x90,0x1234u);set(source+0x1a80,3840u);set(source+0x1a84,2160u);
        set(base+0x41eb1e8+3*20+12,std::array<std::uint16_t,2>{3840,2160});
        set(base+0x747880,std::array<std::uint32_t,2>{static_cast<std::uint32_t>(source),static_cast<std::uint32_t>(state)});
        VPosMatrix projection{};projection[0]=1.2f;projection[5]=1.5f;projection[10]=1;projection[11]=1;projection[14]=-.25f;
        VPosMatrix transform{};for(unsigned i=0;i<4;++i)transform[5*i]=1;
        set(source+0x200,projection);set(view+0x40,projection);set(source+0x180,transform);
        set(source+0x19aa,std::uint16_t{7});set(source+0x19de,std::uint16_t{7});
        set(source+0x19a6,std::uint16_t{9});set(source+0x19dc,std::uint16_t{9});
        vpos_world_constants({3840,2160,{0,0,3840,2160},projection,transform},&native);set(source+0x960,native);
        current=true;configured_base.store(base);
    }
    bool begin(VPosEyeScope& scope,const WaterEyeOrigins* pair=nullptr){return scope.begin(base,buffer,123,eye,viewport,0x1234,19,2,55,
        {this,[](void* p) noexcept {return static_cast<Fixture*>(p)->current;}},pair);}
    template<class T>void rejects(std::uintptr_t p,const T& value,const char* label){
        const auto old=read<T>(p);set(p,value);VPosEyeContext context{};check(!read_vpos_eye_context(&context)&&context==VPosEyeContext{},label);set(p,old);
    }
};
void reader_and_ownership(){
    Fixture f;check(f.base!=0,"fixture memory allocated");if(!f.base)return;
    for(unsigned eye=0;eye<2;++eye){
        f.init(eye);VPosEyeScope scope;VPosEyeContext context{};
        check(!read_vpos_eye_context(&context),"unscoped native data confers no authority");
        check(f.begin(scope),"owned ordinary eye admitted");
        check(read_vpos_eye_context(&context)&&context.eye==eye&&context.view==f.view&&context.source==f.source&&
            context.constants==f.native&&context.constants!=context.corrected_constants,"source and corrected vectors belong to selected eye");
        VPosEyeContext diagnostic{};
        check(read_vpos_eye_owner(&diagnostic)&&diagnostic.eye==eye&&diagnostic.view==f.view&&diagnostic.source==f.source&&
              diagnostic.constants==VPosWorldConstants{}&&diagnostic.corrected_constants==VPosWorldConstants{},"diagnostic owner contains no VPOS correction authority");
        set(f.source+0x19f0,VPosVector{100.f,200.f,300.f,1.f});set(f.source+0x1790,std::array<float,3>{101.f,201.f,301.f});
        set(f.view+0x100,std::array<float,3>{102.f,202.f,302.f});
        set(f.source+0xa00,VPosVector{1.f,2.f,3.f,4.f});set(f.source+0x1090,VPosVector{2.f,3.f,4.f,5.f});
        WaterEyeNativeSnapshot water{};
        check(read_water_eye_native(&water)&&water.owner==diagnostic&&water.source_eye_offset==VPosVector{100.f,200.f,300.f,1.f}&&
            water.source_view_origin[0]==101.f&&water.selected_view_origin[0]==102.f&&water.source_game_time[3]==4.f&&water.source_hdr[0]==2.f,
            "water diagnostic copies camera relative inputs and time/HDR from qualified native source");
        WaterDrawOwnerSnapshot raw{};
        check(read_water_draw_owner(&raw)&&raw.owner.eye==eye&&raw.owner.view==f.view&&raw.owner.source==0&&raw.owner.state==0&&
            raw.owner.target_width==3840&&raw.owner.target_height==2160&&raw.selected_view_origin[0]==102.f,
            "draw diagnostic proves selected eye without claiming a native source");
        const auto old_view=read<std::uint32_t>(f.source+0x1a08);
        set(f.source+0x1a08,0u);set(f.source+0x1a34,0u);set(f.source+0x1a4c,VPosViewport{});
        check(read_water_draw_owner(&raw)&&!read_water_eye_native(&water)&&!read_vpos_eye_context(&context),
            "inactive global source allows raw GPU diagnostic but never authorizes native correction");
        set(f.source+0x1a08,old_view);set(f.source+0x1a34,1u);set(f.source+0x1a4c,f.viewport);
        set(f.buffer+0x16cc38,1-eye);
        check(!read_water_draw_owner(&raw)&&raw==WaterDrawOwnerSnapshot{},"draw diagnostic refuses other selected eye");
        set(f.buffer+0x16cc38,eye);
        const auto cached=read<std::uint16_t>(f.source+0x19aa);set(f.source+0x19aa,std::uint16_t{0});
        check(read_vpos_eye_owner(&diagnostic)&&!read_vpos_eye_context(&context),"unused VPOS cache cannot suppress camera-only diagnostic");set(f.source+0x19aa,cached);
        set(f.state+0x1128,std::uint8_t{10});check(!read_vpos_eye_owner(&diagnostic),"camera-only diagnostic still refuses a different target");set(f.state+0x1128,std::uint8_t{3});
        check(read<VPosWorldConstants>(f.source+0x960)==f.native,"reader does not mutate native VPOS constants");
        f.rejects(f.buffer+0x16cc38,1-eye,"other eye selection refused");
        f.rejects(f.buffer+0x16cc3c,3u,"auxiliary layout refused");
        f.rejects(f.buffer+0x15d7a0,124u,"changed native frame refused");
        f.rejects(f.view+0x18c,1u,"fullscreen view refused");
        f.rejects(f.view+0x38c4,std::uint8_t{1},"extra view refused");
        f.rejects(f.source+0x1a34,2u,"2D source refused");
        f.rejects(f.source+0x1a7c,1u,"full-target selector refused");
        f.rejects(f.source+0x1a0c,1u,"weapon depth hack refused");
        f.rejects(f.source+0x1a88,std::uint8_t{1},"not-yet-updated viewport refused");
        f.rejects(f.state+0x1128,std::uint8_t{10},"reflection target refused");
        f.rejects(f.state+0x90,0x1235u,"foreign device refused");
        f.rejects(f.source+0x1680,0u,"wrong source backend refused");
        f.rejects(f.source+0x1a08,0u,"wrong source camera refused");
        f.rejects(f.source+0x19aa,std::uint16_t{6},"stale projection cache refused");
        auto stale=f.native;stale.one[0]+=.1f;
        f.rejects(f.source+0x960,stale,"stale VPOS vectors refused despite current matrix versions");
        f.rejects(f.state+0x1104,VPosViewport{0,0,3840,2160},"actual native viewport mismatch refused");
        f.rejects(f.base+0x747880,0u,"unproven local source refused");
        auto asymmetric=read<VPosMatrix>(f.source+0x200);asymmetric[8]=.125f;
        const auto projection=read<VPosMatrix>(f.source+0x200);set(f.source+0x200,asymmetric);set(f.view+0x40,asymmetric);
        check(!read_vpos_eye_context(&context),"asymmetric native projection fails closed");set(f.source+0x200,projection);set(f.view+0x40,projection);
        bool foreign_read=true;std::thread worker([&]{VPosEyeContext foreign{};foreign_read=read_vpos_eye_context(&foreign);});worker.join();
        check(!foreign_read,"foreign thread cannot borrow owner scope");
        f.current=false;check(!read_vpos_eye_context(&context),"retired owner key refused");f.current=true;
        check(read_vpos_eye_context(&context),"temporary failed read does not change native state");
        scope.finish();check(!read_vpos_eye_context(&context),"finish revokes source witness");
        check(!read_vpos_eye_owner(&diagnostic),"finish revokes diagnostic camera owner too");
        check(!read_water_draw_owner(&raw),"finish revokes raw draw diagnostic");
        VPosEyeScope outer,inner;check(f.begin(outer)&&!f.begin(inner)&&!read_vpos_eye_context(&context),"nested scope invalidates outer ownership");outer.finish();
        VPosEyeScope scoped;check(f.begin(scoped),"new scope admitted after nested refusal");
        bool ran=false;invoke_vpos_eye_native(&scoped,[](void* p){*static_cast<bool*>(p)=true;},&ran);
        check(ran&&!read_vpos_eye_context(&context),"native invocation revokes scope on return");
    }
}
void water_shading_owner(){
    Fixture f;check(f.base!=0,"water pair fixture allocated");if(!f.base)return;
    const WaterEyeOrigins original{{{-30001.25f,36000.f,100.f},{-29998.75f,36000.f,100.f}}};
    for(unsigned eye=0;eye<2;++eye){
        f.init(eye);const auto views=f.base+0x420f730;
        set(views+0x100,original[0]);set(views+0x38e0+0x100,original[1]);
        VPosEyeScope missing;WaterShadingEyeContext value{};
        check(f.begin(missing)&&!read_water_shading_eye(&value),"no generated pair means no water shading authority");missing.finish();
        auto supplied=original;VPosEyeScope scope;
        check(f.begin(scope,&supplied),"generated water pair scope admitted");
        supplied[0][0]+=100.f;
        check(read_water_shading_eye(&value)&&value.eye_origins==original&&value.draw.owner.source==0&&value.draw.owner.state==0&&
              value.reflection_offset==VPosVector{eye==0?-1.25f:1.25f,0.f,0.f,0.f},"immutable pair copied and signed reflection offset derived");
        const std::array<float,3> world{-30000.f,36500.f,75.f};
        for(unsigned axis=0;axis<3;++axis)
            check((world[axis]-original[eye][axis])+value.reflection_offset[axis]==world[axis]-.5f*(original[0][axis]+original[1][axis]),
                  "eye-relative vector plus offset reaches common viewpoint");
        const auto old_view=read<std::uint32_t>(f.source+0x1a08);set(f.source+0x1a08,0u);
        check(read_water_shading_eye(&value),"water pair authority is independent of inactive immediate source");set(f.source+0x1a08,old_view);
        auto changed=original[eye];changed[0]+=.01f;set(f.view+0x100,changed);
        check(!read_water_shading_eye(&value)&&value==WaterShadingEyeContext{},"selected native origin mismatch fails closed");set(f.view+0x100,original[eye]);
        changed=original[1-eye];changed[1]+=.01f;set(views+(1-eye)*0x38e0+0x100,changed);
        check(!read_water_shading_eye(&value),"other native origin cannot diverge from immutable producer");set(views+(1-eye)*0x38e0+0x100,original[1-eye]);
        set(f.buffer+0x16cc38,1-eye);check(!read_water_shading_eye(&value),"water pair refuses wrong selected eye");set(f.buffer+0x16cc38,eye);
        set(f.buffer+0x15d7a0,124u);check(!read_water_shading_eye(&value),"water pair refuses later native frame");set(f.buffer+0x15d7a0,123u);
        bool foreign=true;std::thread worker([&]{WaterShadingEyeContext other{};foreign=read_water_shading_eye(&other);});worker.join();
        check(!foreign,"foreign thread cannot borrow water pair");
        f.current=false;check(!read_water_shading_eye(&value),"retired frame key revokes water pair");f.current=true;
        check(read_water_shading_eye(&value),"read-only failures leave valid water pair intact");
        scope.finish();check(!read_water_shading_eye(&value),"scope finish revokes water pair");
        for(unsigned defect=0;defect<3;++defect){
            auto invalid=original;
            if(defect==0)invalid[0][0]=std::numeric_limits<float>::quiet_NaN();
            if(defect==1)invalid[1][1]=std::numeric_limits<float>::infinity();
            if(defect==2)invalid[0][0]=invalid[1][0]+8.01f;
            VPosEyeScope refused;check(f.begin(refused,&invalid)&&!read_water_shading_eye(&value),"nonfinite or excessive pair separation grants no shading authority");refused.finish();
        }
        auto zero=original;zero[1]=zero[0];set(views+0x38e0+0x100,zero[1]);
        VPosEyeScope mono;check(f.begin(mono,&zero)&&read_water_shading_eye(&value)&&value.reflection_offset==VPosVector{},"zero separation is a no-op offset");mono.finish();
    }
}
void water_fast_owner(){
    Fixture f;check(f.base!=0,"fast owner fixture allocated");if(!f.base)return;
    const WaterEyeOrigins pair{{{-101.25f,300.f,40.f},{-98.75f,300.f,40.f}}};
    for(unsigned eye=0;eye<2;++eye){
        f.init(eye);const auto views=f.base+0x420f730;
        set(views+0x100,pair[0]);set(views+0x38e0+0x100,pair[1]);
        VPosEyeScope scope;WaterShadingEyeContext witness{};
        check(f.begin(scope,&pair)&&read_water_shading_eye(&witness)&&water_shading_eye_current(witness),
              "full admission supplies a current fast witness for either eye");
        auto rejects=[&](std::uintptr_t address,const auto& value,const char* label){
            using T=std::decay_t<decltype(value)>;const auto previous=read<T>(address);set(address,value);
            WaterShadingEyeContext full{};
            check(!water_shading_eye_current(witness)&&(!read_water_shading_eye(&full)||full!=witness),label);
            set(address,previous);check(water_shading_eye_current(witness),"native restoration restores exact witness validity");
        };
        rejects(f.base+0x3a5b5f8,0u,"fast guard rejects changed backend");
        rejects(f.buffer+0x15d7a0,124u,"fast guard rejects changed frame");
        rejects(f.buffer+0x16cc3c,3u,"fast guard rejects auxiliary count");
        rejects(f.buffer+0x16cc38,1-eye,"fast guard rejects changed eye");
        rejects(f.buffer+0x16cc40,0u,"fast guard rejects changed view array");
        rejects(f.buffer+0x16cbe0,1u,"fast guard rejects changed view parameter count");
        rejects(f.view+0x3510,0u,"fast guard rejects changed view backend");
        rejects(f.view+0x188,1u,"fast guard rejects other client");
        rejects(f.view+0x18c,1u,"fast guard rejects fullscreen view");
        rejects(f.view+0x3620,1u,"fast guard rejects special view mode");
        rejects(f.view+0x38c4,std::uint8_t{1},"fast guard rejects extra view");
        rejects(f.view+0x140,VPosViewport{0,0,3840,2160},"fast guard rejects changed viewport");
        rejects(f.base+0x41eb1e8+3*20+12,std::array<std::uint16_t,2>{1920,2160},"fast guard rejects changed target size");
        auto altered=pair[0];altered[0]+=.25f;
        rejects(views+0x100,altered,"fast guard rejects changed left origin");
        altered=pair[1];altered[2]+=.25f;
        rejects(views+0x38e0+0x100,altered,"fast guard rejects changed right origin");
        for(unsigned defect=0;defect<7;++defect){
            auto wrong=witness;
            if(defect==0)++wrong.draw.owner.producer;
            if(defect==1)++wrong.draw.owner.generation;
            if(defect==2)++wrong.draw.owner.xr_frame;
            if(defect==3)++wrong.draw.owner.device;
            if(defect==4)++wrong.scope_serial;
            if(defect==5)wrong.reflection_offset[0]+=.25f;
            if(defect==6)wrong.draw.owner.source=f.source;
            check(!water_shading_eye_current(wrong),"fast guard rejects forged epoch device or context");
        }
        bool foreign=true;std::thread worker([&]{foreign=water_shading_eye_current(witness);});worker.join();
        check(!foreign,"fast witness cannot cross owner thread");
        f.current=false;check(!water_shading_eye_current(witness),"callback invalidation immediately revokes fast witness");f.current=true;
        scope.finish();check(!water_shading_eye_current(witness),"finished scope revokes fast witness");
        VPosEyeScope replacement;WaterShadingEyeContext newer{};
        check(f.begin(replacement,&pair)&&read_water_shading_eye(&newer)&&newer.scope_serial!=witness.scope_serial&&
              !water_shading_eye_current(witness)&&water_shading_eye_current(newer),"same producer values cannot revive a retired scope incarnation");
        VPosEyeScope nested;check(!f.begin(nested,&pair)&&!water_shading_eye_current(newer),"nested scope immediately invalidates fast witness");
        replacement.finish();
    }
}
}
int main(){reader_and_ownership();water_shading_owner();water_fast_owner();std::printf("vpos eye scope: %u checks, %u failures\n",checks,failures);return failures?1:0;}
