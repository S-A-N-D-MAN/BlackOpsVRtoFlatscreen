// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/weapon_probe.hpp"
#include "t5/native_camera_mode.hpp"
#include "t5/identity.hpp"
#include "t5/weapon_lifecycle.hpp"
#include "t5/weapon_barrel_probe.hpp"
#include "t5/weapon_animation_probe.hpp"
#include "t5/weapon_borrowed_adapter.hpp"
#include "t5/weapon_render_admission.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/weapon_profile_probe.hpp"
#include "t5/manual_grenade_adapter.hpp"
#include "t5/manual_grenade_renderer.hpp"
#include "t5/primary_weapon_variant.hpp"
#include "t5/support_weapon_context.hpp"
#include "t5/rebirth_native_prop_context.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstring>

#if !defined(_M_IX86)
#error The pinned weapon call ABI requires MSVC x86.
#endif
namespace bovr::t5 {
namespace {
constexpr std::uintptr_t call_rva=0x277856,native_rva=0x10b6f0;
template<std::size_t N> bool copy_variant_text_unchecked(std::uintptr_t pointer,char (&out)[N]) noexcept {
    std::memset(out,0,N);if(!pointer||pointer>UINT32_MAX-N)return false;
    for(std::size_t i=0;i<N;++i){const auto c=*reinterpret_cast<const unsigned char*>(pointer+i);
        if(!c)return i>0;if(c<0x20||c>0x7e||i==N-1)return false;out[i]=static_cast<char>(c);}
    return false;
}


enum { animation_words=sizeof(WeaponAnimationStorage)/4, weapon_argument_word=19+animation_words,
       weapon_return_word=18+animation_words, weapon_reserved_bytes=36+sizeof(WeaponAnimationStorage),
       weapon_copy_argument_offset=56+sizeof(WeaponAnimationStorage) };
void* original_weapon{};
std::uintptr_t weapon_image_base{};
std::atomic<unsigned long> weapon_calls{};
std::atomic<unsigned long> weapon_pre_calls{};
SRWLOCK weapon_callback_lock=SRWLOCK_INIT;
WeaponCallbacks weapon_callbacks{};
unsigned weapon_callbacks_in_flight{};
bool weapon_placement_enabled{};
volatile LONG weapon_clear_scope_enabled{};
std::atomic<std::uint64_t> weapon_clear_scope_serial{};
thread_local WeaponNativeClearScope weapon_clear_scope{};
thread_local WeaponRenderWitness weapon_render_witness{};
struct ClearInvocationStorage {std::uint64_t token{};bool entered{},suppress_native{};};
static_assert(sizeof(ClearInvocationStorage)<=sizeof(WeaponAnimationStorage));
static_assert(offsetof(ClearInvocationStorage,suppress_native)==9);
enum {weapon_suppress_native_byte=18*4+9};
ClearInvocationStorage* clear_storage(std::uint32_t* saved) noexcept {
    return reinterpret_cast<ClearInvocationStorage*>(saved+18);
}
void enter_clear_scope(std::uint32_t* saved) noexcept {
    auto* scope=clear_storage(saved);*scope={};
    const auto token=weapon_clear_scope_serial.fetch_add(1)+1;
    ++weapon_clear_scope.depth;weapon_clear_scope.token=token;
    weapon_clear_scope.valid=token&&weapon_clear_scope.depth==1;
    scope->token=token;scope->entered=true;
}
void leave_clear_scope(std::uint32_t* saved) noexcept {
    auto* scope=clear_storage(saved);if(!scope->entered)return;
    scope->entered=false;
    retire_borrowed_weapon_invocation(scope->token);
    if(weapon_clear_scope.depth)--weapon_clear_scope.depth;
    weapon_clear_scope.token=0;weapon_clear_scope.valid=false;
}
struct WeaponSnapshot {
    std::uintptr_t cg{},viewmodel_info{},dobj{},pose{},server_client{},server_clients{},server_link{};
    int local_client{},draw_gun{},command_number{},key_catcher{},connection_state{},native_override{};
    int pm_type{},health{},primary{},offhand{},weapon_state{},other_weapon_state{},stack_count{};
    int third_person{},cubemap{},draw_suppression{};
    std::uint32_t pm_flags{},weap_flags{},eflags{},eflags2{};
    int server_number{},server_health{},server_pm_type{},server_ps_health{},server_connected{},server_session{};
    unsigned char predicted_client{},server_client_number{};
    std::uint32_t server_flags{},server_pm_flags{},server_weap_flags{},server_eflags{},server_eflags2{};
    float placement[8]{},matrix[12]{},pose_origin[3]{},pose_angles[3]{};
    bool readable{},local_identity{},normal_context{},server_identity{},server_normal{},finite_pose{};
    bool linked_weapon_context{},linked_server_context{};
};
template<class T> T load_at(std::uintptr_t address) noexcept {
    T value{};memcpy(&value,reinterpret_cast<const void*>(address),sizeof(T));return value;
}
bool copy_support_descriptor(void*,std::uintptr_t at,void* out,std::size_t size) noexcept {
    if(!out||!support_weapon::detail::bounded(at,size))return false;
    __try {memcpy(out,reinterpret_cast<const void*>(at),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
WeaponSnapshot read_weapon(int local,const void* placement,const void* ps_arg,const void* cent,int draw) noexcept {
    WeaponSnapshot s{};s.local_client=local;s.draw_gun=draw;
    const auto base=weapon_image_base;
    __try {
        s.cg=load_at<std::uintptr_t>(base+0x2bf5354);
        if(!s.cg || !placement || !ps_arg || !cent) return s;
        const auto ps=reinterpret_cast<std::uintptr_t>(ps_arg);
        s.local_identity=local==0 && ps==s.cg+0x8a364 && reinterpret_cast<std::uintptr_t>(cent)==s.cg+0x8bd7c;
        // Never traverse another caller's ps, even in this passive observer.
        if(!s.local_identity) return s;
        s.predicted_client=load_at<unsigned char>(ps+0x130);
        s.local_identity=s.predicted_client==0;
        s.pm_type=load_at<int>(ps+4);s.health=load_at<int>(ps+0x1c4);
        s.pm_flags=load_at<std::uint32_t>(ps+0xc);s.weap_flags=load_at<std::uint32_t>(ps+0x10);
        s.eflags=load_at<std::uint32_t>(ps+0xe0);s.eflags2=load_at<std::uint32_t>(ps+0xe4);
        s.primary=load_at<unsigned char>(ps+0x144);s.offhand=load_at<int>(ps+0x134);
        s.weapon_state=load_at<int>(ps+0x158);s.other_weapon_state=load_at<int>(ps+0x15c);
        s.stack_count=load_at<int>(ps+0x1b0);
        s.third_person=load_at<int>(s.cg+0x8a360);s.cubemap=load_at<int>(s.cg+0xc);
        s.draw_suppression=load_at<int>(s.cg+0xa9d44);
        s.viewmodel_info=load_at<std::uintptr_t>(base+0x81c6d8);
        if(s.viewmodel_info) s.dobj=load_at<std::uintptr_t>(s.viewmodel_info);
        s.pose=s.cg+0xc5e04;
        memcpy(s.placement,placement,sizeof(s.placement));
        memcpy(s.matrix,reinterpret_cast<const void*>(s.cg+0xa9d14),sizeof(s.matrix));
        memcpy(s.pose_origin,reinterpret_cast<const void*>(s.pose+0x24),sizeof(s.pose_origin));
        memcpy(s.pose_angles,reinterpret_cast<const void*>(s.pose+0x30),sizeof(s.pose_angles));
        s.command_number=load_at<int>(base+0x2593850);
        s.key_catcher=load_at<int>(base+0x2510160);s.connection_state=load_at<int>(base+0x2510164);
        s.native_override=load_at<int>(base+0x2511e2c); // Current value, not pre-CreateCmd authority.
        const auto entity=base+0x16796f8;
        s.server_clients=load_at<std::uintptr_t>(base+0x1803140);
        s.server_client=load_at<std::uintptr_t>(entity+0x13c);
        s.server_number=load_at<int>(entity);s.server_health=load_at<int>(entity+0x184);
        s.server_link=load_at<std::uintptr_t>(entity+0x298);
        s.server_identity=s.server_client && s.server_client==s.server_clients && s.server_number==0;
        if(s.server_identity) {
            const auto p=s.server_client;
            s.server_client_number=load_at<unsigned char>(p+0x130);
            s.server_identity=s.server_client_number==0;
            s.server_pm_type=load_at<int>(p+4);s.server_ps_health=load_at<int>(p+0x1c4);
            s.server_connected=load_at<int>(p+0x1a2c);s.server_session=load_at<int>(p+0x1a18);
            s.server_flags=load_at<std::uint32_t>(p+0x1c0c);
            s.server_pm_flags=load_at<std::uint32_t>(p+0xc);s.server_weap_flags=load_at<std::uint32_t>(p+0x10);
            s.server_eflags=load_at<std::uint32_t>(p+0xe0);s.server_eflags2=load_at<std::uint32_t>(p+0xe4);
        }
        s.readable=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return s;}
    s.finite_pose=true;
    for(float v:s.placement) s.finite_pose=s.finite_pose && std::isfinite(v);
    for(float v:s.matrix) s.finite_pose=s.finite_pose && std::isfinite(v);
    for(float v:s.pose_origin) s.finite_pose=s.finite_pose && std::isfinite(v);
    for(float v:s.pose_angles) s.finite_pose=s.finite_pose && std::isfinite(v);
    using namespace primary_weapon_tracking;
    const auto mode=client_mode({s.pm_type,s.health,s.pm_flags,s.weap_flags,s.eflags,s.eflags2},
        {s.third_person,s.key_catcher,s.connection_state,s.native_override!=0,s.cubemap!=0,s.draw_suppression!=0});
    const auto server_mode_value=server_mode({{s.server_pm_type,s.server_ps_health,s.server_pm_flags,
        s.server_weap_flags,s.server_eflags,s.server_eflags2},s.server_identity,s.server_health,
        s.server_connected,s.server_session,s.server_flags,static_cast<std::uint32_t>(s.server_link)});
    s.normal_context=s.readable&&s.local_identity&&s.finite_pose&&s.dobj&&draw&&s.primary>0&&mode==Mode::ordinary;
    s.server_normal=server_mode_value==Mode::ordinary;
    s.linked_weapon_context=s.readable&&s.local_identity&&s.finite_pose&&s.dobj&&draw&&s.primary>0&&mode==Mode::linked;
    s.linked_server_context=server_mode_value==Mode::linked;
    return s;
}
NativeWeaponContext read_weapon_context(const WeaponSnapshot& s,std::uint64_t serial) noexcept {
    NativeWeaponContext c{};c.native_thread=GetCurrentThreadId();c.call_serial=serial;
    c.local_client=s.local_client;c.draw_gun=s.draw_gun;c.command_number=s.command_number;
    c.cg_identity=s.cg;c.refdef_identity=s.cg?s.cg+0x8c100:0;c.viewmodel_identity=s.viewmodel_info;c.dobj_identity=s.dobj;
    c.normal_context=s.normal_context;c.server_normal=s.server_normal;
    c.linked_weapon_context=s.linked_weapon_context;c.linked_server_context=s.linked_server_context;
    c.server_link_identity=static_cast<std::uint32_t>(s.server_link);
    c.weapon_index=static_cast<std::uint32_t>(s.primary);c.offhand=s.offhand;c.weapon_state=s.weapon_state;
    c.other_weapon_state=s.other_weapon_state;c.stack_count=s.stack_count;
    memcpy(&c.placement,s.placement,sizeof(c.placement));
    if(!s.readable||!s.local_identity) return c;
    __try {
        const auto cg=s.cg,ref=c.refdef_identity;
        c.native_time=load_at<int>(cg+0x8a348);c.native_frame_time=load_at<int>(cg+0x8a344);c.refdef_time=load_at<int>(cg+0x8c158);
        c.command_clock=load_at<int>(weapon_image_base+0x2081764);
        auto& v=c.stock_view;v.x=load_at<int>(ref);v.y=load_at<int>(ref+4);v.width=load_at<int>(ref+8);v.height=load_at<int>(ref+12);
        v.tan_half_fov_x=load_at<float>(ref+0x10);v.tan_half_fov_y=load_at<float>(ref+0x14);
        memcpy(&v.origin,reinterpret_cast<void*>(ref+0x20),sizeof(v.origin));
        memcpy(&v.axis,reinterpret_cast<void*>(ref+0x34),sizeof(v.axis));v.near_clip=load_at<float>(ref+0x5c);
        c.body_valid=c.native_time==c.refdef_time;
        // The manual-grenade visual wrapper is the sole transition exception:
        // it proves that the original primary DObj is frozen and the selector
        // flag is masked only for this synchronous render call.
        c.manual_grenade_primary_visual=manual_grenade_primary_visual_scope(c);
        c.primary_viewmodel=c.weapon_index>0 && (!(s.weapon_state>=18&&s.weapon_state<=26)||c.manual_grenade_primary_visual);
        c.valid=true;
        if(c.primary_viewmodel) {
            c.variant_identity=load_at<std::uintptr_t>(weapon_image_base+0x7e19a8+c.weapon_index*4);
            if(c.variant_identity) {
                c.definition_identity=load_at<std::uintptr_t>(c.variant_identity+8);
                char internal[64]{},localized[64]{};
                const auto internal_pointer=load_at<std::uintptr_t>(c.variant_identity);
                const auto localized_pointer=load_at<std::uintptr_t>(c.variant_identity+0xc);
                if(copy_variant_text_unchecked(internal_pointer,internal)&&copy_variant_text_unchecked(localized_pointer,localized))
                    c.primary_knife_variant=primary_knife_variant(internal,localized);
            }
            if(c.definition_identity) {
                c.weapon_type=load_at<int>(c.definition_identity+0x1c);c.weapon_class=load_at<int>(c.definition_identity+0x20);
                c.dual_wield=load_at<unsigned char>(c.definition_identity+0x56a)!=0;c.weapon_definition_valid=true;
                support_weapon::Descriptor marker{};
                // The mission radio lacks the gun/flash tags required by a
                // tracked firearm profile. Preserve its native visual while
                // permitting ordinary world stereo, only for this exact asset.
                c.support_marker_native_visual=weapon_image_base==0x400000&&
                    primary_weapon_mode(c)==primary_weapon_tracking::Mode::ordinary&&
                    c.weapon_type==2&&c.weapon_class==4&&!c.dual_wield&&
                    support_weapon::read_descriptor(copy_support_descriptor,nullptr,c.weapon_index,&marker)&&
                    marker.variant==c.variant_identity&&marker.definition==c.definition_identity&&
                    s.server_identity&&s.server_client==load_at<std::uintptr_t>(0x1c03140)&&
                    s.server_client==load_at<std::uintptr_t>(0x1a796f8+0x13c)&&
                    c.cg_identity==load_at<std::uintptr_t>(0x2ff5354)&&
                    c.weapon_index==load_at<unsigned char>(c.cg_identity+0x8a364+0x144)&&
                    c.weapon_index==load_at<unsigned char>(s.server_client+0x144);
                c.rebirth_native_prop_visual=weapon_image_base==0x400000&&
                    primary_weapon_mode(c)==primary_weapon_tracking::Mode::ordinary&&
                    c.weapon_type==0&&c.weapon_class==0&&!c.dual_wield&&s.server_identity&&
                    rebirth_prop::read_current(copy_support_descriptor,nullptr,
                        {c.weapon_index,c.cg_identity,s.server_client,c.variant_identity,c.definition_identity});
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {c.valid=false;c.body_valid=false;c.weapon_definition_valid=false;}
    WeaponLifetime lifetime{};
    if(read_weapon_lifetime(c.viewmodel_identity,c.dobj_identity,&lifetime)) {c.model_generation=lifetime.generation;c.model_lifetime_verified=lifetime.verified;}
    return c;
}
bool dispatch_weapon_callback(bool before,const WeaponSnapshot& s,std::uint64_t serial,input::ScaledWeaponPlacement* scratch=nullptr) noexcept {
    AcquireSRWLockExclusive(&weapon_callback_lock);
    const auto callbacks=weapon_callbacks;
    const bool active=before?(callbacks.before_native!=nullptr||callbacks.select_placement!=nullptr):callbacks.after_native!=nullptr;
    if(active) ++weapon_callbacks_in_flight;
    ReleaseSRWLockExclusive(&weapon_callback_lock);
    if(!active) return false;
    const auto context=read_weapon_context(s,serial);
    bool selected=false;
    try {
        if(before) {
            if(callbacks.before_native) callbacks.before_native(callbacks.context,context);
            if(weapon_placement_enabled&&scratch&&callbacks.select_placement) selected=callbacks.select_placement(callbacks.context,context,scratch);
        }
        else callbacks.after_native(callbacks.context,context);
    } catch(...) {trace("t5_weapon_callback_exception","before=%u",before);}
    AcquireSRWLockExclusive(&weapon_callback_lock);--weapon_callbacks_in_flight;ReleaseSRWLockExclusive(&weapon_callback_lock);
    if(!selected||!scratch) return false;
    const auto& q=scratch->quaternion;const auto& p=scratch->origin;
    return std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z)&&std::isfinite(q.w)&&
        std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::isfinite(scratch->scale)&&
        std::fabs(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w-1.0F)<0.001F&&scratch->scale==context.placement.scale;
}
void __cdecl before_weapon(std::uint32_t* saved) {
    const DWORD previous=GetLastError();
    const auto serial=weapon_pre_calls.fetch_add(1,std::memory_order_relaxed)+1;
    const auto* args=saved+weapon_argument_word;
    saved[17]=args[1]; // Default selected pointer is the original native argument.
    auto* scratch=reinterpret_cast<input::ScaledWeaponPlacement*>(saved+9);
    // Empty callbacks avoid all new native data reads in observer-only mode.
    AcquireSRWLockShared(&weapon_callback_lock);const bool active=weapon_callbacks.before_native!=nullptr||weapon_callbacks.select_placement!=nullptr;ReleaseSRWLockShared(&weapon_callback_lock);
    const bool animation=weapon_animation_probe_enabled();
    __try {
    if(weapon_clear_scope_enabled)enter_clear_scope(saved);
    if(!weapon_clear_scope_enabled||weapon_clear_scope.depth==1)weapon_render_witness={};
    else weapon_render_witness.nested=true;
    if(active||animation||weapon_borrowed_probe_enabled()) {
        const auto s=read_weapon(static_cast<int>(args[0]),reinterpret_cast<void*>(args[1]),reinterpret_cast<void*>(args[2]),
            reinterpret_cast<void*>(args[3]),static_cast<int>(args[4]));
        if(active&&dispatch_weapon_callback(true,s,serial,scratch)) saved[17]=reinterpret_cast<std::uint32_t>(scratch);
        if(weapon_borrowed_probe_enabled()) {
            const auto copied=read_weapon_context(s,serial);
            const bool writer=weapon_borrowed_placement_enabled();
            if(!weapon_clear_scope_enabled||weapon_clear_scope.depth==1)
                weapon_render_witness={copied,true,expects_tracked_weapon_render(copied,writer),false,false};
            if(native_support_marker_visual(copied)) {
                static std::atomic<unsigned> reports{};const auto n=++reports;
                if(n<=8||n%120==0)trace("t5_support_native_visual","count=%u weapon=%u call=%llu tracked_profile_required=0 native_radio_preserved=1",
                    n,copied.weapon_index,copied.call_serial);
            }
            if(native_rebirth_prop_visual(copied)) {
                static std::atomic<unsigned> reports{};const auto n=++reports;
                if(n<=8||n%120==0)trace("t5_rebirth_prop_native_visual","count=%u weapon=%u call=%llu tracked_profile_required=0 native_prop_preserved=1",
                    n,copied.weapon_index,copied.call_serial);
            }
            observe_borrowed_weapon_before(copied);
            if(writer&&select_borrowed_weapon_placement(copied,scratch)) {
                saved[17]=reinterpret_cast<std::uint32_t>(scratch);
                if(weapon_render_witness.producer.call_serial==serial)weapon_render_witness.substituted=true;
            } else if(weapon_render_witness.expected&&weapon_render_witness.producer.call_serial==serial&&
                      weapon_clear_scope_enabled&&weapon_clear_scope.depth==1&&
                      suppress_untracked_primary_knife_for_world(copied,writer)&&
                      reserve_hidden_primary_weapon_scene_frame(copied)) {
                // Suppress only after this exact native build owns a fresh XR
                // reservation. Claimed/busy history draws remain native calls.
                auto* control=clear_storage(saved);
                control->suppress_native=true;
                weapon_render_witness.hidden_primary_weapon=true;
                weapon_render_witness.native_call_suppressed=true;
                if(serial<=12||serial%120==0)trace("t5_knife_world_fallback",
                    "call=%lu weapon=%u stock_scale=%.6f stock_native_call_suppressed=1 exact_scene_reserved=1 original_argument_writes=0",
                    serial,copied.weapon_index,copied.placement.scale);
            } else if(weapon_render_witness.expected&&weapon_render_witness.producer.call_serial==serial&&
                      weapon_clear_scope_enabled&&weapon_clear_scope.depth==1&&
                      suppress_untracked_primary_knife_for_world(copied,writer)&&
                      select_borrowed_knife_visual_continuity(copied,scratch)) {
                // The current XR frame is already owned by another native
                // build. Keep this non-XR history draw aligned with the most
                // recent completed knife placement instead of alternating
                // between a stock arm and no weapon at all.
                saved[17]=reinterpret_cast<std::uint32_t>(scratch);
                weapon_render_witness.substituted=true;
                if(serial<=12||serial%120==0)trace("t5_knife_visual_continuity",
                    "call=%lu weapon=%u native_call=1 headset_authority=0 original_argument_writes=0",
                    serial,copied.weapon_index);
            }
        }
        if(animation)observe_weapon_animation_before(read_weapon_context(s,serial),reinterpret_cast<WeaponAnimationStorage*>(saved+18));
    }
    }__finally {if(weapon_clear_scope_enabled&&AbnormalTermination())leave_clear_scope(saved);SetLastError(previous);}
}
// Base observer copies values. The independent default-off barrel observer can
// call the native bone helper in this proven frontend scope; it copies its outputs.
// An opt-in native bone call may propagate an engine exception. Do not add a
// noexcept or catch-all boundary here: the engine may still own its DObj lock.
void __cdecl observe_weapon(const std::uint32_t* saved) {
    const DWORD previous=GetLastError();
    __try {
    const auto serial=weapon_calls.fetch_add(1,std::memory_order_relaxed)+1;
    const bool log=serial<=12 || serial%120==0;
    AcquireSRWLockShared(&weapon_callback_lock);const bool active=weapon_callbacks.after_native!=nullptr;ReleaseSRWLockShared(&weapon_callback_lock);
    const bool barrel=weapon_barrel_probe_enabled();
    const bool profile=weapon_profile_probe_enabled();
    const bool animation=weapon_animation_probe_enabled();
    const bool grenade=manual_grenade_render_pending();
    if(!log&&!active&&!barrel&&!profile&&!animation&&!grenade&&!weapon_native_clear_probe_enabled()) {SetLastError(previous);return;}
    const bool native_suppressed=weapon_clear_scope_enabled&&clear_storage(const_cast<std::uint32_t*>(saved))->suppress_native;
    const auto* args=saved+weapon_argument_word;
    const auto s=read_weapon(static_cast<int>(args[0]),reinterpret_cast<void*>(args[1]),
        reinterpret_cast<void*>(args[2]),reinterpret_cast<void*>(args[3]),static_cast<int>(args[4]));
    if(!native_suppressed) {
        dispatch_weapon_callback(false,s,serial);
        if(animation)observe_weapon_animation_after(read_weapon_context(s,serial),reinterpret_cast<WeaponAnimationStorage*>(const_cast<std::uint32_t*>(saved)+18));
        if(grenade)(void)submit_manual_grenade_model(read_weapon_context(s,serial));
        if(weapon_native_clear_probe_enabled()) {
            const auto scope=read_weapon_native_clear_scope();
            const auto* owned=clear_storage(const_cast<std::uint32_t*>(saved));
            const auto token=owned->entered&&scope.valid&&scope.token==owned->token?owned->token:0;
            observe_native_clear_weapon_return(read_weapon_context(s,serial),token);
        }
        if(barrel)observe_weapon_barrel_after(read_weapon_context(s,serial));
        if(profile)observe_weapon_profile_after(read_weapon_context(s,serial));
    }
    if(!log) {SetLastError(previous);return;}
    const bool substituted=saved[17]!=args[1];
    const auto* applied=substituted?reinterpret_cast<const float*>(saved+9):s.placement;
    trace("t5_weapon_placement_native","call=%lu argument_substituted=%u selected_origin=%.6f,%.6f,%.6f persistent_pose_origin=%.6f,%.6f,%.6f original_argument_writes=0 native_calls=%u pose_restore=0 shot_writes=0",
        serial,substituted,applied[4],applied[5],applied[6],s.pose_origin[0],s.pose_origin[1],s.pose_origin[2],!native_suppressed);
    trace("t5_weapon","call=%lu thread=%lu caller_rva=0x%lx local=%d placement=%p ps=%p cent=%p draw=%d readable=%u local_identity=%u normal_context=%u server_identity=%u server_normal=%u linked_weapon=%u linked_server=%u cg=%p viewmodel_info=%p dobj=%p persistent_pose=%p command=%d catcher=0x%x connection=%d current_override=%d pm=%d health=%d pm_flags=0x%x weap_flags=0x%x eflags=0x%x eflags2=0x%x primary=%d offhand=%d states=%d,%d stack_count=%d third_person=%d cubemap=%d suppressed=%d placement_origin=%.4f,%.4f,%.4f placement_quat=%.5f,%.5f,%.5f,%.5f scale=%.4f pose_origin=%.4f,%.4f,%.4f pose_angles=%.4f,%.4f,%.4f finite=%u original_calls=%u stock_primary_suppressed=%u base_observer_writes=0 base_observer_tag_calls=0",
        serial,GetCurrentThreadId(),static_cast<unsigned long>(saved[weapon_return_word]-weapon_image_base),s.local_client,
        reinterpret_cast<void*>(args[1]),reinterpret_cast<void*>(args[2]),reinterpret_cast<void*>(args[3]),s.draw_gun,
        s.readable,s.local_identity,s.normal_context,s.server_identity,s.server_normal,s.linked_weapon_context,s.linked_server_context,reinterpret_cast<void*>(s.cg),
        reinterpret_cast<void*>(s.viewmodel_info),reinterpret_cast<void*>(s.dobj),reinterpret_cast<void*>(s.pose),
        s.command_number,s.key_catcher,s.connection_state,s.native_override,s.pm_type,s.health,s.pm_flags,s.weap_flags,s.eflags,s.eflags2,
        s.primary,s.offhand,s.weapon_state,s.other_weapon_state,s.stack_count,s.third_person,s.cubemap,s.draw_suppression,
        s.placement[4],s.placement[5],s.placement[6],s.placement[0],s.placement[1],s.placement[2],s.placement[3],s.placement[7],
        s.pose_origin[0],s.pose_origin[1],s.pose_origin[2],s.pose_angles[0],s.pose_angles[1],s.pose_angles[2],s.finite_pose,!native_suppressed,native_suppressed);
    trace("t5_weapon_server","call=%lu entity=%p client=%p clients=%p number=%d client_number=%u health=%d ps_health=%d pm=%d connected=%d session=%d flags=0x%x pm_flags=0x%x weap_flags=0x%x eflags=0x%x eflags2=0x%x link=%p atomic_snapshot=0 game_writes=0",
        serial,reinterpret_cast<void*>(weapon_image_base+0x16796f8),reinterpret_cast<void*>(s.server_client),reinterpret_cast<void*>(s.server_clients),
        s.server_number,static_cast<unsigned>(s.server_client_number),s.server_health,s.server_ps_health,s.server_pm_type,
        s.server_connected,s.server_session,s.server_flags,s.server_pm_flags,s.server_weap_flags,s.server_eflags,s.server_eflags2,
        reinterpret_cast<void*>(s.server_link));
    SetLastError(previous);
    }__finally {if(weapon_clear_scope_enabled)leave_clear_scope(const_cast<std::uint32_t*>(saved));SetLastError(previous);}
}
// The optional guarded branch stays on its C++ bridge's own native SEH stack.
// Copy native output to the borrowed saved frame, then restore this thunk's
// caller nonvolatiles. No stack switch or retained saved-frame pointer exists.
__declspec(naked) void __cdecl invoke_weapon_saved(std::uint32_t*,void*) {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov eax,[esp+20]
        push dword ptr [eax+weapon_argument_word*4+16]
        push dword ptr [eax+weapon_argument_word*4+12]
        push dword ptr [eax+weapon_argument_word*4+8]
        push dword ptr [eax+68]
        push dword ptr [eax+weapon_argument_word*4]
        mov edx,[esp+44]
        fxrstor [edx]
        push dword ptr [eax+32]
        popfd
        mov edi,[eax]
        mov esi,[eax+4]
        mov ebp,[eax+8]
        mov ebx,[eax+16]
        mov edx,[eax+20]
        mov ecx,[eax+24]
        mov eax,[eax+28]
        call dword ptr [original_weapon]
        lea esp,[esp+20]
        pushfd
        pushad
        mov esi,esp
        mov edi,[esp+56]
        mov ecx,9
        cld
        rep movsd
        mov eax,[esp+60]
        fxsave [eax]
        lea esp,[esp+36]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
void __cdecl guarded_weapon_original(std::uint32_t* saved,void* fp) {
    __try {invoke_weapon_saved(saved,fp);}
    __finally {
        const auto error=GetLastError();
        if(AbnormalTermination())leave_clear_scope(saved);
        SetLastError(error);
    }
}
// Five cdecl words: copy them in reverse order without touching GP/FP input state.
// Native output EAX, flags, all GP and x87/SSE state survive the post observer.
// Saved frame: GP0..7, flags8, owned scratch9..16, selected pointer17,
// animation copied storage18..417, native return418, original arguments419..423.
// The 1600-byte comparison storage is owned by this same invocation. It is below
// one page, so the enlarged stack reservation does not skip a stack guard page.
// Placement scratch and selected pointer slots are unchanged. Scratch survives until
// the synchronous native call returns; persistent native pose is never restored.
__declspec(naked) void weapon_detour() {
    __asm {
        lea esp,[esp-weapon_reserved_bytes]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push ebp
        call before_weapon
        lea esp,[esp+4]
        fxrstor [esp]
        mov esp,ebp
        cmp dword ptr [weapon_clear_scope_enabled],0
        je unguarded_native
        cmp byte ptr [ebp+weapon_suppress_native_byte],0
        jne suppressed_native
        jmp guarded_native
    unguarded_native:
        popad
        popfd
        push dword ptr [esp+weapon_copy_argument_offset]
        push dword ptr [esp+weapon_copy_argument_offset]
        push dword ptr [esp+weapon_copy_argument_offset]
        push dword ptr [esp+44]
        push dword ptr [esp+weapon_copy_argument_offset]
        call dword ptr [original_weapon]
        lea esp,[esp+20]
        jmp native_finished
    suppressed_native:
        // Restore the caller's exact register/flag state and omit only this
        // render-only stock knife call. Post observers see the owned flag and
        // cannot claim that a tracked native render completed.
        popad
        popfd
        jmp native_finished
    guarded_native:
        // GP/flags remain in the caller's saved frame; the prior aligned FX
        // area below it still contains the exact original native input state.
        sub esp,528
        and esp,0xfffffff0
        push esp
        push ebp
        call guarded_weapon_original
        lea esp,[esp+8]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
    native_finished:
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        push ebp
        call observe_weapon
        lea esp,[esp+4]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        lea esp,[esp+weapon_reserved_bytes]
        ret
    }
}
bool patch_weapon_call(std::uintptr_t base) noexcept {
    if(original_weapon) return weapon_image_base==base;
    auto* site=reinterpret_cast<unsigned char*>(base+call_rva);
    if(bovr::native_guard::compare(site, 5, "16e9e1de1c92040d9b831c73e4b28f2f7be3ff8fe1925d676be4f106bfb227e6")) return false;
    std::int32_t relative{};memcpy(&relative,site+1,4);
    if(reinterpret_cast<std::uintptr_t>(site+5)+relative!=base+native_rva) return false;
    DWORD protection{},unused{};
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection)) return false;
    if(bovr::native_guard::compare(site, 5, "16e9e1de1c92040d9b831c73e4b28f2f7be3ff8fe1925d676be4f106bfb227e6")) {VirtualProtect(site,5,protection,&unused);return false;}
    weapon_image_base=base;original_weapon=reinterpret_cast<void*>(base+native_rva);
    const auto target=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&weapon_detour)-reinterpret_cast<std::uintptr_t>(site+5));
    memcpy(site+1,&target,4); // Keep CALL opcode and native caller cleanup unchanged.
    const BOOL restored=VirtualProtect(site,5,protection,&unused);
    FlushInstructionCache(GetCurrentProcess(),site,5);
    trace("t5_weapon_probe_installed","site=%p target=%p bytes=5 protection_restored=%u native_calls_per_dispatch=1 game_writes=0 tag_calls=0",site,original_weapon,restored);
    return true;
}
}
bool set_weapon_callbacks(const WeaponCallbacks& callbacks) noexcept {
    AcquireSRWLockExclusive(&weapon_callback_lock);
    const bool nonempty=callbacks.before_native||callbacks.after_native||callbacks.select_placement;
    wchar_t flag[8]{};GetEnvironmentVariableW(L"BOVR_WEAPON_PLACEMENT",flag,8);
    const bool enabled=!wcscmp(flag,L"1");
    const bool allowed=!weapon_callbacks_in_flight && (!nonempty||original_weapon!=nullptr)&&(!callbacks.select_placement||enabled);
    if(allowed) {weapon_callbacks=callbacks;weapon_placement_enabled=enabled;}
    ReleaseSRWLockExclusive(&weapon_callback_lock);return allowed;
}
bool install_weapon_probe(HMODULE executable) noexcept {
    wchar_t enabled[8]{};GetEnvironmentVariableW(L"BOVR_WEAPON_PROBE",enabled,8);
    if(wcscmp(enabled,L"1")) return false;
    if(executable!=GetModuleHandleW(nullptr) || reinterpret_cast<std::uintptr_t>(executable)!=0x400000) return false;
    if(original_weapon) return weapon_image_base==reinterpret_cast<std::uintptr_t>(executable);
    try {
        std::string reason;
        if(!validate_path(executable_path()).accepted || !validate_loaded_headers(executable,reason)) return false;
        const auto base=reinterpret_cast<std::uintptr_t>(executable);
        if(bovr::native_guard::compare(reinterpret_cast<void*>(base+native_rva), 8, "02b0ebd35e11ffffde4193172e02cf67a664653e488500506d9c800864e3c675")) return false;
        return patch_weapon_call(base);
    } catch(...) {return false;}
}
bool enable_weapon_native_clear_scope() noexcept {
    if(!original_weapon)return false;
    InterlockedExchange(&weapon_clear_scope_enabled,1);return true;
}
std::uint64_t weapon_native_clear_last_token() noexcept {return weapon_clear_scope_serial.load();}
WeaponNativeClearScope read_weapon_native_clear_scope() noexcept {
    return weapon_clear_scope_enabled?weapon_clear_scope:WeaponNativeClearScope{};
}
WeaponRenderWitness take_weapon_render_witness() noexcept {
    auto copied=weapon_render_witness;weapon_render_witness={};
    if(weapon_clear_scope.depth)copied.nested=true;
    return copied;
}
bool copy_pose_clear_weapon_context(std::uintptr_t object,int local,std::uint64_t serial,NativeWeaponContext* out) noexcept {
    if(!out)return false;*out={};if(local!=0||!object||!weapon_image_base)return false;
    std::uintptr_t cg{};float origin[3]{},angles[3]{};int draw{};
    __try {
        cg=load_at<std::uintptr_t>(weapon_image_base+0x2bf5354);if(!cg)return false;
        const auto owner=load_at<std::uintptr_t>(weapon_image_base+0x81c6d8);
        if(!owner||load_at<std::uintptr_t>(owner)!=object)return false;
        const auto option=load_at<std::uintptr_t>(weapon_image_base+0x2b67f28);
        if(!option||!load_at<unsigned char>(option+0x18))return false;draw=1;
        memcpy(origin,reinterpret_cast<void*>(cg+0xc5e28),sizeof(origin));
        memcpy(angles,reinterpret_cast<void*>(cg+0xc5e34),sizeof(angles));
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    for(float v:origin)if(!std::isfinite(v))return false;
    for(float v:angles)if(!std::isfinite(v))return false;
    constexpr float half_radians=0.008726646259971648F;
    const auto p=angles[0]*half_radians,y=angles[1]*half_radians,r=angles[2]*half_radians;
    const auto cp=std::cos(p),sp=std::sin(p),cy=std::cos(y),sy=std::sin(y),cr=std::cos(r),sr=std::sin(r);
    input::ScaledWeaponPlacement place{{cy*cp*sr-sy*sp*cr,cy*sp*cr+sy*cp*sr,sy*cp*cr-cy*sp*sr,cy*cp*cr+sy*sp*sr},
                                       {origin[0],origin[1],origin[2]},1.0F};
    const auto snapshot=read_weapon(local,&place,reinterpret_cast<void*>(cg+0x8a364),reinterpret_cast<void*>(cg+0x8bd7c),draw);
    if(!snapshot.readable||snapshot.dobj!=object||snapshot.cg!=cg)return false;
    *out=read_weapon_context(snapshot,serial);
    return out->valid&&out->dobj_identity==object;
}
}
