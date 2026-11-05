// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/native_stack_fire_count.hpp"
#include "t5/manual_reload_adapter.hpp"
#include "t5/manual_reload_flag_policy.hpp"
#include "t5/crossbow_policy.hpp"
#include "input/manual_reload.hpp"
#include "input/weapon_reload_mechanism.hpp"
#include "input/predicted_shot_feedback.hpp"
#include "input/paired_reload.hpp"
#include "t5/manual_reload_native_guards.hpp"
#include "t5/manual_reload_transfer.hpp"
#include "t5/weapon_command_profile.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>

namespace bovr::t5 {
namespace {
using U32=std::uint32_t;
namespace m=input::manual;
namespace paired=input::paired_reload;

bool configured{},left_handed{},cod4_controls{};
SRWLOCK gate=SRWLOCK_INIT;
input::manual::Controller controller;
input::PredictedShotFeedback reload_fire_events;
std::uint64_t reload_fire_serial{1}; // Nonzero disables the core's clip-only fallback.
struct ReloadFireLatch {std::uint64_t generation{},epoch{},player{};unsigned weapon{};bool pending{};} reload_fire_latch;
paired::Transaction paired_transaction;
struct Ammo {
    std::uintptr_t ps{},variant{},definition{};unsigned weapon{};int clip{},reserve{},capacity{},state{},time{};U32 flags{},stack_count{};
    int reload_add{},start_add{};unsigned minimum_missing{1};bool fuel_exchange{};
    m::ReloadMechanism mechanism{};std::array<char,128> name{};
    std::array<int,8> mechanism_fields{};
    unsigned primary_weapon{},partner_weapon{},hand{},clip_index{},pool_index{};bool paired{},no_partial{};
    primary_weapon_tracking::Mode mode{};U32 link{};
};
struct Role {std::uintptr_t ps{};int clip{},reserve{},command_time{};bool consumed{};};
struct Publication {
    m::Output interaction{};
    std::uintptr_t cg{},server{},variant{},definition{};
    DWORD input_thread{};int input_command{};
    std::uint64_t epoch{},generation{},updated{},transaction{};
    int commit_time{};std::uint64_t commit_started{};Role roles[2]{};
    Ammo command_ammo{},committed_ammo{},command_partner{},committed_partner{};
    std::uint64_t committed_fire_serial{};
    std::uint64_t request_started{},input_pre_sequence{};
    bool gate_active{},committed{},commit_claimed{},authoritative_pending{},pair_failed{},commit_failed{};
} publication;
// Only a completed physical insertion waiting for native admission is timed.
// Slow belt/receiver/charging gestures have no timer. A claimed native call is
// never replayed, even when its result cannot be verified.
constexpr std::uint64_t commit_wait_ms=2000;
bool cancel_failed_request(const Publication& p,std::uint64_t now) noexcept {
    return p.pair_failed||p.commit_failed||(!p.committed&&p.interaction.request_commit&&p.request_started&&
        (now<p.request_started||now-p.request_started>commit_wait_ms));
}
#ifdef BOVR_TEST_MANUAL_RELOAD_ADAPTER
struct ReloadTestHost {
    bool (*synchronize)(){};
    bool (*roles)(const Publication&,std::uintptr_t,unsigned*,Ammo*){};
    bool (*transfer)(std::uintptr_t,Ammo*){};
} reload_test_host;
#endif
template<class T>T read(std::uintptr_t p) {T v{};std::memcpy(&v,reinterpret_cast<const void*>(p),sizeof(v));return v;}
bool extended_guards() noexcept {
    // Pinned retail checks: boltAction is 054F, not the unrelated alt-hide
    // flag at 0576. The complete transfer guard owns 0584/0588 and fuel 0061.



    __try {return !bovr::native_guard::compare(reinterpret_cast<const void*>(0x765d86),7,"682f5276cacf064c0bc1efb1e3097a541a6f663612b3a1529921c68cd08c533a")&&
        !bovr::native_guard::compare(reinterpret_cast<const void*>(0x768cdf),7,"93bde6d3ce6666a025c632c5479c091a293472b36502410b0b91615229f28c80")&&
        !bovr::native_guard::compare(reinterpret_cast<const void*>(0x61f4bc), 53, "028fb64619cce09e3946195a6cfe0d4b6afe2ea79b8b563e58df51e65b1a24e3")&&
        !bovr::native_guard::compare(reinterpret_cast<const void*>(0x40f2e0), 33, "32e9c4bd6feda1f82c5eb908ea68dab9472c8773ceecc9b6377374bad7a48b48")&&
        !bovr::native_guard::compare(reinterpret_cast<const void*>(0x6395a0), 23, "a1c034fbc5b9ecf8fbf0b45d06ae752b9605cc8ebbdc78198b3788a03acca7c6")&&
        !bovr::native_guard::compare(reinterpret_cast<const void*>(0x769d44), 9, "fcf968ecee363e89c392d7f305ceefea5537f0424f0187d27b9f9cf4d93a4a7f");}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool native_name(std::uintptr_t at,std::array<char,128>* out) noexcept {
    if(!out||at<0x10000||at>UINT32_MAX-128)return false;
    __try {for(unsigned i=0;i<out->size();++i){const auto ch=read<unsigned char>(at+i);
        if(!ch)return i>0;if(ch<0x20||ch>0x7e)return false;(*out)[i]=static_cast<char>(ch);}return false;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
reload_transfer::AmmoValues values(const Ammo& a) noexcept {
    return {a.clip,a.reserve,a.capacity,a.reload_add,a.state,a.minimum_missing,a.fuel_exchange};
}
bool same_definition(const Ammo& a,const Ammo& b) noexcept {
    return a.weapon==b.weapon&&a.variant==b.variant&&a.definition==b.definition&&a.capacity==b.capacity&&
        a.reload_add==b.reload_add&&a.start_add==b.start_add&&a.minimum_missing==b.minimum_missing&&
        a.fuel_exchange==b.fuel_exchange&&a.name==b.name&&a.mechanism_fields==b.mechanism_fields&&
        a.primary_weapon==b.primary_weapon&&a.partner_weapon==b.partner_weapon&&a.hand==b.hand&&a.paired==b.paired&&
        a.clip_index==b.clip_index&&a.pool_index==b.pool_index&&a.mode==b.mode&&a.link==b.link&&a.stack_count==b.stack_count;
}
bool ammo(std::uintptr_t ps,Ammo* out,unsigned hand=0) noexcept {
    if(!out||hand>1)return false;*out={};
    __try {
        if(ps<0x10000||ps>UINT32_MAX-0x500||read<unsigned char>(ps+0x130)||(read<int>(ps+4)!=0&&read<int>(ps+4)!=1)||read<int>(ps+0x1c4)<=0||
           read<unsigned char>(ps+0x61)||(read<U32>(ps+0xc)&0x800)||(read<U32>(ps+0x10)&0x80)||
           (read<U32>(ps+0xe0)&0x4300)||(read<U32>(ps+0xe4)&0x50040000))return false;
        Ammo a{};a.ps=ps;a.hand=hand;a.primary_weapon=read<unsigned char>(ps+0x144);if(!a.primary_weapon)return false;
        const auto primary_variant=read<U32>(0xbe19a8+4*a.primary_weapon);
        if(primary_variant<0x10000)return false;const auto primary_definition=read<U32>(primary_variant+8);
        if(primary_definition<0x10000)return false;const auto pair=read<unsigned char>(primary_definition+0x56a);
        if(pair>1||(!pair&&hand))return false;a.paired=pair!=0;
        if(pair){a.partner_weapon=read<U32>(primary_definition+0x594);
            if(!a.partner_weapon||a.partner_weapon>=256||a.partner_weapon==a.primary_weapon)return false;}
        a.weapon=hand?a.partner_weapon:a.primary_weapon;
        a.variant=read<U32>(0xbe19a8+4*a.weapon);if(a.variant<0x10000)return false;
        a.definition=read<U32>(a.variant+8);if(a.definition<0x10000)return false;
        a.capacity=read<int>(a.variant+0x20);
        if(a.capacity<1||a.capacity>static_cast<int>(m::max_reload_capacity)||!native_name(read<U32>(a.variant),&a.name))return false;
        m::NativeWeaponReloadValues v{};v.weapon_type=read<int>(a.definition+0x1c);
        v.weapon_class=read<int>(a.definition+0x20);v.fire_type=read<int>(a.definition+0x30);
        if(!crossbow::reload_weapon_flags(read<U32>(ps+0x10),
            crossbow::weapon(v.weapon_type,v.weapon_class,a.capacity,a.name.data())))return false;
        const auto segmented=read<unsigned char>(a.definition+0x582),bolt=read<unsigned char>(a.definition+0x54f),
            partial=read<unsigned char>(a.definition+0x581),dual=read<unsigned char>(a.definition+0x56a),
            fuel=read<unsigned char>(a.definition+0x61);
        if(segmented>1||bolt>1||partial>1||dual>1||fuel>1)return false;
        a.mechanism_fields={v.weapon_type,v.weapon_class,v.fire_type,segmented,bolt,partial,dual,fuel};
        v.capacity=a.capacity;v.reload_add=a.reload_add=read<int>(a.definition+0x584);
        v.start_add=a.start_add=read<int>(a.definition+0x588);v.segmented=segmented!=0;v.bolt_action=bolt!=0;
        v.no_partial=a.no_partial=partial!=0;v.dual=dual!=0;v.fuel_tank=fuel!=0;v.internal_name=a.name.data();
        // The paired bridge below validates and commits the two native clips
        // separately; this classifier supplies only each hand's ammo policy.
        if(a.paired)v.dual=false;
        auto classified=m::classify_native_reload(v);
        if(classified.status==m::ReloadClassificationStatus::fuel_tank_requires_native_exchange){
            // The guarded native helper discards the old tank, fills from
            // reserve, and resets its fuel clock. This is an explicit exchange,
            // separate from round-conserving magazine and shell transactions.
            v.fuel_tank=false;classified=m::classify_native_reload(v);
            if(classified.status!=m::ReloadClassificationStatus::supported||a.reload_add)return false;
            a.fuel_exchange=true;classified.mechanism={true,m::ReloadKind::magazine,0,false,false};
            classified.minimum_missing_rounds=1;
        }
        if(classified.status!=m::ReloadClassificationStatus::supported)return false;
        namespace tracking=primary_weapon_tracking;
        a.mode=tracking::Mode::ordinary;
        if(read<int>(ps+4)==1){
            const auto cg=read<U32>(0x2ff5354),server=read<U32>(0x1a796f8+0x13c);
            if(cg<0x10000||server<0x10000||server!=read<U32>(0x1c03140)||
               (ps!=cg+0x8a364&&ps!=server)||read<int>(0x1a796f8))return false;
            const auto player=[](std::uintptr_t p){return tracking::Player{read<int>(p+4),read<int>(p+0x1c4),
                read<U32>(p+0xc),read<U32>(p+0x10),read<U32>(p+0xe0),read<U32>(p+0xe4)};};
            const auto client=tracking::client_mode(player(cg+0x8a364),{read<int>(cg+0x8a360),read<int>(0x2910160),
                read<int>(0x2910164),read<int>(0x2911e2c)!=0,read<int>(cg+0xc)!=0,read<int>(cg+0xa9d44)!=0});
            a.link=read<U32>(0x1a796f8+0x298);
            if(!crossbow::reload_context(client,{player(server),true,read<int>(0x1a796f8+0x184),
                read<int>(server+0x1a2c),read<int>(server+0x1a18),read<U32>(server+0x1c0c),a.link},
                crossbow::weapon(v.weapon_type,v.weapon_class,a.capacity,a.name.data())))return false;
            a.mode=tracking::Mode::linked;
        }
        a.mechanism=classified.mechanism;a.minimum_missing=classified.minimum_missing_rounds;
        if(a.paired){if(a.fuel_exchange)return false;
            a.mechanism={true,m::ReloadKind::magazine,0,a.mechanism.charge_on_empty,false};}
        const auto multiplier=read<U32>(0xbcaf84);if(multiplier<0x10000||read<float>(multiplier+0x18)!=1.f)return false;
        const auto pool=read<U32>(a.variant+0x44),clip=read<U32>(a.variant+0x4c);
        a.pool_index=pool;a.clip_index=clip;
        bool found_clip=false,found_pool=false;
        for(unsigned i=0;i<15;++i){
            if(read<U32>(ps+0x3bc+8*i)==clip){if(found_clip)return false;found_clip=true;a.clip=read<int>(ps+0x3c0+8*i);}
            if(read<U32>(ps+0x344+8*i)==pool){if(found_pool)return false;found_pool=true;a.reserve=read<int>(ps+0x348+8*i);}
        }
        if(!found_clip||!found_pool||a.clip<0||a.clip>a.capacity||a.reserve<0||a.reserve>100000)return false;
        a.state=read<int>(ps+(hand?0x15c:0x158));a.time=read<int>(ps);a.flags=read<U32>(ps+0x10);a.stack_count=read<U32>(ps+0x1b0);
        if(a.state<0||a.state>=18||(read<int>(ps+0x15c)<0||read<int>(ps+0x15c)>=18)||!native_valid_stack_fire_count(a.stack_count))return false;
        if(read<U32>(ps+0x1b0)!=a.stack_count||read<unsigned char>(ps+0x144)!=a.primary_weapon||read<U32>(0xbe19a8+4*a.weapon)!=a.variant||
           read<U32>(a.variant+8)!=a.definition||read<int>(a.variant+0x20)!=a.capacity||
           read<U32>(0xbe19a8+4*a.primary_weapon)!=primary_variant||read<U32>(primary_variant+8)!=primary_definition||
           (a.paired&&read<U32>(primary_definition+0x594)!=a.partner_weapon)||
           read<int>(ps+4)!=(a.mode==primary_weapon_tracking::Mode::linked?1:0)||
           (a.mode==primary_weapon_tracking::Mode::linked&&read<U32>(0x1a796f8+0x298)!=a.link))return false;
        *out=a;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool live_roles(const Publication& p,std::uintptr_t ps,unsigned* role,Ammo* out) noexcept {
#ifdef BOVR_TEST_MANUAL_RELOAD_ADAPTER
    return reload_test_host.roles&&reload_test_host.roles(p,ps,role,out);
#else
    __try {
        if(!p.gate_active||read<U32>(0x2ff5354)!=p.cg||read<U32>(0x1a796f8+0x13c)!=p.server||
           read<U32>(0x1c03140)!=p.server||read<int>(0x2910160)||read<int>(0x2910164)<9||read<int>(0x2911e2c))return false;
        if(ps==p.cg+0x8a364)*role=0;else if(ps==p.server)*role=1;else return false;
        return ammo(ps,out)&&same_definition(*out,p.command_ammo);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
#endif
}

constexpr U32 disable_reload=0x02000000;
bool synchronized_server() noexcept {
#ifdef BOVR_TEST_MANUAL_RELOAD_ADAPTER
    return reload_test_host.synchronize&&reload_test_host.synchronize();
#else
    // The stock command dispatcher (829CD9..829CEC) calls SV_WaitServer
    // before running commands that change server state. Use that same barrier
    // on the native main thread; never race the independently running server.
    __try {
        if(read<DWORD>(0x251ca98)!=GetCurrentThreadId())return false;
        reinterpret_cast<void(__cdecl*)()>(0x640c50)();
        return read<int>(0x2899d4c)==0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
#endif
}
paired::Slot pair_slot(const Ammo& a) noexcept {
    return {a.weapon,a.clip_index,a.pool_index,a.variant,a.definition,a.clip,a.reserve,a.capacity,
        a.reload_add,a.start_add,a.state,a.no_partial,a.fuel_exchange};
}
bool pair_values(const Ammo& primary,const Ammo& partner,const paired::Key& key,paired::Snapshot* out) noexcept {
    if(!out||!primary.paired||!partner.paired||primary.hand||partner.hand!=1||primary.ps!=partner.ps||
       primary.primary_weapon!=partner.primary_weapon||primary.partner_weapon!=partner.weapon||
       primary.clip_index==partner.clip_index||primary.capacity+partner.capacity>static_cast<int>(m::max_reload_capacity)||
       (primary.pool_index==partner.pool_index&&primary.reserve!=partner.reserve))return false;
    *out={key,{pair_slot(primary),pair_slot(partner)}};
    return paired::valid(out->hands[0])&&paired::valid(out->hands[1]);
}
bool copy_pair(std::uintptr_t ps,const paired::Key& key,paired::Snapshot* out,Ammo* primary,Ammo* partner) noexcept {
    return ammo(ps,primary)&&ammo(ps,partner,1)&&pair_values(*primary,*partner,key,out);
}
std::uint64_t observe_reload_fire(const PreCommandSnapshot& pre,const input::CommandRigSample& rig,const Ammo& a,bool live) noexcept {
    input::PredictedFireSample sample{};const auto now=GetTickCount64();
    sample.generation=rig.xr_generation;sample.epoch=pre.gameplay_epoch;sample.occurrence=pre.sequence;
    sample.observed_ms=pre.observed_ms;sample.published_ms=rig.published_ms;sample.native_time=pre.native_game_time;
    if(live){
        if(reload_fire_latch.generation!=rig.xr_generation||reload_fire_latch.epoch!=pre.gameplay_epoch||
           reload_fire_latch.player!=a.ps||reload_fire_latch.weapon!=a.weapon)
            reload_fire_latch={rig.xr_generation,pre.gameplay_epoch,a.ps,a.weapon,false};
        __try {
            const auto ps=pre.native_cg_identity+0x8a364;
            std::array<unsigned char,0x24> before{},after{};
            if(ps==a.ps&&read<U32>(0x2ff5354)==pre.native_cg_identity){
                std::memcpy(before.data(),reinterpret_cast<const void*>(ps+0xe8),before.size());
                sample.sequence=read<unsigned short>(ps+0xe8);
                std::memcpy(sample.events.data(),before.data()+4,sizeof(sample.events));
                sample.player=ps;sample.weapon=a.weapon;
                std::memcpy(after.data(),reinterpret_cast<const void*>(ps+0xe8),after.size());
                sample.valid=before==after&&read<unsigned short>(ps+0xe8)==sample.sequence&&
                    read<unsigned char>(ps+0x144)==a.weapon&&read<U32>(0x2ff5354)==pre.native_cg_identity;
            }
        }__except(EXCEPTION_EXECUTE_HANDLER){sample.valid=false;}
    }
    const auto result=reload_fire_events.observe(sample,now);
    if(result.shots&&reload_fire_serial<=UINT64_MAX-result.shots)reload_fire_serial+=result.shots;
    if(result.shots&&a.mechanism.cycle_after_shot)reload_fire_latch.pending=true;
    return reload_fire_serial;
}
struct FlagOwner {std::uintptr_t cg{},ps{};int game_time{};bool owned{};};
FlagOwner flag_owners[2]{}; // Existing native input owner only.
bool current_flag_storage(const FlagOwner& f,unsigned role) noexcept {
    __try {
        if(!f.owned||!f.cg||read<U32>(0x2ff5354)!=f.cg||read<int>(f.cg+0x8a348)<f.game_time)return false;
        return role==0?f.ps==f.cg+0x8a364:
            (f.ps==read<U32>(0x1a796f8+0x13c)&&f.ps==read<U32>(0x1c03140));
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void retire_reload_flags() noexcept {
    if((flag_owners[0].owned||flag_owners[1].owned)&&!synchronized_server())return;
    for(unsigned role=0;role<2;++role){auto& f=flag_owners[role];
        if(current_flag_storage(f,role)){
            __try {InterlockedAnd(reinterpret_cast<volatile LONG*>(f.ps+0x10),static_cast<LONG>(~disable_reload));}
            __except(EXCEPTION_EXECUTE_HANDLER){}
        }
        f={};
    }
}
bool own_reload_flags(const command_profile::Copy& exact,int game_time) noexcept {
    const std::uintptr_t ps[2]{exact.identity.cg+0x8a364,exact.identity.server};
    for(unsigned role=0;role<2;++role)if(flag_owners[role].owned&&
        (flag_owners[role].cg!=exact.identity.cg||flag_owners[role].ps!=ps[role])){retire_reload_flags();break;}
    Ammo states[2]{};
    bool needs_write=false;
    for(unsigned role=0;role<2;++role){
        if(!ammo(ps[role],&states[role])||states[role].weapon!=exact.profile.weapon||
           states[role].variant!=exact.identity.variant||states[role].definition!=exact.identity.definition||
           static_cast<U32>(states[role].mode)!=exact.identity.tracking_mode||states[role].link!=exact.identity.server_link)return false;
        if(states[role].paired){Ammo partner{};paired::Snapshot pair{};
            if(!ammo(ps[role],&partner,1)||!pair_values(states[role],partner,{ps[role],1,1,1},&pair))return false;}
        const auto& f=flag_owners[role];
        const bool owned=f.ps==ps[role]&&f.cg==exact.identity.cg&&current_flag_storage(f,role);
        const auto decision=manual_reload_flag_policy::decide((states[role].flags&disable_reload)!=0,owned);
        needs_write=needs_write||decision.write_disable;
        // A restored bit permits deliberate physical reloads, but its origin
        // is unknown. Do not acquire ownership or later clear that bit.
        if(!decision.own_after)flag_owners[role]={};
    }
    if(!needs_write)return true;
    if(!synchronized_server())return false;
    __try {
        // Revalidate both roles before changing either authoritative flag.
        for(unsigned role=0;role<2;++role){
            if(!ammo(ps[role],&states[role])||states[role].weapon!=exact.profile.weapon||
               states[role].variant!=exact.identity.variant||states[role].definition!=exact.identity.definition||
           static_cast<U32>(states[role].mode)!=exact.identity.tracking_mode||states[role].link!=exact.identity.server_link)return false;
            if(states[role].paired){Ammo partner{};paired::Snapshot pair{};
                if(!ammo(ps[role],&partner,1)||!pair_values(states[role],partner,{ps[role],1,1,1},&pair))return false;}
        }
        for(unsigned role=0;role<2;++role){
            auto& f=flag_owners[role];
            const bool owned=f.ps==ps[role]&&f.cg==exact.identity.cg&&current_flag_storage(f,role);
            auto decision=manual_reload_flag_policy::decide((states[role].flags&disable_reload)!=0,owned);
            bool acquired=false;
            if(decision.write_disable){
                const auto before=InterlockedOr(reinterpret_cast<volatile LONG*>(ps[role]+0x10),disable_reload);
                acquired=(before&disable_reload)==0;
                decision=manual_reload_flag_policy::decide((before&disable_reload)!=0,owned);
            }
            // A write needed by one role cannot adopt the other role's bit.
            if(decision.own_after){if(acquired)f={exact.identity.cg,ps[role],game_time,true};}
            else f={};
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool transfer_ammo(std::uintptr_t ps,Ammo* after) noexcept {
#ifdef BOVR_TEST_MANUAL_RELOAD_ADAPTER
    return reload_test_host.transfer&&reload_test_host.transfer(ps,after);
#else
    // PM_ReloadClip is shared native inventory/event logic and has a cdecl ps
    // argument. Execute only after the native server synchronization barrier
    // on the current main-thread input owner, with local identity rechecked.
    using ReloadClip=void(__cdecl*)(void*);
    __try {reinterpret_cast<ReloadClip>(0x518030)(reinterpret_cast<void*>(ps));}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    return ammo(ps,after);
#endif
}
bool transfer_selected_hand(std::uintptr_t ps,unsigned hand,bool* restored) noexcept {
    if(!restored||hand>1)return false;*restored=false;bool returned=false;
    // The caller has retired the server worker and validated both complete
    // current clip identities. The native movement loop uses this same byte.
    __try {
        const auto original=read<unsigned char>(ps+0x61);if(original)return false;
        __try {
            *reinterpret_cast<volatile unsigned char*>(ps+0x61)=static_cast<unsigned char>(hand);
            reinterpret_cast<void(__cdecl*)(void*)>(0x518030)(reinterpret_cast<void*>(ps));returned=true;
        }__finally {
            *reinterpret_cast<volatile unsigned char*>(ps+0x61)=original;
            *restored=read<unsigned char>(ps+0x61)==original;
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){returned=false;}
    return returned;
}
void commit_paired_on_server_owner(const Publication& p,int command_time,std::uint64_t now) noexcept {
    const paired::Key key{p.server,p.epoch,p.generation,p.transaction};paired::Snapshot current{};
    Ammo primary{},partner{};
    if(!copy_pair(p.server,key,&current,&primary,&partner)||!same_definition(primary,p.command_ammo)||
       !same_definition(partner,p.command_partner)||!(primary.flags&disable_reload))return;
    const auto proposed=paired::plan(current);
    if(!proposed.allowed||proposed.total_rounds!=p.interaction.commit_rounds)return;
    AcquireSRWLockExclusive(&gate);
    const bool owned=publication.gate_active&&publication.epoch==p.epoch&&publication.generation==p.generation&&
        publication.transaction==p.transaction&&publication.input_command==p.input_command&&!publication.commit_claimed&&
        publication.interaction.request_commit&&!publication.commit_failed&&!publication.pair_failed;
    if(owned)publication.commit_claimed=true;
    ReleaseSRWLockExclusive(&gate);
    if(!owned)return;
    const bool began=paired_transaction.begin(current,p.interaction.commit_rounds);
    bool copied=began;
    if(began)for(unsigned step=0;step<2&&paired_transaction.stage()==paired::Stage::ready;++step){
        Ammo live_primary{};unsigned role{};
        if(!live_roles(p,p.server,&role,&live_primary)||role!=1){paired_transaction.cancel();copied=false;break;}
        const auto request=paired_transaction.claim_next(current);if(!request.allowed)break;
        bool restored=false;const bool returned=transfer_selected_hand(p.server,request.hand,&restored);
        paired::Snapshot after{};Ammo next_primary{},next_partner{};
        copied=restored&&live_roles(p,p.server,&role,&live_primary)&&role==1&&copy_pair(p.server,key,&after,&next_primary,&next_partner)&&
            same_definition(next_primary,p.command_ammo)&&same_definition(next_partner,p.command_partner);
        const bool acknowledged=paired_transaction.acknowledge(request,after,returned&&restored&&copied);
        trace("t5_manual_reload_pair_step","transaction=%llu hand=%u expected_rounds=%u returned=%u selector_restored=%u copied=%u acknowledged=%u completed_mask=%u attempted_mask=%u",
            p.transaction,request.hand,request.rounds,returned,restored,copied,acknowledged,paired_transaction.completed_mask(),paired_transaction.attempted_mask());
        if(copied){current=after;primary=next_primary;partner=next_partner;}
    }
    const bool completed=began&&copied&&paired_transaction.stage()==paired::Stage::complete;
    trace("t5_manual_reload_pair_commit","transaction=%llu primary=%u partner=%u rounds=%u completed=%u outcome=%u completed_mask=%u attempted_mask=%u clips=%d,%d reserves=%d,%d command_time=%d server_synchronized=1 instruction_patches=0",
        p.transaction,p.command_ammo.weapon,p.command_partner.weapon,paired_transaction.completed_rounds(),completed,
        static_cast<unsigned>(paired_transaction.outcome()),paired_transaction.completed_mask(),paired_transaction.attempted_mask(),
        primary.clip,partner.clip,primary.reserve,partner.reserve,command_time);
    AcquireSRWLockExclusive(&gate);
    if(publication.epoch==p.epoch&&publication.transaction==p.transaction){
        if(completed){publication.committed=true;publication.committed_ammo=primary;publication.committed_partner=partner;
            publication.authoritative_pending=true;publication.commit_time=command_time;publication.commit_started=now;
            publication.committed_fire_serial=reload_fire_serial;}
        else publication.pair_failed=true; // Cancel the gesture; never replay a claimed step.
    }
    ReleaseSRWLockExclusive(&gate);
}
void commit_on_server_owner(const Publication& p,int command_time) noexcept {
    if(!p.interaction.request_commit||!p.transaction||p.commit_claimed||p.input_thread!=GetCurrentThreadId()||
       !synchronized_server())return;
    const auto now=GetTickCount64();PreCommandSnapshot pre{};
    if(now<p.updated||now-p.updated>100||!read_pre_command_snapshot(&pre)||!primary_weapon_active(pre)||
       pre.gameplay_epoch!=p.epoch||pre.context.command_number!=p.input_command)return;
    Ammo before{};unsigned role{};
    if(!live_roles(p,p.server,&role,&before)||role!=1||!(before.flags&disable_reload))return;
    if(before.paired){commit_paired_on_server_owner(p,command_time,now);return;}
    const auto plan=reload_transfer::plan(values(before),p.interaction.commit_rounds);
    if(!plan.allowed)return;
    AcquireSRWLockExclusive(&gate);
    const bool current=publication.gate_active&&publication.epoch==p.epoch&&publication.transaction==p.transaction&&
        publication.input_command==p.input_command&&!publication.commit_claimed&&publication.interaction.request_commit&&
        !publication.commit_failed&&!publication.pair_failed;
    if(current)publication.commit_claimed=true;
    ReleaseSRWLockExclusive(&gate);
    if(!current)return;
    // Prediction receives the authoritative inventory through the game's own
    // snapshot path. The stock barrier has retired the concurrent server frame.
    Ammo after{};const bool copied=transfer_ammo(p.server,&after);
    const bool completed=copied&&same_definition(before,after)&&reload_transfer::completed(plan,values(before),values(after));
    trace("t5_manual_reload_commit","transaction=%llu role=1 ps=%p command_time=%d weapon=%u before=%d/%d after=%d/%d conserved=%u native_animation_started=0 instruction_patches=0 server_synchronized=1 thread=%lu expected_rounds=%u completed=%u fuel_exchange=%u discarded_fuel=%u name=%s",
        p.transaction,reinterpret_cast<void*>(p.server),command_time,before.weapon,before.clip,before.reserve,after.clip,after.reserve,
        completed&&!plan.fuel_exchange,GetCurrentThreadId(),plan.rounds,completed,plan.fuel_exchange,plan.discarded,before.name.data());
    AcquireSRWLockExclusive(&gate);
    if(publication.epoch==p.epoch&&publication.generation==p.generation&&publication.transaction==p.transaction&&
       publication.input_command==p.input_command){
        if(completed){publication.committed=true;
            publication.committed_ammo=after;publication.authoritative_pending=true;publication.commit_time=command_time;
            publication.commit_started=now;publication.committed_fire_serial=reload_fire_serial;}
        else publication.commit_failed=true; // Unknown native outcome: cancel, never replay this claim.
    }
    ReleaseSRWLockExclusive(&gate);
}

}
bool manual_reload_enabled() noexcept{return configured;}
bool manual_reload_uses_offhand() noexcept {
    if(!configured)return false;AcquireSRWLockShared(&gate);const bool active=publication.gate_active&&m::uses_offhand(publication.interaction);ReleaseSRWLockShared(&gate);return active;
}
bool cancel_manual_reload_for_selection(const CommandContext& context) noexcept {
    if(!configured)return true;
    if(!context.valid||context.local_client||context.native_thread!=GetCurrentThreadId())return false;
    Publication p{};AcquireSRWLockShared(&gate);p=publication;ReleaseSRWLockShared(&gate);
    if(!p.gate_active||(!p.interaction.block_attack&&!p.interaction.request_commit))return true;
    const auto now=GetTickCount64();PreCommandSnapshot pre{};
    if(p.input_thread!=context.native_thread||p.input_command!=context.command_number||
       now<p.updated||now-p.updated>100||!read_pre_command_snapshot(&pre)||!primary_weapon_active(pre)||
       pre.sequence!=p.input_pre_sequence||pre.gameplay_epoch!=p.epoch||pre.native_cg_identity!=p.cg||
       pre.context.command_number!=context.command_number||pre.context.native_thread!=context.native_thread||
       pre.context.native_frame_time!=context.native_frame_time||pre.context.key_catcher||context.key_catcher||
       pre.context.native_override||context.native_override||!synchronized_server())return false;
    Ammo predicted{},server{};unsigned role{};
    if(!live_roles(p,p.cg+0x8a364,&role,&predicted)||role!=0||!live_roles(p,p.server,&role,&server)||role!=1)return false;
    AcquireSRWLockExclusive(&gate);
    const bool current=publication.gate_active&&publication.epoch==p.epoch&&publication.generation==p.generation&&
        publication.input_thread==p.input_thread&&publication.input_command==p.input_command&&
        publication.input_pre_sequence==p.input_pre_sequence&&publication.transaction==p.transaction;
    if(current){
        publication.interaction=controller.cancel();publication.roles[0]={};publication.roles[1]={};
        publication.committed=publication.commit_claimed=publication.authoritative_pending=false;
        publication.pair_failed=publication.commit_failed=false;publication.request_started=0;
    }
    const auto cycle=publication.interaction.cycle_pending;
    ReleaseSRWLockExclusive(&gate);
    if(current)trace("t5_manual_reload_cancel","reason=weapon_selection weapon=%u transaction=%llu cycle_pending=%u native_transfer=0",
        p.command_ammo.weapon,p.transaction,cycle);
    return current;
}
bool configure_manual_reload(HMODULE executable,bool input_ready) noexcept {
    wchar_t value[8]{};GetEnvironmentVariableW(L"BOVR_MANUAL_RELOAD",value,8);
    if(wcscmp(value,L"1")||!input_ready)return false;if(configured)return true;
    if(executable!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(executable)!=0x400000)return false;
    if(!validate_path(executable_path()).accepted||!manual_native::loaded_guards()||!extended_guards())return false;
    configured=true;
    GetEnvironmentVariableW(L"BOVR_LEFT_HANDED",value,8);left_handed=!wcscmp(value,L"1");
    value[0]=0;GetEnvironmentVariableW(L"BOVR_COD4_CONTROLS",value,8);cod4_controls=!wcscmp(value,L"1");
    trace("t5_manual_reload_configured","clip=00518030 disable_reload_flag=02000000 preexisting_reload_flag=explicit_physical_only_unowned native_instruction_patches=0 physical_magazines=1 empty_charge=1 native_mechanisms=1 event_ring=29,30 bolt_offset=054f native_batches=1 fuel_exchange=1 paired_dual_reload=1 left_fire_mask=00000080");
    return true;
}

void update_manual_reload_input(const CommandContext& context,const input::CommandRigSample& rig,bool live) noexcept {
    if(!configured)return;
    __try {if(read<DWORD>(0x251ca98)!=GetCurrentThreadId())return;}
    __except(EXCEPTION_EXECUTE_HANDLER){return;}
    PreCommandSnapshot pre{};command_profile::Copy exact{};
    const command_profile::ReadHost host{nullptr,command_profile::protected_copy,
        [](void*,PreCommandSnapshot* p)noexcept{return read_pre_command_snapshot(p);},
        [](void*)noexcept{return GetCurrentThreadId();},[](void*)noexcept{return GetTickCount64();}};
    command_profile::Status reader_status=command_profile::Status::context;
    const bool valid=live&&context.valid&&context.local_client==0&&context.native_thread==GetCurrentThreadId()&&
        read_pre_command_snapshot(&pre)&&primary_weapon_active(pre)&&command_input_generation_current(rig)&&
        (reader_status=command_profile::read(host,{context,pre.sequence,pre.gameplay_epoch,true},&exact))==command_profile::Status::accepted;
    Ammo a{},partner{};bool native=valid&&ammo(exact.identity.cg+0x8a364,&a);
    paired::Snapshot predicted_pair{};
    if(native&&a.paired){
        // A dual server frame temporarily selects its left gun. Retire that
        // frame before inspecting either authoritative slot or owning flags.
        native=synchronized_server()&&ammo(a.ps,&partner,1)&&
            pair_values(a,partner,{a.ps,pre.gameplay_epoch,rig.xr_generation,1},&predicted_pair);
    }
    if(native)native=own_reload_flags(exact,pre.native_game_time);
    if(!native)retire_reload_flags();
    const auto shot_sequence=observe_reload_fire(pre,rig,a,native);
    AcquireSRWLockExclusive(&gate);
    const bool same=native&&publication.cg==exact.identity.cg&&publication.epoch==pre.gameplay_epoch&&publication.generation==rig.xr_generation&&
        same_definition(publication.command_ammo,a)&&(!a.paired||same_definition(publication.command_partner,partner));
    const auto old_stage=publication.interaction.stage;
    m::Context c{};c.valid=native;c.left_handed=left_handed;c.commit_completed=same&&publication.committed;
    c.cod4_controls=cod4_controls;
    c.weapon=a.weapon;c.clip=a.clip;c.reserve=a.reserve;c.capacity=a.capacity;c.epoch=pre.gameplay_epoch;c.generation=rig.xr_generation;c.now=GetTickCount64();
    c.cancel_requested=same&&cancel_failed_request(publication,c.now);
    const char* cancel_reason=publication.pair_failed?"paired_native_failure":publication.commit_failed?"native_failure":"native_wait_timeout";
    c.mechanism=a.mechanism;c.shot_sequence=shot_sequence;
    c.confirmed_cycle_pending=native&&reload_fire_latch.pending;
    Ammo interaction_primary=a,interaction_partner=partner;
    if(same&&publication.authoritative_pending){
        const auto& authoritative=publication.committed_ammo;
        const bool prediction_received=(a.clip>=authoritative.clip&&
            (!a.paired||partner.clip>=publication.committed_partner.clip))||shot_sequence>publication.committed_fire_serial;
        const bool bounded=c.now>=publication.commit_started&&c.now-publication.commit_started<=100;
        if(c.commit_completed||(!prediction_received&&bounded)){
            // A native acknowledgement carries the actual committed server
            // result. Prediction can be one snapshot behind this command.
            c.clip=authoritative.clip;c.reserve=authoritative.reserve;
            interaction_primary=authoritative;if(a.paired)interaction_partner=publication.committed_partner;
        }
        if(prediction_received||!bounded)publication.authoritative_pending=false;
    }
    // Ejecting a timed tank intentionally discards its remaining contents in
    // the native helper. Only the copied interaction sees an empty receiver;
    // native inventory/HUD values remain untouched until physical completion.
    if(a.paired){
        paired::Snapshot interaction_pair{};
        if(!pair_values(interaction_primary,interaction_partner,{a.ps,pre.gameplay_epoch,rig.xr_generation,1},&interaction_pair))c.valid=false;
        const auto plan=paired::plan(interaction_pair);
        c.capacity=interaction_primary.capacity+interaction_partner.capacity;
        // Either empty native clip requires the one paired charging gesture.
        c.clip=(!interaction_primary.clip||!interaction_partner.clip)?0:interaction_primary.clip+interaction_partner.clip;
        c.reserve=static_cast<int>(plan.total_rounds);
        c.mechanism={true,m::ReloadKind::magazine,0,
            interaction_primary.mechanism.charge_on_empty||interaction_partner.mechanism.charge_on_empty,false};
        c.confirmed_cycle_pending=false;
    }else {
        if(a.fuel_exchange&&!c.commit_completed)c.clip=0;
        if(!a.fuel_exchange&&c.capacity-c.clip<static_cast<int>(a.minimum_missing))c.reserve=0;
    }
    auto interaction_frame=rig.frame;
    if(!rig.published_ms||c.now<rig.published_ms||c.now-rig.published_ms>100||
       !pre.observed_ms||c.now<pre.observed_ms||c.now-pre.observed_ms>100)interaction_frame.actions.focused=false;
    const auto result=controller.update(interaction_frame,c);
    const auto& reload_hand=interaction_frame.actions.hands[left_handed?0:1];
    const auto& reload_support=interaction_frame.actions.hands[left_handed?1:0];
    if(result.supported&&(result.stage==m::Stage::charge||result.stage==m::Stage::charging)){
        static std::uint64_t charge_observations{},last_charge_trace{};
        if(old_stage!=result.stage||c.now<last_charge_trace||c.now-last_charge_trace>=250){
            last_charge_trace=c.now;++charge_observations;
            trace("t5_manual_reload_charge", "count=%llu weapon=%u transaction=%llu stage=%u sequence=%llu fresh=%u trigger=%.4f armed=%u squeeze=%.4f handle_distance=%.5f coordinate=%.5f capture_near=%u acquired=%u opened=%u fraction=%.4f offhand_owned=%u clip=%d reserve=%d",
                charge_observations,a.weapon,result.transaction,static_cast<unsigned>(result.stage),interaction_frame.actions.sequence,
                result.charge_sample_fresh,reload_support.trigger.current,result.charge_trigger_armed,reload_support.squeeze.current,
                result.charge_handle_distance,result.charge_coordinate,result.charge_capture_near,result.charge_acquired,result.charge_opened,
                result.charge_fraction,m::uses_offhand(result),c.clip,c.reserve);
        }
    }
    if((cod4_controls?reload_hand.primary:reload_hand.secondary).current){
        static std::uint64_t observations{};
        if(++observations<=24||observations%120==0)trace("t5_manual_reload_request",
            "count=%llu weapon=%u reader=%u valid=%u native=%u same=%u context_valid=%u supported=%u stage=%u transaction=%llu clip=%d reserve=%d state=%d flags=%08x focused=%u views=%u rig_age=%llu pre_age=%llu primary=%u secondary=%u squeeze=%.4f trigger=%.4f",
            observations,a.weapon,static_cast<unsigned>(reader_status),valid,native,same,c.valid,result.supported,
            static_cast<unsigned>(result.stage),result.transaction,c.clip,c.reserve,a.state,a.flags,
            interaction_frame.actions.focused,interaction_frame.views_valid,
            c.now>=rig.published_ms?c.now-rig.published_ms:UINT64_MAX,c.now>=pre.observed_ms?c.now-pre.observed_ms:UINT64_MAX,
            reload_hand.primary.current,reload_hand.secondary.current,reload_support.squeeze.current,reload_support.trigger.current);
    }
    if(native&&result.supported&&c.confirmed_cycle_pending&&!result.cycle_pending)
        reload_fire_latch.pending=false;
    if(!same)publication={};
    publication.interaction=result;publication.cg=exact.identity.cg;publication.server=exact.identity.server;
    publication.variant=a.variant;publication.definition=a.definition;publication.epoch=pre.gameplay_epoch;publication.generation=rig.xr_generation;
    publication.command_ammo=a;
    publication.command_partner=partner;publication.pair_failed=false;
    publication.input_thread=context.native_thread;publication.input_command=context.command_number;publication.updated=c.now;
    publication.input_pre_sequence=pre.sequence;
    // A valid weapon identity keeps automatic reload suppressed during a
    // tracking interruption. The gesture cancels and must be started again.
    publication.gate_active=native;
    if(publication.transaction!=result.transaction||!result.request_commit){publication.transaction=result.transaction;publication.roles[0]={};publication.roles[1]={};publication.committed=false;publication.commit_claimed=false;
        publication.commit_failed=false;publication.request_started=0;}
    if(result.request_commit&&!publication.request_started)publication.request_started=c.now;
    ReleaseSRWLockExclusive(&gate);
    if(c.cancel_requested)trace("t5_manual_reload_cancel","reason=%s weapon=%u transaction=%llu cycle_pending=%u native_transfer=0",
        cancel_reason,a.weapon,result.transaction,result.cycle_pending);
    if(old_stage!=result.stage||(!same&&native))trace("t5_manual_reload_stage","stage=%u weapon=%u transaction=%llu clip=%d reserve=%d commit=%u block_attack=%u kind=%u rounds=%u cycle=%u fuel_exchange=%u name=%s paired=%u partner_clip=%d partner_reserve=%d",
        static_cast<unsigned>(result.stage),a.weapon,result.transaction,a.clip,a.reserve,result.request_commit,result.block_attack,
        static_cast<unsigned>(result.kind),result.commit_rounds,result.cycle_pending,a.fuel_exchange,a.name.data(),a.paired,partner.clip,partner.reserve);
}
void apply_manual_reload_command(const CommandContext& context,unsigned char* command) noexcept {
    if(!configured||!command)return;
    Publication p{};AcquireSRWLockShared(&gate);p=publication;ReleaseSRWLockShared(&gate);
    const auto now=GetTickCount64();
    if(!p.gate_active||p.input_thread!=GetCurrentThreadId()||p.input_command!=context.command_number||
       now<p.updated||now-p.updated>100||command[0x18]!=p.command_ammo.weapon)return;
    U32 buttons{};int time{};std::memcpy(&buttons,command+4,4);std::memcpy(&time,command,4);
    // Native ownership survives a cancelled gesture. Its optional Output may
    // be empty on tracking loss, so it cannot gate removal of stock reload.
    // Physical insertion uses native inventory transfer directly. Never queue
    // a stock reload animation, including when the physical request completes.
    buttons&=~0x08000000u;
    if(p.interaction.block_attack)buttons&=~(0x80000000u|(p.command_ammo.paired?0x80u:0u));
    std::memcpy(command+4,&buttons,4);
    commit_on_server_owner(p,time);
}

bool read_manual_reload_visual(m::Output* out) noexcept {
    if(!out)return false;*out={};AcquireSRWLockShared(&gate);const auto p=publication;ReleaseSRWLockShared(&gate);
    const auto now=GetTickCount64();if(!configured||!p.gate_active||!p.interaction.supported||now<p.updated||now-p.updated>100)return false;
    *out=p.interaction;return true;
}
namespace {
SRWLOCK visual_gate=SRWLOCK_INIT;
struct VisualStamp {std::uint64_t frame{},transaction{},updated{};bool device{};} visual_stamp;
}
void publish_manual_reload_pose_visual(std::uint64_t frame,std::uint64_t transaction,bool device) noexcept {
    AcquireSRWLockExclusive(&visual_gate);visual_stamp={frame,transaction,GetTickCount64(),device};ReleaseSRWLockExclusive(&visual_gate);
}
bool manual_reload_authored_device_visible(std::uint64_t frame,std::uint64_t transaction) noexcept {
    AcquireSRWLockShared(&visual_gate);const auto v=visual_stamp;ReleaseSRWLockShared(&visual_gate);const auto now=GetTickCount64();
    return v.device&&v.frame==frame&&v.transaction==transaction&&now>=v.updated&&now-v.updated<=100;
}
}
