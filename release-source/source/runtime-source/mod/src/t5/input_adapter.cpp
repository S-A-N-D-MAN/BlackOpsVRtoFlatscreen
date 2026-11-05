// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/input_adapter.hpp"
#include "t5/manual_reload_adapter.hpp"
#include "t5/crossbow_adapter.hpp"
#include "t5/crossbow_script_aim.hpp"
#include "t5/manual_grenade_adapter.hpp"
#include "t5/command_probe.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/command_rig_adapter.hpp"
#include "t5/native_command_aim_adapter.hpp"
#include "t5/mounted_look_context.hpp"
#include "t5/mounted_look_delivery_guards.hpp"
#include "t5/helicopter_context.hpp"
#include "t5/guided_missile_context.hpp"
#include "t5/support_weapon_context.hpp"
#include "input/guided_missile_steering.hpp"
#include "t5/helicopter_delivery_guards.hpp"
#include "t5/helicopter_cannon_delivery_guards.hpp"
#include "t5/motorcycle_context.hpp"
#include "t5/swimming_context.hpp"
#include "t5/room_scale_native.hpp"
#include "t5/primary_weapon_variant.hpp"
#include "t5/strela_aim.hpp"
#include "t5/slingshot_context.hpp"
#include "t5/aircraft_monitor_context.hpp"
#include "input/helicopter_router.hpp"
#include "input/motorcycle_router.hpp"
#include "input/vehicle_look_delta.hpp"
#include "t5/identity.hpp"
#include "input/action_router.hpp"
#include "input/cod4_controls.hpp"
#include "input/movement_orientation.hpp"
#include "input/swimming_movement.hpp"
#include "input/body_yaw_transfer.hpp"
#include "input/knife_swing_gesture.hpp"
#include "t5/tunnel_crawl_context.hpp"
#include "input/turn_controller.hpp"
#include "loader/trace.hpp"
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <limits>

namespace bovr::t5 {
namespace {
constexpr std::uintptr_t execute_rva=0x219d00,yaw_rva=0x2511e24;
#if defined(BOVR_TEST_NATIVE_GUARDS)
constexpr std::array<native_guard::Span,3> cycle_guard_spans{{
    {execute_rva,28,"3addfb141cd7c9c4c6543a82191a3707ac29c7a041217782e61d4d91c691aee8"},
    {0x163c2c,20,"de47c9b27eb8d300dbb5f2c353e632c393262cf06340c4fa7f1b40c4cbd36f90"},
    {0xeb090,61,"c6e26c3e31bac75ea556356cbbd12190e29f277ea5f9010f8f88d5ab3363a2cf"}}};
constexpr std::array<native_guard::Span,5> alternate_guard_spans{{
    {0x163c7f,40,"2c34ce1df23b838c5abf2a7f6437cca3d3067ed509ff25f11df6b11b582b51eb"},
    {0x1aa390,84,"4fea5e6a3ec5f5474a26d858bc77b6d7bd3ab864ea02d988683fdc648602b248"},
    {0x1aa422,96,"2ea9ab9198d1638007400cd2c3bef1cc745b864b76011a0e1bc52180ac6452d4"},
    {0x207a40,30,"0679246d6c4216de0daa08e5523fb2674db2b6599c3b72ff946b488a15290b62"},
    {0x3983bf,27,"ea49aa9f6f6cf2d53d454e628ba5a339cc000230c4651655d0237711d747f50b"}}};
#else
constexpr std::array<native_guard::Span,3> cycle_guard_spans{{
    {execute_rva,28,"6c96774c46507a14060204cf64d9fc2c9249fd2ed8e3665e8d59a62a14e6a71a"},
    {0x163c2c,20,"cca577e5f4381ddb30ad6638c96a9fd68a429d9c31068f4b469f56614c70b39a"},
    {0xeb090,61,"313747141a577b3000829076bb3d457b3d490b1910bd9115ba0d9ee4e7b5ec6f"}}};
constexpr std::array<native_guard::Span,5> alternate_guard_spans{{
    {0x163c7f,40,"e547657cbff7fef986eb406483803c97c9784eab733435585b855d177fffb954"},
    {0x1aa390,84,"f165e75339b6ada4340a3cc69780a060cfa7ba939f572c46dc337717fee15bf1"},
    {0x1aa422,96,"8388d99ce48a3215844ee6acce1405044b15ef235fa0e6b43ff5f95971714e6b"},
    {0x207a40,30,"52264c4a21c8d024ec70c5f313595c5815c7bd92d60d430696fcff30c4f2d796"},
    {0x3983bf,27,"8d7778eaf13bece301e7d35c6e166d433051ac815a4eafae2c079e4e7b88e73a"}}};
#endif

bool crosshair_guards_match(const unsigned char* image) noexcept {
    if(!image)return false;
    __try {return !bovr::native_guard::compare(image+execute_rva, 28, "6c96774c46507a14060204cf64d9fc2c9249fd2ed8e3665e8d59a62a14e6a71a")&&
        !bovr::native_guard::compare(image+0xa3ea2, 49, "9090dfe20841e94b28e4c0a072b27c27874f50226d361197b47a4acb6dcf8ade")&&
        !bovr::native_guard::compare(image+0x3744d0, 12, "182bab170e4fe41e48aad2d587c32cebd261a0de48cd4b6a136ee6215b1f7e37");}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct CrosshairDvar {
    std::uintptr_t object{},name{};std::uint32_t flags{},type{};std::uint8_t current{};
    friend bool operator==(const CrosshairDvar&,const CrosshairDvar&)=default;
};
bool copy_crosshair_dvar_once(std::uintptr_t base,CrosshairDvar* out) noexcept {
    if(!base||base>UINTPTR_MAX-0x2b67bc8||!out)return false;
    __try {
        CrosshairDvar value{};memcpy(&value.object,reinterpret_cast<const void*>(base+0x2b67bc4),4);
        if(!value.object||value.object>UINTPTR_MAX-0x19)return false;
        memcpy(&value.name,reinterpret_cast<const void*>(value.object),4);
        constexpr char crosshair_expected_name[]="cg_drawCrosshair";
        if(!value.name||value.name>UINTPTR_MAX-sizeof(crosshair_expected_name)||memcmp(reinterpret_cast<const void*>(value.name),crosshair_expected_name,sizeof(crosshair_expected_name)))return false;
        memcpy(&value.flags,reinterpret_cast<const void*>(value.object+0xc),4);
        memcpy(&value.type,reinterpret_cast<const void*>(value.object+0x10),4);
        memcpy(&value.current,reinterpret_cast<const void*>(value.object+0x18),1);
        if(value.flags!=0x1001||value.type||value.current>1)return false;
        *out=value;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool copy_crosshair_dvar(std::uintptr_t base,CrosshairDvar* out) noexcept {
    if(!out)return false;*out={};CrosshairDvar first{},second{};
    if(!copy_crosshair_dvar_once(base,&first)||!copy_crosshair_dvar_once(base,&second)||first!=second)return false;
    *out=second;return true;
}


bool cycle_guards_match(const unsigned char* image) noexcept {
    if(!image) return false;
    __try {
        for(const auto& guard:cycle_guard_spans)if(!native_guard::matches(image+guard.address,guard.size,guard.sha256))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// Retail action slots consume a numbered argument. Down performs the selection
// synchronously; Up only validates/parses that argument and owns no held key.





bool alternate_guards_match(const unsigned char* image) noexcept {
    if(!image)return false;
    __try {
        if(!cycle_guards_match(image))return false;
        for(const auto& guard:alternate_guard_spans)if(!native_guard::matches(image+guard.address,guard.size,guard.sha256))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
constexpr std::array<const char*,8> names{"attack","speed_throw","activate","reload","gostand","movedown","sprint","melee"};
using Execute=void(__cdecl*)(int,int,const char*);
void pulse_action_slot(Execute execute,unsigned slot) {
    if(slot<1||slot>4)return;
    char down[]{"+actionslot 1"},up[]{"-actionslot 1"};
    down[12]=up[12]=char('0'+slot);
    // Complete the bounded release even if Down reenters or exits exceptionally.
    // No retry is retained; the router claim was consumed before native entry.
    __try {execute(0,0,down);}
    __finally {execute(0,0,up);}
}
struct AlternateSlots {
    std::array<int,4> types{};
    unsigned char weapon{};
    int requested{};
};
unsigned unique_alternate_slot(const AlternateSlots& slots) noexcept {
    unsigned result{};
    for(unsigned i=0;i<slots.types.size();++i) {
        if(slots.types[i]<0||slots.types[i]>3)return 0;
        if(slots.types[i]==2) {if(result)return 0;result=i+1;}
    }
    return result;
}
bool copy_native_alternate(std::uintptr_t base,const PreCommandSnapshot& pre,AlternateSlots* out) noexcept {
    if(!base||base>UINTPTR_MAX-0x2bf5358||!out||!pre.native_cg_identity||pre.native_cg_identity>UINTPTR_MAX-0xa9bec)return false;
    __try {
        const auto cg=*reinterpret_cast<const std::uintptr_t*>(base+0x2bf5354);
        if(cg!=pre.native_cg_identity||*reinterpret_cast<const int*>(cg+0x8a348)!=pre.native_game_time)return false;
        AlternateSlots value{};const auto ps=cg+0x8a364;
        memcpy(value.types.data(),reinterpret_cast<const void*>(ps+0x500),sizeof(value.types));
        value.weapon=*reinterpret_cast<const unsigned char*>(ps+0x144);
        value.requested=*reinterpret_cast<const int*>(cg+0xa9be8);
        if(!value.weapon||value.requested!=value.weapon||
           *reinterpret_cast<const std::uintptr_t*>(base+0x2bf5354)!=cg||
           *reinterpret_cast<const int*>(cg+0x8a348)!=pre.native_game_time||
           *reinterpret_cast<const unsigned char*>(ps+0x144)!=value.weapon||
           *reinterpret_cast<const int*>(cg+0xa9be8)!=value.requested||
           memcmp(value.types.data(),reinterpret_cast<const void*>(ps+0x500),sizeof(value.types)))return false;
        *out=value;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
struct PrimaryWeaponMode {
    unsigned weapon{};
    int type{},weapon_class{};
    std::array<char,64> internal_name{},localized_name{};
    bool named_melee{},melee{};
};
template<std::size_t N> bool copy_variant_text(std::uintptr_t pointer,std::array<char,N>* out) noexcept {
    if(!out)return false;out->fill(0);if(!pointer||pointer>UINT32_MAX-N)return false;
    for(std::size_t i=0;i<N;++i){const auto c=*reinterpret_cast<const unsigned char*>(pointer+i);
        if(!c)return i>0;if(c<0x20||c>0x7e||i==N-1)return false;(*out)[i]=static_cast<char>(c);}
    return false;
}
bool copy_primary_weapon_mode(std::uintptr_t base,const PreCommandSnapshot& pre,PrimaryWeaponMode* out) noexcept {
    if(out)*out={};
    if(!base||!out||!pre.native_gameplay_fields_valid||!pre.native_cg_identity||
       pre.native_cg_identity>UINTPTR_MAX-0x8a4a8)return false;
    __try {
        const auto cg=*reinterpret_cast<const std::uintptr_t*>(base+0x2bf5354);
        if(cg!=pre.native_cg_identity||*reinterpret_cast<const int*>(cg+0x8a348)!=pre.native_game_time)return false;
        const auto ps=cg+0x8a364;const auto weapon=*reinterpret_cast<const unsigned char*>(ps+0x144);
        if(!weapon)return false;
        const auto variant=*reinterpret_cast<const std::uintptr_t*>(base+0x7e19a8+sizeof(std::uintptr_t)*weapon);
        if(variant<0x10000||variant>UINT32_MAX-12)return false;
        const auto definition=*reinterpret_cast<const std::uintptr_t*>(variant+8);
        if(definition<0x10000||definition>UINT32_MAX-0x24)return false;
        PrimaryWeaponMode value{};value.weapon=weapon;
        value.type=*reinterpret_cast<const int*>(definition+0x1c);
        value.weapon_class=*reinterpret_cast<const int*>(definition+0x20);
        const auto internal_pointer=*reinterpret_cast<const std::uintptr_t*>(variant);
        const auto localized_pointer=*reinterpret_cast<const std::uintptr_t*>(variant+0xc);
        if(value.type<0||value.type>=8||value.weapon_class<0||value.weapon_class>=13||
           !copy_variant_text(internal_pointer,&value.internal_name)||
           !copy_variant_text(localized_pointer,&value.localized_name))return false;
        value.named_melee=primary_knife_variant(value.internal_name.data(),value.localized_name.data());
        value.melee=value.type==7||value.weapon_class==11||value.named_melee;
        std::array<char,64> checked_internal{},checked_localized{};
        if(*reinterpret_cast<const std::uintptr_t*>(base+0x2bf5354)!=cg||
           *reinterpret_cast<const int*>(cg+0x8a348)!=pre.native_game_time||
           *reinterpret_cast<const unsigned char*>(ps+0x144)!=weapon||
           *reinterpret_cast<const std::uintptr_t*>(base+0x7e19a8+sizeof(std::uintptr_t)*weapon)!=variant||
           *reinterpret_cast<const std::uintptr_t*>(variant+8)!=definition||
           *reinterpret_cast<const std::uintptr_t*>(variant)!=internal_pointer||
           *reinterpret_cast<const std::uintptr_t*>(variant+0xc)!=localized_pointer||
           *reinterpret_cast<const int*>(definition+0x1c)!=value.type||
           *reinterpret_cast<const int*>(definition+0x20)!=value.weapon_class||
           !copy_variant_text(internal_pointer,&checked_internal)||
           !copy_variant_text(localized_pointer,&checked_localized)||
           checked_internal!=value.internal_name||checked_localized!=value.localized_name)return false;
        *out=value;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool copy_strela_input_bytes(void*,std::uintptr_t address,void* out,std::size_t size) noexcept {
    if(!out||!strela::bounded(address,size))return false;
    __try {memcpy(out,reinterpret_cast<const void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool copy_strela_input_owner(std::uintptr_t base,const PreCommandSnapshot& pre,
                             std::uint64_t now,strela::Owner* out) noexcept {
    // The shared reader uses pinned retail addresses and validates both native
    // players, the exact lock-on asset and the ordinary first-person camera.
    return base==0x400000&&strela::copy_owner({nullptr,copy_strela_input_bytes},0,
        GetCurrentThreadId(),pre,now,out);
}
bool set_helicopter_look(std::uintptr_t base,float pitch,float yaw) noexcept {
    if(!std::isfinite(pitch)||!std::isfinite(yaw))return false;
    const float angles[]{pitch,yaw};
    __try {memcpy(reinterpret_cast<void*>(base+yaw_rva-4),angles,sizeof(angles));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct SupportSelection;
bool read_support_selection(std::uintptr_t,const PreCommandSnapshot&,SupportSelection*) noexcept;
bool copy_ladder_delta_pitch(std::uintptr_t,const PreCommandSnapshot&,float*) noexcept;
struct Adapter {
    Adapter(std::uintptr_t b,bool left,bool cycle=false,bool combined=false,bool mounted=false,
            const mounted_look::Host& mounted_host=mounted_look::production_host()):base(b),router(left),execute(reinterpret_cast<Execute>(b+execute_rva)),cycle_enabled(cycle),cycle_exact_requested(combined),mounted_enabled(mounted),mounted_source(mounted_look::make_source(mounted,mounted_host)){}
    std::uintptr_t base{};
    input::ActionRouter router;
    input::TurnController turn;
    input::KnifeSwingGesture knife_swing;
    Execute execute{};
    bool (*copy_alternate)(std::uintptr_t,const PreCommandSnapshot&,AlternateSlots*) noexcept{copy_native_alternate};
    bool (*copy_support)(std::uintptr_t,const PreCommandSnapshot&,SupportSelection*) noexcept{read_support_selection};
    input::Output controls{};
    std::uint64_t generation{}, samples{}, edges{};
    DWORD owner{};
    bool gameplay{}, override_active{}, pending_release{};
    bool knife_primary_active{};
    bool (*copy_primary_mode)(std::uintptr_t,const PreCommandSnapshot&,PrimaryWeaponMode*) noexcept{copy_primary_weapon_mode};
    bool (*copy_strela_owner)(std::uintptr_t,const PreCommandSnapshot&,std::uint64_t,strela::Owner*) noexcept{copy_strela_input_owner};
    std::uint64_t strela_reports{};
    bool cod4_controls{},shouldered{},magnified_optic_active{},smooth_turn{},button_grenades{},left_handed{};
    bool vehicle_weapon_active{},vehicle_attack_owned{};
    input::MovementOrientation movement_orientation{input::MovementOrientation::hmd};
    std::uint64_t movement_orientation_reports{},movement_orientation_rejections{};
    bool ladder_occurrence{};
    bool room_scale_enabled{},room_scale_occurrence{};
    input::room_scale::Follow room_scale_follow;
    PreCommandSnapshot room_scale_pre{};
    input::CommandRigSample room_scale_rig{};
    std::uint64_t room_scale_reports{},room_scale_collision_reports{};
    PreCommandSnapshot ladder_pre{};
    input::CommandRigSample ladder_rig{};
    std::uint64_t ladder_reports{};
    bool (*copy_ladder_delta)(std::uintptr_t,const PreCommandSnapshot&,float*) noexcept{copy_ladder_delta_pitch};
    bool swim_occurrence{};
    bool swim_turn_owned{};
    float swim_turn_yaw{};
    PreCommandSnapshot swim_pre{};
    input::CommandRigSample swim_rig{};
    swimming::Facts swim_facts{};
    std::uint64_t swim_reports{};
    bool (*copy_swimming)(std::uintptr_t,const PreCommandSnapshot&,const CommandContext&,std::uint64_t,swimming::Facts*) noexcept{swimming::copy_current};
    bool (*copy_walking)(std::uintptr_t,const PreCommandSnapshot&,const CommandContext&,std::uint64_t,swimming::Facts*) noexcept{swimming::copy_walking_current};
    std::uint64_t walking_reports{};
    bool (*copy_tunnel_crawl)(std::uintptr_t,const PreCommandSnapshot&,const CommandContext&,std::uint64_t) noexcept{tunnel_crawl::current};
    struct PendingBodyYaw {
        input::CommandRigSample rig{};
        wawvr::xr::Posef desired_anchor{};
        float expected_yaw{},desired_yaw{},delta{};
        DWORD native_thread{};
        int command_number{},native_frame_time{};
        bool valid{};
    } pending_body_yaw{};
    std::uint64_t body_yaw_reports{},body_yaw_rejections{},knife_reports{};
    std::uint64_t smooth_turn_reports{};
    bool crosshair_enabled{},crosshair_busy{},crosshair_attempted{};
    std::uint64_t crosshair_sequence{},crosshair_epoch{},crosshair_reports{};
    bool seen_command{};
    int last_command{};
    const bool cycle_enabled{};
    const bool cycle_exact_requested{};
    std::atomic<DWORD> cycle_owner{};
    std::atomic<unsigned long> cycle_revocations{};
    bool cycle_busy{}; // Only cycle_owner touches the fields below.
    unsigned long cycle_seen_revocation{};
    PreCommandSnapshot cycle_previous{};
    std::uint64_t cycle_generation{},cycle_requests{};
    std::atomic_flag aim_input_running=ATOMIC_FLAG_INIT;
    const bool mounted_enabled{};
    mounted_look::Source mounted_source;
    mounted_look::Copy mounted_copy{};
    input::CommandRigSample mounted_rig{};
    std::uint64_t mounted_generation{},mounted_reports{};
    bool mounted_active{},mounted_release_pending{},mounted_rig_valid{};
    bool monitor_active{};
    PreCommandSnapshot monitor_pre{};
    std::uint64_t monitor_diagnostic_ms{},monitor_diagnostic_seen{},monitor_diagnostic_rejections{};
    std::array<bool,10> mounted_owned_keys{};
    bool helicopter_enabled{},helicopter_active{},helicopter_release_pending{},helicopter_occurrence{};
    helicopter::Facts helicopter_facts{};
    PreCommandSnapshot helicopter_pre{};
    input::HelicopterRouter helicopter_router;
    input::HelicopterOutput helicopter_controls{};
    input::CommandRigSample helicopter_rig{};
    std::uint64_t helicopter_generation{},helicopter_look_ms{},helicopter_reports{};
    unsigned long helicopter_revision{};
    std::array<bool,10> helicopter_keys{};
    bool (*copy_helicopter)(std::uintptr_t,const PreCommandSnapshot&,const CommandContext&,std::uint64_t,helicopter::Facts*) noexcept{helicopter::copy_current};
    bool (*write_helicopter_look)(std::uintptr_t,float,float) noexcept{set_helicopter_look};
    bool motorcycle_enabled{},motorcycle_active{},motorcycle_release_pending{},motorcycle_occurrence{};
    motorcycle::Facts motorcycle_facts{};
    PreCommandSnapshot motorcycle_pre{};
    input::MotorcycleRouter motorcycle_router;
    input::MotorcycleOutput motorcycle_controls{};
    input::CommandRigSample motorcycle_rig{};
    std::uint64_t motorcycle_generation{},motorcycle_reports{},motorcycle_current_rejections{};
    unsigned long motorcycle_revision{};
    std::array<bool,9> motorcycle_keys{};
    input::VehicleLook motorcycle_look;
    bool guided_active{},guided_occurrence{};
    guided_missile::Facts guided_facts{};
    PreCommandSnapshot guided_pre{};
    input::CommandRigSample guided_rig{};
    input::GuidedMissileSteering guided_steering;
    input::GuidedMissileAxes guided_axes{};
    unsigned long guided_revision{};
    std::uint64_t guided_reports{};
    std::uint64_t motorcycle_look_reports{};
    bool (*copy_motorcycle)(std::uintptr_t,const PreCommandSnapshot&,const CommandContext&,std::uint64_t,motorcycle::Facts*) noexcept{motorcycle::copy_current};
    bool slingshot_active{};
    bool simulator_slingshot_load{},simulator_slingshot_load_attempted{};
    bool simulator_motorcycle_load{},simulator_motorcycle_load_attempted{};
    std::uintptr_t slingshot_cg{};
    std::uint64_t slingshot_epoch{},slingshot_reports{},slingshot_rejections{},slingshot_input_reports{};
    wawvr::xr::Basis3f slingshot_axis{};
    bool slingshot_aim_anchored{};
    float slingshot_anchor_pitch{},slingshot_anchor_yaw{};
    std::uint64_t slingshot_anchor_generation{};
};
Adapter* adapter{};
void report_monitor_reader_rejection(Adapter& a,const aircraft_monitor::Diagnostic& d,
                                    const PreCommandSnapshot& pre,std::uint64_t now,const char* phase) noexcept {
    ++a.monitor_diagnostic_rejections;
    const auto key=unsigned(d.reason)*10u+unsigned(d.first_reason)+unsigned(d.second_reason);
    const auto bit=std::uint64_t{1}<<key;
    // First occurrence of each bounded reason is useful; repeated failures and
    // alternating accepted/rejected commands produce at most one sample/sec.
    if((a.monitor_diagnostic_seen&bit)&&now>=a.monitor_diagnostic_ms&&now-a.monitor_diagnostic_ms<1000)return;
    a.monitor_diagnostic_seen|=bit;a.monitor_diagnostic_ms=now;
    const auto& f=d.first;const auto& s=d.second;
    const auto& p=f.predicted;const auto& v=f.authoritative;
    const auto& p2=s.predicted;const auto& v2=s.authoritative;
    trace("t5_vr_aircraft_monitor_reader_rejected",
        "phase=%s reason=%u first=%u second=%u rejects=%llu native_command=%d sequence=%llu epoch=%llu "
        "map=%p name=%p value=%p flags=0x%x type=%u cg=%p server=%p "
        "roles=%u,%u,%u,%u pre_diff=0x%x,0x%x pair_diff=0x%x,0x%x map_changed=%u storage_changed=%u "
        "pre=%d,%d,0x%x,0x%x,0x%x,0x%x "
        "p1=%d,%d,0x%x,0x%x,0x%x,0x%x,%u p1origin=%.9g,%.9g,%.9g p1clamp=%.9g,%.9g "
        "s1=%d,%d,0x%x,0x%x,0x%x,0x%x,%u s1origin=%.9g,%.9g,%.9g s1clamp=%.9g,%.9g "
        "p2=%d,%d,0x%x,0x%x,0x%x,0x%x,%u p2origin=%.9g,%.9g,%.9g p2clamp=%.9g,%.9g "
        "s2=%d,%d,0x%x,0x%x,0x%x,0x%x,%u s2origin=%.9g,%.9g,%.9g s2clamp=%.9g,%.9g",
        phase,unsigned(d.reason),unsigned(d.first_reason),unsigned(d.second_reason),a.monitor_diagnostic_rejections,
        pre.context.command_number,pre.sequence,pre.gameplay_epoch,
        reinterpret_cast<void*>(f.map),reinterpret_cast<void*>(f.name),reinterpret_cast<void*>(f.current),f.flags,f.type,
        reinterpret_cast<void*>(f.cg),reinterpret_cast<void*>(f.server),
        aircraft_monitor::role(p),aircraft_monitor::authoritative_role(v),aircraft_monitor::role(p2),aircraft_monitor::authoritative_role(v2),
        aircraft_monitor::pre_difference(p,pre),aircraft_monitor::pre_difference(p2,pre),
        aircraft_monitor::player_difference(p,p2),aircraft_monitor::player_difference(v,v2),
        f.map!=s.map||f.name!=s.name||f.current!=s.current||f.flags!=s.flags||f.type!=s.type||f.map_name!=s.map_name||f.map_value!=s.map_value,
        f.cg!=s.cg||f.server!=s.server,
        pre.native_pm_type,pre.native_health,pre.native_pm_flags,pre.native_weapon_flags,pre.native_entity_flags,pre.native_entity_flags2,
        p.pm,p.health,p.pm_flags,p.weapon_flags,p.entity_flags,p.entity_flags2,unsigned(p.primary),p.origin[0],p.origin[1],p.origin[2],p.clamp_range[0],p.clamp_range[1],
        v.pm,v.health,v.pm_flags,v.weapon_flags,v.entity_flags,v.entity_flags2,unsigned(v.primary),v.origin[0],v.origin[1],v.origin[2],v.clamp_range[0],v.clamp_range[1],
        p2.pm,p2.health,p2.pm_flags,p2.weapon_flags,p2.entity_flags,p2.entity_flags2,unsigned(p2.primary),p2.origin[0],p2.origin[1],p2.origin[2],p2.clamp_range[0],p2.clamp_range[1],
        v2.pm,v2.health,v2.pm_flags,v2.weapon_flags,v2.entity_flags,v2.entity_flags2,unsigned(v2.primary),v2.origin[0],v2.origin[1],v2.origin[2],v2.clamp_range[0],v2.clamp_range[1]);
}
void leave_knife_primary(Adapter& a) noexcept {
    if(!a.knife_primary_active)return;
    a.knife_swing.reset();
    a.knife_primary_active=false;
}
bool enabled(const wchar_t* key) noexcept {
    wchar_t value[8]{};GetEnvironmentVariableW(key,value,8);return wcscmp(value,L"1")==0;
}
bool requested(const wchar_t* key) noexcept {
    wchar_t value[16]{};const auto n=GetEnvironmentVariableW(key,value,16);
    return n&&(n>=16||wcscmp(value,L"0"));
}
void revoke_cycle(Adapter& a) noexcept {
    if(a.mounted_enabled)a.mounted_source.revoke();
    auto revision=a.cycle_revocations.load(std::memory_order_acquire);
    constexpr auto exhausted=(std::numeric_limits<unsigned long>::max)();
    while(revision!=exhausted && !a.cycle_revocations.compare_exchange_weak(
        revision,revision+1,std::memory_order_acq_rel,std::memory_order_acquire)) {}
}
// No lock spans native delivery. Nested/foreign callbacks revoke the new
// control; only the owner clears its ordinary state after the callback retires.
struct CycleScope {
    Adapter& a;
    unsigned long revision{};
    bool owned{},nested{};
    explicit CycleScope(Adapter& value,const CommandContext& c) noexcept:a(value) {
        if(!a.cycle_enabled) return;
        const DWORD thread=GetCurrentThreadId();
        auto owner=a.cycle_owner.load(std::memory_order_acquire);
        if(!owner && c.valid && c.local_client==0 && c.native_thread==thread) {
            DWORD empty{};
            a.cycle_owner.compare_exchange_strong(empty,thread,std::memory_order_acq_rel);
            owner=a.cycle_owner.load(std::memory_order_acquire);
        }
        if(owner!=thread) {revoke_cycle(a);return;}
        if(a.cycle_busy) {revoke_cycle(a);nested=true;return;}
        owned=true;a.cycle_busy=true;
        revision=a.cycle_revocations.load(std::memory_order_acquire);
        if(revision!=a.cycle_seen_revocation) {
            a.router.invalidate_weapon_cycle();a.cycle_previous={};
            a.cycle_seen_revocation=revision;
        }
        if(!c.valid || c.local_client!=0 || c.native_thread!=thread) revoke_cycle(a);
    }
    ~CycleScope() noexcept {
        if(!owned) return;
        if(!current()) {a.router.invalidate_weapon_cycle();a.cycle_previous={};}
        a.cycle_busy=false;
    }
    bool current() const noexcept {
        return owned && revision!=(std::numeric_limits<unsigned long>::max)() &&
            a.cycle_revocations.load(std::memory_order_acquire)==revision;
    }
    bool permit(const CommandContext& c,std::uint64_t generation,
                const wawvr::xr::ActionSnapshot& sample,std::uint64_t now) noexcept {
        if(!current()) return false;
        PreCommandSnapshot p{};
        const bool copied=read_pre_command_snapshot(&p);
        const auto& q=p.context;
        const bool joined=c.valid && q.valid && c.local_client==0 && q.local_client==c.local_client &&
            q.command_number==c.command_number && q.native_frame_time==c.native_frame_time &&
            q.native_thread==c.native_thread && q.key_catcher==c.key_catcher &&
            q.connection_state==c.connection_state && q.native_override==c.native_override;
        if(!copied || !joined || !p.sequence || !p.gameplay_epoch || !p.native_cg_identity ||
           !generation || !sample.sequence || !p.observed_ms || p.native_game_time<=0 ||
           !p.native_gameplay_fields_valid || !p.gameplay_active ||
           now<p.observed_ms || now-p.observed_ms>250 || !current()) {
            revoke_cycle(a);return false;
        }
        constexpr auto maximum=(std::numeric_limits<std::uint64_t>::max)();
        if(p.sequence==maximum || p.gameplay_epoch==maximum || sample.sequence==maximum ||
           generation==maximum || a.cycle_requests==maximum) {
            a.cycle_revocations.store((std::numeric_limits<unsigned long>::max)(),std::memory_order_release);
            return false;
        }
        const auto& previous=a.cycle_previous;
        if(previous.sequence && (p.sequence<=previous.sequence ||
           p.context.command_number<=previous.context.command_number ||
           p.context.native_frame_time<previous.context.native_frame_time ||
           p.native_game_time<previous.native_game_time)) {
            revoke_cycle(a);return false;
        }
        if(previous.sequence && (p.gameplay_epoch!=previous.gameplay_epoch ||
           p.native_cg_identity!=previous.native_cg_identity || generation!=a.cycle_generation))
            a.router.invalidate_weapon_cycle();
        a.cycle_previous=p;a.cycle_generation=generation;
        return current();
    }
};
bool mounted_current(Adapter& a,const CommandContext& context) noexcept {
    if(!a.mounted_enabled||!a.mounted_active||!a.mounted_source.current(a.mounted_copy.revision)||
       !command_input_generation_current(a.mounted_rig))return false;
    mounted_look::Copy current{};
    const mounted_look::Request request{context,a.mounted_copy.pre.sequence,a.mounted_copy.pre.epoch};
    return a.mounted_source.read(request,&current)==mounted_look::Status::accepted&&
        mounted_look::same_current(a.mounted_copy,current);
}
void send_edges(Adapter& a,const input::Output& output,const CommandContext& context) {
    for(unsigned i=0;i<output.edge_count;++i) {
        const auto edge=output.edges[i];
        const auto index=static_cast<unsigned>(edge.action);
        if(index>=names.size()) continue;
        if(a.mounted_enabled&&a.mounted_active&&edge.pressed&&
           (a.mounted_release_pending||!mounted_current(a,context))) {
            a.mounted_release_pending=true;invalidate_native_command_aim();break;
        }
        char command[96]{};
        // Distinct synthetic key IDs share native two-key ownership without a
        // keyless release clearing an actual keyboard/mouse key's ownership.
        const auto name=a.cod4_controls&&edge.action==input::Action::crouch?"stance":names[index];
        sprintf_s(command,"%c%s %u %d",edge.pressed?'+':'-',name,28672+index,context.native_frame_time);
        const bool mounted_delivery=a.mounted_enabled&&a.mounted_active&&edge.pressed;
        if(mounted_delivery){a.mounted_release_pending=true;a.mounted_owned_keys[index]=true;}
        a.execute(0,0,command);++a.edges;
        if(!edge.pressed)a.mounted_owned_keys[index]=false;
        if(mounted_delivery)a.mounted_release_pending=!mounted_current(a,context);
        trace("t5_vr_action","edge=%llu command=%s native_command=%d thread=%lu",a.edges,command,context.command_number,context.native_thread);
    }
    for(unsigned i=0;i<output.look_edge_count;++i) {
        const auto edge=output.look_edges[i];const auto index=static_cast<unsigned>(edge.action);
        if(index>=2||!a.mounted_enabled)continue;
        if(edge.pressed&&(a.mounted_release_pending||!mounted_current(a,context))) {a.mounted_release_pending=true;break;}
        if(!edge.pressed&&!a.mounted_owned_keys[8+index])continue;
        // Presses are consumed once. Keep uncertain key ownership until an
        // explicit keyed release returns; repeating that release is idempotent.
        if(edge.pressed){a.mounted_release_pending=true;a.mounted_owned_keys[8+index]=true;}
        char command[96]{};
        sprintf_s(command,"%c%s %u %d",edge.pressed?'+':'-',index?"lookdown":"lookup",28680+index,context.native_frame_time);
        a.execute(0,0,command);++a.edges;
        if(!edge.pressed)a.mounted_owned_keys[8+index]=false;
        else a.mounted_release_pending=!mounted_current(a,context);
        trace("t5_vr_mounted_look","command=%s native_command=%d epoch=%llu entity=%d generation=%llu thread=%lu native_pitch_writes=0",
              command,context.command_number,a.mounted_copy.pre.epoch,int(a.mounted_copy.facts.entity),a.generation,context.native_thread);
    }
}
// Vehicle cameras remap the native primary-fire key to vehicleattack. Keep a
// separate synthetic key lease so an interrupted or exited vehicle command can
// only release the press that this adapter owns.
constexpr unsigned vehicle_attack_key=28720;
void send_vehicle_weapon_edges(Adapter& a,const input::Output& output,const CommandContext& context) {
    input::Output ordinary{};
    for(unsigned i=0;i<output.edge_count;++i) {
        const auto edge=output.edges[i];
        if(edge.action!=input::Action::fire) {
            ordinary.edges[ordinary.edge_count++]=edge;
            continue;
        }
        if(!edge.pressed&&!a.vehicle_attack_owned)continue;
        char command[96]{};
        sprintf_s(command,"%cvehicleattack %u %d",edge.pressed?'+':'-',vehicle_attack_key,context.native_frame_time);
        if(edge.pressed)a.vehicle_attack_owned=true;
        a.execute(0,0,command);++a.edges;
        if(!edge.pressed)a.vehicle_attack_owned=false;
        trace("t5_vr_vehicle_weapon_action","command=%s native_command=%d thread=%lu",command,context.command_number,context.native_thread);
    }
    send_edges(a,ordinary,context);
}
void release_vehicle_weapon(Adapter& a,const CommandContext& context) {
    if(!a.vehicle_weapon_active&&!a.vehicle_attack_owned)return;
    const auto releases=a.router.release_all();
    a.vehicle_weapon_active=false;
    send_vehicle_weapon_edges(a,releases,context);
    if(a.vehicle_attack_owned) {
        char command[96]{};
        sprintf_s(command,"-vehicleattack %u %d",vehicle_attack_key,context.native_frame_time);
        a.execute(0,0,command);++a.edges;a.vehicle_attack_owned=false;
        trace("t5_vr_vehicle_weapon_action","command=%s native_command=%d thread=%lu forced_release=1",command,context.command_number,context.native_thread);
    }
}
void release_mounted_keys(Adapter& a,const CommandContext& context) {
    a.mounted_release_pending=true;
    for(unsigned index=0;index<a.mounted_owned_keys.size();++index)if(a.mounted_owned_keys[index]) {
        char command[96]{};
        const auto name=index<8?names[index]:(index==8?"lookup":"lookdown");
        sprintf_s(command,"-%s %u %d",name,28672+index,context.native_frame_time);
        a.execute(0,0,command);++a.edges;a.mounted_owned_keys[index]=false;
        trace("t5_vr_mounted_release","command=%s native_command=%d thread=%lu",command,context.command_number,context.native_thread);
    }
    a.mounted_release_pending=false;
}
bool same_mount(const mounted_look::Copy& previous,const mounted_look::Copy& current) noexcept {
    const auto& a=previous.facts;const auto& b=current.facts;
    const bool tow=mounted_look::scoped_tow_eligible(a.scoped_tow,a.entity)&&
        mounted_look::scoped_tow_eligible(b.scoped_tow,b.entity)&&a.scoped_tow==b.scoped_tow;
    const bool clamps=tow?(a.clamp_bits[2]==b.clamp_bits[2]&&a.clamp_bits[3]==b.clamp_bits[3]):a.clamp_bits==b.clamp_bits;
    return previous.source_identity==current.source_identity&&previous.revision==current.revision&&
        previous.pre.epoch==current.pre.epoch&&a.cg==b.cg&&a.ps==b.ps&&a.entity==b.entity&&
        // Native turret_shoot_internal changes FULL(1) to WEAPONJITTER(2)
        // after firing; turret_clientaim restores FULL. This recoil transition
        // retains the same mount and must not release a held trigger.
        (a.viewlock==1||a.viewlock==2)&&(b.viewlock==1||b.viewlock==2)&&
        (tow?((a.entity_flags&~0x20000U)==(b.entity_flags&~0x20000U)):a.entity_flags==b.entity_flags)&&a.entity_flags2==b.entity_flags2&&
        a.pm_type==b.pm_type&&a.pm_flags==b.pm_flags&&a.weapon_flags==b.weapon_flags&&
        a.camera==b.camera&&a.dvars==b.dvars&&clamps&&a.scoped_tow==b.scoped_tow;
}
bool select_mounted(Adapter& a,const CommandContext& context,std::uint64_t generation) noexcept {
    PreCommandSnapshot pre{};mounted_look::Copy current{};
    const bool have_pre=read_pre_command_snapshot(&pre);
    auto status=have_pre?a.mounted_source.read({context,pre.sequence,pre.gameplay_epoch},&current):mounted_look::Status::command;
    if(status==mounted_look::Status::accepted&&(!generation||generation==UINT64_MAX||
       current.pre.sequence==UINT64_MAX||current.pre.epoch==UINT64_MAX||
       (a.mounted_copy.pre.sequence&&(current.pre.sequence<=a.mounted_copy.pre.sequence||
        current.pre.epoch<a.mounted_copy.pre.epoch||
        (current.pre.epoch==a.mounted_copy.pre.epoch&&
         (current.pre.command.number<=a.mounted_copy.pre.command.number||
          current.pre.command.clock<a.mounted_copy.pre.command.clock||current.facts.game_time<a.mounted_copy.facts.game_time)))))) {
        a.mounted_source.revoke();status=mounted_look::Status::changed;
    }
    const bool accepted=status==mounted_look::Status::accepted;
    if(!accepted&&a.mounted_active)a.mounted_release_pending=true;
    if(accepted&&a.mounted_active&&(!same_mount(a.mounted_copy,current)||a.mounted_generation!=generation))
        a.mounted_release_pending=true;
    if(!accepted) {
        if(++a.mounted_reports<=12||a.mounted_reports%600==0)
            trace("t5_vr_mounted_refused","status=%u pre_sequence=%llu epoch=%llu",unsigned(status),pre.sequence,pre.gameplay_epoch);
        return false;
    }
    // Old keys have fixed IDs; the new copy never authorizes their releases.
    a.mounted_copy=current;a.mounted_generation=generation;return true;
}
bool native_input_current(const PreCommandSnapshot& pre,const CommandContext& c,std::uint64_t now) noexcept {
    const auto& p=pre.context;
    return pre.native_gameplay_fields_valid&&pre.native_health>0&&
        (pre.native_pm_type==0||pre.native_pm_type==1)&&
        !(pre.native_entity_flags&0x4300)&&!(pre.native_entity_flags2&0x40040000)&&pre.sequence&&pre.gameplay_epoch&&
        pre.native_cg_identity&&pre.observed_ms&&now>=pre.observed_ms&&now-pre.observed_ms<=100&&
        c.valid&&p.valid&&c.local_client==0&&p.local_client==c.local_client&&p.command_number==c.command_number&&
        p.native_thread==c.native_thread&&p.native_frame_time==c.native_frame_time&&p.key_catcher==c.key_catcher&&
        p.connection_state==c.connection_state&&p.native_override==c.native_override&&
        !memcmp(p.viewangles,c.viewangles,sizeof(c.viewangles))&&!memcmp(p.kickangles,c.kickangles,sizeof(c.kickangles));
}
bool basic_input_current(const PreCommandSnapshot& pre,const CommandContext& c,std::uint64_t now) noexcept {
    return !(pre.native_pm_flags&0x800)&&native_input_current(pre,c,now);
}
bool ladder_input_current(const PreCommandSnapshot& pre,const CommandContext& c,std::uint64_t now) noexcept {
    return pre.gameplay_active&&pre.native_pm_type==0&&(pre.native_pm_flags&8)&&
        c.connection_state>=9&&!c.key_catcher&&!c.native_override&&basic_input_current(pre,c,now);
}
bool ladder_rig_current(const input::CommandRigSample& rig,std::uint64_t generation,std::uint64_t now) noexcept {
    return generation&&generation==rig.xr_generation&&rig.frame.actions.sequence&&rig.frame.actions.focused&&
        rig.frame.views_valid&&rig.frame.should_render&&rig.published_ms&&now>=rig.published_ms&&
        now-rig.published_ms<=100&&command_input_generation_current(rig);
}
bool copy_ladder_delta_pitch(std::uintptr_t base,const PreCommandSnapshot& pre,float* out) noexcept {
    if(out)*out=0;
    if(!out||base<0x10000||base>UINT32_MAX-0x2bf5358||
       !ladder_input_current(pre,pre.context,GetTickCount64())||
       pre.context.native_thread!=GetCurrentThreadId())return false;
    __try {
        const auto cg=*reinterpret_cast<const std::uintptr_t*>(base+0x2bf5354);
        if(cg!=pre.native_cg_identity||cg<0x10000||cg>UINT32_MAX-0x8a52c)return false;
        const auto ps=cg+0x8a364;
        const float delta=*reinterpret_cast<const volatile float*>(ps+0x84);
        if(!std::isfinite(delta)||std::fabs(delta)>360000.f)return false;
        // Recheck the actual player state, not merely a cached ladder bit. The
        // source and retail reader agree on delta_angles at playerState+0x84.
        for(unsigned pass=0;pass<2;++pass) {
            if(*reinterpret_cast<const volatile std::uintptr_t*>(base+0x2bf5354)!=cg||
               *reinterpret_cast<const volatile int*>(cg+0x8a348)!=pre.native_game_time||
               *reinterpret_cast<const volatile int*>(ps+4)!=pre.native_pm_type||
               *reinterpret_cast<const volatile unsigned int*>(ps+0xc)!=pre.native_pm_flags||
               *reinterpret_cast<const volatile unsigned int*>(ps+0xe0)!=pre.native_entity_flags||
               *reinterpret_cast<const volatile unsigned int*>(ps+0xe4)!=pre.native_entity_flags2||
               *reinterpret_cast<const volatile int*>(ps+0x1c4)!=pre.native_health||
               *reinterpret_cast<const volatile float*>(ps+0x84)!=delta)return false;
        }
        *out=delta;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::int32_t ladder_command_pitch(float delta) noexcept {
    // Stock climbing multiplies forwardmove by (forward.z + .25) * 2.5.
    // A fixed upward pitch makes stick-up climb and stick-down descend even
    // when the desktop mouse last looked at the floor. Compensate the current
    // native delta without changing global view angles or playerState itself.
    const float degrees=std::remainder(-18.f-delta,360.f);
    return static_cast<std::int32_t>(std::lround(degrees*(65536.f/360.f)))&0xffff;
}
bool vehicle_weapon_input_current(const PreCommandSnapshot& pre,const CommandContext& c,std::uint64_t now) noexcept {
    // EF_VEHICLE_ACTIVE is the engine's own BG_UsingVehicleWeapon selector.
    // The user's Cuba anti-air gun is PM_NORMAL_LINKED with this flag and uses
    // CAM_VEHICLE; it intentionally fails the ordinary-weapon/mounted readers.
    const auto& p=pre.context;
    return pre.native_gameplay_fields_valid&&pre.native_health>0&&pre.native_pm_type==1&&
        !(pre.native_pm_flags&0x800)&&!(pre.native_weapon_flags&0x180)&&
        (pre.native_entity_flags&0x4000)&&!(pre.native_entity_flags&0x300)&&
        !(pre.native_entity_flags2&0x40040000)&&pre.sequence&&pre.gameplay_epoch&&
        pre.native_cg_identity&&pre.observed_ms&&now>=pre.observed_ms&&now-pre.observed_ms<=100&&
        c.valid&&p.valid&&c.local_client==0&&p.local_client==c.local_client&&p.command_number==c.command_number&&
        p.native_thread==c.native_thread&&p.native_frame_time==c.native_frame_time&&p.key_catcher==c.key_catcher&&
        p.connection_state==c.connection_state&&p.native_override==c.native_override&&
        c.connection_state>=9&&!c.key_catcher&&!c.native_override&&
        !memcmp(p.viewangles,c.viewangles,sizeof(c.viewangles))&&!memcmp(p.kickangles,c.kickangles,sizeof(c.kickangles));
}
void service_crosshair_preference(Adapter& a,const CommandContext& context) {
    if(!a.crosshair_enabled||a.crosshair_busy||!a.execute||!a.gameplay||a.override_active||
       a.owner!=GetCurrentThreadId()||context.native_thread!=a.owner||context.key_catcher||context.native_override||
       context.connection_state<9)return;
    PreCommandSnapshot pre{};
    if(!read_pre_command_snapshot(&pre)||!pre.gameplay_active||!basic_input_current(pre,context,GetTickCount64())||
       pre.gameplay_epoch<a.crosshair_epoch||(pre.gameplay_epoch==a.crosshair_epoch&&pre.sequence<=a.crosshair_sequence))return;
    if(a.crosshair_epoch!=pre.gameplay_epoch)a.crosshair_attempted=false;
    a.crosshair_epoch=pre.gameplay_epoch;a.crosshair_sequence=pre.sequence;
    CrosshairDvar before{},after{};
    if(!copy_crosshair_dvar(a.base,&before))return;
    if(!before.current){a.crosshair_attempted=false;return;}
    if(a.crosshair_attempted)return; // a rejected native setter is not a per-frame retry loop
    // The native command parser performs a fresh name lookup and honors its
    // normal external-setter rules. No cached native pointer is written by us.
    a.crosshair_attempted=a.crosshair_busy=true;
    __try {a.execute(0,0,"cg_drawCrosshair 0");}
    __finally {a.crosshair_busy=false;}
    const bool after_known=copy_crosshair_dvar(a.base,&after),verified=after_known&&!after.current;
    if(verified)a.crosshair_attempted=false;
    if(++a.crosshair_reports<=12||a.crosshair_reports%120==0)
        trace("t5_vr_crosshair_preference","verified=%u command=%d sequence=%llu epoch=%llu native_dispatch=1 before=1 after_known=%u after=%u native_instruction_patches=0 direct_dvar_writes=0",
            verified,context.command_number,pre.sequence,pre.gameplay_epoch,after_known,unsigned(after.current));
}
bool scripted_use_current(const PreCommandSnapshot& pre,const CommandContext& c,std::uint64_t now) noexcept {
    // Campaign struggles freeze movement while the script still polls native
    // activate, jump or melee buttons. Admit those buttons; do not unfreeze native gameplay or
    // let this exception authorize locomotion, weapon aim, fire or cycling.
    return (pre.native_pm_flags&0x800)&&c.connection_state>=9&&!c.key_catcher&&!c.native_override&&
        native_input_current(pre,c,now);
}
bool scripted_linked_current(const PreCommandSnapshot& pre,const CommandContext& c,std::uint64_t now) noexcept {
    // Alive linked scripts with disabled weapons can poll attack/ADS (the Cuba
    // car), while others still consume movement (carrying Hudson). This is not
    // a vehicle detector and never changes native movement or weapon policy.
    return pre.native_pm_type==1&&(pre.native_weapon_flags&0x80)&&
        c.connection_state>=9&&!c.key_catcher&&!c.native_override&&basic_input_current(pre,c,now);
}
bool scripted_attack_current(const PreCommandSnapshot& pre,const CommandContext& c,std::uint64_t now) noexcept {
    // Frozen linked scenes can poll AttackButtonPressed even with weapons
    // disabled (Dragovich's grab prompt). Only deliver the native button;
    // preserve DisableWeapons, the freeze and all weapon/aim permissions.
    return pre.native_pm_type==1&&(pre.native_weapon_flags&0x80)&&scripted_use_current(pre,c,now);
}
bool copy_slingshot_current(std::uintptr_t base,const PreCommandSnapshot& pre,
                            const CommandContext& c,slingshot::Facts* out) noexcept {
    if(out)*out={};
    if(!base||!out||!pre.native_gameplay_fields_valid||!pre.sequence||
       !c.valid||c.local_client||pre.context.command_number!=c.command_number||
       pre.context.native_frame_time!=c.native_frame_time||pre.context.native_thread!=c.native_thread||
       c.native_thread!=GetCurrentThreadId())return false;
    slingshot::Facts value{};value.pre=pre;
    __try {
        value.cg=*reinterpret_cast<const std::uintptr_t*>(base+0x2bf5354);
        if(value.cg!=pre.native_cg_identity||value.cg<0x10000||value.cg>UINT32_MAX-0x8c160)return false;
        const auto ps=value.cg+0x8a364;value.refdef=value.cg+0x8c100;
        value.native_time=*reinterpret_cast<const int*>(value.cg+0x8a348);
        value.refdef_time=*reinterpret_cast<const int*>(value.cg+0x8c158);
        value.primary=*reinterpret_cast<const unsigned char*>(ps+0x144);
        value.offhand=*reinterpret_cast<const int*>(ps+0x134);
        memcpy(value.delta_angles,reinterpret_cast<const void*>(ps+0x84),sizeof(value.delta_angles));
        memcpy(value.clamp_base,reinterpret_cast<const void*>(ps+0x1a0),sizeof(value.clamp_base));
        memcpy(value.clamp_range,reinterpret_cast<const void*>(ps+0x1a8),sizeof(value.clamp_range));
        memcpy(&value.origin,reinterpret_cast<const void*>(value.refdef+0x20),sizeof(value.origin));
        memcpy(&value.axis,reinterpret_cast<const void*>(value.refdef+0x34),sizeof(value.axis));
        if(value.native_time<=0||value.native_time!=pre.native_game_time||value.refdef_time!=value.native_time||
           !slingshot::emplacement_volume(value)||!slingshot::native_aim_valid(value))return false;
        wawvr::xr::Vec3f checked_origin{};wawvr::xr::Basis3f checked_axis{};
        float checked_delta[3]{},checked_base[2]{},checked_range[2]{};
        memcpy(&checked_origin,reinterpret_cast<const void*>(value.refdef+0x20),sizeof(checked_origin));
        memcpy(&checked_axis,reinterpret_cast<const void*>(value.refdef+0x34),sizeof(checked_axis));
        memcpy(checked_delta,reinterpret_cast<const void*>(ps+0x84),sizeof(checked_delta));
        memcpy(checked_base,reinterpret_cast<const void*>(ps+0x1a0),sizeof(checked_base));
        memcpy(checked_range,reinterpret_cast<const void*>(ps+0x1a8),sizeof(checked_range));
        if(*reinterpret_cast<const std::uintptr_t*>(base+0x2bf5354)!=value.cg||
           *reinterpret_cast<const int*>(value.cg+0x8a348)!=value.native_time||
           *reinterpret_cast<const int*>(value.cg+0x8c158)!=value.refdef_time||
           *reinterpret_cast<const unsigned char*>(ps+0x144)!=value.primary||
           *reinterpret_cast<const int*>(ps+0x134)!=value.offhand||
           memcmp(checked_delta,value.delta_angles,sizeof(checked_delta))||
           memcmp(checked_base,value.clamp_base,sizeof(checked_base))||
           memcmp(checked_range,value.clamp_range,sizeof(checked_range))||
           memcmp(&checked_origin,&value.origin,sizeof(value.origin))||
           memcmp(&checked_axis,&value.axis,sizeof(value.axis)))return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    *out=value;return true;
}
unsigned current_alternate_slot(Adapter& a,const CycleScope& cycle,const CommandContext& context) noexcept {
    const auto& alternate_pre=a.cycle_previous;
    const auto current=[&](PreCommandSnapshot& p) noexcept {
        return cycle.current()&&read_pre_command_snapshot(&p)&&p.gameplay_active&&
            p.sequence==alternate_pre.sequence&&p.gameplay_epoch==alternate_pre.gameplay_epoch&&
            p.native_cg_identity==alternate_pre.native_cg_identity&&p.native_game_time==alternate_pre.native_game_time&&
            basic_input_current(p,context,GetTickCount64())&&cycle.current();
    };
    PreCommandSnapshot pre{};AlternateSlots first{},second{};
    if(!current(pre)||!a.copy_alternate(a.base,pre,&first))return 0;
    const auto slot=unique_alternate_slot(first);
    if(!slot) {
        trace("t5_vr_alternate_refused","reason=no_unique_alt_slot types=%d,%d,%d,%d native_command=%d",
              first.types[0],first.types[1],first.types[2],first.types[3],context.command_number);
        return 0;
    }
    if(!current(pre)||!a.copy_alternate(a.base,pre,&second)||first.types!=second.types||
       first.weapon!=second.weapon||first.requested!=second.requested||!cycle.current()||
       manual_reload_uses_offhand())return 0;
    return slot;
}
struct SupportSelection {
    std::uintptr_t cg{},server{};
    unsigned char held{};
    support_weapon::Descriptor descriptor{};
    friend bool operator==(const SupportSelection&,const SupportSelection&)=default;
};
bool read_support_selection(std::uintptr_t base,const PreCommandSnapshot& pre,SupportSelection* out) noexcept {
    if(base!=0x400000||!out||!pre.gameplay_active)return false;
    __try {
        const auto u=[](std::uintptr_t at){std::uint32_t v{};memcpy(&v,reinterpret_cast<const void*>(at),4);return v;};
        const auto byte=[](std::uintptr_t at){unsigned char v{};memcpy(&v,reinterpret_cast<const void*>(at),1);return v;};
        SupportSelection s{};s.cg=u(0x2ff5354);s.server=u(0x1c03140);
        if(s.cg!=pre.native_cg_identity||u(s.cg+0x8a348)!=static_cast<unsigned>(pre.native_game_time)||
           s.server<0x10000||s.server!=u(0x1a796f8+0x13c)||
           u(0x1a796f8)||u(0x1a796f8+0x298))return false;
        const auto ps=s.cg+0x8a364;
        s.held=byte(ps+0x144);
        if(!s.held||s.held!=byte(s.server+0x144)||s.held!=u(s.cg+0xa9be8)||
           u(ps+4)||u(s.server+4)||byte(ps+0x130)||byte(s.server+0x130)||
           static_cast<int>(u(ps+0x1c4))<=0||static_cast<int>(u(s.server+0x1c4))<=0||
           u(ps+0x50c)!=1||u(s.server+0x50c)!=1||u(ps+0x51c)!=u(s.server+0x51c))return false;
        if(!support_weapon::read_descriptor(helicopter::detail::protected_copy,nullptr,u(ps+0x51c),&s.descriptor))return false;
        if(u(s.cg+0x8a348)!=static_cast<unsigned>(pre.native_game_time))return false;
        *out=s;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool current_support_selection(Adapter& a,const CycleScope& cycle,const CommandContext& context,SupportSelection* out) noexcept {
    if(!out)return false;*out={};
    const auto current=[&](PreCommandSnapshot& p) noexcept {
        return cycle.current()&&read_pre_command_snapshot(&p)&&p.gameplay_active&&
            p.sequence==a.cycle_previous.sequence&&p.gameplay_epoch==a.cycle_previous.gameplay_epoch&&
            p.native_cg_identity==a.cycle_previous.native_cg_identity&&p.native_game_time==a.cycle_previous.native_game_time&&
            basic_input_current(p,context,GetTickCount64());
    };
    PreCommandSnapshot pre{};SupportSelection first{},last{};
    if(!current(pre)||!a.copy_support(a.base,pre,&first)||!current(pre)||
       !a.copy_support(a.base,pre,&last)||first!=last||!current(pre))return false;
    *out=last;return true;
}
bool set_native_yaw(std::uintptr_t base,float value) noexcept {
    __try {memcpy(reinterpret_cast<void*>(base+yaw_rva),&value,4);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// The Hind script reads attack for HUD/audio. Driver weapon discharge has its
// own vehicleattack bit; both keys belong to the same physical trigger lease.
constexpr std::array<const char*,10> helicopter_names{"attack","throw","vehiclemoveup","vehiclemovedown","reload","activate","vehicleattack","gas","reverse","vehicleattacksecond"};
void release_helicopter_keys(Adapter& a,const CommandContext& context) {
    a.helicopter_release_pending=true;a.helicopter_occurrence=false;
    a.helicopter_controls={};a.helicopter_router.reset();a.helicopter_look_ms=0;
    for(unsigned i=0;i<a.helicopter_keys.size();++i)if(a.helicopter_keys[i]) {
        char command[96]{};sprintf_s(command,"-%s %u %d",helicopter_names[i],28700+i,context.native_frame_time);
        a.execute(0,0,command);a.helicopter_keys[i]=false;
        trace("t5_vr_helicopter_action","command=%s native_command=%d release=1",command,context.command_number);
    }
    a.helicopter_release_pending=false;
}
bool helicopter_current(Adapter& a,const CommandContext& c) noexcept {
    const auto& p=a.helicopter_pre.context;
    if(!a.helicopter_enabled||!a.helicopter_active||!a.helicopter_occurrence||
       a.helicopter_release_pending||!c.valid||c.local_client||c.key_catcher||c.native_override||
       c.connection_state<9||c.native_thread!=a.owner||GetCurrentThreadId()!=a.owner||
       c.command_number!=p.command_number||c.native_frame_time!=p.native_frame_time||
       c.connection_state!=p.connection_state||c.native_thread!=p.native_thread||
       a.helicopter_revision!=a.cycle_revocations.load(std::memory_order_acquire)||
       !command_input_generation_current(a.helicopter_rig))return false;
    helicopter::Facts facts{};
    // Native CreateCmd legitimately changes view angles. Join its non-angle
    // occurrence, then validate the exact retained pre-publication and graph.
    return a.copy_helicopter(a.base,a.helicopter_pre,p,GetTickCount64(),&facts)&&
        helicopter::same_vehicle(a.helicopter_facts,facts)&&
        a.helicopter_revision==a.cycle_revocations.load(std::memory_order_acquire);
}
bool service_helicopter(Adapter& a,const CycleScope& cycle,const CommandContext& context,
                       const wawvr::xr::ActionSnapshot& sample,std::uint64_t generation,bool live) {
    if(!a.helicopter_enabled)return false;
    const auto now=GetTickCount64();PreCommandSnapshot pre{};helicopter::Facts facts{};
    const bool selected=cycle.current()&&a.gameplay&&live&&generation&&generation!=UINT64_MAX&&
        generation==a.helicopter_rig.xr_generation&&command_input_generation_current(a.helicopter_rig)&&
        !(sample.menu.active&&sample.menu.current)&&read_pre_command_snapshot(&pre)&&
        a.copy_helicopter(a.base,pre,context,now,&facts);
    const bool previous=a.helicopter_active;
    const bool progression=!previous||(pre.sequence>a.helicopter_pre.sequence&&
        pre.context.command_number>a.helicopter_pre.context.command_number&&
        pre.context.native_frame_time>=a.helicopter_pre.context.native_frame_time&&
        pre.native_game_time>=a.helicopter_pre.native_game_time);
    const bool changed=previous&&(!selected||!progression||a.helicopter_generation!=generation||
        pre.gameplay_epoch!=a.helicopter_pre.gameplay_epoch||
        !helicopter::same_vehicle(a.helicopter_facts,facts)||
        a.helicopter_revision!=cycle.revision);
    if(changed||a.helicopter_release_pending||selected!=previous) {
        a.helicopter_active=false;
        release_helicopter_keys(a,context);
        a.mounted_active=false;
        if(a.vehicle_weapon_active||a.vehicle_attack_owned)release_vehicle_weapon(a,context);
        else send_edges(a,a.router.release_all(),context);
        if(a.mounted_enabled)release_mounted_keys(a,context);
        a.router.invalidate_weapon_cycle();a.cycle_previous={};a.turn.reset();invalidate_native_command_aim();
        trace("t5_vr_helicopter_context","selected=%u previous=%u changed=%u command=%d entity=%d seat=%d type=%d flags=0x%x generation=%llu scripted_huey=%u flight=%u",
            selected,previous,changed,context.command_number,facts.entity_index,facts.predicted.vehicle_pos,
            facts.info_type,facts.vehicle_flags,generation,facts.scripted_huey,helicopter::flight_control_allowed(facts));
    }
    a.helicopter_active=selected&&progression&&cycle.current();
    if(!a.helicopter_active) {
        a.helicopter_router.update(sample,false,now);a.helicopter_pre={};return false;
    }
    a.helicopter_pre=pre;a.helicopter_facts=facts;a.helicopter_generation=generation;
    a.helicopter_revision=cycle.revision;a.helicopter_occurrence=true;
    a.gameplay=false;a.controls={};invalidate_native_command_aim();
    a.helicopter_controls=a.helicopter_router.update(sample,true,now);
    // Redemption's opening path retains native flight ownership. Its script
    // accepts gun inputs independently; never turn those into pilot movement.
    if(!helicopter::flight_control_allowed(facts)) {
        a.helicopter_controls.axes={};
        a.helicopter_controls.held[static_cast<unsigned>(input::HelicopterAction::ascend)]=false;
        a.helicopter_controls.held[static_cast<unsigned>(input::HelicopterAction::descend)]=false;
    }
    auto driver_keys=bovr::input::driver_keys(a.helicopter_controls,facts.info_type);
    // Redemption's script fires gunner1 from attack. Its driver-controlled
    // gunner0 consumes button35 (vehicleattacksecond), unlike Hind button34.
    if(facts.scripted_huey){driver_keys[9]=driver_keys[6];driver_keys[6]=false;}
    for(unsigned i=0;i<a.helicopter_keys.size();++i) {
        const bool wanted=driver_keys[i];
        if(a.helicopter_keys[i]==wanted)continue;
        if(wanted&&!helicopter_current(a,context)){a.helicopter_release_pending=true;break;}
        char command[96]{};sprintf_s(command,"%c%s %u %d",wanted?'+':'-',helicopter_names[i],28700+i,context.native_frame_time);
        // Retain uncertain down ownership until a balanced release returns.
        a.helicopter_release_pending=true;
        if(wanted)a.helicopter_keys[i]=true;
        a.execute(0,0,command);
        if(!wanted)a.helicopter_keys[i]=false;
        a.helicopter_release_pending=false;
        trace("t5_vr_helicopter_action","command=%s native_command=%d sequence=%llu entity=%d",command,context.command_number,pre.sequence,facts.entity_index);
        if(!helicopter_current(a,context)){a.helicopter_release_pending=true;break;}
    }
    if(a.helicopter_release_pending||!a.helicopter_controls.valid||!helicopter_current(a,context)) {
        release_helicopter_keys(a,context);return true;
    }
    const auto& axes=a.helicopter_controls.axes;
    const float dt=a.helicopter_look_ms&&now>=a.helicopter_look_ms?
        static_cast<float>((std::min)(now-a.helicopter_look_ms,std::uint64_t{50}))*0.001f:0.f;
    a.helicopter_look_ms=now;
    if(dt>0&&(axes.pitch!=0||axes.yaw!=0)&&helicopter_current(a,context)) {
        // Same native angle producer slots as mouse/gamepad, before original
        // CreateCmd applies vehicle look limits. Raw input angles include native
        // delta angles, so do not apply an absolute world-pitch clamp here.
        const float pitch=std::remainder(context.viewangles[0]-axes.pitch*70.f*dt,360.f);
        const float yaw=std::remainder(context.viewangles[1]-axes.yaw*90.f*dt,360.f);
        if(!a.write_helicopter_look(a.base,pitch,yaw))release_helicopter_keys(a,context);
        else if(++a.helicopter_reports<=12||a.helicopter_reports%120==0)
            trace("t5_vr_helicopter_look","pitch=%.3f yaw=%.3f dt=%.3f native_command=%d",pitch,yaw,dt,context.command_number);
    }
    ++a.samples;return true;
}

constexpr std::array<const char*,9> motorcycle_names{
    "gas","reverse","vehicleattack","vehicleattacksecond","handbrake","reload","activate","attack","frag"};
void release_motorcycle_keys(Adapter& a,const CommandContext& context) {
    a.motorcycle_release_pending=true;a.motorcycle_occurrence=false;
    a.motorcycle_controls={};a.motorcycle_router.reset();
    a.motorcycle_look.reset();
    for(unsigned i=0;i<a.motorcycle_keys.size();++i)if(a.motorcycle_keys[i]) {
        char command[96]{};
        sprintf_s(command,"-%s %u %d",motorcycle_names[i],28720+i,context.native_frame_time);
        a.execute(0,0,command);a.motorcycle_keys[i]=false;
        trace("t5_vr_motorcycle_action","command=%s native_command=%d release=1",command,context.command_number);
    }
    a.motorcycle_release_pending=false;
}
bool motorcycle_current(Adapter& a,const CommandContext& c) noexcept {
    const auto& p=a.motorcycle_pre.context;
    unsigned reason{};
    if(!a.motorcycle_enabled)reason=1;
    else if(!a.motorcycle_active)reason=2;
    else if(!a.motorcycle_occurrence)reason=3;
    else if(a.motorcycle_release_pending)reason=4;
    else if(!c.valid)reason=5;
    else if(c.local_client)reason=6;
    else if(c.key_catcher)reason=7;
    else if(c.native_override)reason=8;
    else if(c.connection_state<9)reason=9;
    else if(c.native_thread!=a.owner)reason=10;
    else if(GetCurrentThreadId()!=a.owner)reason=11;
    else if(c.command_number!=p.command_number)reason=12;
    else if(c.native_frame_time!=p.native_frame_time)reason=13;
    else if(c.connection_state!=p.connection_state)reason=14;
    else if(c.native_thread!=p.native_thread)reason=15;
    else if(a.motorcycle_revision!=a.cycle_revocations.load(std::memory_order_acquire))reason=16;
    else if(!command_input_generation_current(a.motorcycle_rig))reason=17;
    if(reason) {
        const auto count=++a.motorcycle_current_rejections;
        if(count<=12||count%120==0)
            trace("t5_vr_motorcycle_current_rejected","reason=%u count=%llu command=%d pre_command=%d generation=%llu revision=%lu current_revision=%lu occurrence=%u active=%u pending=%u",
                reason,count,c.command_number,p.command_number,a.motorcycle_generation,a.motorcycle_revision,
                a.cycle_revocations.load(std::memory_order_acquire),a.motorcycle_occurrence,a.motorcycle_active,a.motorcycle_release_pending);
        return false;
    }
    motorcycle::Facts facts{};
    const bool copied=a.copy_motorcycle(a.base,a.motorcycle_pre,p,GetTickCount64(),&facts);
    const bool bridged=!copied&&motorcycle::retained_transition(
        a.motorcycle_pre,a.motorcycle_facts.cg,a.motorcycle_facts.pre.gameplay_epoch);
    if(bridged)facts=motorcycle::transition_facts(a.motorcycle_facts,a.motorcycle_pre);
    const bool eligible=(copied||bridged)&&motorcycle::eligible(
        facts,a.motorcycle_facts.cg,a.motorcycle_facts.pre.gameplay_epoch);
    const bool same=eligible&&motorcycle::same_motorcycle(a.motorcycle_facts,facts);
    const bool revision=same&&a.motorcycle_revision==a.cycle_revocations.load(std::memory_order_acquire);
    if(!revision) {
        const auto count=++a.motorcycle_current_rejections;
        if(count<=12||count%120==0)
            trace("t5_vr_motorcycle_current_rejected","reason=%u count=%llu command=%d sequence=%llu copied=%u bridged=%u eligible=%u same=%u revision=%u game_time=%d vehicle=%d",
                copied?(eligible?(same?21u:20u):19u):18u,count,c.command_number,a.motorcycle_pre.sequence,
                copied,bridged,eligible,same,revision,facts.native_time,int(facts.vehicle_index));
    }
    return revision;
}
bool service_motorcycle(Adapter& a,const CycleScope& cycle,const CommandContext& context,
                        const wawvr::xr::ActionSnapshot& sample,std::uint64_t generation,bool live) {
    if(!a.motorcycle_enabled)return false;
    const auto now=GetTickCount64();PreCommandSnapshot pre{};motorcycle::Facts facts{};
    const bool sample_current=cycle.current()&&(a.gameplay||a.motorcycle_active)&&live&&
        generation&&generation!=UINT64_MAX&&
        generation==a.motorcycle_rig.xr_generation&&command_input_generation_current(a.motorcycle_rig)&&
        !(sample.menu.active&&sample.menu.current);
    const bool pre_read=sample_current&&read_pre_command_snapshot(&pre);
    const bool previous=a.motorcycle_active;
    const bool copied=pre_read&&a.copy_motorcycle(a.base,pre,context,now,&facts);
    const bool bridged=!copied&&previous&&motorcycle::retained_transition(
        pre,a.motorcycle_facts.cg,a.motorcycle_facts.pre.gameplay_epoch);
    if(bridged)facts=motorcycle::transition_facts(a.motorcycle_facts,pre);
    const bool snapshot_gap=!pre_read&&sample_current&&previous&&
        motorcycle::retained_snapshot_gap(a.motorcycle_facts,context,now);
    if(snapshot_gap)facts=a.motorcycle_facts;
    const bool selected=(copied||bridged||snapshot_gap)&&motorcycle::eligible(facts,
        previous?a.motorcycle_facts.cg:0,previous?a.motorcycle_facts.pre.gameplay_epoch:0);
    const bool progression=snapshot_gap||!previous||(pre.sequence>a.motorcycle_pre.sequence&&
        pre.context.command_number>a.motorcycle_pre.context.command_number&&
        pre.context.native_frame_time>=a.motorcycle_pre.context.native_frame_time&&
        pre.native_game_time>=a.motorcycle_pre.native_game_time);
    const bool changed=previous&&(!selected||!progression||a.motorcycle_generation!=generation||
        !motorcycle::same_motorcycle(a.motorcycle_facts,facts)||a.motorcycle_revision!=cycle.revision);
    if(changed||a.motorcycle_release_pending||selected!=previous) {
        a.motorcycle_active=false;
        release_motorcycle_keys(a,context);
        if(a.vehicle_weapon_active||a.vehicle_attack_owned)release_vehicle_weapon(a,context);
        else send_edges(a,a.router.release_all(),context);
        a.router.invalidate_weapon_cycle();a.cycle_previous={};a.turn.reset();
        trace("t5_vr_motorcycle_context","selected=%u previous=%u changed=%u copied=%u bridged=%u snapshot_gap=%u command=%d sequence=%llu epoch=%llu pre_valid=%u pre_cg=0x%llx pre_time=%d pre_health=%d pre_pm=%d pre_pm_flags=0x%x pre_weapon_flags=0x%x pre_eflags=0x%x pm=%d pm_flags=0x%x weapon_flags=0x%x eflags=0x%x vehicle=%d name=%s origin=%.3f,%.3f,%.3f generation=%llu",
            selected,previous,changed,copied,bridged,snapshot_gap,context.command_number,pre.sequence,pre.gameplay_epoch,
            pre.native_gameplay_fields_valid,static_cast<unsigned long long>(pre.native_cg_identity),
            pre.native_game_time,pre.native_health,pre.native_pm_type,pre.native_pm_flags,
            pre.native_weapon_flags,pre.native_entity_flags,
            facts.pm_type,facts.pm_flags,facts.weapon_flags,facts.entity_flags,int(facts.vehicle_index),
            facts.vehicle_name.data(),facts.origin.x,facts.origin.y,facts.origin.z,generation);
    }
    a.motorcycle_active=selected&&progression&&cycle.current();
    if(!a.motorcycle_active) {
        a.motorcycle_router.update(sample,false,now);a.motorcycle_pre={};return false;
    }
    if(snapshot_gap) {
        // Preserve the already-held native leases for this bounded reader gap.
        // The next coherent snapshot must revalidate the same occurrence before
        // any new edge can be issued.
        a.motorcycle_occurrence=true;a.gameplay=false;a.controls={};a.turn.reset();
        ++a.samples;return true;
    }
    a.motorcycle_pre=pre;a.motorcycle_facts=facts;a.motorcycle_generation=generation;
    a.motorcycle_revision=cycle.revision;a.motorcycle_occurrence=true;
    a.gameplay=false;a.controls={};a.turn.reset();
    a.motorcycle_controls=a.motorcycle_router.update(sample,true,now);
    if(motorcycle::vehicle_gunner(facts)) {
        // The truck/APC driver is scripted; the seated player's stick must never
        // acquire gas, reverse, handbrake or steering authority.
        a.motorcycle_controls.drive=a.motorcycle_controls.steer=0;
        a.motorcycle_controls.held[0]=a.motorcycle_controls.held[1]=a.motorcycle_controls.held[4]=false;
    }
    std::array<bool,9> native_keys{};
    std::copy(a.motorcycle_controls.held.begin(),a.motorcycle_controls.held.end(),native_keys.begin());
    const bool apc=motorcycle::rebirth_apc_gunner(facts);
    if(apc) {
        // The seated BTR machine gun consumes attack. Its grenade script polls
        // FragButtonPressed, while the vehicle follows its native rail path.
        // Separate slots keep releases correct even after leaving this seat.
        native_keys.fill(false);
        native_keys[7]=a.motorcycle_controls.held[7];
        native_keys[8]=a.motorcycle_controls.held[3];
    }
    for(unsigned i=0;i<a.motorcycle_keys.size();++i) {
        const bool wanted=a.motorcycle_controls.valid&&native_keys[i];
        if(a.motorcycle_keys[i]==wanted)continue;
        if(wanted&&!motorcycle_current(a,context)){a.motorcycle_release_pending=true;break;}
        char command[96]{};
        sprintf_s(command,"%c%s %u %d",wanted?'+':'-',motorcycle_names[i],28720+i,context.native_frame_time);
        a.motorcycle_release_pending=true;
        if(wanted)a.motorcycle_keys[i]=true;
        a.execute(0,0,command);
        if(!wanted)a.motorcycle_keys[i]=false;
        a.motorcycle_release_pending=false;
        trace("t5_vr_motorcycle_action","command=%s native_command=%d sequence=%llu vehicle=%d name=%s seat=%d rebirth_apc=%u",
            command,context.command_number,pre.sequence,int(facts.vehicle_index),facts.vehicle_name.data(),facts.vehicle_pos,apc);
        if(!motorcycle_current(a,context)){a.motorcycle_release_pending=true;break;}
    }
    if(a.motorcycle_release_pending||!a.motorcycle_controls.valid||!motorcycle_current(a,context)) {
        release_motorcycle_keys(a,context);return true;
    }
    // Bike shots use the guarded scripted-shot seam; turning the camera here
    // cannot overcome its zero native view clamp and would couple aim to riding.
    if(motorcycle::vehicle_gunner(facts)) {
        constexpr wawvr::xr::Basis3f fixed_axis{{1,0,0},{0,1,0},{0,0,1}};
        input::CommandAimProposal proposal{};input::VehicleLookDelta look{};
        const auto hand=a.left_handed?wawvr::xr::Hand::Left:wawvr::xr::Hand::Right;
        const auto status=input::propose_native_command_aim(a.motorcycle_rig,sample,fixed_axis,hand,now,&proposal,false);
        if(status!=input::CommandAimStatus::accepted) a.motorcycle_look.reset();
        else if(a.motorcycle_look.update(sample.sequence,proposal.pitch_degrees,proposal.yaw_degrees,&look)&&
                (look.pitch!=0.f||look.yaw!=0.f)&&motorcycle_current(a,context)) {
            const float pitch=std::remainder(context.viewangles[0]+look.pitch,360.f);
            const float yaw=std::remainder(context.viewangles[1]+look.yaw,360.f);
            const bool written=a.write_helicopter_look(a.base,pitch,yaw);
            const auto report=++a.motorcycle_look_reports;
            if(report<=12||report%120==0)
                trace("t5_vr_vehicle_controller_aim","written=%u pitch_delta=%.3f yaw_delta=%.3f pitch=%.3f yaw=%.3f vehicle=%d seat=%d command=%d xr_sequence=%llu name=%s rebirth_apc=%u",
                    written,look.pitch,look.yaw,pitch,yaw,int(facts.vehicle_index),facts.vehicle_pos,context.command_number,sample.sequence,facts.vehicle_name.data(),apc);
            if(!written)a.motorcycle_look.reset();
        }
    } else a.motorcycle_look.reset();
    if(++a.motorcycle_reports<=12||a.motorcycle_reports%120==0)
        trace("t5_vr_motorcycle_input","drive=%.3f steer=%.3f gas=%u reverse=%u fire=%u secondary=%u handbrake=%u reload=%u use=%u native_command=%d vehicle=%d name=%s rebirth_apc=%u",
            a.motorcycle_controls.drive,a.motorcycle_controls.steer,
            a.motorcycle_controls.held[0],a.motorcycle_controls.held[1],a.motorcycle_controls.held[2],
            a.motorcycle_controls.held[3],a.motorcycle_controls.held[4],a.motorcycle_controls.held[5],
            a.motorcycle_controls.held[6],context.command_number,int(facts.vehicle_index),facts.vehicle_name.data(),apc);
    ++a.samples;return true;
}
bool guided_current(Adapter& a,const CommandContext& c) noexcept {
    const auto& p=a.guided_pre.context;
    if(!a.guided_active||!a.guided_occurrence||!c.valid||c.local_client||c.key_catcher||c.native_override||
       c.connection_state<9||c.native_thread!=a.owner||GetCurrentThreadId()!=a.owner||
       c.command_number!=p.command_number||c.native_frame_time!=p.native_frame_time||
       c.connection_state!=p.connection_state||a.guided_revision!=a.cycle_revocations.load(std::memory_order_acquire)||
       !command_input_generation_current(a.guided_rig))return false;
    guided_missile::Facts current{};
    return guided_missile::copy_current(a.base,a.guided_pre,p,GetTickCount64(),&current)&&
        guided_missile::same_flight(a.guided_facts,current);
}
bool service_guided_missile(Adapter& a,const CycleScope& cycle,const CommandContext& context,
                            const wawvr::xr::ActionSnapshot& sample,std::uint64_t generation,bool live,
                            const input::CommandRigSample& rig) {
    if(!a.cod4_controls||!a.cycle_enabled)return false;
    PreCommandSnapshot pre{};guided_missile::Facts facts{};
    const auto now=GetTickCount64();
    const bool selected=cycle.current()&&a.gameplay&&read_pre_command_snapshot(&pre)&&
        guided_missile::copy_current(a.base,pre,context,now,&facts);
    const bool changed=selected&&a.guided_active&&(!guided_missile::same_flight(a.guided_facts,facts)||
        pre.gameplay_epoch!=a.guided_pre.gameplay_epoch||generation!=a.guided_rig.xr_generation||
        a.guided_revision!=cycle.revision||pre.sequence<=a.guided_pre.sequence);
    if(selected!=a.guided_active||changed) {
        send_edges(a,a.router.release_all(),context);a.guided_steering.reset();a.turn.reset();
        if(selected&&a.mounted_active){a.mounted_active=false;release_mounted_keys(a,context);}
        trace("t5_vr_guided_missile_context","selected=%u changed=%u entity=%d weapon=%u command=%d epoch=%llu",
              selected,changed,facts.number,unsigned(facts.weapon),context.command_number,pre.gameplay_epoch);
    }
    a.guided_active=selected;
    if(!selected){a.guided_steering.reset();return false;}
    // TV flight disables ordinary movement/turning and gun aiming. Its camera
    // remains native; ADS falls to zero naturally once the TV missile launches.
    a.gameplay=false;a.controls={};a.turn.reset();a.pending_body_yaw={};
    invalidate_native_command_aim();a.router.invalidate_weapon_cycle();
    a.guided_pre=pre;a.guided_facts=facts;a.guided_rig=rig;a.guided_revision=cycle.revision;
    a.guided_occurrence=true;a.guided_axes={};
    const bool current=live&&sample.focused&&generation&&generation==rig.xr_generation&&
        command_input_generation_current(rig)&&!(sample.menu.active&&sample.menu.current);
    const auto output=a.router.update(sample,current?input::Context::guided_missile:input::Context::suspended,now);
    // Changing context releases the launch trigger. A later deliberate release
    // and re-press retains the native in-flight detonation control.
    send_edges(a,output,context);
    constexpr wawvr::xr::Basis3f fixed_axis{{1,0,0},{0,1,0},{0,0,1}};
    input::CommandAimProposal proposal{};
    const auto hand=a.left_handed?wawvr::xr::Hand::Left:wawvr::xr::Hand::Right;
    const auto status=input::propose_native_command_aim(rig,sample,fixed_axis,hand,now,&proposal,false);
    const auto& stick=sample.hands[static_cast<unsigned>(hand)].stick;
    a.guided_axes=a.guided_steering.update(current&&status==input::CommandAimStatus::accepted,
        sample.sequence,proposal.pitch_degrees,proposal.yaw_degrees,stick.active,stick.current.x,stick.current.y);
    if(++a.guided_reports<=12||a.guided_reports%120==0)
        trace("t5_vr_guided_missile_input","current=%u pose_status=%u pitch=%d yaw=%d entity=%d command=%d xr_sequence=%llu",
              current,unsigned(status),int(a.guided_axes.pitch),int(a.guided_axes.yaw),facts.number,context.command_number,sample.sequence);
    ++a.samples;return true;
}
void before_native_impl(void* opaque,const CommandContext& context) {
    auto& a=*static_cast<Adapter*>(opaque);
    a.pending_body_yaw={};a.ladder_occurrence=false;a.swim_occurrence=false;a.swim_turn_owned=false;
    a.room_scale_occurrence=false;
    CycleScope cycle(a,context);
    if(a.cycle_enabled&&(!cycle.owned||cycle.nested)){leave_knife_primary(a);if(native_command_aim_enabled())invalidate_native_command_aim();return;}
    // Every post hook starts ineligible. A second context read may recover after
    // an invalid pre hook; that must not revive the previous command's axes.
    a.gameplay=false;a.override_active=true;a.controls={};a.helicopter_occurrence=false;a.motorcycle_occurrence=false;a.guided_occurrence=false;a.monitor_active=false;
    wawvr::xr::ActionSnapshot sample{};std::uint64_t generation{};
    input::CommandRigSample command_rig{};
    SupportSelection support_selection{};
    const bool rig_probe=command_rig_enabled()||native_command_aim_enabled()||manual_reload_enabled()||a.helicopter_enabled||a.motorcycle_enabled;bool live{};
    const bool previous_shouldered=a.shouldered;a.shouldered=false;a.magnified_optic_active=false;
    if(rig_probe) {
        live=read_vr_command_input_snapshot(&sample,&generation,&command_rig);
        if(a.cod4_controls&&live)a.shouldered=input::cod4_shouldered(command_rig.frame,previous_shouldered);
        begin_command_rig(context,sample,generation,command_rig,live);
        update_manual_reload_input(context,command_rig,live);
        a.magnified_optic_active=update_crossbow_input(context,command_rig,live);
        if(a.cod4_controls)update_manual_grenade_input(context,command_rig,live);
        begin_native_command_aim(context,sample,generation,command_rig,live);
        if(a.helicopter_enabled)a.helicopter_rig=command_rig;
        if(a.motorcycle_enabled)a.motorcycle_rig=command_rig;
        if(a.mounted_enabled) {
            a.mounted_rig=command_rig;
            a.mounted_rig_valid=live&&generation==command_rig.xr_generation&&command_input_generation_current(command_rig);
        }
    }
    if(!context.valid || context.local_client!=0) {leave_knife_primary(a);a.pending_release=true;a.turn.reset();return;}
    if(!a.owner) a.owner=context.native_thread;
    if(a.owner!=context.native_thread) {
        leave_knife_primary(a);a.pending_release=true;a.turn.reset();
        trace("t5_vr_input_rejected","reason=command_thread_changed");return;
    }
    if(a.simulator_motorcycle_load&&!a.simulator_motorcycle_load_attempted&&
       context.connection_state>=9&&context.key_catcher) {
        a.simulator_motorcycle_load_attempted=true;
        trace("t5_vr_motorcycle_simulator_load","command=%d thread=%lu save=bovr_motorcycle_r77",
            context.command_number,context.native_thread);
        a.execute(0,0,"loadgame bovr_motorcycle_r77");
        return;
    }
    if(a.simulator_slingshot_load&&!a.simulator_slingshot_load_attempted&&
       context.connection_state>=9&&context.key_catcher) {
        // Private simulator harness only. Dispatch from the admitted native
        // command owner so the engine sees the same path as a menu-issued load.
        a.simulator_slingshot_load_attempted=true;
        trace("t5_vr_slingshot_simulator_load","command=%d thread=%lu save=bovr_slingshot_r74",
            context.command_number,context.native_thread);
        a.execute(0,0,"loadgame bovr_slingshot_r74");
        return;
    }
    if(a.seen_command && context.command_number<a.last_command) {
        // A new map can reset the native command ring without recreating XR.
        // Held controls must pass neutral again before affecting the new map.
        leave_knife_primary(a);send_edges(a,a.router.release_all(),context);a.turn.reset();
        trace("t5_vr_input_reset","reason=native_command_sequence_reset previous=%d current=%d",
              a.last_command,context.command_number);
    }
    a.seen_command=true;a.last_command=context.command_number;
    if(a.pending_release) {
        if(a.motorcycle_active||a.motorcycle_release_pending)release_motorcycle_keys(a,context);
        else if(a.vehicle_weapon_active||a.vehicle_attack_owned)release_vehicle_weapon(a,context);
        else send_edges(a,a.router.release_all(),context);
        a.pending_release=false;a.turn.reset();
    }
    if(!rig_probe)live=read_vr_input_snapshot(&sample,&generation);
    if(generation!=a.generation) {
        if(a.motorcycle_active||a.motorcycle_release_pending)release_motorcycle_keys(a,context);
        else if(a.vehicle_weapon_active||a.vehicle_attack_owned)release_vehicle_weapon(a,context);
        else send_edges(a,a.router.release_all(),context);
        leave_knife_primary(a);a.turn.reset();a.generation=generation;
    }
    a.gameplay=context.connection_state>=9 && context.key_catcher==0 && !context.native_override;
    a.override_active=context.native_override;
    if(service_guided_missile(a,cycle,context,sample,generation,live,command_rig)){leave_knife_primary(a);return;}
    if(service_motorcycle(a,cycle,context,sample,generation,live)){leave_knife_primary(a);return;}
    if(service_helicopter(a,cycle,context,sample,generation,live)){leave_knife_primary(a);return;}
    PreCommandSnapshot vehicle_pre{};
    const bool vehicle_weapon=live&&a.cod4_controls&&read_pre_command_snapshot(&vehicle_pre)&&
        vehicle_weapon_input_current(vehicle_pre,context,GetTickCount64());
    if(vehicle_weapon) {
        if(!a.vehicle_weapon_active) {
            // Release any ordinary native key leases before changing the fire
            // command name; a held trigger must pass neutral to arm this mode.
            send_edges(a,a.router.release_all(),context);
            a.vehicle_weapon_active=true;a.turn.reset();invalidate_native_command_aim();
            trace("t5_vr_vehicle_weapon_context","selected=1 command=%d sequence=%llu epoch=%llu flags=0x%x pm=%d generation=%llu",
                context.command_number,vehicle_pre.sequence,vehicle_pre.gameplay_epoch,
                vehicle_pre.native_entity_flags,vehicle_pre.native_pm_type,generation);
        }
        a.gameplay=false;
        leave_knife_primary(a);
        a.controls=a.router.update(sample,input::Context::vehicle_weapon,GetTickCount64(),false);
        send_vehicle_weapon_edges(a,a.controls,context);a.controls={};++a.samples;return;
    }
    if(a.vehicle_weapon_active||a.vehicle_attack_owned) {
        release_vehicle_weapon(a,context);a.turn.reset();invalidate_native_command_aim();
        trace("t5_vr_vehicle_weapon_context","selected=0 command=%d generation=%llu",context.command_number,generation);
    }
    bool ordinary_turn=true,scripted_use=false,scripted_attack=false,scripted_linked=false,slingshot_selected=false,monitor_selected=false,tunnel_crawl_selected=false;
    bool manual_busy_for_body=false,dual_for_body=false,knife_primary=false;
    if(a.mounted_enabled) {
        const bool was_mounted=a.mounted_active;
        const bool selected=live&&a.mounted_rig_valid&&a.gameplay&&select_mounted(a,context,generation);
        if(selected!=was_mounted||a.mounted_release_pending) {
            trace("t5_vr_mounted_context","selected=%u previous=%u release_pending=%u command=%d pre_sequence=%llu epoch=%llu entity=%d flags=0x%x pm=%d generation=%llu",
                selected,was_mounted,a.mounted_release_pending,context.command_number,a.mounted_copy.pre.sequence,
                a.mounted_copy.pre.epoch,int(a.mounted_copy.facts.entity),a.mounted_copy.facts.entity_flags,a.mounted_copy.facts.pm_type,generation);
            invalidate_native_command_aim();a.router.invalidate_weapon_cycle();a.turn.reset();
            // Releases use only the established owner command path and distinct
            // key IDs, including when the old turret identity has disappeared.
            a.mounted_active=false;
            send_edges(a,a.router.release_all(),context);
            release_mounted_keys(a,context);a.cycle_previous={};
        }
        a.mounted_active=selected&&a.mounted_source.current(a.mounted_copy.revision);
        if(a.mounted_active) {
            leave_knife_primary(a);invalidate_native_command_aim();a.gameplay=false;
            a.controls=a.router.update(sample,input::Context::mounted,GetTickCount64(),false);
            send_edges(a,a.controls,context);a.controls={};++a.samples;return;
        }
        // Native linked objectives (for example carrying Hudson) still consume
        // ordinary movement/buttons. Keep those native controls independent of
        // exact weapon-aim eligibility. Unknown turret/vehicle states remain
        // excluded, and only ordinary gameplay may use direct snap-turn/cycle.
        PreCommandSnapshot ordinary{};
        const bool current_pre=read_pre_command_snapshot(&ordinary);
        const auto input_now=GetTickCount64();
        scripted_use=a.cod4_controls&&a.gameplay&&current_pre&&scripted_use_current(ordinary,context,input_now);
        scripted_attack=scripted_use&&scripted_attack_current(ordinary,context,input_now);
        scripted_linked=a.cod4_controls&&a.gameplay&&current_pre&&scripted_linked_current(ordinary,context,input_now);
        if(scripted_linked) {
            tunnel_crawl_selected=a.copy_tunnel_crawl&&a.copy_tunnel_crawl(a.base,ordinary,context,input_now);
            aircraft_monitor::Diagnostic diagnostic{};
            monitor_selected=aircraft_monitor::current(a.base,ordinary,&diagnostic);
            if(!monitor_selected)report_monitor_reader_rejection(a,diagnostic,ordinary,input_now,"pre");
        }
        if(monitor_selected){a.monitor_active=true;a.monitor_pre=ordinary;}
        slingshot::Facts slingshot_first{};
        const bool slingshot_facts=a.cod4_controls&&current_pre&&
            copy_slingshot_current(a.base,ordinary,context,&slingshot_first);
        const bool slingshot_exact=slingshot_facts&&slingshot::eligible(slingshot_first);
        const bool slingshot_retained=slingshot_facts&&a.slingshot_active&&
            slingshot::retained(slingshot_first,a.slingshot_cg,a.slingshot_epoch);
        const bool slingshot_state=slingshot_exact||slingshot_retained;
        if(slingshot_state&&(!a.slingshot_active||a.slingshot_cg!=slingshot_first.cg||
           a.slingshot_epoch!=slingshot_first.pre.gameplay_epoch)) {
            a.slingshot_active=true;a.slingshot_cg=slingshot_first.cg;
            a.slingshot_epoch=slingshot_first.pre.gameplay_epoch;a.slingshot_axis=slingshot_first.axis;
            a.slingshot_aim_anchored=false;a.slingshot_anchor_generation=0;
            trace("t5_vr_slingshot_context","selected=1 command=%d sequence=%llu epoch=%llu origin=%.3f,%.3f,%.3f primary=%d offhand=%d clamp=%.3f,%.3f range=%.3f,%.3f",
                context.command_number,ordinary.sequence,ordinary.gameplay_epoch,
                slingshot_first.origin.x,slingshot_first.origin.y,slingshot_first.origin.z,
                slingshot_first.primary,slingshot_first.offhand,
                slingshot_first.clamp_base[0],slingshot_first.clamp_base[1],
                slingshot_first.clamp_range[0],slingshot_first.clamp_range[1]);
        } else if(!slingshot_state&&a.slingshot_active) {
            a.slingshot_active=false;a.slingshot_cg=0;a.slingshot_epoch=0;a.slingshot_axis={};
            a.slingshot_aim_anchored=false;a.slingshot_anchor_generation=0;
            trace("t5_vr_slingshot_context","selected=0 command=%d facts=%u exact=%u retained=%u primary=%d offhand=%d pm=%d pm_flags=0x%x weapon_flags=0x%x eflags=0x%x origin=%.3f,%.3f,%.3f",
                context.command_number,slingshot_facts,slingshot_exact,slingshot_retained,
                slingshot_first.primary,slingshot_first.offhand,ordinary.native_pm_type,
                ordinary.native_pm_flags,ordinary.native_weapon_flags,ordinary.native_entity_flags,
                slingshot_first.origin.x,slingshot_first.origin.y,slingshot_first.origin.z);
        }
        slingshot_selected=scripted_linked&&slingshot_state&&a.slingshot_active&&live&&generation&&
            generation==command_rig.xr_generation&&command_input_generation_current(command_rig);
        if(slingshot_selected) {
            input::CommandAimProposal proposal{};
            const auto hand=a.left_handed?wawvr::xr::Hand::Left:wawvr::xr::Hand::Right;
            const auto status=input::propose_native_command_aim(command_rig,sample,a.slingshot_axis,
                hand,input_now,&proposal,false);
            const bool establishing=!a.slingshot_aim_anchored||a.slingshot_anchor_generation!=generation;
            const float anchor_pitch=establishing?proposal.pitch_degrees:a.slingshot_anchor_pitch;
            const float anchor_yaw=establishing?proposal.yaw_degrees:a.slingshot_anchor_yaw;
            slingshot::RelativeAim relative{};
            const bool mapped=status==input::CommandAimStatus::accepted&&slingshot::relative_aim(
                slingshot_first,anchor_pitch,anchor_yaw,proposal.pitch_degrees,proposal.yaw_degrees,&relative);
            slingshot::Facts slingshot_second{};
            const bool current=mapped&&
                copy_slingshot_current(a.base,ordinary,context,&slingshot_second)&&
                slingshot::same_current(slingshot_first,slingshot_second)&&
                command_input_generation_current(command_rig);
            if(current&&a.write_helicopter_look(a.base,relative.command_pitch,relative.command_yaw)) {
                if(establishing){a.slingshot_aim_anchored=true;a.slingshot_anchor_generation=generation;
                    a.slingshot_anchor_pitch=anchor_pitch;a.slingshot_anchor_yaw=anchor_yaw;}
                const auto report=++a.slingshot_reports;
                if(report<=12||report%120==0)
                    trace("t5_vr_slingshot_aim","status=accepted controller=%.3f,%.3f offset=%.3f,%.3f desired=%.3f,%.3f command_angles=%.3f,%.3f calibrated=%u exact=%u retained=%u command=%d xr_sequence=%llu generation=%llu pull_axis=left_stick hand=%s native_view_writes=2",
                        proposal.pitch_degrees,proposal.yaw_degrees,relative.pitch_offset,relative.yaw_offset,
                        relative.desired_pitch,relative.desired_yaw,relative.command_pitch,relative.command_yaw,
                        establishing,slingshot_exact,slingshot_retained,context.command_number,sample.sequence,generation,
                        a.left_handed?"left":"right");
            } else {
                const auto report=++a.slingshot_rejections;
                if(report<=12||report%600==0)
                    trace("t5_vr_slingshot_aim","status=rejected detail=%u mapped=%u current=%u command=%d xr_sequence=%llu generation=%llu native_view_writes=0",
                        unsigned(status),mapped,current,context.command_number,sample.sequence,generation);
            }
        }
        if(scripted_use||((!current_pre||!basic_input_current(ordinary,context,input_now))&&
           !manual_grenade_gameplay_current(context)))
            a.gameplay=false;
        ordinary_turn=ordinary.gameplay_active&&!monitor_selected;
    }
    // DisableWeapons is independent of native locomotion. Creek's boat
    // approach keeps PM_NORMAL swimming and polls melee while primary weapon
    // tracking is deliberately unavailable. Admit only a fresh, fully read
    // native swim context, without changing the global weapon admission.
    PreCommandSnapshot swim_pre{},swim_confirmed{};swimming::Facts swim_facts{};
    const auto swim_now=GetTickCount64();
    const bool swim=a.cod4_controls&&live&&a.gameplay&&!scripted_use&&!scripted_linked&&
        !slingshot_selected&&!monitor_selected&&rig_probe&&
        command_rig.frame.actions.sequence==sample.sequence&&ladder_rig_current(command_rig,generation,swim_now)&&
        a.copy_swimming&&read_pre_command_snapshot(&swim_pre)&&
        a.copy_swimming(a.base,swim_pre,context,swim_now,&swim_facts)&&
        read_pre_command_snapshot(&swim_confirmed)&&swimming::rd::same_pre(swim_pre,swim_confirmed);
    if(swim)ordinary_turn=true;
    // Weapon visibility/update permission is independent of ordinary walking.
    // This dry native reader preserves every linked/frozen/camera exclusion;
    // in particular it cannot turn the tightly clamped tunnel crawl camera.
    PreCommandSnapshot walk_pre{},walk_confirmed{};swimming::Facts walk_facts{};
    const bool walking=!ordinary_turn&&a.cod4_controls&&live&&a.gameplay&&!scripted_use&&!scripted_linked&&
        !slingshot_selected&&!monitor_selected&&rig_probe&&
        command_rig.frame.actions.sequence==sample.sequence&&ladder_rig_current(command_rig,generation,swim_now)&&
        a.copy_walking&&read_pre_command_snapshot(&walk_pre)&&
        a.copy_walking(a.base,walk_pre,context,swim_now,&walk_facts)&&
        read_pre_command_snapshot(&walk_confirmed)&&swimming::rd::same_pre(walk_pre,walk_confirmed);
    if(walking){
        ordinary_turn=true;
        if(++a.walking_reports<=12||a.walking_reports%120==0)
            trace("t5_vr_disarmed_walk","accepted=1 native_command=%d weapon_flags=0x%x hidden=%d weapon_permission_unchanged=1",
                context.command_number,walk_pre.native_weapon_flags,walk_facts.camera[6]);
    }
    if(!live || (!a.gameplay&&!scripted_use)) {
        leave_knife_primary(a);
        a.controls=a.router.release_all();a.turn.reset();
    } else if(scripted_use) {
        leave_knife_primary(a);
        a.turn.reset();
        a.controls=a.router.update(sample,scripted_attack?input::Context::scripted_attack:input::Context::scripted_use,GetTickCount64());
    } else if(scripted_linked) {
        leave_knife_primary(a);
        a.turn.reset();
        a.controls=a.router.update(sample,monitor_selected?input::Context::aircraft_monitor:input::Context::scripted_linked,GetTickCount64());
        if(slingshot_selected) {
            const auto& left=sample.hands[0].stick;
            const auto& right_hand=sample.hands[1].stick;
            const bool commanded=std::fabs(left.current.x)>0.001f||std::fabs(left.current.y)>0.001f||
                std::fabs(right_hand.current.x)>0.001f||std::fabs(right_hand.current.y)>0.001f||
                std::fabs(a.controls.forward)>0.001f||std::fabs(a.controls.right)>0.001f;
            if(commanded||++a.slingshot_input_reports<=12||a.slingshot_input_reports%120==0)
                trace("t5_vr_slingshot_input","left_active=%u left=%.3f,%.3f right_active=%u right=%.3f,%.3f routed_forward=%.3f routed_right=%.3f native_command=%d xr_sequence=%llu",
                    left.active,left.current.x,left.current.y,right_hand.active,right_hand.current.x,right_hand.current.y,
                    a.controls.forward,a.controls.right,context.command_number,sample.sequence);
        }
    } else {
        const auto now=GetTickCount64();
        // The exact combined mode uses the single original rig/action copy.
        const bool cycle_allowed=(!a.cycle_exact_requested||exact_profile_aim_enabled())&&
            (!rig_probe||exact_profile_aim_enabled())&&cycle.permit(context,generation,sample,now);
        const bool reload_busy=manual_reload_uses_offhand(),grenade_busy=manual_grenade_consumes_trigger();
        const bool manual_busy=reload_busy||grenade_busy;
        const bool support_available=a.cod4_controls&&cycle_allowed&&
            current_support_selection(a,cycle,context,&support_selection);
        manual_busy_for_body=manual_busy;dual_for_body=native_command_dual_current(context);
        PreCommandSnapshot knife_pre{};PrimaryWeaponMode weapon_mode{};
        const bool primary_current=a.cod4_controls&&read_pre_command_snapshot(&knife_pre)&&
            basic_input_current(knife_pre,context,now)&&a.copy_primary_mode&&
            a.copy_primary_mode(a.base,knife_pre,&weapon_mode);
        knife_primary=primary_current&&weapon_mode.melee;
        bool ads_requested=a.shouldered&&!a.magnified_optic_active;
        const bool strela_candidate=primary_current&&weapon_mode.type==2&&weapon_mode.weapon_class==6&&
            (std::strcmp(weapon_mode.internal_name.data(),"strela_sp")==0||
             std::strcmp(weapon_mode.internal_name.data(),"flashpoint_strela_sp")==0);
        if(strela_candidate) {
            // A launcher may rest beside/behind the eye or point away from the
            // headset. Its native lock script still requires real ADS. Keep
            // the existing two-hand support geometry, without rifle shouldering
            // distance/gaze constraints or writes to ADS/target-lock state.
            ads_requested=false;
            strela::Owner first{},second{};PreCommandSnapshot confirmed{};
            input::CommandAimProposal support{};
            bool grips_tracked=true;
            for(const auto& hand:sample.hands)grips_tracked=grips_tracked&&hand.grip.active&&
                hand.grip.position_valid&&hand.grip.position_tracked&&
                hand.grip.orientation_valid&&hand.grip.orientation_tracked;
            const bool current=rig_probe&&live&&!dual_for_body&&grips_tracked&&
                context.native_thread==GetCurrentThreadId()&&
                command_rig.frame.actions.sequence==sample.sequence&&
                input::propose_native_command_aim(command_rig,sample,{},wawvr::xr::Hand::Right,
                    now,&support,true)==input::CommandAimStatus::accepted&&
                ladder_rig_current(command_rig,generation,now)&&a.copy_strela_owner&&
                a.copy_strela_owner(a.base,knife_pre,now,&first)&&
                first.weapon.weapon==weapon_mode.weapon&&
                a.copy_strela_owner(a.base,knife_pre,GetTickCount64(),&second)&&first==second&&
                read_pre_command_snapshot(&confirmed)&&swimming::rd::same_pre(knife_pre,confirmed)&&
                ladder_rig_current(command_rig,generation,GetTickCount64());
            if(current) {
                ensure_strela_aim();
                ads_requested=support.support.paired&&!a.left_handed&&!(sample.menu.active&&sample.menu.current)&&
                    command_input_generation_current(command_rig);
            }
            if(++a.strela_reports<=12||a.strela_reports%120==0)
                trace("t5_strela_ads","weapon=%u owner_current=%u supported=%u requested=%u manual_busy=%u rifle_shouldered=%u native_command=%d xr_sequence=%llu native_ads_lock_writes=0",
                    weapon_mode.weapon,current,support.support.paired,ads_requested,manual_busy,a.shouldered,
                    context.command_number,sample.sequence);
        }
        const bool swim_melee=swim&&knife_primary&&(swim_pre.native_weapon_flags&0x80)&&
            swimming::rd::same_pre(swim_pre,knife_pre)&&
            std::strcmp(weapon_mode.internal_name.data(),"creek_knife_sp")==0;
        const auto knife=a.knife_swing.update(sample,now,knife_primary);
        a.knife_primary_active=knife_primary;
        const bool ordinary_attack_wait=native_command_ordinary_attack_inhibited(context);
        a.controls=a.router.update(sample,input::Context::gameplay,now,cycle_allowed,ads_requested,manual_busy,dual_for_body,
            a.button_grenades&&grenade_busy&&!reload_busy,knife_primary,knife.pulse,reload_busy&&!grenade_busy,support_available,swim_melee,
            ordinary_attack_wait);
        if(ordinary_attack_wait){static std::uint64_t reports{};if(++reports<=24||reports%120==0)
            trace("t5_vr_ordinary_attack_wait","count=%llu native_command=%d xr_sequence=%llu edges=%u trigger=%.4f grip=%.4f stick_click=%u fresh_ordinary_owner=1 synthetic_neutral=0",
                reports,context.command_number,sample.sequence,a.controls.edge_count,sample.hands[1].trigger.current,
                sample.hands[1].squeeze.current,sample.hands[1].stick_click.current);}
        if(knife_primary) {
            const auto report=++a.knife_reports;
            if(knife.pulse||report<=12||report%120==0)
                trace("t5_vr_knife_swing","weapon=%u internal=%s localized=%s type=%d class=%d named_melee=%u pulse=%u speed_mps=%.3f right_trigger_suppressed=1 native_command=%d xr_sequence=%llu scripted_swim_melee=%u",
                    weapon_mode.weapon,weapon_mode.internal_name.data(),weapon_mode.localized_name.data(),weapon_mode.type,weapon_mode.weapon_class,weapon_mode.named_melee,
                    knife.pulse,knife.speed_meters_per_second,context.command_number,sample.sequence,swim_melee);
        }
    }
    if(!ordinary_turn)a.controls.turn=0;
    PreCommandSnapshot ladder_pre{};
    const auto ladder_now=GetTickCount64();
    const bool ladder=a.cod4_controls&&a.gameplay&&ordinary_turn&&read_pre_command_snapshot(&ladder_pre)&&
        ladder_input_current(ladder_pre,context,ladder_now);
    if(ladder) {
        // Native PMF_LADDER makes forward/backward the climbing axis. Rotating
        // this axis by HMD/controller heading turns looking aside into lateral
        // motion off the ladder. Native collision and ladder attachment remain
        // authoritative; only this command's climb pitch is normalized below.
        a.ladder_occurrence=true;a.ladder_pre=ladder_pre;a.ladder_rig=command_rig;
        if(!rig_probe||!live||command_rig.frame.actions.sequence!=sample.sequence||
           !ladder_rig_current(command_rig,generation,ladder_now))
            a.controls.forward=a.controls.right=0.0f;
    }
    if(swim) {
        a.swim_occurrence=true;a.swim_pre=swim_pre;a.swim_rig=command_rig;a.swim_facts=swim_facts;
        if(!rig_probe||!live||command_rig.frame.actions.sequence!=sample.sequence||
           !ladder_rig_current(command_rig,generation,GetTickCount64()))a.controls.forward=a.controls.right=0.f;
    }
    if(a.cod4_controls&&!ladder&&!swim&&!slingshot_selected&&!monitor_selected&&!tunnel_crawl_selected&&(a.controls.forward!=0.0f||a.controls.right!=0.0f)) {
        float forward{},right{};
        const bool oriented=rig_probe&&live&&generation==command_rig.xr_generation&&
            command_input_generation_current(command_rig)&&
            input::orient_movement(command_rig.frame,command_rig.tracking_anchor,a.left_handed,
                a.movement_orientation,a.controls.forward,a.controls.right,&forward,&right);
        if(oriented) {
            a.controls.forward=forward;a.controls.right=right;
            if(++a.movement_orientation_reports<=12||a.movement_orientation_reports%120==0)
                trace("t5_vr_movement_orientation","mode=%s forward=%.4f right=%.4f xr_sequence=%llu generation=%llu native_command=%d",
                    a.movement_orientation==input::MovementOrientation::controller?"controller":"hmd",
                    forward,right,sample.sequence,generation,context.command_number);
        } else {
            a.controls.forward=a.controls.right=0.0f;
            if(++a.movement_orientation_rejections<=12||a.movement_orientation_rejections%120==0)
                trace("t5_vr_movement_orientation_rejected","mode=%s xr_sequence=%llu generation=%llu native_command=%d",
                    a.movement_orientation==input::MovementOrientation::controller?"controller":"hmd",
                    sample.sequence,generation,context.command_number);
        }
    }
    if(a.cod4_controls&&!ladder&&!swim&&a.movement_orientation==input::MovementOrientation::hmd&&a.gameplay&&ordinary_turn&&
       !a.shouldered&&!manual_busy_for_body&&!dual_for_body&&a.controls.turn==0.0f&&
       (a.controls.forward!=0.0f||a.controls.right!=0.0f)&&rig_probe&&live&&
       generation==command_rig.xr_generation&&command_input_generation_current(command_rig)&&
       std::isfinite(context.viewangles[1])) {
        input::BodyYawTransferPlan plan{};
        if(input::plan_hmd_body_yaw_transfer(command_rig.frame,command_rig.tracking_anchor,
                                             input::kHmdBodyYawTransferDeadbandDegrees,&plan)) {
            auto& pending=a.pending_body_yaw;pending.rig=command_rig;pending.desired_anchor=plan.desired_anchor;
            pending.expected_yaw=context.viewangles[1];pending.desired_yaw=std::remainder(context.viewangles[1]+plan.delta_degrees,360.f);
            pending.delta=plan.delta_degrees;pending.native_thread=context.native_thread;
            pending.command_number=context.command_number;pending.native_frame_time=context.native_frame_time;pending.valid=true;
        }
    }
    // Consume before any paired native edge can throw or reenter. A lost or
    // intercepted native request has no retry queue. Action-slot release is an
    // immediate balanced pulse and never retained as a held router action.
    const bool support_reserved=a.controls.weapon_support;
    const bool alternate_reserved=a.controls.weapon_alternate&&!support_reserved;
    const bool cycle_reserved=(a.controls.weapon_next||alternate_reserved||support_reserved) && cycle.current();
    a.controls.weapon_next=a.controls.weapon_alternate=a.controls.weapon_support=false;
    const auto cycle_request=cycle_reserved?++a.cycle_requests:0;
    send_edges(a,a.controls,context);
    if(monitor_selected&&(a.controls.edge_count||a.samples<=12||a.samples%120==0))
        trace("t5_vr_aircraft_monitor","selected=1 map=wmd_sr71 command=%d edges=%u forward=%.3f right=%.3f xr_sequence=%llu command_button=A jump_key=28676 raw_navigation=1",
            context.command_number,a.controls.edge_count,a.controls.forward,a.controls.right,sample.sequence);
    if(scripted_use&&a.controls.edge_count)
        trace("t5_vr_scripted_use","edges=%u native_command=%d xr_sequence=%llu generation=%llu use_key=28674 jump_key=28676 scripted_attack=%u attack_key=28672 native_movement_retained=1 native_weapon_policy_retained=1",
            a.controls.edge_count,context.command_number,sample.sequence,generation,scripted_attack);
    if(scripted_linked&&a.controls.edge_count)
        trace("t5_vr_scripted_linked","edges=%u native_command=%d xr_sequence=%llu generation=%llu attack_key=28672 ads_key=28673 use_key=28674 native_weapon_policy_retained=1",
              a.controls.edge_count,context.command_number,sample.sequence,generation);
    if(!live || !a.gameplay) {a.controls={};return;}
    service_crosshair_preference(a,context);
    if(a.cod4_controls&&a.controls.lower_stance&&ordinary_turn){a.controls.lower_stance=false;a.execute(0,0,"lowerstance");}
    if(cycle_reserved && cycle.current()) {
        const auto& p=a.cycle_previous;
        SupportSelection support_now{};
        const auto alternate_slot=support_reserved?4u:(alternate_reserved?current_alternate_slot(a,cycle,context):0);
        if(support_reserved&&(!current_support_selection(a,cycle,context,&support_now)||support_now!=support_selection||
           manual_reload_uses_offhand()||manual_grenade_consumes_trigger())) {
            trace("t5_vr_support_refused","request=%llu reason=current_slot_admission native_command=%d",cycle_request,context.command_number);
        } else if(alternate_reserved&&!alternate_slot) {
            trace("t5_vr_alternate_refused","request=%llu reason=current_slot_admission native_command=%d",cycle_request,context.command_number);
        } else if(!alternate_reserved&&!support_reserved&&a.cod4_controls&&
                  (manual_grenade_consumes_trigger()||!cancel_manual_reload_for_selection(context))) {
            trace("t5_vr_weapon_cycle_refused","request=%llu reason=reload_or_grenade_ownership native_command=%d",cycle_request,context.command_number);
        } else if(cycle.current()) {
        trace("t5_vr_weapon_cycle","request=%llu phase=dispatch_start xr_sequence=%llu generation=%llu pre_sequence=%llu epoch=%llu cg=%p native_command=%d native_clock=%d thread=%lu command=%s action_slot=%u",
              cycle_request,sample.sequence,generation,p.sequence,p.gameplay_epoch,
              reinterpret_cast<void*>(p.native_cg_identity),context.command_number,context.native_frame_time,context.native_thread,
              support_reserved?"actionslot_support_pulse":(alternate_reserved?"actionslot_alt_pulse":"weapnext"),alternate_slot);
        // A dispatched selection cancels this occurrence's aim; native requested/current agreement is still checked on later commands.
        if(exact_profile_aim_enabled())retire_native_command_aim_for_selection(context);
        if(alternate_reserved||support_reserved)pulse_action_slot(a.execute,alternate_slot);
        else a.execute(0,0,"weapnext");
        trace("t5_vr_weapon_cycle","request=%llu phase=dispatch_returned native_command=%d thread=%lu weapon_change_unconfirmed=1",
              cycle_request,context.command_number,context.native_thread);
        }
    }
    const auto turn_mode=a.smooth_turn?input::TurnMode::smooth:input::TurnMode::snap;
    float yaw_delta{};
    if(ordinary_turn)yaw_delta=a.turn.update(a.controls.turn,GetTickCount64(),turn_mode,a.cod4_controls?45.f:30.f,90.f);
    else a.turn.suspend(); // A refused swim/weapon context is not stick neutral.
    if(yaw_delta!=0 && std::isfinite(context.viewangles[1])) {
        const float next=input::apply_turn_delta(context.viewangles[1],yaw_delta,turn_mode);
        const bool written=set_native_yaw(a.base,next);
        if(written&&a.swim_occurrence){a.swim_turn_owned=true;a.swim_turn_yaw=next;}
        if(!a.smooth_turn||++a.smooth_turn_reports<=12||a.smooth_turn_reports%120==0)
            trace("t5_vr_turn","written=%u degrees=%.3f native_yaw_before=%.3f requested_yaw_after=%.3f native_command=%d",written,yaw_delta,context.viewangles[1],next,context.command_number);
    }
    if(a.room_scale_enabled&&rig_probe&&live&&a.gameplay&&ordinary_turn&&!scripted_use&&!scripted_linked&&
       !slingshot_selected&&!monitor_selected&&!tunnel_crawl_selected&&!ladder&&!swim&&!a.pending_body_yaw.valid&&
       !a.controls.turn&&yaw_delta==0&&command_input_generation_current(command_rig)&&
       read_pre_command_snapshot(&a.room_scale_pre)) {
        a.room_scale_rig=command_rig;a.room_scale_occurrence=true;
    }
    ++a.samples;
}
void before_native(void* opaque,const CommandContext& context) {
    auto& a=*static_cast<Adapter*>(opaque);
    if(exact_profile_aim_enabled()&&!exact_profile_input_owner(context,true)){if(a.cycle_enabled)revoke_cycle(a);return;}
    if(!native_command_aim_enabled()){before_native_impl(opaque,context);return;}
    // Preserve the original outer controls and rig when native textual input
    // dispatch reenters this callback. Aim never adopts a second pose sample.
    if(a.aim_input_running.test_and_set()){invalidate_native_command_aim();if(a.cycle_enabled)revoke_cycle(a);return;}
    __try {before_native_impl(opaque,context);}
    __finally {if(AbnormalTermination())invalidate_native_command_aim();a.aim_input_running.clear();}
}
void after_native(void* opaque,const CommandContext& context,unsigned char* command) {
    auto& a=*static_cast<Adapter*>(opaque);
    if(a.cycle_enabled){const auto owner=a.cycle_owner.load(std::memory_order_acquire);
        if(owner!=GetCurrentThreadId()||a.cycle_busy){revoke_cycle(a);if(native_command_aim_enabled())invalidate_native_command_aim();return;}}
    if(exact_profile_aim_enabled()&&(!exact_profile_input_owner(context,false)||a.aim_input_running.test())){
        invalidate_native_command_aim();if(a.cycle_enabled)revoke_cycle(a);return;
    }
    if(native_command_aim_enabled()&&(a.aim_input_running.test()||GetCurrentThreadId()!=a.owner))return;
    struct RoomScaleCommit {
        Adapter& a;const CommandContext& context;
        input::room_scale::Follow next{};
        input::room_scale::Sample sample{};
        input::room_scale::Plan plan{};
        RoomScaleView view{};float fraction{},query_fraction{};bool prepared{},query_ok{},query_solid{};
        ~RoomScaleCommit(){
            a.room_scale_occurrence=false;
            if(!a.room_scale_enabled)return;
            if(!prepared){a.room_scale_follow.reset();revoke_room_scale_view();return;}
            const bool committed=commit_native_command_aim_room_scale(context,sample.rig,plan.anchor,view);
            if(committed)a.room_scale_follow=next;
            // A racing newer XR frame may refuse the transaction. Keep the
            // last approved camera; its stale fallback is the native eye.
            // A hit consumes rejected displacement into the anchor; the next
            // unchanged pose can legitimately trace fraction1. Keep bounded
            // hit evidence even when the periodic sample misses that command.
            const bool collision_event=(query_ok&&(query_solid||query_fraction<1.f))||
                (plan.blocked&&fraction>=1.f);
            const bool collision_report=collision_event&&a.room_scale_collision_reports<32;
            if(collision_report)++a.room_scale_collision_reports;
            if(++a.room_scale_reports<=20||a.room_scale_reports%90==0||collision_report)
                trace("t5_room_scale","committed=%u rebased=%u blocked=%u command=%d origin=%.4f,%.4f,%.4f observed=%.4f,%.4f eye=%.4f,%.4f,%.4f residual=%.4f,%.4f forward=%.4f right=%.4f fraction=%.5f query_ok=%u query_solid=%u query_fraction=%.5f collision_event=%u collision_reports=%llu camera_radius=%.5f near_clip=%.5f",
                    committed,plan.rebased,plan.blocked,context.command_number,sample.origin.x,sample.origin.y,sample.origin.z,
                    plan.observed_body.x,plan.observed_body.y,view.eye.x,view.eye.y,view.eye.z,
                    plan.residual.x,plan.residual.y,plan.forward,plan.right,fraction,query_ok,query_solid,query_fraction,
                    collision_event,a.room_scale_collision_reports,view.camera_radius,view.near_clip);
        }
    } room_scale_commit{a,context};
    // Declared before FinishRig so its destructor runs last. Aim, manual reload
    // and command-rig consumers must finish in the original anchor domain.
    struct BodyYawCommit {
        Adapter& a;const CommandContext& context;
        ~BodyYawCommit(){
            const auto pending=a.pending_body_yaw;a.pending_body_yaw={};
            if(!pending.valid||pending.native_thread!=context.native_thread||
               pending.command_number!=context.command_number||pending.native_frame_time!=context.native_frame_time)return;
            const bool committed=commit_native_command_aim_body_yaw(context,pending.rig,pending.desired_anchor,
                pending.expected_yaw,pending.desired_yaw);
            auto& reports=committed?a.body_yaw_reports:a.body_yaw_rejections;
            if(++reports<=12||reports%120==0)
                trace(committed?"t5_vr_hmd_body_yaw":"t5_vr_hmd_body_yaw_rejected",
                    "committed=%u delta=%.3f native_yaw_before=%.3f native_yaw_after=%.3f native_command=%d xr_frame=%llu xr_sequence=%llu generation=%llu",
                    committed,pending.delta,pending.expected_yaw,pending.desired_yaw,context.command_number,
                    pending.rig.frame.frame_id,pending.rig.frame.actions.sequence,pending.rig.xr_generation);
        }
    } body_yaw_commit{a,context};
    // Runs on every ordinary post path, after optional analog writes, including
    // neutral controls and invalid-context returns. Never acquires a newer pose.
    struct FinishRig {
        const CommandContext& context;const unsigned char* command;
        ~FinishRig(){apply_manual_reload_command(context,const_cast<unsigned char*>(command));finish_native_command_aim(context,const_cast<unsigned char*>(command));finish_command_rig(context,command);}
    } finish_rig{context,command};
    if(a.guided_occurrence) {
        const bool current=command&&guided_current(a,context);
        a.guided_occurrence=false;
        if(!current){a.guided_steering.reset();return;}
        // Each result is a new native command. Neutral VR axes leave desktop
        // mouse steering intact; no preceding command's VR values are reused.
        if(a.guided_axes.pitch||a.guided_axes.yaw) {
            memcpy(command+0x1e,&a.guided_axes.pitch,1);memcpy(command+0x1f,&a.guided_axes.yaw,1);
        }
        if(a.guided_reports<=12||a.guided_reports%120==0)
            trace("t5_vr_guided_missile_axes","pitch=%d yaw=%d entity=%d native_command=%d",
                  int(a.guided_axes.pitch),int(a.guided_axes.yaw),a.guided_facts.number,context.command_number);
        return;
    }
    if(a.motorcycle_enabled&&a.motorcycle_occurrence) {
        const bool current=command&&a.motorcycle_controls.valid&&motorcycle_current(a,context);
        a.motorcycle_occurrence=false;
        if(!current){a.motorcycle_release_pending=true;return;}
        const auto axis=[](float v){return static_cast<std::int8_t>(std::clamp(std::lround(v*127.f),-127L,127L));};
        const std::int8_t drive=axis(a.motorcycle_controls.drive),steer=axis(a.motorcycle_controls.steer);
        // Throttle/brake are native vehicle key bits. These bytes preserve the
        // analog stick magnitude for scripts that also inspect usercmd motion.
        if(drive||steer){memcpy(command+0x1b,&drive,1);memcpy(command+0x1c,&steer,1);}
        if(a.motorcycle_reports<=12||a.motorcycle_reports%120==0)
            trace("t5_vr_motorcycle_axes","drive=%d steer=%d native_command=%d vehicle=%d",
                int(drive),int(steer),context.command_number,int(a.motorcycle_facts.vehicle_index));
        return;
    }
    if(a.helicopter_enabled&&a.helicopter_occurrence) {
        const bool current=command&&a.helicopter_controls.valid&&helicopter_current(a,context);
        a.helicopter_occurrence=false; // The borrowed native result is consumed once.
        if(!current){a.helicopter_release_pending=true;return;}
        const auto& axes=a.helicopter_controls.axes;
        const auto axis=[](float v){return static_cast<std::int8_t>(std::clamp(std::lround(v*127.f),-127L,127L));};
        const std::int8_t forward=axis(axes.forward),right=axis(axes.right),pitch=axis(-axes.pitch),yaw=axis(-axes.yaw);
        // Neutral groups leave desktop input intact. The native command is newly
        // produced each occurrence, so no old VR axis remains to be cleared.
        if(forward||right){memcpy(command+0x1b,&forward,1);memcpy(command+0x1c,&right,1);}
        if(pitch||yaw){memcpy(command+0x1e,&pitch,1);memcpy(command+0x1f,&yaw,1);}
        if(a.samples<=12||a.samples%120==0)
            trace("t5_vr_helicopter_axes","forward=%d right=%d pitch=%d yaw=%d native_command=%d entity=%d",int(forward),int(right),int(pitch),int(yaw),context.command_number,a.helicopter_facts.entity_index);
        return;
    }
    if(a.swim_occurrence) {
        a.swim_occurrence=false;
        const bool turned=a.swim_turn_owned;a.swim_turn_owned=false;
        const auto forward=a.controls.forward,right=a.controls.right;
        a.controls.forward=a.controls.right=0.f;
        if(!forward&&!right)return; // Fresh native neutral pitch and buoyancy remain native.
        // The post hook rereads the live client angles. Only the yaw written by
        // this occurrence's successful snap/smooth turn may differ from the
        // original pre snapshot. Validate that exact result, then read native
        // swim facts against the unchanged original command identity. Movement
        // is decomposed using the actual generated command yaw below.
        const auto& original_context=a.swim_pre.context;
        auto expected_context=original_context;
        if(turned)expected_context.viewangles[1]=a.swim_turn_yaw;
        if(!swimming::rd::same_command(context,expected_context))return;
        PreCommandSnapshot current{};swimming::Facts facts{};const auto now=GetTickCount64();
        if(!a.gameplay||a.override_active||context.native_thread!=a.owner||!command||
           !basic_input_current(a.swim_pre,original_context,now)||!read_pre_command_snapshot(&current)||
           !basic_input_current(current,original_context,now)||current.sequence!=a.swim_pre.sequence||
           current.gameplay_epoch!=a.swim_pre.gameplay_epoch||current.native_cg_identity!=a.swim_pre.native_cg_identity||
           current.native_game_time!=a.swim_pre.native_game_time||!a.copy_swimming||
           !a.copy_swimming(a.base,current,original_context,now,&facts)||facts!=a.swim_facts||
           !ladder_rig_current(a.swim_rig,a.generation,now))return;
        const auto angle=[&](unsigned offset,unsigned component) {
            std::int32_t packed{};memcpy(&packed,command+offset,sizeof(packed));
            return std::remainder(static_cast<float>(packed)*(360.f/65536.f)+facts.delta_angles[component],360.f);
        };
        wawvr::xr::Basis3f stock{};
        static_assert(sizeof(stock)==sizeof(facts.refdef_axis));
        memcpy(&stock,facts.refdef_axis.data(),sizeof(stock));
        input::SwimmingMovement move{};
        const auto yaw=angle(0x10,1),roll=angle(0x14,2);
        if(!input::orient_swimming(a.swim_rig.frame,a.swim_rig.tracking_anchor,stock,yaw,roll,
            facts.pitch_up_limit,facts.pitch_down_limit,forward,right,&move))return;
        PreCommandSnapshot confirmed{};const auto commit_now=GetTickCount64();
        if(!read_pre_command_snapshot(&confirmed)||!basic_input_current(confirmed,original_context,commit_now)||
           !swimming::rd::same_pre(confirmed,current)||
           !ladder_rig_current(a.swim_rig,a.generation,commit_now))return;
        const auto pitch=static_cast<std::int32_t>(std::lround(std::remainder(move.pitch_degrees-facts.delta_angles[0],360.f)*(65536.f/360.f)))&0xffff;
        const auto axis=[](float value){return static_cast<std::int8_t>(std::clamp(std::lround(value*127.f),-127L,127L));};
        const auto f=axis(move.forward),r=axis(move.right);
        // Keep native yaw/roll, native swim speed, buoyancy and surface clamp.
        // Only this borrowed command receives the HMD-directed 3D swim vector.
        memcpy(command+0xc,&pitch,sizeof(pitch));memcpy(command+0x1b,&f,1);memcpy(command+0x1c,&r,1);
        if(++a.swim_reports<=16||a.swim_reports%120==0)
            trace("t5_vr_swim_move","waterlevel=%d forward=%d right=%d pitch=%.4f native_yaw=%.4f native_roll=%.4f pitch_limited=%u body_yaw_transfer=0 command=%d xr_sequence=%llu",
                facts.waterlevel,int(f),int(r),move.pitch_degrees,yaw,roll,move.pitch_limited,context.command_number,a.swim_rig.frame.actions.sequence);
        return;
    }
    if(a.ladder_occurrence) {
        a.ladder_occurrence=false;
        const auto forward=a.controls.forward,right=a.controls.right;
        a.controls.forward=a.controls.right=0.0f; // One native result owns this sample.
        PreCommandSnapshot current{};const auto now=GetTickCount64();
        if(!a.gameplay||a.override_active||context.native_thread!=a.owner||!command||
           !ladder_input_current(a.ladder_pre,context,now)||
           !read_pre_command_snapshot(&current)||!ladder_input_current(current,context,now)||
           current.sequence!=a.ladder_pre.sequence||current.gameplay_epoch!=a.ladder_pre.gameplay_epoch||
           current.native_cg_identity!=a.ladder_pre.native_cg_identity||
           current.native_game_time!=a.ladder_pre.native_game_time||
           !ladder_rig_current(a.ladder_rig,a.generation,now))return;
        const auto axis=[](float value){return static_cast<std::int8_t>(std::clamp(std::lround(value*127.f),-127L,127L));};
        const auto f=axis(forward),r=axis(right);
        float delta_pitch{};
        if(f) {
            if(!a.copy_ladder_delta||!a.copy_ladder_delta(a.base,current,&delta_pitch)||
               !std::isfinite(delta_pitch)||std::fabs(delta_pitch)>360000.f) {
                if(++a.ladder_reports<=12||a.ladder_reports%120==0)
                    trace("t5_vr_ladder_refused","reason=native_delta_unavailable command=%d pm_flags=0x%x",context.command_number,current.native_pm_flags);
                return;
            }
            PreCommandSnapshot confirmed{};const auto commit_now=GetTickCount64();
            if(!read_pre_command_snapshot(&confirmed)||!ladder_input_current(confirmed,context,commit_now)||
               confirmed.sequence!=current.sequence||confirmed.gameplay_epoch!=current.gameplay_epoch||
               confirmed.native_cg_identity!=current.native_cg_identity||
               confirmed.native_game_time!=current.native_game_time||
               !ladder_rig_current(a.ladder_rig,a.generation,commit_now))return;
            const auto pitch=ladder_command_pitch(delta_pitch);
            memcpy(command+0xc,&pitch,sizeof(pitch));
        }
        // The ordinary native command is regenerated every occurrence. Neutral,
        // strafe-only and ladder-exit commands retain that command's own pitch;
        // there is no saved angle to restore into a later command or camera.
        if(f||r){memcpy(command+0x1b,&f,1);memcpy(command+0x1c,&r,1);}
        if(++a.ladder_reports<=12||a.ladder_reports%120==0)
            trace("t5_vr_ladder_move","forward=%d right=%d command=%d pm_flags=0x%x raw_stick=1 body_yaw_transfer=0 command_pitch_adjusted=%u climb_pitch=-18 delta_pitch=%.4f xr_sequence=%llu",
                int(f),int(r),context.command_number,current.native_pm_flags,f!=0,delta_pitch,a.ladder_rig.frame.actions.sequence);
        return;
    }
    if(!a.gameplay || a.override_active || !context.valid || context.local_client!=0 ||
       context.native_thread!=a.owner || context.key_catcher || context.connection_state<9 || !command) return;
    if(a.monitor_active) {
        a.monitor_active=false;
        const auto forward=a.controls.forward,right=a.controls.right;
        a.controls.forward=a.controls.right=0; // Consume this occurrence once.
        if(context.command_number!=a.monitor_pre.context.command_number||
           context.native_frame_time!=a.monitor_pre.context.native_frame_time)return;
        aircraft_monitor::Diagnostic diagnostic{};
        if(!aircraft_monitor::current(a.base,a.monitor_pre,&diagnostic)) {
            report_monitor_reader_rejection(a,diagnostic,a.monitor_pre,GetTickCount64(),"post");return;
        }
        const auto axis=[](float v){return static_cast<std::int8_t>(std::clamp(std::lround(v*127.f),-127L,127L));};
        // wmd_rts.gsc reads GetNormalizedMovement, not camera-look axes.
        const std::int8_t f=axis(forward),r=axis(right);
        if(f||r){memcpy(command+0x1b,&f,1);memcpy(command+0x1c,&r,1);}
        if(a.samples<=12||a.samples%120==0)
            trace("t5_vr_aircraft_monitor_axes","forward=%d right=%d native_command=%d",int(f),int(r),context.command_number);
        return;
    }
    if(a.room_scale_occurrence) {
        auto& pending=room_scale_commit;
        room_scale_native::Facts facts{},again{};
        PreCommandSnapshot current{};
        const auto now=GetTickCount64();
        if(read_pre_command_snapshot(&current)&&swimming::rd::same_pre(current,a.room_scale_pre)&&
           room_scale_native::copy(a.base,current,context,now,&facts)&&
           command_input_generation_current(a.room_scale_rig)) {
            auto& sample=pending.sample;
            sample.rig=a.room_scale_rig;sample.origin=facts.origin;
            sample.body_yaw=context.viewangles[1]+facts.walking.delta_angles[1];
            sample.epoch=current.gameplay_epoch;sample.player=facts.walking.predicted.ps;
            sample.now=now;sample.command_number=context.command_number;sample.eligible=true;
            sample.user_movement=a.controls.forward!=0||a.controls.right!=0||command[0x1b]!=0||command[0x1c]!=0;
            pending.next=a.room_scale_follow;pending.plan=pending.next.step(sample);
            if(pending.plan.valid) {
                const auto relative=wawvr::xr::OpenXrPoseToIwRelative(sample.rig.frame.head_center,pending.plan.anchor,
                    input::room_scale::units_per_meter);
                auto end=facts.eye;
                end.x+=pending.plan.residual.x;end.y+=pending.plan.residual.y;end.z+=relative.position.z;
                room_scale_native::Sweep sweep{};
                float camera_radius{};
                const bool traced=input::room_scale::camera_clearance(sample.rig.frame,facts.near_clip,&camera_radius)&&
                    room_scale_native::sweep(a.base,facts.eye,end,&sweep,camera_radius);
                if(room_scale_native::copy(a.base,current,context,GetTickCount64(),&again)&&
                   room_scale_native::same(facts,again)&&command_input_generation_current(sample.rig)) {
                    pending.query_ok=traced;pending.query_solid=sweep.solid;pending.query_fraction=sweep.fraction;
                    pending.fraction=!traced||sweep.solid?0.f:sweep.fraction;
                    pending.next.clip(sample,pending.plan,pending.fraction,sweep.solid);
                    pending.view.eye={facts.eye.x+(end.x-facts.eye.x)*pending.fraction,
                        facts.eye.y+(end.y-facts.eye.y)*pending.fraction,facts.eye.z+(end.z-facts.eye.z)*pending.fraction};
                    pending.view.cg=current.native_cg_identity;pending.view.epoch=current.gameplay_epoch;
                    pending.view.camera_radius=traced?camera_radius:0.f;pending.view.near_clip=facts.near_clip;
                    pending.view.horizontal_discontinuity=pending.plan.rebased;
                    pending.view.vertical_clipped=pending.fraction<1;pending.prepared=true;
                    if(!sample.user_movement) {
                        const auto f=static_cast<std::int8_t>(std::clamp(std::lround(pending.plan.forward*127.f),-127L,127L));
                        const auto r=static_cast<std::int8_t>(std::clamp(std::lround(pending.plan.right*127.f),-127L,127L));
                        memcpy(command+0x1b,&f,1);memcpy(command+0x1c,&r,1);
                    }
                }
            }
        }
    }
    // Preserve native buttons, weapon/script overrides, command time, ordinary
    // viewangles and opaque fields. The optional finish scope separately owns
    // only gun-angle bytes0x20..0x23. Neutral axes preserve desktop movement.
    const int forward=static_cast<int>(std::lround(a.controls.forward*127.f));
    const int right=static_cast<int>(std::lround(a.controls.right*127.f));
    if(forward==0 && right==0) return;
    const std::int8_t f=static_cast<std::int8_t>(std::clamp(forward,-127,127));
    const std::int8_t r=static_cast<std::int8_t>(std::clamp(right,-127,127));
    memcpy(command+0x1b,&f,1);memcpy(command+0x1c,&r,1);
    if(a.samples<=12 || a.samples%120==0)
        trace("t5_vr_move","forward=%d right=%d native_command=%d field_writes=2",int(f),int(r),context.command_number);
}
}
bool configure_input_adapter(HMODULE executable,bool command_hook,bool weapon_hook) noexcept {
    if(!enabled(L"BOVR_INPUT_PROBE") || !enabled(L"BOVR_XR_STEREO_PROBE") || !command_hook) return false;
    if(adapter) return true;
    if(executable!=GetModuleHandleW(nullptr) || reinterpret_cast<std::uintptr_t>(executable)!=0x400000) return false;
    try {
        if(!validate_path(executable_path()).accepted) return false;
        auto* image=reinterpret_cast<unsigned char*>(executable);
        if(bovr::native_guard::compare(image+execute_rva, 12, "3e9cdd8571bdb17f3bafeb720da0fb12919e3a475eeb0969712782b3736a8a7d")||
           !mounted_delivery::adjust_angles_loaded_guard(image)) return false;
        const bool cycle=enabled(L"BOVR_WEAPON_CYCLE_PROBE");
        const bool cod4=enabled(L"BOVR_COD4_CONTROLS");
        if(cycle && (enabled(L"BOVR_CONVENTIONAL_OFFHAND_PROBE") || !cycle_guards_match(image))) {
            trace("t5_vr_weapon_cycle_rejected","reason=configuration_guard_or_offhand_conflict");return false;
        }
        if(cycle&&cod4&&!alternate_guards_match(image)) {
            trace("t5_vr_weapon_cycle_rejected","reason=action_slot_loaded_guards");return false;
        }
        const bool mounted=requested(L"BOVR_MOUNTED_LOOK_PROBE");
        if(mounted&&(!enabled(L"BOVR_MOUNTED_LOOK_PROBE")||!cycle||!enabled(L"BOVR_EXACT_PROFILE_AIM_PROBE")||
           !mounted_delivery::loaded_guards(image))) {
            trace("t5_vr_mounted_rejected","reason=configuration_or_loaded_alias_guards");return false;
        }
        // Normal initialization configures aiming below. Mounted delivery must
        // establish and verify that dependency before installing its callbacks.
        if(mounted&&(!configure_native_command_aim(executable,command_hook)||!exact_profile_aim_enabled()))return false;
        auto* candidate=new Adapter(reinterpret_cast<std::uintptr_t>(executable),enabled(L"BOVR_LEFT_HANDED"),cycle,requested(L"BOVR_EXACT_PROFILE_AIM_PROBE"),mounted);
        candidate->cod4_controls=cod4;
        candidate->room_scale_enabled=cod4&&enabled(L"BOVR_ROOM_SCALE")&&
            room_scale_native::guards(reinterpret_cast<std::uintptr_t>(executable));
        trace("t5_room_scale_configured","enabled=%u native_axes_only=1 camera_radius=stereo_near_plane minimum_radius=6 skin=0.5 horizontal_follow=1 vertical_stance_writes=0",candidate->room_scale_enabled);
        // Crossbow uses native projectile code unchanged.
        candidate->left_handed=enabled(L"BOVR_LEFT_HANDED");
        candidate->smooth_turn=enabled(L"BOVR_SMOOTH_TURN");
        candidate->button_grenades=cod4&&enabled(L"BOVR_BUTTON_GRENADES");
        candidate->simulator_slingshot_load=enabled(L"BOVR_SIMULATOR_AUTO_LOAD_SLINGSHOT");
        candidate->simulator_motorcycle_load=enabled(L"BOVR_SIMULATOR_AUTO_LOAD_MOTORCYCLE");
        wchar_t movement_value[32]{};
        const auto movement_length=GetEnvironmentVariableW(L"BOVR_MOVEMENT_ORIENTATION",movement_value,32);
        candidate->movement_orientation=movement_length&&movement_length<32&&_wcsicmp(movement_value,L"controller")==0?
            input::MovementOrientation::controller:input::MovementOrientation::hmd;
        trace("t5_vr_comfort_options","smooth_turn=%u smooth_degrees_per_second=90 snap_degrees=%u button_grenades=%u movement_orientation=%s",
            candidate->smooth_turn,cod4?45u:30u,candidate->button_grenades,
            candidate->movement_orientation==input::MovementOrientation::controller?"controller":"hmd");
        candidate->helicopter_enabled=cod4&&cycle&&mounted&&helicopter_delivery::loaded_guards(image)&&helicopter_cannon_delivery::loaded_guards(image);
        trace("t5_vr_helicopter_configured","enabled=%u keys=28700..28709 flight=left_stick look=right_stick cannon=right_trigger_attack_and_vehicleattack huey_cannon=right_trigger_attack_and_vehicleattacksecond rockets=left_trigger altitude=A_B rocket_reload=right_grip boat_type4=1 boat_primary=right_trigger_vehicleattacksecond boat_drive=left_stick boat_secondary=left_trigger_throw boat_aim=right_controller",candidate->helicopter_enabled);
        candidate->motorcycle_enabled=cod4&&cycle&&mounted&&helicopter_cannon_delivery::loaded_guards(image);
        trace("t5_vr_motorcycle_configured","enabled=%u keys=28720..28728 drive=left_stick_y steer=left_stick_x fire=right_trigger_attack_and_vehicleattack aim=controller_rotation secondary=left_trigger handbrake=A reload=right_grip use=X truck_gunner=1 rebirth_apc_gunner=1 apc_primary=right_trigger_attack apc_secondary=left_trigger_frag apc_drive=script",
            candidate->motorcycle_enabled);
        candidate->crosshair_enabled=cod4&&crosshair_guards_match(image);
        if(cod4&&!candidate->crosshair_enabled){delete candidate;return false;}
        if(candidate->cod4_controls)candidate->router=input::ActionRouter(enabled(L"BOVR_LEFT_HANDED"),true);
        if(mounted&&candidate->mounted_source.configuration()!=mounted_look::Status::accepted) {
            trace("t5_vr_mounted_rejected","reason=copied_context_factory");delete candidate;return false;
        }
        if(cod4&&!configure_manual_grenades(executable,weapon_hook)){delete candidate;return false;}
        if(!set_command_callbacks({candidate,before_native,after_native})) {delete candidate;return false;}
        adapter=candidate;
        if(candidate->crosshair_enabled)trace("t5_vr_crosshair_preference_configured","enabled=1 delivery=native_named_command current1_only=1 native_instruction_patches=0 direct_dvar_writes=0");
        if(!mounted)configure_native_command_aim(executable,command_hook);
        if(cod4)configure_crossbow_script_aim(executable);
        if(mounted)trace("t5_vr_mounted_configured","enabled=1 delivery=balanced_lookup_lookdown keys=28680,28681 native_pitch_writes=0 native_instruction_hooks_added=0");
        if(cycle)trace("t5_vr_weapon_cycle_configured","enabled=1 delivery=weapnext_once native_instruction_hooks_added=0 weapon_field_writes=0 current_pre_command_required=1 exact_profile_combination=%u",exact_profile_aim_enabled());
        if(cycle&&cod4)trace("t5_vr_alternate_configured","enabled=1 control=dominant_grip_press delivery=balanced_unique_alt_actionslot native_instruction_hooks_added=0");
        trace("t5_vr_input_configured","native_dispatch=guarded_game_thread key_ownership=28672_plus_action analog_offsets=1b,1c menus=unimplemented native_command_aim=%u physical_muzzle=unimplemented",native_command_aim_enabled());
        return true;
    } catch(...) {return false;}
}
}

