// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/weapon_command_profile.hpp"
#include "t5/native_camera_mode.hpp"
#include "t5/primary_weapon_tracking.hpp"
#include "t5/native_weapon_tracking_state.hpp"
#include "t5/runtime_profile_catalog.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace bovr::t5::command_profile {
namespace {
namespace p=input::profile_admission;
namespace catalog=borrowed_profile;
using U8=std::uint8_t;
using U16=std::uint16_t;
using U32=std::uint32_t;
struct Failure {Status status;Rejection rejection;};
[[noreturn]] void fail(Status s,Rejection reason=Rejection::none){throw Failure{s,reason};}
struct Reader {
    const ReadHost& h;
    Diagnostic* diagnostic{};
    static U32 address(std::uintptr_t base,std::size_t offset,std::size_t size) {
        if(base<0x10000||base>UINT32_MAX||offset>UINT32_MAX||size>UINT32_MAX)fail(Status::storage);
        const auto start=std::uint64_t(base)+offset;
        if(start>UINT32_MAX||!size||size>UINT32_MAX-start+1)fail(Status::storage);
        return static_cast<U32>(start);
    }
    void bytes(std::uintptr_t base,std::size_t offset,void* out,std::size_t size)const {
        const auto at=address(base,offset,size);
        if(!out||!h.copy(h.context,at,out,size))fail(Status::storage);
    }
    template<class T>T value(std::uintptr_t base,std::size_t offset=0)const {
        T v{};bytes(base,offset,&v,sizeof(v));return v;
    }
    template<std::size_t N>void text(std::uintptr_t at,std::array<char,N>& out)const {
        out={};
        for(std::size_t i=0;i<N;++i){out[i]=value<char>(at,i);if(!out[i])return;}
        fail(Status::profile,Rejection::unterminated_name);
    }
};
bool same_command(const CommandContext& a,const CommandContext& b) noexcept {
    return a.valid&&b.valid&&a.local_client==b.local_client&&a.native_thread==b.native_thread&&
        a.command_number==b.command_number&&a.native_frame_time==b.native_frame_time&&
        a.native_override==b.native_override&&a.key_catcher==b.key_catcher&&a.connection_state==b.connection_state&&
        !std::memcmp(a.viewangles,b.viewangles,sizeof(a.viewangles))&&!std::memcmp(a.kickangles,b.kickangles,sizeof(a.kickangles));
}
PreCommandSnapshot precommand(const ReadHost& h,const Request& q,std::uint64_t now) {
    PreCommandSnapshot s{};
    if(!h.command(h.context,&s)||!same_command(s.context,q.command)||s.sequence!=q.pre_sequence||
       s.gameplay_epoch!=q.gameplay_epoch||!primary_weapon_active(s)||!s.native_gameplay_fields_valid||
       !s.observed_ms||now<s.observed_ms||now-s.observed_ms>100)fail(Status::command);
    return s;
}
float number(U32 word)noexcept{return std::bit_cast<float>(word);}
bool rigid(const std::array<U32,12>& v)noexcept {
    for(auto word:v)if(!std::isfinite(number(word)))return false;
    const auto dot=[&](unsigned a,unsigned b){return number(v[a])*number(v[b])+number(v[a+1])*number(v[b+1])+number(v[a+2])*number(v[b+2]);};
    const float cx=number(v[4])*number(v[8])-number(v[5])*number(v[7]);
    const float cy=number(v[5])*number(v[6])-number(v[3])*number(v[8]);
    const float cz=number(v[3])*number(v[7])-number(v[4])*number(v[6]);
    return std::fabs(dot(3,3)-1)<.003F&&std::fabs(dot(6,6)-1)<.003F&&std::fabs(dot(9,9)-1)<.003F&&
        std::fabs(dot(3,6))<.003F&&std::fabs(dot(3,9))<.003F&&std::fabs(dot(6,9))<.003F&&
        cx*number(v[9])+cy*number(v[10])+cz*number(v[11])>.997F;
}
struct Storage {
    struct BoneValue {
        std::array<char,input::profile_max_name_bytes+1> name{};
        std::array<U32,8> bind{};
        U8 parent{};
        friend bool operator==(const BoneValue&,const BoneValue&)=default;
    };
    Copy copy{};
    std::array<std::array<U32,8>,p::max_models> headers{};
    std::array<std::array<char,input::profile_max_name_bytes+1>,p::max_models> model_names{};
    std::array<U8,p::max_models> attachments{};
    std::array<U8,255> duplicates{};
    std::array<BoneValue,p::max_bones> bones{};
    unsigned model_count{},bone_count{},flash_model{},flash_local{};
    U32 topdown_name{};
    friend bool operator==(const Storage&,const Storage&)=default;
};
// Only immutable catalog row numbers survive an occurrence. A hint is never
// an admission: all current metadata, assembly and payload bytes are recopied
// and compared on both passes before it can be reused.
thread_local std::array<unsigned,256> recent_rows=[] {
    std::array<unsigned,256> rows{};rows.fill(catalog::invalid_runtime_row);return rows;
}();
bool metadata_matches(const Storage& s,unsigned row) noexcept {
    const auto* shape=catalog::shape(row);const auto* profile=catalog::get(row);
    if(!shape||!profile||shape->weapon!=s.copy.profile.weapon||shape->total!=s.bone_count||
       profile->model.size()!=s.model_count)return false;
    if(profile->model_parents.empty()){
        if(s.model_count!=2||s.copy.identity.duplicate_size!=21||s.duplicates!=std::array<U8,255>{}||
           s.attachments[0]!=255||s.attachments[1]!=shape->attachment)return false;
    }else if(profile->model_parents.size()!=s.model_count||profile->duplicate_data.size()!=s.copy.identity.duplicate_size||
        !std::equal(profile->model_parents.begin(),profile->model_parents.end(),s.attachments.begin())||
        !std::equal(profile->duplicate_data.begin(),profile->duplicate_data.end(),s.duplicates.begin()))return false;
    for(unsigned i=0;i<s.model_count;++i){const auto& expected=profile->model[i];
        if((s.headers[i][1]&255)!=expected.count||((s.headers[i][1]>>8)&255)!=expected.root||
           std::string_view(s.model_names[i].data())!=expected.name)return false;}
    return true;
}

void context(const Reader& r,const Request& q,const PreCommandSnapshot& pre,Storage& s) {
    auto& out=s.copy;auto& id=out.identity;auto& v=out.native;
    id.cg=r.value<U32>(0x2ff5354);
    if(id.cg!=pre.native_cg_identity||id.cg<0x10000||id.cg>UINT32_MAX-0xc5e40)fail(Status::context);
    const auto ps=Reader::address(id.cg,0x8a364,0x1c8),ref=Reader::address(id.cg,0x8c100,0x60);
    namespace tracking=primary_weapon_tracking;
    const auto player=[&](U32 address){return tracking::Player{r.value<int>(address,4),r.value<int>(address,0x1c4),
        r.value<U32>(address,0xc),r.value<U32>(address,0x10),r.value<U32>(address,0xe0),r.value<U32>(address,0xe4)};};
    const auto mode=tracking::client_mode(player(ps),{r.value<int>(id.cg,0x8a360),r.value<int>(0x2910160),r.value<int>(0x2910164),
        r.value<int>(0x2911e2c)!=0,r.value<int>(id.cg,0xc)!=0,r.value<int>(id.cg,0xa9d44)!=0});
    if(r.value<U8>(ps,0x130)||mode==tracking::Mode::rejected||
       (mode==tracking::Mode::linked)!=pre.linked_weapon_active||
       r.value<int>(0x2993850)!=q.command.command_number||r.value<int>(0x2481764)!=q.command.native_frame_time)fail(Status::context);
    const unsigned weapon=r.value<U8>(ps,0x144);
    if(r.diagnostic)r.diagnostic->weapon=weapon;
    if(!weapon||(weapon==42&&!q.olympia_enabled))fail(Status::profile,Rejection::weapon_gate);
    out.profile.weapon=weapon;
    v.weapon_state=r.value<int>(ps,0x158);v.other_state=r.value<int>(ps,0x15c);
    v.stack_count=r.value<U32>(ps,0x1b0);
    if(!native_weapon_tracking_state(v.weapon_state)||!native_weapon_tracking_state(v.other_state)||
       !native_valid_stack_fire_count(v.stack_count))fail(Status::context);
    id.variant=r.value<U32>(0xbe19a8,4*weapon);
    id.definition=r.value<U32>(id.variant,8);
    v.weapon_class=r.value<int>(id.definition,0x20);v.fire_type=r.value<int>(id.definition,0x30);
    v.weapon_type=r.value<int>(id.definition,0x1c);
    const auto dual=r.value<U8>(id.definition,0x56a);v.dual_wield=dual!=0;
    if((v.weapon_type!=0&&v.weapon_type!=2&&v.weapon_type!=4)||v.weapon_class<0||(v.weapon_class==9&&v.weapon_type!=4)||
       v.fire_type<0||v.fire_type>6||dual>1)fail(Status::context);
    if(v.dual_wield){
        if(!native_dual_weapon_tracking_type(v.weapon_type,v.weapon_class))fail(Status::context);
        v.dual_weapon=r.value<U32>(id.definition,0x594);
        if(!v.dual_weapon||v.dual_weapon>=256||v.dual_weapon==weapon)fail(Status::context);
        v.dual_variant=r.value<U32>(0xbe19a8,4*v.dual_weapon);v.dual_definition=r.value<U32>(v.dual_variant,8);
        const auto left_type=r.value<int>(v.dual_definition,0x1c);v.dual_weapon_class=r.value<int>(v.dual_definition,0x20);
        v.dual_fire_type=r.value<int>(v.dual_definition,0x30);
        if(left_type!=v.weapon_type||!native_dual_weapon_tracking_type(left_type,v.dual_weapon_class)||
           v.dual_weapon_class<0||v.dual_weapon_class==9||v.dual_fire_type<0||v.dual_fire_type>6||
           r.value<U8>(v.dual_definition,0x56a)!=1||r.value<U32>(v.dual_definition,0x594)!=weapon)fail(Status::context);
        // CG_ChangeViewmodel puts the companion's gunXModel[0] at model2,
        // attached to tag_weapon1. Observe that native ordinal, never guess it.
        v.dual_model_table=r.value<U32>(v.dual_definition,4);v.dual_gun_model=r.value<U32>(v.dual_model_table);
        if(!v.dual_gun_model)fail(Status::context);
    }
    v.native_time=r.value<int>(id.cg,0x8a348);v.refdef_time=r.value<int>(id.cg,0x8c158);
    v.native_frame_time=r.value<int>(id.cg,0x8a344);
    if(v.native_time<=0||v.native_time!=v.refdef_time||v.native_time!=pre.native_game_time)fail(Status::context);
    r.bytes(ref,0x20,v.stock_view_bits.data(),12);
    r.bytes(ref,0x34,v.stock_view_bits.data()+3,36);
    if(!rigid(v.stock_view_bits))fail(Status::context);
    v.topdown=r.value<U32>(0xbcd150);s.topdown_name=r.value<U32>(v.topdown);
    constexpr char expected[]="player_topDownCamMode";std::array<char,sizeof(expected)> name{};
    r.bytes(s.topdown_name,0,name.data(),name.size());
    if(std::memcmp(name.data(),expected,sizeof(expected))||r.value<int>(v.topdown,0x10)!=5||r.value<int>(v.topdown,0x18))fail(Status::context);
    v.visibility=r.value<U32>(0x2f67f28);
    if(!r.value<U8>(v.visibility,0x18))fail(Status::context); // exact existing visibility slot, no invented dvar name
    const auto pose=Reader::address(id.cg,0xc5e04,0x3c);
    if(r.value<U8>(pose,2)!=20||r.value<U8>(pose,5)||r.value<U8>(pose,0x14))fail(Status::context);
    r.bytes(pose,0x24,v.pose_bits.data(),24);
    for(auto word:v.pose_bits)if(!std::isfinite(number(word)))fail(Status::context);
    constexpr U32 entity=0x1a796f8;
    id.server=r.value<U32>(entity,0x13c);
    if(!id.server||id.server!=r.value<U32>(0x1c03140)||r.value<int>(entity)||r.value<U8>(id.server,0x130))fail(Status::context);
    id.server_link=r.value<U32>(entity,0x298);
    const auto server_mode=tracking::server_mode({player(id.server),true,r.value<int>(entity,0x184),
        r.value<int>(id.server,0x1a2c),r.value<int>(id.server,0x1a18),r.value<U32>(id.server,0x1c0c),id.server_link});
    if(tracking::paired_mode(mode,server_mode)==tracking::Mode::rejected)fail(Status::context);
    id.tracking_mode=static_cast<U32>(mode);
    out.pre_sequence=q.pre_sequence;out.gameplay_epoch=q.gameplay_epoch;
    out.command_number=q.command.command_number;out.command_clock=q.command.native_frame_time;out.thread=q.command.native_thread;
}
void storage(const Reader& r,const Request& q,const PreCommandSnapshot& pre,Storage& s) {
    context(r,q,pre,s);auto& id=s.copy.identity;
    id.owner=r.value<U32>(0xc1c6d8);id.object=r.value<U32>(id.owner);
    const auto pool=r.value<short>(0x24e6150,1536*2);
    if(pool<=0||pool>=0xc00||id.object!=0x2489148+static_cast<unsigned>(pool)*0x7c||!r.value<U8>(0x2488548,static_cast<unsigned>(pool)))fail(Status::context);
    id.pool=static_cast<U16>(pool);
    s.model_count=r.value<U8>(id.object,9);
    if(r.diagnostic)r.diagnostic->model_count=s.model_count;
    if(s.model_count<2||s.model_count>p::max_models)fail(Status::profile,Rejection::model_count);
    const auto bones=r.value<U8>(id.object,0xa);
    if(r.diagnostic)r.diagnostic->bone_count=bones;
    if(!bones||bones>p::max_bones)fail(Status::profile,Rejection::bone_count);
    s.bone_count=bones;
    id.table=r.value<U32>(id.object,0x78);id.tag=r.value<U16>(0x23a566e);id.arena=r.value<U32>(0x3067c00);
    id.duplicates=r.value<U16>(id.object,4);id.duplicate_size=r.value<U8>(id.object,8);
    if(!id.tag||!id.duplicates||id.duplicate_size<21||!(id.duplicate_size&1))fail(Status::profile,Rejection::duplicate_storage);
    r.bytes(id.table,4*s.model_count,s.attachments.data(),s.model_count);
    const auto duplicate_entry=Reader::address(id.arena,16U*id.duplicates,4+id.duplicate_size);
    if(!r.value<U16>(duplicate_entry))fail(Status::profile,Rejection::duplicate_storage);
    r.bytes(duplicate_entry,4,s.duplicates.data(),id.duplicate_size);
    unsigned total=0;
    for(unsigned i=0;i<s.model_count;++i) {
        if(r.diagnostic){r.diagnostic->reading_model=i;r.diagnostic->reading_bone=0;}
        auto& m=id.model_storage[i];m[0]=r.value<U32>(id.table,4*i);
        r.bytes(m[0],0,s.headers[i].data(),32);
        const auto& h=s.headers[i];m[1]=h[0];m[2]=h[2];m[3]=h[3];m[4]=h[7];
        const unsigned count=h[1]&255,roots=(h[1]>>8)&255;
        if(r.diagnostic){if(i==0)r.diagnostic->arms_roots=roots;if(i==1)r.diagnostic->gun_roots=roots;}
        if(!count||!roots||roots>count||count>p::max_bones-total)fail(Status::profile,Rejection::model_topology);
        total+=count;r.text(m[1],s.model_names[i]);
        if(r.diagnostic){if(i==0)r.diagnostic->arms_name=s.model_names[i];if(i==1)r.diagnostic->gun_name=s.model_names[i];}
    }
    if(total!=bones)fail(Status::profile,Rejection::total_bones);
    if(s.copy.native.dual_wield&&(s.model_count<3||id.model_storage[2][0]!=s.copy.native.dual_gun_model))fail(Status::profile,Rejection::dual_model);
    // A previously enrolled bind variant can share names/counts. Its complete
    // runtime-owned payload is a hint only; a changed payload below still gets
    // two complete copies before a fresh canonical enrollment.
    const auto hint=recent_rows[s.copy.profile.weapon];
    unsigned selected=metadata_matches(s,hint)?hint:catalog::invalid_runtime_row;
    if(selected==catalog::invalid_runtime_row){s.copy.profile.row=catalog::invalid_runtime_row;return;}
    const auto& shape=*catalog::shape(selected);
    s.copy.profile={selected,shape.revision,shape.weapon,shape.gun,shape.flash,shape.total,p::policy_revision};
}
void payload(const Reader& r,Storage& s) {
    const auto* known=catalog::get(s.copy.profile.row);auto& id=s.copy.identity;unsigned tags=0,start=0;bool matches=known!=nullptr;
    for(unsigned i=0;i<s.model_count;++i) {
        const auto& m=id.model_storage[i];const unsigned count=s.headers[i][1]&255,roots=(s.headers[i][1]>>8)&255;
        for(unsigned j=0;j<count;++j) {
            if(r.diagnostic){r.diagnostic->reading_model=i;r.diagnostic->reading_bone=j;}
            const auto index=start+j;auto& value=s.bones[index];
            const auto sid=r.value<U16>(m[2],2*j);if(!sid)fail(Status::profile,Rejection::string_entry);id.ids[index]=sid;
            const auto entry=Reader::address(id.arena,16U*sid,4);
            if(!r.value<U16>(entry))fail(Status::profile,Rejection::string_entry);
            r.text(Reader::address(entry,4,1),value.name);
            if(j>=roots)value.parent=r.value<U8>(m[3],j-roots);
            r.bytes(m[4],32*j,value.bind.data(),32);
            if(known){const auto& expected=known->bone[index];
                if(std::string_view(value.name.data())!=expected.name||value.parent!=expected.parent||value.bind!=expected.bind)matches=false;}
            if(sid==id.tag){++tags;s.flash_model=i;s.flash_local=j;if(known&&index!=s.copy.profile.flash)matches=false;}
        }
        start+=count;
    }
    if(r.diagnostic){r.diagnostic->tag_count=tags;r.diagnostic->selected_model=s.flash_model;r.diagnostic->selected_bone=s.flash_local;}
    if(tags!=1)fail(Status::profile,Rejection::tag_count);
    if(!matches){const auto weapon=s.copy.profile.weapon;s.copy.profile={};
        s.copy.profile.row=catalog::invalid_runtime_row;s.copy.profile.weapon=weapon;}
}
bool same_storage(const Storage& a,const Storage& b)noexcept {
    auto ac=a.copy,bc=b.copy;
    ac.identity.ids={};bc.identity.ids={};ac.profile={};bc.profile={};
    // Row selection is derived from payload; this tail has deliberately not
    // recopied it. Current storage/command identity and assembly remain exact.
    return ac==bc&&a.copy.profile.weapon==b.copy.profile.weapon&&a.headers==b.headers&&
        a.model_names==b.model_names&&a.attachments==b.attachments&&a.duplicates==b.duplicates&&
        a.model_count==b.model_count&&a.bone_count==b.bone_count&&a.topdown_name==b.topdown_name;
}
std::vector<input::ProfileModelCopy> copied_models(const Storage& s) {
    std::vector<input::ProfileModelCopy> result;result.reserve(s.model_count);unsigned start=0;
    for(unsigned i=0;i<s.model_count;++i){input::ProfileModelCopy model;
        model.name={s.model_names[i].data(),true,true};model.root_count=(s.headers[i][1]>>8)&255;
        const auto count=s.headers[i][1]&255;model.bones.reserve(count);
        for(unsigned j=0;j<count;++j){const auto index=start+j;const auto& b=s.bones[index];input::ProfileBoneCopy bone;
            bone.name={b.name.data(),true,true};bone.script_id=s.copy.identity.ids[index];bone.observed_ref_count=1;
            bone.parent_delta=b.parent;bone.bind_bits=b.bind;model.bones.push_back(std::move(bone));}
        start+=count;result.push_back(std::move(model));}
    return result;
}
void register_current(const Storage& before,Storage& after,std::uint64_t started,std::uint64_t finished,Diagnostic* diagnostic) {
    if(after.copy.profile.row!=catalog::invalid_runtime_row)return;
    input::ProfileObservationCopies copies;copies.image_sha256=input::profile_sp_image_sha256;
    copies.before_stamp={after.copy.pre_sequence,after.copy.gameplay_epoch,started,after.copy.thread};
    copies.after_stamp={after.copy.pre_sequence,after.copy.gameplay_epoch,finished,after.copy.thread};
    copies.rechecks={true,true,true,true,true,true};copies.before=copied_models(before);copies.after=copied_models(after);
    copies.selected_model=static_cast<U16>(after.flash_model);copies.selected_local_bone=static_cast<U16>(after.flash_local);
    input::CanonicalProfileIdentity canonical;
    const auto identity=input::canonical_profile_identity(copies,after.copy.thread,after.copy.gameplay_epoch,finished,&canonical);
    if(diagnostic)diagnostic->canonical_status=static_cast<unsigned>(identity);
    if(identity!=input::ProfileIdentityStatus::accepted)fail(Status::profile,Rejection::canonical);
    catalog::CandidateStatus reason{};
    const auto row=catalog::register_runtime_profile(after.copy.profile.weapon,copies,canonical,
        {{after.attachments.data(),after.model_count},{after.duplicates.data(),after.copy.identity.duplicate_size},after.bone_count},&reason);
    if(diagnostic)diagnostic->catalog_status=static_cast<unsigned>(reason);
    const auto* shape=catalog::shape(row);if(!shape)fail(Status::profile,Rejection::catalog);
    after.copy.profile={row,shape->revision,shape->weapon,shape->gun,shape->flash,shape->total,p::policy_revision};
}
bool paired_dual_profile(const Storage& s)noexcept {
    const auto* profile=catalog::get(s.copy.profile.row);
    if(!profile||profile->model.size()<3)return false;
    const auto& arm=profile->model[0];const auto& left=profile->model[2];
    unsigned attachment=UINT32_MAX,flash=UINT32_MAX,flashes=0;
    for(unsigned j=0;j<arm.count;++j)if(profile->bone[arm.offset+j].name=="tag_weapon1"){
        if(attachment!=UINT32_MAX)return false;attachment=j;}
    for(unsigned j=0;j<profile->bone.size();++j)if(profile->bone[j].name=="tag_flash1"){++flashes;flash=j;}
    if(attachment==UINT32_MAX||s.attachments[2]!=attachment||left.root!=1||flashes!=1||
       flash<=left.offset||flash>=left.offset+left.count)return false;
    for(unsigned n=0;n<left.count;++n){if(flash==left.offset)return true;
        const auto delta=profile->bone[flash].parent;
        if(!delta||delta>flash-left.offset)return false;flash-=delta;}
    return false;
}
} // namespace

bool loaded_guards() noexcept {
    __try {
        if(bovr::native_guard::compare(reinterpret_cast<void*>(0x43cfc0), 74, "a811e587992b791be8e3d1605d2a45f7e02a7c5d3331d1ad267ffe069bd98f67"))return false; // Com_SafeClientDObjFree_complete
        if(bovr::native_guard::compare(reinterpret_cast<void*>(0x4cf420), 135, "fbfac9130db12c5383694acc0a8a839780784a5377a90b3049b6054c1674b86d"))return false; // Com_ClientDObjCreate_complete
        if(bovr::native_guard::compare(reinterpret_cast<void*>(0x5ae7a0), 107, "413cfaddb8bff5f1800193978799ec48685321eaff85a2a8e955624dc4cddf93"))return false; // CG_FreeWeapons_complete
        if(bovr::native_guard::compare(reinterpret_cast<void*>(0x796a50), 968, "881cd2360080fc8a689c6a324d87ec3c95502759c43e033296c2756dbff59904"))return false; // CG_ChangeViewmodel_complete
        if(bovr::native_guard::compare(reinterpret_cast<void*>(0x4d2670), 80, "22ea3de9abc905fc7775910a5bd7952757e960cbc9f7488080b24a03f2d1569b"))return false; // Com_InitDObj_complete
        if(bovr::native_guard::compare(reinterpret_cast<void*>(0x53357f), 39, "bc9c1b0fe767f71b5f832d3817374344198e83466988b750a87cd501309c9f68"))return false; // viewmodel_owner_allocation_reset
        if(bovr::native_guard::compare(reinterpret_cast<void*>(0x64f79b), 24, "9d412024e0494141561c86b0be36a90f71f50e17f81bd3d564177dfd3232cfb5"))return false; // viewmodel_local_reset
        if(bovr::native_guard::compare(reinterpret_cast<void*>(0x63c877), 116, "5e5599991a8ed4550d65b17e9e59f7411cfbd4a0b9958b51cc10c378d9d23559"))return false; // viewmodel_owner_free_and_globals_clear
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool protected_copy(void*,std::uintptr_t address,void* out,std::size_t size) noexcept {
    if(!out||!size||address<0x10000||address>UINT32_MAX||size>UINT32_MAX||size>UINT32_MAX-address+1)return false;
    __try{std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
Status read(const ReadHost& h,const Request& q,Copy* output) noexcept {return read(h,q,output,nullptr);}
Status read(const ReadHost& h,const Request& q,Copy* output,Diagnostic* diagnostic) noexcept {
    if(diagnostic)*diagnostic={};
    if(!output)return Status::arguments;*output={};
    if(!h.copy||!h.command||!h.thread||!h.now||!q.pre_sequence||!q.gameplay_epoch||
       !q.command.valid||q.command.local_client||!q.command.native_thread||q.command.command_number<0||
       q.command.native_override||q.command.key_catcher||q.command.connection_state<9)return Status::arguments;
    try {
        if(h.thread(h.context)!=q.command.native_thread)return Status::command;
        const auto started=h.now(h.context);if(!started)return Status::stale;
        const auto pre=precommand(h,q,started);Reader r{h,diagnostic};Storage before{},after{},tail{};
        storage(r,q,pre,before);payload(r,before);
        if(h.thread(h.context)!=q.command.native_thread)return Status::command;
        const auto middle=precommand(h,q,h.now(h.context));
        storage(r,q,middle,after);payload(r,after);
        if(before!=after)return Status::changed;
        const auto last=precommand(h,q,h.now(h.context));
        storage(r,q,last,tail);
        if(!same_storage(after,tail))return Status::changed;
        const auto finished=h.now(h.context);
        const auto post=precommand(h,q,finished);(void)post;
        if(h.thread(h.context)!=q.command.native_thread)return Status::command;
        if(finished<started||finished-started>100)return Status::stale;
        register_current(before,after,started,finished,diagnostic);
        if(after.copy.native.dual_wield&&!paired_dual_profile(after))fail(Status::profile,Rejection::paired_dual);
        recent_rows[after.copy.profile.weapon]=after.copy.profile.row;
        after.copy.started_ms=started;after.copy.finished_ms=finished;
        *output=after.copy;return Status::accepted;
    }catch(const Failure& e){if(diagnostic)diagnostic->rejection=e.rejection;return e.status;}
    catch(...){return Status::exception;}
}
bool same_current(const Copy& a,const Copy& b) noexcept {
    return p::valid(a.profile)&&p::valid(b.profile)&&a.profile==b.profile&&a.identity==b.identity&&a.native==b.native&&
        a.pre_sequence&&a.pre_sequence==b.pre_sequence&&a.gameplay_epoch&&a.gameplay_epoch==b.gameplay_epoch&&
        a.command_number==b.command_number&&a.command_clock==b.command_clock&&a.thread&&a.thread==b.thread;
}
} // namespace bovr::t5::command_profile
