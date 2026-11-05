// SPDX-License-Identifier: GPL-3.0-only
// Actual input callback and paired retail-owner reader over owned memory.
#define main legacy_strela_aim_main
#define manual_reload_uses_offhand strela_test_reload_busy
#define manual_grenade_consumes_trigger strela_test_grenade_busy
#define ensure_strela_aim strela_test_ensure_aim
#include "native_command_aim_adapter_tests.cpp"
#undef ensure_strela_aim
#undef manual_grenade_consumes_trigger
#undef manual_reload_uses_offhand
#undef main

namespace {
bool reload_busy{},grenade_busy{};
unsigned installs{},owner_reads{},owner_fault{};
Fixture* owner_fixture{};
bool copy_owner_memory(void*,std::uintptr_t address,void* out,std::size_t size) noexcept {
    if(!owner_fixture||!out||!t5::strela::bounded(address,size))return false;
    // Only pinned globals are relocated; all pointees are actual owned storage.
    bool global=(address>=0xbe19a8&&address<0xbe1da8)||
        (address>=0x1a796f8&&address<0x1a79a44);
    for(const auto value:{0x251ca98u,0x251cac0u,0x2ff5354u,0x1c03140u,
        0x2910160u,0x2910164u,0x2911e2cu,0xbcd150u})global|=address==value;
    if(global)address=owner_fixture->aim.base+(address-0x400000);
    __try {memcpy(out,reinterpret_cast<const void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool copied_owner(std::uintptr_t,const t5::PreCommandSnapshot& pre,std::uint64_t now,
                  t5::strela::Owner* out) noexcept {
    ++owner_reads;
    if(owner_fault==1)return false;
    if(!t5::strela::copy_owner({nullptr,copy_owner_memory},0,GetCurrentThreadId(),pre,now,out))return false;
    if(owner_reads==2&&owner_fault==2)++out->weapon.variant;
    if(owner_reads==1&&owner_fault==3)++t5::pre_snapshot.gameplay_epoch;
    if(owner_reads==1&&owner_fault==4)generation_current=false;
    return true;
}
struct StrelaFixture {
    Fixture native{true,true};
    unsigned char* server{};
    StrelaFixture() {
        owner_fixture=&native;owner_fault=owner_reads=installs=0;reload_busy=grenade_busy=false;
        native.aim.exact_profile=true;
        native.controls.cod4_controls=true;native.controls.router=input::ActionRouter(false,true);
        native.controls.copy_strela_owner=copied_owner;
        server=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        check(server!=nullptr,"owned server player allocation");
        const auto base=native.aim.base,sv=reinterpret_cast<std::uintptr_t>(server);
        store(base+0x211ca98,GetCurrentThreadId());store(base+0x211cac0,GetCurrentThreadId());
        store(base+0x16796f8+0x13c,sv);store(base+0x1803140,sv);
        store(base+0x16796f8+0x184,100);store(sv+0x1c4,100);store(sv+0x1a2c,2);
        store(sv+0x144,static_cast<unsigned char>(4));store(sv,5000);
        store(reinterpret_cast<std::uintptr_t>(native.cg+0x8a364),5000);
        store(reinterpret_cast<std::uintptr_t>(native.def+0x1c),2);
        store(reinterpret_cast<std::uintptr_t>(native.def+0x20),6);
        store(reinterpret_cast<std::uintptr_t>(native.variant+0x20),1);
        store(reinterpret_cast<std::uintptr_t>(native.def+0x474),65);
        store(reinterpret_cast<std::uintptr_t>(native.def+0x478),1500);
        store(reinterpret_cast<std::uintptr_t>(native.def+0x47c),static_cast<unsigned char>(1));
        store(reinterpret_cast<std::uintptr_t>(native.variant),reinterpret_cast<std::uintptr_t>(native.dvar+0x900));
        store(reinterpret_cast<std::uintptr_t>(native.variant+0xc),reinterpret_cast<std::uintptr_t>(native.dvar+0x950));
        std::memcpy(native.dvar+0x900,"strela_sp",sizeof("strela_sp"));
        std::memcpy(native.dvar+0x950,"WEAPON_STRELA",sizeof("WEAPON_STRELA"));
        for(auto& h:actions.hands) {
            h.grip.active=h.grip.orientation_valid=h.grip.position_valid=true;
            h.grip.orientation_tracked=h.grip.position_tracked=true;
            h.trigger.active=h.trigger_click.active=h.squeeze.active=h.stick.active=true;
            h.primary.active=h.secondary.active=true;
        }
        actions.menu.active=true;
        // Right hand behind the eye, launcher still supported along its barrel.
        actions.hands[1].grip.pose.position={.10f,-.05f,.04f};
        actions.hands[0].grip.pose.position={.10f,-.05f,-.35f};
    }
    ~StrelaFixture(){owner_fixture=nullptr;if(server)VirtualFree(server,0,MEM_RELEASE);}
    void step(bool join=true) {
        native.next();aim_now+=16;++actions.sequence;
        t5::pre_snapshot.observed_ms=aim_now;
        if(join)rig.frame.actions=actions;
        rig.published_ms=aim_now;
        owner_reads=0;t5::before_native(&native.controls,native.context());
    }
    void support(){actions.hands[0].squeeze.current=1;step();}
    unsigned count(const char* prefix) const {
        return unsigned(std::count_if(dispatches.begin(),dispatches.end(),
            [&](const auto& s){return s.starts_with(prefix);}));
    }
    bool ads()const{return count("+speed_throw ")>count("-speed_throw ");}
};
}
namespace bovr::t5 {
bool strela_test_reload_busy() noexcept {return reload_busy;}
bool strela_test_grenade_busy() noexcept {return grenade_busy;}
bool strela_test_ensure_aim() noexcept {++installs;return true;}
}
int main() {
    {
        StrelaFixture f;f.step();
        check(installs==1&&!f.ads(),"exact paired Strela installs aim on live entry before grip is held");
        f.support();
        check(!f.native.controls.shouldered&&f.ads(),"supported launcher behind eye gets native ADS without rifle distance gate");
        actions.hands[0].squeeze.current=0;f.step();
        check(!f.ads(),"support grip release balances native ADS");
        f.support();reload_busy=true;f.step();
        check(!f.ads(),"manual reload offhand ownership releases native ADS");
        reload_busy=false;f.step();check(f.ads(),"completed reload restores supported launcher ADS through normal router");
        grenade_busy=true;f.step();check(!f.ads(),"grenade offhand ownership releases launcher ADS");
    }
    for(unsigned mode=0;mode<19;++mode) {
        StrelaFixture f;f.step();f.support();check(f.ads(),"transition starts from held launcher ADS");
        const auto before=installs;
        bool join=true;
        switch(mode) {
        case 0:actions.hands[0].grip.position_tracked=false;break;
        case 1:actions.hands[1].aim.orientation_tracked=false;break;
        case 2:actions.focused=false;break;
        case 3:generation_current=false;break;
        case 4:rig_live=false;break;
        case 5:rig.frame.views_valid=false;break;
        case 6:rig.frame.predicted_display_time=0;break;
        case 7:t5::pre_snapshot.context.key_catcher=16;break;
        case 8:actions.menu.current=true;break;
        case 9:owner_fault=1;break;
        case 10:owner_fault=2;break;
        case 11:owner_fault=3;break;
        case 12:owner_fault=4;break;
        case 13:store(reinterpret_cast<std::uintptr_t>(f.server+0x144),static_cast<unsigned char>(5));break;
        case 14:store(reinterpret_cast<std::uintptr_t>(f.native.def+0x47c),static_cast<unsigned char>(0));break;
        case 15:store(reinterpret_cast<std::uintptr_t>(f.server+0x10),0x80);break;
        case 16:std::memcpy(f.native.dvar+0x900,"rpg_sp",sizeof("rpg_sp"));break;
        case 17:rig.frame.actions=actions;join=false;break;
        case 18:rig.anchor_generation=2;break;
        }
        f.step(join);
        check(!f.ads(),"tracking, generation, read identity, pause, switching and metadata rejection release ADS");
        if(mode!=8)check(installs==before,"rejected launcher state cannot install targeting hook");
    }
    {
        StrelaFixture f;f.step();
        // Aim 60 degrees away from head gaze, with a valid supported barrel.
        actions.hands[1].aim.pose.orientation={0,.5f,0,.8660254f};
        actions.hands[0].grip.pose.position={-.238f,-.05f,-.155f};
        f.support();check(!f.native.controls.shouldered&&f.ads(),"Strela ADS follows valid supported launcher away from head gaze");
    }
    {
        StrelaFixture f;std::memcpy(f.native.dvar+0x900,"m16_sp",sizeof("m16_sp"));
        store(reinterpret_cast<std::uintptr_t>(f.native.def+0x1c),0);
        store(reinterpret_cast<std::uintptr_t>(f.native.def+0x20),1);
        f.step();f.support();check(!f.ads()&&!installs,"ordinary rifle retains shoulder-distance rejection");
        actions.hands[1].grip.pose.position={.05f,-.05f,-.20f};
        actions.hands[0].grip.pose.position={.05f,-.05f,-.50f};f.step();
        check(f.native.controls.shouldered&&f.ads()&&!installs,"ordinary rifle keeps normal shouldered ADS and no Strela installation");
    }
    {
        StrelaFixture f;std::memcpy(f.native.dvar+0x900,"flashpoint_strela_sp",sizeof("flashpoint_strela_sp"));
        f.step();f.support();check(f.ads(),"exact Flashpoint Strela uses same supported launcher ADS");
        auto bytes=f.native.bytes();const auto before=bytes;
        t5::after_native(&f.native.controls,f.native.context(),bytes.data());
        check(*reinterpret_cast<float*>(f.native.cg+0x8a364+0x168)==0.f&&
            *reinterpret_cast<unsigned*>(f.native.cg+0x8a364+0x4e8)==0&&
            *reinterpret_cast<float*>(f.server+0x168)==0.f&&*reinterpret_cast<unsigned*>(f.server+0x4e8)==0,
            "native ADS fraction and target lock flags remain untouched on both roles");
        (void)before;
    }
    std::printf("strela_input_callback_checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
