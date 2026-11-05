// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/weapon_profile_reader.hpp"
#include "t5/native_camera_mode.hpp"
#include "t5/native_weapon_tracking_state.hpp"
#include "t5/borrowed_profile_shape.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace bovr::t5::profile_detail {
namespace {
using namespace input;
using U8=std::uint8_t;
using U16=std::uint16_t;
using U32=std::uint32_t;
using Addr=std::uintptr_t;
struct ReadFailure {};
struct CountFailure {};
struct ChangedFailure {};
struct Reader {
    const ReadHost& host;
    static Addr address(Addr base,std::size_t offset,std::size_t size) {
        const auto start=static_cast<std::uint64_t>(base)+offset;
        if(base<0x10000||start>UINT32_MAX||size>UINT32_MAX||size>UINT32_MAX-start+1)throw ReadFailure{};
        return static_cast<Addr>(start);
    }
    void bytes(Addr base,std::size_t offset,void* out,std::size_t n) const {
        if(!n)return;
        if(!out||!host.copy(host.context,address(base,offset,n),out,n))throw ReadFailure{};
    }
    template<class T>T value(Addr base,std::size_t offset=0) const {
        T v{};bytes(base,offset,&v,sizeof(v));return v;
    }
    ProfileTextCopy text(Addr pointer) const {
        ProfileTextCopy t{};
        for(unsigned i=0;i<=profile_max_name_bytes;++i) {
            const auto ch=value<U8>(pointer,i);
            if(!ch){t.readable=t.terminated=true;return t;}
            if(i==profile_max_name_bytes)return t;
            t.bytes.push_back(static_cast<char>(ch));
        }
        return t;
    }
};
// Exists only on the stack during this one read. These pointers are compared,
// never serialized or retained in observer state. Refcounts are deliberately
// absent: a positive count can change without changing the copied asset.
struct ModelStorage {
    Addr model{},name{},names{},parents{},bind{};
    U8 count{},roots{};
    friend bool operator==(const ModelStorage&,const ModelStorage&)=default;
};
struct BorrowStorage {
    Addr cg{},owner{},dobj{},variant{},definition{},server{},table{},arena{};
    primary_weapon_tracking::Mode client_mode{},server_mode{};
    U32 server_link{};
    short pool{};
    U16 tag{},duplicates{};
    U8 model_count{},bone_count{},duplicate_size{};
    U32 dual_weapon{};
    Addr dual_variant{},dual_definition{},dual_model_table{},dual_gun_model{};
    int fire_type{},dual_fire_type{},dual_class{};
    U32 stack_count{};
    std::array<ModelStorage,profile_max_models> models{};
    friend bool operator==(const BorrowStorage&,const BorrowStorage&)=default;
};
bool command_fresh(const NativeWeaponContext& c,const PreCommandSnapshot& s,std::uint64_t now) noexcept {
    const auto& p=s.context;
    return p.valid&&s.native_gameplay_fields_valid&&primary_weapon_active(s)&&s.gameplay_epoch&&s.sequence&&s.observed_ms&&
        p.local_client==0&&p.native_thread==c.native_thread&&s.native_cg_identity==c.cg_identity&&
        !p.native_override&&!p.key_catcher&&p.connection_state>=9&&p.native_frame_time==c.command_clock&&
        c.command_number>=p.command_number&&static_cast<std::int64_t>(c.command_number)-p.command_number<=1&&
        now>=s.observed_ms&&now-s.observed_ms<=profile_max_copy_age_ms;
}
void current_context(const Reader& r,const NativeWeaponContext& c,BorrowStorage& b) {
    b.cg=r.value<Addr>(0x2ff5354);b.owner=r.value<Addr>(0xc1c6d8);
    if(b.cg!=c.cg_identity||b.owner!=c.viewmodel_identity||!b.owner)throw ChangedFailure{};
    b.dobj=r.value<Addr>(b.owner);b.pool=r.value<short>(0x24e6150,1536*2);
    if(b.dobj!=c.dobj_identity||b.pool<=0||b.pool>=0xc00||
       b.dobj!=0x2489148+static_cast<unsigned>(b.pool)*0x7c||!r.value<U8>(0x2488548,b.pool))throw ChangedFailure{};
    b.variant=r.value<Addr>(0xbe19a8,c.weapon_index*4);
    if(b.variant!=c.variant_identity||!b.variant)throw ChangedFailure{};
    b.definition=r.value<Addr>(b.variant,8);if(b.definition!=c.definition_identity)throw ChangedFailure{};
    const auto dual=r.value<U8>(b.definition,0x56a);b.fire_type=r.value<int>(b.definition,0x30);
    if(r.value<int>(b.definition,0x1c)!=c.weapon_type||r.value<int>(b.definition,0x20)!=c.weapon_class||
       dual>1||(dual!=0)!=c.dual_wield||b.fire_type<0||b.fire_type>6)throw ChangedFailure{};
    if(c.dual_wield){
        b.dual_weapon=r.value<U32>(b.definition,0x594);
        if(!b.dual_weapon||b.dual_weapon>=256||b.dual_weapon==c.weapon_index)throw ChangedFailure{};
        b.dual_variant=r.value<Addr>(0xbe19a8,4*b.dual_weapon);b.dual_definition=r.value<Addr>(b.dual_variant,8);
        b.dual_fire_type=r.value<int>(b.dual_definition,0x30);b.dual_class=r.value<int>(b.dual_definition,0x20);
        const auto dual_type=r.value<int>(b.dual_definition,0x1c);
        if(dual_type!=c.weapon_type||!native_dual_weapon_tracking_type(dual_type,b.dual_class)||r.value<U8>(b.dual_definition,0x56a)!=1||
           r.value<U32>(b.dual_definition,0x594)!=c.weapon_index||b.dual_class<0||b.dual_class==9||
           b.dual_fire_type<0||b.dual_fire_type>6)throw ChangedFailure{};
        b.dual_model_table=r.value<Addr>(b.dual_definition,4);b.dual_gun_model=r.value<Addr>(b.dual_model_table);
        if(!b.dual_gun_model)throw ChangedFailure{};
    }
    const auto ps=Reader::address(b.cg,0x8a364,0x1c8);
    b.client_mode=primary_weapon_tracking::client_mode(
        {r.value<int>(ps,4),r.value<int>(ps,0x1c4),r.value<U32>(ps,0xc),r.value<U32>(ps,0x10),r.value<U32>(ps,0xe0),r.value<U32>(ps,0xe4)},
        {r.value<int>(b.cg,0x8a360),r.value<int>(0x2910160),r.value<int>(0x2910164),
         r.value<int>(0x2911e2c)!=0,r.value<int>(b.cg,0xc)!=0,r.value<int>(b.cg,0xa9d44)!=0});
    b.stack_count=r.value<U32>(ps,0x1b0);
    if(b.client_mode==primary_weapon_tracking::Mode::rejected||r.value<U8>(ps,0x130)!=0||
        r.value<U8>(ps,0x144)!=c.weapon_index||r.value<int>(ps,0x158)!=c.weapon_state||
        r.value<int>(ps,0x15c)!=c.other_weapon_state||b.stack_count!=static_cast<U32>(c.stack_count)||
        r.value<int>(b.cg,0x8a348)!=c.native_time||r.value<int>(b.cg,0x8c158)!=c.refdef_time||
        r.value<int>(0x2481764)!=c.command_clock||r.value<int>(0x2993850)!=c.command_number)throw ChangedFailure{};
    constexpr Addr entity=0x1a796f8;
    b.server=r.value<Addr>(entity,0x13c);
    if(!b.server||b.server!=r.value<Addr>(0x1c03140)||r.value<int>(entity)!=0||r.value<U8>(b.server,0x130)!=0)throw ChangedFailure{};
    b.server_link=r.value<U32>(entity,0x298);
    b.server_mode=primary_weapon_tracking::server_mode(
        {{r.value<int>(b.server,4),r.value<int>(b.server,0x1c4),r.value<U32>(b.server,0xc),
          r.value<U32>(b.server,0x10),r.value<U32>(b.server,0xe0),r.value<U32>(b.server,0xe4)},
         true,r.value<int>(entity,0x184),r.value<int>(b.server,0x1a2c),r.value<int>(b.server,0x1a18),
         r.value<U32>(b.server,0x1c0c),b.server_link});
    const auto mode=primary_weapon_tracking::paired_mode(b.client_mode,b.server_mode);
    if(!primary_weapon_tracking::same_link_mode(primary_weapon_mode(c),c.server_link_identity,mode,b.server_link))throw ChangedFailure{};
    const auto origin=r.value<wawvr::xr::Vec3f>(c.refdef_identity,0x20);
    const auto axis=r.value<wawvr::xr::Basis3f>(c.refdef_identity,0x34);
    if(memcmp(&origin,&c.stock_view.origin,sizeof(origin))||memcmp(&axis,&c.stock_view.axis,sizeof(axis)))throw ChangedFailure{};
}
void storage(const Reader& r,const NativeWeaponContext& c,BorrowStorage& b) {
    current_context(r,c,b);
    b.model_count=r.value<U8>(b.dobj,9);b.bone_count=r.value<U8>(b.dobj,0xa);b.table=r.value<Addr>(b.dobj,0x78);
    b.tag=r.value<U16>(0x23a566e);b.arena=r.value<Addr>(0x3067c00);
    b.duplicates=r.value<U16>(b.dobj,4);b.duplicate_size=r.value<U8>(b.dobj,8);
    if(!b.model_count||b.model_count>borrowed_profile::max_profile_models||!b.bone_count||b.bone_count>profile_max_bones||
       !b.tag||!b.table||!b.arena||!b.duplicates||b.duplicate_size<21||!(b.duplicate_size&1))throw CountFailure{};
    unsigned total=0;
    for(unsigned i=0;i<b.model_count;++i) {
        auto& s=b.models[i];s.model=r.value<Addr>(b.table,4*i);if(!s.model)throw ReadFailure{};
        s.count=r.value<U8>(s.model,4);s.roots=r.value<U8>(s.model,5);
        if(!s.count||s.count>profile_max_bones-total||!s.roots||s.roots>s.count)throw CountFailure{};
        s.name=r.value<Addr>(s.model);s.names=r.value<Addr>(s.model,8);
        s.parents=r.value<Addr>(s.model,0xc);s.bind=r.value<Addr>(s.model,0x1c);total+=s.count;
    }
    if(total!=b.bone_count)throw CountFailure{};
    if(c.dual_wield&&(b.model_count<3||b.models[2].model!=b.dual_gun_model))throw CountFailure{};
}
void snapshot(const Reader& r,const NativeWeaponContext& c,BorrowStorage& b,
              std::vector<ProfileModelCopy>& models,AssemblyDiagnostic& assembly,U16& selected_model,U16& selected_bone) {
    storage(r,c,b);
    assembly.total_bones=b.bone_count;assembly.model_parents.resize(b.model_count);
    r.bytes(b.table,4*b.model_count,assembly.model_parents.data(),assembly.model_parents.size());
    const auto duplicate_entry=Reader::address(b.arena,16*b.duplicates,4+b.duplicate_size);
    if(!r.value<U16>(duplicate_entry))throw ReadFailure{};
    assembly.duplicate_data.resize(b.duplicate_size);r.bytes(duplicate_entry,4,assembly.duplicate_data.data(),assembly.duplicate_data.size());
    unsigned total=0,matches=0;
    models.reserve(b.model_count);
    for(unsigned i=0;i<b.model_count;++i) {
        const auto& s=b.models[i];
        ProfileModelCopy model{};model.name=r.text(s.name);model.root_count=s.roots;model.bones.reserve(s.count);
        for(unsigned j=0;j<s.count;++j) {
            ProfileBoneCopy bone{};bone.script_id=r.value<U16>(s.names,2*j);if(!bone.script_id)throw ReadFailure{};
            const auto entry=Reader::address(b.arena,16*bone.script_id,4);
            bone.observed_ref_count=r.value<U16>(entry);if(!bone.observed_ref_count)throw ReadFailure{};
            bone.name=r.text(Reader::address(entry,4,1));
            bone.parent_delta=j<s.roots?0:r.value<U8>(s.parents,j-s.roots);
            r.bytes(s.bind,32*j,bone.bind_bits.data(),32);
            if(bone.script_id==b.tag){++matches;selected_model=static_cast<U16>(i);selected_bone=static_cast<U16>(j);}
            model.bones.push_back(std::move(bone));
        }
        total+=s.count;models.push_back(std::move(model));
    }
    if(total!=b.bone_count||matches!=1)throw CountFailure{};
}
float dot(wawvr::xr::Vec3f a,wawvr::xr::Vec3f b) noexcept {return a.x*b.x+a.y*b.y+a.z*b.z;}
bool finite(wawvr::xr::Vec3f v) noexcept {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool paired_dual_values(const ProfileRecord& record) noexcept {
    const auto& copied=record.copies.after;
    if(copied.size()<3||record.canonical.selected_model!=1||record.assembly.model_parents.size()!=copied.size())return false;
    const auto& arms=copied[0];const auto& left=copied[2];unsigned attachment=UINT32_MAX,flash=UINT32_MAX,flashes=0;
    for(unsigned n=0;n<arms.bones.size();++n)if(arms.bones[n].name.bytes=="tag_weapon1"){
        if(attachment!=UINT32_MAX)return false;attachment=n;}
    for(unsigned m=0;m<copied.size();++m)for(unsigned n=0;n<copied[m].bones.size();++n)
        if(copied[m].bones[n].name.bytes=="tag_flash1"){++flashes;if(m!=2)return false;flash=n;}
    if(attachment==UINT32_MAX||record.assembly.model_parents[2]!=attachment||left.root_count!=1||flashes!=1||!flash||flash>=left.bones.size())return false;
    for(unsigned n=0;n<left.bones.size();++n){if(!flash)return true;const auto delta=left.bones[flash].parent_delta;
        if(!delta||delta>flash)return false;flash-=delta;}
    return false;
}
}
bool supported_context(const NativeWeaponContext& c,DWORD thread) noexcept {
    const auto& a=c.stock_view.axis;
    const wawvr::xr::Vec3f cross{a.forward.y*a.left.z-a.forward.z*a.left.y,
        a.forward.z*a.left.x-a.forward.x*a.left.z,a.forward.x*a.left.y-a.forward.y*a.left.x};
    return c.valid&&c.body_valid&&primary_weapon_context(c)&&c.primary_viewmodel&&c.weapon_definition_valid&&
        c.call_serial&&c.local_client==0&&c.draw_gun&&c.native_thread==thread&&(c.weapon_type==0||c.weapon_type==2||c.weapon_type==4)&&
        (!c.dual_wield||native_dual_weapon_tracking_type(c.weapon_type,c.weapon_class))&&native_valid_stack_fire_count(static_cast<std::uint32_t>(c.stack_count))&&
        tracked_primary_weapon_state(c)&&native_weapon_tracking_state(c.other_weapon_state)&&
        c.weapon_index>0&&c.weapon_index<256&&c.cg_identity>=0x10000&&c.cg_identity<=UINT32_MAX-0xc5e40&&
        c.viewmodel_identity&&c.dobj_identity&&c.variant_identity&&c.definition_identity&&
        c.refdef_identity==c.cg_identity+0x8c100&&c.native_time==c.refdef_time&&c.command_number>=0&&
        std::isfinite(c.placement.scale)&&std::fabs(c.placement.scale-1)<0.0001F&&finite(c.stock_view.origin)&&
        finite(a.forward)&&finite(a.left)&&finite(a.up)&&std::fabs(dot(a.forward,a.forward)-1)<.003F&&
        std::fabs(dot(a.left,a.left)-1)<.003F&&std::fabs(dot(a.up,a.up)-1)<.003F&&
        std::fabs(dot(a.forward,a.left))<.003F&&std::fabs(dot(a.forward,a.up))<.003F&&
        std::fabs(dot(a.left,a.up))<.003F&&dot(cross,a.up)>.997F;
}
bool valid_assembly(const AssemblyDiagnostic& a,const std::vector<input::ProfileModelCopy>& models) noexcept {
    if(models.empty()||models.size()>profile_max_models||a.model_parents.size()!=models.size()||
       !a.total_bones||a.total_bones>profile_max_bones||a.duplicate_data.size()<21||
       a.duplicate_data.size()>255||!(a.duplicate_data.size()&1)||a.duplicate_data.back())return false;
    unsigned start=0;
    for(std::size_t i=0;i<models.size();++i) {
        if(models[i].bones.empty()||models[i].bones.size()>profile_max_bones-start||
           (a.model_parents[i]!=0xff&&a.model_parents[i]>=start))return false;
        start+=static_cast<unsigned>(models[i].bones.size());
    }
    if(start!=a.total_bones)return false;
    std::array<U32,5> expected{};
    int previous=-1;
    for(std::size_t i=20;i+1<a.duplicate_data.size();i+=2) {
        const unsigned child=a.duplicate_data[i],parent=a.duplicate_data[i+1];
        if(!child||!parent||child>a.total_bones||parent>=child||static_cast<int>(child)<=previous)return false;
        previous=static_cast<int>(child);const auto ordinal=child-1;
        expected[ordinal/32]|=0x80000000U>>(ordinal%32);
    }
    for(unsigned i=0;i<5;++i) {
        U32 actual{};for(unsigned j=0;j<4;++j)actual|=static_cast<U32>(a.duplicate_data[i*4+j])<<(j*8);
        if(actual!=expected[i])return false;
    }
    return true;
}
ReadStatus read_profile(const ReadHost& host,const NativeWeaponContext& c,DWORD thread,std::uint64_t now,ProfileRecord* out,ReadWorkCounts* counts) noexcept {
    if(counts)*counts={};
    if(!out)return ReadStatus::context;*out={};
    if(!supported_context(c,thread)||!host.copy||!host.now||!host.command||!now)return ReadStatus::context;
    try {
        PreCommandSnapshot pre{},post{};
        if(!host.command(host.context,&pre)||!command_fresh(c,pre,now))return ReadStatus::command;
        Reader reader{host};BorrowStorage before{},after{},tail{};ProfileRecord record{};
        auto& p=record.copies;p.image_sha256=profile_sp_image_sha256;
        p.before_stamp={c.call_serial,pre.gameplay_epoch,now,thread};
        AssemblyDiagnostic final_assembly{};U16 final_model{},final_bone{};
        snapshot(reader,c,before,p.before,record.assembly,p.selected_model,p.selected_local_bone);
        snapshot(reader,c,after,p.after,final_assembly,final_model,final_bone);
        // A final context read follows all text/bind copies. It cannot establish
        // an allocation generation; it rejects observed changes within this borrow.
        storage(reader,c,tail);
        if(!(before==after)||!(after==tail)||
           !(record.assembly==final_assembly)||final_model!=p.selected_model||final_bone!=p.selected_local_bone)return ReadStatus::changed;
        if(!host.command(host.context,&post))return ReadStatus::command;
        const auto finished=host.now(host.context);
        if(!command_fresh(c,post,finished)||post.sequence!=pre.sequence||post.gameplay_epoch!=pre.gameplay_epoch||
           finished<now||finished-now>profile_max_copy_age_ms)return ReadStatus::changed;
        p.after_stamp={c.call_serial,post.gameplay_epoch,finished,thread};p.rechecks={true,true,true,true,true,true};
        if(!valid_assembly(record.assembly,p.before))return ReadStatus::assembly;
        if(counts)++counts->canonical_builds;
        record.identity_status=canonical_profile_identity(p,thread,post.gameplay_epoch,finished,&record.canonical,host.optimized_identity);
        if(record.identity_status!=ProfileIdentityStatus::accepted){out->identity_status=record.identity_status;return ReadStatus::canonical;}
        if(c.dual_wield&&!paired_dual_values(record))return ReadStatus::assembly;
        record.command_sequence=pre.sequence;record.native_time=c.native_time;record.command_number=c.command_number;
        record.command_clock=c.command_clock;record.weapon_index=c.weapon_index;*out=std::move(record);return ReadStatus::accepted;
    }catch(const ReadFailure&){return ReadStatus::read_failed;}
    catch(const CountFailure&){return ReadStatus::count;}
    catch(const ChangedFailure&){return ReadStatus::changed;}
    catch(...){return ReadStatus::exception;}
}
} // namespace bovr::t5::profile_detail
