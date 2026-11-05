// SPDX-License-Identifier: GPL-3.0-only
#include "t5/motorcycle_shot.hpp"
#include "t5/motorcycle_context.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "loader/trace.hpp"
#include <atomic>
namespace bovr::t5 {
namespace {
struct Owner {
    std::uintptr_t client{},cg{},vehicle{},script{},info{},variant{},definition{};
    int vehicle_index{},server_time{},client_time{};
    float yaw{};
    std::array<unsigned char,68> parameters{};
    friend bool operator==(const Owner&,const Owner&)=default;
};
bool owner(std::uintptr_t base,const std::uint32_t* args,Owner* out) noexcept {
    if(base!=0x400000||!args||!out||args[0]!=0x1a796f8||args[3]!=args[0]||!args[2])return false;
    __try {
        const auto u=[](std::uintptr_t p){return *reinterpret_cast<const std::uint32_t*>(p);};
        const auto i=[](std::uintptr_t p){return *reinterpret_cast<const int*>(p);};
        Owner o{};o.client=u(args[0]+0x13c);o.cg=u(0x2ff5354);
        if(o.client<0x10000||o.client!=u(0x1c03140)||o.cg<0x10000)return false;
        o.server_time=i(0x1c04890);o.client_time=i(o.cg+0x8a348);
        if(o.server_time<=0||o.client_time<=0||std::abs(o.server_time-o.client_time)>250)return false;
        const auto ps=o.cg+0x8a364;
        for(auto p:{o.client,ps}) {
            if(i(p+4)!=1||i(p+0x1c4)<=0||(u(p+0xc)&0xc00)||!(u(p+0xe0)&0x4000)||
               (u(p+0xe0)&0x10300)||(u(p+0xe4)&0x50040000)||i(p+0x440)||i(p+0x448)||i(p+0x44c)!=1||
               *reinterpret_cast<const unsigned char*>(p+0x130)||*reinterpret_cast<const unsigned char*>(p+0x144))return false;
        }
        o.vehicle_index=*reinterpret_cast<const short*>(o.client+0x444);
        if(o.vehicle_index<0||o.vehicle_index>1021||o.vehicle_index!=*reinterpret_cast<const short*>(ps+0x444))return false;
        o.vehicle=0x1a796f8+o.vehicle_index*0x34c;
        if(i(o.vehicle)!=o.vehicle_index)return false;
        o.script=u(o.vehicle+0x148);if(o.script<0x10000)return false;
        const int index=*reinterpret_cast<const short*>(o.script+0x1ec);if(index<0||index>=32)return false;
        o.info=0x1cc1480+index*0x1dc8;
        if(memcmp(reinterpret_cast<const void*>(o.info),"motorcycle_player",sizeof("motorcycle_player")))return false;
        o.yaw=*reinterpret_cast<const float*>(o.vehicle+0x12c);if(!std::isfinite(o.yaw))return false;
        memcpy(o.parameters.data(),reinterpret_cast<const void*>(args[2]),68);
        memcpy(&o.variant,o.parameters.data()+0x3c,4);memcpy(&o.definition,o.parameters.data()+0x40,4);
        if(o.variant<0x10000||o.definition<0x10000||u(o.variant+8)!=o.definition||i(o.definition+0x1c))return false;
        const auto name=u(o.variant);if(name<0x10000||memcmp(reinterpret_cast<const void*>(name),
            "vorkuta_motorcycle_shotgun_sp",sizeof("vorkuta_motorcycle_shotgun_sp")))return false;
        *out=o;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool commit_endpoint(std::uintptr_t wp,const NativeBulletGeometry& g) noexcept {
    ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);
    // The guarded MagicBullet caller owns WP and its endpoint in this stack.
    if(wp<low+36||wp>high-68)return false;
    float end[3];for(unsigned i=0;i<3;++i)end[i]=g.muzzle[i]+g.forward[i]*8192.f;
    __try {memcpy(reinterpret_cast<void*>(wp-36),end,12);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
bool select_motorcycle_script_shot(std::uintptr_t base,const std::uint32_t* args,unsigned char* scratch) noexcept {
    Owner first{},second{};PreCommandSnapshot pre{};
    if(!scratch||!owner(base,args,&first)||!read_pre_command_snapshot(&pre)||
       !pre.native_gameplay_fields_valid||pre.native_cg_identity!=first.cg||pre.context.key_catcher||
       pre.context.connection_state!=10)return false;
    wawvr::xr::ActionSnapshot actions{};std::uint64_t generation{};input::CommandRigSample rig{};
    if(!read_vr_command_input_snapshot(&actions,&generation,&rig)||generation!=rig.xr_generation)return false;
    NativeBulletGeometry original{},selected{};memcpy(&original,first.parameters.data(),sizeof(original));
    wchar_t handed[8]{};GetEnvironmentVariableW(L"BOVR_LEFT_HANDED",handed,8);
    const auto hand=handed[0]==L'1'?wawvr::xr::Hand::Left:wawvr::xr::Hand::Right;
    if(!motorcycle_shot::geometry(rig,actions,first.yaw,hand,GetTickCount64(),original,&selected)||
       !command_input_generation_current(rig)||!owner(base,args,&second)||first!=second||
       !commit_endpoint(args[2],selected))return false;
    memcpy(scratch,first.parameters.data(),68);memcpy(scratch,&selected,sizeof(selected));
    // MagicBullet's gunForward is initialized and used by its tracer event.
    memcpy(scratch+48,selected.forward,12);
    static std::atomic<unsigned> count{};const auto n=++count;
    trace("t5_motorcycle_script_shot","count=%u vehicle=%d xr_sequence=%llu native_time=%d forward=%.6f,%.6f,%.6f original=%.6f,%.6f,%.6f native_calls_added=0",
        n,first.vehicle_index,actions.sequence,first.server_time,selected.forward[0],selected.forward[1],selected.forward[2],
        original.forward[0],original.forward[1],original.forward[2]);
    return true;
}
}
