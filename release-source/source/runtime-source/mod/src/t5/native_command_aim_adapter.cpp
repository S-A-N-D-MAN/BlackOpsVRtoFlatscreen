// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/native_command_aim_adapter.hpp"
#include "t5/dual_pistol_effects.hpp"
#include "t5/ordinary_event_fx.hpp"
#include "t5/client_command_angles.hpp"
#include "t5/manual_reload_adapter.hpp"
#include "t5/manual_grenade_adapter.hpp"
#include "t5/native_camera_mode.hpp"
#include "t5/primary_weapon_tracking.hpp"
#include "t5/crossbow_policy.hpp"
#include "t5/primary_support_rearm.hpp"
#include "t5/bullet_probe.hpp"
#include "t5/native_weapon_tracking_state.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/weapon_animation_probe.hpp"
#include "t5/weapon_borrowed_adapter.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <bit>
#include <limits>
namespace bovr::t5 {
namespace {
namespace xr=wawvr::xr;
using S=input::CommandAimStatus;
struct AimNative {
    PreCommandSnapshot pre{};
    xr::Basis3f axis{};
    std::uintptr_t cg{},ref{},topdown{},definition{},variant{};
    int native_time{},refdef_time{},weapon_state{},other_state{},weapon{},type{},weapon_class{},fire_type{};
    unsigned stack_count{};
    bool dual_wield{};
};
bool equal_context(const CommandContext&a,const CommandContext&b) noexcept {
    return a.valid&&b.valid&&a.local_client==b.local_client&&a.command_number==b.command_number&&
        a.native_thread==b.native_thread&&a.key_catcher==b.key_catcher&&a.connection_state==b.connection_state&&
        a.native_frame_time==b.native_frame_time&&a.native_override==b.native_override&&
        !memcmp(a.viewangles,b.viewangles,sizeof(a.viewangles))&&!memcmp(a.kickangles,b.kickangles,sizeof(a.kickangles));
}
template<class T>T aim_load(std::uintptr_t address){T value{};memcpy(&value,reinterpret_cast<void*>(address),sizeof(value));return value;}
bool copy_aim_native(std::uintptr_t base,const CommandContext& c,AimNative* out) noexcept {
    AimNative v{};
    if(!out||!read_pre_command_snapshot(&v.pre)||!equal_context(c,v.pre.context)||!v.pre.sequence||
       !v.pre.gameplay_epoch||!primary_weapon_active(v.pre)||!v.pre.native_gameplay_fields_valid||
       c.local_client||c.native_thread!=GetCurrentThreadId()||c.native_override||c.key_catcher||c.connection_state<9)return false;
    __try {
        v.cg=aim_load<std::uintptr_t>(base+0x2bf5354);if(!v.cg||v.cg!=v.pre.native_cg_identity)return false;
        const auto ps=v.cg+0x8a364;v.ref=v.cg+0x8c100;
        const auto mode=primary_weapon_tracking::client_mode(
            {aim_load<int>(ps+4),aim_load<int>(ps+0x1c4),aim_load<unsigned>(ps+0xc),aim_load<unsigned>(ps+0x10),
             aim_load<unsigned>(ps+0xe0),aim_load<unsigned>(ps+0xe4)},
            {aim_load<int>(v.cg+0x8a360),c.key_catcher,c.connection_state,c.native_override,
             aim_load<int>(v.cg+0xc)!=0,aim_load<int>(v.cg+0xa9d44)!=0});
        // Only the exact-profile paired client/server reader grants linked aim.
        if(aim_load<unsigned char>(ps+0x130)||mode!=primary_weapon_tracking::Mode::ordinary)return false;
        v.native_time=aim_load<int>(v.cg+0x8a348);v.refdef_time=aim_load<int>(v.cg+0x8c158);
        if(v.native_time<=0||v.native_time!=v.refdef_time||v.native_time!=v.pre.native_game_time)return false;
        memcpy(&v.axis,reinterpret_cast<void*>(v.ref+0x34),sizeof(v.axis));
        v.topdown=aim_load<std::uintptr_t>(base+0x7cd150);if(!v.topdown)return false;
        const auto name=aim_load<std::uintptr_t>(v.topdown);constexpr char expected_topdown[]="player_topDownCamMode";
        if(!name||memcmp(reinterpret_cast<void*>(name),expected_topdown,sizeof(expected_topdown))||
           aim_load<int>(v.topdown+0x10)!=5||aim_load<int>(v.topdown+0x18)!=0)return false;
        v.weapon=aim_load<unsigned char>(ps+0x144);if(v.weapon<=0)return false;
        v.weapon_state=aim_load<int>(ps+0x158);v.other_state=aim_load<int>(ps+0x15c);
        v.stack_count=aim_load<unsigned>(ps+0x1b0);
        // Preserve the native three-bit stackFireCount, including retained values
        // after changing firing types. Reject values outside its serialized range.
        if(!native_weapon_tracking_state(v.weapon_state)||!native_weapon_tracking_state(v.other_state)||!native_valid_stack_fire_count(v.stack_count))return false;
        v.variant=aim_load<std::uintptr_t>(base+0x7e19a8+4*v.weapon);if(!v.variant)return false;
        v.definition=aim_load<std::uintptr_t>(v.variant+8);if(!v.definition)return false;
        v.type=aim_load<int>(v.definition+0x1c);v.weapon_class=aim_load<int>(v.definition+0x20);
        v.fire_type=aim_load<int>(v.definition+0x30);
        if((v.type!=0&&v.type!=2&&v.type!=4)||v.weapon_class<0||(v.weapon_class==9&&v.type!=4)||aim_load<unsigned char>(v.definition+0x56a))return false;
        // Recheck copied ownership and view values. No server pointer or native
        // function is consulted, and the snapshot is not called atomic.
        if(aim_load<std::uintptr_t>(base+0x2bf5354)!=v.cg||aim_load<int>(v.cg+0x8a348)!=v.native_time||
           aim_load<int>(v.cg+0x8c158)!=v.refdef_time||memcmp(&v.axis,reinterpret_cast<void*>(v.ref+0x34),sizeof(v.axis))||
           aim_load<std::uintptr_t>(base+0x7cd150)!=v.topdown||aim_load<int>(v.topdown+0x18)||
           aim_load<std::uintptr_t>(v.topdown)!=name||memcmp(reinterpret_cast<void*>(name),expected_topdown,sizeof(expected_topdown)))return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    *out=v;return true;
}
bool same_native(const AimNative&a,const AimNative&b) noexcept {
    return a.pre.sequence==b.pre.sequence&&a.pre.gameplay_epoch==b.pre.gameplay_epoch&&
        equal_context(a.pre.context,b.pre.context)&&a.cg==b.cg&&a.ref==b.ref&&a.native_time==b.native_time&&
        a.refdef_time==b.refdef_time&&a.topdown==b.topdown&&a.definition==b.definition&&a.variant==b.variant&&
        a.weapon==b.weapon&&a.weapon_state==b.weapon_state&&a.other_state==b.other_state&&
        a.type==b.type&&a.weapon_class==b.weapon_class&&a.fire_type==b.fire_type&&
        a.stack_count==b.stack_count&&a.dual_wield==b.dual_wield&&!memcmp(&a.axis,&b.axis,sizeof(a.axis));
}
struct AimPending {input::CommandRigSample rig{};xr::ActionSnapshot actions{};AimNative native{};command_profile::Copy profile{};std::uint64_t invalidation{};bool active{},render_only{},visual_only_unarmed{};};
bool client_angle_cas(void*,std::uintptr_t address,std::uint32_t expected,std::uint32_t desired,std::uint32_t* observed) noexcept {
    if(!observed||!address||(address&3))return false;
    __try {*observed=static_cast<std::uint32_t>(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(address),
        static_cast<LONG>(desired),static_cast<LONG>(expected)));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct AimAdapter {
    std::uintptr_t base{};xr::Hand hand=xr::Hand::Right;
    std::atomic<DWORD> owner{};std::atomic_flag running=ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> invalidation{};
    AimPending pending{};
    std::mutex publications_lock;
    std::array<CommandAimPublication,128> publications{};
    std::uint64_t serial{},epoch{},generation{},last_sequence{},counts[11]{};
    int last_command{};bool seen_command{};
    int shot_forgotten_time{-1};
    bool two_hand_support{},exact_profile{},olympia{};
    command_profile::ReadHost profile_host{};
    input::profile_admission::RearmState rearm{};
    command_profile::Copy admission_profile{};
    input::CommandRigSample admission_rig{};
    std::atomic<std::uint64_t> admission_serial{},admission_revision{};
    std::uint64_t next_admission{},profile_reports{};
    std::array<std::uint64_t,static_cast<unsigned>(command_profile::Status::exception)+1> profile_reader_reports{};
    client_command_angles::Mode client_angles_mode=client_command_angles::Mode::disabled;
    client_command_angles::Host client_angles_host{nullptr,command_profile::protected_copy,client_angle_cas};
    bool client_angles_ready{};
    std::uint64_t client_angle_reports{},client_angle_refusals{};
};
std::atomic<AimAdapter*> aim_adapter{};
void revoke_aim(AimAdapter& a) noexcept {
    if(!a.exact_profile){++a.invalidation;return;}
    auto value=a.invalidation.load();
    while(value!=UINT64_MAX&&!a.invalidation.compare_exchange_weak(value,value+1)){}
    a.admission_serial.store(0);
}
bool profile_command(void*,PreCommandSnapshot* p) noexcept {return read_pre_command_snapshot(p);}
DWORD profile_thread(void*) noexcept {return GetCurrentThreadId();}
std::uint64_t profile_time(void*) noexcept {return GetTickCount64();}
void profile_report(AimAdapter& a,const char* reason,unsigned detail=0,const command_profile::Diagnostic* diagnostic=nullptr) noexcept {
    // Reader failures and their outer context report must not share a throttle.
    auto& counter=!std::strcmp(reason,"reader")&&detail<a.profile_reader_reports.size()?
        a.profile_reader_reports[detail]:a.profile_reports;
    const auto count=++counter;
    if(count>24&&count%120)return;
    if(diagnostic){const auto& d=*diagnostic;
        trace("t5_exact_profile_aim","status=%s detail=%u count=%llu serial=%llu revision=%llu rejection=%u weapon=%u models=%u bones=%u tags=%u selected=%u,%u reading=%u,%u roots=%u,%u canonical=%u catalog=%u arms=%s gun=%s writes=0 observer_only=1",
            reason,detail,count,a.admission_serial.load(),a.invalidation.load(),static_cast<unsigned>(d.rejection),d.weapon,d.model_count,d.bone_count,
            d.tag_count,d.selected_model,d.selected_bone,d.reading_model,d.reading_bone,d.arms_roots,d.gun_roots,d.canonical_status,d.catalog_status,
            d.arms_name.data(),d.gun_name.data());
    }else trace("t5_exact_profile_aim","status=%s detail=%u count=%llu serial=%llu revision=%llu writes=0 observer_only=1",
        reason,detail,count,a.admission_serial.load(),a.invalidation.load());
}
bool read_exact_native(AimAdapter& a,const CommandContext& c,AimNative* native,command_profile::Copy* profile) noexcept {
    PreCommandSnapshot pre{};
    if(!a.profile_host.command||!a.profile_host.command(a.profile_host.context,&pre))return false;
    const command_profile::Request request{c,pre.sequence,pre.gameplay_epoch,a.olympia};
    command_profile::Diagnostic diagnostic{};
    const auto result=command_profile::read(a.profile_host,request,profile,&diagnostic);
    if(result!=command_profile::Status::accepted){profile_report(a,"reader",unsigned(result),&diagnostic);return false;}
    const auto& p=*profile;AimNative n{};n.pre=pre;n.cg=p.identity.cg;n.ref=n.cg+0x8c100;
    std::memcpy(&n.axis,p.native.stock_view_bits.data()+3,sizeof(n.axis));
    n.topdown=p.native.topdown;n.definition=p.identity.definition;n.variant=p.identity.variant;
    n.native_time=p.native.native_time;n.refdef_time=p.native.refdef_time;n.weapon_state=p.native.weapon_state;
    n.other_state=p.native.other_state;n.weapon=int(p.profile.weapon);n.weapon_class=p.native.weapon_class;n.fire_type=p.native.fire_type;
    n.type=p.native.weapon_type;n.dual_wield=p.native.dual_wield;n.stack_count=p.native.stack_count;
    *native=n;return true;
}
input::profile_admission::NeutralSample neutral_actions(const xr::ActionSnapshot& actions) noexcept {
    input::profile_admission::NeutralSample s{};s.focused=actions.focused;
    const auto& h=actions.hands[1];const auto& p=h.grip;
    s.dominant_tracking=p.active&&p.position_valid&&p.position_tracked&&
        std::isfinite(p.pose.position.x)&&std::isfinite(p.pose.position.y)&&std::isfinite(p.pose.position.z)&&
        h.aim.active&&h.aim.orientation_valid&&h.aim.orientation_tracked;
    s.menu_active=actions.menu.active;s.menu_pressed=actions.menu.current;
    for(unsigned i=0;i<2;++i){const auto& a=actions.hands[i];s.squeeze_active[i]=a.squeeze.active;s.squeeze[i]=a.squeeze.current;
        s.trigger[i]={a.trigger_click.active,a.trigger_click.current,a.trigger.active,a.trigger.current};}
    return s;
}
bool crossbow_support_rearm(const command_profile::Copy& p) noexcept {
    if(p.native.weapon_type!=2||p.native.weapon_class!=0)return false;
    int capacity{};std::uint32_t name_pointer{};char name[128]{};
    if(!command_profile::protected_copy(nullptr,p.identity.variant+0x20,&capacity,4)||capacity!=1||
       !command_profile::protected_copy(nullptr,p.identity.variant,&name_pointer,4))return false;
    for(unsigned i=0;i<127;++i){
        if(!command_profile::protected_copy(nullptr,name_pointer+i,&name[i],1))return false;
        if(!name[i])return crossbow::weapon(p.native.weapon_type,p.native.weapon_class,capacity,name);
    }
    return false;
}
input::profile_admission::RearmKey rearm_key(const command_profile::Copy& p,const input::CommandRigSample& r,std::uint64_t invalidation) noexcept {
    input::profile_admission::RearmKey k{};k.profile=p.profile;k.identity=p.identity;k.gameplay_epoch=p.gameplay_epoch;
    k.xr_generation=r.xr_generation;k.invalidation=invalidation;k.owner_thread=p.thread;
    const auto& a=r.tracking_anchor;
    k.anchor_bits={std::bit_cast<unsigned>(a.position.x),std::bit_cast<unsigned>(a.position.y),std::bit_cast<unsigned>(a.position.z),
        std::bit_cast<unsigned>(a.orientation.x),std::bit_cast<unsigned>(a.orientation.y),std::bit_cast<unsigned>(a.orientation.z),std::bit_cast<unsigned>(a.orientation.w)};
    return k;
}
struct AimRunning {
    AimAdapter& a;DWORD error=GetLastError();bool entered{};
    explicit AimRunning(AimAdapter& value):a(value){entered=!a.running.test_and_set();if(!entered)revoke_aim(a);}
    ~AimRunning(){if(entered)a.running.clear();SetLastError(error);}
};
void aim_report(AimAdapter&a,S status,const CommandAimPublication* p=nullptr) noexcept {
    const auto count=++a.counts[static_cast<unsigned>(status)];
    if(count>3&&count%(status==S::accepted?120:600))return;
    if(p)trace("t5_native_command_aim","status=%u count=%llu serial=%llu command=%d epoch=%llu generation=%llu xr_frame=%llu actions=%llu original_age_ms=%llu pitch=%.6f yaw=%.6f packed=0x%08x cg=%p native_time=%d writes=4 viewangle_writes=0 origin=native_eye_lean latency=previous_completed_command shot_authority=0 support_pair=%u support_reason=%u support_separation=%.6f",
        unsigned(status),count,p->command.publication_serial,p->command.command_number,p->command.gameplay_epoch,p->command.rig.xr_generation,p->command.rig.frame.frame_id,p->command.rig.frame.actions.sequence,
        p->command.observed_ms-p->command.rig.published_ms,p->aim.pitch_degrees,p->aim.yaw_degrees,p->aim.packed,reinterpret_cast<void*>(p->cg_identity),p->native_time,
        p->aim.support.paired,unsigned(p->aim.support.reason),p->aim.support.separation_meters);
    else trace("t5_native_command_aim","status=%u count=%llu writes=0 stock_command_preserved=1",unsigned(status),count);
}
S aim_write(unsigned char* command,std::uint32_t packed,unsigned weapon,input::NativeCommandBytes* copied) noexcept {
    if(!command||!copied||(reinterpret_cast<std::uintptr_t>(command+0x20)&3))return S::storage;
    __try {
        memcpy(copied->data(),command,copied->size());
        // Native881547 copies a separate requested weapon selection. A pending
        // switch cannot inherit our current predicted weapon's ordinary gate.
        if((*copied)[0x18]!=weapon)return S::context;
        // One aligned native DWORD; no byte-by-byte partial field update.
        *reinterpret_cast<volatile std::uint32_t*>(command+0x20)=packed;
        memcpy(copied->data()+0x20,&packed,4);return S::accepted;
    }__except(EXCEPTION_EXECUTE_HANDLER){return S::storage;}
}
bool option(const wchar_t* key){wchar_t b[16]{};return GetEnvironmentVariableW(key,b,16)<16&&!wcscmp(b,L"1");}
bool idle_dominant_aim_rearm(const xr::ActionSnapshot& actions) noexcept {
    if(!option(L"BOVR_RENDER_NEUTRAL_RECOVERY")||manual_reload_uses_offhand()||manual_grenade_uses_offhand())return false;
    for(const auto& hand:actions.hands)
        if(hand.primary.current||hand.secondary.current||hand.stick_click.current)return false;
    return true;
}
bool off(const wchar_t* key){wchar_t b[16]{};const auto n=GetEnvironmentVariableW(key,b,16);return !n||(n<16&&!wcscmp(b,L"0"));}
bool aim_hand(xr::Hand* hand) noexcept {
    wchar_t text[16]{};const auto n=GetEnvironmentVariableW(L"BOVR_WEAPON_HAND",text,16);
    if(!hand||n>=16)return false;
    if(!n||!wcscmp(text,L"right")){*hand=xr::Hand::Right;return true;}
    if(!wcscmp(text,L"left")){*hand=xr::Hand::Left;return true;}return false;
}
enum class AimMode {disabled,standalone,borrowed_placement,two_hand_support,refused,exact_profile};
AimMode aim_startup_mode(bool command_hook,xr::Hand* hand) noexcept {
    if(!hand)return AimMode::refused;
    *hand=xr::Hand::Right;
    const bool exact_requested=!off(L"BOVR_EXACT_PROFILE_AIM_PROBE");
    if(exact_requested&&(!option(L"BOVR_EXACT_PROFILE_AIM_PROBE")||!option(L"BOVR_COMMAND_AIM_PLACEMENT_PROBE")||
       !option(L"BOVR_TWO_HAND_SUPPORT_PROBE")||!weapon_integration_renderer_ready()))return AimMode::refused;
    if(exact_requested){
        for(auto key:{L"BOVR_UI_PRODUCER_PROBE",L"BOVR_BLOOM_SHADER_PROBE",L"BOVR_CONVENTIONAL_OFFHAND_PROBE"})if(!off(key))return AimMode::refused;
        if(!option(L"BOVR_BORROWED_OLYMPIA_PROBE")||!weapon_olympia_profile_enabled())return AimMode::refused;
    }
    const bool support_requested=!off(L"BOVR_TWO_HAND_SUPPORT_PROBE");
    if(support_requested){wchar_t h[16]{};const auto n=GetEnvironmentVariableW(L"BOVR_WEAPON_HAND",h,16);
        if(!option(L"BOVR_TWO_HAND_SUPPORT_PROBE")||!option(L"BOVR_COMMAND_AIM_PLACEMENT_PROBE")||n!=5||wcscmp(h,L"right"))return AimMode::refused;}
    if(!support_requested&&off(L"BOVR_COMMAND_AIM_PROBE")&&off(L"BOVR_COMMAND_AIM_PLACEMENT_PROBE"))return AimMode::disabled;
    if(!option(L"BOVR_COMMAND_AIM_PROBE")||!option(L"BOVR_XR_STEREO_PROBE")||
       !option(L"BOVR_INPUT_PROBE")||!command_hook||!aim_hand(hand))return AimMode::refused;
    for(auto key:{L"BOVR_BULLET_PROBE",L"BOVR_BULLET_ENTRY_PROBE",L"BOVR_BULLET_TAIL_PROBE",
        L"BOVR_SHOT_COMMAND_PROBE",L"BOVR_CONSUMED_COMMAND_PROBE",L"BOVR_CONSUMED_TAIL_PROBE",
        L"BOVR_COMMAND_RIG_PROBE",L"BOVR_BULLET_SELECTION",L"BOVR_SHOT_HAPTICS",L"BOVR_WEAPON_PLACEMENT"})
        if(!off(key))return AimMode::refused;
    if(off(L"BOVR_COMMAND_AIM_PLACEMENT_PROBE")) {
        // The original A1 standalone exclusions remain unchanged.
        return off(L"BOVR_WEAPON_PROBE")&&off(L"BOVR_BORROWED_WEAPON_PLACEMENT")?
            AimMode::standalone:AimMode::refused;
    }
    if(!option(L"BOVR_COMMAND_AIM_PLACEMENT_PROBE")||*hand!=xr::Hand::Right)return AimMode::refused;
    for(auto key:{L"BOVR_COMMAND_PROBE",L"BOVR_WEAPON_PROBE",L"BOVR_WEAPON_NATIVE_CLEAR_PROBE",
        L"BOVR_BORROWED_WEAPON_PROBE",L"BOVR_BORROWED_WEAPON_PLACEMENT"})
        if(!option(key))return AimMode::refused;
    for(auto key:{L"BOVR_WEAPON_RESERVATION_PROBE",L"BOVR_WEAPON_LIFECYCLE_PROBE",
        L"BOVR_WEAPON_ANIMATION_PROBE",L"BOVR_WEAPON_BARREL_PROBE",L"BOVR_WEAPON_PROFILE_PROBE"})
        if(!off(key))return AimMode::refused;
    // Factory setup precedes input configuration. Flag requests alone cannot
    // grant this exception after native-clear or borrowed setup failed.
    return weapon_native_clear_probe_enabled()&&weapon_borrowed_probe_enabled()&&
        weapon_borrowed_placement_enabled()?(exact_requested?AimMode::exact_profile:(support_requested?AimMode::two_hand_support:AimMode::borrowed_placement)):AimMode::refused;
}
const native_guard::Span aim_code_guards[]{
#if defined(BOVR_TEST_NATIVE_GUARDS)
        {0x4815b6,8,"af5570f5a1810b7af78caf4bc70a660f0df51e42baf91d4de5b2328de0e83dfc"},
        {0x4815c6,4,"df3f619804a92fdb4057192dc43dd748ea778adc52bc498ce80524c014b81119"},
        {0x4815da,4,"df3f619804a92fdb4057192dc43dd748ea778adc52bc498ce80524c014b81119"},
        {0x29d77d,5,"8855508aade16ec573d21e6a485dfd0a7624085c1a14b5ecdd6485de0c6839a4"},
        {0x29db53,8,"af5570f5a1810b7af78caf4bc70a660f0df51e42baf91d4de5b2328de0e83dfc"},
        {0x29db68,8,"af5570f5a1810b7af78caf4bc70a660f0df51e42baf91d4de5b2328de0e83dfc"},
        {0x270bb3,10,"01d448afd928065458cf670b60f5a594d735af0172c8d67f22a81680132681ca"},
        {0x270bbf,5,"8855508aade16ec573d21e6a485dfd0a7624085c1a14b5ecdd6485de0c6839a4"},
        {0x270bcc,8,"af5570f5a1810b7af78caf4bc70a660f0df51e42baf91d4de5b2328de0e83dfc"},
        {0x270bda,8,"af5570f5a1810b7af78caf4bc70a660f0df51e42baf91d4de5b2328de0e83dfc"},
        {0xcfdD2,5,"8855508aade16ec573d21e6a485dfd0a7624085c1a14b5ecdd6485de0c6839a4"},
        {0x113e58,5,"8855508aade16ec573d21e6a485dfd0a7624085c1a14b5ecdd6485de0c6839a4"},
        {0x625c0,8,"af5570f5a1810b7af78caf4bc70a660f0df51e42baf91d4de5b2328de0e83dfc"}
#else
        {0x4815b6,8,"f8b2c53fb4d0465cc8b703aaf30867b8a104188476d28dab9c5717486abc6877"},
        {0x4815c6,4,"b26fa41f7078d9a34cfaa8313b26532e13639d1043ae41ae81e79f893e6a7f9a"},
        {0x4815da,4,"e940b0f6fdffd9278ecca1de24a124b933273dbb3e4441f9fe05c3bd3a5db9c5"},
        {0x29d77d,5,"cbee8d9f99e47d8b4c78193b6208b2eefb2ba4cd929541d72b787c5d9353e98e"},
        {0x29db53,8,"6aa1280bb4918a5dfa6e76a0b1ab8875f0e880953b055685ab221b9c1d19636b"},
        {0x29db68,8,"29a6ff6b44816f5afbdad5a096003deba5a98fe3485723db8fdb5398defd0e8a"},
        {0x270bb3,10,"75e73d540ff408362bf7c47d55dccbacfad4a199155af77acc340084fcb4c05f"},
        {0x270bbf,5,"7ce0da749a4bc45a164a36a2a39643d0dc79bc988906bb73e34e9d8e7c724ba3"},
        {0x270bcc,8,"3d747149c5b8e103a200df85b5d951d80b985e0bd4ee1ed3491ac4302f6f5c02"},
        {0x270bda,8,"031eb6eaa468158991254f0bcc3bfd3b8a944a4aacd0cec751039bd4524add64"},
        {0xcfdD2,5,"a3939f360275533a5b097554780fbd1ca7cfce2b4d91272d37267a5769be3a4b"},
        {0x113e58,5,"85f9df365ae59aee1b9b6be480c761f961f8abac34d4e328e3872e28cc4b0eb6"},
        {0x625c0,8,"589dae351264bef13a10d98febef43bda87fb00b90662ece321791a3735add61"}
#endif
};
bool aim_guards(std::uintptr_t base) noexcept {
    for(const auto& g:aim_code_guards)if(!native_guard::matches(base,g))return false;
    return true;
}
}
bool native_command_aim_enabled() noexcept {return aim_adapter.load()!=nullptr;}
bool weapon_two_hand_support_enabled() noexcept {const auto* a=aim_adapter.load();return a&&a->two_hand_support&&!manual_reload_uses_offhand()&&!manual_grenade_uses_offhand();}
void invalidate_native_command_aim() noexcept {if(auto* a=aim_adapter.load())revoke_aim(*a);}
void retire_native_command_aim_for_selection(const CommandContext& c) noexcept {
    auto* a=aim_adapter.load();if(!a||!a->exact_profile)return;
    if(!exact_profile_input_owner(c,false))return;
    AimRunning running(*a);if(!running.entered)return;
    a->admission_serial.store(0);
    PreCommandSnapshot current{};
    if(!read_pre_command_snapshot(&current)||!equal_context(c,current.context)||
       !primary_weapon_active(current)||!current.native_gameplay_fields_valid||
       current.sequence!=a->last_sequence||current.gameplay_epoch!=a->epoch||
       (a->pending.active&&(!equal_context(c,a->pending.native.pre.context)||
        a->pending.invalidation!=a->invalidation.load()))) {
        a->pending={};revoke_aim(*a);return;
    }
    // Keep only the original occurrence's copied rig. finish must reread and
    // compare the actual post-selection native profile before issuing any new
    // render token. The old pending native model can never authorize a write.
    if(a->pending.active)a->pending.render_only=true;
}
bool copy_render_command(const unsigned char* command,input::NativeCommandBytes* copied) noexcept {
    if(!command||!copied||(reinterpret_cast<std::uintptr_t>(command+0x20)&3))return false;
    __try {memcpy(copied->data(),command,copied->size());return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
namespace {
client_command_angles::Mode requested_client_angles() noexcept {
    wchar_t value[16]{};const auto size=GetEnvironmentVariableW(L"BOVR_CLIENT_COMMAND_ANGLES",value,16);
    if(size==7&&!wcscmp(value,L"observe"))return client_command_angles::Mode::observe;
    if(size==7&&!wcscmp(value,L"correct"))return client_command_angles::Mode::correct;
    return client_command_angles::Mode::disabled;
}
bool client_angle_layout(std::uintptr_t base) noexcept {
    // The same retail reads already pinned by the public placement writer.
    return native_guard::matches(base,{0x816d8,8,"42a62a3d66ac5b98b1c00ff0d403b61f8a36727964ecf6b7b513b999179c8319"})&&
        native_guard::matches(base,{0x816e8,8,"4e6a228817bfafd1be4d82b97b72a9e10b25884446cea328f81b052f6006d907"});
}
struct ClientAngleOwner {AimAdapter* adapter{};const CommandAimPublication* publication{};};
bool client_owned_angle_cas(void* opaque,std::uintptr_t address,std::uint32_t expected,std::uint32_t desired,std::uint32_t* observed) noexcept {
    const auto& owner=*static_cast<ClientAngleOwner*>(opaque);auto& a=*owner.adapter;const auto& p=*owner.publication;
    PreCommandSnapshot pre{};std::uintptr_t cg{};
    // Check incarnation even during failure cleanup. A changed gameplay state
    // is not itself permission to dereference the previous world's allocation.
    if(!a.profile_host.command||!a.profile_host.copy||!a.client_angles_host.compare_exchange||
       !a.profile_host.command(a.profile_host.context,&pre)||pre.gameplay_epoch!=p.command.gameplay_epoch||
       pre.native_cg_identity!=p.cg_identity||pre.context.native_thread!=GetCurrentThreadId()||
       pre.context.command_number!=p.command.command_number||pre.native_game_time!=p.native_time||
       !a.profile_host.copy(a.profile_host.context,0x2ff5354,&cg,sizeof(cg))||cg!=p.cg_identity||
       (address!=cg+0xa9ce4&&address!=cg+0xa9ce8))return false;
    return a.client_angles_host.compare_exchange(a.client_angles_host.context,address,expected,desired,observed);
}
void refresh_client_command_angles(AimAdapter& a,const CommandContext& context,const CommandAimPublication& p,
                                  const AimNative& native,const command_profile::Copy& profile,const unsigned char* command) noexcept {
    using namespace client_command_angles;
    if(!a.client_angles_ready||a.client_angles_mode==Mode::disabled)return;
    const auto now=GetTickCount64();
    Facts facts{a.exact_profile,profile.identity.tracking_mode==static_cast<unsigned>(primary_weapon_tracking::Mode::ordinary),
        native.dual_wield||p.dual_weapon!=0,native.type,native.weapon_class,native.fire_type,p.weapon,now,p.command.rig.published_ms,
        p.aim.packed,p.command.bytes};
    Words selected{},before{};
    if(!select(facts,&selected))return;
    unsigned rejection{},owned{};
    const auto address=native.cg+0xa9ce4;
    if(native.cg<0x10000||native.cg>UINT32_MAX-0xa9cec||!a.client_angles_host.read||
       !a.client_angles_host.read(a.client_angles_host.context,address,before.data(),sizeof(before))||!finite(before))rejection=1;
    AimNative current{};command_profile::Copy current_profile{};input::NativeCommandBytes current_command{};
    if(!rejection&&(!read_exact_native(a,context,&current,&current_profile)||!same_native(native,current)||
       !command_profile::same_current(profile,current_profile)||!copy_render_command(command,&current_command)||
       current_command!=p.command.bytes||a.invalidation.load()!=p.invalidation_revision||
       GetCurrentThreadId()!=p.command.native_thread||!command_input_generation_current(p.command.rig)))rejection=2;
    const auto write_now=GetTickCount64();
    if(!rejection&&(write_now<p.command.rig.published_ms||write_now-p.command.rig.published_ms>100))rejection=3;
    if(!rejection&&a.client_angles_mode==Mode::correct) {
        ClientAngleOwner owner{&a,&p};const Host current_host{&owner,nullptr,client_owned_angle_cas};
        if(!transfer(current_host,address,before,selected,&owned))rejection=4;
        else if(a.invalidation.load()!=p.invalidation_revision||!command_input_generation_current(p.command.rig)) {
            restore(current_host,address,before,selected,owned);owned=0;rejection=5;
        }
    }
    auto& counter=rejection?a.client_angle_refusals:a.client_angle_reports;
    if(++counter<=16||counter%120==0) {
        int command_time{};std::memcpy(&command_time,p.command.bytes.data(),4);
        trace("t5_client_command_angles",
            "count=%llu rejection=%u mode=%u command=%d command_time=%d serial=%llu epoch=%llu generation=%llu weapon=%u packed=0x%08x previous=%.6f,%.6f selected=%.6f,%.6f owned_mask=%u cg_writes=%u server_writes=0 command_writes=0 new_hooks=0 per_event_proven=0 source=authored_command",
            counter,rejection,unsigned(a.client_angles_mode),p.command.command_number,command_time,p.command.publication_serial,
            p.command.gameplay_epoch,p.command.rig.xr_generation,p.weapon,p.aim.packed,std::bit_cast<float>(before[0]),
            std::bit_cast<float>(before[1]),std::bit_cast<float>(selected[0]),std::bit_cast<float>(selected[1]),owned,
            !rejection&&a.client_angles_mode==Mode::correct?8u:0u);
    }
}
}
bool configure_native_command_aim(HMODULE image,bool command_hook) noexcept {
    const auto error=GetLastError();bool accepted=false;
    try {
        xr::Hand hand{};const auto mode=aim_startup_mode(command_hook,&hand);
        if(mode!=AimMode::disabled) {
            if((mode==AimMode::standalone||mode==AimMode::borrowed_placement||mode==AimMode::two_hand_support||mode==AimMode::exact_profile)&&image==GetModuleHandleW(nullptr)&&reinterpret_cast<std::uintptr_t>(image)==0x400000&&
               validate_path(executable_path()).accepted&&aim_guards(reinterpret_cast<std::uintptr_t>(image))&&
               (mode!=AimMode::exact_profile||command_profile::loaded_guards())) {
                if(!aim_adapter.load()) {auto* a=new AimAdapter;a->base=reinterpret_cast<std::uintptr_t>(image);a->hand=hand;a->two_hand_support=mode==AimMode::two_hand_support||mode==AimMode::exact_profile;
                    a->exact_profile=mode==AimMode::exact_profile;a->olympia=a->exact_profile&&weapon_olympia_profile_enabled();
                    a->profile_host={nullptr,command_profile::protected_copy,profile_command,profile_thread,profile_time};
                    a->client_angles_mode=requested_client_angles();
                    a->client_angles_ready=a->exact_profile&&a->client_angles_mode!=client_command_angles::Mode::disabled&&client_angle_layout(a->base);
                    trace("t5_client_command_angles_configured","mode=%u ready=%u new_hooks=0 server_writes=0 per_event_proven=0",unsigned(a->client_angles_mode),a->client_angles_ready);
                    aim_adapter.store(a);}
                accepted=aim_adapter.load()->two_hand_support==(mode==AimMode::two_hand_support||mode==AimMode::exact_profile)&&aim_adapter.load()->exact_profile==(mode==AimMode::exact_profile);
            }
            trace("t5_native_command_aim_configured","accepted=%u mode=%u guards=13 new_hooks=0 command_bytes=0x20,0x21,0x22,0x23 hand=%s independent_scene_sample=1 origin=native_eye_lean latency=previous_completed_command physical_muzzle=0",accepted,unsigned(mode),hand==xr::Hand::Left?"left":"right");
        }
    }catch(...){}SetLastError(error);return accepted;
}
void begin_native_command_aim(const CommandContext& c,const xr::ActionSnapshot& actions,
    std::uint64_t generation,const input::CommandRigSample& rig,bool live) noexcept {
    auto* a=aim_adapter.load();if(!a)return;
    if(a->exact_profile&&!exact_profile_input_owner(c,true))return;
    AimRunning running(*a);if(!running.entered)return;
    const DWORD thread=GetCurrentThreadId();auto owner=a->owner.load();
    if(!owner&&c.valid&&c.local_client==0&&c.native_thread==thread)a->owner.compare_exchange_strong(owner,thread);
    if(a->owner.load()!=thread||c.native_thread!=thread){revoke_aim(*a);return;}
    if(a->pending.active){a->pending={};revoke_aim(*a);aim_report(*a,S::ownership);return;}
    a->pending={};
    if(a->exact_profile)a->admission_serial.store(0); // occurrence retirement is NOT hard neutral rearm
    try {
        const auto entry_revision=a->invalidation.load();
        if(a->exact_profile){
            PreCommandSnapshot pre{};
            if(!read_pre_command_snapshot(&pre)||!equal_context(c,pre.context)||!pre.sequence||!pre.gameplay_epoch){
                a->rearm.revoke(actions.sequence);return;
            }
            std::lock_guard lock(a->publications_lock);
            if(pre.sequence<=a->last_sequence||pre.gameplay_epoch<a->epoch||
               (a->seen_command&&pre.gameplay_epoch==a->epoch&&c.command_number<=a->last_command)){
                profile_report(*a,"occurrence");return;
            }
            if(a->epoch!=pre.gameplay_epoch||a->generation!=generation){a->publications={};a->shot_forgotten_time=-1;}
            a->epoch=pre.gameplay_epoch;a->generation=generation;a->last_sequence=pre.sequence;
            a->last_command=c.command_number;a->seen_command=true;
        }
        command_profile::Copy profile{};
        AimNative native{};input::CommandAimProposal p{};
        if(!live||generation!=rig.xr_generation||!(a->exact_profile?read_exact_native(*a,c,&native,&profile):copy_aim_native(a->base,c,&native))){
            if(a->exact_profile){a->rearm.revoke(actions.sequence);profile_report(*a,"context");}aim_report(*a,S::context);return;}
        // Consume the producer occurrence even if later tracking/math refuses.
        // A fresh epoch may restart native command numbers; repeated callbacks
        // and regressions within that epoch never acquire another write.
        if(!a->exact_profile){
            std::lock_guard lock(a->publications_lock);
            if(native.pre.sequence<=a->last_sequence||native.pre.gameplay_epoch<a->epoch||
               (a->seen_command&&native.pre.gameplay_epoch==a->epoch&&c.command_number<=a->last_command)) {
                aim_report(*a,S::ownership);return;
            }
            if(a->epoch!=native.pre.gameplay_epoch||a->generation!=generation)a->publications={};
            a->epoch=native.pre.gameplay_epoch;a->generation=generation;
            a->last_sequence=native.pre.sequence;a->last_command=c.command_number;a->seen_command=true;
        }
        bool visual_only_unarmed=false;
        if(a->exact_profile){
            const auto observed=a->rearm.observe(rearm_key(profile,rig,a->invalidation.load()),actions.sequence,
                primary_support_rearm::sample(crossbow::rearm_sample(neutral_actions(actions),crossbow_support_rearm(profile)),profile,
                    idle_dominant_aim_rearm(actions)),
                command_input_generation_current(rig)&&a->invalidation.load()!=UINT64_MAX);
            if(observed!=input::profile_admission::RearmResult::ready){
                profile_report(*a,"rearm",unsigned(observed));
                // A fresh exact-profile rendering token does not grant a shot,
                // command write, or input action while neutral is pending.
                // Idle ordinary single guns may observe AIM neutral with the
                // dominant grip held, but triggers/actions remain independent.
                using R=input::profile_admission::RearmResult;
                visual_only_unarmed=option(L"BOVR_RENDER_NEUTRAL_RECOVERY")&&
                    (observed==R::wait_neutral||observed==R::neutral_observed);
                if(!visual_only_unarmed)return;
            }
        }
        const auto status=input::propose_native_command_aim(rig,actions,native.axis,a->hand,GetTickCount64(),&p,
            weapon_two_hand_support_enabled()&&!native.dual_wield);
        // A missed prediction retires this command only. The exact profile,
        // tracking, epoch and generation have already been checked above and
        // are checked again on the next occurrence. A timing hitch must not
        // latch the weapon to the stock camera until both grips are released.
        if(status!=S::accepted){if(a->exact_profile&&status!=S::stale)a->rearm.revoke(actions.sequence);aim_report(*a,status);return;}
        if(a->exact_profile&&entry_revision!=a->invalidation.load()){a->rearm.revoke(actions.sequence);return;}
        a->pending={rig,actions,native,profile,a->invalidation.load(),true,visual_only_unarmed,visual_only_unarmed};
        if(a->exact_profile&&!visual_only_unarmed&&native.pre.gameplay_active&&native.pre.native_pm_type==0&&
           (native.dual_wield||dual_pistol_effects::ordinary_mode()!=dual_pistol_effects::OrdinaryMode::disabled||
            ordinary_event_fx::mode()!=ordinary_event_fx::Mode::disabled))
            ensure_dual_pistol_hooks(c);
    }catch(...){a->pending={};}
}
void finish_native_command_aim(const CommandContext& c,unsigned char* command) noexcept {
    auto* a=aim_adapter.load();if(!a)return;AimRunning running(*a);if(!running.entered)return;
    if(a->owner.load()!=GetCurrentThreadId()||c.native_thread!=GetCurrentThreadId()){revoke_aim(*a);return;}
    const auto pending=a->pending;a->pending={};if(!pending.active)return;
    try {
        AimNative after{};command_profile::Copy after_profile{},final_profile{};CommandAimPublication p{};
        if(pending.invalidation!=a->invalidation.load()||!equal_context(c,pending.native.pre.context)){aim_report(*a,S::ownership);return;}
        if(!(a->exact_profile?read_exact_native(*a,c,&after,&after_profile):copy_aim_native(a->base,c,&after))||
           !command_input_generation_current(pending.rig)){
            if(a->exact_profile)a->rearm.revoke(pending.actions.sequence);
            aim_report(*a,S::changed);return;}
        const bool same_occurrence=(!a->exact_profile||command_profile::same_current(pending.profile,after_profile))&&
            same_native(pending.native,after);
        // A newly validated current gun may replace the outgoing model while
        // native weapon selection runs. It gets rendering permission only;
        // command angles still require unchanged before/after native identity.
        if(a->exact_profile){
            if((!pending.visual_only_unarmed&&!a->rearm.armed())||!input::profile_admission::same_control_owner(
                rearm_key(pending.profile,pending.rig,pending.invalidation),
                rearm_key(after_profile,pending.rig,a->invalidation.load()))){
                a->rearm.revoke(pending.actions.sequence);aim_report(*a,S::changed);return;}
        }else if(!same_occurrence){aim_report(*a,S::changed);return;}
        bool render_only=a->exact_profile&&(pending.render_only||!same_occurrence);
        const auto now=GetTickCount64();const auto status=input::propose_native_command_aim(pending.rig,pending.actions,after.axis,a->hand,now,&p.aim,
            weapon_two_hand_support_enabled()&&!after.dual_wield);
        if(status!=S::accepted){aim_report(*a,status);return;}
        // All locks and potentially throwing work happen before the only write.
        std::lock_guard lock(a->publications_lock);
        p.command.rig=pending.rig;p.command.gameplay_epoch=after.pre.gameplay_epoch;p.command.gameplay_active=true;
        p.command.observed_ms=now;p.command.native_thread=c.native_thread;p.command.command_number=c.command_number;
        p.cg_identity=after.cg;p.refdef_identity=after.ref;p.native_time=after.native_time;
        AimNative final_native{};
        if(!command_input_generation_current(pending.rig)||
           !(a->exact_profile?read_exact_native(*a,c,&final_native,&final_profile):copy_aim_native(a->base,c,&final_native))||
           (a->exact_profile&&!command_profile::same_current(after_profile,final_profile))||
           !same_native(after,final_native)||pending.invalidation!=a->invalidation.load()) {
            if(a->exact_profile)a->rearm.revoke(pending.actions.sequence);
            aim_report(*a,S::changed);return;}
        const auto final_now=GetTickCount64();
        if(final_now<pending.rig.published_ms||final_now-pending.rig.published_ms>100){aim_report(*a,S::stale);return;}
        p.command.observed_ms=final_now;
        if(a->exact_profile&&(a->next_admission>=UINT64_MAX-1||a->invalidation.load()==UINT64_MAX)){profile_report(*a,"exhausted");return;}
        if(a->exact_profile){
            if(!copy_render_command(command,&p.command.bytes)){a->rearm.revoke(pending.actions.sequence);aim_report(*a,S::storage);return;}
            render_only=render_only||p.command.bytes[0x18]!=unsigned(final_native.weapon);
        }
        if(!render_only){
            const auto written=aim_write(command,p.aim.packed,unsigned(final_native.weapon),&p.command.bytes);
            if(written!=S::accepted){if(a->exact_profile)a->rearm.revoke(pending.actions.sequence);aim_report(*a,written);return;}
            p.invalidation_revision=pending.invalidation;p.dual_wield=final_native.dual_wield;
            p.weapon=unsigned(final_native.weapon);p.variant_identity=final_native.variant;p.definition_identity=final_native.definition;
            p.dual_weapon=final_profile.native.dual_weapon;p.dual_variant_identity=final_profile.native.dual_variant;
            p.dual_definition_identity=final_profile.native.dual_definition;
            const auto& replaced=a->publications[a->serial%a->publications.size()];
            if(replaced.command.publication_serial){int time{};memcpy(&time,replaced.command.bytes.data(),4);
                if(time>a->shot_forgotten_time)a->shot_forgotten_time=time;}
            p.command.publication_serial=++a->serial;a->publications[(a->serial-1)%a->publications.size()]=p;
            refresh_client_command_angles(*a,c,p,final_native,final_profile,command);
        }
        if(a->exact_profile){
            a->admission_profile=final_profile;a->admission_rig=pending.rig;
            a->admission_revision.store(pending.invalidation);a->admission_serial.store(++a->next_admission);
            if(a->invalidation.load()!=pending.invalidation)a->admission_serial.store(0);
            if(a->next_admission<=12||a->next_admission%120==0)trace(render_only?"t5_exact_profile_render_admitted":"t5_exact_profile_aim_written","serial=%llu revision=%llu command=%d sequence=%llu epoch=%llu row=%u profile_revision=%u weapon=%u writes=%u placement_guaranteed=0",
                a->admission_serial.load(),pending.invalidation,c.command_number,final_profile.pre_sequence,final_profile.gameplay_epoch,
                final_profile.profile.row,final_profile.profile.revision,final_profile.profile.weapon,render_only?0u:4u);
            if(pending.visual_only_unarmed)profile_report(*a,"render_waiting_neutral");
        }
        if(!render_only)aim_report(*a,S::accepted,&p);
    }catch(...){}
}
bool exact_profile_aim_enabled() noexcept {const auto* a=aim_adapter.load();return a&&a->exact_profile;}
bool native_command_dual_current(const CommandContext& c) noexcept {
    const auto* a=aim_adapter.load();
    if(!a||!a->exact_profile||a->owner.load()!=GetCurrentThreadId()||c.native_thread!=GetCurrentThreadId()||
       !a->pending.active||a->pending.visual_only_unarmed||a->pending.render_only||
       !a->pending.native.dual_wield||!a->pending.profile.native.dual_wield||
       a->pending.invalidation!=a->invalidation.load()||!equal_context(c,a->pending.native.pre.context))return false;
    PreCommandSnapshot current{};const auto now=GetTickCount64();
    return read_pre_command_snapshot(&current)&&equal_context(c,current.context)&&primary_weapon_active(current)&&
        current.sequence==a->pending.native.pre.sequence&&current.gameplay_epoch==a->pending.native.pre.gameplay_epoch&&
        current.observed_ms&&now>=current.observed_ms&&now-current.observed_ms<=100&&
        input::profile_admission::valid(a->pending.profile.profile)&&command_input_generation_current(a->pending.rig);
}
bool native_command_hitscan_current(const CommandContext& c) noexcept {
    const auto* a=aim_adapter.load();
    if(!a||!a->exact_profile||a->owner.load()!=GetCurrentThreadId()||c.native_thread!=GetCurrentThreadId()||
       !a->pending.active||a->pending.render_only||a->pending.visual_only_unarmed||a->pending.native.dual_wield||
       a->pending.native.type!=0||a->pending.native.weapon_class<0||a->pending.native.weapon_class>=9||
       a->pending.invalidation!=a->invalidation.load()||!equal_context(c,a->pending.native.pre.context))return false;
    PreCommandSnapshot current{};const auto now=GetTickCount64();
    return read_pre_command_snapshot(&current)&&equal_context(c,current.context)&&current.gameplay_active&&
        current.native_pm_type==0&&current.sequence==a->pending.native.pre.sequence&&
        current.gameplay_epoch==a->pending.native.pre.gameplay_epoch&&current.observed_ms&&
        now>=current.observed_ms&&now-current.observed_ms<=100&&
        input::profile_admission::valid(a->pending.profile.profile)&&command_input_generation_current(a->pending.rig);
}
bool native_command_ordinary_attack_inhibited(const CommandContext& c) noexcept {
    const auto* a=aim_adapter.load();
    if(!option(L"BOVR_RENDER_NEUTRAL_RECOVERY")||!a||!a->exact_profile||
       a->owner.load()!=GetCurrentThreadId()||c.native_thread!=GetCurrentThreadId()||
       !a->pending.active||
       a->pending.native.dual_wield||a->pending.native.type!=0||
       a->pending.native.weapon_class<0||a->pending.native.weapon_class>4||
       a->pending.profile.identity.tracking_mode!=static_cast<unsigned>(primary_weapon_tracking::Mode::ordinary)||
       a->pending.profile.identity.server_link||a->pending.invalidation!=a->invalidation.load()||
       !equal_context(c,a->pending.native.pre.context))return false;
    const auto& dominant=a->pending.actions.hands[1];
    const bool held_grip=dominant.squeeze.active&&std::isfinite(dominant.squeeze.current)&&
        dominant.squeeze.current>=.65f&&dominant.squeeze.current<=1;
    const bool native_melee=a->pending.native.weapon_state>=17&&a->pending.native.weapon_state<=19;
    const bool anticipate_melee=held_grip&&((dominant.stick_click.active&&dominant.stick_click.current)||native_melee);
    if(!(a->pending.render_only&&a->pending.visual_only_unarmed)&&!anticipate_melee)return false;
    PreCommandSnapshot current{};const auto now=GetTickCount64();
    return read_pre_command_snapshot(&current)&&equal_context(c,current.context)&&current.gameplay_active&&
        current.native_gameplay_fields_valid&&current.native_pm_type==0&&
        current.sequence==a->pending.native.pre.sequence&&current.gameplay_epoch==a->pending.native.pre.gameplay_epoch&&
        current.native_cg_identity==a->pending.native.cg&&current.native_game_time==a->pending.native.native_time&&
        current.observed_ms&&now>=current.observed_ms&&now-current.observed_ms<=100&&
        a->pending.rig.published_ms&&now>=a->pending.rig.published_ms&&now-a->pending.rig.published_ms<=100&&
        input::profile_admission::valid(a->pending.profile.profile)&&command_input_generation_current(a->pending.rig);
}
bool exact_profile_input_owner(const CommandContext& c,bool bind) noexcept {
    auto* a=aim_adapter.load();if(!a||!a->exact_profile)return true;
    const auto thread=GetCurrentThreadId();auto owner=a->owner.load();
    if(!owner&&bind&&c.valid&&!c.local_client&&c.native_thread==thread)a->owner.compare_exchange_strong(owner,thread);
    if(a->owner.load()!=thread||c.native_thread!=thread){revoke_aim(*a);return false;}
    return true;
}
bool command_profile_admission_witness(input::profile_admission::Token t) noexcept {
    const auto* a=aim_adapter.load();
    return a&&a->exact_profile&&t.serial&&t.revision!=UINT64_MAX&&
        a->admission_serial.load()==t.serial&&a->admission_revision.load()==t.revision&&
        a->invalidation.load()==t.revision&&a->admission_serial.load()==t.serial;
}
bool command_profile_admission_current(input::profile_admission::Token t) noexcept {
    auto* a=aim_adapter.load();if(!a||!command_profile_admission_witness(t))return false;
    const auto error=GetLastError();bool valid=false;
    try {
        input::CommandRigSample rig{};command_profile::Copy copied{};
        {std::lock_guard lock(a->publications_lock);if(command_profile_admission_witness(t)){
            rig=a->admission_rig;copied=a->admission_profile;}}
        PreCommandSnapshot pre{};const auto now=GetTickCount64();
        valid=GetCurrentThreadId()==copied.thread&&read_pre_command_snapshot(&pre)&&primary_weapon_active(pre)&&
            pre.native_gameplay_fields_valid&&pre.sequence==copied.pre_sequence&&pre.gameplay_epoch==copied.gameplay_epoch&&
            pre.context.native_thread==copied.thread&&!pre.context.local_client&&!pre.context.native_override&&
            !pre.context.key_catcher&&pre.context.connection_state>=9&&pre.context.command_number==copied.command_number&&
            pre.context.native_frame_time==copied.command_clock&&pre.native_cg_identity==copied.identity.cg&&
            pre.native_game_time==copied.native.native_time&&now>=copied.finished_ms&&now-copied.finished_ms<=100&&
            rig.published_ms&&now>=rig.published_ms&&now-rig.published_ms<=100&&
            pre.observed_ms&&now>=pre.observed_ms&&now-pre.observed_ms<=100&&
            command_input_generation_current(rig)&&command_profile_admission_witness(t);
    }catch(...){}
    SetLastError(error);return valid;
}
static bool commit_native_anchor(const CommandContext& c,const input::CommandRigSample& original,
    const xr::Posef& desired_anchor,float expected_yaw,float desired_yaw,const RoomScaleView* room_scale) noexcept {
    const auto commit=[&]{return room_scale?commit_vr_room_scale(original,desired_anchor,*room_scale):
        commit_vr_body_yaw(original,desired_anchor,expected_yaw,desired_yaw);};
    auto* a=aim_adapter.load();
    if(!a||!a->exact_profile)return commit();
    if(!exact_profile_input_owner(c,false))return false;
    if(room_scale&&!memcmp(&original.tracking_anchor,&desired_anchor,sizeof(desired_anchor)))return commit();
    AimRunning running(*a);if(!running.entered)return false;
    bool committed=false;
    try {
        const input::profile_admission::Token token{a->admission_serial.load(),a->admission_revision.load()};
        input::CommandRigSample admitted{};command_profile::Copy profile{};
        {std::lock_guard lock(a->publications_lock);admitted=a->admission_rig;profile=a->admission_profile;}
        const auto revision=a->invalidation.load(),rearm_revision=a->rearm.revision();
        const auto expected=rearm_key(profile,original,revision);
        const bool carry=!a->pending.active&&a->rearm.armed()&&token.revision==revision&&
            c.valid&&c.command_number==profile.command_number&&c.native_frame_time==profile.command_clock&&
            c.native_thread==profile.thread&&admitted.xr_generation==original.xr_generation&&
            admitted.anchor_generation==original.anchor_generation&&admitted.published_ms==original.published_ms&&
            admitted.frame.frame_id==original.frame.frame_id&&
            admitted.frame.actions.sequence==original.frame.actions.sequence&&
            !memcmp(&admitted.tracking_anchor,&original.tracking_anchor,sizeof(original.tracking_anchor))&&
            command_profile_admission_current(token);
        // Retire first, before the renderer can see a changed anchor. Never
        // carry a render token or a command publication into the new space.
        a->admission_serial.store(0);
        committed=commit();
        const auto same_receipt=[&](const command_profile::Copy& current){
            return current.pre_sequence==profile.pre_sequence&&current.command_number==profile.command_number&&
                current.command_clock==profile.command_clock&&current.native.native_time==profile.native.native_time&&
                rearm_key(current,original,revision)==expected;
        };
        if(!committed){
            // A newer XR frame or an active render placement may refuse an
            // otherwise harmless body turn. Retain the old render receipt only
            // if its original coordinate space and exact occurrence still hold.
            AimNative native{};command_profile::Copy current{};
            const auto now=GetTickCount64();
            if(carry&&a->invalidation.load()==revision&&a->rearm.revision()==rearm_revision&&
               now>=original.published_ms&&now-original.published_ms<=100&&
               now>=profile.finished_ms&&now-profile.finished_ms<=100&&
               command_input_generation_current(original)&&read_exact_native(*a,c,&native,&current)&&command_profile::same_current(profile,current)&&
               a->invalidation.load()==revision){std::uint64_t empty=0;a->admission_serial.compare_exchange_strong(empty,token.serial);}
            return false;
        }
        // Retain historical commands for the delayed native server consumer.
        // Current render/effect readers still require exact current anchor;
        // epoch, session generation and invalidation retire both consumers.
        auto rebased=original;rebased.tracking_anchor=desired_anchor;
        AimNative native{};command_profile::Copy current{};
        const bool current_owner=carry&&a->invalidation.load()==revision&&a->rearm.revision()==rearm_revision&&
            command_input_generation_current(rebased)&&read_exact_native(*a,c,&native,&current)&&
            same_receipt(current)&&a->invalidation.load()==revision;
        const bool carried=current_owner&&(room_scale?
            a->rearm.rebase_horizontal_anchor(expected,rearm_key(current,rebased,revision)):
            a->rearm.rebase_anchor(expected,rearm_key(current,rebased,revision)))&&
            a->invalidation.load()==revision;
        if(!carried)a->rearm.revoke(original.frame.actions.sequence);
        if(room_scale&&carried&&a->next_admission!=UINT64_MAX) {
            // Continuous body translation must not leave every rendered frame
            // without a weapon receipt. Publish a NEW visual receipt in the
            // rebased space after exact native revalidation; retained shot
            // commands keep their original pose/anchor and immutable bytes.
            std::lock_guard lock(a->publications_lock);
            if(a->invalidation.load()==revision) {
                a->admission_rig=rebased;a->admission_profile=current;
                a->admission_revision.store(revision);a->admission_serial.store(++a->next_admission);
                if(a->invalidation.load()!=revision)a->admission_serial.store(0);
            }
        }
        // Only the next freshly read native command can publish an admission.
        return true;
    }catch(...){a->admission_serial.store(0);a->rearm.revoke(original.frame.actions.sequence);return committed;}
}
bool commit_native_command_aim_body_yaw(const CommandContext& c,const input::CommandRigSample& r,
    const xr::Posef& anchor,float expected,float desired) noexcept {
    return commit_native_anchor(c,r,anchor,expected,desired,nullptr);
}
bool commit_native_command_aim_room_scale(const CommandContext& c,const input::CommandRigSample& r,
    const xr::Posef& anchor,const RoomScaleView& view) noexcept {
    return commit_native_anchor(c,r,anchor,0,0,&view);
}
bool read_command_profile_admission(const animation_detail::ClearComparisonKey& key,
    input::profile_admission::Token* token,command_profile::Copy* output) noexcept {
    if(token)*token={};if(output)*output={};auto* a=aim_adapter.load();
    if(!a||!a->exact_profile||!token||!output)return false;
    const auto error=GetLastError();bool valid=false;
    try {
        input::profile_admission::Token t{};command_profile::Copy p{};
        {std::lock_guard lock(a->publications_lock);t={a->admission_serial.load(),a->admission_revision.load()};p=a->admission_profile;}
        const auto delta=std::int64_t(key.command_number)-p.command_number;
        const auto& id=p.identity;const auto& profile=p.profile;
        valid=command_profile_admission_current(t)&&key.command_epoch==p.gameplay_epoch&&key.command_sequence==p.pre_sequence&&
            key.thread==p.thread&&key.weapon==profile.weapon&&key.profile_row==profile.row&&key.profile_revision==profile.revision&&
            key.gun_bone==profile.gun&&key.flash_bone==profile.flash&&key.bone_count==profile.bones&&key.pool_index==id.pool&&
            key.command_clock==p.command_clock&&delta>=0&&delta<=1&&key.native_time==p.native.native_time&&
            key.frame_time==p.native.native_frame_time&&key.weapon_state==p.native.weapon_state&&key.other_weapon_state==p.native.other_state&&
            key.opaque_identity==std::array<std::uint32_t,6>{id.cg,id.owner,id.object,id.variant,id.definition,id.table}&&
            command_profile_admission_witness(t);
        if(valid){*token=t;*output=p;}
    }catch(...){}
    SetLastError(error);return valid;
}

bool read_latest_native_command_aim_publication(CommandAimPublication* out) noexcept {
    if(out)*out={};auto* a=aim_adapter.load();if(!a||!out)return false;
    std::uint64_t serial{};
    try {std::lock_guard lock(a->publications_lock);serial=a->serial;}catch(...){return false;}
    return read_native_command_aim_publication(serial,out);
}
bool read_native_command_aim_for_shot(const input::NativeCommandBytes& command,std::uintptr_t cg,CommandAimPublication* out,
                                    bool allow_ordinary) noexcept {
    if(out)*out={};auto* a=aim_adapter.load();if(!out||!a||!a->exact_profile)return false;
    const auto error=GetLastError();bool valid=false;unsigned rejected{},matches{},same_time{};
    int time{},forgotten{};std::uint64_t different{},publication{},pose_age{},publication_age{};DWORD producer{},owner{};
    try {std::lock_guard lock(a->publications_lock);
        const auto revision=a->invalidation.load();const auto now=GetTickCount64();memcpy(&time,command.data(),4);
        CommandAimPublication p{};forgotten=a->shot_forgotten_time;owner=a->owner.load();
        for(const auto& candidate:a->publications)if(candidate.command.publication_serial){
            int candidate_time{};memcpy(&candidate_time,candidate.command.bytes.data(),4);
            if(candidate_time==time){++same_time;different=0;
                for(unsigned i=0;i<command.size();++i)if(candidate.command.bytes[i]!=command[i])different|=std::uint64_t{1}<<i;}
            if(candidate.command.bytes==command){++matches;p=candidate;}}
        PreCommandSnapshot current{};
        producer=p.command.native_thread;publication=p.command.publication_serial;
        publication_age=now>=p.command.observed_ms?now-p.command.observed_ms:UINT64_MAX;
        pose_age=now>=p.command.rig.published_ms?now-p.command.rig.published_ms:UINT64_MAX;
        if(matches!=1)rejected|=1;
        if(time<0||time<=forgotten)rejected|=2;
        if((!p.dual_wield&&!allow_ordinary)||p.weapon!=command[0x18]||(!p.dual_wield&&p.dual_weapon))rejected|=4;
        if(p.invalidation_revision!=revision)rejected|=8;
        if(p.cg_identity!=cg||!p.command.gameplay_active)rejected|=16;
        // The owned server shot executes on the native server worker. The
        // retained command must belong to the bound input producer, not that
        // consumer thread. The caller independently proves the firing stack.
        if(!owner||producer!=owner)rejected|=32;
        if(p.command.gameplay_epoch!=a->epoch)rejected|=64;
        if(p.command.rig.xr_generation!=a->generation)rejected|=128;
        // A server worker consumes an already authored native command later
        // than its producer. Preserve that command's original pose; validate
        // its freshness at publication and bound consumption separately.
        // Pinned ClientThink_real0x69d49e bounds past commandTime at1000ms.
        if(!p.command.observed_ms||publication_age>1000)rejected|=256;
        if(!p.command.rig.published_ms||p.command.observed_ms<p.command.rig.published_ms||
           p.command.observed_ms-p.command.rig.published_ms>100)rejected|=512;
        if(!consumed_command_input_generation_current(p.command.rig))rejected|=1024;
        if(!read_pre_command_snapshot(&current)||!primary_weapon_active(current)||current.native_cg_identity!=cg||
           current.context.native_thread!=owner||current.gameplay_epoch!=p.command.gameplay_epoch)rejected|=2048;
        if(a->invalidation.load()!=revision)rejected|=4096;
        if(!rejected){*out=p;valid=true;}
    }catch(...){rejected|=8192;}
    static std::atomic<unsigned> rejected_count{},accepted_count{};
    const auto count=valid?++accepted_count:++rejected_count;
    if(count<=16||count%120==0)trace(allow_ordinary?"t5_hitscan_shot_lookup":"t5_dual_pistol_shot_lookup",
        "accepted=%u count=%u rejected_mask=%u exact_matches=%u same_time=%u byte_difference=%llx time=%d forgotten=%d publication=%llu producer=%lu owner=%lu consumer=%lu publication_age=%llu pose_age=%llu",
        valid,count,rejected,matches,same_time,different,time,forgotten,publication,producer,owner,GetCurrentThreadId(),publication_age,pose_age);
    SetLastError(error);return valid;
}
bool read_native_command_aim_for_effect(int command_time,std::uintptr_t cg,unsigned primary_weapon,CommandAimPublication* out,
                                      EffectAimDiagnostic* diagnostic) noexcept {
    if(out)*out={};if(diagnostic)*diagnostic={};auto* a=aim_adapter.load();
    if(!out||!a||!a->exact_profile)return false;
    const auto error=GetLastError();bool valid=false;EffectAimDiagnostic d{};
    try {std::lock_guard lock(a->publications_lock);const auto revision=a->invalidation.load();const auto now=GetTickCount64();
        CommandAimPublication p{};
        for(const auto& candidate:a->publications){int time{};memcpy(&time,candidate.command.bytes.data(),4);
            if(!candidate.command.publication_serial)continue;
            if(time>d.latest_command_time)d.latest_command_time=time;
            if(time==command_time&&candidate.command.bytes[0x18]==primary_weapon){++d.matches;p=candidate;}}
        d.publication=p.command.publication_serial;
        d.command_age_ms=p.command.observed_ms&&now>=p.command.observed_ms?now-p.command.observed_ms:UINT64_MAX;
        d.pose_age_ms=p.command.rig.published_ms&&now>=p.command.rig.published_ms?now-p.command.rig.published_ms:UINT64_MAX;
        PreCommandSnapshot current{};
        if(d.matches!=1)d.rejection|=1;
        if(command_time<0||command_time<=a->shot_forgotten_time)d.rejection|=2;
        if(p.weapon!=primary_weapon||p.cg_identity!=cg||!p.command.gameplay_active)d.rejection|=4;
        if(p.invalidation_revision!=revision||p.command.gameplay_epoch!=a->epoch||p.command.rig.xr_generation!=a->generation)d.rejection|=8;
        if(p.command.native_thread!=GetCurrentThreadId())d.rejection|=16;
        if(d.command_age_ms>100||d.pose_age_ms>100)d.rejection|=32;
        // This is the exact retained native event's authored geometry. An owned
        // later body/room-scale rebase must not substitute the stock gun angles;
        // session/recenter generations still retire the event. Current render
        // and generic publication readers deliberately keep the stricter anchor.
        if(!consumed_command_input_generation_current(p.command.rig))d.rejection|=64;
        if(!read_pre_command_snapshot(&current)||!primary_weapon_active(current)||current.native_cg_identity!=cg||
           current.gameplay_epoch!=p.command.gameplay_epoch)d.rejection|=128;
        if(a->invalidation.load()!=revision)d.rejection|=256;
        if(!d.rejection){*out=p;valid=true;}
    }catch(...){d.rejection|=512;}
    if(diagnostic)*diagnostic=d;SetLastError(error);return valid;
}
bool read_native_command_aim_publication(std::uint64_t serial,CommandAimPublication* out) noexcept {
    if(out)*out={};auto* a=aim_adapter.load();if(!a||!out||!serial)return false;
    const auto error=GetLastError();bool valid=false;
    try {std::lock_guard lock(a->publications_lock);const auto& p=a->publications[(serial-1)%a->publications.size()];
        const auto now=GetTickCount64();if(p.command.publication_serial==serial&&now>=p.command.rig.published_ms&&
           now-p.command.rig.published_ms<=100&&command_input_generation_current(p.command.rig)){
            PreCommandSnapshot current{};if(read_pre_command_snapshot(&current)&&primary_weapon_active(current)&&current.gameplay_epoch==p.command.gameplay_epoch){*out=p;valid=true;}}
    }catch(...){}SetLastError(error);return valid;
}
}
