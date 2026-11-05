// SPDX-License-Identifier: GPL-3.0-only
// Owned sparse layout and protected native-memory copy only. No game process,
// native helper, GPU, original CreateCmd or scene wrapper runs in this fixture.
#include "t5/weapon_command_profile.hpp"
#include "t5/runtime_profile_catalog.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "command_profile_fixture.hpp"
#include "synthetic_runtime_profile_fixture.hpp"
namespace {
using namespace q1_fixture;
void diagnostic_tests() {
    Memory good(0);cp::Copy value{};cp::Diagnostic diagnostic{};
    check(good.read(&value)==cp::Status::accepted,"diagnostic baseline accepted");
    const auto reads=good.reads.size();good.reads.clear();
    check(cp::read(good.host(),good.request,&value,&diagnostic)==cp::Status::accepted&&
        diagnostic.rejection==cp::Rejection::none&&diagnostic.weapon==4&&diagnostic.model_count==2&&
        diagnostic.bone_count==82&&diagnostic.tag_count==1&&diagnostic.selected_model==1&&
        diagnostic.selected_bone==6&&good.reads.size()==reads,"diagnostics copy existing values without extra native reads");
    const auto* known=cat::get(good.runtime_profile_row);
    check(known&&std::string_view(diagnostic.arms_name.data())==known->model[0].name&&
        std::string_view(diagnostic.gun_name.data())==known->model[1].name,"model diagnostic names are bounded copies");
    Memory count(0);count.put<U8>(Memory::object+9,17);
    check(cp::read(count.host(),count.request,&value,&diagnostic)==cp::Status::profile&&value==cp::Copy{}&&
        diagnostic.rejection==cp::Rejection::model_count&&diagnostic.model_count==17,"oversized model count has specific failure evidence");
    Memory tags(0);U16 tag{};std::memcpy(&tag,tags.pointer(0x23a566e,2),sizeof(tag));
    tags.put<U16>(Memory::model(1)+0x2000+2,tag);
    check(cp::read(tags.host(),tags.request,&value,&diagnostic)==cp::Status::profile&&value==cp::Copy{}&&
        diagnostic.rejection==cp::Rejection::tag_count&&diagnostic.tag_count==2,"ambiguous muzzle reports copied tag count");
    Memory root(0);root.weapon(93);root.text(Memory::arena+16*(73*8)+4,"j_other");
    check(cp::read(root.host(),root.request,&value,&diagnostic)==cp::Status::profile&&value==cp::Copy{}&&
        diagnostic.rejection==cp::Rejection::catalog&&diagnostic.catalog_status==static_cast<unsigned>(cat::CandidateStatus::gun_root),
        "invalid semantic root reports catalog cause without admission");
    Memory geometry(0);geometry.put<U32>(Memory::model(1)+0x4000+16,0x7fc00000U);
    check(cp::read(geometry.host(),geometry.request,&value,&diagnostic)==cp::Status::profile&&value==cp::Copy{}&&
        diagnostic.rejection==cp::Rejection::canonical&&diagnostic.canonical_status==
        static_cast<unsigned>(bovr::input::ProfileIdentityStatus::invalid_bind_geometry),"invalid bind reports canonical cause");
    auto invalid=good.request;invalid.pre_sequence=0;
    check(cp::read(good.host(),invalid,&value,&diagnostic)==cp::Status::arguments&&value==cp::Copy{}&&
        diagnostic.rejection==cp::Rejection::none&&diagnostic.weapon==0&&!diagnostic.gun_name[0],"new failed occurrence clears prior diagnostic values");
}
void reader_tests() {
    for(unsigned row=0;row<synthetic_profile_fixture::rows().size();++row) {
        Memory m(row);cp::Copy baseline{};check(m.read(&baseline)==cp::Status::accepted,"actual catalogue current reader");
        check(baseline.profile.row==m.runtime_profile_row&&p::valid(baseline.profile)&&baseline.identity.ids[baseline.profile.flash]==baseline.identity.tag,"runtime row and unique flash");
        check(baseline.command_clock==9000&&baseline.native.native_frame_time==16,"absolute key clock is not native frame delta");
        for(const auto mode:{0,2}) {Memory shadow(row);cp::Copy out=baseline;
            shadow.put<int>(Memory::cg+0x8a360,mode);check(shadow.read(&out)==cp::Status::accepted,"shadow camera mode accepted for weapon command profile");
        }
        for(int mode:{1,-1,3}){Memory shadow(row);cp::Copy out=baseline;shadow.put<int>(Memory::cg+0x8a360,mode);
            check(shadow.read(&out)!=cp::Status::accepted&&out==cp::Copy{},"unsupported camera mode is rejected with empty output");}
        for(const auto at:{Memory::cg+0xc,Memory::cg+0xa9d44}) {Memory shadow(row);cp::Copy out=baseline;shadow.put<int>(Memory::cg+0x8a360,2);shadow.put<U32>(at,1);
            check(shadow.read(&out)!=cp::Status::accepted&&out==cp::Copy{},"shadow mode preserves independent camera exclusions");}
        const auto good_reads=m.reads;
        for(const auto span:m.exact_bytes)for(std::size_t offset=0;offset<span.size;++offset) {
            const auto address=span.at+static_cast<U32>(offset);auto* byte=m.pointer(address,1);const auto old=*byte;
            m.mutate_call=0;
            for(std::size_t n=0;n<good_reads.size();++n)if(good_reads[n].at<=address&&address-good_reads[n].at<good_reads[n].size){m.mutate_call=n+1;break;}
            check(m.mutate_call!=0,"every payload byte belongs to a complete protected pass");
            m.mutate_address=address;m.mutate_value=old^1;
            cp::Copy out=baseline;check(m.read(&out)!=cp::Status::accepted&&out==cp::Copy{},"every exact payload byte changing after its first copy rejects empty");
            *byte=old;m.mutate_call=0;
        }
        for(std::size_t call=1;call<=good_reads.size();++call) {
            m.fail_call=call;cp::Copy out=baseline;
            check(m.read(&out)!=cp::Status::accepted&&out==cp::Copy{},"each copy failure leaves no output");
            check(m.reads.size()==call,"first failure stops dependent copies");
        }
        m.fail_call=0;
        if(cat::shape(m.runtime_profile_row)->weapon==42){m.request.olympia_enabled=false;cp::Copy out=baseline;check(m.read(&out)==cp::Status::profile&&out==cp::Copy{},"configured weapon readiness gate");m.request.olympia_enabled=true;}
        m.put<U32>(Memory::cg+0x8a364+0x1b0,1);cp::Copy out=baseline;
        check(m.read(&out)==cp::Status::accepted&&out.native.stack_count==1&&out.profile==baseline.profile,
            "retained native stacked-fire count admits the exact current weapon profile");
        for(U32 count:{2u,4u,7u}) {m.put<U32>(Memory::cg+0x8a364+0x1b0,count);
            check(m.read(&out)==cp::Status::accepted&&out.native.stack_count==count&&out.profile==baseline.profile,
                "valid native stacked-fire count preserves exact weapon profile and count");}
        check(!cp::same_current(baseline,out),"stack count remains part of current native observation identity");
        for(U32 count:{8u,0x80000000u,0xffffffffu}) {m.put<U32>(Memory::cg+0x8a364+0x1b0,count);
            check(m.read(&out)==cp::Status::context&&out==cp::Copy{},"out-of-range unsigned stack counts reject empty");}
        for(U32 initial:{0u,1u,4u})for(unsigned pass:{2u,3u}) {
            m.put<U32>(Memory::cg+0x8a364+0x1b0,initial);check(m.read(&out)==cp::Status::accepted,"stack transition baseline");
            const auto reads=m.reads;unsigned occurrence=0;
            for(std::size_t n=0;n<reads.size();++n)if(reads[n].at==Memory::cg+0x8a364+0x1b0&&++occurrence==pass){m.mutate_call=n;break;}
            check(m.mutate_call!=0,"stack count is reread in each protected context pass");
            m.mutate_address=Memory::cg+0x8a364+0x1b0;m.mutate_value=static_cast<U8>(initial==1?4u:1u);
            check(m.read(&out)==cp::Status::changed&&out==cp::Copy{},"valid stack count change between copied passes rejects empty");m.mutate_call=0;
        }
        m.put<U32>(Memory::cg+0x8a364+0x1b0,0);
        for(const auto state_offset:{0x158U,0x15cU}) {
            for(int state=-1;state<53;++state){m.put<int>(Memory::cg+0x8a364+state_offset,state);
                const bool accepted_state=(state>=0&&state<18)||(state>=28&&state<=33)||(state>=48&&state<=50);
                check((m.read(&out)==cp::Status::accepted)==accepted_state,"actual reader admits pinned primary sprint/continuous/low-ready states and rejects previous/offhand/unknown states");
                if(accepted_state)check(out.profile==baseline.profile&&out.identity==baseline.identity,
                    "native animation state cannot change admitted model payload or weapon identity");
            }
            m.put<int>(Memory::cg+0x8a364+state_offset,0);
        }
        m.pre.sequence=3;check(m.read(&out)==cp::Status::command&&out==cp::Copy{}&&m.reads.empty(),"wrong occurrence prevents native reads");m.pre.sequence=2;
        ++m.thread;check(m.read(&out)==cp::Status::command&&out==cp::Copy{}&&m.reads.empty(),"foreign thread prevents native reads");--m.thread;
        m.ticks=1101;check(m.read(&out)==cp::Status::command&&out==cp::Copy{}&&m.reads.empty(),"stale pre prevents native reads");m.ticks=1000;
        m.put<U16>(0x24e6150+1536*2,0xffff);check(m.read(&out)!=cp::Status::accepted&&out==cp::Copy{},"negative pool rejects");m.put<U16>(0x24e6150+1536*2,Memory::pool);
        check(m.read(&out)==cp::Status::accepted&&cp::same_current(baseline,out),"restored fixture fresh current read");
        auto changed=out;changed.finished_ms+=1;check(cp::same_current(out,changed),"observation time is not current identity");
        changed.identity.model_storage[0][0]+=4;check(!cp::same_current(out,changed),"different typed model observation refuses equality");
        // Mutate after the first complete bind copy, not before either read.
        const auto address=Memory::model(0)+0x4000;
        for(std::size_t n=0;n<m.reads.size();++n)if(m.reads[n].at==address){m.mutate_call=n+1;break;}
        m.mutate_address=address;m.mutate_value=1;
        check(m.read(&out)!=cp::Status::accepted&&out==cp::Copy{},"payload changed between complete passes cannot enroll");
    }
    Memory m(3);cp::Copy out{};check(m.read(&out)==cp::Status::accepted,"protected fault baseline");
    const auto prefix=m.reads.size()/2;m.inaccessible=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS);
    check(m.inaccessible!=nullptr,"owned inaccessible page allocation");
    if(m.inaccessible){m.protected_fault=true;m.fail_call=prefix;check(m.read(&out)!=cp::Status::accepted&&out==cp::Copy{}&&m.reads.size()==prefix,"actual SEH after readable prefix");VirtualFree(m.inaccessible,0,MEM_RELEASE);}
    auto h=m.host();h.command=nullptr;check(cp::read(h,m.request,&out)==cp::Status::arguments&&out==cp::Copy{},"missing callback rejects");
    check(cp::read(m.host(),m.request,nullptr)==cp::Status::arguments,"null output rejects");
}
void runtime_reader_tests(){
    for(const auto& model:{synthetic_profile_fixture::multi_root_gun(0),synthetic_profile_fixture::multi_root_gun(1)}){
        Memory m(6);m.weapon(9);m.gun(model);cp::Copy first{},again{};cp::Diagnostic diagnostic{};
        check(cp::read(m.host(),m.request,&first,&diagnostic)==cp::Status::accepted&&
            cat::get(first.profile.row)->model[1].root==2&&first.profile.gun==72&&
            first.profile.bones==72+model.bones.size(),"authored multi-root forests pass actual command reader");
        check(m.read(&again)==cp::Status::accepted&&cp::same_current(first,again),"multi-root current profile reuses its exact immutable entry");
        const auto at=Memory::model(1)+0x4000+32+16;
        for(std::size_t n=0;n<m.reads.size();++n)if(m.reads[n].at==Memory::model(1)+0x4000+32){m.mutate_call=n+1;break;}
        m.mutate_address=at;m.mutate_value=1;
        check(m.read(&again)==cp::Status::changed&&again==cp::Copy{},"secondary-root bind mutation between complete passes rejects");
        m.mutate_call=0;m.put<U8>(at,0);
        auto wrong=model;for(auto& bone:wrong.bones)if(bone.name.bytes=="tag_flash")--bone.parent_delta;m.gun(wrong);
        check(cp::read(m.host(),m.request,&again,&diagnostic)==cp::Status::profile&&again==cp::Copy{}&&
            diagnostic.catalog_status==static_cast<unsigned>(cat::CandidateStatus::barrel),"barrel under independent attachment root remains rejected");
    }
    {Memory m(0);m.weapon(50);m.text(Memory::model(1)+0x1000,"t5_weapon_campaign_variant");
        cp::Copy first{},again{};check(m.read(&first)==cp::Status::accepted&&first.profile.row>=cat::runtime_row_base&&
            first.profile.weapon==50&&p::valid(first.profile),"new weapon ID enrolls only after complete copied semantic profile validation");
        const auto count=m.reads.size();check(m.read(&again)==cp::Status::accepted&&cp::same_current(first,again)&&m.reads.size()==count,
            "immutable runtime row hint retains two complete current payload passes");
        for(int type:{0,2,4}){m.put<int>(Memory::definition+0x1c,type);check(m.read(&again)==cp::Status::accepted&&again.native.weapon_type==type,
            "ordinary bullet projectile and gas weapon types retain native current proof");}
        for(int type:{-1,1,3,5}){m.put<int>(Memory::definition+0x1c,type);check(m.read(&again)==cp::Status::context&&again==cp::Copy{},
            "unsupported native weapon type remains excluded");}
        m.put<int>(Memory::definition+0x1c,0);
        const auto address=Memory::model(1)+0x4000+6*32;
        check(m.read(&again)==cp::Status::accepted,"runtime hint baseline before protected mutation");
        for(std::size_t n=0;n<m.reads.size();++n)if(m.reads[n].at==address){m.mutate_call=n+1;break;}
        m.mutate_address=address;m.mutate_value=1;
        check(m.read(&again)!=cp::Status::accepted&&again==cp::Copy{},"cached runtime row never hides a between-pass bind mutation");
        m.mutate_call=0;m.put<U8>(address,0);check(m.read(&again)==cp::Status::accepted&&again.profile==first.profile,
            "restored exact runtime payload reuses original immutable row");
        m.text(Memory::model(0)+0x1000,"world_asset_with_similar_bones");
        check(m.read(&again)==cp::Status::profile&&again==cp::Copy{},"unknown model name does not bypass semantic first-person arms validation");
    }
    {Memory m(0);cp::Copy original{},variant{},repeat{},restored{};check(m.read(&original)==cp::Status::accepted,"runtime bind baseline");
        const auto at=Memory::model(1)+0x4000+6*32+16;const auto old=*reinterpret_cast<U32*>(m.pointer(at,4));
        m.put<float>(at,8.0F);check(m.read(&variant)==cp::Status::accepted&&variant.profile.row!=original.profile.row,
            "stable changed bind with unchanged model names gets a separate canonical runtime row");
        check(m.read(&repeat)==cp::Status::accepted&&repeat.profile==variant.profile,
            "same-name runtime bind variant remains eligible for the stable row hint");
        m.put<U32>(at,old);check(m.read(&restored)==cp::Status::accepted&&restored.profile==original.profile,
            "original runtime payload regains its row after variant restoration");
    }
    {Memory m(0);m.weapon(51);m.attachment(1,true);cp::Copy first{},next{};
        check(m.read(&first)==cp::Status::accepted&&first.profile.bones==83&&cat::get(first.profile.row)->model.size()==3&&
            first.identity.duplicate_size==23,"three-model assembly with complete native duplicate mapping enrolls");
        check(m.read(&next)==cp::Status::accepted&&cp::same_current(first,next),"nonzero exact duplicate assembly reuses immutable runtime row");
        m.put<U8>(Memory::arena+20+21,83);check(m.read(&next)==cp::Status::profile&&next==cp::Copy{},
            "invalid duplicate parent rejects even with cached weapon row");
        m.put<U8>(Memory::arena+20+21,73);m.put<U8>(Memory::table+14,73);
        check(m.read(&next)==cp::Status::accepted&&next.profile.row!=first.profile.row,
            "different valid attachment parent requires a different immutable assembly row");
    }
    {Memory m(0);m.weapon(52);m.attachment(78);cp::Copy value{};
        check(m.read(&value)==cp::Status::accepted&&value.profile.bones==160,"complete160bone current assembly is copied without truncation");
        m.put<U8>(Memory::object+0xa,161);check(m.read(&value)==cp::Status::profile&&value==cp::Copy{},"161bone current assembly rejects");
    }
    {Memory m(0);m.weapon(53);for(unsigned n=2;n<p::max_models;++n)m.attachment();cp::Copy value{};
        check(m.read(&value)==cp::Status::accepted&&cat::get(value.profile.row)->model.size()==p::max_models,"all16current models are copied and admitted");
        m.put<U8>(Memory::object+9,17);check(m.read(&value)==cp::Status::profile&&value==cp::Copy{},"17models exceed pinned native assembly bound");
    }
    {Memory m(0);m.weapon(54);m.text(Memory::model(1)+0x1000,std::string(255,'m'));cp::Copy value{};
        check(m.read(&value)==cp::Status::accepted&&cat::get(value.profile.row)->model[1].name.size()==255,"255byte model name is completely protected copied");
        m.put<U8>(Memory::model(1)+0x1000+255,'m');check(m.read(&value)==cp::Status::profile&&value==cp::Copy{},"unterminated256byte name rejects");
    }
}
void dual_reader_tests(){
    for(unsigned scenario=0;scenario<7;++scenario){Memory pair(0);pair.dual();cp::Copy copied{};
        pair.put<int>(Memory::definition+0x1c,2);pair.put<int>(Memory::definition+0x20,4);
        pair.put<int>(Memory::companion_definition+0x1c,2);pair.put<int>(Memory::companion_definition+0x20,4);
        switch(scenario){
        case 1:pair.put<int>(Memory::companion_definition+0x1c,0);break;
        case 2:pair.put<int>(Memory::companion_definition+0x20,3);break;
        case 3:pair.put<int>(Memory::definition+0x20,2);break;
        case 4:pair.put<U32>(Memory::companion_definition+0x594,0);break;
        case 5:pair.put<U32>(Memory::companion_models,Memory::model(1));break;
        case 6:pair.put<U8>(Memory::table+14,9);break;
        }
        const auto status=pair.read(&copied);
        if(!scenario)check(status==cp::Status::accepted&&copied.native.weapon_type==2&&copied.native.weapon_class==4&&
            copied.native.dual_weapon_class==4&&copied.native.dual_gun_model==Memory::model(2),
            "native projectile pistol pair earns exact command profile with its reciprocal companion model");
        else check(status!=cp::Status::accepted&&copied==cp::Copy{},
            "projectile pistol pair rejects mixed types wrong classes and changed reciprocal model or attachment");
    }
    {Memory m(0);m.dual();cp::Copy first{},again{};
        check(m.read(&first)==cp::Status::accepted&&first.native.dual_wield&&first.native.dual_weapon==56&&
            first.native.dual_definition==Memory::companion_definition&&first.native.dual_gun_model==Memory::model(2),
            "paired dual command copies companion native definition and exact model2 identity");
        check(m.read(&again)==cp::Status::accepted&&cp::same_current(first,again),"dual pair reuses exact immutable row with full current proof");
        for(std::size_t n=0;n<m.reads.size();++n)if(m.reads[n].at==Memory::companion_definition+0x30){m.mutate_call=n+1;break;}
        m.mutate_address=Memory::companion_definition+0x30;m.mutate_value=2;
        check(m.read(&again)==cp::Status::changed&&again==cp::Copy{},"left weapon metadata changing during command copy rejects");
    }
    for(unsigned scenario=0;scenario<15;++scenario){Memory bad(0);bad.dual();cp::Copy value{};
        switch(scenario){
        case 0:bad.put<U8>(Memory::definition+0x56a,2);break;
        case 1:bad.put<U32>(Memory::definition+0x594,0);break;
        case 2:bad.put<U32>(Memory::definition+0x594,55);break;
        case 3:bad.put<U32>(Memory::definition+0x594,256);break;
        case 4:bad.put<U32>(Memory::companion_variant+8,0);break;
        case 5:bad.put<U8>(Memory::companion_definition+0x56a,0);break;
        case 6:bad.put<U32>(Memory::companion_definition+0x594,0);break;
        case 7:bad.put<U32>(Memory::companion_models,Memory::model(1));break;
        case 8:bad.put<U8>(Memory::table+14,9);break;
        case 9:bad.text(Memory::arena+16*(84*20)+4,"tag_unrelated");break;
        case 10:bad.put<U8>(Memory::model(2)+0x3000,0);break;
        case 11:bad.put<int>(Memory::companion_definition+0x1c,2);break;
        case 12:bad.put<int>(Memory::definition+0x1c,2);break;
        case 13:bad.put<int>(Memory::companion_definition+0x30,7);break;
        case 14:bad.put<U8>(Memory::model(2)+5,2);break;
        }
        check(bad.read(&value)!=cp::Status::accepted&&value==cp::Copy{},"dual admission rejects missing inconsistent or ambiguous companion evidence");
    }
    {Memory gas(0);gas.weapon(57);gas.put<int>(Memory::definition+0x1c,4);gas.put<int>(Memory::definition+0x20,9);cp::Copy out{};
        check(gas.read(&out)==cp::Status::accepted&&out.native.weapon_type==4&&out.native.weapon_class==9,"native gas type4 class9 is a supported handheld profile");
        gas.put<int>(Memory::definition+0x1c,0);check(gas.read(&out)==cp::Status::context&&out==cp::Copy{},"gas class cannot masquerade as ordinary bullet weapon");}
    for(int fire=0;fire<=7;++fire){Memory m(0);m.put<int>(Memory::definition+0x30,fire);cp::Copy value{};
        check((m.read(&value)==cp::Status::accepted)==(fire<=6),"full native firing enum includes stacked and minigun with zero stack count");}
}
p::NeutralSample neutral_sample(){p::NeutralSample s{};s.focused=s.dominant_tracking=s.menu_active=true;s.squeeze_active={true,true};for(auto& t:s.trigger)t.analog_active=true;return s;}
void state_tests() {
    Memory memory(3);cp::Copy current{};check(memory.read(&current)==cp::Status::accepted,"rearm actual reader baseline");
    p::RearmKey key{};key.profile=current.profile;key.identity=current.identity;key.gameplay_epoch=3;key.xr_generation=4;key.owner_thread=current.thread;key.anchor_bits[6]=0x3f800000;
    auto sample=neutral_sample();p::RearmState state;
    check(state.observe(key,10,sample,true)==p::RearmResult::wait_neutral,"initial sample cannot arm");
    check(state.observe(key,10,sample,true)==p::RearmResult::wait_neutral,"duplicate neutral cannot arm");
    check(state.observe(key,11,sample,true)==p::RearmResult::neutral_observed,"fresh neutral arms without aiming");
    check(state.observe(key,11,sample,true)==p::RearmResult::ready,"later occurrence can use same fresh action identity");
    auto held=sample;held.trigger[1].analog=.8F;held.squeeze={.8F,.8F};
    check(state.observe(key,12,held,true)==p::RearmResult::ready,"held controls on same armed profile");
    auto new_key=key;new_key.identity.object+=0x7c;
    const auto stable_revision=state.revision();
    check(state.observe(new_key,13,held,true)==p::RearmResult::ready&&state.armed()&&state.revision()==stable_revision,
        "fresh model allocation retains armed same-owner controls without issuing admission");
    Memory replacement(0);cp::Copy replaced{};check(replacement.read(&replaced)==cp::Status::accepted,"replacement uses actual full profile reader");
    new_key.profile=replaced.profile;new_key.identity=replaced.identity;
    check(state.observe(new_key,14,held,true)==p::RearmResult::ready&&state.revision()==stable_revision,
        "fresh different weapon and assembly retain established input intent");
    state.revoke(15);check(state.observe(new_key,15,sample,true)==p::RearmResult::wait_neutral,"restore after explicit revoke is not revival");
    check(state.observe(new_key,15,sample,true)==p::RearmResult::wait_neutral,"same neutral still consumed");
    check(state.observe(new_key,16,sample,true)==p::RearmResult::neutral_observed,"next neutral can rearm");
    auto lost=sample;lost.dominant_tracking=false;check(state.observe(new_key,17,lost,true)==p::RearmResult::refused&&!state.armed(),"dominant tracking loss revokes");
    for(unsigned field=0;field<9;++field){p::RearmState isolated;
        isolated.observe(key,1,sample,true);isolated.observe(key,2,sample,true);isolated.observe(key,2,sample,true);
        auto changed_owner=key;
        switch(field){case 0:++changed_owner.gameplay_epoch;break;case 1:++changed_owner.xr_generation;break;
        case 2:++changed_owner.invalidation;break;case 3:++changed_owner.owner_thread;break;
        case 4:++changed_owner.anchor_bits[0];break;case 5:++changed_owner.identity.cg;break;
        case 6:++changed_owner.identity.owner;break;case 7:++changed_owner.identity.server;break;
        case 8:++changed_owner.identity.arena;break;}
        check(!p::same_control_owner(key,changed_owner)&&isolated.observe(changed_owner,3,held,true)==p::RearmResult::wait_neutral&&
            !isolated.armed(),"lifecycle owner identity hard rearm survives profile-refresh policy");
        check(isolated.observe(changed_owner,4,held,true)==p::RearmResult::wait_neutral,
            "held controls cannot arm a replaced lifecycle owner");
    }
    {p::RearmState disarmed;disarmed.observe(key,1,sample,true);
        check(disarmed.observe(new_key,2,held,true)==p::RearmResult::wait_neutral&&!disarmed.armed(),
            "valid profile refresh cannot arm controls that were never neutral");}
    {p::RearmState rebased;auto turned=key;turned.anchor_bits[4]=0x3e800000;turned.anchor_bits[6]=0x3f780000;
        check(!rebased.rebase_anchor(key,turned)&&!rebased.armed(),"owned rebase cannot manufacture initial neutral intent");
        rebased.observe(key,1,sample,true);rebased.observe(key,2,sample,true);
        const auto revision=rebased.revision();
        check(rebased.rebase_anchor(key,turned)&&rebased.armed()&&rebased.revision()==revision&&
            rebased.observe(turned,3,held,true)==p::RearmResult::ready,
            "explicit same-owner yaw rebase preserves armed grip and trigger without issuing admission");
        check(!rebased.rebase_anchor(key,turned),"already consumed old anchor receipt cannot be reused");
        rebased.revoke(4);check(!rebased.rebase_anchor(turned,key)&&!rebased.armed(),"rebase cannot revive revoked intent");}
    for(unsigned field=0;field<12;++field){p::RearmState isolated;isolated.observe(key,1,sample,true);isolated.observe(key,2,sample,true);
        auto changed=key;changed.anchor_bits[4]=0x3e800000;
        switch(field){case 0:++changed.gameplay_epoch;break;case 1:++changed.xr_generation;break;
        case 2:++changed.invalidation;break;case 3:++changed.owner_thread;break;case 4:++changed.anchor_bits[0];break;
        case 5:++changed.identity.cg;break;case 6:++changed.identity.owner;break;case 7:++changed.identity.server;break;
        case 8:++changed.identity.arena;break;case 9:++changed.identity.server_link;break;
        case 10:++changed.identity.tracking_mode;break;case 11:++changed.profile.row;break;}
        check(!isolated.rebase_anchor(key,changed),"owned yaw rebase cannot change context, position, identity or profile");
    }
    check(!p::neutral(held),"held trigger and squeeze not neutral");
    for(float value:{-.1F,.65F,1.F,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){auto bad=sample;bad.squeeze[0]=value;check(!p::neutral(bad),"squeeze neutral bounds");}
    auto bad=sample;bad.trigger[1].analog=.55F;check(!p::neutral(bad),"trigger threshold is pressed");
    bad=sample;bad.menu_active=false;check(!p::neutral(bad),"missing menu action not neutral");
    bad=sample;bad.trigger[0].analog_active=false;check(!p::neutral(bad),"missing trigger action not neutral");
    bad=sample;bad.trigger[0].click_active=true;bad.trigger[0].click_pressed=true;check(!p::neutral(bad),"click overrides released analogue");
    new_key.invalidation=UINT64_MAX;check(!p::valid(new_key),"poison invalidation never valid");
    check(state.observe(key,9,sample,true)==p::RearmResult::refused&&!state.armed(),"backwards action sequence refuses");
    auto fresh=key;fresh.xr_generation=5;
    check(state.observe(fresh,1,sample,true)==p::RearmResult::wait_neutral,"new verified XR generation can restart actions");
    check(state.observe(fresh,1,sample,true)==p::RearmResult::wait_neutral,"new generation duplicate remains disarmed");
    check(state.observe(fresh,2,sample,true)==p::RearmResult::neutral_observed,"fresh new-generation neutral rearms");
    std::uint64_t revision=UINT64_MAX-1;
    check(!p::advance_revision(revision)&&revision==UINT64_MAX,"revision exhaustion latches poison");
    check(!p::advance_revision(revision)&&revision==UINT64_MAX,"revision cannot wrap or revive");
}
void linked_weapon_tests() {
    const auto ps=Memory::cg+0x8a364, entity=0x1a796f8U, server_link=0x306ed00U;
    auto linked = [&]() {
        Memory m(0);
        m.pre.context = m.request.command;
        m.pre.native_gameplay_fields_valid=true;
        m.pre.gameplay_active=false;
        m.pre.linked_weapon_active=true;
        m.pre.native_pm_type=1;
        m.put<int>(ps+4,1);
        m.put<U32>(ps+0xc,0x1800000U);
        m.put<U32>(ps+0x10,0);
        m.put<U32>(ps+0xe0,0x80002U);
        m.put<int>(Memory::server+4,1);
        m.put<U32>(Memory::server+0xc,0x1800000U);
        m.put<U32>(Memory::server+0x10,0);
        m.put<U32>(Memory::server+0xe0,0x80002U);
        m.put<U32>(entity+0x298,server_link);
        return m;
    };
    Memory baseline_memory = linked();
    cp::Copy linked_accept{};
    check(baseline_memory.read(&linked_accept)==cp::Status::accepted,"linked mode baseline reads accepted");
    check(linked_accept.identity.server_link==server_link,"linked mode copies server_link identity");
    check(linked_accept.identity.tracking_mode==2,"linked mode capture is tracking mode 2");
    Memory replay = linked();
    cp::Copy replay_accept{};
    check(replay.read(&replay_accept)==cp::Status::accepted,"linked mode replay reads accepted");
    check(cp::same_current(linked_accept,replay_accept),"linked mode preserves exact model/copy across replay");
    {
        Memory frozen = linked();
        frozen.put<U32>(ps+0xc,0x1800800U);
        frozen.put<U32>(Memory::server+0xc,0x1800800U);
        cp::Copy out{};
        check(frozen.read(&out)==cp::Status::context&&out==cp::Copy{},"frozen linked movement flags reject linked read");
    }
    {
        Memory primary_disabled = linked();
        primary_disabled.put<U32>(ps+0x10,0x80U);
        primary_disabled.put<U32>(Memory::server+0x10,0x80U);
        cp::Copy out{};
        check(primary_disabled.read(&out)==cp::Status::context&&out==cp::Copy{},"primary-disabled weapon flag rejects linked read");
    }
    {
        Memory disagreement = linked();
        disagreement.put<int>(ps+4,0);
        cp::Copy out{};
        check(disagreement.read(&out)==cp::Status::context&&out==cp::Copy{},"client/server pm mismatch rejects linked read");
    }
    {
        Memory missing_link = linked();
        missing_link.put<U32>(entity+0x298,0);
        cp::Copy out{};
        check(missing_link.read(&out)==cp::Status::context&&out==cp::Copy{},"missing linked weapon identity rejects linked read");
    }
    {
        Memory pre_linked_off = linked();
        pre_linked_off.pre.linked_weapon_active=false;
        cp::Copy out{};
        check(pre_linked_off.read(&out)==cp::Status::command&&out==cp::Copy{},"non-linked precommand rejects linked read");
    }
    {
        Memory override = linked();
        cp::Copy out{};
        override.put<U8>(0x2911e2c,1);
        check(override.read(&out)==cp::Status::context&&out==cp::Copy{},"command native override rejects linked read");
    }
    {
        Memory hidden = linked();
        hidden.put<U8>(Memory::visibility+0x18,0);
        cp::Copy out{};
        check(hidden.read(&out)==cp::Status::context&&out==cp::Copy{},"hidden visibility rejects linked read");
    }
    {
        Memory no_weapon = linked();
        no_weapon.put<U8>(ps+0x144,0);
        cp::Copy out{};
        check(no_weapon.read(&out)==cp::Status::profile&&out==cp::Copy{},"zero equipped weapon rejects linked read");
    }
    {
        Memory offhand_only = linked();
        offhand_only.put<U32>(ps+0x10,0x100U);
        offhand_only.put<U32>(Memory::server+0x10,0x100U);
        cp::Copy out{};
        check(offhand_only.read(&out)==cp::Status::accepted&&cp::same_current(linked_accept,out),
            "offhand-only bit does not reject linked camera-movement mode");
    }
    {
        Memory mutation = linked();
        cp::Copy out{};
        check(mutation.read(&out)==cp::Status::accepted,"linked mutation baseline accepted");
        for(std::size_t n=0;n<mutation.reads.size();++n)if(mutation.reads[n].at==entity+0x298){
            mutation.mutate_call=n+1;
            break;
        }
        check(mutation.mutate_call!=0,"linked entity link pointer mutation is scheduled between passes");
        mutation.mutate_address=entity+0x298;
        mutation.mutate_value=4;
        check(mutation.read(&out)==cp::Status::changed&&out==cp::Copy{},"bounded linked server-link change between copies rejects with changed");
    }
}
}
int main(){reader_tests();runtime_reader_tests();dual_reader_tests();state_tests();linked_weapon_tests();diagnostic_tests();std::printf("command_profile_checks=%u failures=%u actual_game_calls=0 gpu_calls=0\n",checks,failures);return failures?1:0;}
