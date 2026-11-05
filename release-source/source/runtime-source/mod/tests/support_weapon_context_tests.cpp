// SPDX-License-Identifier: GPL-3.0-only
#include "t5/support_weapon_context.hpp"
#include "t5/support_script_aim.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace s=bovr::t5::support_weapon;
namespace xr=wawvr::xr;
namespace {
unsigned checks{},failures{};
void check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
struct Memory {
    std::map<std::uintptr_t,std::vector<unsigned char>> parts;
    unsigned name_reads{};bool drift{};
    static constexpr std::uintptr_t variant=0x20000,definition=0x30000,name=0x40000;
    template<class T>void put(std::uintptr_t at,const T& value){
        auto& bytes=parts[at];bytes.resize(sizeof value);std::memcpy(bytes.data(),&value,sizeof value);
    }
    Memory(){
        put(0xbe19a8+4*24,std::uint32_t(variant));put(variant,std::uint32_t(name));
        put(variant+8,std::uint32_t(definition));put(variant+0x20,1);
        put(definition+0x1c,2);put(definition+0x20,4);put(name,"rocket_barrage_sp");
    }
    static bool copy(void* context,std::uintptr_t at,void* out,std::size_t size) noexcept {
        auto& m=*static_cast<Memory*>(context);const auto found=m.parts.find(at);
        if(found==m.parts.end()||found->second.size()<size)return false;
        std::memcpy(out,found->second.data(),size);
        if(at==name&&++m.name_reads==2&&m.drift)static_cast<char*>(out)[0]='x';
        return true;
    }
    bool read(unsigned weapon=24){s::Descriptor d{};return s::read_descriptor(copy,this,weapon,&d);}
};
xr::Quaternionf yaw(float degrees){const float r=degrees*.00872664625997f;return {0,std::sin(r),0,std::cos(r)};}
struct Rig {
    bovr::input::CommandRigSample rig{};xr::EnginePose stock{{100,200,50},{}};
    Rig(){
        rig.xr_generation=rig.anchor_generation=7;rig.published_ms=990;
        auto& f=rig.frame;f.frame_id=23;f.predicted_display_time=100000;f.predicted_display_period=11111111;
        f.should_render=f.views_valid=f.actions.focused=true;f.actions.sequence=29;
        rig.tracking_anchor.position={0,1.6f,0};f.head_center=rig.tracking_anchor;
        for(auto& eye:f.eyes)eye.pose=f.head_center;
        auto& aim=f.actions.hands[1].aim;aim.active=aim.orientation_valid=aim.orientation_tracked=true;
    }
    bool angles(xr::Vec3f* out){return s::script_angles(rig,stock,1000,out);}
};
bool close(float a,float b){return std::fabs(a-b)<.001f;}
}
int main(){
    check(s::script_pre_current(1000,1109)&&s::script_pre_current(1000,1250),
          "pre-command context admits observed109ms delay and exact250ms boundary");
    check(!s::script_pre_current(1000,1251)&&!s::script_pre_current(1000,999)&&!s::script_pre_current(0,1000),
          "stale, future and uninitialized pre-command publications reject");
    for(const auto stage:{"pre_changed","owner_changed","stock_changed","scene_clocks"}) {
        check(s::script_read_retry(stage,1)&&s::script_read_retry(stage,2)&&!s::script_read_retry(stage,3),
              "concurrent frame transitions receive at most three fresh attempts");
    }
    for(const auto stage:{"owner_identity_changed","epoch_changed","pre_context","paired_ordinary_asset","rig_geometry","rig_generation"})
        check(!s::script_read_retry(stage,1),"identity, focus, tracking and context failures never retry");
    check(s::script_scene_current(1000,1000,1000),"same scene and command time accepted");
    check(s::script_scene_current(1016,1016,1000),"observed coherent frontend sixteen milliseconds ahead accepted");
    check(s::script_scene_current(1100,1100,1000),"forward scene progression accepts exact hundred millisecond boundary");
    check(!s::script_scene_current(1101,1101,1000),"scene over hundred milliseconds ahead rejected");
    check(!s::script_scene_current(999,999,1000),"future pre-command publication cannot bind an older scene");
    check(!s::script_scene_current(1016,1015,1000),"mismatched scene and refdef times rejected");
    check(!s::script_scene_current(0,0,0)&&!s::script_scene_current(1,1,0),"uninitialized scene or pre-command clocks rejected");
    Memory m;s::Descriptor d{};
    check(s::read_descriptor(Memory::copy,&m,24,&d)&&d.weapon==24&&d.variant==Memory::variant&&
          d.definition==Memory::definition&&d.name==Memory::name,"exact marker descriptor joins table, name, type, class and capacity");
    check(!m.read(0)&&!m.read(256)&&!m.read(23),"zero, oversized and unloaded indices cannot alias marker");
    m=Memory{};m.put(0xbe19a8+4*31,std::uint32_t(Memory::variant));
    check(m.read(31),"exact loaded asset can occupy a different native weapon index");
    for(const char* name:{"rocket_barrage","air_support","rocket_barrage_sp_alt","m72_law_sp"}){
        m=Memory{};std::array<char,32> text{};std::memcpy(text.data(),name,std::strlen(name));m.put(Memory::name,text);
        check(!m.read(),"near names and other launchers do not acquire support authority");
    }
    m=Memory{};m.put(Memory::definition+0x1c,0);check(!m.read(),"wrong weapon type rejected");
    m=Memory{};m.put(Memory::definition+0x20,0);check(!m.read(),"wrong weapon class rejected");
    m=Memory{};m.put(Memory::variant+0x20,2);check(!m.read(),"wrong capacity rejected");
    m=Memory{};m.put(Memory::variant+8,std::uint32_t(0xfffffff0));check(!m.read(),"overflowing definition rejected before offsets");
    m=Memory{};m.drift=true;check(!m.read(),"changing name between independent copies rejected");
    d.weapon=24;check(!s::read_descriptor(nullptr,nullptr,24,&d)&&d==s::Descriptor{},"failed read clears output");
    Rig f;xr::Vec3f a{};
    check(f.angles(&a)&&close(a.x,0)&&close(a.y,0),"neutral right-controller direction returns native forward");
    f.rig.frame.actions.hands[1].aim.pose.orientation=yaw(35);
    check(f.angles(&a)&&close(a.y,35),"right-controller yaw selects marker direction without grip");
    f.rig.frame.head_center.orientation=yaw(-60);
    check(f.angles(&a)&&close(a.y,35),"independent head yaw does not cancel controller pointing");
    f.rig.tracking_anchor.orientation=yaw(15);
    check(f.angles(&a)&&close(a.y,20),"frozen tracking anchor is removed exactly once");
    f=Rig{};f.rig.frame.actions.hands[0].aim.pose.orientation=yaw(-50);
    check(f.angles(&a)&&close(a.y,0),"left-controller orientation does not redirect marker");
    f=Rig{};f.rig.frame.actions.menu.active=f.rig.frame.actions.menu.current=true;
    check(!f.angles(&a),"menu action preserves native angles");
    f=Rig{};f.rig.frame.actions.focused=false;check(!f.angles(&a),"focus loss preserves native angles");
    f=Rig{};f.rig.published_ms=899;check(!f.angles(&a),"stale command rig preserves native angles");
    f=Rig{};f.rig.anchor_generation=6;check(!f.angles(&a),"generation mismatch preserves native angles");
    f=Rig{};f.rig.frame.actions.hands[1].aim.orientation_tracked=false;
    check(!f.angles(&a),"lost right-controller tracking preserves native angles");
    std::printf("support weapon context: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
