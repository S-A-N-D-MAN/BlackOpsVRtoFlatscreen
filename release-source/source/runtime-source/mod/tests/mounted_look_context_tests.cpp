// SPDX-License-Identifier: GPL-3.0-only
// Actual reader over owned logical-address buffers using its real protected
// copy. Native guard inputs are pinned SP bytes, not a running game image.
#include "t5/mounted_look_context.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <functional>
#include <thread>
#include <bit>
#include <xmmintrin.h>
namespace ml=bovr::t5::mounted_look;
using U8=std::uint8_t;using U32=std::uint32_t;
namespace {
unsigned checks{},failures{};
void check(bool good,const char* label){++checks;if(!good){++failures;std::printf("FAIL %s\n",label);}}
struct GuardInput {U32 address;std::size_t size;};
// Authored zero-filled ranges mirror only the public guard boundaries. The
// production library retains the retail addresses, lengths and SHA-256 values.
constexpr GuardInput guard_inputs[]{
    {0x005a2810,378},{0x0075f3e0,332},{0x0075f300,218},{0x004cecd0,407},
    {0x00623b60,623},{0x00793fb0,1357},{0x007923f0,557},{0x0045bdd0,940},
    {0x00651350,321},{0x0081c540,586},{0x0069d450,327},{0x0054d490,38},
    {0x0065e787,135},{0x00660878,42},{0x00679020,116},{0x00651910,104},
};
struct Span {U32 address;std::size_t size;};
struct Fixture {
    std::map<U32,std::vector<U8>> blocks;
    std::vector<Span> reads;
    ml::Request request{};bovr::t5::PreCommandSnapshot pre{};
    DWORD owner=GetCurrentThreadId();std::uint64_t ticks=1000;
    unsigned pre_calls{},clock_calls{},thread_calls{};std::size_t fail_at{},mutate_at{};
    U32 mutate_address{};U8 mutate_xor=1;
    bool command_ok=true,fault=false,raw_fault=false;
    void* noaccess{};
    std::function<void()> hook;
    std::function<void(unsigned)> pre_hook,clock_hook,thread_hook;
    static constexpr U32 cg=0x6000000,ps=cg+0x8a364;
    static constexpr U32 dvar(unsigned i){return 0x7400000+0x1000*i;}
    static constexpr U32 name(unsigned i){return 0x7500000+0x1000*i;}
    static constexpr U32 slots[]{0xbcd150,0xbcd1b8,0xbcaf04,0xbcaebc};
    static constexpr const char* names[]{"player_topDownCamMode","player_viewLockEnt","player_view_pitch_up","player_view_pitch_down"};
    U8* pointer(U32 at,std::size_t size){auto i=blocks.upper_bound(at);if(i==blocks.begin())return nullptr;--i;
        const auto delta=std::uint64_t(at)-i->first;if(delta>i->second.size()||size>i->second.size()-delta)return nullptr;
        return i->second.data()+static_cast<std::size_t>(delta);}
    void block(U32 at,std::size_t size){blocks.emplace(at,std::vector<U8>(size));}
    template<class T>void put(U32 at,T value){auto* p=pointer(at,sizeof(T));if(!p){block(at,sizeof(T));p=pointer(at,sizeof(T));}std::memcpy(p,&value,sizeof(T));}
    template<class T>T get(U32 at){T out{};std::memcpy(&out,pointer(at,sizeof(T)),sizeof(T));return out;}
    void bytes(U32 at,const void* p,std::size_t size){auto* to=pointer(at,size);if(!to){block(at,size);to=pointer(at,size);}std::memcpy(to,p,size);}
    Fixture(){
        block(cg,0xaa000);for(const auto& g:guard_inputs)block(g.address,g.size);
        put<U32>(0x2ff5354,cg);put<int>(0x2993850,20);put<int>(0x2481764,9000);put<int>(0x2910160,0);
        put<int>(0x2910164,9);put<int>(0x2911e2c,0);put<int>(cg+0x8a348,10000);put<int>(ps+0x1c4,100);
        put<U32>(ps+0xe0,0x100);put<U32>(ps+0x440,1);put<std::int16_t>(ps+0x444,42);put<int>(cg+0xa4680,5);
        put<float>(ps+0x1a0,10);put<float>(ps+0x1a4,120);put<float>(ps+0x1a8,45);put<float>(ps+0x1ac,60);
        for(unsigned i=0;i<4;++i){block(dvar(i),0x30);block(name(i),32);put<U32>(slots[i],dvar(i));put<U32>(dvar(i),name(i));
            put<U32>(dvar(i)+0x10,i<2?5U:1U);bytes(name(i),names[i],std::strlen(names[i])+1);if(i>=2)put<float>(dvar(i)+0x18,85);}
        request.command.valid=true;request.command.native_thread=owner;request.command.command_number=20;
        request.command.native_frame_time=9000;request.command.connection_state=9;request.pre_sequence=2;request.gameplay_epoch=3;
        pre.context=request.command;pre.sequence=2;pre.gameplay_epoch=3;pre.observed_ms=ticks;pre.native_cg_identity=cg;
        pre.native_gameplay_fields_valid=true;sync();
        noaccess=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS);check(noaccess!=nullptr,"owned inaccessible fixture page");
    }
    ~Fixture(){if(noaccess)VirtualFree(noaccess,0,MEM_RELEASE);}
    void sync(){pre.native_game_time=get<int>(cg+0x8a348);pre.native_health=get<int>(ps+0x1c4);pre.native_pm_type=get<int>(ps+4);
        pre.native_pm_flags=get<U32>(ps+0xc);pre.native_weapon_flags=get<U32>(ps+0x10);pre.native_entity_flags=get<U32>(ps+0xe0);pre.native_entity_flags2=get<U32>(ps+0xe4);}
    static constexpr U32 tow_clients=0x7600000,tow_client=tow_clients+42*0x31c,
        tow_server=0x1a796f8+42*0x34c,tow_variant=0x7700000,tow_definition=0x7710000,tow_name=0x7720000;
    void scoped_tow(){
        put<int>(cg+0xa9d44,1);put<int>(ps+4,1);put<U32>(ps+0xc,0x1000000);
        put<U32>(ps+0x10,0x800000);put<U32>(ps+0xe0,0xa0302);put<U8>(ps+0x144,48);
        block(tow_clients,0x31c*43);block(tow_server,0x34c);block(tow_variant,0x90);block(tow_definition,0x24);block(tow_name,29);
        put<U32>(0x2ff6684,tow_clients);put<U8>(tow_client+2,11);put<U32>(tow_client+0x164,42);put<U8>(tow_client+0x238,3);put<U32>(tow_server,42);
        put<std::uint16_t>(tow_server+0xbe,11);put<U8>(tow_server+0xdd,1);put<U8>(tow_server+0xd4,3);
        put<U32>(0xbe19a8+3*4,tow_variant);put<U32>(tow_variant,tow_name);put<U32>(tow_variant+8,tow_definition);
        put<U32>(tow_variant+0x8c,0x7730000);put<int>(tow_definition+0x1c,2);put<int>(tow_definition+0x20,7);
        constexpr char name[]="m220_tow_emplaced_khesanh_sp";static_assert(sizeof name==29);bytes(tow_name,name,sizeof name);sync();
    }
    void reset(){reads.clear();pre_calls=clock_calls=thread_calls=0;fail_at=mutate_at=0;fault=raw_fault=false;hook={};pre_hook={};clock_hook={};thread_hook={};}
    static bool copy(void* opaque,std::uintptr_t at,void* out,std::size_t size)noexcept {
        auto& f=*static_cast<Fixture*>(opaque);if(at>UINT32_MAX)return false;
        try {
            f.reads.push_back({static_cast<U32>(at),size});const auto index=f.reads.size();
            if(index==f.fail_at){if(f.raw_fault)RaiseException(0xc0000005,0,0,nullptr);
                if(f.fault)return ml::protected_copy(nullptr,reinterpret_cast<std::uintptr_t>(f.noaccess),out,size);return false;}
            auto* p=f.pointer(static_cast<U32>(at),size);if(!p)return false;
            if(!ml::protected_copy(nullptr,reinterpret_cast<std::uintptr_t>(p),out,size))return false;
            if(index==f.mutate_at)*f.pointer(f.mutate_address,1)^=f.mutate_xor;
            if(f.hook){auto hook=std::move(f.hook);f.hook={};hook();}return true;
        }catch(...){return false;}
    }
    static bool command(void* opaque,bovr::t5::PreCommandSnapshot* out)noexcept{auto& f=*static_cast<Fixture*>(opaque);
        ++f.pre_calls;if(f.pre_hook)f.pre_hook(f.pre_calls);*out=f.pre;return f.command_ok;}
    static DWORD thread(void* opaque)noexcept{auto& f=*static_cast<Fixture*>(opaque);++f.thread_calls;
        if(f.thread_hook)f.thread_hook(f.thread_calls);return GetCurrentThreadId()==f.owner?f.owner:GetCurrentThreadId();}
    static std::uint64_t now(void* opaque)noexcept{auto& f=*static_cast<Fixture*>(opaque);++f.clock_calls;
        if(f.clock_hook)f.clock_hook(f.clock_calls);return f.ticks;}
    ml::Host host(){return {this,copy,command,thread,now};}
};
bool empty(const ml::Copy& c){return c==ml::Copy{};}
void configuration(){
    {Fixture f;ml::Source off;ml::Copy out{};check(off.read(f.request,&out)==ml::Status::disabled&&f.reads.empty(),"default source off no callbacks");
        auto disabled=ml::make_source(false,f.host());check(disabled.configuration()==ml::Status::disabled&&f.reads.empty(),"false factory request does not inspect native bytes");}
    for(unsigned member=0;member<4;++member){Fixture f;auto h=f.host();if(member==0)h.copy=nullptr;if(member==1)h.command=nullptr;
        if(member==2)h.thread=nullptr;if(member==3)h.now=nullptr;auto s=ml::make_source(true,h);check(s.configuration()==ml::Status::arguments,"missing required host callback refuses startup");}
    for(const auto& guard:guard_inputs)for(auto at:{std::size_t{0},guard.size/2,guard.size-1}){Fixture f;
        *f.pointer(guard.address+static_cast<U32>(at),1)^=1;auto s=ml::make_source(true,f.host());
        check(s.configuration()==ml::Status::guards,"pinned byte change at guard beginning middle end refuses");
        f.reset();ml::Copy out{};check(s.read(f.request,&out)==ml::Status::guards&&f.reads.empty()&&empty(out),"failed guard source cannot publish later");}
    for(bool raw:{false,true}){Fixture f;f.fail_at=8;f.fault=!raw;f.raw_fault=raw;auto s=ml::make_source(true,f.host());
        check(s.configuration()==ml::Status::guards,"protected and raw native guard access faults contained");}
}
void acceptance(){
    for(int pm:{0,1})for(U32 flags:{0x100U,0x200U,0x300U})for(U32 lock:{1U,2U}){Fixture f;
        f.put<int>(Fixture::ps+4,pm);f.put<U32>(Fixture::ps+0xe0,flags);f.put<U32>(Fixture::ps+0x440,lock);f.put<int>(Fixture::cg+0x8a360,0);f.sync();
        auto source=ml::make_source(true,f.host());check(source.configuration()==ml::Status::accepted,"exact loaded guards configure");f.reset();
        const auto original=f.blocks;ml::Copy a{},b{};SetLastError(0x1abc);const auto mxcsr=_mm_getcsr();
        check(source.read(f.request,&a)==ml::Status::accepted&&ml::eligible(a.facts),"living normal or linked turret tuple accepts");
        check(GetLastError()==0x1abc&&_mm_getcsr()==mxcsr&&f.blocks==original,"reader preserves memory LastError and caller MXCSR");
        const auto reads=f.reads;check(f.pre_calls==3&&reads.size()%2==0,"two complete fact passes and three actual pre snapshots");
        const auto n=reads.size()/2;bool order=true;for(std::size_t i=0;i<n;++i)order=order&&reads[i].address==reads[n+i].address&&reads[i].size==reads[n+i].size;
        check(order,"both complete passes preserve the same native query order");
        ++f.ticks;f.reset();check(source.read(f.request,&b)==ml::Status::accepted&&ml::same_current(a,b),"same occurrence raw facts ignore observation duration only");
        f.put<int>(Fixture::cg+0x8a360,2);f.reset();
        check(source.read(f.request,&b)==ml::Status::accepted&&!ml::same_current(a,b)&&a.facts.camera[0]==0&&b.facts.camera[0]==2,
            "shadow2 accepts on mounted contexts and is retained in raw facts");
        ++f.pre.sequence;++f.request.pre_sequence;f.reset();check(source.read(f.request,&b)==ml::Status::accepted&&!ml::same_current(a,b),"new command occurrence has a distinct full key");
        source.revoke();check(!source.current(b.revision),"atomic owner invalidation retires copied revision");
        f.reset();check(source.read(f.request,&b)==ml::Status::accepted,"fresh full read recovers after revocation");
    }
}
void negatives(){
    const std::vector<std::pair<U32,U32>> fields{
      {Fixture::ps+4,2},{Fixture::ps+4,5},{Fixture::ps+4,9},{Fixture::ps+4,10},{Fixture::ps+0x1c4,0},
      {Fixture::ps+0xc,0x800},{Fixture::ps+0x10,0x80},{Fixture::ps+0x10,0x100},
      {Fixture::ps+0xe0,0},{Fixture::ps+0xe0,0x4100},{Fixture::ps+0xe4,0x40000},{Fixture::ps+0xe4,0x40000000},
      {Fixture::ps+0x440,0},{Fixture::ps+0x440,3},
      {Fixture::cg+0x8a360,1},{Fixture::cg+0xc,1},{Fixture::cg+0xa9d44,1},{Fixture::cg+0x1c,1},
      {Fixture::cg+0x14,3},{Fixture::cg+0xa4780,1},{Fixture::cg+0xa4680,0},{Fixture::cg+0xa4680,1},
      {0x2910160,16},{0x2910164,8},{0x2911e2c,1},{0x2993850,21},{0x2481764,9001},
      {Fixture::dvar(0)+0x18,1},{Fixture::dvar(1)+0x18,42},
      {Fixture::ps+0x180,0x7f800000},{Fixture::ps+0x84,0x7fc00001},{Fixture::ps+0x1a0,0xff800000},
      {Fixture::ps+0x1a8,0xbf800000},{Fixture::ps+0x1ac,0x43340001},
      {Fixture::dvar(2)+0x18,0},{Fixture::dvar(2)+0x18,0x80000000},{Fixture::dvar(3)+0x18,0x42b40001},
      {Fixture::dvar(3)+0x18,0x7fc00000},{0x2ff5354,0},{0x2ff5354,0xfffffff0}};
    for(const auto& [address,value]:fields){Fixture f;auto source=ml::make_source(true,f.host());f.put<U32>(address,value);f.sync();f.reset();
        ml::Copy out{};out.revision=99;check(source.read(f.request,&out)!=ml::Status::accepted&&empty(out),"unsafe selector flag clamp override or identity rejected without output");}
    for(int mode:{-1,3}){Fixture f;auto source=ml::make_source(true,f.host());f.put<int>(Fixture::cg+0x8a360,mode);f.reset();
        ml::Copy out{};out.revision=99;check(source.read(f.request,&out)!=ml::Status::accepted&&empty(out),"unsupported shadow camera modes are rejected");}
    for(U32 offset:{0xcu,0xa9d44u,0x1cu}){Fixture f;auto source=ml::make_source(true,f.host());f.put<int>(Fixture::cg+0x8a360,2);f.put<int>(Fixture::cg+offset,1);f.reset();
        ml::Copy out{};out.revision=99;check(source.read(f.request,&out)!=ml::Status::accepted&&empty(out),"mounted shadow mode preserves independent camera exclusions");}
    {Fixture f;auto source=ml::make_source(true,f.host());f.put<U32>(Fixture::ps+0xe0,0);f.sync();f.put<int>(Fixture::cg+0x8a360,2);f.reset();
        ml::Copy out{};out.revision=99;check(source.read(f.request,&out)!=ml::Status::accepted&&empty(out),"shadow2 cannot admit nonmounted context");}
    for(std::int16_t entity:{std::int16_t{-1},std::int16_t{1023},std::int16_t{1024}}){Fixture f;auto s=ml::make_source(true,f.host());f.put<std::int16_t>(Fixture::ps+0x444,entity);f.reset();ml::Copy out{};
        check(s.read(f.request,&out)!=ml::Status::accepted&&empty(out),"invalid or unsupported mounted entity rejected");}
    for(unsigned i=0;i<4;++i)for(unsigned kind=0;kind<5;++kind){Fixture f;auto s=ml::make_source(true,f.host());
        if(kind==0)f.put<U32>(Fixture::slots[i],0);if(kind==1)f.put<U32>(Fixture::dvar(i),0xfffffff0);
        if(kind==2)f.put<U32>(Fixture::dvar(i)+0x10,i<2?1U:5U);if(kind==3)f.put<U8>(Fixture::name(i),'!');
        if(kind==4)f.put<U8>(Fixture::name(i)+static_cast<U32>(std::strlen(Fixture::names[i])),'!');f.reset();ml::Copy out{};
        check(s.read(f.request,&out)!=ml::Status::accepted&&empty(out),"dvar slot pointer type exact name and NUL required");}
    for(int mode=0;mode<2;++mode){Fixture f;auto s=ml::make_source(true,f.host());f.put<int>(Fixture::ps+4,mode);f.put<U32>(Fixture::ps+0xe0,0);f.sync();f.reset();ml::Copy out{};
        check(s.read(f.request,&out)!=ml::Status::accepted,"unflagged normal and linked states are never mounted permission");}
}
void boundaries(){
    Fixture template_fixture;auto ts=ml::make_source(true,template_fixture.host());template_fixture.reset();ml::Copy first{};
    check(ts.read(template_fixture.request,&first)==ml::Status::accepted,"measure actual reader boundaries");const auto spans=template_fixture.reads;const auto half=spans.size()/2;
    for(std::size_t at=1;at<=spans.size();++at){Fixture f;auto s=ml::make_source(true,f.host());f.reset();f.fail_at=at;f.fault=true;ml::Copy out{};out.revision=99;
        check(s.read(f.request,&out)!=ml::Status::accepted&&empty(out),"every protected getter fault produces empty failure");
        check(f.reads.size()==at,"failed getter short circuits later native reads");f.reset();check(s.read(f.request,&out)==ml::Status::accepted,"same source recovers with full fresh read");}
    for(std::size_t i=0;i<half;++i){Fixture f;auto s=ml::make_source(true,f.host());f.reset();f.mutate_at=half;f.mutate_address=spans[i].address;ml::Copy out{};
        check(s.read(f.request,&out)!=ml::Status::accepted&&empty(out),"all copied fact spans detect persistent change between complete passes");}
    for(std::size_t at:{std::size_t{1},half,spans.size()}){Fixture f;auto s=ml::make_source(true,f.host());f.reset();f.fail_at=at;f.raw_fault=true;ml::Copy out{};
        check(s.read(f.request,&out)==ml::Status::exception&&empty(out),"raw early middle late SEH contained");f.reset();check(s.read(f.request,&out)==ml::Status::accepted,"SEH clears owned busy state for recovery");}
    for(unsigned boundary=1;boundary<=3;++boundary)for(unsigned what=0;what<5;++what){Fixture f;auto s=ml::make_source(true,f.host());f.reset();
        f.pre_hook=[&](unsigned index){if(index!=boundary)return;if(what==0)++f.pre.sequence;if(what==1)++f.pre.gameplay_epoch;
            if(what==2)f.pre.context.native_override=true;if(what==3)f.pre.native_health=99;if(what==4)f.command_ok=false;};ml::Copy out{};
        check(s.read(f.request,&out)!=ml::Status::accepted&&empty(out),"actual pre occurrence and payload mutations reject at each boundary");}
    for(unsigned mode=0;mode<4;++mode){Fixture f;auto s=ml::make_source(true,f.host());f.reset();ml::Copy out{};
        if(mode==0)f.pre.observed_ms=899;if(mode==1){f.request.gameplay_epoch=0;f.pre.gameplay_epoch=0;}
        if(mode==2)f.clock_hook=[&](unsigned n){if(n==2)--f.ticks;};if(mode==3)f.clock_hook=[&](unsigned n){if(n==3)f.ticks=1101;};
        check(s.read(f.request,&out)!=ml::Status::accepted&&empty(out),"stale regression duration and cold epoch0 refuse");}
    for(unsigned mode=0;mode<3;++mode){Fixture f;auto s=ml::make_source(true,f.host());f.reset();ml::Copy out{};
        f.hook=[&]{if(mode==0){ml::Copy nested{};check(s.read(f.request,&nested)==ml::Status::busy&&empty(nested),"nested same-source reader refuses");}
            if(mode==1){std::thread foreign([&]{ml::Copy other{};check(s.read(f.request,&other)==ml::Status::owner&&empty(other),"foreign callback refuses without native copy");});foreign.join();}
            if(mode==2)s.revoke();};
        check(s.read(f.request,&out)!=ml::Status::accepted&&empty(out),"nested foreign and explicit atomic revocation reject outer publication");
        f.reset();check(s.read(f.request,&out)==ml::Status::accepted,"fresh owner observation recovers after interruption");}
    {Fixture f;auto s=ml::make_source(true,f.host());f.reset();ml::Copy a{},b{};check(s.read(f.request,&a)==ml::Status::accepted,"raw signed zero baseline");
        f.put<U32>(Fixture::ps+0x1a8,0x80000000);f.put<U32>(Fixture::ps+0x180,0x80000000);f.reset();
        check(s.read(f.request,&b)==ml::Status::accepted&&!ml::same_current(a,b),"finite signed zero retained exactly despite same numeric zero");
        auto c=b;c.facts.delta_bits[2]^=1;check(!ml::same_current(b,c),"unused raw observed delta word participates in identity");
        ++c.source_identity;check(!ml::same_current(b,c),"source identity cannot be swapped");}
    {Fixture f;auto s=ml::make_source(true,f.host());f.reset();ml::Copy a{},b{};check(s.read(f.request,&a)==ml::Status::accepted,"prior current copy before invalid request");
        auto bad=f.request;bad.gameplay_epoch=0;check(s.read(bad,&b)==ml::Status::arguments&&empty(b)&&!s.current(a.revision),"invalid request revokes earlier copied revision");
        check(s.read(f.request,&b)==ml::Status::accepted,"fresh owner read after malformed request recovers");
        check(s.read(f.request,nullptr)==ml::Status::arguments&&!s.current(b.revision),"missing output revokes previous copy too");}
    std::printf("mounted_fact_pass_getters=%zu\n",half);
}
void scoped_tow_context(){
    Fixture f;f.scoped_tow();auto source=ml::make_source(true,f.host());f.reset();ml::Copy baseline{};
    check(source.read(f.request,&baseline)==ml::Status::accepted&&baseline.facts.camera[2]==1,
        "exact scoped truck TOW retains mounted input with ordinary viewmodel hidden");
    check(baseline.facts.scoped_tow.weapon==3&&baseline.facts.scoped_tow.client_weapon==3,
        "scoped turret weapon comes from matching mounted entities not the held knife");
    const auto spans=f.reads;const auto half=spans.size()/2;
    for(U32 at:{Fixture::tow_variant,Fixture::tow_variant+8,Fixture::tow_variant+0x8c,
        Fixture::tow_definition+0x1c,Fixture::tow_definition+0x20,Fixture::tow_server}){
        Fixture bad;bad.scoped_tow();auto reader=ml::make_source(true,bad.host());bad.put<U32>(at,0);bad.reset();ml::Copy out{};
        check(reader.read(bad.request,&out)!=ml::Status::accepted&&empty(out),
            "scoped TOW requires exact live turret number name definition type class and overlay");
    }
    for(U32 at:{Fixture::tow_server+0xbe,Fixture::tow_server+0xdd,Fixture::tow_server+0xd4,
        Fixture::tow_client+2,Fixture::tow_client+0x238,Fixture::tow_name,Fixture::tow_name+28}){
        Fixture bad;bad.scoped_tow();auto reader=ml::make_source(true,bad.host());bad.put<U8>(at,99);bad.reset();ml::Copy out{};
        check(reader.read(bad.request,&out)!=ml::Status::accepted&&empty(out),
            "non TOW changed inactive or mismatched mounted identity stays excluded");
    }
    for(unsigned which=0;which<6;++which){Fixture bad;bad.scoped_tow();auto reader=ml::make_source(true,bad.host());
        if(which==0)bad.put<U32>(Fixture::ps+0xe0,0);
        if(which==1)bad.put<U32>(Fixture::ps+0xe0,0x4300);
        if(which==2)bad.put<int>(Fixture::cg+0xa9d44,2);
        if(which==3)bad.put<int>(Fixture::cg+0xc,1);
        if(which==4)bad.put<int>(Fixture::cg+0xa4680,0);
        if(which==5)bad.put<U8>(Fixture::tow_client+4,1);
        bad.sync();bad.reset();ml::Copy out{};
        check(reader.read(bad.request,&out)!=ml::Status::accepted&&empty(out),
            "scoped TOW cannot authorize nonmounted vehicle unknown camera cubemap or remote pose");
    }
    for(std::size_t i=0;i<half;++i){Fixture bad;bad.scoped_tow();auto reader=ml::make_source(true,bad.host());bad.reset();
        bad.mutate_at=half;bad.mutate_address=spans[i].address;ml::Copy out{};
        check(reader.read(bad.request,&out)!=ml::Status::accepted&&empty(out),
            "scoped ownership graph changes between complete copies reject publication");
    }
}
} // namespace
namespace bovr::t5 {bool read_pre_command_snapshot(PreCommandSnapshot*) noexcept{return false;}}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);configuration();acceptance();negatives();boundaries();scoped_tow_context();
    std::printf("mounted_look_context_checks=%u failures=%u native_calls=0 gpu_calls=0\n",checks,failures);return failures?1:0;}
