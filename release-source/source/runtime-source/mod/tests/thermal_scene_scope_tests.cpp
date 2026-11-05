// SPDX-License-Identifier: GPL-3.0-only
// Run the production transaction implementation against copied fixture memory.
// The executable never maps game addresses, installs hooks, or invokes native
// game code. Faults exercise the same write journal and Windows finally path.
#define BOVR_THERMAL_SCENE_SCOPE_TEST 1
#include "../src/t5/thermal_scene_scope.cpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
namespace t=bovr::t5;
namespace d=bovr::t5::thermal_scene_detail;
namespace f=bovr::t5::thermal_scene_fixture;
namespace {
struct Region {std::uintptr_t base;std::vector<std::uint8_t> bytes;bool operator==(const Region&) const=default;};
std::vector<Region> memory;
constexpr std::uintptr_t cg=0x10000000,centities=0x11000000,buffer=d::first_buffer;
constexpr std::uint32_t frame=401;
unsigned checks{},native_calls{};
bool hooks_ready=true,guards_ready=true,fail_native{},consume_vision=true;
std::uintptr_t fail_write{};
void check(bool okay,const char* label){++checks;if(!okay){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
Region* region(std::uintptr_t p,std::size_t size){for(auto& r:memory)if(p>=r.base&&p-r.base<=r.bytes.size()&&size<=r.bytes.size()-(p-r.base))return &r;return nullptr;}
template<class T>void put(std::uintptr_t p,const T& value){if(!f::write_bytes(p,&value,sizeof value))std::abort();}
template<class T>T get(std::uintptr_t p){T out{};if(!f::read_bytes(p,&out,sizeof out))std::abort();return out;}
void reset(){
    memory.clear();hooks_ready=guards_ready=true;fail_native=false;consume_vision=true;fail_write=0;native_calls=0;
    const auto add=[](std::uintptr_t p,std::size_t n){memory.push_back({p,std::vector<std::uint8_t>(n)});};
    add(cg,0xa4580);add(centities,8*d::client_stride);add(buffer,d::buffer_stride);
    for(const auto p:{d::cg_slot,d::frontend_slot,d::backend_slot,d::client_entities_slot,d::scene_count,d::rg_view_count,std::uintptr_t(0x251ca98)})add(p,4);
    add(d::scene_entities,8*0x84);
    put(d::cg_slot,cg);put(d::frontend_slot,buffer);put(d::backend_slot,buffer);put(d::client_entities_slot,centities);
    put(0x251ca98,GetCurrentThreadId());put(buffer+d::frame_offset,frame);
    put(buffer+d::view_pointer_offset,d::first_views);put(buffer+d::gfx_count_offset,8u);put(d::scene_count,6u);
    d::put_counts(buffer,2,1,2);
    std::array<std::uint8_t,0x288> vision{};vision.fill(0x36);put(cg+0x8c164,vision);put(cg+0xa457c,0x3f800000u);
    for(unsigned i=0;i<6;++i){const auto n=i+1;const auto scene=d::scene_entities+i*0x84;const auto cent=centities+n*d::client_stride;
        put(scene+0x6c,static_cast<std::uint16_t>(n));put(scene+0x6e,static_cast<std::uint16_t>(n));put(scene+0x74,cent);
        put(cent+0x164,n);put(cent+0x222,std::uint16_t(0x11));put(cent+0x310,std::uint8_t(2));put(cent+4,std::uint8_t(0));
        put(buffer+d::gfx_offset+n*0x18,0x400004u);
    }
    // Only the first actor has complete native eligibility and identity.
    put(centities+2*d::client_stride+0x310,std::uint8_t(0));
    put(centities+3*d::client_stride+0x222,std::uint16_t(0x12));
    put(d::scene_entities+3*0x84+0x74,centities);
    put(centities+5*d::client_stride+0x164,17u);
    put(buffer+d::gfx_offset+6*0x18,4u);
}
void completed(void*){check(d::put_counts(buffer,4,3,4),"fixture completes reserved native slot");}
void cpp_failure(void*){throw 7;}
void seh_failure(void*){RaiseException(0xe0000110,0,0,nullptr);}
bool catch_seh(t::ThermalSceneScope* thermal,t::ThermalViewSlotScope* slot) noexcept {
    __try {t::invoke_thermal_scene_native(thermal,slot,seh_failure,nullptr);}
    __except(GetExceptionCode()==0xe0000110?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
void backend_completed(void*){
    check(get<std::uint32_t>(buffer+d::view_count_offset)==4&&get<std::uint32_t>(buffer+d::view_index_offset)==3,"backend selects only the reserved fourth view");
    check(!t::invoke_thermal_backend_native(buffer,completed,nullptr),"nested thermal backend pass rejected");
}
bool catch_backend_seh() noexcept {
    __try {t::invoke_thermal_backend_native(buffer,seh_failure,nullptr);}
    __except(GetExceptionCode()==0xe0000110?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
}
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {bool ensure_infrared_scope_adapter() noexcept{return hooks_ready;}}
namespace bovr::t5::thermal_scene_fixture {
bool read_bytes(std::uintptr_t p,void* out,std::size_t size) noexcept {auto* r=region(p,size);if(!r)return false;std::memcpy(out,r->bytes.data()+(p-r->base),size);return true;}
bool write_bytes(std::uintptr_t p,const void* value,std::size_t size) noexcept {if(fail_write==p){fail_write=0;return false;}auto* r=region(p,size);if(!r)return false;std::memcpy(r->bytes.data()+(p-r->base),value,size);return true;}
bool guards() noexcept{return guards_ready;}
void apply_vision(){
    ++native_calls;
    if(consume_vision)check(thermal_scene_vision_override(),"IR override available only during native vision conversion");
    std::array<std::uint8_t,0x288> vision{};vision.fill(0x81);put(cg+0x8c164,vision);put(cg+0xa457c,0x40000000u);
    if(fail_native)RaiseException(0xe0000111,0,0,nullptr);
}
}
int main(){
    reset();const auto original=memory;
    check(!t::thermal_scene_vision_override(),"IR override absent outside native conversion");
    {t::ThermalSceneScope thermal;
        check(thermal.begin(cg+0x8c100,buffer,frame)&&thermal.active()&&thermal.actor_count()==1,"exact eligible actor and native vision admitted");
        check(native_calls==1&&!t::thermal_scene_vision_override(),"native vision called once and override immediately cleared");
        check(get<std::uint32_t>(buffer+d::gfx_offset+0x18)==0x400084u,"eligible actor receives native IR material bit");
        for(unsigned i=2;i<=6;++i)check(get<std::uint32_t>(buffer+d::gfx_offset+i*0x18)==(i==6?4u:0x400004u),"ineligible actors and mismatched identities retain flags");
        t::ThermalSceneScope nested;check(!nested.begin(cg+0x8c100,buffer,frame)&&nested.restore(),"nested thermal pass rejected without writes");
        check(thermal.restore()&&thermal.safe()&&memory==original,"thermal restore is byte-exact for actors vision and postbrightening");
        check(thermal.restore(),"thermal repeated restore harmless");}
    for(unsigned failure=0;failure<8;++failure){reset();
        switch(failure){case 0:put(buffer+d::frame_offset,frame+1);break;case 1:put(d::cg_slot,cg+4);break;
        case 2:put(d::scene_count,1025u);break;case 3:put(buffer+d::gfx_count_offset,257u);break;
        case 4:put(d::frontend_slot,buffer+d::buffer_stride);break;case 5:hooks_ready=false;break;
        case 6:put(0x251ca98,GetCurrentThreadId()+1);break;case 7:put(d::client_entities_slot,std::uintptr_t(0));break;}
        const auto before=memory;t::ThermalSceneScope thermal;
        check(!thermal.begin(cg+0x8c100,buffer,frame)&&thermal.restore()&&memory==before&&!native_calls,"foreign identities and out-of-bounds records fail before mutations");}
    for(unsigned failure=0;failure<3;++failure){reset();const auto before=memory;
        if(failure==0)fail_native=true;if(failure==1)consume_vision=false;if(failure==2)fail_write=buffer+d::gfx_offset+0x18;
        t::ThermalSceneScope thermal;
        check(!thermal.begin(cg+0x8c100,buffer,frame)&&thermal.restore()&&thermal.safe()&&memory==before&&!t::thermal_scene_vision_override(),"partial actor/native-vision failure restores full journal and clears override");}
    reset();{const auto before=memory;t::ThermalViewSlotScope slot;
        check(slot.begin(buffer,frame)&&slot.slot()==d::first_views+3*0x38e0&&!slot.built(),"native slot3 reservation retains distinct view address");
        std::array<std::uint32_t,4> counts{};check(d::count_values(buffer,&counts)&&counts==std::array<std::uint32_t,4>{3,3,1,3},"reservation changes only count boundaries");
        check(slot.restore()&&slot.safe()&&!slot.built()&&memory==before,"unconsumed reservation restores normal counts");}
    for(unsigned field=0;field<5;++field){reset();
        const std::array<std::uintptr_t,5> fields{d::rg_view_count,buffer+d::view_count_offset,buffer+d::view_index_offset,buffer+d::parms_count_offset,buffer+d::view_pointer_offset};
        put(fields[field],9u);const auto before=memory;t::ThermalViewSlotScope slot;
        check(!slot.begin(buffer,frame)&&slot.restore()&&memory==before,"unexpected native counts or view array refuse reservation without writes");}
    reset();{const auto before=memory;t::ThermalViewSlotScope slot;t::ThermalSceneScope thermal;
        check(slot.begin(buffer,frame)&&thermal.begin(cg+0x8c100,buffer,frame),"paired transaction begins");
        t::invoke_thermal_scene_native(&thermal,&slot,completed,nullptr);
        check(slot.built()&&slot.safe()&&thermal.safe()&&memory==before,"completed third view leaves normal actor vision and view ownership intact");}
    for(unsigned failure=0;failure<2;++failure){reset();const auto before=memory;t::ThermalViewSlotScope slot;t::ThermalSceneScope thermal;
        check(slot.begin(buffer,frame)&&thermal.begin(cg+0x8c100,buffer,frame),"exception transaction begins");bool caught=false;
        if(failure==0){try{t::invoke_thermal_scene_native(&thermal,&slot,cpp_failure,nullptr);}catch(int n){caught=n==7;}}
        else caught=catch_seh(&thermal,&slot);
        check(caught&&slot.safe()&&thermal.safe()&&memory==before&&!t::thermal_scene_vision_override(),"C++ and Windows exceptions restore both native journals");}
    for(unsigned count:{2u,3u}){reset();d::put_counts(buffer,count,count-1,count);const auto before=memory;
        check(t::invoke_thermal_backend_native(buffer,backend_completed,nullptr)&&memory==before,"thermal backend restores ordinary or auxiliary view ownership byte-exactly");}
    for(unsigned failure=0;failure<3;++failure){reset();const auto before=memory;bool caught=false;
        if(failure==0){try{t::invoke_thermal_backend_native(buffer,cpp_failure,nullptr);}catch(int n){caught=n==7;}}
        if(failure==1)caught=catch_backend_seh();
        if(failure==2){fail_write=buffer+d::view_index_offset;caught=!t::invoke_thermal_backend_native(buffer,backend_completed,nullptr);}
        check(caught&&memory==before&&!d::backend_owner,"backend C++/Windows exceptions and partial installation restore exact counts");}
    std::printf("%u thermal scene scope checks passed\n",checks);return 0;
}
