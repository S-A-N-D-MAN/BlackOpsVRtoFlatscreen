// SPDX-License-Identifier: GPL-3.0-only
#include "t5/weapon_barrel_probe.hpp"
#include "t5/native_camera_mode.hpp"
#include "t5/native_weapon_tracking_state.hpp"
#include "t5/weapon_animation_probe.hpp"
#include "t5/weapon_animation_locked.hpp"
#include "t5/pose_clear_probe.hpp"
#include "t5/native_clear_pair.hpp"
#include "t5/borrowed_matrix_copy.hpp"
#include "t5/borrowed_profile_copy.hpp"
#include "t5/borrowed_barrel_geometry.hpp"
#include "t5/runtime_profile_catalog.hpp"
#include "t5/weapon_borrowed_adapter.hpp"
#include "t5/native_command_aim_adapter.hpp"
#include "t5/borrowed_weapon_claim.hpp"
#include "t5/borrowed_weapon_writer.hpp"
#include "t5/manual_reload_pose.hpp"
#include "t5/manual_reload_adapter.hpp"
#include "input/tracked_gun_pose.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/profile_record_output.hpp"
#include "t5/weapon_probe.hpp"
#include "t5/command_probe.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"
#include "loader/native_span_guard.hpp"
#include "t5/hand_mesh_draw.hpp"
#include "t5/client_gun_angles.hpp"
#include "t5/shot_haptic_adapter.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

#if !defined(_M_IX86)
#error Native barrel observation requires the pinned x86 ABI.
#endif
namespace bovr::t5 {
namespace {
using Vec=wawvr::xr::Vec3f;
using Axis=wawvr::xr::Basis3f;
constexpr unsigned max_models=16,max_bones=160;
constexpr std::uint64_t sample_interval_ms=100,max_attempts=2048;
enum class BarrelStatus:unsigned {accepted,context,command,borrow,tag_missing,changed,geometry,exception,budget,count};
// Internal stack-local borrow; never returned or retained by the observer.
struct NativeBorrow {
    std::uintptr_t owner{},dobj{},models_table{},cg{},variant{},definition{},server{};
    primary_weapon_tracking::Mode client_mode{},server_mode{};
    std::uint32_t server_link{};
    std::array<std::uintptr_t,max_models> models{};
    std::array<unsigned char,max_models> model_bones{};
    std::array<unsigned short,max_bones> bone_names{};
    unsigned model_count{},bone_count{},tag{},tag_model{},tag_bone{},tag_local_bone{};
    int pool_index{},skeleton_context{},skeleton_clock{},skeleton_stamp{};
    Vec model_origin{},pose_origin{},pose_angles{};
    Axis model_axis{};
    friend bool operator==(const NativeBorrow& a,const NativeBorrow& b) noexcept {
        return a.owner==b.owner&&a.dobj==b.dobj&&a.models_table==b.models_table&&a.cg==b.cg&&
            a.variant==b.variant&&a.definition==b.definition&&a.server==b.server&&
            a.client_mode==b.client_mode&&a.server_mode==b.server_mode&&a.server_link==b.server_link&&
            a.models==b.models&&a.model_bones==b.model_bones&&
            a.bone_names==b.bone_names&&a.model_count==b.model_count&&a.bone_count==b.bone_count&&
            a.tag==b.tag&&a.tag_model==b.tag_model&&a.tag_bone==b.tag_bone&&a.tag_local_bone==b.tag_local_bone&&
            a.pool_index==b.pool_index&&a.skeleton_context==b.skeleton_context&&a.skeleton_clock==b.skeleton_clock&&
            !memcmp(&a.model_origin,&b.model_origin,sizeof(Vec))&&!memcmp(&a.model_axis,&b.model_axis,sizeof(Axis))&&
            !memcmp(&a.pose_origin,&b.pose_origin,sizeof(Vec))&&!memcmp(&a.pose_angles,&b.pose_angles,sizeof(Vec));
        // Native tag calculation may create/update skeleton_stamp legitimately.
    }
};
// All fields are copied values. No source pointer/address or model generation.
struct BarrelSample {
    std::uint64_t observation{},observed_ms{},gameplay_epoch{},command_sequence{},topology_fingerprint{};
    DWORD native_thread{};
    int native_time{},command_number{},command_clock{},weapon_state{},skeleton_clock{};
    std::uint32_t weapon_index{};
    unsigned model_count{},bone_count{},tag{},tag_model{},tag_bone{},tag_local_bone{};
    std::array<unsigned char,max_models> model_bones{};
    std::array<unsigned short,max_bones> bone_names{};
    Vec body_origin{},native_model_origin{},native_pose_origin{},tag_world_origin{},tag_model_origin{},model_body_origin{};
    Axis body_axis{},native_model_axis{},tag_world_axis{},tag_model_axis{},model_body_axis{};
    float position_roundtrip_error{},axis_roundtrip_error{};
    bool tag_attempted{};
};
static_assert(std::is_trivially_copyable_v<BarrelSample>);
float dot(Vec a,Vec b) noexcept {return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec sub(Vec a,Vec b) noexcept {return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec cross(Vec a,Vec b) noexcept {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
bool finite(Vec v) noexcept {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool rigid(const Axis& a) noexcept {
    return finite(a.forward)&&finite(a.left)&&finite(a.up)&&
        std::fabs(dot(a.forward,a.forward)-1)<0.003F&&std::fabs(dot(a.left,a.left)-1)<0.003F&&
        std::fabs(dot(a.up,a.up)-1)<0.003F&&std::fabs(dot(a.forward,a.left))<0.003F&&
        std::fabs(dot(a.forward,a.up))<0.003F&&std::fabs(dot(a.left,a.up))<0.003F&&
        dot(cross(a.forward,a.left),a.up)>0.997F;
}

Vec local(const Axis& a,Vec v) noexcept {return {dot(a.forward,v),dot(a.left,v),dot(a.up,v)};}
Vec world(const Axis& a,Vec v) noexcept {return {a.forward.x*v.x+a.left.x*v.y+a.up.x*v.z,
    a.forward.y*v.x+a.left.y*v.y+a.up.y*v.z,a.forward.z*v.x+a.left.z*v.y+a.up.z*v.z};}
float distance(Vec a,Vec b) noexcept {const auto d=sub(a,b);return std::sqrt(dot(d,d));}
bool derive_geometry(BarrelSample& s) noexcept {
    if(!finite(s.body_origin)||!finite(s.native_model_origin)||!finite(s.native_pose_origin)||!finite(s.tag_world_origin)||
       !rigid(s.body_axis)||!rigid(s.native_model_axis)||!rigid(s.tag_world_axis)||
       distance(s.native_model_origin,s.native_pose_origin)>0.01F) return false;
    s.tag_model_origin=local(s.native_model_axis,sub(s.tag_world_origin,s.native_model_origin));
    s.tag_model_axis={local(s.native_model_axis,s.tag_world_axis.forward),local(s.native_model_axis,s.tag_world_axis.left),local(s.native_model_axis,s.tag_world_axis.up)};
    s.model_body_origin=local(s.body_axis,sub(s.native_model_origin,s.body_origin));
    s.model_body_axis={local(s.body_axis,s.native_model_axis.forward),local(s.body_axis,s.native_model_axis.left),local(s.body_axis,s.native_model_axis.up)};
    const auto delta=world(s.native_model_axis,s.tag_model_origin);
    const Vec roundtrip{delta.x+s.native_model_origin.x,delta.y+s.native_model_origin.y,delta.z+s.native_model_origin.z};
    s.position_roundtrip_error=distance(roundtrip,s.tag_world_origin);
    s.axis_roundtrip_error=distance(world(s.native_model_axis,s.tag_model_axis.forward),s.tag_world_axis.forward)+
        distance(world(s.native_model_axis,s.tag_model_axis.left),s.tag_world_axis.left)+distance(world(s.native_model_axis,s.tag_model_axis.up),s.tag_world_axis.up);
    return finite(s.tag_model_origin)&&finite(s.model_body_origin)&&s.position_roundtrip_error<0.02F&&s.axis_roundtrip_error<0.01F;
}
bool supported(const NativeWeaponContext& c,DWORD thread) noexcept {
    return c.valid&&c.body_valid&&primary_weapon_context(c)&&c.primary_viewmodel&&c.weapon_definition_valid&&
        c.local_client==0&&c.draw_gun&&c.native_thread==thread&&(c.weapon_type==0||c.weapon_type==2||c.weapon_type==4)&&
        (!c.dual_wield||native_dual_weapon_tracking_type(c.weapon_type,c.weapon_class))&&native_valid_stack_fire_count(static_cast<std::uint32_t>(c.stack_count))&&
        tracked_primary_weapon_state(c)&&native_weapon_tracking_state(c.other_weapon_state)&&
        c.weapon_index>0&&c.weapon_index<256&&c.cg_identity&&c.viewmodel_identity&&c.dobj_identity&&
        c.variant_identity&&c.definition_identity&&c.refdef_identity==c.cg_identity+0x8c100&&c.native_time==c.refdef_time&&
        std::isfinite(c.placement.scale)&&std::fabs(c.placement.scale-1)<0.0001F&&finite(c.stock_view.origin)&&rigid(c.stock_view.axis);
}
bool fresh_command(const NativeWeaponContext& c,const PreCommandSnapshot& s,std::uint64_t now) noexcept {
    const auto& p=s.context;
    return p.valid&&s.native_gameplay_fields_valid&&primary_weapon_active(s)&&s.gameplay_epoch&&s.sequence&&
        p.local_client==0&&p.native_thread==c.native_thread&&s.native_cg_identity==c.cg_identity&&
        !p.native_override&&!p.key_catcher&&p.connection_state>=9&&p.native_frame_time==c.command_clock&&
        c.command_number>=p.command_number&&static_cast<std::int64_t>(c.command_number)-p.command_number<=1&&
        now>=s.observed_ms&&now-s.observed_ms<=100;
}
std::uint64_t topology_hash(const NativeBorrow& b) noexcept {
    // Diagnostic copied topology, never an allocation generation/profile key.
    std::uint64_t h=14695981039346656037ULL;
    const auto add=[&](unsigned v){h^=v;h*=1099511628211ULL;};
    add(b.model_count);add(b.bone_count);
    for(unsigned i=0;i<b.model_count;++i)add(b.model_bones[i]);
    for(unsigned i=0;i<b.bone_count;++i)add(b.bone_names[i]);
    return h;
}
struct BarrelHost {
    void* context{};
    std::uint64_t (*now)(void*){};
    bool (*precommand)(void*,PreCommandSnapshot*){};
    bool (*borrow)(void*,const NativeWeaponContext&,NativeBorrow*){};
    bool (*tag)(void*,const NativeWeaponContext&,unsigned,Axis*,Vec*){};
    void (*report)(void*,BarrelStatus,const BarrelSample&){};
};
struct BarrelObserver {
    BarrelHost host{};
    std::atomic_flag running=ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> serial{};
    std::uint64_t last_attempt{},attempts{};
    void observe(const NativeWeaponContext& c,DWORD thread,std::uint64_t now) {
        if(running.test_and_set(std::memory_order_acquire))return;
        __try {work(c,thread,now);}
        __finally {running.clear(std::memory_order_release);}
    }
    void work(const NativeWeaponContext& c,DWORD thread,std::uint64_t now) {
        if(last_attempt&&now>=last_attempt&&now-last_attempt<sample_interval_ms)return;
        last_attempt=now;
        BarrelSample sample{};sample.observation=serial.fetch_add(1)+1;sample.observed_ms=now;sample.native_thread=thread;
        auto status=BarrelStatus::context;
        {
            if(attempts>=max_attempts)status=BarrelStatus::budget;
            else if(supported(c,thread)) {
                PreCommandSnapshot pre{},post{};
                if(!host.precommand(host.context,&pre)||!fresh_command(c,pre,now))status=BarrelStatus::command;
                else {
                    ++attempts;NativeBorrow before{},after{};
                    if(!host.borrow(host.context,c,&before))status=BarrelStatus::borrow;
                    else {
                        sample.gameplay_epoch=pre.gameplay_epoch;sample.command_sequence=pre.sequence;
                        sample.native_time=c.native_time;sample.command_number=c.command_number;sample.command_clock=c.command_clock;
                        sample.weapon_index=c.weapon_index;sample.weapon_state=c.weapon_state;sample.skeleton_clock=before.skeleton_clock;
                        sample.model_count=before.model_count;sample.bone_count=before.bone_count;sample.tag=before.tag;
                        sample.tag_model=before.tag_model;sample.tag_bone=before.tag_bone;sample.tag_local_bone=before.tag_local_bone;
                        sample.model_bones=before.model_bones;sample.bone_names=before.bone_names;sample.topology_fingerprint=topology_hash(before);
                        sample.body_origin=c.stock_view.origin;sample.body_axis=c.stock_view.axis;sample.native_model_origin=before.model_origin;
                        sample.native_model_axis=before.model_axis;sample.native_pose_origin=before.pose_origin;
                        sample.tag_attempted=true;
                        if(!host.tag(host.context,c,before.tag,&sample.tag_world_axis,&sample.tag_world_origin))status=BarrelStatus::tag_missing;
                        else {
                            const auto finished=host.now?host.now(host.context):0;
                            if(!host.borrow(host.context,c,&after)||!(before==after)||after.skeleton_stamp!=after.skeleton_clock||
                                !host.precommand(host.context,&post)||finished<now||finished-now>100||
                                !fresh_command(c,post,finished)||post.sequence!=pre.sequence||post.gameplay_epoch!=pre.gameplay_epoch)
                                status=BarrelStatus::changed;
                            else status=derive_geometry(sample)?BarrelStatus::accepted:BarrelStatus::geometry;
                        }
                    }
                }
            }
            // Only our own evidence failure is contained. The native tag call
            // above must propagate C++/SEH unchanged, including lock ownership.
            try {if(host.report)host.report(host.context,status,sample);}catch(...) {}
        }
        // No sample/borrow/native pointer survives this invocation.
    }
};
std::atomic<bool> barrel_enabled{};
BarrelObserver observer{};
template<class T>T load(std::uintptr_t a) noexcept {T v{};memcpy(&v,reinterpret_cast<const void*>(a),sizeof(v));return v;}
bool native_borrow(void*,const NativeWeaponContext& c,NativeBorrow* out) noexcept {
    if(!out)return false;*out={};
    __try {
        auto& b=*out;b.cg=load<std::uintptr_t>(0x2ff5354);b.owner=load<std::uintptr_t>(0xc1c6d8);
        if(b.cg!=c.cg_identity||b.owner!=c.viewmodel_identity||!b.owner)return false;
        b.dobj=load<std::uintptr_t>(b.owner);b.pool_index=load<short>(0x24e6150+1536*2);
        if(b.dobj!=c.dobj_identity||b.pool_index<=0||b.pool_index>=0xc00||
            b.dobj!=0x2489148+static_cast<unsigned>(b.pool_index)*0x7c||!load<unsigned char>(0x2488548+b.pool_index))return false;
        b.variant=load<std::uintptr_t>(0xbe19a8+c.weapon_index*4);
        if(b.variant!=c.variant_identity||!b.variant)return false;b.definition=load<std::uintptr_t>(b.variant+8);
        if(b.definition!=c.definition_identity)return false;
        const auto ps=b.cg+0x8a364;
        b.client_mode=primary_weapon_tracking::client_mode(
            {load<int>(ps+4),load<int>(ps+0x1c4),load<unsigned>(ps+0xc),load<unsigned>(ps+0x10),load<unsigned>(ps+0xe0),load<unsigned>(ps+0xe4)},
            {load<int>(b.cg+0x8a360),load<int>(0x2910160),load<int>(0x2910164),
             load<int>(0x2911e2c)!=0,load<int>(b.cg+0xc)!=0,load<int>(b.cg+0xa9d44)!=0});
        if(b.client_mode==primary_weapon_tracking::Mode::rejected||load<unsigned char>(ps+0x130)!=0||
            load<unsigned char>(ps+0x144)!=c.weapon_index||load<int>(ps+0x158)!=c.weapon_state||
            load<int>(ps+0x15c)!=c.other_weapon_state||load<int>(ps+0x1b0)!=c.stack_count||
            load<int>(b.cg+0x8a348)!=c.native_time||load<int>(b.cg+0x8c158)!=c.refdef_time||
            load<int>(0x2481764)!=c.command_clock||load<int>(0x2993850)!=c.command_number)return false;
        constexpr std::uintptr_t entity=0x1a796f8;
        b.server=load<std::uintptr_t>(entity+0x13c);
        const auto server=b.server;
        if(!server||server!=load<std::uintptr_t>(0x1c03140)||load<int>(entity)!=0||load<unsigned char>(server+0x130)!=0)return false;
        b.server_link=load<unsigned>(entity+0x298);
        b.server_mode=primary_weapon_tracking::server_mode(
            {{load<int>(server+4),load<int>(server+0x1c4),load<unsigned>(server+0xc),load<unsigned>(server+0x10),
              load<unsigned>(server+0xe0),load<unsigned>(server+0xe4)},true,load<int>(entity+0x184),
             load<int>(server+0x1a2c),load<int>(server+0x1a18),load<unsigned>(server+0x1c0c),b.server_link});
        const auto mode=primary_weapon_tracking::paired_mode(b.client_mode,b.server_mode);
        if(!primary_weapon_tracking::same_link_mode(primary_weapon_mode(c),c.server_link_identity,mode,b.server_link))return false;
        if(memcmp(reinterpret_cast<void*>(c.refdef_identity+0x20),&c.stock_view.origin,sizeof(Vec))||
            memcmp(reinterpret_cast<void*>(c.refdef_identity+0x34),&c.stock_view.axis,sizeof(Axis)))return false;
        b.skeleton_context=load<int>(0x3b1f794);if(b.skeleton_context<0||b.skeleton_context>1)return false;
        b.skeleton_clock=load<int>(0x2911e40+static_cast<unsigned>(b.skeleton_context)*0x2008e0);
        b.skeleton_stamp=load<int>(b.dobj+0x50);if(b.skeleton_clock<=0)return false;
        memcpy(&b.model_axis,reinterpret_cast<void*>(b.cg+0xa9d14),sizeof(Axis));
        memcpy(&b.model_origin,reinterpret_cast<void*>(b.cg+0xa9d38),sizeof(Vec));
        memcpy(&b.pose_origin,reinterpret_cast<void*>(b.cg+0xc5e28),sizeof(Vec));
        memcpy(&b.pose_angles,reinterpret_cast<void*>(b.cg+0xc5e34),sizeof(Vec));
        b.model_count=load<unsigned char>(b.dobj+9);b.bone_count=load<unsigned char>(b.dobj+0xa);b.models_table=load<std::uintptr_t>(b.dobj+0x78);
        b.tag=load<unsigned short>(0x23a566e);
        if(!b.model_count||b.model_count>max_models||!b.bone_count||b.bone_count>max_bones||!b.models_table||!b.tag)return false;
        unsigned total=0,matches=0;
        for(unsigned i=0;i<b.model_count;++i) {
            const auto model=load<std::uintptr_t>(b.models_table+i*4);if(!model)return false;b.models[i]=model;
            const unsigned count=load<unsigned char>(model+4);if(!count||count>max_bones-total)return false;
            b.model_bones[i]=static_cast<unsigned char>(count);const auto names=load<std::uintptr_t>(model+8);if(!names)return false;
            for(unsigned j=0;j<count;++j) {
                const auto name=load<unsigned short>(names+j*2);b.bone_names[total+j]=name;
                if(name==b.tag) {++matches;b.tag_model=i;b.tag_local_bone=j;b.tag_bone=total+j;}
            }
            total+=count;
        }
        return total==b.bone_count&&matches==1&&rigid(b.model_axis)&&finite(b.model_origin)&&finite(b.pose_origin);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool native_tag(void*,const NativeWeaponContext& c,unsigned tag,Axis* axis,Vec* origin) {
    using Function=int(__cdecl*)(const void*,const void*,unsigned,Axis*,Vec*);
    return reinterpret_cast<Function>(0x4bb2e0)(reinterpret_cast<void*>(c.cg_identity+0xc5e04),
        reinterpret_cast<void*>(c.dobj_identity),tag,axis,origin)!=0;
}
bool native_precommand(void*,PreCommandSnapshot* out) noexcept {return read_pre_command_snapshot(out);}
std::uint64_t native_now(void*) noexcept {return GetTickCount64();}
void native_report(void*,BarrelStatus status,const BarrelSample& s) noexcept {
    static std::array<std::uint64_t,static_cast<unsigned>(BarrelStatus::count)> counts{};
    const auto n=++counts[static_cast<unsigned>(status)];
    if(n>3&&n%60)return;
    trace("t5_weapon_barrel","observation=%llu status=%u count=%llu time=%d command=%d clock=%d epoch=%llu command_sequence=%llu weapon=%u state=%d models=%u bones=%u tag=%u model_ordinal=%u bone=%u local_bone=%u topology_diagnostic=%llu skeleton_clock=%d native_origin=%.5f,%.5f,%.5f tag_world=%.5f,%.5f,%.5f tag_model=%.5f,%.5f,%.5f model_body=%.5f,%.5f,%.5f tag_axis_world=%.6f,%.6f,%.6f;%.6f,%.6f,%.6f;%.6f,%.6f,%.6f tag_axis_model=%.6f,%.6f,%.6f;%.6f,%.6f,%.6f;%.6f,%.6f,%.6f roundtrip_pos=%.8f roundtrip_axis=%.8f profile_identity_verified=0 model_generation=unclaimed pointer_retention=0 placement_writes=0 shot_writes=0 native_tag_attempted=%u",
        s.observation,static_cast<unsigned>(status),n,s.native_time,s.command_number,s.command_clock,s.gameplay_epoch,s.command_sequence,
        s.weapon_index,s.weapon_state,s.model_count,s.bone_count,s.tag,s.tag_model,s.tag_bone,s.tag_local_bone,s.topology_fingerprint,s.skeleton_clock,
        s.native_model_origin.x,s.native_model_origin.y,s.native_model_origin.z,s.tag_world_origin.x,s.tag_world_origin.y,s.tag_world_origin.z,
        s.tag_model_origin.x,s.tag_model_origin.y,s.tag_model_origin.z,s.model_body_origin.x,s.model_body_origin.y,s.model_body_origin.z,
        s.tag_world_axis.forward.x,s.tag_world_axis.forward.y,s.tag_world_axis.forward.z,s.tag_world_axis.left.x,s.tag_world_axis.left.y,s.tag_world_axis.left.z,
        s.tag_world_axis.up.x,s.tag_world_axis.up.y,s.tag_world_axis.up.z,s.tag_model_axis.forward.x,s.tag_model_axis.forward.y,s.tag_model_axis.forward.z,
        s.tag_model_axis.left.x,s.tag_model_axis.left.y,s.tag_model_axis.left.z,s.tag_model_axis.up.x,s.tag_model_axis.up.y,s.tag_model_axis.up.z,
        s.position_roundtrip_error,s.axis_roundtrip_error,s.tag_attempted);
}
bool barrel_guards() noexcept {
    __try {
        return !bovr::native_guard::compare(reinterpret_cast<void*>(0x4bb2e0), 12, "3e9cdd8571bdb17f3bafeb720da0fb12919e3a475eeb0969712782b3736a8a7d")&&
            !bovr::native_guard::compare(reinterpret_cast<void*>(0x607fd0), 10, "91ef94787287a005234ca62956862b8a9196076c26872a5349a226fb77408aa0")&&
            !bovr::native_guard::compare(reinterpret_cast<void*>(0x47e180), 8, "d2dd42099257d4f7aa94f0bfd298e37d62a1faf8a01e91a00fb14c3e0e1c74a5")&&
            !bovr::native_guard::compare(reinterpret_cast<void*>(0x59c4a0), 8, "baaee2b64a30d7857ba4fd8ccd0862597f979d456dfa340639359dffed61e08f");
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
bool configure_weapon_barrel_probe(HMODULE image,bool hook) noexcept {
    const DWORD error=GetLastError();bool result=false;
    try {
        wchar_t flag[8]{};
        if(GetEnvironmentVariableW(L"BOVR_WEAPON_BARREL_PROBE",flag,8)==1&&flag[0]==L'1'&&hook&&
            image==GetModuleHandleW(nullptr)&&reinterpret_cast<std::uintptr_t>(image)==0x400000) {
            std::string reason;
            if(validate_path(executable_path()).accepted&&validate_loaded_headers(image,reason)&&barrel_guards()) {
                observer.host={nullptr,native_now,native_precommand,native_borrow,native_tag,native_report};
                barrel_enabled.store(true,std::memory_order_release);result=true;
                trace("t5_weapon_barrel_configured","passive=1 interval_ms=100 max_native_attempts=2048 extra_patches=0 placement_writes=0 shot_writes=0 topology_is_not_profile_identity=1");
            }
        }
    }catch(...){}
    SetLastError(error);return result;
}
bool weapon_barrel_probe_enabled() noexcept {return barrel_enabled.load(std::memory_order_acquire);}
void observe_weapon_barrel_after(const NativeWeaponContext& c) {
    const DWORD error=GetLastError();
    __try {if(weapon_barrel_probe_enabled())observer.observe(c,GetCurrentThreadId(),GetTickCount64());}
    __finally {SetLastError(error);}
}
}

// Independent passive comparison, sharing only the already-proven current
// native borrow reader above. No existing barrel callback is replaced.
namespace bovr::t5 {
namespace {
namespace anim=animation_detail;
#include "t5/weapon_animation_native_guards.hpp"
struct AnimationProfile {
    std::array<unsigned short,borrowed_profile::max_profile_bones> bone_ids{};
    std::array<unsigned char,borrowed_profile::max_profile_bones> parent_deltas{};
    std::array<unsigned char,borrowed_profile::max_profile_models> counts{},roots{};
    unsigned gun_bone{},profile_row{},weapon_index{4},profile_revision{1};
    friend bool operator==(const AnimationProfile&,const AnimationProfile&)=default;
};
struct AnimationAssembly {
    std::uintptr_t arena{};
    unsigned short duplicate_handle{};
    unsigned char duplicate_size{};
    std::array<unsigned char,borrowed_profile::max_profile_models> parents{};
    std::array<unsigned char,255> duplicates{};
    std::array<std::uintptr_t,borrowed_profile::max_profile_models> names{},parent_data{},bind_data{};
    friend bool operator==(const AnimationAssembly&,const AnimationAssembly&)=default;
};
struct AnimationFrame {
    std::uint64_t serial{},started{};
    NativeWeaponContext context{};
    NativeBorrow borrow{};
    PreCommandSnapshot command{};
    anim::LockedCopy before{};
    AnimationProfile profile{};
    unsigned gun_bone{};
    bool valid{};
};
static_assert(std::is_trivially_copyable_v<AnimationFrame>&&sizeof(AnimationFrame)<=sizeof(WeaponAnimationStorage));
std::atomic<bool> animation_enabled{};
std::atomic<bool> native_clear_enabled{};
std::atomic<bool> olympia_enabled{};
std::atomic_flag animation_running=ATOMIC_FLAG_INIT;
std::atomic<std::uint64_t> animation_serial{};
std::uint64_t animation_last{},animation_attempts{};
enum class AnimationStatus:unsigned {accepted,context,throttled,command,profile,borrow,pre_cache,post_cache,changed,geometry,intervening,count};
struct ProfilePerformanceOptions {bool optimized{true},diagnostics{};std::uint64_t frequency{};};
const ProfilePerformanceOptions& profile_performance_options() noexcept {
    static const auto options=[]() noexcept {
        ProfilePerformanceOptions result{};wchar_t flag[8]{};
        result.optimized=!(GetEnvironmentVariableW(L"BOVR_PROFILE_PERFORMANCE_OPTIMIZATION",flag,8)==1&&flag[0]==L'0');
        result.diagnostics=GetEnvironmentVariableW(L"BOVR_PROFILE_PERFORMANCE_DIAGNOSTICS",flag,8)==1&&flag[0]==L'1';
        if(result.diagnostics){LARGE_INTEGER frequency{};
            result.diagnostics=QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0;
            if(result.diagnostics)result.frequency=static_cast<std::uint64_t>(frequency.QuadPart);}
        trace("t5_profile_performance_configured","optimized=%u diagnostics=%u native_recopies_unchanged=1 locked_payload_rechecks_unchanged=1 cross_invocation_reuse=0",
            result.optimized,result.diagnostics);
        return result;
    }();return options;
}
std::uint64_t profile_performance_tick() noexcept {
    LARGE_INTEGER value{};return QueryPerformanceCounter(&value)&&value.QuadPart>0?static_cast<std::uint64_t>(value.QuadPart):0;
}
struct ProfilePerformanceTotals {
    std::uint64_t first{},calls{},accepted{},read_ticks{},catalog_ticks{},values_ticks{},total_ticks{},worst_ticks{};
    std::uint64_t canonical_builds{},canonical_verifications{},bind_conversions{};
    unsigned reports{};
};
struct ProfilePerformanceSample {
    const ProfilePerformanceOptions& options;
    profile_detail::ReadWorkCounts reader{};borrowed_profile::CatalogWorkCounts catalog{};
    std::uint64_t started{},read_ticks{},catalog_ticks{},values_ticks{};bool accepted{};
    explicit ProfilePerformanceSample(const ProfilePerformanceOptions& value) noexcept:options(value){
        if(options.diagnostics)started=profile_performance_tick();}
    std::uint64_t tick() const noexcept {return options.diagnostics?profile_performance_tick():0;}
    static std::uint64_t elapsed(std::uint64_t before,std::uint64_t after) noexcept {return before&&after>=before?after-before:0;}
    ~ProfilePerformanceSample() noexcept {
        if(!options.diagnostics||!started)return;
        const auto finished=profile_performance_tick();if(!finished||finished<started)return;
        static thread_local ProfilePerformanceTotals totals{};if(totals.reports>=120)return;
        if(!totals.first)totals.first=started;
        ++totals.calls;if(accepted)++totals.accepted;
        totals.read_ticks+=read_ticks;totals.catalog_ticks+=catalog_ticks;totals.values_ticks+=values_ticks;
        const auto total=finished-started;totals.total_ticks+=total;if(total>totals.worst_ticks)totals.worst_ticks=total;
        totals.canonical_builds+=reader.canonical_builds+catalog.canonical_builds;
        totals.canonical_verifications+=catalog.canonical_verifications;totals.bind_conversions+=catalog.bind_conversions;
        if(finished-totals.first<options.frequency*5)return;
        const auto milliseconds=[&](std::uint64_t ticks){return 1000.0*static_cast<double>(ticks)/static_cast<double>(options.frequency);};
        trace("t5_profile_performance","report=%u thread=%lu optimized=%u calls=%llu accepted=%llu rejected=%llu interval_ms=%.6f read_ms=%.6f catalog_ms=%.6f values_ms=%.6f total_ms=%.6f mean_ms=%.6f worst_ms=%.6f canonical_builds=%llu canonical_verifications=%llu bind_conversions=%llu profile_read_attempts=%llu native_recopies_unchanged=1 cross_invocation_reuse=0",
            ++totals.reports,GetCurrentThreadId(),options.optimized,totals.calls,totals.accepted,totals.calls-totals.accepted,
            milliseconds(finished-totals.first),milliseconds(totals.read_ticks),milliseconds(totals.catalog_ticks),milliseconds(totals.values_ticks),
            milliseconds(totals.total_ticks),milliseconds(totals.total_ticks)/static_cast<double>(totals.calls),milliseconds(totals.worst_ticks),
            totals.canonical_builds,totals.canonical_verifications,totals.bind_conversions,totals.calls);
        const auto reports=totals.reports;totals={};totals.reports=reports;
    }
};
bool animation_profile_shape(const AnimationProfile& p,unsigned* total) noexcept {
    if(!total)return false;*total=0;
    const auto* row=borrowed_profile::shape(p.profile_row);
    const auto* known=borrowed_profile::get(p.profile_row);
    if(!row||!known||known->model.size()>p.counts.size()||!borrowed_profile::binding(p.profile_row,p.weapon_index,p.profile_revision)||p.gun_bone!=row->gun)return false;
    unsigned count=0;
    for(unsigned i=0;i<p.counts.size();++i){
        if(i<known->model.size()){
            if(p.counts[i]!=known->model[i].count||p.roots[i]!=known->model[i].root)return false;
            count+=p.counts[i];
        }else if(p.counts[i]||p.roots[i])return false;
    }
    if(count!=row->total||count>p.bone_ids.size()||count>p.parent_deltas.size()||row->flash>=count)return false;
    *total=count;return true;
}
// Copied-value admission only. The reader still owns its original protected
// context/asset recopy; the catalog owns no native pointers or retail payload.
bool animation_profile_record(const NativeWeaponContext& c,const profile_detail::ProfileRecord& r,unsigned row_index,AnimationProfile* out,
    bool registered_here=false,borrowed_profile::CatalogWorkCounts* counts=nullptr) noexcept {
    if(!out)return false;*out={};const auto* row=borrowed_profile::shape(row_index);
    // registered_here is used ONLY below, immediately after exact registration
    // with this same local, unmodified record. Catalog entries are immutable.
    // It removes a duplicate pure candidate check, never a native recheck.
    if(!row||(!registered_here&&!borrowed_profile::matches_runtime_record(row_index,c.weapon_index,r.copies,r.canonical,
        {r.assembly.model_parents,r.assembly.duplicate_data,r.assembly.total_bones},false,counts))||r.copies.after.size()>out->counts.size())return false;
    out->profile_row=row_index;out->weapon_index=c.weapon_index;out->profile_revision=row->revision;out->gun_bone=row->gun;
    unsigned total=0;
    for(unsigned i=0;i<r.copies.after.size();++i){const auto& model=r.copies.after[i];
        if(model.bones.size()>255||model.root_count>255||total+model.bones.size()>out->bone_ids.size())return false;
        out->counts[i]=static_cast<unsigned char>(model.bones.size());out->roots[i]=static_cast<unsigned char>(model.root_count);
        for(const auto& bone:model.bones){out->parent_deltas[total]=static_cast<unsigned char>(bone.parent_delta);out->bone_ids[total++]=bone.script_id;}}
    unsigned checked{};return animation_profile_shape(*out,&checked)&&checked==total;
}
bool animation_profile(const NativeWeaponContext& c,std::uint64_t now,AnimationProfile* out) noexcept {
    if(!out)return false;*out={};
    const auto& options=profile_performance_options();ProfilePerformanceSample sample(options);
    try {
        profile_detail::ProfileRecord r;
        const profile_detail::ReadHost h{nullptr,profile_detail::protected_profile_copy,native_now,native_precommand,options.optimized};
        const auto read_started=sample.tick();
        const auto read_status=profile_detail::read_profile(h,c,GetCurrentThreadId(),now,&r,options.diagnostics?&sample.reader:nullptr);
        sample.read_ticks=sample.elapsed(read_started,sample.tick());
        static unsigned diagnostics{};
        if(diagnostics<20){
            ++diagnostics;
            trace("t5_weapon_animation_profile_diagnostic","ordinal=%u comparison=%llu read_status=%u identity_status=%u models=%zu selected_model=%u selected_bone=%u copies=%zu total_bones=%u parent_count=%zu duplicate_count=%zu placement_scale=%.9g",
                diagnostics,animation_serial.load(),static_cast<unsigned>(read_status),static_cast<unsigned>(r.identity_status),r.canonical.model_payloads.size(),r.canonical.selected_model,r.canonical.selected_local_bone,r.copies.after.size(),r.assembly.total_bones,r.assembly.model_parents.size(),r.assembly.duplicate_data.size(),c.placement.scale);
            if(read_status==profile_detail::ReadStatus::accepted&&r.copies.after.size()==2&&r.canonical.model_payloads.size()==2){
                const auto& arms=r.copies.after[0];const auto& gun=r.copies.after[1];
                const auto a=sha256(r.canonical.model_payloads[0]),g=sha256(r.canonical.model_payloads[1]),m=sha256(r.canonical.assembly_payload);
                trace("t5_weapon_animation_profile_identity","ordinal=%u arm_count=%zu arm_roots=%u gun_count=%zu gun_roots=%u flash_parent=%u arms_sha256=%s gun_sha256=%s ordered_sha256=%s",
                    diagnostics,arms.bones.size(),arms.root_count,gun.bones.size(),gun.root_count,gun.bones.size()>r.canonical.selected_local_bone?gun.bones[r.canonical.selected_local_bone].parent_delta:0,a.c_str(),g.c_str(),m.c_str());
            }
        }
        if(read_status!=profile_detail::ReadStatus::accepted||r.canonical.model_payloads.size()<2)return false;
        if(!exact_profile_aim_enabled())return false;
        const auto catalog_started=sample.tick();
        const auto row_index=borrowed_profile::register_runtime_profile(c.weapon_index,r.copies,r.canonical,
            {r.assembly.model_parents,r.assembly.duplicate_data,r.assembly.total_bones},nullptr,options.optimized,
            options.diagnostics?&sample.catalog:nullptr);
        sample.catalog_ticks=sample.elapsed(catalog_started,sample.tick());
        if(row_index==borrowed_profile::invalid_runtime_row)return false;
        const auto values_started=sample.tick();
        sample.accepted=animation_profile_record(c,r,row_index,out,options.optimized,options.diagnostics?&sample.catalog:nullptr);
        sample.values_ticks=sample.elapsed(values_started,sample.tick());return sample.accepted;
    }catch(...){return false;} // Copied values/hashing only; never a native helper.
}
bool animation_extra_context(const NativeWeaponContext& c) noexcept {
    __try {
        const auto pose=c.cg_identity+0xc5e04;
        const int fire_type=load<int>(c.definition_identity+0x30);
        return load<unsigned char>(pose+2)==20&&!load<unsigned char>(pose+5)&&!load<unsigned char>(pose+0x14)&&
            (load<int>(c.definition_identity+0x20)!=9||c.weapon_type==4)&&fire_type>=0&&fire_type<=6&&
            finite(load<Vec>(pose+0x24))&&finite(load<Vec>(pose+0x30));
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool animation_assembly_copy(const NativeBorrow& b,const AnimationProfile& profile,AnimationAssembly* out,std::uintptr_t arena_pointer=0x3067c00) noexcept {
    if(!out)return false;*out={};
    const auto* known=borrowed_profile::get(profile.profile_row);
    unsigned total{};if(!known||!animation_profile_shape(profile,&total)||b.model_count!=known->model.size()||b.bone_count!=total)return false;
    const auto& row=*borrowed_profile::shape(profile.profile_row);
    for(unsigned i=0;i<total;++i)if(b.bone_names[i]!=profile.bone_ids[i])return false;
    const auto range=[](std::uintptr_t p,unsigned bytes){return p>=0x10000&&bytes&&p<=UINT32_MAX-bytes+1;};
    if(!range(b.dobj,0x7c)||!range(b.models_table,5*b.model_count))return false;
    __try {
        auto& a=*out;a.arena=load<std::uintptr_t>(arena_pointer);a.duplicate_handle=load<unsigned short>(b.dobj+4);a.duplicate_size=load<unsigned char>(b.dobj+8);
        const unsigned expected_size=known->duplicate_data.empty()?21U:static_cast<unsigned>(known->duplicate_data.size());
        if(a.arena<0x10000||!a.duplicate_handle||a.duplicate_size!=expected_size||a.arena>UINT32_MAX-16U*a.duplicate_handle-4U-a.duplicate_size)return false;
        const auto entry=a.arena+16U*a.duplicate_handle;if(!load<unsigned short>(entry))return false;
        memcpy(a.duplicates.data(),reinterpret_cast<void*>(entry+4),a.duplicate_size);
        if(known->duplicate_data.empty()){if(a.duplicates!=std::array<unsigned char,255>{})return false;}
        else if(!std::equal(known->duplicate_data.begin(),known->duplicate_data.end(),a.duplicates.begin()))return false;
        memcpy(a.parents.data(),reinterpret_cast<void*>(b.models_table+4*b.model_count),b.model_count);
        if(known->model_parents.empty()){
            if(b.model_count!=2||a.parents[0]!=255||a.parents[1]!=row.attachment)return false;
        }else if(known->model_parents.size()!=b.model_count||!std::equal(known->model_parents.begin(),known->model_parents.end(),a.parents.begin()))return false;
        unsigned start=0;
        for(unsigned i=0;i<b.model_count;++i){const auto model=b.models[i];
            if(!range(model,0x20))return false;
            if(load<unsigned char>(model+4)!=profile.counts[i]||load<unsigned char>(model+5)!=profile.roots[i]||b.model_bones[i]!=profile.counts[i])return false;
            a.names[i]=load<std::uintptr_t>(model+8);a.parent_data[i]=load<std::uintptr_t>(model+0xc);a.bind_data[i]=load<std::uintptr_t>(model+0x1c);
            if(!range(a.names[i],2U*profile.counts[i])||
               (profile.counts[i]>profile.roots[i]&&!range(a.parent_data[i],profile.counts[i]-profile.roots[i]))||!range(a.bind_data[i],32U*profile.counts[i]))return false;
            for(unsigned j=0;j<profile.counts[i];++j){
                if(load<unsigned short>(a.names[i]+2*j)!=profile.bone_ids[start+j])return false;
                if(j<profile.roots[i]){if(profile.parent_deltas[start+j])return false;}
                else if(load<unsigned char>(a.parent_data[i]+j-profile.roots[i])!=profile.parent_deltas[start+j])return false;
            }
            start+=profile.counts[i];
        }
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct AnimationNative {const NativeWeaponContext* c{};NativeBorrow expected{};const AnimationProfile* profile{};AnimationAssembly assembly{};std::uint64_t clear_observation{},wrapper_observation{};};
bool borrowed_protected_copy(void*,std::uintptr_t p,void* out,std::size_t size) noexcept {
    __try{memcpy(out,reinterpret_cast<void*>(p),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Tests may supply owned copied buffers here. Production ALWAYS rereads native
// globals/context below before passing its just-copied borrow into this provider.
bool animation_copied_state(const AnimationNative& h,const NativeBorrow& b,const anim::LockedRequest& r,anim::CacheCopy* out,
                            std::uintptr_t arena_pointer=0x3067c00,bool full_payload=false) noexcept {
    unsigned total{};if(!out||!h.c||!h.profile||!animation_profile_shape(*h.profile,&total)||
       h.c->weapon_index!=h.profile->weapon_index||!(b==h.expected)||r.object!=b.dobj||r.bone_count!=total||
       r.gun_bone!=h.profile->gun_bone||r.flash_bone!=borrowed_profile::shape(h.profile->profile_row)->flash)return false;
    const auto& row=*borrowed_profile::shape(h.profile->profile_row);
    if(h.profile->profile_row>=borrowed_profile::runtime_row_base&&((row.weapon==42&&!olympia_enabled.load())||!full_payload||b.tag_model!=1||
       b.tag_local_bone!=row.flash_local||b.tag_bone!=row.flash||b.bone_count!=row.total))return false;
    AnimationAssembly assembly{};if(!animation_assembly_copy(b,*h.profile,&assembly,arena_pointer)||!(assembly==h.assembly))return false;
    if(full_payload){
        if(!borrowed_profile::current_payload(h.profile->profile_row,borrowed_protected_copy,nullptr,b.models,h.profile->bone_ids,assembly.arena))return false;}
    __try {
        out->clock=b.skeleton_clock;out->stamp=load<int>(b.dobj+0x50);out->matrices=load<std::uintptr_t>(b.dobj+0x54);
        out->pose={b.pose_origin,b.pose_angles};
        // Exact455C50 getter: five MSB-first computed words at DObj+3C.
        const auto computed=[&](unsigned bone){return (load<std::uint32_t>(b.dobj+0x3c+4*(bone>>5))&(0x80000000U>>(bone&31)))!=0;};
        out->gun_computed=computed(r.gun_bone);out->flash_computed=computed(r.flash_bone);
        if(r.require_completed_native_clear)memcpy(out->part_bits.data(),reinterpret_cast<void*>(b.dobj+0x14),60);
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool animation_current_state(void* p,const anim::LockedRequest& r,anim::CacheCopy* out,bool full_payload) noexcept {
    const auto& h=*static_cast<AnimationNative*>(p);NativeBorrow b{};unsigned total{};
    if(h.clear_observation&&pose_clear_serial()!=h.clear_observation)return false;
    if(h.clear_observation){const auto scope=read_weapon_native_clear_scope();
        if(h.wrapper_observation?(!scope.valid||scope.depth!=1||scope.token!=h.wrapper_observation):scope.depth!=0)return false;}
    if(!out||!h.profile||!animation_profile_shape(*h.profile,&total)||r.object!=h.expected.dobj||
       r.bone_count!=total||r.gun_bone!=h.profile->gun_bone||r.flash_bone!=borrowed_profile::shape(h.profile->profile_row)->flash)return false;
    if(!native_borrow(nullptr,*h.c,&b)||!(b==h.expected)||!animation_extra_context(*h.c))return false;
    return animation_copied_state(h,b,r,out,0x3067c00,full_payload);
}
bool animation_state(void* p,const anim::LockedRequest& r,anim::CacheCopy* out) noexcept {
    return animation_current_state(p,r,out,false);
}
void animation_lock(void*,std::uintptr_t obj){reinterpret_cast<void(__cdecl*)(void*)>(0x504970)(reinterpret_cast<void*>(obj));}
void animation_unlock(void*,std::uintptr_t obj){reinterpret_cast<void(__cdecl*)(void*)>(0x54d480)(reinterpret_cast<void*>(obj));}
int animation_cache(void*,std::uintptr_t obj,unsigned bone,std::uintptr_t* matrices){
    return reinterpret_cast<int(__cdecl*)(void*,unsigned,std::uintptr_t*)>(0x4c2e80)(reinterpret_cast<void*>(obj),bone,matrices);
}
void animation_hierarchy(void*,std::uintptr_t obj,unsigned bone,std::uint32_t* bits){
    reinterpret_cast<void(__cdecl*)(void*,unsigned,std::uint32_t*)>(0x45af30)(reinterpret_cast<void*>(obj),bone,bits);
}
void animation_pose(void* p,std::uintptr_t obj,const std::uint32_t* bits){
    const auto& h=*static_cast<AnimationNative*>(p);
    reinterpret_cast<void(__cdecl*)(const void*,void*,const std::uint32_t*)>(0x5c7dc0)(reinterpret_cast<void*>(h.c->cg_identity+0xc5e04),reinterpret_cast<void*>(obj),bits);
}
void animation_skeleton(void*,std::uintptr_t obj,const std::uint32_t* bits){
    reinterpret_cast<void(__cdecl*)(void*,const std::uint32_t*)>(0x564ab0)(reinterpret_cast<void*>(obj),bits);
}
bool animation_copy(void*,const anim::LockedRequest& r,std::uintptr_t matrices,anim::NativeMat32* gun,anim::NativeMat32* flash) noexcept {
    if(!gun||!flash||!r.bone_count||r.bone_count>borrowed_profile::max_profile_bones||r.gun_bone>=r.bone_count||r.flash_bone>=r.bone_count||
       matrices<0x10000||matrices>UINT32_MAX-32*r.bone_count)return false;
    __try {memcpy(gun,reinterpret_cast<void*>(matrices+32*r.gun_bone),32);memcpy(flash,reinterpret_cast<void*>(matrices+32*r.flash_bone),32);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
anim::LockedStatus animation_capture(const NativeWeaponContext& c,const NativeBorrow& b,const AnimationProfile& profile,bool pre,anim::LockedCopy* out) {
    if(profile.profile_row>=borrowed_profile::runtime_row_base)return anim::LockedStatus::identity; // Runtime rows use the native-clear route.
    AnimationNative native{&c,b,&profile,{}};
    if(!animation_assembly_copy(b,profile,&native.assembly))return anim::LockedStatus::identity;
    const anim::LockedHost h{&native,animation_lock,animation_unlock,animation_state,animation_cache,animation_hierarchy,animation_pose,animation_skeleton,animation_copy};
    return anim::capture_locked(h,{b.dobj,profile.gun_bone,b.tag_bone,b.bone_count,b.skeleton_clock,pre},out);
}
void animation_report(AnimationStatus status,const AnimationFrame& f,const anim::LockedCopy* after=nullptr,const anim::Difference* d=nullptr,unsigned native_status=0) noexcept {
    static std::array<std::array<std::uint64_t,19>,static_cast<unsigned>(AnimationStatus::count)> counts{};
    const auto state=f.context.weapon_state>=0&&f.context.weapon_state<18?static_cast<unsigned>(f.context.weapon_state):18U;
    const auto n=++counts[static_cast<unsigned>(status)][state];if(n>4&&n%120)return;
    const anim::LockedCopy empty{};const anim::Difference zero{};if(!after)after=&empty;if(!d)d=&zero;
    trace("t5_weapon_animation","comparison=%llu status=%u count=%llu native_status=%u thread=%lu command=%d native_time=%d clock=%d epoch=%llu command_sequence=%llu weapon=%u state=%d gun_bone=%u flash_bone=%u skeleton_clock=%d pre_stamp=%d post_stamp=%d pre_calculated=%u post_calculated=%u gun_position_delta=%.8f gun_angle_rad=%.8f flash_position_delta=%.8f flash_angle_rad=%.8f native_placement_writes=0 shot_writes=0 forced_cache_clears=0 xr_prediction_authority=0 native_fault_unlock=original_semantics",
        f.serial,static_cast<unsigned>(status),n,native_status,GetCurrentThreadId(),f.context.command_number,f.context.native_time,f.context.command_clock,
        f.command.gameplay_epoch,f.command.sequence,f.context.weapon_index,f.context.weapon_state,f.gun_bone,f.borrow.tag_bone,f.borrow.skeleton_clock,
        f.before.old_stamp,after->final_stamp,f.before.calculated,after->calculated,d->gun_position,d->gun_angle_radians,d->flash_position,d->flash_angle_radians);
    if(status==AnimationStatus::accepted){
        const auto raw=[&](const char* phase,const anim::LockedCopy& sample){
            std::array<std::uint32_t,8> gun{},flash{};memcpy(gun.data(),&sample.gun,32);memcpy(flash.data(),&sample.flash,32);
            trace("t5_weapon_animation_raw","comparison=%llu phase=%s gun_u32=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X flash_u32=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X pose_origin=%.9g,%.9g,%.9g pose_angles=%.9g,%.9g,%.9g pose_source=persistent_cpose",
                f.serial,phase,gun[0],gun[1],gun[2],gun[3],gun[4],gun[5],gun[6],gun[7],flash[0],flash[1],flash[2],flash[3],flash[4],flash[5],flash[6],flash[7],
                sample.pose.origin.x,sample.pose.origin.y,sample.pose.origin.z,sample.pose.angles.x,sample.pose.angles.y,sample.pose.angles.z);
        };
        raw("pre",f.before);raw("post",*after);
    }
}
bool animation_same_identity(NativeBorrow a,const NativeBorrow& b) noexcept {
    a.model_origin=b.model_origin;a.model_axis=b.model_axis;a.pose_origin=b.pose_origin;a.pose_angles=b.pose_angles;return a==b;
}
void animation_before_work(const NativeWeaponContext& c,AnimationFrame& f) {
    f.context=c;f.started=GetTickCount64();
    if(!supported(c,GetCurrentThreadId())||!animation_extra_context(c)){animation_report(AnimationStatus::context,f);return;}
    if(animation_attempts>=512||(animation_last&&f.started>=animation_last&&f.started-animation_last<100))return;
    animation_last=f.started;++animation_attempts;
    if(!native_precommand(nullptr,&f.command)||!fresh_command(c,f.command,f.started)){animation_report(AnimationStatus::command,f);return;}
    if(!animation_profile(c,f.started,&f.profile)||f.profile.profile_row>=borrowed_profile::runtime_row_base){animation_report(AnimationStatus::profile,f);return;}
    f.gun_bone=f.profile.gun_bone;
    if(!native_borrow(nullptr,c,&f.borrow)||f.borrow.tag_model!=1||f.borrow.tag_local_bone!=6||f.borrow.tag_bone!=f.gun_bone+6){animation_report(AnimationStatus::borrow,f);return;}
    const auto status=animation_capture(c,f.borrow,f.profile,true,&f.before);
    if(status!=anim::LockedStatus::accepted){animation_report(AnimationStatus::pre_cache,f,nullptr,nullptr,static_cast<unsigned>(status));return;}
    if(animation_serial.load()!=f.serial){animation_report(AnimationStatus::intervening,f);return;}
    f.valid=true;
}
void animation_after_work(const NativeWeaponContext& c,AnimationFrame& f) {
    if(!f.valid)return;f.valid=false;
    if(animation_serial.load()!=f.serial){animation_report(AnimationStatus::intervening,f);return;}
    const auto now=GetTickCount64();NativeBorrow b{};PreCommandSnapshot command{};AnimationProfile profile{};
    if(now<f.started||now-f.started>100||!supported(c,GetCurrentThreadId())||!animation_extra_context(c)||
       c.native_thread!=f.context.native_thread||c.native_time!=f.context.native_time||c.native_frame_time!=f.context.native_frame_time||
       c.command_number!=f.context.command_number||c.command_clock!=f.context.command_clock||c.weapon_index!=f.context.weapon_index||
       c.weapon_state!=f.context.weapon_state||c.other_weapon_state!=f.context.other_weapon_state||
       !native_precommand(nullptr,&command)||!fresh_command(c,command,now)||command.sequence!=f.command.sequence||command.gameplay_epoch!=f.command.gameplay_epoch||
       !animation_profile(c,now,&profile)||!(profile==f.profile)||!native_borrow(nullptr,c,&b)||!animation_same_identity(f.borrow,b)){
        animation_report(AnimationStatus::changed,f);return;
    }
    anim::LockedCopy after{};const auto status=animation_capture(c,b,profile,false,&after);
    if(status!=anim::LockedStatus::accepted){animation_report(AnimationStatus::post_cache,f,&after,nullptr,static_cast<unsigned>(status));return;}
    anim::LocalGeometry pre_geometry{},post_geometry{};anim::Difference delta{};
    if(animation_serial.load()!=f.serial||GetTickCount64()-f.started>100||!native_precommand(nullptr,&command)||
       !fresh_command(c,command,GetTickCount64())||command.sequence!=f.command.sequence||command.gameplay_epoch!=f.command.gameplay_epoch){animation_report(AnimationStatus::intervening,f,&after);return;}
    if(!anim::derive_local(f.before.gun,f.before.flash,f.before.pose,&pre_geometry)||!anim::derive_local(after.gun,after.flash,after.pose,&post_geometry)||
       !anim::difference(pre_geometry,post_geometry,&delta)){animation_report(AnimationStatus::geometry,f,&after);return;}
    animation_report(AnimationStatus::accepted,f,&after,&delta);
}
}
bool configure_weapon_animation_probe(HMODULE image,bool hook) noexcept {
    const auto error=GetLastError();bool ok=false;
    try {
        wchar_t flag[8]{},writer[8]{},lifecycle[8]{};
        GetEnvironmentVariableW(L"BOVR_WEAPON_PLACEMENT",writer,8);GetEnvironmentVariableW(L"BOVR_WEAPON_LIFECYCLE_PROBE",lifecycle,8);
        if(GetEnvironmentVariableW(L"BOVR_WEAPON_ANIMATION_PROBE",flag,8)==1&&flag[0]==L'1'&&writer[0]!=L'1'&&lifecycle[0]!=L'1'&&hook&&
           image==GetModuleHandleW(nullptr)&&reinterpret_cast<std::uintptr_t>(image)==0x400000) {
            std::string reason;
            if(validate_path(executable_path()).accepted&&validate_loaded_headers(image,reason)&&animation_native_guards()){
                animation_enabled=true;ok=true;trace("t5_weapon_animation_configured","passive=1 interval_ms=100 maximum_pre_attempts=512 same_clock_pre=reject exact_known_assembly=1 native_cache_computation=1 aiming=0 new_instruction_hooks=0");
            }
        }
    }catch(...){}
    SetLastError(error);return ok;
}
bool weapon_animation_probe_enabled() noexcept {return animation_enabled.load()&&!native_clear_enabled.load();}
void observe_weapon_animation_before(const NativeWeaponContext& c,WeaponAnimationStorage* storage) {
    const auto error=GetLastError();if(!storage)return;
    memset(storage,0,sizeof(*storage));auto& f=*reinterpret_cast<AnimationFrame*>(storage);f.serial=++animation_serial;
    if(!weapon_animation_probe_enabled()||animation_running.test_and_set()){SetLastError(error);return;}
    __try {animation_before_work(c,f);}
    __finally {animation_running.clear();SetLastError(error);}
}
void observe_weapon_animation_after(const NativeWeaponContext& c,WeaponAnimationStorage* storage) {
    const auto error=GetLastError();if(!storage||!weapon_animation_probe_enabled())return;
    auto& f=*reinterpret_cast<AnimationFrame*>(storage);
    if(animation_running.test_and_set()){f.valid=false;SetLastError(error);return;}
    __try {animation_after_work(c,f);}
    __finally {f.valid=false;animation_running.clear();SetLastError(error);}
}
}
#include "t5/weapon_native_clear_adapter.inc"
