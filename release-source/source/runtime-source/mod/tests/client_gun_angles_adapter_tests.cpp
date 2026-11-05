// SPDX-License-Identifier: GPL-3.0-only
// Compile the real client-angle writer against isolated native-layout memory.
// Only expected code hashes use the existing synthetic-guard test convention.
#define BOVR_TEST_NATIVE_GUARDS
#include "loader/native_span_guard.hpp"
#include "t5/weapon_probe.hpp"
#include "t5/client_gun_angles.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
namespace {bool exact_enabled=true;unsigned reports{};}
namespace {
constexpr std::uintptr_t fixture_base=0x10000000;
std::uintptr_t fixture_address(std::uintptr_t at) {
    for(auto base:{0x480000u,0x2ff0000u,0x1a70000u,0x1c00000u,0x2910000u,0xbe0000u})
        if(at>=base&&at<base+65536)return fixture_base+at;
    return at;
}
}
namespace bovr {void trace(const char*,const char*,...) noexcept {++reports;}}
namespace bovr::t5 {
bool exact_profile_aim_enabled() noexcept {return exact_enabled;}
namespace {
namespace anim=animation_detail;
template<class T>T load(std::uintptr_t at) {T value{};std::memcpy(&value,reinterpret_cast<void*>(fixture_address(at)),sizeof(value));return value;}
}
#include "../src/t5/client_gun_angles_adapter.inc"
}
namespace {
using namespace bovr::t5;
unsigned checks{},failures{};
void check(bool okay,const char* label){++checks;if(!okay){++failures;std::printf("FAIL %s\n",label);}}
template<class T>void put(std::uintptr_t at,T value){std::memcpy(reinterpret_cast<void*>(fixture_address(at)),&value,sizeof(value));}
std::vector<unsigned char> cg,server;
std::array<unsigned char,16> variant{},definition{};
std::uintptr_t cg_address{},server_address{};
constexpr std::uintptr_t entity=0x1a796f8;
NativeWeaponContext context;
void setup(bool linked) {
    std::fill(cg.begin(),cg.end(),0);std::fill(server.begin(),server.end(),0);
    for(auto base:{0x480000u,0x2ff0000u,0x1a70000u,0x1c00000u,0x2910000u,0xbe0000u})std::memset(reinterpret_cast<void*>(fixture_base+base),0,65536);
    exact_enabled=true;context={};context.valid=true;context.local_client=0;context.native_thread=GetCurrentThreadId();
    context.cg_identity=cg_address;context.weapon_index=4;
    context.variant_identity=reinterpret_cast<std::uintptr_t>(variant.data());
    context.definition_identity=reinterpret_cast<std::uintptr_t>(definition.data());
    context.normal_context=context.server_normal=!linked;
    context.linked_weapon_context=context.linked_server_context=linked;
    context.server_link_identity=linked?0x6500000:0;
    const auto ps=cg_address+0x8a364;
    put(0x2ff5354,cg_address);put<unsigned char>(ps+0x144,4);
    put(cg_address+0xa9ce4,12.f);put(cg_address+0xa9ce8,34.f);
    put(ps+4,linked?1:0);put(ps+0x1c4,100);put(0x2910164,10);
    put(entity+0x13c,server_address);put(0x1c03140,server_address);put(entity+0x184,100);
    put<unsigned char>(entity+0xd4,4);put(entity+0x298,context.server_link_identity);
    put(server_address+4,linked?1:0);put(server_address+0x1c4,100);put(server_address+0x1a2c,2);
    put<unsigned char>(server_address+0x144,4);put(0xbe19a8+16,context.variant_identity);
    put(context.variant_identity+8,context.definition_identity);
}
void refuses(const char* label) {
    const auto before_cg=cg,before_server=server;const auto before_reports=reports;
    check(!bovr::t5::write_client_barrel_angles(context,{0,1,0})&&cg==before_cg&&server==before_server&&reports==before_reports,label);
}
void accepted(const char* label) {
    auto expected=cg;const float angles[2]{0.f,90.f};std::memcpy(expected.data()+0xa9ce4,angles,sizeof(angles));
    const auto before_server=server;const auto before_reports=reports;
    check(bovr::t5::write_client_barrel_angles(context,{0,1,0}),label);
    float selected[2]{};std::memcpy(selected,cg.data()+0xa9ce4,sizeof(selected));
    // Ignore signed zero only; every unrelated byte must remain unchanged.
    std::memcpy(expected.data()+0xa9ce4,selected,sizeof(selected));
    check(selected[0]==0.f&&selected[1]==90.f&&cg==expected&&server==before_server&&reports==before_reports+1,
        "accepted writer changes exactly client pitch/yaw and never server or unrelated CG bytes");
}
}
int main() {
    for(auto base:{0x480000u,0x2ff0000u,0x1a70000u,0x1c00000u,0x2910000u,0xbe0000u})
        if(VirtualAlloc(reinterpret_cast<void*>(fixture_base+base),65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)!=reinterpret_cast<void*>(fixture_base+base)) {
            std::printf("FAIL isolated fixed memory allocation at%x\n",base);return 1;
        }
    cg.resize(0xd0000);server.resize(0x1e00);
    cg_address=reinterpret_cast<std::uintptr_t>(cg.data());server_address=reinterpret_cast<std::uintptr_t>(server.data());
    setup(false);accepted("ordinary paired primary preserves existing client barrel effect update");
    setup(true);accepted("real writer now updates effects for exact paired linked primary");
    setup(true);put<unsigned>(cg_address+0x8a364+0x10,0x100);put<unsigned>(server_address+0x10,0x100);
    accepted("offhand-disable after breach does not disable linked primary effect angles");
    setup(true);put(cg_address+0x8a360,2);accepted("native first-person shadow camera remains eligible");

    for(unsigned fault=0;fault<12;++fault) {
        setup(true);
        switch(fault) {
        case 0:context.linked_server_context=false;break;
        case 1:context.server_normal=true;break;
        case 2:context.normal_context=true;break;
        case 3:context.server_link_identity=0;break;
        case 4:context.server_link_identity=0x6500001;break;
        case 5:context.native_thread+=1;break;
        case 6:context.local_client=1;break;
        case 7:context.valid=false;break;
        case 8:context.cg_identity+=4;break;
        case 9:exact_enabled=false;break;
        case 10:context.variant_identity+=4;break;
        case 11:context.definition_identity+=4;break;
        }
        refuses("invalid or mismatched copied linked ownership rejects without writes");
    }
    for(unsigned fault=0;fault<23;++fault) {
        setup(true);const auto ps=cg_address+0x8a364;
        switch(fault) {
        case 0:put(entity+0x298,0u);break;
        case 1:put(entity+0x298,0x6500010u);break;
        case 2:put(0x1c03140,server_address+4);break;
        case 3:put(entity,1);break;
        case 4:put<unsigned char>(server_address+0x130,1);break;
        case 5:put<unsigned char>(ps+0x130,1);break;
        case 6:put<unsigned char>(server_address+0x144,5);break;
        case 7:put<unsigned char>(entity+0xd4,5);break;
        case 8:put<unsigned char>(ps+0x144,5);break;
        case 9:put(ps+4,0);break;
        case 10:put(server_address+4,0);break;
        case 11:put<unsigned>(ps+0xc,0x400);break;
        case 12:put<unsigned>(server_address+0xc,0x800);break;
        case 13:put<unsigned>(ps+0x10,0x80);break;
        case 14:put<unsigned>(server_address+0x10,0x80);break;
        case 15:put(cg_address+0x8a360,1);break;
        case 16:put(0x2910160,1);break;
        case 17:put(0x2911e2c,1);break;
        case 18:put(entity+0x184,0);break;
        case 19:put(server_address+0x1a2c,1);break;
        case 20:put<std::uintptr_t>(entity+0x13c,1);put<std::uintptr_t>(0x1c03140,1);break;
        case 21:put(0x2ff5354,cg_address+4);break;
        case 22:put<unsigned>(server_address+0x1c0c,0x10);break;
        }
        refuses("changed live linked player, weapon, mode, camera or link rejects without writes");
    }
    setup(false);context.server_normal=false;refuses("ordinary unpaired server context cannot authorize client effects");
    setup(false);put<unsigned char>(0x4816d8,1);refuses("native pitch code guard still rejects mismatch");
    setup(true);put<unsigned char>(0x4816e8,1);refuses("native yaw code guard still rejects mismatch");
    setup(true);put(cg_address+0xa9ce4,std::numeric_limits<float>::quiet_NaN());refuses("nonfinite existing client angles remain refused");
    setup(true);const auto before=cg;
    check(!bovr::t5::write_client_barrel_angles(context,{0,0,0})&&cg==before,"invalid forward vector cannot mutate client angles");
    check(!bovr::t5::write_client_barrel_angles(context,{0,0,1})&&cg==before,"vertical pole keeps native fallback unchanged");
    std::printf("client_barrel_adapter_checks=%u failures=%u native_game_calls=0 server_writes=0\n",checks,failures);
    return failures?1:0;
}
