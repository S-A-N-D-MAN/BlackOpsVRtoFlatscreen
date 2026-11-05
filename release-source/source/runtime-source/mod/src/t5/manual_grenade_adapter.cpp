// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/manual_grenade_adapter.hpp"
#include "input/manual_grenade.hpp"
#include "t5/grenade_throw_hook.hpp"
#include "t5/grenade_projectile_update.hpp"
#include "t5/manual_reload_adapter.hpp"
#include "t5/manual_grenade_render_math.hpp"
#include "t5/manual_grenade_renderer.hpp"
#include "t5/manual_grenade_native_guards.hpp"
#include "t5/manual_grenade_visual_pose.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/native_camera_mode.hpp"
#include "t5/identity.hpp"
#include "camera_comfort_logic.hpp"
#include "xr_math.h"
#include "loader/trace.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
namespace bovr::t5 {
namespace {
namespace m=input::grenade;namespace g=wawvr::mod;namespace xr=wawvr::xr;
using U32=std::uint32_t;
template<class T>T read(std::uintptr_t p){T v{};memcpy(&v,reinterpret_cast<const void*>(p),sizeof(v));return v;}
bool pointer(U32 p,unsigned extent){return p>=0x10000&&p<=UINT32_MAX-extent;}
bool finite(xr::Vec3f v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool visual_rig_current(const input::CommandRigSample& rig,std::uint64_t now) noexcept {
    const auto& frame=rig.frame;const auto& grip=frame.actions.hands[0].grip;
    const auto unit=[](xr::Quaternionf q){const float length=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
        return std::isfinite(length)&&std::fabs(length-1)<.003f;};
    return rig.xr_generation&&rig.anchor_generation==rig.xr_generation&&rig.published_ms&&now>=rig.published_ms&&now-rig.published_ms<=100&&
        frame.frame_id&&frame.actions.sequence&&frame.actions.focused&&frame.views_valid&&frame.should_render&&
        grip.active&&grip.position_valid&&grip.position_tracked&&grip.orientation_valid&&grip.orientation_tracked&&
        finite(grip.pose.position)&&unit(grip.pose.orientation)&&finite(rig.tracking_anchor.position)&&unit(rig.tracking_anchor.orientation);
}
xr::Vec3f transform(const xr::Basis3f& b,xr::Vec3f v){return {
    b.forward.x*v.x+b.left.x*v.y+b.up.x*v.z,b.forward.y*v.x+b.left.y*v.y+b.up.y*v.z,b.forward.z*v.x+b.left.z*v.y+b.up.z*v.z};}
struct Weapon {U32 index{},variant{},definition{},projectile_model{};int slot{},kind{},ammo{};g::ManualGrenadeLaunchCalibration tuning{};};
struct Native {U32 cg{},server{};int time{},state{},offhand{};bool ordinary{};xr::Vec3f eye{};xr::Basis3f body{};std::array<Weapon,2> weapons{};};
// Button grenades must not borrow the dual weapon's left-fire control. This
// denial reads the current primary independently of the later aim callback.
bool button_primary_single(const Native& n) noexcept {
    if(!n.ordinary||!pointer(n.cg,0x8a4a9))return false;
    __try {
        const U32 ps=n.cg+0x8a364;const auto weapon=read<unsigned char>(ps+0x144);
        if(!weapon)return false;const U32 variant=read<U32>(0xbe19a8+4*weapon);
        if(!pointer(variant,12))return false;const U32 definition=read<U32>(variant+8);
        if(!pointer(definition,0x56b)||read<unsigned char>(definition+0x56a)!=0)return false;
        return read<U32>(0x2ff5354)==n.cg&&read<int>(n.cg+0x8a348)==n.time&&
            read<unsigned char>(ps+0x144)==weapon&&read<U32>(0xbe19a8+4*weapon)==variant&&
            read<U32>(variant+8)==definition&&read<unsigned char>(definition+0x56a)==0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
int keyed(U32 ps,U32 offset,int key){for(unsigned i=0;i<15;++i)if(read<int>(ps+offset+i*8)==key)return read<int>(ps+offset+i*8+4);return 0;}
bool native_copy(const CommandContext& context,Native* output) noexcept {
    if(!output||!context.valid||context.local_client||context.key_catcher||context.native_override||context.connection_state<9)return false;
    Native n{};
    __try {
        n.cg=read<U32>(0x2ff5354);n.server=read<U32>(0x1a796f8+0x13c);
        if(!pointer(n.cg,0xcc900)||!pointer(n.server,0x600)||n.server!=read<U32>(0x1c03140))return false;
        const U32 ps=n.cg+0x8a364;
        if(read<int>(ps+4)||read<int>(ps+0x1c4)<=0||(read<U32>(ps+0xc)&0x800)||
           (read<U32>(ps+0x10)&0x100)||(read<U32>(ps+0xe0)&0x4300)||(read<U32>(ps+0xe4)&0x40040000)||
           read<int>(n.cg+0xc)||!native_first_person_camera(read<int>(n.cg+0x8a360))||read<int>(n.cg+0xa9d44))return false;
        n.time=read<int>(n.cg+0x8a348);if(n.time<=0||read<int>(n.cg+0x8c158)!=n.time)return false;
        n.state=read<int>(ps+0x158);n.offhand=read<unsigned char>(ps+0x134);
        n.ordinary=read<unsigned char>(ps+0x130)==0&&read<int>(ps+0x58)<=0&&!(read<U32>(ps+0x10)&0x180)&&n.state>=0&&n.state<18;
        if(!n.ordinary&&(n.state<18||n.state>26))return false;
        n.eye=read<xr::Vec3f>(n.cg+0x8c120);const auto axis=read<xr::Basis3f>(n.cg+0x8c134);
        if(!finite(n.eye)||!g::gravity_level_t4_camera_axis(axis,&n.body))return false;
        std::array<U32,15> held{};
        for(unsigned i=0;i<15;++i)held[i]=read<U32>(ps+0x1dc+i*24);
        for(const U32 index:held){
            if(!index)continue;if(index>=256)return false;
            const U32 v=read<U32>(0xbe19a8+4*index);if(!pointer(v,0x50))return false;
            const U32 d=read<U32>(v+8);if(!pointer(d,0x650))return false;
            const int type=read<int>(d+0x1c),slot=read<int>(d+0x6c),kind=read<int>(d+0x68);
            if((type!=1&&type!=6)||slot<1||slot>2||(slot==1&&kind!=1))continue;
            const int reserve=keyed(ps,0x344,read<int>(v+0x44)),clip=keyed(ps,0x3bc,read<int>(v+0x4c));
            if(reserve<0||clip<0||reserve>100000||clip>100000)return false;
            if(!reserve&&!clip)continue;
            auto& w=n.weapons[slot-1];if(w.index)continue;
            w={index,v,d,read<U32>(d+0x5e8),slot,kind,reserve+clip,{float(read<int>(d+0x5c8)),float(read<int>(d+0x5d4)),
                float(read<int>(d+0x5cc))}};
        }
        if(read<U32>(0x2ff5354)!=n.cg||read<U32>(0x1a796f8+0x13c)!=n.server||read<U32>(0x1c03140)!=n.server||
           read<int>(n.cg+0x8a348)!=n.time||read<int>(ps+0x158)!=n.state||read<unsigned char>(ps+0x134)!=n.offhand||
           memcmp(&n.eye,reinterpret_cast<void*>(n.cg+0x8c120),sizeof(n.eye))||
           memcmp(&axis,reinterpret_cast<void*>(n.cg+0x8c134),sizeof(axis)))return false;
        for(unsigned i=0;i<15;++i)if(read<U32>(ps+0x1dc+i*24)!=held[i])return false;
        for(const auto& w:n.weapons)if(w.index&&(read<U32>(0xbe19a8+4*w.index)!=w.variant||read<U32>(w.variant+8)!=w.definition||
            read<U32>(w.definition+0x5e8)!=w.projectile_model))return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    *output=n;return true;
}
struct State {m::Controller controller;m::Output visual{};Native native{};Weapon weapon{};
    std::uint64_t started{},released{},generation{};int start_time{};unsigned native_key{};
    bool active{},pending{},consumed{},cancelled{},trigger{},gameplay{},button_mode{};CommandContext context{};
    xr::Vec3f origin{},velocity{},forward{};std::uint64_t pose_ms{};};
State state;std::mutex gate;bool configured{},button_grenades{};
ManualGrenadePose grenade_pose;
std::uint64_t authored_transaction{},authored_generation{},authored_ms{};
bool input_thread(const CommandContext&) noexcept;
bool grenade_pose_definition_current(const ManualGrenadePose& value) noexcept {
    __try {return value.weapon>0&&value.weapon<256&&read<U32>(0xbe19a8+4*value.weapon)==value.variant&&
        read<U32>(value.variant+8)==value.definition&&read<U32>(value.definition+0x5e8)==value.projectile_model&&
        value.primary_weapon>0&&value.primary_weapon<256&&read<U32>(0xbe19a8+4*value.primary_weapon)==value.primary_variant&&
        read<U32>(value.primary_variant+8)==value.primary_definition;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool primary_visual_current(const ManualGrenadePose& value,std::uintptr_t ps,U32 expected_flags,U32 expected_primary_word,bool require_offhand_bit) noexcept {
    if(!ps||!value.transaction||!value.generation||!value.cg||!value.server||!value.weapon||
       !value.primary_weapon||value.primary_weapon==value.weapon||!value.viewmodel||!value.dobj||
       value.previous_viewmodel_weapon!=value.primary_weapon||
       !value.context.valid||value.context.local_client||value.context.native_thread!=GetCurrentThreadId()||
       !input_thread(value.context))return false;
    __try {
        if(ps!=value.cg+0x8a364||read<U32>(0x2ff5354)!=value.cg||
           read<U32>(0x1a796f8+0x13c)!=value.server||read<U32>(0x1c03140)!=value.server||
           read<int>(value.cg+0x8a348)<value.native_start_time||read<U32>(ps+0x10)!=expected_flags||
           read<int>(ps+4)!=0||read<int>(ps+0x1c4)<=0||read<unsigned char>(ps+0x130)!=0)return false;
        const int weapon_state=read<int>(ps+0x158);const auto offhand=read<unsigned char>(ps+0x134);
        const U32 primary_word=read<U32>(ps+0x144);const auto primary=primary_word&0xffffU;
        const U32 loaded_viewmodel_weapon=read<U32>(value.cg+0xa9c18);const U32 flags=read<U32>(ps+0x10);
        if(primary_word!=expected_primary_word||loaded_viewmodel_weapon!=value.primary_weapon)return false;
        if(require_offhand_bit&&!grenade_render_detail::owns_primary_visual(true,flags,weapon_state,offhand,value.weapon,primary,value.primary_weapon))return false;
        if(!require_offhand_bit&&((flags&2U)||primary!=value.primary_weapon))return false;
        const U32 owner=read<U32>(0xc1c6d8);const U32 object=owner?read<U32>(owner):0;
        if(owner!=value.viewmodel||object!=value.dobj||offhand!=value.weapon||
           weapon_state<21||weapon_state>26||!grenade_pose_definition_current(value))return false;
        return read<U32>(0x2ff5354)==value.cg&&read<U32>(ps+0x10)==expected_flags&&
            read<int>(ps+0x158)==weapon_state&&read<unsigned char>(ps+0x134)==offhand&&
            read<U32>(ps+0x144)==primary_word&&read<U32>(value.cg+0xa9c18)==loaded_viewmodel_weapon&&
            read<U32>(0xc1c6d8)==owner&&read<U32>(owner)==object;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void capture_primary_visual(const Native& native,ManualGrenadePose* pose) noexcept {
    if(!pose)return;
    __try {
        const U32 ps=native.cg+0x8a364;pose->primary_weapon=read<std::uint16_t>(ps+0x144);
        pose->previous_viewmodel_weapon=read<U32>(native.cg+0xa9c18);
        if(pose->primary_weapon&&pose->primary_weapon<256){
            pose->primary_variant=read<U32>(0xbe19a8+4*pose->primary_weapon);
            if(pointer(static_cast<U32>(pose->primary_variant),12))pose->primary_definition=read<U32>(pose->primary_variant+8);
        }
        pose->viewmodel=read<U32>(0xc1c6d8);
        if(pointer(static_cast<U32>(pose->viewmodel),4))pose->dobj=read<U32>(pose->viewmodel);
        if(pose->previous_viewmodel_weapon!=pose->primary_weapon){
            pose->primary_weapon=0;pose->primary_variant=pose->primary_definition=pose->viewmodel=pose->dobj=0;
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){pose->primary_weapon=pose->previous_viewmodel_weapon=0;pose->primary_variant=pose->primary_definition=pose->viewmodel=pose->dobj=0;}
}
bool primary_visual_words(std::uintptr_t ps,U32* flags,U32* primary_word,int* weapon_state) noexcept {
    if(!flags||!primary_word||!weapon_state)return false;
    __try {*flags=read<U32>(ps+0x10);*primary_word=read<U32>(ps+0x144);*weapon_state=read<int>(ps+0x158);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Owned by the existing native command thread. Its callback reads State under
// gate, so never hold gate across projectile capture/poll or native commands.
grenade_projectile::Baseline projectile_baseline{};
using Execute=void(__cdecl*)(int,int,const char*);
bool input_thread(const CommandContext& context) noexcept {
    __try {return context.native_thread==GetCurrentThreadId()&&read<DWORD>(0x251ca98)==GetCurrentThreadId();}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void command(unsigned slot,bool pressed,const CommandContext& context){
    if(slot<1||slot>2)return;char text[96]{};
    sprintf_s(text,"%c%s %u %d",pressed?'+':'-',slot==1?"frag":"smoke",28689+slot,context.native_frame_time);
    reinterpret_cast<Execute>(0x619d00)(0,0,text);
    trace("t5_manual_grenade_input","command=%s native_command=%d thread=%lu",text,context.command_number,context.native_thread);
}
bool local_spawn(U32 parent,const Weapon& w,U32 server,int start_time) noexcept {
    __try {return read<U32>(0x2ff5354)==state.native.cg&&parent==0x1a796f8&&read<int>(parent)==0&&read<U32>(parent+0x13c)==server&&read<U32>(0x1c03140)==server&&read<unsigned char>(server+0x130)==0&&
        read<int>(server+4)==0&&read<int>(server+0x1c4)>0&&read<unsigned char>(server+0x134)==w.index&&
        read<U32>(0xbe19a8+4*w.index)==w.variant&&read<U32>(w.variant+8)==w.definition&&
        read<int>(state.native.cg+0x8a348)>=start_time&&
        static_cast<std::int64_t>(read<int>(state.native.cg+0x8a348))-start_time<=15000;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool replace_throw(const GrenadeThrowArguments& args,GrenadeThrowReplacement* replacement) noexcept {
    try {
        std::lock_guard lock(gate);auto& s=state;const auto now=GetTickCount64();
        if(!replacement||!s.active||s.consumed||args.weapon!=s.weapon.index||
           !local_spawn(static_cast<U32>(args.parent),s.weapon,s.native.server,s.start_time)||
           (!s.cancelled&&(now<s.started||now-s.started>15000||now<s.pose_ms||now-s.pose_ms>(s.pending?2000u:100u)||
           (s.pending&&(now<s.released||now-s.released>2000)))))return false;
        // Cooking expires in native code while held: use a physical drop at the
        // latest hand, not a stale swing. A released token can be consumed once.
        const xr::Vec3f physical=s.pending&&!s.cancelled?s.velocity:xr::Vec3f{};
        g::ManualGrenadeLaunchResult result{};
        if(s.button_mode&&s.pending&&!s.cancelled){
            xr::Vec3f velocity{};if(!m::button_launch_velocity(s.forward,s.weapon.tuning,&velocity))return false;
            result.velocity_game_units_per_second={velocity.x,velocity.y,velocity.z};result.normalized_strength=1;
        }else if(!g::build_manual_grenade_launch_velocity({physical.x,physical.y,physical.z},
            {s.forward.x,s.forward.y,s.forward.z},s.weapon.tuning,&result))return false;
        // Retain the engine's launch origin until a native hand-reach sweep is
        // available. Hand velocity controls the throw without spawning through
        // a wall because a controller was pushed past it. Cancellation drops
        // at this current native origin and never reuses a stale hand pose.
        replacement->origin=args.origin;
        replacement->velocity={result.velocity_game_units_per_second.x,result.velocity_game_units_per_second.y,result.velocity_game_units_per_second.z};
        s.consumed=true;
        trace("t5_manual_grenade_throw","transaction=%llu weapon=%u slot=%d parent=%p start=%.4f,%.4f,%.4f velocity=%.4f,%.4f,%.4f physical=%.4f,%.4f,%.4f drop=%u strength=%.4f fuse=%d native_ammo=1 exactly_once=1 native_origin_retained=1 cancelled=%u",
            s.visual.transaction,args.weapon,s.weapon.slot,reinterpret_cast<void*>(args.parent),replacement->origin[0],replacement->origin[1],replacement->origin[2],
            replacement->velocity[0],replacement->velocity[1],replacement->velocity[2],physical.x,physical.y,physical.z,result.deliberate_drop,result.normalized_strength,args.fuse_time,s.cancelled);
        return true;
    }catch(...){return false;}
}
}
bool configure_manual_grenades(HMODULE executable,bool weapon_probe_installed) noexcept {
    if(!weapon_probe_installed){trace("t5_manual_grenade_rejected","reason=weapon_submission_hook");return false;}
    if(configured)return true;
    if(!manual_grenade_native_guards::execute_loaded(reinterpret_cast<const unsigned char*>(executable))){
        trace("t5_manual_grenade_rejected","reason=native_execute_entry_guard");return false;
    }
    if(!grenade_projectile::configure(executable)||!configure_manual_grenade_renderer(executable))return false;
    wchar_t value[8]{};GetEnvironmentVariableW(L"BOVR_BUTTON_GRENADES",value,8);button_grenades=!wcscmp(value,L"1");
    configured=true;trace("t5_manual_grenade_configured","button_mode=%u grab=left_trigger frag=%s tactical=%s hold=native_cook release=%s synchronized_projectile_update=1 visual_scopes=2 standalone_projectile=1 colored_fallback=0",
        button_grenades,button_grenades?"button":"left_hip",button_grenades?"left_stick_click_modifier":"right_hip",button_grenades?"head_forward_native_speed":"hand_velocity");return true;
}
void update_manual_grenade_input(const CommandContext& context,const input::CommandRigSample& rig,bool live) {
    if(!configured)return;
    if(!input_thread(context))return;
    const auto now=GetTickCount64();Native n{};
    const bool native=live&&context.native_thread==GetCurrentThreadId()&&command_input_generation_current(rig)&&native_copy(context,&n);
    unsigned release{},press{};grenade_projectile::Key projectile_key{};
    {
        std::lock_guard lock(gate);auto& s=state;
        const bool changed=native&&(n.cg!=s.native.cg||n.server!=s.native.server||n.time<s.start_time);
        if(s.active&&(s.consumed||changed||(native&&n.time-s.start_time>15000))){
            release=s.native_key;s.native_key=0;s.active=s.pending=s.consumed=s.cancelled=false;
            if(s.visual.stage==g::ManualGrenadeStage::released_pending)s.controller.retire();else s.controller.reset();
        }else if(s.active&&(!native||rig.xr_generation!=s.generation||
           (!s.cancelled&&(now<s.started||now-s.started>15000||(s.pending&&now-s.released>2000))))){
            release=s.native_key;s.native_key=0;s.pending=s.cancelled=true;s.released=now;s.velocity={};s.controller.reset();
        }
        const bool allowed=native&&(n.ordinary||s.active);
        const auto& support=rig.frame.actions.hands[0];
        const bool button_blocked=button_grenades&&(!button_primary_single(n)||
            !support.squeeze.active||!std::isfinite(support.squeeze.current));
        m::Context c{now,n.cg,rig.xr_generation,allowed,
            s.active||manual_reload_uses_offhand()||rig.frame.actions.hands[1].primary.current||
            button_blocked||(support.squeeze.active&&support.squeeze.current>=.65f),n.weapons[0].ammo>0,n.weapons[1].ammo>0,button_grenades};
        auto output=s.controller.update(rig,c);s.trigger=output.consume_trigger;s.gameplay=allowed;s.context=context;
        if(output.event==g::ManualGrenadeEvent::reset&&s.native_key){release=s.native_key;s.native_key=0;s.pending=s.cancelled=true;s.released=now;s.velocity={};}
        if(output.event==g::ManualGrenadeEvent::grab_frag||output.event==g::ManualGrenadeEvent::grab_tactical){
            const unsigned slot=output.slot==g::ManualGrenadeSlot::frag?1:2;
            s.weapon=n.weapons[slot-1];s.active=true;s.pending=s.consumed=s.cancelled=false;s.native=n;s.started=now;s.start_time=n.time;
            s.button_mode=button_grenades;
            s.generation=rig.xr_generation;s.native_key=slot;press=slot;
            projectile_key={n.cg,rig.xr_generation,output.transaction,s.weapon.index};
            grenade_pose={};grenade_pose.transaction=output.transaction;grenade_pose.generation=rig.xr_generation;
            grenade_pose.cg=n.cg;grenade_pose.server=n.server;grenade_pose.native_start_time=n.time;
            grenade_pose.variant=s.weapon.variant;grenade_pose.definition=s.weapon.definition;
            grenade_pose.projectile_model=s.weapon.projectile_model;grenade_pose.weapon=s.weapon.index;
            capture_primary_visual(n,&grenade_pose);
            authored_transaction=authored_generation=authored_ms=0;
            trace("t5_manual_grenade_grab","transaction=%llu weapon=%u slot=%u ammo=%d",output.transaction,s.weapon.index,slot,s.weapon.ammo);
        }
        if(s.active&&output.valid&&!s.pending){
            const auto relative=transform(n.body,output.local_origin);s.origin={n.eye.x+relative.x,n.eye.y+relative.y,n.eye.z+relative.z};
            s.velocity=transform(n.body,output.local_velocity);s.forward=transform(n.body,output.local_forward);s.pose_ms=now;
        }
        if(s.active&&output.event==g::ManualGrenadeEvent::release){s.pending=true;s.released=now;release=s.native_key;s.native_key=0;
            trace("t5_manual_grenade_release","transaction=%llu weapon=%u velocity=%.4f,%.4f,%.4f",output.transaction,s.weapon.index,s.velocity.x,s.velocity.y,s.velocity.z);}
        s.visual=output;
        if(grenade_pose.transaction){
            if(!native||grenade_pose.cg!=n.cg||grenade_pose.server!=n.server||n.time<grenade_pose.native_start_time||grenade_pose.generation!=rig.xr_generation||
               (n.ordinary&&!s.active))grenade_pose={};
            else if(!visual_rig_current(rig,now))grenade_pose.updated_ms=0;
            else {
                grenade_pose.hand=grenade_visual_pose::hand(rig.frame.actions.hands[0],rig.tracking_anchor,{n.eye,n.body});
                grenade_pose.context=context;grenade_pose.updated_ms=now;
                grenade_pose.holding=s.active&&!s.pending&&!s.consumed&&!s.cancelled;
            }
        }
    }
    // The one shared command-thread adapter already owns reentrancy. Never hold
    // the publication lock across a native input call or projectile spawn.
    if(projectile_baseline.ready&&!projectile_baseline.retired)
        (void)grenade_projectile::poll(&projectile_baseline,projectile_baseline.key,replace_throw);
    if(press&&!grenade_projectile::capture(&projectile_baseline,projectile_key)){
        std::lock_guard lock(gate);state.native_key=0;state.active=state.pending=state.cancelled=false;
        state.controller.reset();state.visual={};press=0;
        grenade_pose={};
        trace("t5_manual_grenade_capture_rejected","new_native_input=0");
    }
    if(release)command(release,false,context);if(press)command(press,true,context);
}
bool manual_grenade_uses_offhand() noexcept {if(!configured)return false;std::lock_guard lock(gate);return state.active;}
bool manual_grenade_consumes_trigger() noexcept {if(!configured)return false;std::lock_guard lock(gate);return state.trigger;}
bool manual_grenade_gameplay_current(const CommandContext& context) noexcept {if(!configured)return false;std::lock_guard lock(gate);
    return state.gameplay&&state.active&&state.context.command_number==context.command_number&&state.context.native_thread==context.native_thread;}
bool read_manual_grenade_visual(m::Output* output) noexcept {
    if(!output||!configured)return false;std::lock_guard lock(gate);const auto now=GetTickCount64();
    if(!state.active||state.consumed||!state.visual.valid||state.visual.stage!=g::ManualGrenadeStage::holding||
       now<state.visual.updated_ms||now-state.visual.updated_ms>100)return false;*output=state.visual;return true;
}
bool read_manual_grenade_pose(ManualGrenadePose* output) noexcept {
    if(!output||!configured)return false;*output={};ManualGrenadePose value{};
    {std::lock_guard lock(gate);value=grenade_pose;}
    const auto now=GetTickCount64();Native native{};
    if(!value.transaction||!value.generation||!value.updated_ms||now<value.updated_ms||now-value.updated_ms>100||
       !input_thread(value.context)||!native_copy(value.context,&native)||native.cg!=value.cg||native.server!=value.server||
       native.time<value.native_start_time||native.ordinary||
       native.offhand!=int(value.weapon)||!animation_detail::finite(value.hand.origin)||!animation_detail::rigid(value.hand.axis))return false;
    if(!grenade_pose_definition_current(value))return false;
    *output=value;return true;
}
bool read_manual_grenade_render_pose(ManualGrenadePose* output) noexcept {
    if(!output||!configured)return false;*output={};ManualGrenadePose value{};
    {
        std::lock_guard lock(gate);
        if(!state.active||state.pending||state.consumed||state.cancelled||!grenade_pose.holding||
           !state.visual.transaction||state.visual.transaction!=grenade_pose.transaction||
           state.generation!=grenade_pose.generation||state.weapon.index!=grenade_pose.weapon)return false;
        value=grenade_pose;
    }
    const auto now=GetTickCount64();Native native{};
    if(!value.transaction||!value.generation||!value.updated_ms||!value.projectile_model||
       now<value.updated_ms||now-value.updated_ms>100||!input_thread(value.context)||
       !native_copy(value.context,&native)||native.cg!=value.cg||native.server!=value.server||
       native.time<value.native_start_time||!animation_detail::finite(value.hand.origin)||
       !animation_detail::rigid(value.hand.axis)||!grenade_pose_definition_current(value))return false;
    *output=value;return true;
}
bool manual_grenade_render_pending() noexcept {
    if(!configured)return false;std::lock_guard lock(gate);
    return state.active&&!state.pending&&!state.consumed&&!state.cancelled&&grenade_pose.holding&&
        grenade_pose.transaction&&grenade_pose.transaction==state.visual.transaction&&
        grenade_pose.generation==state.generation&&grenade_pose.weapon==state.weapon.index;
}
bool manual_grenade_pose_current(const ManualGrenadePose& value) noexcept {
    ManualGrenadePose current{};
    return read_manual_grenade_pose(&current)&&current.transaction==value.transaction&&current.generation==value.generation&&
        current.updated_ms==value.updated_ms&&current.cg==value.cg&&current.server==value.server&&current.native_start_time==value.native_start_time&&
        current.variant==value.variant&&current.definition==value.definition&&current.projectile_model==value.projectile_model&&
        current.primary_variant==value.primary_variant&&current.primary_definition==value.primary_definition&&
        current.viewmodel==value.viewmodel&&current.dobj==value.dobj&&current.weapon==value.weapon&&
        current.primary_weapon==value.primary_weapon&&current.previous_viewmodel_weapon==value.previous_viewmodel_weapon&&
        current.holding==value.holding&&current.context.command_number==value.context.command_number;
}
bool read_manual_grenade_primary_visual(int local_client,ManualGrenadePrimaryVisual* output) noexcept {
    if(output)*output={};if(!output||!configured||local_client!=0)return false;
    ManualGrenadePose value{};{std::lock_guard lock(gate);value=grenade_pose;}
    const auto ps=value.cg+0x8a364;U32 flags{},primary_word{};int weapon_state{};
    if(!primary_visual_words(ps,&flags,&primary_word,&weapon_state))return false;
    if(!primary_visual_current(value,ps,flags,primary_word,true))return false;
    *output={value.transaction,value.generation,ps,value.cg,value.server,value.viewmodel,value.dobj,
        value.weapon,value.primary_weapon,value.previous_viewmodel_weapon,flags,primary_word,weapon_state};
    return true;
}
bool manual_grenade_primary_visual_current(const ManualGrenadePrimaryVisual& value,std::uint32_t expected_flags) noexcept {
    if(!configured||value.ps!=value.cg+0x8a364||value.weap_flags!=(expected_flags|2U))return false;
    ManualGrenadePose pose{};{std::lock_guard lock(gate);pose=grenade_pose;}
    if(pose.transaction!=value.transaction||pose.generation!=value.generation||pose.cg!=value.cg||pose.server!=value.server||
       pose.viewmodel!=value.viewmodel||pose.dobj!=value.dobj||pose.weapon!=value.grenade_weapon||
       pose.primary_weapon!=value.primary_weapon||pose.previous_viewmodel_weapon!=value.previous_viewmodel_weapon)return false;
    const U32 injected_primary=grenade_render_detail::replace_low_word(value.primary_word,value.primary_weapon);
    return primary_visual_current(pose,value.ps,expected_flags,injected_primary,false);
}
void publish_manual_grenade_pose(std::uint64_t transaction,std::uint64_t generation) noexcept {
    std::lock_guard lock(gate);
    if(grenade_pose.transaction==transaction&&grenade_pose.generation==generation&&grenade_pose.holding){
        authored_transaction=transaction;authored_generation=generation;authored_ms=GetTickCount64();
    }
}
bool manual_grenade_authored_visible(std::uint64_t transaction,std::uint64_t generation) noexcept {
    std::lock_guard lock(gate);const auto now=GetTickCount64();
    return transaction&&transaction==authored_transaction&&generation==authored_generation&&
        authored_ms&&now>=authored_ms&&now-authored_ms<=100;
}
}
