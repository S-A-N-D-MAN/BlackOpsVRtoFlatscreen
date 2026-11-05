// SPDX-License-Identifier: GPL-3.0-only
#include <windows.h>
#include <cstdint>
static ULONGLONG body_fixture_now=1000;
static ULONGLONG body_fixture_clock(){return body_fixture_now;}
#define GetTickCount64 body_fixture_clock
#include "../src/t5/shot_body_probe.cpp"
#undef GetTickCount64
#include <cstdio>
#include <vector>
#include <functional>
#include <limits>
namespace {
using namespace bovr::t5;
unsigned checks{},failures{},scope_reads{},rig_reads{},trace_calls{};
bool configured=true,scope_readable=true,rig_readable=true;
NativeConsumedCommand current_scope{};
bovr::input::CommandRigMatch current_rig{};
std::function<void(unsigned)> on_scope,on_rig;
void check(bool pass,const char* name){++checks;if(!pass){++failures;std::printf("FAIL %s\n",name);}}
template<class T>void put(std::uintptr_t address,T value){memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));}
struct Fixture {
    unsigned char* image{};std::uintptr_t base{},entity{},client{},server{},variant{},definition{};
    std::vector<unsigned char> client_bytes=std::vector<unsigned char>(0x2000),server_bytes=std::vector<unsigned char>(0x11000),definition_bytes=std::vector<unsigned char>(0x800);
    std::array<unsigned char,16> variant_bytes{};
    bovr::input::NativeCommandBytes source_command{};
    NativeBulletSelectionContext shot{};
    Fixture() {
        image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000000,MEM_RESERVE,PAGE_NOACCESS));if(!image)throw 1;
        base=reinterpret_cast<std::uintptr_t>(image);entity=base+0x16796f8;
        for(auto rva:{0x16796f8u,0x1803140u,0x7e19a8u,0x246d01cu,0x1804890u,0x2510160u,0x2511e2cu})
            if(!VirtualAlloc(image+(rva&~0xfffu),4096,MEM_COMMIT,PAGE_READWRITE))throw 2;
        client=reinterpret_cast<std::uintptr_t>(client_bytes.data());server=reinterpret_cast<std::uintptr_t>(server_bytes.data());
        variant=reinterpret_cast<std::uintptr_t>(variant_bytes.data());definition=reinterpret_cast<std::uintptr_t>(definition_bytes.data());reset();
    }
    ~Fixture(){VirtualFree(image,0,MEM_RELEASE);}
    void reset() {
        body_fixture_now=1000;configured=scope_readable=rig_readable=true;scope_reads=rig_reads=0;on_scope={};on_rig={};
        std::fill(client_bytes.begin(),client_bytes.end(),static_cast<unsigned char>(0));std::fill(server_bytes.begin(),server_bytes.end(),static_cast<unsigned char>(0));std::fill(definition_bytes.begin(),definition_bytes.end(),static_cast<unsigned char>(0));variant_bytes={};
        memset(reinterpret_cast<void*>(entity),0,0x300);source_command={};put(reinterpret_cast<std::uintptr_t>(source_command.data()),700);source_command[0x18]=4;
        put(base+0x1803140,client);put(base+0x246d01c,server);put(base+0x7e19a8+16,variant);put(variant+8,definition);
        put(entity+0x13c,client);put(entity+0x184,100);put<unsigned char>(entity+0xd4,4);
        put(server,4);memcpy(reinterpret_cast<void*>(server+0x10f38),source_command.data(),52);
        put(client+0x1c4,100);put(client+0x1a2c,2);put<unsigned char>(client+0x144,4);
        put(client+0x24,10.F);put(client+0x28,20.F);put(client+0x2c,30.F);put(client+0x190,64.F);put(client+0x184,90.F);
        put(base+0x2510160,0);put(base+0x2510164,10);put(base+0x2511e2c,0);
        put(base+0x246d014,1001);put(base+0x1804890,850);put(client+0x1d00,900);
        current_scope={};current_scope.image_base=base;current_scope.scope_serial=11;current_scope.observed_ms=990;
        current_scope.server_client_identity=server;current_scope.entity_identity=entity;current_scope.game_client_identity=client;
        current_scope.source_command_identity=reinterpret_cast<std::uintptr_t>(source_command.data());current_scope.native_thread=GetCurrentThreadId();
        current_scope.server_state=4;current_scope.command_time=700;current_scope.command=source_command;current_scope.readable=current_scope.local_identity=current_scope.active=true;
        current_rig={};current_rig.valid=true;current_rig.scope_serial=11;current_rig.ledger_revision=3;current_rig.server_thread=GetCurrentThreadId();
        auto& p=current_rig.produced;p.publication_serial=5;p.gameplay_epoch=7;p.observed_ms=980;p.native_thread=GetCurrentThreadId()+100;
        p.command_number=99;p.gameplay_active=true;p.bytes=source_command;p.rig.xr_generation=p.rig.anchor_generation=9;p.rig.published_ms=970;
        p.rig.frame.frame_id=123;p.rig.frame.actions.sequence=456;p.rig.frame.actions.focused=true;p.rig.frame.should_render=p.rig.frame.views_valid=true;
        p.rig.frame.head_center.position={0.1F,1.6F,0.3F};p.rig.tracking_anchor.position={0,1.6F,0};
        shot={};shot.eligible=true;auto& n=shot.native;n.readable=n.local_identity=n.normal_context=n.weapon_identity=n.finite=true;
        n.attacker_identity=entity;n.client_identity=client;n.variant_identity=variant;n.definition_identity=definition;n.weapon_index=4;
        n.observed_ms=995;n.native_thread=GetCurrentThreadId();n.game_time=900;n.server_health=100;
    }
    ShotBodyStatus read(NativeShotBody* out){return read_body_impl(base,shot,out);}
};
}
namespace bovr {void trace(const char*,const char*,...) noexcept {++trace_calls;}}
namespace bovr::t5 {
bool command_rig_enabled() noexcept{return configured;}
bool read_consumed_command_scope(NativeConsumedCommand* out) noexcept {++scope_reads;if(on_scope)on_scope(scope_reads);if(out)*out=current_scope;return scope_readable;}
bool read_current_command_rig(input::CommandRigMatch* out) noexcept {++rig_reads;if(on_rig)on_rig(rig_reads);if(out)*out=current_rig;return rig_readable;}
}
int main() {
    Fixture f;NativeShotBody out{};
    check(f.read(&out)==ShotBodyStatus::accepted&&out.valid,"actual copied native-memory fixture accepts current local command and body");
    check(scope_reads==2&&rig_reads==2,"both current consumed scope and original rig are rechecked");
    check(out.body_origin[0]==10&&out.body_origin[1]==20&&out.body_origin[2]==94&&out.yaw_degrees==90&&out.lean==0,"body is server origin plus current viewheight with copied yaw and zero lean");
    check(out.native_command_time==700&&out.shot_game_time==900&&out.current_level_time==850&&out.current_last_server_time==900,"command, antialag shot, and level clocks remain distinct");
    check(out.rig.produced.rig.published_ms==970&&out.observed_ms==1000&&out.rig.produced.rig.frame.frame_id==123&&out.rig.produced.rig.frame.head_center.position.x==0.1F,"fresh body observation keeps original complete rig and original publication time");
    check(out.rig.produced.native_thread!=out.native_thread&&out.rig.server_thread==out.native_thread,"server-native authority does not demand render/input thread identity");
    check(f.read(nullptr)==ShotBodyStatus::ineligible,"null typed output rejects");
    for(std::uint32_t count:{0u,1u,2u,4u,7u,8u,0x80000000u,0xffffffffu}) {f.reset();put(f.client+0x1b0,count);
        check((f.read(&out)==ShotBodyStatus::accepted)==(count<=7),"actual shot body admits retained valid native counts and rejects out-of-range values");}
    f.reset();
    const std::function<void()> native_mutations[]{
        [&]{put(f.server,3);},[&]{put(f.entity,1);},[&]{put<unsigned char>(f.client+0x130,1);},
        [&]{put<unsigned char>(f.entity+0xd4,5);},[&]{put<unsigned char>(f.client+0x144,5);},
        [&]{put(f.entity+0x184,0);},[&]{put(f.client+0x1c4,0);},[&]{put(f.client+4,9);},
        [&]{put(f.client+0x1a2c,1);},[&]{put(f.client+0x1a18,1);},[&]{put(f.entity+0x298,std::uintptr_t{99});},
        [&]{put(f.definition+0x1c,1);},[&]{put<unsigned char>(f.definition+0x56a,1);},
        [&]{put(f.client+0x158,18);},[&]{put(f.client+0x15c,18);},[&]{put(f.client+0x1b0,8);},
        [&]{put(f.client+0x1c0c,4u);},[&]{put(f.client+0xc,0x800u);},[&]{put(f.client+0x10,0x80u);},
        [&]{put(f.client+0xe0,0x100u);},[&]{put(f.client+0xe0,0x4000u);},[&]{put(f.client+0xe4,0x40000000u);},
        [&]{put(f.base+0x2510160,0x10);},[&]{put(f.base+0x2510164,8);},[&]{put(f.base+0x2511e2c,1);},
        [&]{put(f.client+0x7c,0.01F);},[&]{put(f.client+0x7c,std::numeric_limits<float>::quiet_NaN());},
        [&]{put(f.client+0x24,std::numeric_limits<float>::infinity());},[&]{put(f.client+0x190,std::numeric_limits<float>::infinity());},
        [&]{put(f.client+0x184,std::numeric_limits<float>::quiet_NaN());},
        [&]{put(f.client+0x2c,std::numeric_limits<float>::max());put(f.client+0x190,std::numeric_limits<float>::max());},
        [&]{put(f.base+0x1803140,std::uintptr_t{123});},[&]{put(f.base+0x246d01c,std::uintptr_t{123});},
        [&]{put(f.variant+8,std::uintptr_t{0});},[&]{put(f.base+0x7e19a8+16,std::uintptr_t{1});},
        [&]{put<unsigned char>(f.server+0x10f38+5,123);},[&]{f.source_command[6]=123;}
    };
    for(const auto& mutate:native_mutations) {
        f.reset();mutate();out.valid=true;out.rig.valid=true;
        check(f.read(&out)!=ShotBodyStatus::accepted&&!out.valid&&!out.rig.valid,"unsupported/changed/unreadable native state rejects and clears typed output");
    }
    const std::function<void()> identity_mutations[]{
        []{current_scope.native_thread++;},[]{current_scope.scope_serial=0;},[]{current_scope.mismatches=1;},
        []{current_scope.native_returned=true;},[]{current_scope.nesting_invalidated=true;},[]{current_scope.image_base++;},
        []{current_scope.command_time++;},[]{current_scope.command_time=-1;},[]{scope_readable=false;},[]{rig_readable=false;},
        []{current_rig.valid=false;},[]{current_rig.scope_serial++;},[]{current_rig.server_thread++;},[]{current_rig.ledger_revision=0;},
        []{current_rig.produced.bytes[5]++;},[]{current_rig.produced.gameplay_epoch=0;},[]{current_rig.produced.gameplay_active=false;},
        []{current_rig.produced.publication_serial=0;},[]{current_rig.produced.rig.anchor_generation++;},
        []{current_rig.produced.rig.frame.actions.focused=false;},[]{current_rig.produced.rig.published_ms=899;},
        []{current_rig.produced.rig.published_ms=1001;},[]{current_rig.produced.observed_ms=899;},
        []{current_scope.observed_ms=899;},[&]{f.shot.native.observed_ms=899;},[&]{f.shot.native.local_shot_confirmed=true;},
        [&]{f.shot.native.native_thread++;},[&]{f.shot.eligible=false;}
    };
    for(const auto& mutate:identity_mutations) {f.reset();mutate();check(f.read(&out)!=ShotBodyStatus::accepted&&!out.valid,"scope/rig/input identity and original age gate reject without refreshing");}
    const std::function<void()> between_copies[]{
        [&]{put(f.client+0x1b0,1u);},
        [&]{put(f.client+0x24,11.F);},[&]{put(f.client+0x190,65.F);},[&]{put(f.client+0x184,91.F);},
        [&]{put(f.base+0x2511e2c,1);},[&]{put(f.entity+0x184,99);},
        []{current_scope.scope_serial++;},[]{current_scope.nesting_invalidated=true;},[]{current_rig.ledger_revision++;},
        []{current_rig.produced.publication_serial++;},[]{current_rig.produced.gameplay_epoch++;},
        []{current_rig.produced.rig.xr_generation++;current_rig.produced.rig.anchor_generation++;},
        []{current_rig.produced.rig.frame.actions.sequence++;},[]{current_rig.produced.rig.published_ms++;},
        []{body_fixture_now=1101;},[]{body_fixture_now=999;}
    };
    for(const auto& mutate:between_copies) {f.reset();on_scope=[&](unsigned n){if(n==2)mutate();};check(f.read(&out)!=ShotBodyStatus::accepted&&!out.valid,"changes or staleness across protected copy/recheck reject");}
    f.reset();put(f.client+0x7c,-0.F);check(f.read(&out)==ShotBodyStatus::accepted&&out.lean==0,"finite negative-zero lean has native no-op semantics");
    f.reset();current_rig.produced.rig.published_ms=900;current_rig.produced.observed_ms=900;check(f.read(&out)==ShotBodyStatus::accepted,"original100ms boundary accepted without renewed tracking time");
    f.reset();const auto client_before=f.client_bytes,server_before=f.server_bytes,def_before=f.definition_bytes;
    check(f.read(&out)==ShotBodyStatus::accepted&&f.client_bytes==client_before&&f.server_bytes==server_before&&f.definition_bytes==def_before,"observer never mutates native player/server/weapon storage");
    f.reset();SetLastError(5555);out.valid=true;
    check(!read_native_shot_body(f.base,f.shot,&out)&&!out.valid&&GetLastError()==5555,"public reader rejects unpinned synthetic base and preserves LastError");
    configured=false;out.valid=true;check(!read_native_shot_body(0x400000,f.shot,&out)&&!out.valid,"disabled command-rig probe never reads native pointers");
    const auto traces=trace_calls;trace_native_shot_body(0x400000,f.shot);check(trace_calls==traces&&GetLastError()==5555,"disabled passive trace is silent and preserves LastError");
    std::printf("shot_body_checks=%u failures=%u native_game_calls=0 render_camera_reads=0 game_launches=0 native_writes=0\n",checks,failures);
    return failures?1:0;
}
