// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/mounted_look_context.hpp"
#include "t5/native_camera_mode.hpp"
#include <cstring>
#include <limits>

namespace bovr::t5::mounted_look {
namespace {
using U32=std::uint32_t;
struct Failure {Status value;};
[[noreturn]] void fail(Status s){throw Failure{s};}
constexpr std::array<U32,7> camera_offsets{0x8a360,0xc,0xa9d44,0x1c,0x14,0xa4780,0xa4680};
constexpr std::array<U32,4> dvar_slots{0xbcd150,0xbcd1b8,0xbcaf04,0xbcaebc};
constexpr const char* dvar_names[]{"player_topDownCamMode","player_viewLockEnt","player_view_pitch_up","player_view_pitch_down"};
constexpr std::array<U32,4> dvar_types{5,5,1,1};
#if defined(BOVR_TEST_NATIVE_GUARDS)
// Authored zero-filled spans retain boundary/mutation coverage in the isolated
// test library without copying retail instructions into distributable tests.
constexpr native_guard::Span guards[]{
    {0x005a2810,378,"8d8c294831ae2b1dd67f6a4903ae6eb38cdaede56707bf6508e70b1e4acbb43e"},
    {0x0075f3e0,332,"f18e4c8388d95251d22fdb63456987c14f67d4027576801db5bcb985dc061565"},
    {0x0075f300,218,"ed9949015fee6260563cea958f1f6fcd9be0da8d2b0a03c52b0faef1a22e7df1"},
    {0x004cecd0,407,"f5d3f822a8435f91f7a5d54b720aa637f8b8f8102c7670d1b52d98f2d0123beb"},
    {0x00623b60,623,"0e001ba37a9757becef02f051bbcf27e71712e6f543f5fc1ab1e2562ac437dec"},
    {0x00793fb0,1357,"f7d4a7a24961c7cfdbe9557c6b22895bc006b4617c8034815b907f3fa9b521ac"},
    {0x007923f0,557,"58bd661ff1a892697366215a8938d1c616cb4523e1ede78b49d155b132430917"},
    {0x0045bdd0,940,"ed4d01b36d154179c85e5c07aa207d8dae0daef80a28ee9d3c410cdaa507cc2c"},
    {0x00651350,321,"838f48e7795289411a2511787e131da15058f7df2745cffd3c9a3f4210aea7f1"},
    {0x0081c540,586,"fcfe9ae45cd7a2562a82135eec5742b6025b7017f29f2566cfa106d6b687032e"},
    {0x0069d450,327,"8166412c71a3a284a1786a39932e467da95d90d7fd14971eef0a1f55e16a97d4"},
    {0x0054d490,38,"762b023699a0e48aa95763f0cf7c0467f1d6e9880308c78ebbc1c423de7072d3"},
    {0x0065e787,135,"41a9a42d8072d429502781344a47e9c041ccfcd5455d014064c9227ae54e0187"},
    {0x00660878,42,"094c4931fdb2f2af417c9e0322a9716006e8211fe9017f671ac6e3251300acca"},
    {0x00679020,116,"5b517952cbe9c4c147bc3f3434f9d82409e76d09ea58905aefe7fb5415912d9a"},
    {0x00651910,104,"39f37f8d1931b3bdf767e7510dd69509fbf23af1f7654933d0a4d291cbdd4418"},
};
#else
// Complete consumer/publication guards and aligned registration fragments from
// mounted-look-predicate-review-build/proof.json, exact SP db00603d...73ff3.
constexpr native_guard::Span guards[]{
    {0x005a2810, 378, "5b91bd2b8f330f28a838344165f66c1476deba6afd289e8490a2c38c7cdeaeb2"}, // pm_update_view_angles
    {0x0075f3e0, 332, "098828ce30e394e00fc7920fee2a46d159f509370f437b8042a0931cb1aa4507"}, // general_pitch_clamp
    {0x0075f300, 218, "0fcaa1cebd95e5b74e1fcf5316641ab1cbb6b9e647afbb90427716f76853abd2"}, // forced_view_lock
    {0x004cecd0, 407, "f2c8f65cd6d48465df5a7aebb0dcd1f700be9e05d02eac915f908e0589d7ee68"}, // mounted_linked_range_clamp
    {0x00623b60, 623, "b856b7576361fd4843925dfd71b69cee07a1e60089d70e2b98643f6bb1587a3e"}, // camera_selector
    {0x00793fb0, 1357, "4120712773b65efa80703cb35238f870a8e2b8c70ab87fd1afceeef03117e421"}, // calc_view_values
    {0x007923f0, 557, "c5ea1de4fb9b204cfa648e1c75a7461577e872f40f38391164176ecbe39f4263"}, // turret_camera
    {0x0045bdd0, 940, "5707ec2f42331082e0fabec8fe0998778daac617c9db9a7bceecf1f554b3a348"}, // turret_entry
    {0x00651350, 321, "04ed8c4050bac9e8b5c61ddc62b34200ab9d0f4e2d0c5c24b37a63f6dcaf7d74"}, // turret_exit
    {0x0081c540, 586, "695d0c242a80de50ec2d40bb09313d7962796151c47806bd40f1921970076db7"}, // turret_view_lock_update
    {0x0069d450, 327, "dfb221855bbcdba40a3205e4817505b4823690b64f4d3da95abae83237a12c94"}, // client_think_pm_assignment
    {0x0054d490, 38, "d00c4f7c051b338eafb40a08030eccf68cce12475faca69d9858985ff874a1fe"}, // vehicle_weapon_exclusion
    {0x0065e787, 135, "cbdd6758496f528bef4936139dc9ff68258a03b41f90b6f40044d8f2ef7c0ec2"}, // pitch_dvar_registration
    {0x00660878, 42, "d304928310c300cfd32fe5c14160e6703343e1872c29c6064750a985dda30151"}, // viewlock_dvar_registration
    {0x00679020, 116, "1be7952339dec365be56422ad98bb386ca9a436592594d040207a43605fdcc87"}, // register_float_type
    {0x00651910, 104, "7a4b467a962fc5bd850346724dbeb4be64c8517c2f9d1ea85c4ae396989bfe7b"}, // register_int_type
};
#endif
bool guard_bytes(const Host& h) noexcept {
    __try {
        std::array<unsigned char,1536> bytes{};
        for(const auto& guard:guards){
            if(guard.size>bytes.size()||!h.copy(h.context,guard.address,bytes.data(),guard.size)||
               native_guard::compare(bytes.data(),guard.size,guard.sha256))return false;
        }
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct Read {
    const Host& h;
    static U32 address(std::uintptr_t base,std::size_t offset,std::size_t size) {
        if(base<0x10000||base>UINT32_MAX||offset>UINT32_MAX||!size||size>UINT32_MAX)fail(Status::storage);
        const auto at=std::uint64_t(base)+offset;
        if(at>UINT32_MAX||size>UINT32_MAX-at+1)fail(Status::storage);
        return static_cast<U32>(at);
    }
    void bytes(std::uintptr_t base,std::size_t offset,void* out,std::size_t size)const {
        if(!out||!h.copy(h.context,address(base,offset,size),out,size))fail(Status::storage);
    }
    template<class T>T value(std::uintptr_t base,std::size_t offset=0)const {
        T out{};bytes(base,offset,&out,sizeof(out));return out;
    }
};
CommandKey command_key(const CommandContext& c) noexcept {
    CommandKey key{c.local_client,c.command_number,c.key_catcher,c.connection_state,c.native_frame_time,
                   c.native_thread,c.native_override,c.valid,{}};
    std::memcpy(key.angle_bits.data(),c.viewangles,sizeof(c.viewangles));
    std::memcpy(key.angle_bits.data()+3,c.kickangles,sizeof(c.kickangles));return key;
}
bool finite(U32 bits)noexcept{return (bits&0x7f800000U)!=0x7f800000U;}
bool positive_pitch(U32 bits)noexcept{return bits>0&&bits<=0x42b40000U;} // (0,90], raw IEEE positive order
bool range(U32 bits)noexcept{return (bits&0x7fffffffU)==0||bits<=0x43340000U;} // +/-0 or [0,180]
bool valid_request(const Request& q)noexcept {
    const auto& c=q.command;
    if(!c.valid||c.local_client||!c.native_thread||c.command_number<0||c.native_frame_time<0||
       c.native_override||c.key_catcher||c.connection_state<9||!q.pre_sequence||!q.gameplay_epoch)return false;
    const auto k=command_key(c);for(auto word:k.angle_bits)if(!finite(word))return false;return true;
}
PreKey pre(const Host& h,const Request& q,DWORD owner,std::uint64_t now) {
    if(!now||h.thread(h.context)!=owner)fail(Status::owner);
    PreCommandSnapshot s{};
    if(!h.command(h.context,&s))fail(Status::command);
    PreKey key{command_key(s.context),s.sequence,s.observed_ms,s.gameplay_epoch,s.native_cg_identity,
               s.native_game_time,s.native_health,s.native_pm_type,s.native_pm_flags,s.native_weapon_flags,
               s.native_entity_flags,s.native_entity_flags2,s.native_gameplay_fields_valid,s.gameplay_active};
    if(key.command!=command_key(q.command)||key.sequence!=q.pre_sequence||key.epoch!=q.gameplay_epoch||
       !key.fields_valid||key.ordinary_active||!key.observed_ms||now<key.observed_ms||now-key.observed_ms>100)
        fail(Status::command);
    return key;
}
Facts facts(const Read& r) {
    Facts f{};f.cg=r.value<U32>(0x2ff5354);f.ps=Read::address(f.cg,0x8a364,0x446);
    f.game_time=r.value<int>(f.cg,0x8a348);f.health=r.value<int>(f.ps,0x1c4);f.pm_type=r.value<int>(f.ps,4);
    f.local=r.value<std::uint8_t>(f.ps,0x130);f.pm_flags=r.value<U32>(f.ps,0xc);f.weapon_flags=r.value<U32>(f.ps,0x10);
    f.entity_flags=r.value<U32>(f.ps,0xe0);f.entity_flags2=r.value<U32>(f.ps,0xe4);
    f.viewlock=r.value<U32>(f.ps,0x440);f.entity=r.value<std::int16_t>(f.ps,0x444);
    for(std::size_t i=0;i<camera_offsets.size();++i)f.camera[i]=r.value<int>(f.cg,camera_offsets[i]);
    if(f.camera[2]&&!copy_scoped_tow(r.h,f.entity,&f.scoped_tow))fail(Status::ineligible);
    f.catcher=r.value<int>(0x2910160);f.connection=r.value<int>(0x2910164);f.override_value=r.value<int>(0x2911e2c);
    f.command_number=r.value<int>(0x2993850);f.command_clock=r.value<int>(0x2481764);
    r.bytes(f.ps,0x180,f.view_bits.data(),sizeof(f.view_bits));r.bytes(f.ps,0x84,f.delta_bits.data(),sizeof(f.delta_bits));
    r.bytes(f.ps,0x1a0,f.clamp_bits.data(),sizeof(f.clamp_bits));
    for(std::size_t i=0;i<f.dvars.size();++i){auto& d=f.dvars[i];d.address=r.value<U32>(dvar_slots[i]);
        d.name_address=r.value<U32>(d.address);d.type=r.value<U32>(d.address,0x10);d.value=r.value<U32>(d.address,0x18);
        const auto size=std::strlen(dvar_names[i])+1;r.bytes(d.name_address,0,d.name.data(),size);
    }
    return f;
}
bool matches(const Facts& f,const PreKey& p)noexcept {
    return f.cg==p.cg&&f.game_time==p.game_time&&f.health==p.health&&f.pm_type==p.pm_type&&
        f.pm_flags==p.pm_flags&&f.weapon_flags==p.weapon_flags&&f.entity_flags==p.entity_flags&&f.entity_flags2==p.entity_flags2&&
        f.command_number==p.command.number&&f.command_clock==p.command.clock&&f.catcher==p.command.catcher&&
        f.connection==p.command.connection&&f.override_value==0;
}
bool native_command(void*,PreCommandSnapshot* p)noexcept{return read_pre_command_snapshot(p);}
DWORD native_thread(void*)noexcept{return GetCurrentThreadId();}
std::uint64_t native_now(void*)noexcept{return GetTickCount64();}
} // namespace

bool scoped_tow_eligible(const ScopedTow& s,std::int16_t entity) noexcept {
    constexpr char expected[]="m220_tow_emplaced_khesanh_sp";
    static_assert(sizeof expected==sizeof s.internal_name);
    return entity>=0&&entity<1023&&s.client_entities>=0x10000&&s.client_entities<=UINT32_MAX-0x31c*1024&&
        s.client_entity==s.client_entities+static_cast<U32>(entity)*0x31c&&
        s.server_entity==0x1a796f8+static_cast<U32>(entity)*0x34c&&
        s.number==static_cast<U32>(entity)&&s.client_number==s.number&&s.type==11&&s.pose_type==11&&
        !s.pose_local&&s.inuse==1&&s.weapon&&s.weapon==s.client_weapon&&
        s.variant>=0x10000&&s.variant<=UINT32_MAX-0x90&&s.definition>=0x10000&&s.definition<=UINT32_MAX-0x24&&
        s.name>=0x10000&&s.name<=UINT32_MAX-sizeof expected&&s.overlay>=0x10000&&
        s.weapon_type==2&&s.weapon_class==7&&!std::memcmp(s.internal_name.data(),expected,sizeof expected);
}
bool copy_scoped_tow(const Host& h,std::int16_t entity,ScopedTow* out) noexcept {
    if(!out)return false;*out={};if(!h.copy||entity<0||entity>=1023)return false;
    try {
        const Read r{h};ScopedTow s{};
        s.client_entities=r.value<U32>(0x2ff6684);
        if(s.client_entities<0x10000||s.client_entities>UINT32_MAX-0x31c*1024)return false;
        s.client_entity=s.client_entities+static_cast<U32>(entity)*0x31c;
        s.server_entity=0x1a796f8+static_cast<U32>(entity)*0x34c;
        s.pose_type=r.value<std::uint8_t>(s.client_entity,2);s.pose_local=r.value<std::uint8_t>(s.client_entity,4);
        // Retail turret-camera consumer 0x790BE0 reads nextState.number at
        // centity+164 and nextState.weapon at +238 (entityState weapon +D4).
        s.client_number=r.value<U32>(s.client_entity,0x164);s.client_weapon=r.value<std::uint8_t>(s.client_entity,0x238);
        s.number=r.value<U32>(s.server_entity);s.type=r.value<std::uint16_t>(s.server_entity,0xbe);
        s.inuse=r.value<std::uint8_t>(s.server_entity,0xdd);s.weapon=r.value<std::uint8_t>(s.server_entity,0xd4);
        if(!s.weapon||s.weapon!=s.client_weapon)return false;
        s.variant=r.value<U32>(0xbe19a8,4u*s.weapon);s.name=r.value<U32>(s.variant);
        s.definition=r.value<U32>(s.variant,8);s.overlay=r.value<U32>(s.variant,0x8c);
        s.weapon_type=r.value<int>(s.definition,0x1c);s.weapon_class=r.value<int>(s.definition,0x20);
        r.bytes(s.name,0,s.internal_name.data(),s.internal_name.size());
        if(!scoped_tow_eligible(s,entity))return false;*out=s;return true;
    }catch(...){return false;}
}
bool eligible(const Facts& f) noexcept {
    if(f.cg<0x10000||std::uint64_t(f.cg)+0x8a364!=f.ps||f.local||f.game_time<=0||f.health<=0||
       (f.pm_type!=0&&f.pm_type!=1)||(f.pm_flags&0x800)||(f.weapon_flags&0x180)||
       !(f.entity_flags&0x300)||(f.entity_flags&0x4000)||(f.entity_flags2&0x40040000)||
       (f.viewlock!=1&&f.viewlock!=2)||f.entity<0||f.entity>=1023||f.catcher||f.connection<9||
       f.override_value||f.command_number<0||f.command_clock<0)return false;
    if(!native_first_person_camera(f.camera[0]))return false;
    if(f.camera[1]||f.camera[3])return false;
    if(f.camera[2]&&(f.camera[2]!=1||!scoped_tow_eligible(f.scoped_tow,f.entity)))return false;
    if(f.camera[4]==3||f.camera[5]||f.camera[6]!=5)return false;
    for(auto word:f.view_bits)if(!finite(word))return false;
    for(auto word:f.delta_bits)if(!finite(word))return false;
    if(!finite(f.clamp_bits[0])||!finite(f.clamp_bits[1])||!range(f.clamp_bits[2])||!range(f.clamp_bits[3]))return false;
    for(std::size_t i=0;i<f.dvars.size();++i){const auto& d=f.dvars[i];
        if(d.address<0x10000||d.name_address<0x10000||d.type!=dvar_types[i]||
           std::memcmp(d.name.data(),dvar_names[i],std::strlen(dvar_names[i])+1))return false;
        if(i<2?d.value!=0:!positive_pitch(d.value))return false;
    }
    return true;
}
bool same_current(const Copy& a,const Copy& b) noexcept {
    return a.source_identity&&a.source_identity==b.source_identity&&a.revision&&a.revision==b.revision&&a.pre.sequence&&a.pre.epoch&&a.pre==b.pre&&
        eligible(a.facts)&&eligible(b.facts)&&a.facts==b.facts&&matches(a.facts,a.pre)&&matches(b.facts,b.pre);
}
bool protected_copy(void*,std::uintptr_t address,void* out,std::size_t size) noexcept {
    if(!out||!size||address<0x10000||address>UINT32_MAX||size>UINT32_MAX||size>UINT32_MAX-address+1)return false;
    __try{std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
Host production_host() noexcept{return {nullptr,protected_copy,native_command,native_thread,native_now};}
Source make_source(bool requested,const Host& host) noexcept{return Source(requested,host);}
Source::Source(bool requested,const Host& host) noexcept {
    if(!requested)return;
    if(!host.copy||!host.command||!host.thread||!host.now){configuration_=Status::arguments;return;}
    host_=host;
    __try {
        owner_=host_.thread(host_.context);
        configuration_=owner_&&guard_bytes(host_)&&host_.thread(host_.context)==owner_?Status::accepted:Status::guards;
    }__except(EXCEPTION_EXECUTE_HANDLER){configuration_=Status::guards;}
}
void Source::revoke() noexcept {
    auto value=revision_.load();while(value!=UINT64_MAX&&!revision_.compare_exchange_weak(value,value+1)){}
}
bool Source::current(std::uint64_t revision)const noexcept {
    return configuration_==Status::accepted&&revision&&revision!=UINT64_MAX&&revision_.load()==revision;
}
Status Source::body(const Request& q,Copy* output,bool& entered) noexcept {
    try {
        if(host_.thread(host_.context)!=owner_||q.command.native_thread!=owner_){revoke();return Status::owner;}
        if(running_.test_and_set()){revoke();return Status::busy;}entered=true;
        const auto revision=revision_.load();if(!current(revision))return Status::changed;
        const auto started=host_.now(host_.context);if(!started)return Status::stale;
        const auto first=pre(host_,q,owner_,started);const Read r{host_};
        const auto a=facts(r);if(!eligible(a)||!matches(a,first))return Status::ineligible;
        if(!current(revision))return Status::changed;
        const auto middle_time=host_.now(host_.context);if(middle_time<started)return Status::stale;
        const auto middle=pre(host_,q,owner_,middle_time);if(middle!=first)return Status::changed;
        const auto b=facts(r);if(a!=b)return Status::changed;
        const auto finished=host_.now(host_.context);
        if(finished<middle_time||finished-started>100)return Status::stale;
        const auto last=pre(host_,q,owner_,finished);
        if(last!=first||host_.thread(host_.context)!=owner_||!current(revision))return Status::changed;
        *output={last,b,reinterpret_cast<std::uintptr_t>(this),revision,started,finished};return Status::accepted;
    }catch(const Failure& e){return e.value;}
    catch(...){return Status::exception;}
}
Status Source::read(const Request& q,Copy* output) noexcept {
    if(!output){revoke();return Status::arguments;}*output={};
    if(configuration_!=Status::accepted)return configuration_;
    if(!valid_request(q)){revoke();return Status::arguments;}
    const auto error=GetLastError();bool entered=false;Status result=Status::exception;
    __try{result=body(q,output,entered);}
    __except(EXCEPTION_EXECUTE_HANDLER){result=Status::exception;}
    if(entered){if(result!=Status::accepted)revoke();running_.clear();}
    if(result!=Status::accepted)*output={};SetLastError(error);return result;
}
} // namespace bovr::t5::mounted_look
