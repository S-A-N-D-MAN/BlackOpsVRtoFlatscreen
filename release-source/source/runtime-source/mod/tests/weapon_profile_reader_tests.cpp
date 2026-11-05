// SPDX-License-Identifier: GPL-3.0-only
#include "t5/weapon_profile_reader.hpp"
#include "t5/profile_record_output.hpp"
#include "t5/weapon_profile_probe.hpp"
#include "t5/identity.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <stdexcept>

namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {bool read_pre_command_snapshot(PreCommandSnapshot*) noexcept {return false;}}
namespace {
using namespace bovr::t5;
using namespace bovr::t5::profile_detail;
using U8=std::uint8_t;using U16=std::uint16_t;using U32=std::uint32_t;using Addr=std::uintptr_t;
unsigned checks{},failures{};
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
constexpr Addr cg=0x4000000,owner=0x5000000,variant=0x5100000,definition=0x5200000,server=0x5300000;
constexpr Addr table=0x5400000,model0=0x5500000,model1=0x5501000,text0=0x5600000,text1=0x5601000;
constexpr Addr names0=0x5700000,names1=0x5701000,parents0=0x5800000,parents1=0x5801000,bind0=0x5900000,bind1=0x5902000;
constexpr Addr arena=0x6000000,dobj=0x2489148+0x7c;
struct Fake {
    std::map<Addr,std::vector<U8>> memory;
    NativeWeaponContext c{};PreCommandSnapshot pre{};
    unsigned reads{},command_reads{},fail_at{},change_at{},clock_reads{};
    std::uint64_t clock=1000;
    bool command_ok=true,change_command{},throw_copy{},throw_clock{};
    std::function<void(Fake&,Addr,std::size_t)> mutate;
    void block(Addr a,std::size_t n){memory[a]=std::vector<U8>(n);}
    U8* pointer(Addr a,std::size_t n) {
        auto i=memory.upper_bound(a);if(i==memory.begin())return nullptr;--i;
        const auto offset=static_cast<std::uint64_t>(a)-i->first;
        if(offset>i->second.size()||n>i->second.size()-offset)return nullptr;
        return i->second.data()+static_cast<std::size_t>(offset);
    }
    void raw(Addr a,const void* p,std::size_t n){auto* out=pointer(a,n);if(!out){block(a,n);out=pointer(a,n);}memcpy(out,p,n);}
    template<class T>void put(Addr a,const T& value){raw(a,&value,sizeof(value));}
    void text(Addr a,const char* s){raw(a,s,strlen(s)+1);}
    void script(unsigned id,const char* s){put<U16>(arena+16*id,1);text(arena+16*id+4,s);}
    void bind(Addr a,float x=0,float y=0,float z=0){const float values[]{0,0,0,1,x,y,z,2};raw(a,values,sizeof(values));}
    Fake() {
        block(cg,0xc6000);block(owner,0x100);block(variant,0x100);block(definition,0x600);block(server,0x1d00);
        block(dobj,0x7c);block(table,256);block(model0,64);block(model1,64);block(text0,512);block(text1,512);
        block(names0,320);block(names1,320);block(parents0,160);block(parents1,160);block(bind0,5120);block(bind1,5120);block(arena,65536);
        block(0x1a796f8,0x300);
        c.valid=c.body_valid=c.normal_context=c.server_normal=c.primary_viewmodel=c.weapon_definition_valid=true;
        c.call_serial=45;c.native_thread=17;c.draw_gun=1;c.weapon_index=4;c.command_number=10;c.command_clock=3000;c.native_time=c.refdef_time=3500;
        c.cg_identity=cg;c.refdef_identity=cg+0x8c100;c.viewmodel_identity=owner;c.dobj_identity=dobj;
        c.variant_identity=variant;c.definition_identity=definition;c.placement.scale=1;c.stock_view.origin={100,200,300};
        pre.context.valid=true;pre.context.native_thread=17;pre.context.connection_state=10;pre.context.command_number=10;
        pre.context.native_frame_time=3000;pre.sequence=33;pre.observed_ms=1000;pre.gameplay_epoch=7;
        pre.native_cg_identity=cg;pre.native_gameplay_fields_valid=pre.gameplay_active=true;
        put<Addr>(0x2ff5354,cg);put<Addr>(0xc1c6d8,owner);put<Addr>(owner,dobj);put<short>(0x24e6150+1536*2,1);
        put<U8>(0x2488548+1,1);put<Addr>(0xbe19a8+4*4,variant);put<Addr>(variant+8,definition);
        const auto ps=cg+0x8a364;put<int>(ps+0x1c4,100);put<U8>(ps+0x144,4);
        put<int>(0x2910160,0);put<int>(0x2910164,10);put<int>(0x2911e2c,0);
        put<int>(cg+0x8a348,3500);put<int>(cg+0x8c158,3500);put<int>(0x2481764,3000);put<int>(0x2993850,10);
        put<Addr>(0x1a796f8+0x13c,server);put<Addr>(0x1c03140,server);put<int>(0x1a796f8+0x184,100);
        put<int>(server+0x1c4,100);put<int>(server+0x1a2c,2);
        put(c.refdef_identity+0x20,c.stock_view.origin);put(c.refdef_identity+0x34,c.stock_view.axis);
        put<U8>(dobj+9,2);put<U8>(dobj+0xa,3);put<Addr>(dobj+0x78,table);put<U16>(0x23a566e,30);put<Addr>(0x3067c00,arena);
        put<U16>(dobj+4,40);put<U8>(dobj+8,21);put<U16>(arena+16*40,1);
        put<Addr>(table,model0);put<Addr>(table+4,model1);put<U8>(table+8,0xff);put<U8>(table+9,0xff);
        for(unsigned i=0;i<2;++i) {
            const Addr m=i?model1:model0;
            put<Addr>(m,i?text1:text0);put<U8>(m+4,static_cast<U8>(i?2:1));put<U8>(m+5,1);
            put<Addr>(m+8,i?names1:names0);put<Addr>(m+0xc,i?parents1:parents0);put<Addr>(m+0x1c,i?bind1:bind0);
        }
        text(text0,"viewmodel_hands");text(text1,"viewmodel_m1911");script(10,"tag_origin");script(30,"tag_flash");
        put<U16>(names0,10);put<U16>(names1,10);put<U16>(names1+2,30);put<U8>(parents1,1);
        bind(bind0);bind(bind1);bind(bind1+32,12.5F,-2,.25F);
    }
    static constexpr Addr model2=0x5502000,text2=0x5602000,names2=0x5702000,parents2=0x5802000,bind2=0x5910000;
    static constexpr Addr companion_variant=0x6200000,companion_definition=0x6300000,companion_models=0x6400000;
    void linked(){
        c.normal_context=c.server_normal=false;c.linked_weapon_context=c.linked_server_context=true;
        c.server_link_identity=0x23450000;pre.gameplay_active=false;pre.linked_weapon_active=true;pre.native_pm_type=1;
        for(const Addr ps:{cg+0x8a364,server}){put<int>(ps+4,1);put<U32>(ps+0xc,0x1800000);put<U32>(ps+0x10,8);put<U32>(ps+0xe0,0x80002);}
        put<U32>(0x1a796f8+0x298,c.server_link_identity);
    }
    void dual(){
        c.dual_wield=true;block(model2,64);block(text2,512);block(names2,320);block(parents2,160);block(bind2,5120);
        block(companion_variant,16);block(companion_definition,0x600);block(companion_models,4);
        put<U8>(definition+0x56a,1);put<U32>(definition+0x594,5);put<Addr>(0xbe19a8+4*5,companion_variant);
        put<Addr>(companion_variant+8,companion_definition);put<U8>(companion_definition+0x56a,1);
        put<U32>(companion_definition+0x594,4);put<Addr>(companion_definition+4,companion_models);put<Addr>(companion_models,model2);
        put<U8>(model0+4,2);script(11,"tag_weapon1");put<U16>(names0+2,11);put<U8>(parents0,1);bind(bind0+32);
        put<Addr>(model2,text2);text(text2,"viewmodel_left_pistol");put<U8>(model2+4,2);put<U8>(model2+5,1);
        put<Addr>(model2+8,names2);put<Addr>(model2+0xc,parents2);put<Addr>(model2+0x1c,bind2);
        script(50,"tag_flash1");put<U16>(names2,10);put<U16>(names2+2,50);put<U8>(parents2,1);bind(bind2);bind(bind2+32,12.5F,2,.25F);
        put<U8>(dobj+9,3);put<U8>(dobj+0xa,6);put<Addr>(table+8,model2);
        put<U8>(table+12,255);put<U8>(table+13,255);put<U8>(table+14,1);
    }
    static bool copy(void* p,Addr a,void* out,std::size_t n) {
        auto& f=*static_cast<Fake*>(p);++f.reads;
        if(f.throw_copy)throw std::runtime_error("copy provider fixture");
        if(f.fail_at&&f.reads==f.fail_at)return false;
        if(f.mutate)f.mutate(f,a,n);
        const auto* source=f.pointer(a,n);if(!source)return false;memcpy(out,source,n);return true;
    }
    static std::uint64_t now(void* p){auto& f=*static_cast<Fake*>(p);++f.clock_reads;if(f.throw_clock)throw std::runtime_error("clock fixture");return f.clock;}
    static bool command(void* p,PreCommandSnapshot* out){auto& f=*static_cast<Fake*>(p);*out=f.pre;if(++f.command_reads>1&&f.change_command)++out->sequence;return f.command_ok;}
    ReadHost host(){return {this,copy,now,command};}
    ReadStatus run(ProfileRecord* out){return read_profile(host(),c,17,1000,out);}
};
void require_reject(Fake& f,const char* message){ProfileRecord out;const auto status=f.run(&out);check(status!=ReadStatus::accepted&&out.canonical.assembly_payload.empty()&&out.copies.before.empty(),message);}
}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);
    Fake f;ProfileRecord good;const auto initial=f.run(&good);check(initial==ReadStatus::accepted,"actual reader over guarded fake addresses accepts canonical copies");
    if(initial!=ReadStatus::accepted){std::printf("initial_status=%u identity=%u reads=%u\n",static_cast<unsigned>(initial),static_cast<unsigned>(good.identity_status),f.reads);return 1;}
    {Fake reference;ProfileRecord copied;auto host=reference.host();host.optimized_identity=false;ReadWorkCounts counts{};
      check(read_profile(host,reference.c,17,1000,&copied,&counts)==ReadStatus::accepted&&reference.reads==f.reads&&
        reference.command_reads==f.command_reads&&reference.clock_reads==f.clock_reads&&counts.canonical_builds==1&&
        copied.canonical.model_payloads==good.canonical.model_payloads&&copied.canonical.assembly_payload==good.canonical.assembly_payload&&
        copied.assembly==good.assembly&&copied.command_sequence==good.command_sequence,
        "optimized and reference actual native readers retain every protected read command check tail copy and exact canonical byte");}
    for(int count:{0,1,2,4,7}) {Fake single;single.c.stack_count=count;single.put<U32>(cg+0x8a364+0x1b0,static_cast<U32>(count));ProfileRecord out;
        check(single.run(&out)==ReadStatus::accepted&&out.canonical.assembly_payload==good.canonical.assembly_payload,
            "retained native stacked-fire count preserves exact borrowed weapon geometry");}
    for(int count:{8,-1,(-2147483647-1)}) {Fake invalid;invalid.c.stack_count=count;
        require_reject(invalid,"out-of-range stack count cannot begin a borrowed model read");check(!invalid.reads,"invalid stack rejects before native reads");}
    for(unsigned pass:{2u,3u})for(U32 initial:{0u,1u,4u}) {Fake changed;changed.c.stack_count=static_cast<int>(initial);
        changed.put<U32>(cg+0x8a364+0x1b0,initial);unsigned starts=0;
        changed.mutate=[&](Fake& memory,Addr address,std::size_t){if(address==0x2ff5354&&++starts==pass)memory.put<U32>(cg+0x8a364+0x1b0,initial==1?4u:1u);};
        require_reject(changed,"stack count changed after first or second borrow pass rejects the copied profile");}
    for(const U32 flags:{U32{8},U32{0},U32{0x100}}){Fake linked;linked.linked();
        linked.put<U32>(cg+0x8a364+0x10,flags);linked.put<U32>(server+0x10,flags);ProfileRecord out;
        check(linked.run(&out)==ReadStatus::accepted&&out.canonical.assembly_payload==good.canonical.assembly_payload,
              "actual copied reader admits enabled linked primary geometry without ordinary command gameplay");}
    for(unsigned fault=0;fault<7;++fault){Fake bad;bad.linked();
        switch(fault){case 0:bad.put<int>(server+4,0);break;case 1:bad.put<U32>(0x1a796f8+0x298,0);break;
        case 2:bad.put<U32>(cg+0x8a364+0x10,0x80);break;case 3:bad.put<U32>(server+0xc,0x1800400);break;
        case 4:bad.put<int>(cg+0x8a360,1);break;case 5:bad.pre.linked_weapon_active=false;break;
        case 6:bad.c.weapon_state=21;bad.put<int>(cg+0x8a364+0x158,21);break;}
        require_reject(bad,"linked tracking rejects mixed modes absent link disabled frozen camera and offhand contexts");}
    for(unsigned pass:{2u,3u})for(unsigned change=0;change<3;++change){Fake changed;changed.linked();unsigned starts=0;
        changed.mutate=[&](Fake& m,Addr address,std::size_t){if(address!=0x2ff5354||++starts!=pass)return;
            if(change==0)m.put<U32>(0x1a796f8+0x298,0x23450004);
            if(change==1){m.put<int>(cg+0x8a364+4,0);m.put<int>(server+4,0);m.put<U32>(0x1a796f8+0x298,0);}
            if(change==2)m.put<int>(server+4,0);};
        require_reject(changed,"second or final native copy cannot change primary mode or link identity");}
    for(const bool other:{false,true})for(int state=-1;state<53;++state) {
        Fake animation_state;ProfileRecord out;
        (other?animation_state.c.other_weapon_state:animation_state.c.weapon_state)=state;
        animation_state.put<int>(cg+0x8a364+(other?0x15c:0x158),state);
        const auto status=animation_state.run(&out);
        const bool accepted_state=(state>=0&&state<18)||(state>=28&&state<=33)||(state>=48&&state<=50);
        check((status==ReadStatus::accepted)==accepted_state,"actual render profile reader accepts pinned primary states and rejects previous/offhand/unknown states");
        if(accepted_state)check(out.canonical.assembly_payload==good.canonical.assembly_payload,
            "sprint, continuous fire and low-ready retain the exact copied model assembly");
        else check(!animation_state.reads,"unsupported animation state cannot begin a native model borrow");
    }
    for(int state=21;state<=26;++state) {
        Fake grenade_visual;grenade_visual.c.weapon_state=state;grenade_visual.c.manual_grenade_primary_visual=true;
        grenade_visual.put<int>(cg+0x8a364+0x158,state);ProfileRecord out;
        check(grenade_visual.run(&out)==ReadStatus::accepted&&out.canonical.assembly_payload==good.canonical.assembly_payload,
              "exact manual grenade primary visual admits each native offhand state to the copied profile reader");
        Fake secondary_transition;secondary_transition.c.other_weapon_state=state;
        secondary_transition.c.manual_grenade_primary_visual=true;secondary_transition.put<int>(cg+0x8a364+0x15c,state);
        require_reject(secondary_transition,"manual grenade primary visual cannot admit a secondary weapon transition state");
        check(!secondary_transition.reads,"secondary transition rejection occurs before any native model borrow");
    }
    {Fake shadow;shadow.put<int>(cg+0x8a360,2);ProfileRecord out;
      check(shadow.run(&out)==ReadStatus::accepted&&out.canonical.assembly_payload==good.canonical.assembly_payload,
            "actual shadow camera keeps identical canonical weapon identity");}
    for(int mode:{1,-1,3}){Fake bad;bad.put<int>(cg+0x8a360,mode);require_reject(bad,"actual profile reader rejects model and unknown cameras");}
    for(Addr offset:{Addr{0xc},Addr{0xa9d44}}){Fake bad;bad.put<int>(cg+0x8a360,2);bad.put<int>(cg+offset,1);
      require_reject(bad,"shadow camera does not bypass independent camera exclusions");}
    {Fake bad;bad.put<int>(cg+0x8a360,2);bad.pre.observed_ms=1;require_reject(bad,"shadow camera does not refresh stale command identity");}
    {Fake pair;pair.dual();ProfileRecord out;check(pair.run(&out)==ReadStatus::accepted&&out.canonical.model_payloads.size()==3&&
        out.canonical.selected_model==1&&out.copies.after[2].bones[1].name.bytes=="tag_flash1","native reader admits exact companion model2 and its distinct left muzzle");}
    for(unsigned scenario=0;scenario<7;++scenario){Fake pair;pair.dual();ProfileRecord out;
        pair.c.weapon_type=2;pair.c.weapon_class=4;pair.put<int>(definition+0x1c,2);pair.put<int>(definition+0x20,4);
        pair.put<int>(Fake::companion_definition+0x1c,2);pair.put<int>(Fake::companion_definition+0x20,4);
        switch(scenario){
        case 1:pair.put<int>(Fake::companion_definition+0x1c,0);break;
        case 2:pair.put<int>(Fake::companion_definition+0x20,3);break;
        case 3:pair.c.weapon_class=2;pair.put<int>(definition+0x20,2);break;
        case 4:pair.put<U32>(Fake::companion_definition+0x594,5);break;
        case 5:pair.put<Addr>(Fake::companion_models,model1);break;
        case 6:pair.put<U8>(table+14,0);break;
        }
        if(!scenario)check(pair.run(&out)==ReadStatus::accepted&&out.canonical.model_payloads.size()==3&&
            out.copies.after[2].bones[1].name.bytes=="tag_flash1",
            "native projectile pistol render reader admits exact reciprocal companion and distinct muzzle");
        else require_reject(pair,"projectile pair reader rejects mixed types wrong classes and unowned companion assembly");
    }
    for(unsigned bad_case=0;bad_case<6;++bad_case){Fake pair;pair.dual();
        switch(bad_case){case 0:pair.put<Addr>(Fake::companion_models,model1);break;
        case 1:pair.put<U32>(Fake::companion_definition+0x594,5);break;
        case 2:pair.put<U8>(table+14,0);break;case 3:pair.script(50,"tag_flash_unknown");break;
        case 4:pair.put<U8>(Fake::parents2,0);break;case 5:pair.put<U8>(definition+0x56a,0);break;}
        require_reject(pair,"dual native model ordinal parent muzzle and reciprocal identity checks reject mismatches");}
    check(good.canonical.model_payloads.size()==2&&good.canonical.selected_model==1&&good.canonical.selected_local_bone==1,"reader retains ordered model and unique canonical barrel ordinals");
    check(sha256(good.canonical.model_payloads[1])=="15a55a94e3e4344b369eeef1e740714afc1ae4c6e9f9f2d73bfbd7ef9950ea95","reader produces independently constructed v1 asset fixture exactly");
    check(sha256(good.canonical.assembly_payload)=="9c62359186effca5470b38168c6250232f44c3a58df2da7d0a408a9dac44f4f3","reader leaves ordered-model v1 schema byte-identical");
    check(good.copies.before_stamp.borrow_serial==45&&good.copies.after_stamp.native_epoch==7&&good.command_sequence==33,"native scope and original command epoch retained as evidence only");
    const unsigned baseline_reads=f.reads;
    for(unsigned n=1;n<=baseline_reads;++n){Fake bad;bad.fail_at=n;require_reject(bad,"every individual native-copy failure rejects with empty identity");}
    for(unsigned n=0;n<21;++n){Fake bad;
        switch(n){case 0:bad.c.valid=false;break;case 1:bad.c.body_valid=false;break;case 2:bad.c.server_normal=false;break;
        case 3:bad.c.call_serial=0;break;case 4:bad.c.native_thread++;break;case 5:bad.c.weapon_type=1;break;
        case 6:bad.c.dual_wield=true;bad.c.weapon_type=2;break;case 7:bad.c.weapon_state=18;break;case 8:bad.c.other_weapon_state=18;break;
        case 9:bad.c.stack_count=8;break;case 10:bad.c.cg_identity=UINT32_MAX;break;case 11:bad.c.draw_gun=0;break;
        case 12:bad.c.command_number=-1;break;case 13:bad.c.placement.scale=2;break;case 14:bad.c.stock_view.axis.up.z=-1;break;
        case 15:bad.c.local_client=1;break;case 16:bad.c.weapon_index=256;break;case 17:bad.c.refdef_time++;break;
        case 18:bad.c.primary_viewmodel=false;break;case 19:bad.c.normal_context=false;break;case 20:bad.c.weapon_definition_valid=false;break;}
        require_reject(bad,"unsupported copied weapon context never reads native assets");check(!bad.reads,"context rejection causes zero native memory reads");}
    for(unsigned n=0;n<15;++n){Fake bad;
        switch(n){case 0:bad.pre.gameplay_active=false;break;case 1:bad.pre.gameplay_epoch=0;break;case 2:bad.pre.sequence=0;break;
        case 3:bad.pre.observed_ms=899;break;case 4:bad.pre.observed_ms=1001;break;case 5:bad.pre.context.command_number=8;break;
        case 6:bad.pre.context.native_thread++;break;case 7:bad.pre.context.key_catcher=16;break;case 8:bad.pre.context.native_override=true;break;
        case 9:bad.pre.context.connection_state=8;break;case 10:bad.pre.native_cg_identity++;break;case 11:bad.pre.native_gameplay_fields_valid=false;break;
        case 12:bad.pre.context.valid=false;break;case 13:bad.pre.context.local_client=1;break;case 14:bad.command_ok=false;break;}
        require_reject(bad,"missing or stale pre-command rejects before asset read");check(!bad.reads,"pre-command rejection causes zero native memory reads");}
    // Every native guard type is tested through the real reader, not a fake
    // 'borrow succeeded' boolean. Fields are restored by independent fixtures.
    const std::array<std::pair<Addr,U32>,36> invalid_ints{{
        {0x2ff5354,cg+4},{0xc1c6d8,owner+4},{owner,dobj+4},{0xbe19a8+16,variant+4},{variant+8,definition+4},
        {cg+0x8a364+4,1},{cg+0x8a364+0x1c4,0},{cg+0x8a364+0x158,1},{cg+0x8a364+0x15c,1},
        {cg+0x8a364+0x1b0,1},{cg+0x8a364+0xc,0x800},{cg+0x8a364+0x10,0x180},{cg+0x8a364+0xe0,0x4300},
        {cg+0x8a364+0xe4,0x40040000},{cg+0x8a360,1},{cg+0xc,1},{cg+0xa9d44,1},{0x2910160,16},{0x2910164,8},
        {0x2911e2c,1},{cg+0x8a348,3501},{cg+0x8c158,3501},{0x2481764,3001},{0x2993850,11},
        {0x1a796f8+0x13c,server+4},{0x1c03140,server+4},{0x1a796f8,1},{0x1a796f8+0x184,0},{0x1a796f8+0x298,1},
        {server+4,1},{server+0x1c4,0},{server+0x1a2c,1},{server+0x1a18,1},{server+0x1c0c,0x17},{server+0xc,0x800},{server+0x10,0x180}
    }};
    for(const auto& [address,value]:invalid_ints){Fake bad;bad.put(address,value);require_reject(bad,"actual copied local/native/server guard rejects unsupported identity or state");}
    for(const auto address:{server+0xe0,server+0xe4}){Fake bad;bad.put<U32>(address,0xffffffff);require_reject(bad,"remaining server flag masks reject");}
    for(const auto address:{cg+0x8a364+0x130,server+0x130,cg+0x8a364+0x144}){Fake bad;bad.put<U8>(address,3);require_reject(bad,"client and weapon byte identities reject");}
    for(const short index:{short(0),short(-1),short(0xc00),short(2)}){Fake bad;bad.put<short>(0x24e6150+1536*2,index);require_reject(bad,"pool free out-of-range or reassigned pointer rejects");}
    {Fake bad;bad.put<U8>(0x2488549,0);require_reject(bad,"freed native pool slot rejects");}
    for(const auto address:{dobj+9,dobj+0xa,model0+4,model0+5}){Fake bad;bad.put<U8>(address,0);require_reject(bad,"zero model bone or root counts reject");}
    for(const auto address:{dobj+9,dobj+0xa,model0+4,model0+5}){Fake bad;bad.put<U8>(address,255);require_reject(bad,"oversized model bone or root counts reject before allocation");}
    for(const auto address:{dobj+0x78,table,model0,model0+8,model0+0x1c,model1+0xc,Addr(0x3067c00)}){Fake bad;bad.put<Addr>(address,0);require_reject(bad,"null native array name arena or bind pointer rejects");}
    {Fake bad;bad.put<Addr>(model0+0x1c,UINT32_MAX-15);require_reject(bad,"overflowing bind range rejects before host copy");}
    {Fake bad;bad.put<Addr>(0x3067c00,UINT32_MAX-15);require_reject(bad,"overflowing script entry rejects before host copy");}
    for(const auto address:{Addr(0x23a566e),names0,names1+2,arena+16*10,arena+16*30,arena+16*40,dobj+4}){Fake bad;bad.put<U16>(address,0);require_reject(bad,"zero tag handle bone handle or script refcount rejects");}
    {Fake bad;bad.text(arena+16*30+4,"other_tag");require_reject(bad,"matching raw tag ID without canonical tag_flash name rejects");}
    {Fake bad;bad.put<U16>(names0,30);require_reject(bad,"ambiguous native tag ordinal rejects");}
    {Fake bad;bad.text(text0,"");require_reject(bad,"empty asset name rejects");}
    {Fake bad;bad.put<U8>(text0,1);require_reject(bad,"nonprintable asset name rejects");}
    {Fake bad;memset(bad.pointer(text0,256),'x',256);require_reject(bad,"overlong unterminated native asset name rejects");}
    {Fake bad;memset(bad.pointer(text0,255),'x',255);bad.put<U8>(text0+255,0);ProfileRecord out;check(bad.run(&out)==ReadStatus::accepted,"exact255byte terminated native name accepts");}
    {Fake bad;bad.put<U8>(parents1,0);require_reject(bad,"zero nonroot parent delta rejects");}
    {Fake bad;bad.put<U8>(parents1,2);require_reject(bad,"parent before model start rejects");}
    for(unsigned i=0;i<8;++i){Fake bad;bad.put<U32>(bind1+32+i*4,0x7fc00000);require_reject(bad,"every nonfinite bind field rejects via canonical reducer");}
    {Fake changed;unsigned starts=0;changed.mutate=[&](Fake& m,Addr a,std::size_t){if(a==0x2ff5354&&++starts==2)m.put<U32>(bind1+32+16,0x41500000);};require_reject(changed,"bind bytes changing between copies reject");}
    {Fake changed;unsigned starts=0;changed.mutate=[&](Fake& m,Addr a,std::size_t){if(a==0x2ff5354&&++starts==3)m.put<Addr>(model1+0x1c,bind0);};require_reject(changed,"post-copy storage recheck catches late bind pointer change");}
    {Fake changed;unsigned starts=0;changed.mutate=[&](Fake& m,Addr a,std::size_t){if(a==0x2ff5354&&++starts==2)m.put<U8>(table+9,0);};require_reject(changed,"assembly parent mutation between copies rejects");}
    {Fake changed;unsigned starts=0;changed.mutate=[&](Fake& m,Addr a,std::size_t){if(a==0x2ff5354&&++starts==2)m.put<U16>(arena+16*10,5);};ProfileRecord out;check(changed.run(&out)==ReadStatus::accepted&&out.canonical.assembly_payload==good.canonical.assembly_payload,"positive mutable reference count change does not change canonical identity");}
    {Fake changed;changed.change_command=true;require_reject(changed,"native command replacement during copy rejects");}
    for(const auto time:{999ULL,1101ULL}){Fake bad;bad.clock=time;require_reject(bad,"backwards or over100ms copy clock rejects");}
    {Fake edge;edge.pre.observed_ms=1000;edge.clock=1100;ProfileRecord out;check(edge.run(&out)==ReadStatus::accepted,"exact100ms copy and command age accepts");}
    {Fake bad;bad.throw_copy=true;require_reject(bad,"own copy-provider exception contains no native call");}
    {Fake bad;bad.throw_clock=true;require_reject(bad,"own clock-provider exception clears result");}
    // A separate raw assembly diagnostic never changes the v1 model packets.
    {Fake assembly;assembly.put<U8>(table+9,0);assembly.put<U8>(dobj+8,23);assembly.put<U32>(arena+16*40+4,0x20000000);
      assembly.put<U8>(arena+16*40+4+20,3);assembly.put<U8>(arena+16*40+4+21,1);assembly.put<U8>(arena+16*40+4+22,0);
      ProfileRecord out;check(assembly.run(&out)==ReadStatus::accepted,"native LE words and MSB-first child mask plus ordered encoded pair accept");
      check(out.canonical.assembly_payload==good.canonical.assembly_payload&&out.assembly!=good.assembly,"assembly diagnostic extension remains outside unchanged v1 manifest");
      for(unsigned i=0;i<160;++i){auto changed=out.assembly;changed.duplicate_data[(i/32)*4+(3-(i%32)/8)]^=static_cast<U8>(1U<<(7-i%8));check(!valid_assembly(changed,out.copies.after),"each of160 unmatched mask bits including unused bones rejects");}
      for(unsigned i=0;i<6;++i){auto changed=out.assembly;switch(i){case 0:changed.duplicate_data[20]=0;break;case 1:changed.duplicate_data[21]=0;break;case 2:changed.duplicate_data[21]=3;break;case 3:changed.duplicate_data[20]=4;break;case 4:changed.duplicate_data.back()=1;break;case 5:changed.model_parents[1]=1;break;}check(!valid_assembly(changed,out.copies.after),"bad encoded pair sentinel or model-parent ordinal rejects");}}
    {auto a=good.assembly;a.model_parents[0]=0;check(!valid_assembly(a,good.copies.after),"first model cannot attach to itself");}
    for(const U8 size:{U8(0),U8(20),U8(22),U8(254)}){Fake bad;bad.put<U8>(dobj+8,size);require_reject(bad,"binary size must be odd and at least21");}
    // Actual protected-copy wrapper contains access violations and does not
    // convert failures into a filled identity. This is CPU-only host memory.
    U32 copied{},source=0x12345678;
    check(protected_profile_copy(nullptr,reinterpret_cast<Addr>(&source),&copied,4)&&copied==source,"production protected copy reads owned host bytes");
    check(!protected_profile_copy(nullptr,0,&copied,4)&&!protected_profile_copy(nullptr,0x10000,&copied,4),"production protected copy rejects null and inaccessible host memory");
    SetLastError(0x1234);observe_weapon_profile_after(f.c);check(GetLastError()==0x1234&&!weapon_profile_probe_enabled(),"default-off public observer preserves LastError and reads no game");
    // Actual CREATE_NEW output and hashing, in a unique machine-fixture folder.
    wchar_t temporary[MAX_PATH]{};GetTempPathW(MAX_PATH,temporary);
    const auto folder=std::filesystem::path(temporary)/(L"bovr-profile-fixture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directory(folder);
    const auto first=write_record(folder.wstring(),GetCurrentProcessId(),1,good);
    check(first.complete&&std::filesystem::exists(first.json_path),"complete evidence writes JSON last with exact canonical payloads");
    const auto marker=read_file(first.json_path);const std::string json(marker.begin(),marker.end());
    check(json.find("viewmodel_m1911")!=std::string::npos&&json.find("tag_flash")!=std::string::npos&&json.find("12.5")!=std::string::npos&&json.find("included_in_v1_canonical\": false")!=std::string::npos,"readable names bind numbers and separate diagnostic limitations are present");
    const auto again=write_record(folder.wstring(),GetCurrentProcessId(),1,good);
    check(!again.complete&&read_file(first.json_path)==marker,"colliding evidence names cannot overwrite prior output");
    auto tampered=good;tampered.canonical.assembly_payload[0]^=1;
    check(!write_record(folder.wstring(),GetCurrentProcessId(),2,tampered).complete,"writer independently rejects packet not matching copied identity");
    check(!write_record((folder/L"missing").wstring(),GetCurrentProcessId(),3,good).complete,"writer never creates missing output directory");
    std::printf("profile_reader_checks=%u failures=%u baseline_native_copies=%u fixture_directory=%ls\n",checks,failures,baseline_reads,folder.c_str());
    return failures?1:0;
}
