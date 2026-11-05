// SPDX-License-Identifier: GPL-3.0-only
#include "t5/mounted_gun_runtime.hpp"
#include "t5/mounted_look_context.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/identity.hpp"
#include "t5/native_read_recipe.hpp"
#include "input/mounted_gun_aim.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#if !defined(_M_IX86)
#error The retail mounted gun bridges require MSVC x86.
#endif
namespace bovr::t5 {
namespace mounted_gun_runtime_detail {
namespace xr=wawvr::xr;
namespace aim=input::mounted_gun_aim;
constexpr std::uintptr_t entities=0x1a796f8,entity_stride=0x34c;
constexpr std::uintptr_t client_entities_slot=0x2ff6684,client_entity_stride=0x31c;
constexpr std::uintptr_t aim_target=0x81c540,fire_target=0x819970,visual_target=0x782270;
std::uintptr_t aim_original=aim_target,fire_original=fire_target,visual_original=visual_target;
enum class Kind:unsigned {aim,fire,visual};
// Only these three guarded native CALL instructions enter the bridges. Tests
// replace them with the return sites of their authored native caller.
std::array<std::uintptr_t,3> consumer_returns{0x81c7a2,0x819cbf,0x5c7ed1};
constexpr std::uintptr_t main_thread_slot=0x251ca98,server_thread_slot=0x251cac0;
SRWLOCK cache_lock=SRWLOCK_INIT,install_lock=SRWLOCK_INIT;
std::atomic<bool> enabled{};
std::unique_ptr<mounted_look::Source> owner_source;
mounted_look::Host owner_host=mounted_look::production_host();
struct Scene {
    input::CommandRigSample rig{};
    xr::Vec3f origin{};xr::Basis3f axis{};
    mounted_look::Copy owner{};
    std::uintptr_t refdef{};
    std::uint64_t serial{};
    bool valid{};
};
struct NativeAim {
    std::uintptr_t turret{},client{};
    std::uint64_t scene_serial{},epoch{},observed_ms{};
    std::array<float,3> relative{},base{};
    Scene source{};
    bool valid{};
};
Scene scene{};NativeAim native_aim{};std::uint64_t next_serial{};
thread_local unsigned depth{};
std::atomic<std::uint64_t> accepted[3]{};
enum class Refusal:unsigned {owner_pre,owner_storage,owner_ineligible,owner_changed,scene_absent,
    scene_identity,tracking,turret,proposal,consumer_identity,witness,publication,consumer_thread,count};
std::atomic<std::uint64_t> refused[static_cast<unsigned>(Refusal::count)]{};
std::atomic<std::uint64_t> consumer_refused[3][static_cast<unsigned>(Refusal::count)]{};
std::atomic<std::uint64_t> consumer_entries[3]{};
thread_local unsigned diagnostic_kind=3;
thread_local const std::uint32_t* diagnostic_saved{};
bool reject(Refusal reason) noexcept {
    const auto i=static_cast<unsigned>(reason);
    const auto n=++refused[i];
    if(n<=3||n%600==0)trace("t5_mounted_gun_refused","reason=%u count=%llu thread=%lu",i,n,GetCurrentThreadId());
    if(diagnostic_kind<3&&diagnostic_saved){
        const auto count=++consumer_refused[diagnostic_kind][i];
        if(count<=3||count%600==0){PreCommandSnapshot pre{};read_pre_command_snapshot(&pre);
            trace("t5_mounted_gun_consumer_refused","consumer=%u reason=%u count=%llu thread=%lu owner=%lu caller=0x%x eax=0x%x esi=0x%x args=0x%x,0x%x",
                diagnostic_kind,i,count,GetCurrentThreadId(),pre.context.native_thread,diagnostic_saved[9],
                diagnostic_saved[7],diagnostic_saved[1],diagnostic_saved[10],diagnostic_saved[11]);}
    }
    return false;
}
template<class T>bool get(std::uintptr_t p,T* out) noexcept {
    if(!out||p<0x10000||p>UINT32_MAX-sizeof(T)+1)return false;
    __try{std::memcpy(out,reinterpret_cast<const void*>(p),sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T>bool put(std::uintptr_t p,const T& value) noexcept {
    if(p<0x10000||p>UINT32_MAX-sizeof(T)+1)return false;
    __try{std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool finite(const std::array<float,3>& v) noexcept {
    return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]);
}
bool same_mount(const mounted_look::Facts&a,const mounted_look::Facts&b) noexcept {
    const bool moving_tow=mounted_look::scoped_tow_eligible(a.scoped_tow,a.entity)&&
        mounted_look::scoped_tow_eligible(b.scoped_tow,b.entity)&&a.scoped_tow==b.scoped_tow;
    // The truck rotates its native clamp base as it moves. Keep its authored
    // limits and copied turret identity stable, without treating each vehicle
    // orientation update as a different mount. Ordinary turrets stay exact.
    const bool clamp_current=moving_tow?
        a.clamp_bits[2]==b.clamp_bits[2]&&a.clamp_bits[3]==b.clamp_bits[3]:a.clamp_bits==b.clamp_bits;
    return a.cg==b.cg&&a.ps==b.ps&&a.local==b.local&&a.entity==b.entity&&
        a.pm_type==b.pm_type&&a.pm_flags==b.pm_flags&&a.weapon_flags==b.weapon_flags&&
        (a.entity_flags&0x300)==(b.entity_flags&0x300)&&a.entity_flags2==b.entity_flags2&&
        a.camera==b.camera&&a.scoped_tow==b.scoped_tow&&clamp_current&&a.dvars==b.dvars;
}
template<class T>bool fact(std::uintptr_t address,T* out) noexcept {
    return owner_host.copy&&address>=0x10000&&address<=UINT32_MAX-sizeof(T)+1&&
        owner_host.copy(owner_host.context,address,out,sizeof(T));
}
bool read_facts(mounted_look::Facts* out) noexcept {
    mounted_look::Facts f{};
    if(!fact(0x2ff5354,&f.cg)||f.cg<0x10000||f.cg>UINT32_MAX-0xaa000)return false;
    f.ps=f.cg+0x8a364;
    if(!fact(f.cg+0x8a348,&f.game_time)||!fact(f.ps+0x1c4,&f.health)||!fact(f.ps+4,&f.pm_type)||
       !fact(f.ps+0x130,&f.local)||!fact(f.ps+0xc,&f.pm_flags)||!fact(f.ps+0x10,&f.weapon_flags)||
       !fact(f.ps+0xe0,&f.entity_flags)||!fact(f.ps+0xe4,&f.entity_flags2)||
       !fact(f.ps+0x440,&f.viewlock)||!fact(f.ps+0x444,&f.entity)||
       !fact(0x2910160,&f.catcher)||!fact(0x2910164,&f.connection)||!fact(0x2911e2c,&f.override_value)||
       !fact(0x2993850,&f.command_number)||!fact(0x2481764,&f.command_clock)||
       !fact(f.ps+0x180,&f.view_bits)||!fact(f.ps+0x84,&f.delta_bits)||!fact(f.ps+0x1a0,&f.clamp_bits))return false;
    constexpr std::uintptr_t camera[]{0x8a360,0xc,0xa9d44,0x1c,0x14,0xa4780,0xa4680};
    for(unsigned i=0;i<7;++i)if(!fact(f.cg+camera[i],&f.camera[i]))return false;
    if(f.camera[2]&&!mounted_look::copy_scoped_tow(owner_host,f.entity,&f.scoped_tow))return false;
    constexpr std::uintptr_t slots[]{0xbcd150,0xbcd1b8,0xbcaf04,0xbcaebc};
    constexpr const char* names[]{"player_topDownCamMode","player_viewLockEnt","player_view_pitch_up","player_view_pitch_down"};
    for(unsigned i=0;i<4;++i){auto& d=f.dvars[i];
        if(!fact(slots[i],&d.address)||d.address<0x10000||d.address>UINT32_MAX-0x20||
           !fact(d.address,&d.name_address)||!fact(d.address+0x10,&d.type)||!fact(d.address+0x18,&d.value)||
           !owner_host.copy(owner_host.context,d.name_address,d.name.data(),std::strlen(names[i])+1))return false;
    }
    *out=f;return true;
}
bool same_pre(const PreCommandSnapshot& a,const PreCommandSnapshot& b) noexcept {
    return a.sequence==b.sequence&&a.gameplay_epoch==b.gameplay_epoch&&a.observed_ms==b.observed_ms&&
        a.native_cg_identity==b.native_cg_identity&&a.context.native_thread==b.context.native_thread&&
        a.context.key_catcher==b.context.key_catcher&&a.context.connection_state==b.context.connection_state&&
        a.context.native_override==b.context.native_override&&a.context.command_number==b.context.command_number;
}
bool owner_thread(const PreCommandSnapshot& pre,bool server_consumer) noexcept {
    DWORD main{},server{};const auto current=GetCurrentThreadId();
    // Same verified engine thread table already used by helicopter consumers.
    // Render publication/visible pose remain main-thread owned. Only the exact
    // server aim/fire consumers may additionally run on the native server.
    return fact(main_thread_slot,&main)&&main&&pre.context.native_thread==main&&
        (current==main||(server_consumer&&fact(server_thread_slot,&server)&&server&&current==server));
}
bool copy_owner(const PreCommandSnapshot& pre,mounted_look::Copy* out,bool server_consumer=false) noexcept {
    if(!out||!owner_source||owner_source->configuration()!=mounted_look::Status::accepted)return false;
    const auto now=GetTickCount64();const auto& c=pre.context;
    if(!owner_thread(pre,server_consumer))return reject(Refusal::consumer_thread);
    if(!c.valid||c.local_client||c.key_catcher||c.native_override||c.connection_state<9||
       !pre.sequence||!pre.gameplay_epoch||!pre.native_gameplay_fields_valid||pre.gameplay_active||
       !pre.observed_ms||now<pre.observed_ms||now-pre.observed_ms>100)return reject(Refusal::owner_pre);
    // These consumers run after CreateCmd; its counters/gameTime are not an
    // equality key for a later render or server call. The actual current mount
    // is read twice in full, bracketed by the same still-fresh command epoch.
    mounted_look::Facts first{},second{};PreCommandSnapshot latest{};
    if(!read_facts(&first)||!read_facts(&second))return reject(Refusal::owner_storage);
    if(!mounted_look::eligible(first)||!mounted_look::eligible(second))return reject(Refusal::owner_ineligible);
    if(first!=second||first.cg!=pre.native_cg_identity||!read_pre_command_snapshot(&latest)||!same_pre(pre,latest))
        return reject(Refusal::owner_changed);
    out->facts=second;out->pre.epoch=pre.gameplay_epoch;out->pre.sequence=pre.sequence;
    out->pre.command.thread=c.native_thread;out->pre.observed_ms=pre.observed_ms;
    out->started_ms=now;out->finished_ms=GetTickCount64();
    return out->finished_ms>=now&&out->finished_ms-now<=100;
}
bool same_scene_owner(const Scene& a,const Scene& b) noexcept {
    return a.valid&&b.valid&&a.owner.pre.epoch==b.owner.pre.epoch&&
        a.owner.pre.command.thread==b.owner.pre.command.thread&&a.refdef==b.refdef&&
        a.rig.xr_generation==b.rig.xr_generation&&a.rig.anchor_generation==b.rig.anchor_generation&&
        same_mount(a.owner.facts,b.owner.facts);
}
bool current_scene(Kind kind,Scene* out,mounted_look::Copy* current) noexcept {
    if(!enabled.load(std::memory_order_acquire)||!out||!current)return false;
    AcquireSRWLockShared(&cache_lock);*out=scene;ReleaseSRWLockShared(&cache_lock);
    if(!out->valid)return reject(Refusal::scene_absent);
    const auto now=GetTickCount64();PreCommandSnapshot pre{};
    if(!read_pre_command_snapshot(&pre)||pre.gameplay_epoch!=out->owner.pre.epoch||
       !copy_owner(pre,current,kind!=Kind::visual)||
       !same_mount(current->facts,out->owner.facts)||!command_input_generation_current(out->rig))return reject(Refusal::scene_identity);
    xr::ActionSnapshot latest_actions{};input::CommandRigSample latest{};std::uint64_t generation{};
    input::CommandAimProposal check{};
    if(!read_vr_command_input_snapshot(&latest_actions,&generation,&latest)||
       generation!=out->rig.xr_generation||latest.xr_generation!=generation||
       input::propose_native_command_aim(latest,latest_actions,out->axis,xr::Hand::Right,now,&check,false)!=input::CommandAimStatus::accepted||
       aim::right_hand(out->rig,out->axis,now,&check)!=input::CommandAimStatus::accepted)return reject(Refusal::tracking);
    // A newer render publication can coexist with this server callback. Keep
    // the original copied rig/age, but reject any mount/epoch/generation change.
    AcquireSRWLockShared(&cache_lock);const bool same=same_scene_owner(scene,*out);ReleaseSRWLockShared(&cache_lock);
    return same;
}
struct Turret {
    std::uintptr_t entity{},client{},info{};
    std::array<float,3> base{},relative{},ps_angles{};
    aim::Angles minimum{},maximum{};
};
bool read_turret(const mounted_look::Facts& owner,Turret* out) noexcept {
    if(!out||owner.entity<0||owner.entity>=1023)return false;Turret t{};
    t.entity=entities+entity_stride*static_cast<unsigned>(owner.entity);
    std::uint32_t client{},info{},number{},player_number{},flags{},flags2{},pmflags{},weapflags{},viewlock{};
    std::int16_t mounted{};int health{},pm{};std::uint8_t local{},pose_type{},pose_local{};
    std::uint32_t centities{};
    float cap{};std::array<float,3> ground{};
    if(!get(client_entities_slot,&centities)||centities<0x10000||centities>UINT32_MAX-client_entity_stride*1024||
       !get(centities+client_entity_stride*static_cast<unsigned>(owner.entity)+2,&pose_type)||pose_type!=11||
       !get(centities+client_entity_stride*static_cast<unsigned>(owner.entity)+4,&pose_local)||pose_local||
       !get(entities,&player_number)||player_number||!get(entities+0x13c,&client)||client<0x10000||client>UINT32_MAX-0x500||
       !get(t.entity,&number)||number!=static_cast<unsigned>(owner.entity)||
       !get(t.entity+0x14c,&info)||info<0x10000||info>UINT32_MAX-0x100||
       !get(client+4,&pm)||(pm!=0&&pm!=1)||!get(client+0x1c4,&health)||health<=0||
       !get(client+0x130,&local)||local||!get(client+0xc,&pmflags)||(pmflags&0x800)||
       !get(client+0x10,&weapflags)||(weapflags&0x180)||
       !get(client+0xe0,&flags)||!(flags&0x300)||(flags&0x4000)||
       !get(client+0xe4,&flags2)||(flags2&0x40040000)||
       !get(client+0x440,&viewlock)||(viewlock!=1&&viewlock!=2)||
       !get(client+0x444,&mounted)||mounted!=owner.entity||
       !get(t.entity+0x128,&t.base)||!finite(t.base)||
       !get(t.entity+0x54,&t.relative)||!finite(t.relative)||
       !get(client+0x180,&t.ps_angles)||!finite(t.ps_angles)||
       !get(client+0x464,&ground)||!finite(ground)||ground[0]!=0||ground[1]!=0||ground[2]!=0||
       !get(info+0x34,&t.minimum)||!get(info+0x3c,&t.maximum)||
       !get(0xb77a58,&cap)||!std::isfinite(cap)||cap<=0||cap>90)return false;
    if(!aim::finite(t.minimum)||!aim::finite(t.maximum)||t.minimum.pitch>t.maximum.pitch||
       t.minimum.yaw>t.maximum.yaw||t.minimum.pitch < -180||t.maximum.pitch>180||
       t.minimum.yaw < -180||t.maximum.yaw>180)return false;
    // Native applies its pitch cap after the authored turret arc clamp. Do the
    // same to both arc endpoints, including mounts with an arc beyond the cap.
    t.minimum.pitch=std::clamp(t.minimum.pitch,-cap,cap);
    t.maximum.pitch=std::clamp(t.maximum.pitch,-cap,cap);
    t.client=client;t.info=info;*out=t;return true;
}
struct Patch {
    std::uintptr_t address{},turret{},client{};
    std::array<float,3> original{},angles{};
    std::uint32_t original_pointer{};
    std::uint64_t scene_serial{},epoch{};
    Scene source{};
    bool active{},pointer{};
};
bool begin(Kind kind,const std::uint32_t* saved,Patch* p) noexcept {
    NativeReadPreserve preserve;
    // These global native seams also service AI entities; refuse them before
    // reading scene/XR state or doing controller geometry.
    if(!p||!saved||static_cast<unsigned>(kind)>=3)return false;
    if(saved[9]!=consumer_returns[static_cast<unsigned>(kind)]||
       (kind==Kind::aim&&saved[7]!=entities)||
       (kind==Kind::fire&&(saved[10]!=entities||saved[11]!=0)))return reject(Refusal::consumer_identity);
    if(kind==Kind::visual){std::uint8_t using_player{},local{};
        if(!get(saved[1]+0x6c,&using_player)||using_player!=1||!get(saved[1]+4,&local)||local)return reject(Refusal::consumer_identity);
    }
    Scene s{};mounted_look::Copy current{};Turret t{};
    if(!current_scene(kind,&s,&current))return false;
    if(!read_turret(current.facts,&t))return reject(Refusal::turret);
    input::CommandAimProposal requested{};aim::ClampedAim proposal{};
    if(aim::right_hand(s.rig,s.axis,GetTickCount64(),&requested)!=input::CommandAimStatus::accepted||
       !aim::clamp({requested.pitch_degrees,requested.yaw_degrees},{t.base[0],t.base[1]},t.minimum,t.maximum,&proposal))return reject(Refusal::proposal);
    // pushad images: EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX,EFLAGS,return,args.
    if(kind==Kind::aim&&(saved[1]!=t.entity||saved[7]!=entities))return reject(Refusal::consumer_identity);
    if(kind==Kind::fire&&(saved[7]!=t.entity||saved[10]!=entities||saved[11]!=0))return reject(Refusal::consumer_identity);
    if(kind==Kind::visual){
        std::uint32_t centities{},view{};std::uint8_t type{},local{},using_player{};
        if(!get(client_entities_slot,&centities)||centities<0x10000||
           saved[1]!=centities+client_entity_stride*static_cast<unsigned>(current.facts.entity)||
           !get(saved[1]+2,&type)||type!=11||!get(saved[1]+4,&local)||local||
           !get(saved[1]+0x6c,&using_player)||using_player!=1||
           !get(saved[1]+0x60,&view)||view!=current.facts.ps+0x180)return reject(Refusal::consumer_identity);
        p->address=saved[1]+0x60;p->original_pointer=view;p->pointer=true;
    }else{
        p->address=t.client+0x180;p->original=t.ps_angles;
    }
    p->angles={proposal.world.pitch,proposal.world.yaw,0};
    if(kind==Kind::fire){
        NativeAim witnessed{};AcquireSRWLockShared(&cache_lock);witnessed=native_aim;ReleaseSRWLockShared(&cache_lock);
        const auto now=GetTickCount64();
        input::CommandAimProposal original{};
        if(!witnessed.valid||!same_scene_owner(witnessed.source,s)||witnessed.epoch!=current.pre.epoch||
           witnessed.turret!=t.entity||witnessed.client!=t.client||now<witnessed.observed_ms||now-witnessed.observed_ms>100||
           witnessed.relative!=t.relative||witnessed.base!=t.base||!command_input_generation_current(witnessed.source.rig)||
           aim::right_hand(witnessed.source.rig,witnessed.source.axis,now,&original)!=input::CommandAimStatus::accepted)return reject(Refusal::witness);
        // Shot direction comes from the exact native-clamped result, retaining
        // the stock muzzle origin, spread, heat and bullet/event paths.
        p->angles={std::remainder(t.base[0]+t.relative[0],360.f),
                   std::remainder(t.base[1]+t.relative[1],360.f),0};
    }
    p->turret=t.entity;p->client=t.client;p->scene_serial=s.serial;p->epoch=current.pre.epoch;
    p->source=s;
    if(p->pointer){
        const auto proposed=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p->angles.data()));
        p->active=put(p->address,proposed);
    }else p->active=put(p->address,p->angles);
    return p->active;
}
void finish(Kind kind,Patch& p,bool normal) noexcept {
    NativeReadPreserve preserve;
    if(!p.active)return;
    const bool restored=p.pointer?put(p.address,p.original_pointer):put(p.address,p.original);
    if(kind==Kind::aim){
        NativeAim result{};result.turret=p.turret;result.client=p.client;
        result.scene_serial=p.scene_serial;result.epoch=p.epoch;result.observed_ms=GetTickCount64();
        result.source=p.source;
        result.valid=normal&&restored&&get(p.turret+0x54,&result.relative)&&finite(result.relative)&&
            get(p.turret+0x128,&result.base)&&finite(result.base);
        AcquireSRWLockExclusive(&cache_lock);
        native_aim=same_scene_owner(scene,p.source)?result:NativeAim{};
        ReleaseSRWLockExclusive(&cache_lock);
    }
    if(normal&&restored){const auto index=static_cast<unsigned>(kind);const auto count=++accepted[index];
        if(count<=8||count%240==0)trace("t5_mounted_gun_aim","consumer=%u count=%llu entity=%lu pitch=%.5f yaw=%.5f scoped_restore=1 camera_writes=0 native_once=1",
            index,count,static_cast<unsigned long>((p.turret-entities)/entity_stride),p.angles[0],p.angles[1]);
    }
}

// Bridge definitions follow the existing consumed-command full-state bridge.
// Each invoker restores native incoming GP/flags/FP state, copies only its ABI
// stack arguments, calls native exactly once, and returns every native output.
void __cdecl invoke_aim(std::uint32_t*,void*);
void __cdecl invoke_fire(std::uint32_t*,void*);
void __cdecl invoke_visual(std::uint32_t*,void*);
void note_entry(unsigned kind,const std::uint32_t* saved) noexcept {
    NativeReadPreserve preserve;
    const auto count=++consumer_entries[kind];
    if(count<=3||count%600==0)trace("t5_mounted_gun_consumer_entry",
        "consumer=%u count=%llu thread=%lu caller=0x%x eax=0x%x esi=0x%x args=0x%x,0x%x",
        kind,count,GetCurrentThreadId(),saved[9],saved[7],saved[1],saved[10],saved[11]);
}
void __cdecl dispatch(std::uint32_t* saved,void* fx,unsigned raw_kind) {
    const auto kind=static_cast<Kind>(raw_kind);Patch patch{};
    const bool outer=depth++==0;
    if(outer){diagnostic_kind=raw_kind;diagnostic_saved=saved;
        note_entry(raw_kind,saved);
        begin(kind,saved,&patch);
    }
    __try {
        if(kind==Kind::aim)invoke_aim(saved,fx);
        else if(kind==Kind::fire)invoke_fire(saved,fx);
        else invoke_visual(saved,fx);
    }__finally {finish(kind,patch,AbnormalTermination()==FALSE);--depth;
        if(outer){diagnostic_kind=3;diagnostic_saved=nullptr;}}
}

// Full-state bridge implementations are kept in a local include so the assembly
// harness can exercise exactly the shipped invokers with authored native stubs.
#include "t5/mounted_gun_bridges.inc"

struct Site {std::uintptr_t address{};void* bridge{};std::array<std::uint8_t,5> original{},owned{};bool installed{};};
Site sites[]{{0x81c79d,reinterpret_cast<void*>(&aim_bridge)},
             {0x819cba,reinterpret_cast<void*>(&fire_bridge)},
             {0x5c7ecc,reinterpret_cast<void*>(&visual_bridge)}};
constexpr native_guard::Span guard_spans[]{
    // Sys_IsServerThread / Sys_IsMainThread pin the two native thread slots.
    {0x55b420,62,"9656e8b25b2cf79a6a62be1dd15e0f59c8d0eb3593aa8c4900c12175d419ee52"},
    {0x5a48f0,62,"6b118ebbf0c3223f4aec2cce836a7533a9ec4d5aff2a06ddf762a09491551449"},
    {0x81c540,586,"695d0c242a80de50ec2d40bb09313d7962796151c47806bd40f1921970076db7"},
    {0x81c790,210,"096b19797959185a8ea4ec9053e220a94923d3dd9d5affec1fe2aec63bb371d5"},
    {0x819970,753,"6747f4176673539354c849eb4db38d53f7af95dda185c7f1de5e658783cf0f5c"},
    {0x819c70,281,"ed985bff0a885f89db2a8778a8a2b64c8bdd3c2062a2d4a124805704a85e51a0"},
    {0x8196a0,448,"a004fec407767faf6896dfc52fe68ac160137d55120bb6753dcdd4fff56f4823"},
    {0x782270,368,"a19359675ea7ec5711d4c82c06f09b3b38752c86a76242743dd0eb182e6894b3"},
    {0x5c7dc0,386,"e94197a47011416f61a1797424bf18541c021da3cff3bbd4858b8b13ad0c2264"},
    {0x5bebb0,19,"b207578140bc695cffe07f0ec9ab1675d9d5e291c5dc53aef504345ff16a7077"},
    {0x4ae7f0,299,"9018f1167e0ab595cce7860a206df57d5eefc7a2767287b0b7a63bc4dd3d30be"},
};
bool guards_match() noexcept {
    std::array<std::uint8_t,1024> bytes{};
    for(const auto& guard:guard_spans){
        if(!mounted_look::protected_copy(nullptr,guard.address,bytes.data(),guard.size))return false;
        for(const auto& site:sites)if(site.installed&&site.address>=guard.address&&site.address+5<=guard.address+guard.size){
            auto* at=bytes.data()+site.address-guard.address;
            if(std::memcmp(at,site.owned.data(),5))return false;
            std::memcpy(at,site.original.data(),5);
        }
        if(!native_guard::matches(bytes.data(),guard.size,guard.sha256))return false;
    }
    return true;
}
struct Peers {
    std::array<HANDLE,512> handles{};std::size_t count{},suspended{};
    ~Peers(){resume();for(std::size_t i=0;i<count;++i)CloseHandle(handles[i]);}
    void resume()noexcept{while(suspended)ResumeThread(handles[--suspended]);}
    bool acquire()noexcept{
        const auto process=GetCurrentProcessId(),self=GetCurrentThreadId();
        const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE)return false;
        THREADENTRY32 entry{};entry.dwSize=sizeof entry;bool okay=Thread32First(snapshot,&entry)!=FALSE;
        if(okay)do{if(entry.th32OwnerProcessID!=process||entry.th32ThreadID==self)continue;
            if(count==handles.size()){okay=false;break;}
            const auto h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|SYNCHRONIZE,FALSE,entry.th32ThreadID);
            if(!h){if(GetLastError()==ERROR_INVALID_PARAMETER)continue;okay=false;break;}handles[count++]=h;
        }while(Thread32Next(snapshot,&entry));
        if(okay&&GetLastError()!=ERROR_NO_MORE_FILES)okay=false;CloseHandle(snapshot);if(!okay)return false;
        for(std::size_t i=0;i<count;++i){if(SuspendThread(handles[i])==DWORD(-1))return false;++suspended;
            CONTEXT c{};c.ContextFlags=CONTEXT_CONTROL;if(!GetThreadContext(handles[i],&c))return false;
            for(const auto& site:sites)if(c.Eip>=site.address&&c.Eip<site.address+5)return false;
        }return true;
    }
};
bool write_site(Site& site,const std::array<std::uint8_t,5>& data)noexcept{
    auto* at=reinterpret_cast<void*>(site.address);DWORD previous{},ignored{};
    if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&previous))return false;
    const bool wrote=put(site.address,data);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),at,5)!=FALSE;
    const bool protected_again=VirtualProtect(at,5,previous,&ignored)!=FALSE;
    std::array<std::uint8_t,5> after{};
    return wrote&&flushed&&protected_again&&get(site.address,&after)&&after==data;
}
bool install() {
    if(enabled.load())return guards_match();
    if(!guards_match())return false;
    // Source is deliberately noncopyable; construct directly from its factory
    // prvalue so the startup native-thread owner survives for the hook lifetime.
    owner_source.reset(new mounted_look::Source(mounted_look::make_source(true)));
    if(owner_source->configuration()!=mounted_look::Status::accepted)return false;
    Peers peers;if(!peers.acquire()||!guards_match())return false;
    for(auto& site:sites){
        if(!get(site.address,&site.original)||site.original[0]!=0xe8)return false;
        site.owned=site.original;
        const auto d=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(site.bridge)-(site.address+5));
        std::memcpy(site.owned.data()+1,&d,4);
    }
    bool okay=true;
    for(auto& site:sites){site.installed=true;if(!write_site(site,site.owned)){okay=false;break;}}
    if(!okay){for(auto& site:sites)if(site.installed&&write_site(site,site.original))site.installed=false;return false;}
    enabled.store(true,std::memory_order_release);peers.resume();return true;
}
} // namespace mounted_gun_runtime_detail

bool configure_mounted_gun_runtime(HMODULE executable,bool requested) noexcept {
    NativeReadPreserve preserve;using namespace mounted_gun_runtime_detail;
    if(!requested)return false;
    AcquireSRWLockExclusive(&install_lock);bool okay{};
    try{std::string reason;okay=reinterpret_cast<std::uintptr_t>(executable)==0x400000&&
        validate_path(executable_path()).accepted&&validate_loaded_headers(executable,reason)&&install();}
    catch(...){okay=false;}
    ReleaseSRWLockExclusive(&install_lock);
    trace("t5_mounted_gun_configured","accepted=%u consumers=3 scoped_player_angles=1 visual_pose_pointer=1 camera_writes=0",okay);
    return okay;
}
void revoke_mounted_gun_scene() noexcept {
    using namespace mounted_gun_runtime_detail;AcquireSRWLockExclusive(&cache_lock);
    scene={};native_aim={};ReleaseSRWLockExclusive(&cache_lock);
}
void publish_mounted_gun_scene(const input::CommandRigSample& rig,const wawvr::xr::Vec3f& origin,
    const wawvr::xr::Basis3f& axis,const PreCommandSnapshot& pre,std::uintptr_t refdef) noexcept {
    NativeReadPreserve preserve;using namespace mounted_gun_runtime_detail;
    if(!enabled.load(std::memory_order_acquire))return;
    if(!pre.native_gameplay_fields_valid||pre.gameplay_active||!(pre.native_entity_flags&0x300)||
       (pre.native_entity_flags&0x4000)){revoke_mounted_gun_scene();return;}
    mounted_look::Copy owner{};input::CommandAimProposal proposal{};
    if(!refdef||pre.context.native_thread!=GetCurrentThreadId()||!copy_owner(pre,&owner)||refdef!=owner.facts.cg+0x8c100||
       !std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z)||
       aim::right_hand(rig,axis,GetTickCount64(),&proposal)!=input::CommandAimStatus::accepted){reject(Refusal::publication);revoke_mounted_gun_scene();return;}
    AcquireSRWLockExclusive(&cache_lock);
    if(next_serial==UINT64_MAX){scene={};native_aim={};}
    else scene={rig,origin,axis,owner,refdef,++next_serial,true};
    ReleaseSRWLockExclusive(&cache_lock);
}
} // namespace bovr::t5
