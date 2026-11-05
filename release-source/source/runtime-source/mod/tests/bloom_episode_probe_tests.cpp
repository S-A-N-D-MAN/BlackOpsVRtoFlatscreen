// SPDX-License-Identifier: GPL-3.0-only
// Actual protected provider, production source/configuration factory, typed
// collector, and optional native-once DIP wrapper. All COM tables are fake.
#define main inherited_bloom_probe_main
#include "bloom_shader_probe_tests.cpp"
#undef main
#include "bloom_episode_fixture.hpp"
#include "t5/bloom_episode_source.hpp"
#include "../src/loader/d3d9_observer.cpp"
namespace episode_integration {
using namespace bovr;
using namespace episode_fixture;
struct NativeException {unsigned value;};
struct Run;
Run* running{};
HRESULT __stdcall all_texture(Device* d,DWORD i,IDirect3DBaseTexture9** p){
    check(i<16,"bounded current sampler destination");if(!d->step("texture"))return E_FAIL;
    *p=reinterpret_cast<IDirect3DBaseTexture9*>(&d->texture);oadd(&d->texture);return S_OK;}
HRESULT __stdcall all_sampler(Device* d,DWORD i,D3DSAMPLERSTATETYPE t,DWORD* p){
    check(i<16,"bounded current sampler state destination");if(!d->step("sampler"))return E_FAIL;*p=static_cast<DWORD>(t);return S_OK;}
HRESULT __stdcall actual_viewport(Device* d,D3DVIEWPORT9* p){if(!d->step("viewport"))return E_FAIL;*p={0,0,d->target_width,d->target_height,0,1};return S_OK;}
struct Run {
    Fixture f;Memory memory;AuxiliaryViewLayout layout;BloomEpisodeSourceContext context;
    unsigned mask{},first_count{1},mode{},native{},copies{},before_calls{},after_calls{},abandoned{},reset_calls{};
    bool active{};std::uint32_t index{2};void** table{};std::map<std::string,unsigned> native_entry_calls;
    using Thunk=HRESULT (__stdcall*)(Device*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
    Thunk thunk{};
    Run(unsigned m=0,unsigned passes=1):layout(fill(memory,0)),mask(m),first_count(passes){
        f.dt[64]=reinterpret_cast<void*>(all_texture);f.dt[68]=reinterpret_cast<void*>(all_sampler);f.dt[48]=reinterpret_cast<void*>(actual_viewport);
        context={f.source(),memory.base,&layout};select(1,0,0);
    }
    void select(unsigned eye,unsigned role,unsigned pass){
        episode_setup(memory,layout,eye,role,pass,mask,first_count);
        const auto ptr=[](auto* p){return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));};
        for(unsigned r=0;r<13;++r){const unsigned k=r==11?9:r;const auto b=memory.base+0x30000+k*0x2000;
            memory.write(b+0x804,ptr(&f.d.vs));memory.write(b+0x904,ptr(&f.d.ps));}
        for(unsigned id:{3u,6u,25u,26u,27u,28u,29u,30u}){
            memory.write(memory.base+0x80000+id*0x100,ptr(&f.d.texture));memory.write(memory.base+0x41eb1e8+20*id+4,ptr(&f.d.surface));}
        BloomEpisodePlan plan{};BloomReadStatus why{};
        check(read_bloom_episode_shader(memory.base,layout,{eye,role,pass,0},&f.d.snapshot,&plan,&why),"fixture is an actual current protected provider tuple");
        f.d.target_width=f.d.snapshot.width;f.d.target_height=f.d.snapshot.height;
    }
    BloomShaderSource source(){return make_bloom_episode_source(context);}
    void begin(unsigned serial=123){configure_bloom_episode_probe(f.probe,true,f.dir);f.probe.begin({0,serial,2,99,GetCurrentThreadId()},7,f.d.api(),source());}
    BloomDrawCall call()const{return {memory.base+0x30b5d3,D3DPT_TRIANGLELIST,0,0,4,13,2};}
    std::uint64_t before(){SetLastError(0x7755);auto t=f.probe.before(f.d.api(),call());check(GetLastError()==0x7755,"full contract preserves entry LastError");return t;}
    void direct_pair(){for(unsigned eye:{1u,0u})for(unsigned role:bloom_role_order){if(!bloom_role_enabled(role,mask))continue;
        for(unsigned p=0;p<(role?1u:first_count);++p){select(eye,role,p);auto t=before();check(t!=0,"current full-plan draw obtains typed lease");f.after(t);}}}
};
HRESULT __stdcall no_swap(Device*,UINT,IDirect3DSwapChain9** out){*out=nullptr;return E_NOINTERFACE;}
HRESULT __stdcall native_reset(Device*,D3DPRESENT_PARAMETERS*){++running->reset_calls;return S_OK;}
HRESULT __stdcall native_present(Device*,const RECT*,const RECT*,HWND,const RGNDATA*){return S_OK;}
HRESULT __stdcall native_create_swap(Device*,D3DPRESENT_PARAMETERS*,IDirect3DSwapChain9** p){*p=nullptr;return E_FAIL;}
HRESULT __stdcall native_draw(Device* d,D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertices,UINT start,UINT primitives){
    auto& r=*running;++r.native;check(d==&r.f.d&&type==D3DPT_TRIANGLELIST&&!base&&!min&&vertices==4&&start==13&&primitives==2,"actual full-plan wrapper preserves every original native argument");
    check(!set_indexed_draw_callbacks({}),"full plan native call retains callback tuple lease");r.native_entry_calls=d->calls;SetLastError(0x51515151);
    if(r.mode==1||r.mode==6)RaiseException(0xe032c110,0,0,nullptr);if(r.mode==2)throw NativeException{0x1289};return S_OK;}
std::uint64_t before(void* p,IDirect3DDevice9* d,const IndexedDrawCall& c) noexcept {
    auto& r=*static_cast<Run*>(p);++r.before_calls;std::uint64_t token{};
    check(r.active&&!set_indexed_draw_callbacks({}),"actual backend and draw leases overlap");
    __try {token=r.f.probe.before(d,{c.caller,static_cast<unsigned>(c.type),c.base_vertex,c.min_vertex,c.num_vertices,c.start_index,c.primitive_count});
        if(token&&(r.mode==5||r.mode==6))RaiseException(0xe0c11000,0,0,nullptr);}
    __except(EXCEPTION_EXECUTE_HANDLER){r.f.probe.abandon();token=0;}return token;}
void after(void* p,std::uint64_t t,HRESULT hr) noexcept {auto& r=*static_cast<Run*>(p);++r.after_calls;r.f.probe.after(t,hr);}
void abandon(void* p,std::uint64_t t) noexcept {auto& r=*static_cast<Run*>(p);++r.abandoned;const auto calls=r.f.d.calls;r.f.probe.abandon(t);check(calls==r.f.d.calls,"native unwind abandon has zero COM work");}
bool reset(void* p,IDirect3DDevice9*){auto& r=*static_cast<Run*>(p);r.f.d.current=false;return !r.active;}
void prepare(Run& r){running=&r;r.table=static_cast<void**>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));check(!!r.table,"private vtable allocated");
    std::memcpy(r.table,r.f.dt.data(),sizeof r.f.dt);r.f.d.table=r.table;r.table[13]=reinterpret_cast<void*>(native_create_swap);r.table[14]=reinterpret_cast<void*>(no_swap);
    r.table[16]=reinterpret_cast<void*>(native_reset);r.table[17]=reinterpret_cast<void*>(native_present);r.table[82]=reinterpret_cast<void*>(native_draw);
    // CPU-only forwarding thunk: exact runtime-relative native DIP return.
    // Copy seven original stack arguments, call current slot82, ret28. This
    // does not patch any game image and owns its executable allocation.
    std::vector<unsigned char> code{0x8b,0x44,0x24,0x04,0x8b,0x00,0x8b,0x80,0x48,0x01,0,0};
    for(unsigned i=0;i<7;++i)code.insert(code.end(),{0xff,0x74,0x24,0x1c});
    check(code.size()==40,"forwarding thunk reaches exact copied CALL site");code.insert(code.end(),{0xff,0xd0,0xc2,0x1c,0});
    const auto entry=r.memory.base+0x30b5d1-40;r.memory.commit(entry,code.size());std::memcpy(reinterpret_cast<void*>(entry),code.data(),code.size());
    DWORD old{};check(VirtualProtect(reinterpret_cast<void*>(entry&~std::uintptr_t(4095)),4096,PAGE_EXECUTE_READ,&old)!=0,"owned thunk page executable read-only");FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(entry),code.size());r.thunk=reinterpret_cast<Run::Thunk>(entry);
    set_observation_active(true);check(set_indexed_draw_callbacks({&r,before,after,abandon})&&set_graphics_callbacks({&r,nullptr,reset,nullptr,nullptr}),"complete callbacks installed");
    check(observe_device(r.f.d.api(),false),"actual documented optional draw observer installed");r.begin();}
bool body(Run* r){bool started{};const auto ok=execute_stereo_draw_plan(auxiliary_draw_plan(),[&](unsigned i){r->index=i;},[&]{
    const unsigned eye=r->index==2?1:0;for(unsigned role:bloom_role_order){if(!bloom_role_enabled(role,r->mask))continue;
        for(unsigned p=0;p<(role?1u:r->first_count);++p){r->select(eye,role,p);
            if(r->mode==8&&eye==1&&role==1)r->memory.write(r->f.d.snapshot.pass+15,std::uint8_t{4});
            SetLastError(0x45454545);
            const auto hr=r->thunk(&r->f.d,D3DPT_TRIANGLELIST,0,0,4,13,2);check(hr==S_OK&&GetLastError()==0x51515151,"full-plan actual native HRESULT/LastError preserved");}}},[&](unsigned){++r->copies;return true;},&started);
    r->f.probe.finish(ok);return started&&ok;}
bool backend(Run* r){r->active=true;bool result{};__try {result=body(r);}__finally {if(AbnormalTermination())r->f.probe.abandon();else r->f.probe.retire();r->active=false;}return result;}
bool seh_backend(Run* r){__try {backend(r);}__except(GetExceptionCode()==0xe032c110?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;}
void cleanup(Run& r){r.f.probe.retire();check(set_indexed_draw_callbacks({})&&set_graphics_callbacks({}),"normal retirement permits callback replacement");}
}
int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);using namespace episode_integration;
    if(argc==2&&std::strcmp(argv[1],"--emit-episode-wire-fixture")==0){Run r(3,4);r.begin();r.direct_pair();r.f.probe.finish(true);r.f.preserve_outputs=true;std::printf("fixture_directory=%ls\n",r.f.dir.c_str());return failures?1:0;}
    for(unsigned mask=0;mask<4;++mask)for(unsigned passes=1;passes<=4;++passes){Run r(mask,passes);r.begin();r.direct_pair();r.f.probe.finish(true);
        check(r.f.probe.attempts()==1&&r.f.manifest().find("\"schema\":2")!=std::string::npos&&r.f.manifest().find("\"complete\":true")!=std::string::npos,"real protected provider and production factory complete18/22/26 and exact multipass32 contracts");}
    for(unsigned bad=0;bad<5;++bad){Run r;r.begin();auto s=r.source();
        if(bad==0)s.read_episode=nullptr;if(bad==1)s.native_draw_return=0;if(bad==2)r.context.layout=nullptr;if(bad==3)r.context.owner.current=nullptr;if(bad==4)r.memory.write(r.layout.slots[0].address+0x3784,0x7fc00000u);
        r.f.probe.begin({0,124,2,99,GetCurrentThreadId()},7,r.f.d.api(),s);check(!r.before()&&!r.f.probe.attempts()&&r.f.d.calls.empty(),"full adapter source never falls back to inherited legacy reader on missing/bad plan");}
    for(unsigned bad=0;bad<6;++bad){Run r;r.begin();auto c=r.call();if(bad==0)++c.caller;if(bad==1)c.type=D3DPT_LINESTRIP;if(bad==2)c.base_vertex=1;if(bad==3)c.min_vertex=1;if(bad==4)c.num_vertices=5;if(bad==5)c.primitive_count=3;
        check(!r.f.probe.before(r.f.d.api(),c)&&r.f.d.calls.empty(),"actual callsite/topology gates precede optional COM");}
    for(unsigned field=0;field<9;++field)for(bool post:{false,true}){Run r;r.begin();r.f.after(r.before());r.select(1,1,0);
        auto mutate=[&]{if(field==0)r.memory.write(r.layout.slots[0].address+0x3784,0x3f800000u);
            if(field==1)r.memory.write(r.layout.slots[1].address+0x37d8,0x3f800000u);
            if(field==2)r.memory.write(r.memory.base+0x30000+7*0x2000+0x506,std::uint16_t{2});
            if(field==3)r.memory.write(r.memory.base+0x30000+8*0x2000+0x804,0xdeadbeefu);
            if(field==4)r.memory.write(r.memory.base+0x30000+0x2000+0xa00+12,37u);
            if(field==5)r.memory.write(r.memory.base+0x41eb1e8+25*20,0u);
            if(field==6)r.memory.write(r.memory.base+0x417eed4,1u);
            if(field==7)r.memory.write(r.memory.base+0x417ff28,std::uint8_t{28});
            if(field==8)r.memory.write(r.f.d.snapshot.pass+15,std::uint8_t{4});};
        std::uint64_t t{};if(post){t=r.before();check(t!=0,"pre-native current plan accepted before mutation");mutate();}else{r.f.d.hook_name="pixel";r.f.d.hook=mutate;t=r.before();}if(t)r.f.after(t);r.f.probe.finish(true);
        check(r.f.manifest().find("\"complete\":false")!=std::string::npos&&r.f.manifest().find("\"diagnostic_failed\":1")!=std::string::npos,"future/current plan/getter/native-after mutations poison claimed attempt");}
    {Run r;r.begin();r.f.after(r.before());r.select(1,2,0);check(!r.before(),"missing expected role is not silently skipped");r.f.probe.finish(true);check(r.f.manifest().find("\"complete\":false")!=std::string::npos,"ordered-prefix incomplete artifact retained");}
    {Run r;r.begin();r.f.after(r.before());r.select(1,1,0);const auto value=r.f.d.snapshot.scene[0]+1;
        r.memory.write(r.layout.slots[1].address+0x170,value);r.memory.write(r.memory.base+0x3ccbfa8,value);
        const auto t=r.before();check(t!=0,"individually current changed SceneDef still has exact getter binding");r.f.after(t);r.f.probe.finish(true);
        check(r.f.manifest().find("\"complete\":false")!=std::string::npos&&r.f.manifest().find("pair_order_or_duplicate")!=std::string::npos,"complete episode rejects an inter-role SceneDef change within an eye");}
    {Run r;r.memory.write(r.memory.base+0x30000+0xa0c,37u);const auto initial=logs.size();auto source=r.source();BloomDrawSnapshot n{};BloomEpisodePlan p{};BloomReadStatus why{};
        for(unsigned i=0;i<40;++i){SetLastError(0x88776655);check(!source.read_episode(source.context,{1,0,0,0},&n,&p,&why)&&GetLastError()==0x88776655,"production factory contains exact argument rejection and preserves LastError");}
        check(logs.size()==initial+1&&logs.back().find("reason=unhandled_code_sampler")!=std::string::npos&&logs.back().find("argument_value=00000025")!=std::string::npos,"exact rejected raw code/index is logged once for owner invocation");}
    {Run r;for(unsigned i=0;i<40;++i){r.select(1,0,0);r.memory.write(r.layout.slots[0].address+0x3784,0x7fc00000u);r.begin(i+1);check(!r.before(),"failed preclaim reads remain bounded");r.f.probe.finish(true);}r.select(1,0,0);r.begin(99);check(!r.before()&&r.f.d.calls.empty(),"32 preclaim failures exhaust collector without optional COM or retry rearm");}
    {Run r;r.begin();r.direct_pair();r.f.probe.finish(true);r.begin(124);r.direct_pair();r.f.probe.finish(true);r.begin(125);check(!r.before()&&r.f.probe.attempts()==2,"two claimed full episodes remain final across later backends");}
    std::vector<std::unique_ptr<Run>> retained;
    for(unsigned mode=0;mode<9;++mode){retained.push_back(std::make_unique<Run>(mode==7?3u:0u,mode==7?4u:1u));auto& r=*retained.back();r.mode=mode;prepare(r);
        if(mode==4)r.f.d.source_seh_after_vertex=true;
        if(mode==3){r.f.d.hook_name="function_size";r.f.d.hook=[&]{D3DPRESENT_PARAMETERS p{};check(r.f.d.api()->Reset(&p)==D3DERR_DEVICELOST,"actual Reset defers while backend lease is active");};}
        if(mode==1||mode==2||mode==6){bool caught{};if(mode==1||mode==6)caught=seh_backend(&r);else try{backend(&r);}catch(const NativeException& e){caught=e.value==0x1289;}
            check(caught&&r.native==1&&!r.copies&&!r.active&&r.f.d.calls==r.native_entry_calls&&r.f.d.vs.refs>1,"actual native SEH/C++ crosses complete-provider wrapper once with no COM in unwind");check(r.f.manifest().empty(),"native unwind never writes output");}
        else {check(backend(&r)&&r.native==(mode==7?32u:18u)&&r.copies==2,"actual full-LDR native draw/stereo staging contract");
            check((r.f.manifest().find("\"complete\":true")!=std::string::npos)==(mode==0||mode==7),"only complete unchanged actual source/wrapper episodes claim completion");}
        cleanup(r);}
    running=nullptr;
    {Run r(3,4);r.f.d.shader_size=65536;r.begin();const auto before=r.f.probe.bytes_used();
        for(unsigned eye:{1u,0u})for(unsigned role:bloom_role_order)for(unsigned p=0;p<(role?1u:4u);++p){r.select(eye,role,p);const auto t=r.before();if(t)r.f.after(t);}
        r.f.probe.finish(true);const auto spent=r.f.probe.bytes_used();check(spent>before&&spent<=bloom_process_bytes&&bloom_process_bytes-spent<65536,"full optional episode enforces process-wide4MiB including previous captures");
        check(r.f.manifest().find("\"complete\":false")!=std::string::npos,"byte cap cannot be presented as a complete full episode");
        Run next;next.f.d.shader_size=65536;next.begin();next.f.after(next.before());next.f.probe.finish(true);
        check(next.f.probe.bytes_used()==spent&&r.f.probe.bytes_used()==spent,"failed retrieval has no debit refund across new collector instances");}
    std::printf("bloom_episode_probe_checks=%u failures=%u real_gpu_calls=0\n",checks,failures);return failures?1:0;
}
