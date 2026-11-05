// SPDX-License-Identifier: GPL-3.0-only
#include "t5/crossbow_adapter.hpp"
#include "t5/crossbow_policy.hpp"
#include "t5/crossbow_optic.hpp"
#include "t5/magnified_optic_policy.hpp"
#include "t5/magnified_optic_fit.hpp"
#include "t5/physical_infrared_scope.hpp"
#include "t5/infrared_scope_adapter.hpp"
#include "t5/borrowed_profile_shape.hpp"
#include "t5/weapon_command_profile.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "t5/manual_reload_adapter.hpp"
#include "loader/trace.hpp"
#include <cstring>
#include <array>
namespace bovr::t5 {
namespace {
using U32=std::uint32_t;
template<class T>T read(std::uintptr_t p){T v{};std::memcpy(&v,reinterpret_cast<const void*>(p),sizeof v);return v;}
using Kind=magnified_optic::Kind;
struct State {command_profile::Copy exact{};std::uint64_t updated{},generation{};bool eligible{},lens_valid{};Kind kind{};crossbow::LensFit lens{};} state;
SRWLOCK lock=SRWLOCK_INIT;
bool identity(unsigned weapon,std::uintptr_t* variant,std::uintptr_t* definition,Kind* kind=nullptr) noexcept {
    __try {
        if(!weapon||weapon>255)return false;
        *variant=read<U32>(0xbe19a8+weapon*4);if(*variant<0x10000)return false;
        *definition=read<U32>(*variant+8);if(*definition<0x10000)return false;
        char name[128]{};const auto address=read<U32>(*variant);
        for(unsigned i=0;i<127;++i){name[i]=read<char>(address+i);if(!name[i])break;}
        char overlay[128]{};const auto material=read<U32>(*variant+0x8c);
        if(material>0x10000){const auto text=read<U32>(material);
            for(unsigned i=0;i<127&&text>0x10000;++i){overlay[i]=read<char>(text+i);if(!overlay[i])break;}}
        const auto selected=magnified_optic::classify(read<int>(*definition+0x1c),read<int>(*definition+0x20),read<int>(*variant+0x20),name,overlay);
        if(kind)*kind=selected;
        return selected!=Kind::none;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool weapon_now(const PreCommandSnapshot& p,Kind* kind) noexcept {
    __try {std::uintptr_t v{},d{};return p.native_cg_identity&&identity(read<unsigned char>(p.native_cg_identity+0x8a364+0x144),&v,&d,kind);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool infrared_owner_current(const State& s,const PreCommandSnapshot& pre) noexcept {
    __try {
        const auto& id=s.exact.identity;std::uintptr_t variant{},definition{};Kind kind{};
        return GetCurrentThreadId()==read<DWORD>(0x251ca98)&&
            id.cg==pre.native_cg_identity&&id.cg==read<U32>(0x2ff5354)&&
            id.server==read<U32>(0x1a796f8+0x13c)&&id.server==read<U32>(0x1c03140)&&
            !read<U32>(0x1a796f8+0x298)&&read<int>(0x1a796f8+0x184)>0&&
            !read<int>(id.cg+0x8a360)&&!read<int>(id.cg+0x8a364+4)&&!read<int>(id.server+4)&&
            read<int>(id.cg+0x8a364+0x1c4)>0&&read<int>(id.server+0x1c4)>0&&
            s.exact.profile.weapon==read<unsigned char>(id.cg+0x8a364+0x144)&&
            s.exact.profile.weapon==read<unsigned char>(id.server+0x144)&&
            !read<unsigned char>(id.cg+0x8a364+0x130)&&!read<unsigned char>(id.server+0x130)&&
            identity(s.exact.profile.weapon,&variant,&definition,&kind)&&kind==Kind::thermal&&
            variant==id.variant&&definition==id.definition;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Read only the currently admitted primary model's visible rigid lens surfaces.
// Alternate scopes share one XModel, so the current native hide bits select the
// actual attachment. Cache copied values by immutable profile and visibility.
bool lens_fit(const command_profile::Copy& exact,Kind kind,crossbow::LensFit* fit) noexcept {
    if(kind==Kind::crossbow){*fit={};return true;}
    static unsigned row=~0u;static std::array<U32,5> last_hidden{};
    static crossbow::LensFit cached{};static Kind last_kind{};static bool valid{};
    __try {
        const auto* p=borrowed_profile::get(exact.profile.row);
        if(!p||p->model.size()<2||exact.profile.gun>=p->bone.size()||exact.profile.flash>=p->bone.size())return false;
        const auto object=exact.identity.object,model=exact.identity.model_storage[1][0];
        std::array<U32,5> hidden{};for(unsigned i=0;i<5;++i)hidden[i]=read<U32>(object+0x5c+4*i);
        if(row==exact.profile.row&&hidden==last_hidden&&last_kind==kind){*fit=cached;return valid;}
        row=exact.profile.row;last_hidden=hidden;last_kind=kind;valid=false;
        const auto surfaces=read<U32>(model+0x20),materials=read<U32>(model+0x24);
        const auto count=read<unsigned char>(model+6);
        if(!count||count>128||surfaces<0x10000||materials<0x10000)return false;
        animation_detail::NativeMat32 gun_raw{},flash_raw{};animation_detail::Transform gun{},flash{};
        std::memcpy(&gun_raw,p->bone[exact.profile.gun].bind.data(),32);
        std::memcpy(&flash_raw,p->bone[exact.profile.flash].bind.data(),32);
        if(!animation_detail::convert_native(gun_raw,&gun)||!animation_detail::convert_native(flash_raw,&flash))return false;
        float rear=1.e6f;
        for(unsigned i=0;i<count;++i){
            const auto material=read<U32>(materials+4*i);if(material<0x10000)continue;
            const auto text=read<U32>(material);char name[128]{};
            if(text<0x10000)continue;
            for(unsigned n=0;n<127;++n){name[n]=read<char>(text+n);if(!name[n])break;}
            if(!magnified_optic::lens_material(name))continue;
            const auto s=surfaces+i*0x44,verts=read<U32>(s+0x20),lists=read<U32>(s+0x28);
            const auto n=read<std::uint16_t>(s+4),flags=read<std::uint16_t>(s+2);
            if(!n||n>2048||(flags&0x81)||read<unsigned char>(s+1)!=1||verts<0x10000||lists<0x10000)continue;
            const auto offset=read<std::uint16_t>(lists);const unsigned local_bone=offset/64;
            if(offset%64||local_bone>=p->model[1].count||read<std::uint16_t>(lists+2)!=n)continue;
            const auto bone=local_bone+p->model[1].offset;
            if(bone>=160||(hidden[bone>>5]&(0x80000000u>>(bone&31))))continue;
            animation_detail::Vec low{1.e6f,1.e6f,1.e6f},high{-1.e6f,-1.e6f,-1.e6f};bool finite=true;
            for(unsigned v=0;v<n;++v){const auto point=read<animation_detail::Vec>(verts+32*v);
                if(!animation_detail::finite(point)){finite=false;break;}
                low={std::min(low.x,point.x),std::min(low.y,point.y),std::min(low.z,point.z)};
                high={std::max(high.x,point.x),std::max(high.y,point.y),std::max(high.z,point.z)};}
            crossbow::LensFit candidate{};
            if(finite&&low.x<rear&&magnified_optic::fit_bounds(low,high,gun,flash,kind==Kind::compact,&candidate)){
                cached=candidate;rear=low.x;valid=true;}
        }
        for(unsigned i=0;i<5;++i)if(hidden[i]!=read<U32>(object+0x5c+4*i)){row=~0u;return false;}
        if(read<U32>(exact.identity.table+4)!=model){row=~0u;return false;}
        *fit=cached;
        trace("t5_magnified_optic_fit","weapon=%u row=%u kind=%u valid=%u offset=%.5f,%.5f,%.5f radius=%.5f",
            exact.profile.weapon,row,unsigned(kind),valid,fit->offset.x,fit->offset.y,fit->offset.z,fit->radius);
        return valid;
    }__except(EXCEPTION_EXECUTE_HANDLER){row=~0u;valid=false;return false;}
}
// Per-equipped-weapon tuning, with restoration only while the exact asset
// identity and our written values still match. Never patch projectile code.
struct SpreadTuning { unsigned weapon{};std::uintptr_t variant{},definition{};std::array<float,6> original{}; } tuning;
void tune_spread(const command_profile::Copy* exact) noexcept {
    __try {
        if(tuning.weapon&&(!exact||exact->identity.definition!=tuning.definition)) {
            std::uintptr_t v{},d{};
            if(identity(tuning.weapon,&v,&d)&&v==tuning.variant&&d==tuning.definition)
                for(unsigned i=0;i<6;++i)if(read<float>(d+0x4cc+4*i)==0.f)
                    std::memcpy(reinterpret_cast<void*>(d+0x4cc+4*i),&tuning.original[i],4);
            tuning={};
        }
        if(!exact||tuning.weapon)return;
        std::uintptr_t v{},d{};const auto weapon=exact->profile.weapon;
        if(!identity(weapon,&v,&d)||v!=exact->identity.variant||d!=exact->identity.definition)return;
        SpreadTuning next{};next.weapon=weapon;next.variant=v;next.definition=d;
        for(unsigned i=0;i<6;++i){next.original[i]=read<float>(d+0x4cc+4*i);
            if(!std::isfinite(next.original[i])||next.original[i]<0||next.original[i]>90)return;}
        tuning=next;const float zero{};
        for(unsigned i=0;i<6;++i)std::memcpy(reinterpret_cast<void*>(d+0x4cc+4*i),&zero,4);
        trace("t5_crossbow_spread","weapon=%u hip=0 native_projectile_unchanged=1 original=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",weapon,
            next.original[0],next.original[1],next.original[2],next.original[3],next.original[4],next.original[5]);
    }__except(EXCEPTION_EXECUTE_HANDLER){tuning={};}
}
}
bool update_crossbow_input(const CommandContext& c,const input::CommandRigSample& rig,bool live) noexcept {
    State next{};PreCommandSnapshot pre{};
    const command_profile::ReadHost host{nullptr,command_profile::protected_copy,
        [](void*,PreCommandSnapshot* p)noexcept{return read_pre_command_snapshot(p);},
        [](void*)noexcept{return GetCurrentThreadId();},[](void*)noexcept{return GetTickCount64();}};
    const bool recognized=live&&read_pre_command_snapshot(&pre)&&weapon_now(pre,&next.kind);
    next.eligible=recognized&&primary_weapon_active(pre)&&
        command_input_generation_current(rig)&&command_profile::read(host,{c,pre.sequence,pre.gameplay_epoch,true},&next.exact)==command_profile::Status::accepted;
    tune_spread(next.eligible?&next.exact:nullptr);
    next.lens_valid=next.eligible&&lens_fit(next.exact,next.kind,&next.lens);
    next.updated=GetTickCount64();next.generation=rig.xr_generation;
    AcquireSRWLockExclusive(&lock);const bool changed=state.eligible!=next.eligible;state=next;ReleaseSRWLockExclusive(&lock);
    if(next.eligible&&next.lens_valid&&next.kind==Kind::thermal)ensure_infrared_scope_adapter();
    if(changed)trace("t5_crossbow_input","eligible=%u weapon=%u fullscreen_ads=0 physical_scope=1",next.eligible,next.exact.profile.weapon);
    return recognized;
}
bool crossbow_native_infrared(int local_client) noexcept {
    if(local_client)return false;
    State s{};AcquireSRWLockShared(&lock);s=state;ReleaseSRWLockShared(&lock);
    const auto now=GetTickCount64();
    if(!s.eligible||!s.lens_valid||s.kind!=Kind::thermal||!s.generation||
       now<s.updated||now-s.updated>100||manual_reload_uses_offhand())return false;
    PreCommandSnapshot pre{},after{};wawvr::xr::ActionSnapshot actions{};
    input::CommandRigSample rig{};std::uint64_t generation{};
    if(!read_pre_command_snapshot(&pre)||!pre.gameplay_active||!pre.native_gameplay_fields_valid||
       !pre.context.valid||pre.context.local_client||pre.context.key_catcher||pre.context.native_override||
       pre.context.connection_state<9||now<pre.observed_ms||now-pre.observed_ms>100||
       pre.gameplay_epoch!=s.exact.gameplay_epoch||pre.sequence<s.exact.pre_sequence||
       !primary_weapon_active(pre)||!infrared_owner_current(s,pre)||
       !read_vr_command_input_snapshot(&actions,&generation,&rig)||generation!=s.generation||
       !actions.focused||(actions.menu.active&&actions.menu.current)||
       !physical_infrared_scope(rig,s.lens,generation,GetTickCount64())||!command_input_generation_current(rig)||
       !read_pre_command_snapshot(&after)||after.sequence!=pre.sequence||after.gameplay_epoch!=pre.gameplay_epoch||
       !infrared_owner_current(s,after)||!command_input_generation_current(rig))return false;
    return true;
}
void crossbow_scope_layout(void*,const wawvr::xr::FrameState& frame,wawvr::xr::StereoSourceLayout* layout) noexcept {
    State s{};AcquireSRWLockShared(&lock);s=state;ReleaseSRWLockShared(&lock);
    const auto now=GetTickCount64();
    if(!s.eligible||!s.lens_valid||now<s.updated||now-s.updated>100||manual_reload_uses_offhand())return;
    if(crossbow::scope(frame,layout,s.lens)){layout->physical_scope.use_separate_source=s.kind==Kind::thermal;static unsigned n{};if(++n<=4||n%240==0)
        trace("t5_crossbow_scope","frame=%llu source=%.5f,%.5f,%.5f,%.5f lens=%.4f,%.4f,%.4f",
            frame.frame_id,layout->physical_scope.source.x,layout->physical_scope.source.y,layout->physical_scope.source.width,layout->physical_scope.source.height,
            layout->physical_scope.lens_origin_reference_meters.x,layout->physical_scope.lens_origin_reference_meters.y,layout->physical_scope.lens_origin_reference_meters.z);}
}
float select_crossbow_spread(std::uintptr_t entity,unsigned weapon,std::uintptr_t wp,float spread) noexcept {
    State s{};AcquireSRWLockShared(&lock);s=state;ReleaseSRWLockShared(&lock);
    const auto now=GetTickCount64();if(!s.eligible||s.kind!=Kind::crossbow||now<s.updated||now-s.updated>100||entity!=0x1a796f8||weapon!=s.exact.profile.weapon)return spread;
    __try {
        std::uintptr_t variant{},definition{};const auto& id=s.exact.identity;
        if(!identity(weapon,&variant,&definition)||variant!=id.variant||definition!=id.definition||
           read<U32>(wp+60)!=variant||read<U32>(wp+64)!=definition||read<U32>(entity+0x13c)!=id.server||
           read<U32>(0x1c03140)!=id.server||read<U32>(0x2ff5354)!=id.cg||read<U32>(entity+0x298)!=id.server_link||
           read<unsigned char>(id.server+0x144)!=weapon||read<unsigned char>(id.cg+0x8a364+0x144)!=weapon||
           read<int>(id.server+0x1c4)<=0||(read<U32>(id.server+0x10)&0x80)||(read<U32>(id.server+0xc)&0xc00))return spread;
        trace("t5_crossbow_projectile","weapon=%u spread_before=%.5f spread_after=0 forward=%.5f,%.5f,%.5f gun=%.3f,%.3f muzzle=%.3f,%.3f,%.3f native_projectile=1",
            weapon,spread,read<float>(wp),read<float>(wp+4),read<float>(wp+8),read<float>(id.server+0x1c34),read<float>(id.server+0x1c38),
            read<float>(wp+36),read<float>(wp+40),read<float>(wp+44));
        return 0.f;
    }__except(EXCEPTION_EXECUTE_HANDLER){return spread;}
}
}
