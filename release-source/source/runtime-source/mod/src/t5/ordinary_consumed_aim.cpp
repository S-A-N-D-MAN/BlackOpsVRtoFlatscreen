// SPDX-License-Identifier: GPL-3.0-only
#include "t5/ordinary_consumed_aim.hpp"
#include "t5/dual_pistol_effects.hpp"
#include "t5/native_command_aim_adapter.hpp"
#include "t5/native_weapon_tracking_state.hpp"
#include "t5/primary_weapon_tracking.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#include <atomic>
namespace bovr::t5::ordinary_consumed_aim {
namespace {
std::atomic<bool> ready{};
template<class T>T load(std::uintptr_t p) noexcept {T v{};std::memcpy(&v,reinterpret_cast<void*>(p),sizeof v);return v;}
struct Owner {
    std::uintptr_t cg{},client{},server{},variant{},definition{};
    unsigned weapon{},use_count{},network_use_count{};int birth_time{},command_time{},level_time{};
    primary_weapon_tracking::Player predicted{},authoritative{};
    primary_weapon_tracking::View view{};primary_weapon_tracking::Server state{};
    Angles angles{};
    friend bool operator==(const Owner&,const Owner&)=default;
};
struct Pending {Owner owner{};NativeConsumedCommand native{};Angles selected{};std::uint64_t publication{};unsigned possible_writes{};bool active{};};
thread_local Pending pending;
bool copy(const NativeConsumedCommand& n,Owner* out) noexcept {
    if(!out||n.image_base!=0x400000||!n.active||!n.readable||!n.local_identity||n.mismatches||
       n.native_returned||n.nesting_invalidated||n.local_client_index||n.server_state!=4||
       !n.scope_serial||n.native_thread!=GetCurrentThreadId()||n.entity_identity!=0x1a796f8)return false;
    __try {
        Owner o{};const auto i=[](std::uintptr_t p){return load<int>(p);};const auto u=[](std::uintptr_t p){return load<unsigned>(p);};
        const auto b=[](std::uintptr_t p){return load<unsigned char>(p);};
        o.cg=u(0x2ff5354);o.client=u(n.entity_identity+0x13c);o.server=u(0x286d01c);
        if(o.cg<0x10000||o.client<0x10000||(o.client&3)||o.server<0x10000||o.client!=n.game_client_identity||
           o.server!=n.server_client_identity||o.client!=u(0x1c03140)||i(o.server)!=4||
           i(n.entity_identity)||b(o.client+0x130)||std::memcmp(n.command.data(),reinterpret_cast<void*>(o.server+0x10f38),52)||
           !n.source_command_identity||std::memcmp(n.command.data(),reinterpret_cast<void*>(n.source_command_identity),52))return false;
        const auto ps=o.cg+0x8a364;o.weapon=b(o.client+0x144);
        if(!o.weapon||o.weapon!=n.command[0x18]||o.weapon!=b(ps+0x144)||o.weapon!=b(n.entity_identity+0xd4)||b(ps+0x130))return false;
        o.variant=u(0xbe19a8+4*o.weapon);if(o.variant<0x10000)return false;
        o.definition=u(o.variant+8);if(o.definition<0x10000||
            !dual_pistol_effects::ordinary_weapon(i(o.definition+0x1c),i(o.definition+0x20),i(o.definition+0x30),b(o.definition+0x56a)))return false;
        const auto player=[&](std::uintptr_t p){return primary_weapon_tracking::Player{i(p+4),i(p+0x1c4),u(p+0xc),u(p+0x10),u(p+0xe0),u(p+0xe4)};};
        o.predicted=player(ps);o.authoritative=player(o.client);
        o.view={i(o.cg+0x8a360),i(0x2910160),i(0x2910164),i(0x2911e2c)!=0,i(o.cg+0xc)!=0,i(o.cg+0xa9d44)!=0};
        o.state={o.authoritative,true,i(n.entity_identity+0x184),i(o.client+0x1a2c),i(o.client+0x1a18),u(o.client+0x1c0c),u(n.entity_identity+0x298)};
        if(primary_weapon_tracking::paired_mode(primary_weapon_tracking::client_mode(o.predicted,o.view),
            primary_weapon_tracking::server_mode(o.state))!=primary_weapon_tracking::Mode::ordinary)return false;
        for(auto p:{ps,o.client})if(!native_weapon_tracking_state(i(p+0x158))||!native_weapon_tracking_state(i(p+0x15c)))return false;
        // Native G_GetPlayerMuzzlePoint must select its gun-angle branch.
        const auto force_view=u(0xbcd150);if(force_view<0x10000||i(force_view+0x18)||(u(o.client+0xe0)&0x4000))return false;
        // The pinned inactivity function returns 1 unconditionally when this
        // dvar is disabled. Enabled inactivity may exit before the native stores.
        const auto inactivity=u(0x3023b00);if(inactivity<0x10000||i(inactivity+0x18))return false;
        o.use_count=u(n.entity_identity+0x330);o.network_use_count=u(n.entity_identity+0x74);o.birth_time=i(n.entity_identity+0x340);
        o.command_time=i(o.client);o.level_time=i(0x1c04890);
        // ClientThink_real clamps to level.time[-1000,+200], then rejects a
        // nonadvancing command. Do not preload a command that takes either path.
        const auto time=static_cast<std::int64_t>(n.command_time),level=static_cast<std::int64_t>(o.level_time);
        if(i(reinterpret_cast<std::uintptr_t>(n.command.data()))!=n.command_time||time<=o.command_time||time<level-1000||time>level+200)return false;
        o.angles=load<Angles>(o.client+0x1c34);if(!finite(o.angles))return false;
        *out=o;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
LONG bits(float value) noexcept {LONG result{};std::memcpy(&result,&value,4);return result;}
bool write_if_current() noexcept {
    // Each native field is 4-byte aligned, not 8-byte aligned. The transaction
    // exists before writing; each indivisible successful store earns ownership.
    __try {for(unsigned j=0;j<2;++j){
        const auto before=j?pending.owner.angles.yaw:pending.owner.angles.pitch;
        const auto selected=j?pending.selected.yaw:pending.selected.pitch;
        if(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(pending.owner.client+0x1c34+4*j),bits(selected),bits(before))!=bits(before))return false;
        pending.possible_writes|=1u<<j;
    }return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void finish(const NativeConsumedCommand& n) noexcept {
    if(!pending.active||pending.native.scope_serial!=n.scope_serial)return;
    const auto p=pending;pending={};unsigned outcome=unsigned(Completion::owner_retired),restored{};Angles current{};bool same{};
    __try {
        const auto e=n.entity_identity,c=p.owner.client;
        same=e==0x1a796f8&&load<unsigned>(e+0x13c)==c&&load<unsigned>(0x1c03140)==c&&
            load<unsigned>(0x286d01c)==p.owner.server&&load<unsigned>(e+0x330)==p.owner.use_count&&
            load<unsigned>(e+0x74)==p.owner.network_use_count&&load<int>(e+0x340)==p.owner.birth_time;
        if(same){current=load<Angles>(c+0x1c34);outcome=unsigned(Completion::native_result);
            // Every normal return belongs to native code, including death,
            // switching or a same-bit native store. Never roll it back.
            // Abnormal/nested cancellation is best-effort field ownership: a
            // changed native field wins; equal bits cannot prove write provenance.
            if(!n.native_returned)for(unsigned j=0;j<2;++j)if(p.possible_writes&(1u<<j)){
                const auto selected=j?p.selected.yaw:p.selected.pitch,before=j?p.owner.angles.yaw:p.owner.angles.pitch;
                if(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(c+0x1c34+4*j),bits(before),bits(selected))==bits(selected))restored|=1u<<j;
            }
            if(restored)outcome=unsigned(Completion::restore_original);
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){}
    static std::atomic<unsigned> counts[3]{};const auto count=++counts[outcome];
    if(count<=16||count%120==0)trace("t5_ordinary_consumed_aim_complete",
        "count=%u outcome=%u scope=%llu publication=%llu command_time=%d returned=%u restored_mask=%u owner_same=%u observed=%.6f,%.6f selected=%.6f,%.6f native_calls_added=0",
        count,outcome,n.scope_serial,p.publication,n.command_time,n.native_returned,restored,same,current.pitch,current.yaw,p.selected.pitch,p.selected.yaw);
}
}
bool prepare(std::uintptr_t base) noexcept {
    // ClientEvents0069d77d executes BEFORE the native command-angle stores at
    //0069db53/68. Only those same two data fields are made available earlier.
    return base==0x400000&&
        native_guard::matches(base,{0x270b70,0x178,"cf3fdf439409b4099b5e195fde5bbe7aa2d59d6127783f32e2333dffbbdea544"})&&
        native_guard::matches(base,{0x29d450,0x790,"172cf99736c98243a8a4c23ef0ace185ac4b43ea95b7b131e88971f5fa5b5d14"})&&
        native_guard::matches(base,{0x176470,0x34,"1954e99cf94417c16c6db4229aebbccba14ade6f2d927b307c44a2e78461f3ea"})&&
        native_guard::matches(base,{0x5cdec4,4,"aae182a9fe4b98eb7c50714e2a2750cc86fb80c6a86bca033c2aeb6dce282047"});
}
void enable() noexcept {ready.store(true,std::memory_order_release);}
void before(const NativeConsumedCommand& n) noexcept {
    if(!ready.load(std::memory_order_acquire)||pending.active||
       dual_pistol_effects::ordinary_mode()==dual_pistol_effects::OrdinaryMode::disabled)return;
    const auto error=GetLastError();Owner first{},second{};CommandAimPublication aim{},again{};unsigned status=1,attempted{};
    if(copy(n,&first)&&read_native_command_aim_for_shot(n.command,first.cg,&aim,true)&&
       !aim.dual_wield&&!aim.dual_weapon&&aim.weapon==first.weapon&&aim.variant_identity==first.variant&&
       aim.definition_identity==first.definition&&copy(n,&second)&&first==second&&
       read_native_command_aim_for_shot(n.command,first.cg,&again,true)&&
       again.command.publication_serial==aim.command.publication_serial&&again.invalidation_revision==aim.invalidation_revision){
        std::uint32_t packed{};std::memcpy(&packed,n.command.data()+0x20,4);const auto selected=decode(packed);
        status=2;
        if(finite(selected)){
            const bool correct=dual_pistol_effects::ordinary_mode()==dual_pistol_effects::OrdinaryMode::correct;
            const bool changed=correct&&std::memcmp(&selected,&first.angles,sizeof selected);
            if(changed)pending={first,n,selected,aim.command.publication_serial,0,true};
            const bool written=!changed||write_if_current();attempted=pending.possible_writes;
            if(written){
                status=0;
                static std::atomic<unsigned> accepted{};const auto count=++accepted;
                if(count<=16||count%120==0)trace("t5_ordinary_consumed_aim",
                    "count=%u scope=%llu publication=%llu command_time=%d weapon=%u packed=0x%08x before=%.6f,%.6f selected=%.6f,%.6f writes=%u observe=%u native_calls_added=0 shot_call_sites_changed=0",
                    count,n.scope_serial,aim.command.publication_serial,n.command_time,first.weapon,packed,
                    first.angles.pitch,first.angles.yaw,selected.pitch,selected.yaw,changed?8u:0u,!correct);
            }else finish(n);
        }
    }
    if(status){static std::atomic<unsigned> rejected[3]{};const auto count=++rejected[status];
        if(count<=8||count%240==0)trace("t5_ordinary_consumed_aim_rejected","status=%u count=%u scope=%llu command_time=%d attempted_field_mask=%u",status,count,n.scope_serial,n.command_time,attempted);}
    SetLastError(error);
}
void after(const NativeConsumedCommand& n) noexcept {const auto error=GetLastError();finish(n);SetLastError(error);}
void cancel_for_nested() noexcept {const auto error=GetLastError();if(pending.active){auto n=pending.native;n.native_returned=false;finish(n);}SetLastError(error);}
}
