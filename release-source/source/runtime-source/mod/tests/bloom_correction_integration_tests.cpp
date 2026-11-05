// SPDX-License-Identifier: GPL-3.0-only
// Real indexed wrapper -> real active class -> typed CPU device UP. Native
// failures escape the unchanged original call; only our UP faults are caught.
namespace {void check(bool,const char*);}
#include "bloom_episode_fixture.hpp"
#define main inherited_c12_replay_main
#include "bloom_correction_replay_tests.cpp"
#undef main
#include "t5/bloom_episode_reuse_source.hpp"
namespace replay_integration {
struct NativeCpp {unsigned value;};
struct Run;Run* running{};
struct Run {Fixture fixture;void** table{};unsigned mode{},callbacks{},abandoned{},resets{};bool begun{},nested{};
    std::map<std::string,unsigned> at_native;
    BloomEpisodeReuseSession reuse;BloomEpisodeReuseContext source_context;
    std::unique_ptr<episode_fixture::Memory> memory;AuxiliaryViewLayout layout{};unsigned mask{},view_count{3};
    bool grouped{true},owned_guard{};unsigned source_reads{},query_guards{};BloomShaderSource uncounted_source{};std::function<void()> at_original;
    std::uint64_t last_token{};unsigned guard_action_at{};std::function<void()> guard_action;bool guard_seh{};
    std::function<void(unsigned,unsigned)> after_select;
    using Thunk=HRESULT (__stdcall*)(Device*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);Thunk thunk{};
    explicit Run(unsigned optional=3,bool actual_source=false,unsigned slot=0,bool query_grouped=true,unsigned scale=1,unsigned views=3,bool query_owned=false):fixture(optional),mask(optional),view_count(views),grouped(query_grouped),owned_guard(query_owned){
        check(reuse.prepare(),"actual wrapper session allocation once before begin");
        fixture.d.resolution_scale=scale;
        if(actual_source){memory=std::make_unique<episode_fixture::Memory>();layout=episode_fixture::fill(*memory,slot,views);
            for(unsigned eye=0;eye<2;++eye){auto& v=layout.slots[eye];
                v.scene=v.display=v.scissor={static_cast<int>(eye*640*scale),0,static_cast<int>(640*scale),static_cast<int>(720*scale)};
                memory->write(v.address+0x140,v.scene);memory->write(v.address+0x150,v.display);memory->write(v.address+0x160,v.scissor);}
            fixture.use_native_geometry();}}
    void select(unsigned eye,unsigned role){auto& f=fixture;f.select(eye,role);if(!memory)return;
        episode_fixture::episode_setup(*memory,layout,eye,role,0,mask);
        // The actual reader consumes the currently installed eye viewport.
        // fill() begins with a full-canvas setup value, before any eye draw.
        memory->write(memory->base+0x3ccbfbc,layout.slots[eye].display);
        for(unsigned r=0;r<13;++r){const unsigned k=r==11?9:r;const auto b=memory->base+0x30000+k*0x2000;const auto& sh=bloom_embedded::roles[r];
            memory->write(b+0x804,ptr(&f.d.programs[sh.vertex]));memory->write(b+0x904,ptr(&f.d.programs[sh.pixel]));
            std::array<BloomArgument,5> args{};args[0]={3,0,0x040000db};
            for(unsigned i=0;i<sh.sampler_count;++i)args[i+1]={4,static_cast<std::uint16_t>(i),sh.codes[i]};
            memory->write(b+0xa00,args);memory->write(b+0x508+12,std::array<std::uint8_t,4>{0,static_cast<std::uint8_t>(sh.sampler_count+1),0,0});}
        for(unsigned id:{3u,6u,25u,26u,27u,28u,29u,30u}){
            memory->write(memory->base+0x80000+id*0x100,ptr(&f.d.textures[id]));memory->write(memory->base+0x41eb1e8+20*id+4,ptr(&f.d.surfaces[id]));
            const auto size=description(id,f.d.resolution_scale);
            memory->write(memory->base+0x41eb1e8+20*id+12,static_cast<std::uint16_t>(size.Width));
            memory->write(memory->base+0x41eb1e8+20*id+14,static_cast<std::uint16_t>(size.Height));}
        memory->write(memory->base+0x3ccbff0,f.d.n.width);memory->write(memory->base+0x3ccbff4,f.d.n.height);
        memory->write(memory->base+0x3ccbfcc,f.d.n.scissor);memory->write(memory->base+0x3ccb340,f.d.n.viewport_constant);
        check(read_bloom_episode_shader(memory->base,layout,{eye,role,0,0},&f.d.n,&f.d.plan),"actual protected source feeds typed getter and full plan fixture");
        if(after_select)after_select(eye,role);
    }
    bool begin(){auto& f=fixture;const auto revision=reuse.bind();check(revision!=0,"fresh actual wrapper backend binds a new reuse lifetime");
        if(!memory)return f.begin();source_context={f.source(),memory->base,&layout,&reuse,{12,2,9,1,7,1,7},revision};
        uncounted_source=make_bloom_episode_reuse_source(source_context);auto source=uncounted_source;
        source.query_guard_owned=nullptr; // Existing matrices remain the legacy contract control.
        source.read_episode=[](void* p,const BloomEpisodeCursor& c,BloomDrawSnapshot* n,BloomEpisodePlan* plan,BloomReadStatus* why) noexcept {
            auto& run=*running;++run.source_reads;return run.uncounted_source.read_episode(p,c,n,plan,why);};
        source.query_guard=grouped?+[](void* p,const BloomEpisodeCursor& c,const BloomDrawSnapshot& n,const BloomDrawCall& call) noexcept {
            auto& run=*running;++run.query_guards;
            if(run.guard_action&&run.guard_action_at==run.query_guards){auto action=std::move(run.guard_action);run.guard_action={};action();}
            if(run.guard_seh)RaiseException(0xe012c077,0,0,nullptr);
            return run.uncounted_source.query_guard&&run.uncounted_source.query_guard(p,c,n,call);}:nullptr;
        if(owned_guard)source.query_guard_owned=+[](void* p,const BloomEpisodeCursor& c,const BloomDrawSnapshot& n,const BloomDrawCall& call) noexcept {
            auto& run=*running;++run.query_guards;
            if(run.guard_action&&run.guard_action_at==run.query_guards){auto action=std::move(run.guard_action);run.guard_action={};action();}
            if(run.guard_seh)RaiseException(0xe012c077,0,0,nullptr);
            return run.uncounted_source.query_guard_owned&&run.uncounted_source.query_guard_owned(p,c,n,call);};
        f.d.correction.configure(true);return f.d.correction.begin({0,12,2,9,GetCurrentThreadId()},1,f.d.api(),source,
            {&f.d,[](void* p,const BloomDrawSnapshot&,const BloomDrawCall&,BloomQuadSnapshot* q) noexcept {auto& d=*static_cast<Device*>(p);++d.quad_copies;*q=d.q;return true;}});
    }
};
alignas(16) unsigned char expected_fp[512],observed_fp[512],saved_fp[512];
void initialize_fp(){_fxsave(saved_fp);__asm {fninit}__asm {fld1}__asm {fldpi}_fxsave(expected_fp);_fxrstor(saved_fp);
    for(unsigned i=160;i<288;++i)expected_fp[i]=static_cast<unsigned char>(i*13+7);const DWORD mx=0x1f80;std::memcpy(expected_fp+24,&mx,4);}
void check_fp(){for(unsigned i=0;i<5;++i)check(observed_fp[i]==expected_fp[i],"active wrapper preserves native x87 environment");
    for(unsigned i=24;i<28;++i)check(observed_fp[i]==expected_fp[i],"active wrapper preserves native MXCSR");
    for(unsigned r=0;r<8;++r)for(unsigned i=0;i<10;++i)check(observed_fp[32+r*16+i]==expected_fp[32+r*16+i],"active wrapper preserves native x87 data");
    for(unsigned i=160;i<288;++i)check(observed_fp[i]==expected_fp[i],"active wrapper preserves native XMM data");}
HRESULT __stdcall no_swap(Device*,UINT,IDirect3DSwapChain9** out){*out=nullptr;return E_NOINTERFACE;}
HRESULT __stdcall original_reset(Device*,D3DPRESENT_PARAMETERS*){++running->resets;return S_OK;}
HRESULT __stdcall original_present(Device*,const RECT*,const RECT*,HWND,const RGNDATA*){return S_OK;}
HRESULT __stdcall create_swap(Device*,D3DPRESENT_PARAMETERS*,IDirect3DSwapChain9** out){*out=nullptr;return E_FAIL;}
HRESULT __stdcall original(Device* d,D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertices,UINT start,UINT primitives){
    auto& run=*running;++d->native_draws;
    if(run.at_original){auto callback=std::move(run.at_original);run.at_original={};callback();}
    check(type==D3DPT_TRIANGLELIST&&base==0&&min==0&&vertices==4&&start==16&&primitives==2,"actual native DIP original seven arguments unchanged");
    check(!set_indexed_draw_callbacks({}),"actual native call retains indexed callback lease");
    if(!run.nested)check(!d->vs->derivative&&!d->ps->derivative&&d->stream==&d->vb&&d->indices==&d->ib,"original native DIP enters with stock state and native bindings");
    run.at_native=d->calls;SetLastError(0x6abc1290);
    if(!run.nested&&run.mode==1)RaiseException(0xe032c012,0,0,nullptr);
    if(!run.nested&&run.mode==2)throw NativeCpp{0x789abc};
    _fxrstor(expected_fp);return run.mode==3?E_FAIL:S_OK;
}
std::uint64_t before(void* context,IDirect3DDevice9* device,const IndexedDrawCall& call) noexcept {
    auto& run=*static_cast<Run*>(context);auto& f=run.fixture;++run.callbacks;
    if(!run.begun){f.d.caller=call.caller;run.begin();run.begun=true;}
    const BloomDrawCall draw{call.caller,static_cast<std::uint32_t>(call.type),call.base_vertex,call.min_vertex,call.num_vertices,call.start_index,call.primitive_count};
    std::uint64_t token{};__try {if(run.reuse.arm(draw))token=f.d.correction.before(device,draw);if(run.mode==4&&token)RaiseException(0xe0120001,0,0,nullptr);}
    __except(EXCEPTION_EXECUTE_HANDLER){f.d.correction.abandon();token=0;}if(!token){run.reuse.report_refusal();run.reuse.close();}run.last_token=token;return token;
}
void after(void* p,std::uint64_t token,HRESULT result) noexcept {auto& run=*static_cast<Run*>(p);run.fixture.d.correction.after(token,result);run.reuse.report_refusal();run.reuse.close();}
void abandon(void* p,std::uint64_t token) noexcept {auto& run=*static_cast<Run*>(p);++run.abandoned;const auto before=run.fixture.d.calls;
    run.reuse.revoke();run.reuse.close();run.fixture.d.correction.abandon(token);check(before==run.fixture.d.calls,"actual native fault callback performs no COM");}
bool reset_before(void* p,IDirect3DDevice9*){auto& run=*static_cast<Run*>(p);run.reuse.revoke();run.fixture.d.current=false;return !run.fixture.d.backend&&!run.fixture.d.correction.busy();}
void prepare(Run& run,void** shared=nullptr){running=&run;auto& f=run.fixture;
    run.table=shared?shared:static_cast<void**>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));check(run.table!=nullptr,"private actual observed vtable");
    if(!shared){std::memcpy(run.table,f.dt.data(),sizeof f.dt);run.table[13]=reinterpret_cast<void*>(create_swap);run.table[14]=reinterpret_cast<void*>(no_swap);run.table[16]=reinterpret_cast<void*>(original_reset);run.table[17]=reinterpret_cast<void*>(original_present);run.table[82]=reinterpret_cast<void*>(original);}f.d.table=run.table;
    if(run.memory){std::vector<unsigned char> code{0x8b,0x44,0x24,0x04,0x8b,0x00,0x8b,0x80,0x48,0x01,0,0};
        for(unsigned i=0;i<7;++i)code.insert(code.end(),{0xff,0x74,0x24,0x1c});check(code.size()==40,"owned forwarding thunk exact native-relative call offset");
        code.insert(code.end(),{0xff,0xd0,0xc2,0x1c,0});const auto entry=run.memory->base+0x30b5d1-40;
        run.memory->commit(entry,code.size());std::memcpy(reinterpret_cast<void*>(entry),code.data(),code.size());DWORD old{};
        check(VirtualProtect(reinterpret_cast<void*>(entry&~std::uintptr_t(4095)),4096,PAGE_EXECUTE_READ,&old)!=0&&FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(entry),code.size()),"only owned fixture thunk made executable and cache flushed");run.thunk=reinterpret_cast<Run::Thunk>(entry);}
    set_observation_active(true);check(set_indexed_draw_callbacks({&run,before,after,abandon})&&set_graphics_callbacks({&run,nullptr,reset_before,nullptr,nullptr}),"actual complete callback registration");check(observe_device(f.d.api(),false),"actual slot82 installed");}
void nested(Run& run){run.nested=true;const auto callback_count=run.callbacks;run.fixture.d.api()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,16,2);run.nested=false;
    check(callback_count==run.callbacks,"nested native DIP forwards without recursive active callbacks");}
bool body(Run* run){auto& f=run->fixture;bool started=false;std::uint32_t index=run->view_count-1;
    const auto draw_plan=run->view_count==2?StereoDrawPlan{1,{0,1},{0,1},false}:auxiliary_draw_plan();
    const bool staged=execute_stereo_draw_plan(draw_plan,[&](std::uint32_t n){index=n;},[&]{
        const auto eye=run->view_count==2?index:index==2?1u:0u;
        for(const auto role:bloom_role_order)if(bloom_role_enabled(role,f.d.plan.enabled[eye])){
            run->select(eye,role);const auto hr=run->thunk?run->thunk(&f.d,D3DPT_TRIANGLELIST,0,0,4,16,2):f.d.api()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,16,2);_fxsave(observed_fp);
            const auto err=GetLastError();check(hr==(run->mode==3?E_FAIL:S_OK)&&err==0x6abc1290,"actual active callback preserves original HRESULT and LastError");check_fp();}
    },[&](unsigned){++f.d.copies;return true;},&started);
    check(started&&staged&&index==run->view_count-1,"actual native scene plan and selected index remain balanced");return f.d.correction.finish(staged);
}
bool backend(Run* run){run->fixture.d.backend=true;bool result=false;__try {result=body(run);}
    __finally {run->reuse.revoke();run->reuse.close();if(AbnormalTermination())run->fixture.d.correction.abandon();else run->fixture.d.correction.end_scope();run->fixture.d.backend=false;}return result;}
bool native_seh(Run* run){__try {backend(run);}__except(GetExceptionCode()==0xe032c012?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;}
void cleanup(Run& run){auto& d=run.fixture.d;d.hook={};d.failure.clear();d.throwing.clear();d.seh_failure.clear();
    d.correction.reconcile_normal(d.api(),true);check(d.correction.retire(),"normal owner retains then retires all active references");
    check(set_indexed_draw_callbacks({})&&set_graphics_callbacks({}),"both actual callback leases released normally");
    // Preserve observed device/vtable addresses until process end, but free the
    // closed private 70MB native address reservation before the next fixture.
    // No source callback or executable thunk remains callable after cleanup.
    run.reuse.retire();run.memory.reset();run.thunk=nullptr;}
void query_group_contract(std::vector<std::unique_ptr<Run>>& runs){
    const auto initial=checks;
    // Native devices normally share their implementation vtable. Reuse one
    // private table for this added matrix instead of exhausting the observer's
    // bounded hook registry with a unique implementation for every case.
    void** shared_table{};
    const auto prepare_case=[&](Run& run){prepare(run,shared_table);if(!shared_table)shared_table=run.table;};
    // Both variants call the actual protected native factory and unchanged
    // indexed wrapper. Only the optional guard pointer is removed for the
    // legacy full-check control. Geometry remains the copied quad fixture.
    for(unsigned mask=0;mask<4;++mask)for(unsigned slot=0;slot<2;++slot){
        std::map<std::string,unsigned> baseline_calls;unsigned baseline_reads{},baseline_current{},baseline_quad{};
        for(bool grouped:{false,true}){runs.push_back(std::make_unique<Run>(mask,true,slot,grouped));auto& run=*runs.back();auto& f=run.fixture;prepare_case(run);
            check(backend(&run)&&f.restored(),"actual grouped and null-guard factory paths complete each optional full pair");
            check(f.d.native_draws==f.d.plan.expected_draws&&f.d.own_draws==f.d.plan.expected_draws,"grouping retains exact native and owned draw counts");
            if(!grouped){baseline_calls=f.d.calls;baseline_reads=run.source_reads;baseline_current=f.d.current_calls;baseline_quad=f.d.quad_copies;
                check(run.query_guards==0,"null guard executes no cheap guards");}
            else {const auto hits=owner_hits(f.d.plan);owner_method_delta(baseline_calls,f.d.calls,hits);
                check(f.d.current_calls+24*hits==baseline_current&&f.d.quad_copies==baseline_quad,"actual factory owner-hit current delta and complete quad barrier count are exact");
                check(run.query_guards>0&&run.source_reads+run.query_guards+6*hits==baseline_reads,"actual full reads, guards and omitted owner guards account for the complete old path");
                const std::array<unsigned,4> old_source{1046,1294,1294,1540},old_guard{10284,12792,12792,15300};
                check(run.source_reads==old_source[mask]&&run.query_guards+6*hits==old_guard[mask],"source-derived delta matches frozen I cold protected-wrapper full-read and guard counts");
                std::printf("query_group_boundary mask=%u buffer=%u draws=%u full_reads=%u grouped_reads=%u guards=%u current=%u typed_methods=%zu\n",mask,slot,f.d.own_draws,baseline_reads,run.source_reads,run.query_guards,f.d.current_calls,f.d.calls.size());}
            cleanup(run);}
    }
    for(const char* operation:{"get_vs","render_state","viewport","declaration_elements","get_matrix","get_native_pc","get_texture","sampler_state","get_pc0"})for(unsigned phase=1;phase<=3;++phase){
        runs.push_back(std::make_unique<Run>(3,true));auto& run=*runs.back();auto& f=run.fixture;prepare_case(run);
        f.d.hook_name=operation;f.d.hook_at=phase*(!std::strcmp(operation,"render_state")?24u:!std::strcmp(operation,"sampler_state")?static_cast<unsigned>(std::size(bloom_replay_sampler_states)):1u);
        f.d.hook=[&]{run.memory->write(run.layout.slots[0].address+0x21cc,0x70001234u);};
        check(!backend(&run)&&!f.d.own_draws&&!f.d.first_set_current,"persistent native-only change at a former getter barrier is caught at full query endpoint before any Set or UP");
        check(run.query_guards>0&&f.restored()&&!f.d.correction.pending(),"all three query phases retain grouped ownership and normal reference cleanup");cleanup(run);
    }
    for(unsigned shader:{0u,1u}){runs.push_back(std::make_unique<Run>(3,true));auto& run=*runs.back();auto& f=run.fixture;prepare_case(run);bool cleared=false,typed_after=false;
        f.d.hook_name="get_vs";f.d.hook=[&]{check(f.d.vs->refs>1,"GetVS has acquired strong typed reference before native shader metadata changes");
            const auto record=run.memory->base+0x30000+(shader?0x904:0x804);run.memory->write(record,0u);cleared=true;
            f.d.hook_name="object_device";f.d.hook_at=shader?2:1;f.d.hook=[&,record]{std::uint32_t word{};std::memcpy(&word,reinterpret_cast<const void*>(record),4);
                check(word==0&&f.d.vs->refs>1&&(!shader||f.d.ps->refs>1),"later typed GetDevice safely uses retained shader objects while native record remains cleared");typed_after=true;};};
        check(!backend(&run)&&cleared&&typed_after&&!f.d.first_set_current&&!f.d.own_draws,"native VS or PS record clear does not dereference raw COM metadata and is refused at full endpoint before Set or UP");
        cleanup(run);check(f.restored(),"all strong shader references return normally after metadata-clear refusal");}
    for(bool grouped:{false,true}){runs.push_back(std::make_unique<Run>(3,true,0,grouped));auto& run=*runs.back();auto& f=run.fixture;prepare_case(run);
        bool restored_native=false;f.d.hook_name="get_vs";f.d.hook=[&]{const auto address=run.layout.slots[0].address+0x21cc;std::uint32_t saved{};std::memcpy(&saved,reinterpret_cast<const void*>(address),4);
            run.memory->write(address,saved^1u);f.d.hook_name="get_ps";f.d.hook=[&,address,saved]{run.memory->write(address,saved);restored_native=true;};};
        const bool accepted=backend(&run);check(accepted==grouped&&restored_native==grouped,"explicit changed contract: grouped endpoint accepts restored native transient; legacy post-getter check refuses before restoration callback");
        check(f.d.own_draws==(grouped?26u:0u)&&f.restored()&&!f.d.correction.pending(),"transient differential has exact own draw count and no unresolved graphics journal");cleanup(run);
    }
    {runs.push_back(std::make_unique<Run>(3,true));auto& run=*runs.back();auto& f=run.fixture;prepare_case(run);std::uintptr_t page{};DWORD previous{};bool page_restored=false;
        f.d.hook_name="get_vs";f.d.hook=[&]{page=run.memory->base+0x30000+7*0x2000;check(VirtualProtect(reinterpret_cast<void*>(page),4096,PAGE_NOACCESS,&previous)!=0,"owned recorded future-plan page made inaccessible after entry");
            run.at_original=[&]{DWORD ignored{};check(VirtualProtect(reinterpret_cast<void*>(page),4096,previous,&ignored)!=0,"after rejected before callback, original fixture call restores only its private page for later role setup");page_restored=true;};};
        check(!backend(&run)&&!f.d.own_draws&&!f.d.first_set_current,"inaccessible recorded span is caught by full query endpoint before mutation");
        check(page&&page_restored,"normal original call ran once after protected endpoint rejection");cleanup(run);
    }
    for(unsigned mode=0;mode<7;++mode){runs.push_back(std::make_unique<Run>(3,true));auto& run=*runs.back();auto& f=run.fixture;prepare_case(run);
        f.d.hook_name="get_vs";f.d.hook=[&]{if(mode==0)f.d.current=false;else if(mode==1)++run.source_context.lifetime.history;
            else if(mode==2)run.source_context.base+=4;else if(mode==3)run.reuse.revoke();else if(mode==4)nested(run);
            else if(mode==5)_mm_setcsr(0x3f80);else f.d.correction.retire();};
        check(!backend(&run)&&!f.d.own_draws&&!f.d.first_set_current,"actual grouped factory rejects current, lifetime, key, revoke, nested witness, FP and retirement changes before Set or UP");
        _mm_setcsr(0x1f80);cleanup(run);
    }
    for(unsigned mode=0;mode<3;++mode){runs.push_back(std::make_unique<Run>(3,true));auto& run=*runs.back();auto& f=run.fixture;prepare_case(run);
        f.d.hook_name="object_release";f.d.hook=[&]{if(mode==0)nested(run);else if(mode==1)f.d.correction.retire();else run.reuse.revoke();};
        check(!backend(&run)&&!f.d.own_draws&&!f.d.first_set_current,"query-side intermediate Release retains post-call ownership and reentry guards");cleanup(run);
    }
    for(const char* operation:{"get_vs","get_native_pc","sampler_state"})for(unsigned mode=0;mode<3;++mode){
        runs.push_back(std::make_unique<Run>(3,true));auto& run=*runs.back();auto& f=run.fixture;prepare_case(run);
        if(mode==0)f.d.failure=operation;else if(mode==1)f.d.throwing=operation;else f.d.seh_failure=operation;
        check(!backend(&run)&&!f.d.own_draws&&!f.d.first_set_current,"actual grouped typed HRESULT, C++ and SEH query failures retain zero mutation");
        if(mode==1)check(f.d.cpp_destroyed>0,"own grouped query exception object is destroyed normally");cleanup(run);
    }
    {runs.push_back(std::make_unique<Run>(3,true));auto& run=*runs.back();auto& f=run.fixture;prepare_case(run);run.select(1,0);
        f.d.hook_name="up";f.d.hook=[&]{f.d.failure="get_pc0";};f.d.backend=true;
        check(run.thunk(&f.d,D3DPT_TRIANGLELIST,0,0,4,16,2)==S_OK,"actual grouped first native draw returns once despite restoration getter refusal");
        f.d.backend=false;run.reuse.revoke();run.reuse.close();f.d.correction.end_scope();
        check(f.d.own_draws==1&&f.d.correction.pending(),"normal grouped operation preserves unresolved journal across scope close");
        f.d.failure.clear();check(f.d.correction.reconcile_normal(f.d.api(),true)&&!f.d.correction.pending()&&f.restored(),"same state restores retained journal at normal boundary");
        const auto own=f.d.own_draws;run.begun=false;check(backend(&run)&&f.d.own_draws==own+26&&f.restored(),"same actual wrapper/session state starts a fresh complete grouped plan after restoration");cleanup(run);
    }
    std::printf("bloom_query_group_actual_wrapper_checks=%u protected_native_factory=1 actual_indexed_wrapper=1 copied_quad_provider=1 native_gpu_calls=0\n",checks-initial);
}
void owner_wrapper_contract(std::vector<std::unique_ptr<Run>>& runs){const auto initial=checks;void** shared{};
    const auto setup=[&](Run& run){prepare(run,shared);if(!shared)shared=run.table;};
    // Completed private devices leave the bounded observer registry only after
    // all callbacks and references retire. Their addresses stay alive in runs.
    const auto finish_case=[](Run& run){cleanup(run);forget_device(run.fixture.d.api());};
    // The protected native factory and the actual wrapper run in both modes.
    // The null control also disables grouping; exact additional owner deltas
    // are asserted separately from that already accepted grouping difference.
    for(unsigned mask=0;mask<4;++mask){std::map<std::string,unsigned> old_calls;unsigned old_source{},old_current{},old_quad{};
        for(bool grouped:{false,true}){runs.push_back(std::make_unique<Run>(mask,true,0,grouped));auto& run=*runs.back();auto& f=run.fixture;setup(run);
            check(backend(&run)&&f.restored(),"owner-proof comparison warms all shaders with a complete real-factory pair");
            run.begun=false;const auto calls=f.d.calls;const auto native=f.d.native_draws,own=f.d.own_draws;
            const auto source=run.source_reads,guards=run.query_guards,current=f.d.current_calls,quad=f.d.quad_copies;f.d.track_owners=true;f.d.owner_values.clear();
            check(backend(&run)&&f.restored()&&!f.d.correction.pending(),"warm owner-proof pair fully restores through actual indexed callbacks");
            const auto delta=call_difference(f.d.calls,calls);const auto hits=owner_hits(f.d.plan);
            check(f.d.native_draws-native==f.d.plan.expected_draws&&f.d.own_draws-own==f.d.plan.expected_draws,"warm owner-proof comparison retains exact native-once and own-UP totals");
            if(!grouped){old_calls=delta;old_source=run.source_reads-source;old_current=f.d.current_calls-current;old_quad=f.d.quad_copies-quad;}
            else {owner_method_delta(old_calls,delta,hits);
                check(old_source==run.source_reads-source+run.query_guards-guards+6*hits&&old_current==f.d.current_calls-current+24*hits,"warm actual protected full-read, current and cheap-guard vectors have only documented owner deltas");
                check(old_quad==f.d.quad_copies-quad&&f.d.owner_values.size()==hits/2,"all full quad checks remain; every draw proves ownership only in its new before query");
                std::printf("owner_proof_warm mask=%u draws=%u hits=%u full=%u guards=%u current=%u quad=%u retained_methods=%zu\n",mask,f.d.plan.expected_draws,hits,run.source_reads-source,run.query_guards-guards,f.d.current_calls-current,f.d.quad_copies-quad,delta.size());}
            finish_case(run);}
    }
    for(unsigned boundary:{1u,2u})for(unsigned action=0;action<13;++action){runs.push_back(std::make_unique<Run>(3,true));auto& run=*runs.back();auto& f=run.fixture;setup(run);
        std::uint64_t accepted{};bool reached=false;std::uint32_t saved_native{};BloomQuadSnapshot saved_quad{};const auto original_guard=run.uncounted_source.query_guard;
        // begin() fills the actual source later; retain that real dependency
        // only after the first accepted before, in the original native call.
        decltype(run.uncounted_source.query_guard) saved_guard=original_guard;
        run.at_original=[&]{accepted=run.last_token;saved_guard=run.uncounted_source.query_guard;saved_quad=f.d.q;
            std::memcpy(&saved_native,reinterpret_cast<const void*>(run.layout.slots[0].address+0x21cc),4);check(accepted!=0,"native original sees a fully accepted before token");
            f.d.hook_name="get_vs";f.d.hook_at=f.d.calls["get_vs"]+1;f.d.hook=[&]{run.guard_action_at=run.query_guards+1+boundary;run.guard_action=[&]{reached=true;
                if(action==0)run.uncounted_source.query_guard=nullptr;else if(action==1)run.guard_seh=true;else if(action==2)run.reuse.revoke();
                else if(action==3){D3DPRESENT_PARAMETERS p{};check(f.d.api()->Reset(&p)==D3DERR_DEVICELOST,"Reset request at owner hit remains deferred under backend lease");}
                else if(action==4)nested(run);else if(action==5){std::thread foreign([&]{run.reuse.revoke();});foreign.join();}
                else if(action==6)f.d.correction.retire();else if(action==7)f.d.correction.after(0,S_OK);
                else if(action==8)_mm_setcsr(0x3f80);else if(action==9)run.memory->write(run.layout.slots[0].address+0x21cc,0x70001234u);
                else if(action==10)++run.source_context.lifetime.history;else if(action==11)f.d.native_pc[7]^=1;else f.d.q.vertices[0]^=1;};};};
        check(!backend(&run)&&reached&&!f.d.own_draws&&!f.d.first_set_current,"both owner-hit guard positions retain dependency, fault, reset, witness, foreign and token revocation");
        check(f.d.native_draws==(action==4?27u:26u)&&!run.resets,"only explicitly nested original adds a native call; deferred Reset never runs original");
        run.uncounted_source.query_guard=saved_guard;run.guard_seh=false;f.d.current=true;
        if(action==9)run.memory->write(run.layout.slots[0].address+0x21cc,saved_native);
        if(action==11)f.d.native_pc[7]^=1;if(action==12)f.d.q=saved_quad;
        const auto methods=f.d.calls;f.d.correction.after(accepted,S_OK);check(methods==f.d.calls&&!f.d.own_draws,"restored actual callback dependency never revives a failed accepted token");
        check(f.d.correction.reconcile_normal(f.d.api(),true)&&f.restored(),"revoked owner proof leaves normal graphics ownership recoverable");
        run.begun=false;check(backend(&run)&&f.d.own_draws==26,"fresh complete before/token republishes after hit failure on the same actual State");finish_case(run);
    }
    // New typed level interfaces must perform their full owner check even if
    // their canonical identity equals the retained original level interface.
    for(unsigned slot=0;slot<2;++slot)for(unsigned fault=0;fault<4;++fault){runs.push_back(std::make_unique<Run>(3,true));auto& run=*runs.back();auto& f=run.fixture;setup(run);
        Object alternate{};Object* texture{};Object* original_level{};bool changed=false;unsigned prefix{};
        // Chain native callbacks until the first ordinary two-sampler draw.
        std::function<void()> select_case;select_case=[&]{if(f.d.n.sampler_count!=2){run.at_original=select_case;return;}
            texture=reinterpret_cast<Object*>(static_cast<std::uintptr_t>(f.d.n.samplers[slot].texture));original_level=texture->level;
            alternate=*original_level;alternate.refs=1;alternate.canonical=original_level;texture->level=&alternate;f.d.preserve_level_canonical=true;
            prefix=f.d.own_draws;changed=true;f.d.track_owners=true;f.d.owner_values.clear();
            if(fault==1)f.d.failure="object_device";if(fault==2)f.d.throwing="object_device";if(fault==3)f.d.seh_failure="object_device";};run.at_original=select_case;
        check(!backend(&run)&&changed&&f.d.own_draws==prefix,"actual native original preserves once semantics while changed typed level refuses its own replay");
        check(f.d.owner_values==std::vector<Object*>{&alternate},"same canonical identity never suppresses full GetDevice for another typed level");
        texture->level=original_level;f.d.preserve_level_canonical=false;f.d.failure.clear();f.d.throwing.clear();f.d.seh_failure.clear();
        check(alternate.refs==1&&f.restored(),"actual wrapper balances alternate typed level and partial owner outputs");finish_case(run);
    }
    for(const char* method:{"object_device","device_query"})for(bool grouped:{false,true}){runs.push_back(std::make_unique<Run>(0,true,0,grouped));auto& run=*runs.back();auto& f=run.fixture;setup(run);
        run.at_original=[&]{f.d.failure=method;};const bool result=backend(&run);
        // The persistent fault is absent from the first reused after interval,
        // but the next draw's full before must observe it at the same addresses.
        check(!result&&f.d.own_draws==(grouped?1u:0u)&&f.d.native_draws==18,"removed hit-only owner fault differs on first draw, but fresh next before still samples GetDevice/QI");
        f.d.failure.clear();finish_case(run);
    }
    // Normal cleanup can reenter through a released before anchor. The original
    // draw remains exactly once; no after-owner proof survives that cleanup.
    {runs.push_back(std::make_unique<Run>(0,true));auto& run=*runs.back();auto& f=run.fixture;setup(run);bool released=false;
        run.at_original=[&]{f.d.hook_name="object_release";f.d.hook=[&]{released=true;nested(run);};f.d.correction.end_scope();};
        check(!backend(&run)&&released&&!f.d.own_draws&&f.d.native_draws==19,"before-anchor Release reentry sees revoked owner proof and only its distinct nested native draw");finish_case(run);}
    // A two-sampler pending journal survives proof invalidation, then a new
    // one-sampler plan must execute fresh full before ownership proofs.
    {runs.push_back(std::make_unique<Run>(3,true));auto& run=*runs.back();auto& f=run.fixture;setup(run);f.d.backend=true;
        for(auto role:bloom_role_order){run.select(1,role);if(f.d.n.sampler_count==2){f.d.hook_name="up";f.d.hook=[&]{f.d.failure="get_pc1";};
                check(run.thunk(&f.d,D3DPT_TRIANGLELIST,0,0,4,16,2)==S_OK,"pending two-sampler original still returns once");break;}
            check(run.thunk(&f.d,D3DPT_TRIANGLELIST,0,0,4,16,2)==S_OK&&!f.d.correction.rejected(),"prefix before two-sampler pending case stays complete");}
        f.d.backend=false;run.reuse.revoke();check(f.d.correction.pending()&&!f.d.correction.finish(true),"owner-proof invalidation never clears pending two-sampler graphics journal");
        const auto methods=f.d.calls;check(!f.d.correction.reconcile_normal(f.d.api(),false)&&methods==f.d.calls,"unusable Reset state retains pending journal without outbound calls");
        f.d.failure.clear();check(f.d.correction.reconcile_normal(f.d.api(),true)&&f.restored(),"normal existing restore path resolves retained sampler1 state");
        run.mask=0;run.begun=false;const auto draws=f.d.own_draws;check(backend(&run)&&f.d.own_draws==draws+18,"same State recovers a full plan beginning with one sampler and fresh owner proofs");finish_case(run);}
    std::printf("owner_proof_actual_wrapper_checks=%u protected_episode=1 copied_quad=1 native_gpu_calls=0\n",checks-initial);
}
void owned_query_contract(std::vector<std::unique_ptr<Run>>& runs){const auto initial=checks;void** shared{};
    const auto setup=[&](Run& run){prepare(run,shared);if(!shared)shared=run.table;};
    const auto done=[&](Run& run){cleanup(run);forget_device(run.fixture.d.api());};
    for(unsigned views:{2u,3u})for(unsigned mask:{0u,3u})for(unsigned slot:{0u,1u}){
        std::map<std::string,unsigned> old_calls;unsigned old_source{},old_guard{},old_current{},old_quad{};
        for(bool owned:{false,true}){runs.push_back(std::make_unique<Run>(mask,true,slot,true,3,views,owned));auto& run=*runs.back();auto& f=run.fixture;setup(run);
            check(backend(&run)&&f.restored()&&!f.d.correction.pending(),"legacy and owned-only guards complete exact3840 native count2/count3 pair");
            check(f.d.native_draws==f.d.plan.expected_draws&&f.d.own_draws==f.d.plan.expected_draws,"owned-only current reduction preserves native-once and all correction draws");
            if(!owned){old_calls=f.d.calls;old_source=run.source_reads;old_guard=run.query_guards;old_current=f.d.current_calls;old_quad=f.d.quad_copies;}
            else {check(f.d.calls==old_calls&&run.source_reads==old_source&&run.query_guards==old_guard&&f.d.quad_copies==old_quad,"owned-only guard preserves every typed method, complete source read, quad read and query boundary");
                check(f.d.current_calls+2*run.query_guards==old_current,"only the two redundant inner owner callbacks per guard are removed");
                std::printf("owned_query_boundary views=%u mask=%u slot=%u source=%u quad=%u guard=%u old_current=%u current=%u typed_methods=%zu\n",views,mask,slot,run.source_reads,f.d.quad_copies,run.query_guards,old_current,f.d.current_calls,f.d.calls.size());}
            done(run);}
    }
    // Each of the before/post-native/post-program query groups retains both
    // owner callbacks around each guard; no typed getter can cross retirement.
    for(unsigned phase:{1u,2u,3u})for(unsigned mutation=0;mutation<11;++mutation){runs.push_back(std::make_unique<Run>(0,true,0,true,3,2,true));auto& run=*runs.back();auto& f=run.fixture;setup(run);
        f.d.hook_name="get_vs";f.d.hook_at=phase;f.d.hook=[&]{
            if(mutation==0)f.d.current=false;if(mutation==1)++run.source_context.lifetime.history;if(mutation==2)++run.source_context.base;
            if(mutation==3)run.reuse.revoke();if(mutation==4)nested(run);if(mutation==5)_mm_setcsr(_mm_getcsr()^0x2000);
            if(mutation==6)f.d.correction.retire();if(mutation==7)run.memory->write(run.layout.slots[0].address+0x21cc,0x70001234u);
            if(mutation==8){std::thread foreign([&]{run.reuse.revoke();});foreign.join();}
            if(mutation>=9)f.d.retire_on_current=f.d.current_calls+(mutation-8);
        };
        check(!backend(&run)&&!f.d.own_draws&&!f.d.first_set_current,"owned-only path rejects current, key, lifetime, FP, nested, native, reset and either retained-current retirement before any Set or UP");
        check(f.restored()&&!f.d.correction.pending(),"owned-only boundary refusal releases current references normally");done(run);
    }
    for(const char* operation:{"get_vs","get_target","get_native_pc","sampler_state"})for(unsigned mode=0;mode<3;++mode){runs.push_back(std::make_unique<Run>(0,true,0,true,3,2,true));auto& run=*runs.back();auto& f=run.fixture;setup(run);
        if(mode==0)f.d.failure=operation;else if(mode==1)f.d.throwing=operation;else f.d.seh_failure=operation;
        check(!backend(&run)&&!f.d.own_draws&&!f.d.first_set_current,"owned-only typed HRESULT, C++ or SEH query failure cannot replay");
        done(run);
    }
    for(bool reentry:{false,true}){runs.push_back(std::make_unique<Run>(0,true,0,true,3,2,true));auto& run=*runs.back();auto& f=run.fixture;setup(run);
        f.d.hook_name="object_release";f.d.hook=[&]{if(reentry)nested(run);else f.d.correction.retire();};
        check(!backend(&run)&&!f.d.own_draws&&!f.d.first_set_current,"owned-only Release retirement or native reentry revokes admission before any mutation");done(run);}
    {runs.push_back(std::make_unique<Run>(0,true,0,true,3,2,true));auto& run=*runs.back();auto& f=run.fixture;setup(run);
        run.after_select=[&](unsigned eye,unsigned){if(eye==1)run.memory->write(run.layout.buffer+0x16cc38,0u);};
        check(!backend(&run)&&f.d.own_draws==9&&f.d.native_draws==18&&f.restored(),"owned-only count2 first eye succeeds but invalid second eye denies full pair");done(run);}
    std::printf("bloom_owned_query_actual_wrapper_checks=%u retained_typed_and_full_native_barriers=1\n",checks-initial);
}
void steady_wrapper_contract(std::vector<std::unique_ptr<Run>>& runs){const auto initial=checks;wchar_t saved[32]{};
    const auto saved_size=GetEnvironmentVariableW(L"BOVR_BLOOM_STEADY_PROFILE",saved,32);
    check(SetEnvironmentVariableW(L"BOVR_BLOOM_STEADY_PROFILE",L"1")!=FALSE,"isolated fixture enables startup-only steady profiling");
    runs.push_back(std::make_unique<Run>(0,true,0,true,3,2,true));auto& run=*runs.back();auto& f=run.fixture;prepare(run,runs.front()->table);
    std::map<std::string,unsigned> baseline;unsigned source{},quad{},guard{},current{};const auto log_start=logs.size();
    for(unsigned round=1;round<=41;++round){run.begun=false;const auto before_calls=f.d.calls;
        const auto before_source=run.source_reads,before_quad=f.d.quad_copies,before_guard=run.query_guards,before_current=f.d.current_calls;
        check(backend(&run)&&f.restored()&&!f.d.correction.pending(),"cold, warm, timed and exhausted real ordinary pairs preserve completion and restoration");
        const auto delta=call_difference(f.d.calls,before_calls);
        if(round==3){baseline=delta;source=run.source_reads-before_source;quad=f.d.quad_copies-before_quad;guard=run.query_guards-before_guard;current=f.d.current_calls-before_current;}
        if(round>=4)check(delta==baseline&&run.source_reads-before_source==source&&f.d.quad_copies-before_quad==quad&&run.query_guards-before_guard==guard&&f.d.current_calls-before_current==current,"steady timing preserves all typed, complete source, quad, guard and owner callbacks");
        unsigned reports{};for(std::size_t i=log_start;i<logs.size();++i)if(logs[i].starts_with("t5_bloom_steady_profile "))++reports;
        check(reports==(round<39?0u:8u),"eight records remain buffered until final sample; exhausted profiling emits nothing more");
    }
    for(std::size_t i=log_start;i<logs.size();++i)if(logs[i].starts_with("t5_bloom_steady_profile ")){const auto& line=logs[i];
        check(line.find("complete=1 abandoned=0 views=2 draws=18 expected=18")!=std::string::npos&&line.find("entered=18,18,18,54,18,18,18,18,18,18")!=std::string::npos,"actual steady counts include all eighteen initial reads/quads and all fifty-four queries");
        check(line.find("source_calls=18,0,0")!=std::string::npos&&line.find("active_mask=0 nested=0 overflow=0")!=std::string::npos&&line.size()<1536,"steady record has accepted source categories, valid nesting and bounded trace size");}
    cleanup(run);forget_device(f.d.api());
    // A separate State spends timed slots on unrelated input and a real
    // rejection as well as successful pairs; rejection logging stays buffered.
    runs.push_back(std::make_unique<Run>(0,true,1,true,3,2,true));auto& edge=*runs.back();auto& d=edge.fixture.d;prepare(edge,runs.front()->table);const auto edge_log=logs.size();
    for(unsigned round=1;round<=39;++round){edge.begun=false;
        if(round==32){edge.select(0,0);edge.begin();edge.begun=true;d.backend=true;
            const auto p=edge.memory->base+0x417eec4;edge.memory->write(p,0x70001234u);
            const auto own=d.own_draws,native=d.native_draws;
            check(edge.thunk(&d,D3DPT_TRIANGLELIST,0,0,4,16,2)==S_OK&&d.own_draws==own&&d.native_draws==native+1,"timed unrelated quad forwards exactly once without correction");d.backend=false;
        }
        if(round==33){d.hook_name="get_vs";d.hook=[&]{d.current=false;};}
        const auto log_boundary=logs.size();const bool result=backend(&edge);
        check(result==(round!=33)&&edge.fixture.restored()&&!d.correction.pending(),"timed current rejection and later ordinary recovery preserve original outcomes");
        if(round==33)for(std::size_t i=log_boundary;i<logs.size();++i)check(!logs[i].starts_with("t5_bloom_correction_rejected "),"measured rejection does not flush a synchronous trace in the callback");
        d.current=true;
    }
    unsigned edge_reports{};for(std::size_t i=edge_log;i<logs.size();++i)if(logs[i].starts_with("t5_bloom_steady_profile ")){++edge_reports;const auto& line=logs[i];
        if(edge_reports==1)check(line.find("source_calls=18,1,0")!=std::string::npos&&line.find("entered=19,19,18,54")!=std::string::npos,"unrelated source category has separate counts without losing successful pair timing");
        if(edge_reports==2)check(line.find("complete=0 abandoned=0 views=2 draws=0 expected=18")!=std::string::npos&&line.find("reason=current_quad_or_state")!=std::string::npos,"failed timed record retains actual rejection rather than claiming complete zero-cost work");}
    check(edge_reports==8,"failed timing sample spends exactly one of eight buffered slots");cleanup(edge);forget_device(d.api());
    check(SetEnvironmentVariableW(L"BOVR_BLOOM_STEADY_PROFILE",saved_size&&saved_size<32?saved:nullptr)!=FALSE,"fixture restores prior startup environment");
    std::printf("steady_actual_wrapper_checks=%u typed_and_native_calls_unchanged=1\n",checks-initial);
}
void stage_wrapper_contract(std::vector<std::unique_ptr<Run>>& runs){const auto initial=checks;void** shared{};
    const auto setup=[&](Run& run){prepare(run,shared);if(!shared)shared=run.table;};
    // Real native episode memory, active reuse factory, actual slot82 wrapper
    // and typed COM fixture. The quad provider remains copied, as in the
    // retained integration tests; this is not a new protected-quad proof.
    for(unsigned mask=0;mask<4;++mask){runs.push_back(std::make_unique<Run>(mask,true));auto& run=*runs.back();auto& f=run.fixture;setup(run);
        const auto start=logs.size();std::map<std::string,unsigned> measured_calls;unsigned measured_source{},measured_guard{},measured_current{};
        for(unsigned attempt=1;attempt<=3;++attempt){run.begun=false;const auto calls=f.d.calls;const auto native=f.d.native_draws,own=f.d.own_draws,copies=f.d.copies;
            const auto source=run.source_reads,guard=run.query_guards,current=f.d.current_calls;
            check(backend(&run)&&f.restored()&&!f.d.correction.pending()&&!run.reuse.open(),"stage wrapper measured and budget-exhausted pairs fully restore and close");
            check(f.d.native_draws-native==f.d.plan.expected_draws&&f.d.own_draws-own==f.d.plan.expected_draws&&f.d.copies-copies==2,"stage instrumentation preserves original-once plus owned-UP and exact pair copies");
            const auto delta=call_difference(f.d.calls,calls);
            if(attempt==2){measured_calls=delta;measured_source=run.source_reads-source;measured_guard=run.query_guards-guard;measured_current=f.d.current_calls-current;}
            if(attempt==3)check(delta==measured_calls&&run.source_reads-source==measured_source&&run.query_guards-guard==measured_guard&&f.d.current_calls-current==measured_current,
                "actual warm2 versus unmeasured3 has identical typed/native-verification/guard/current boundaries");}
        const auto rows=stage_logs(start);check(rows.size()==2,"actual wrapper emits exactly two claimed stage records");
        for(const auto& row:rows){const std::uint64_t d=f.d.plan.expected_draws;expect_stage(row,{3*d-1,d,d,d},{3*d-1,d,d,0},{},{0,0,0,d},true);}
        cleanup(run);
    }
    for(unsigned mode=0;mode<8;++mode){runs.push_back(std::make_unique<Run>(3,true));auto& run=*runs.back();auto& f=run.fixture;setup(run);const auto start=logs.size();
        if(mode<3)run.mode=mode+1;
        if(mode==3)f.d.failure="up";if(mode==4)f.d.throwing="up";if(mode==5)f.d.seh_failure="up";
        if(mode==6){f.d.hook_name="up";f.d.hook=[&]{f.d.failure="get_pc0";};}
        if(mode==7){f.d.hook_name="up";f.d.hook=[&]{f.d.hook_name="get_vs";f.d.hook=[&]{nested(run);};};}
        if(mode<2){bool caught=false;if(mode==0)caught=native_seh(&run);else try{backend(&run);}catch(const NativeCpp& e){caught=e.value==0x789abc;}
            check(caught&&f.d.native_draws==1&&!f.d.own_draws&&run.abandoned==1&&f.d.calls==run.at_native,"stage wrapper leaves native C++/SEH propagation and no-unwind-COM unchanged");
            check(stage_logs(start).empty(),"native unwind and abandon never emit stage logging");check(!f.d.correction.finish(false),"later normal owner can retire faulted timing scope");
        }else check(!backend(&run),"partial timing never changes actual pair refusal");
        auto rows=stage_logs(start);check(rows.size()==1,"one failed attempt is reported only at normal finish");
        if(!rows.empty()){
            if(mode<3)expect_stage(rows[0],{},{},{},{},false);
            else if(mode<6)expect_stage(rows[0],{2,1,1,1},{2,1,0,0},{0,0,1,0},{0,0,0,1},false);
            else if(mode==6){expect_stage(rows[0],{2,1,1,2},{2,1,1,0},{},{0,0,0,2},false);check(f.d.correction.pending(),"normal_void restoration is not a restoration-success assertion");}
            else {const auto restore=rows[0].vector("entered")[3];check(restore>=1&&restore<=2,"restore reentry permits only existing bounded normal retry");
                expect_stage(rows[0],{2,1,1,restore},{2,1,1,0},{},{0,0,0,restore},false);check(f.d.native_draws==27&&f.d.own_draws==1,"timed reentrant restore still forwards only the one distinct nested original");}}
        f.d.failure.clear();f.d.throwing.clear();f.d.seh_failure.clear();f.d.hook={};run.mode=0;
        check(f.d.correction.reconcile_normal(f.d.api(),true)&&f.restored()&&!f.d.correction.pending(),"normal reconciliation resolves retained timing-case bindings");
        check(stage_logs(start).size()==1,"unmeasured reconciliation cannot extend finished stage record");
        run.begun=false;const auto old_own=f.d.own_draws;check(backend(&run)&&f.d.own_draws==old_own+26,"same actual State recovers full second attempt after measured partial/native failure");
        rows=stage_logs(start);check(rows.size()==2,"fresh recovered attempt has its own reset counters");if(rows.size()==2)expect_stage(rows[1],{77,26,26,26},{77,26,26,0},{},{0,0,0,26},true);
        cleanup(run);
    }
    std::printf("bloom_stage_actual_wrapper_checks=%u protected_native_factory=1 actual_indexed_wrapper=1 copied_quad_provider=1 native_gpu_calls=0\n",checks-initial);
}
}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);_mm_setcsr(0x1f80);replay_integration::initialize_fp();
    std::vector<std::unique_ptr<replay_integration::Run>> runs;
    for(unsigned mode=0;mode<8;++mode){runs.push_back(std::make_unique<replay_integration::Run>());auto& run=*runs.back();run.mode=mode;auto& f=run.fixture;replay_integration::prepare(run);
        if(mode==5)f.d.throwing="up";if(mode==6)f.d.seh_failure="up";
        if(mode==7){f.d.hook_name="up";f.d.hook=[&]{D3DPRESENT_PARAMETERS p{};check(f.d.api()->Reset(&p)==D3DERR_DEVICELOST,"actual reentrant Reset is deferred while active backend owns state");};}
        if(mode==1||mode==2){bool caught=false;if(mode==1)caught=replay_integration::native_seh(&run);else try{replay_integration::backend(&run);}catch(const replay_integration::NativeCpp& e){caught=e.value==0x789abc;}
            check(caught&&f.d.native_draws==1&&!f.d.own_draws&&!f.d.copies&&run.abandoned==1,"original native C++/SEH propagates once with no correction or fallback");
            check(f.d.calls==run.at_native&&f.d.programs[0].refs>1,"no COM during complete original native unwind; before references retained");}
        else {const bool completed=replay_integration::backend(&run);check(completed==(mode==0),"exact corrected complete pair only on ordinary complete success");
            check(f.d.native_draws==26&&f.d.copies==2,"all ordinary native role calls and two copies stay exactly once");
            check(f.d.own_draws==(mode==0?26u:mode>=5?1u:0u)&&f.restored(),"only normal native success invokes own UP and all own failures restore");if(mode==7)check(!run.resets,"nested Reset never reaches native original during replay");}
        replay_integration::cleanup(run);
    }
    // Reenter an actual suppressed slot82 from each restore interval. The
    // nested native draw is detected, not prevented or called transparent.
    for(const char* stage:{"get_vs","object_release","set_vs","get_stream","set_stream","set_indices"}){
        runs.push_back(std::make_unique<replay_integration::Run>());auto& run=*runs.back();auto& f=run.fixture;replay_integration::prepare(run);
        f.d.hook_name="up";f.d.hook=[&]{f.d.hook_name=stage;f.d.hook=[&]{replay_integration::nested(run);};};
        const auto old=indexed_draw_reentry_serial();check(!replay_integration::backend(&run)&&indexed_draw_reentry_serial()==old+1,"restore Get/Release/Set nested native call invalidates whole exact pair");
        check(f.d.native_draws==27&&f.d.own_draws==1,"nested original draw forwards exactly once, no recursive correction");
        replay_integration::cleanup(run);check(f.restored(),"normal later reconciliation restores or verifies retained interval journal");
    }
    const auto protected_begin=checks;
    void** protected_table{};
    for(unsigned views:{2u,3u})for(unsigned scale:{1u,3u,4u})for(unsigned mask=0;mask<4;++mask)for(unsigned slot=0;slot<2;++slot){
        runs.push_back(std::make_unique<replay_integration::Run>(mask,true,slot,true,scale,views));auto& run=*runs.back();auto& f=run.fixture;replay_integration::prepare(run,protected_table);if(!protected_table)protected_table=run.table;
        check(replay_integration::backend(&run),"actual protected reader plus reuse factory and wrapper completes current optional pair");
        check(f.d.native_draws==f.d.plan.expected_draws&&f.d.own_draws==f.d.plan.expected_draws&&f.d.copies==2&&!run.reuse.open(),"all roles/eyes retain native-once, per-role refresh and closed backend session");
        check(f.d.n.width==1280*scale&&f.d.n.height==720*scale&&f.d.n.scene_viewport.width==static_cast<int>(640*scale)&&
              f.d.n.scene_viewport.height==static_cast<int>(720*scale),"protected factory reaches final full-resolution target with the actual per-eye viewport at1280 and3840");
        replay_integration::cleanup(run);check(f.restored(),"actual protected factory replay restores stock bindings");forget_device(f.d.api());}
    {runs.push_back(std::make_unique<replay_integration::Run>(0,true,0,true,3,2));auto& run=*runs.back();auto& f=run.fixture;replay_integration::prepare(run,protected_table);
        run.after_select=[&](unsigned eye,unsigned){if(eye==1)run.memory->write(run.layout.buffer+0x16cc38,0u);};
        check(!replay_integration::backend(&run)&&f.d.own_draws==9&&f.d.native_draws==18,
            "ordinary left eye completes but changed right selected index denies the whole pair while all native calls run once");
        replay_integration::cleanup(run);check(f.restored()&&!f.d.correction.pending(),"ordinary second-eye failure restores all owned state");forget_device(f.d.api());}
    for(bool full_canvas:{false,true}){runs.push_back(std::make_unique<replay_integration::Run>(3,true,0,true,3));auto& run=*runs.back();auto& f=run.fixture;replay_integration::prepare(run);
        f.d.hook_name="get_vs";f.d.hook=[&]{auto bad=run.layout.slots[1].display;if(full_canvas)bad={0,0,3840,2160};else ++bad.x;
            run.memory->write(run.memory->base+0x3ccbfbc,bad);};
        check(!replay_integration::backend(&run)&&!f.d.own_draws&&f.restored()&&!f.d.correction.pending(),
              "actual3840 protected source rejects full-canvas or shifted-eye query mutation before replay");replay_integration::cleanup(run);}
    for(unsigned mode=1;mode<=3;++mode){runs.push_back(std::make_unique<replay_integration::Run>(3,true));auto& run=*runs.back();auto& f=run.fixture;run.mode=mode;replay_integration::prepare(run);
        bool caught=false;if(mode==1)caught=replay_integration::native_seh(&run);
        if(mode==2)try{replay_integration::backend(&run);}catch(const replay_integration::NativeCpp& e){caught=e.value==0x789abc;}
        if(mode==3)check(!replay_integration::backend(&run),"actual factory native failed HRESULT refuses own replay");
        else check(caught&&f.d.native_draws==1&&run.abandoned==1&&f.d.calls==run.at_native,"actual protected factory native C++/SEH propagates once with no unwind COM");
        check(!f.d.own_draws&&!run.reuse.open(),"all actual factory native failure exits close recipe without own draw");replay_integration::cleanup(run);}
    for(unsigned mode=0;mode<4;++mode){runs.push_back(std::make_unique<replay_integration::Run>(3,true));auto& run=*runs.back();auto& f=run.fixture;replay_integration::prepare(run);
        const auto log_start=logs.size();
        f.d.hook_name=mode==1?"set_vc":"get_vs";f.d.hook=[&]{
            if(mode<2)run.memory->write(run.layout.slots[0].address+0x21cc,0x70001234u); // Incidental copied history field.
            if(mode==2)run.reuse.revoke();
            if(mode==3){std::thread foreign([&]{run.reuse.revoke();});foreign.join();}
        };
        check(!replay_integration::backend(&run)&&!f.d.own_draws,"captured incidental bytes or reset/foreign revocation never resurrect authority");
        if(mode<2){unsigned refusal_logs=0;for(std::size_t i=log_start;i<logs.size();++i)if(logs[i].starts_with("t5_bloom_read_reuse_rejected ")){
            ++refusal_logs;check(logs[i].find("stage=read_mismatch")!=std::string::npos&&logs[i].find("diagnostic_value_valid=1")!=std::string::npos&&
                logs[i].find("authority_unconfirmed=1 diagnostic_only=1 accepted=0")!=std::string::npos,"real normal callback reports only observed owned mismatch bytes without current authority claim");}
            check(refusal_logs==1,"actual wrapper mismatch diagnostic is reachable exactly once before normal session close");}
        check(f.restored()&&!f.d.correction.pending()&&!run.reuse.open(),"post-set recipe mismatch uses existing journal and normal restoration");replay_integration::cleanup(run);}
    std::printf("bloom_correction_protected_factory_wrapper_checks=%u copied_quad_provider=1 native_episode_reader=1 gpu_calls=0\n",checks-protected_begin);
    replay_integration::query_group_contract(runs);
    replay_integration::stage_wrapper_contract(runs);
    replay_integration::owner_wrapper_contract(runs);
    replay_integration::owned_query_contract(runs);
    replay_integration::steady_wrapper_contract(runs);
    set_observation_active(false);replay_integration::running=nullptr;runs.clear();_fxrstor(replay_integration::saved_fp);
    std::printf("bloom_correction_actual_wrapper_checks=%u failures=%u gpu_calls=0\n",checks,failures);return failures?1:0;
}
