// SPDX-License-Identifier: GPL-3.0-only
#include "t5/bloom_correction_replay.hpp"
#include "t5/bloom_correction_policy.hpp"
#include "t5/bloom_stage_timing.hpp"
#include "t5/bloom_steady_profile.hpp"
#include <cstdio>
#include "loader/trace.hpp"
#include "loader/d3d9_observer.hpp"
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <iterator>
namespace bovr::t5 {
namespace {
// Process-wide diagnostic budget, independent of episode/retirement and the
// attempt/rejection counters. Exhaustion never changes admission.
std::atomic<unsigned> state_vectors{};
static_assert(bloom_embedded::metadata_valid());
template<std::size_t N>void state_words(const std::array<DWORD,N>& words,char(&out)[N*9]) noexcept {
    constexpr char hex[]="0123456789abcdef";
    for(std::size_t i=0;i<N;++i){for(unsigned k=0;k<8;++k)out[i*9+k]=hex[(words[i]>>(28-k*4))&15];out[i*9+8]=i+1==N?'\0':',';}
}
constexpr bool state_ids_match() noexcept {
    constexpr unsigned render[]{7,14,52,185,15,27,206,168,8,22,174,152,161,162,176,194,28,26};
    constexpr unsigned blend[]{19,20,171,207,208,209};
    if constexpr(std::size(render)!=std::size(bloom_replay_states)||std::size(blend)!=std::size(bloom_replay_blend_states))return false;
    for(unsigned i=0;i<std::size(render);++i)if(render[i]!=static_cast<unsigned>(bloom_replay_states[i].state))return false;
    for(unsigned i=0;i<std::size(blend);++i)if(blend[i]!=static_cast<unsigned>(bloom_replay_blend_states[i]))return false;return true;
}
static_assert(state_ids_match());
struct Preserve {alignas(16) unsigned char fp[512];DWORD error;
    Preserve() noexcept {_fxsave(fp);error=GetLastError();}~Preserve(){SetLastError(error);_fxrstor(fp);}};
// This inner SEH frame is deliberately NOT noexcept. A C++ exception reaches
// the outer catch before any noexcept boundary. Neither boundary releases COM.
template<class F>HRESULT contained_seh(const F& f) {__try{return f();}__except(GetExceptionCode()==0xe06d7363?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER){return E_FAIL;}}
template<class F>HRESULT contained(const F& f) noexcept {try{return contained_seh(f);}catch(...){return E_FAIL;}}
template<class T>void release(T*& p) noexcept {auto* old=p;p=nullptr;if(old)contained([&]{old->Release();return S_OK;});}
template<class T>bool same_bytes(const T& a,const T& b) noexcept {return std::memcmp(&a,&b,sizeof(T))==0;}
template<class T>std::uintptr_t address(T* p) noexcept {return reinterpret_cast<std::uintptr_t>(p);}
struct ShaderCache {IDirect3DVertexShader9* vertex{};IDirect3DPixelShader9* pixel{};};
struct TextureBinding {
    IDirect3DBaseTexture9* base{};IDirect3DTexture9* texture{};IDirect3DSurface9* level{};IUnknown* canonical{};
    D3DSURFACE_DESC desc{};std::array<DWORD,std::size(bloom_replay_sampler_states)> states{};
    void clear() noexcept {release(canonical);release(level);release(texture);release(base);}
};
struct Bindings {
    IDirect3DVertexShader9* vertex{};IDirect3DPixelShader9* pixel{};IDirect3DSurface9* target{};IUnknown* canonical{};
    IDirect3DVertexBuffer9* stream{};IDirect3DIndexBuffer9* indices{};IDirect3DVertexDeclaration9* declaration{};
    std::array<TextureBinding,2> textures{};D3DSURFACE_DESC desc{};D3DVIEWPORT9 viewport{};
    std::array<D3DVERTEXELEMENT9,16> elements{};UINT element_count{},offset{},stride{},frequency{};
    std::array<DWORD,std::size(bloom_replay_states)> render{};
    std::array<DWORD,std::size(bloom_replay_blend_states)> blend{};
    std::array<float,16> matrix{};std::array<float,4> vertex_constant{};
    std::array<std::array<float,4>,2> pixel_constants{},uv_constants{};
    std::array<float,800> native_pixel_constants{}; // API c0..c199; never set by this class.
    void clear() noexcept {for(auto& t:textures)t.clear();release(declaration);release(indices);release(stream);
        release(canonical);release(target);release(pixel);release(vertex);}
};
enum Field : unsigned { vertex_field=1, pixel_field=2, vertex_constant_field=4, pixel0_field=8,pixel1_field=16,stream_field=32,index_field=64 };
struct Journal {
    unsigned pending{};bool up_attempted{},up_returned{};
    std::array<float,4> affine{};std::array<std::array<float,4>,2> clamp{};
    IDirect3DVertexShader9* installed_vertex{};IDirect3DPixelShader9* installed_pixel{};
};
bool equal_bindings(const Bindings& a,const Bindings& b) noexcept {
    if(a.vertex!=b.vertex||a.pixel!=b.pixel||a.target!=b.target||a.canonical!=b.canonical||a.stream!=b.stream||a.indices!=b.indices||
       a.declaration!=b.declaration||a.offset!=b.offset||a.stride!=b.stride||a.frequency!=b.frequency||a.element_count!=b.element_count||
       !same_bytes(a.elements,b.elements)||!same_bytes(a.desc,b.desc)||!same_bytes(a.viewport,b.viewport)||a.render!=b.render||a.blend!=b.blend||
       !same_bytes(a.matrix,b.matrix)||!same_bytes(a.vertex_constant,b.vertex_constant)||!same_bytes(a.pixel_constants,b.pixel_constants)||!same_bytes(a.uv_constants,b.uv_constants)||!same_bytes(a.native_pixel_constants,b.native_pixel_constants))return false;
    for(unsigned i=0;i<2;++i){const auto& x=a.textures[i];const auto& y=b.textures[i];if(x.base!=y.base||x.texture!=y.texture||x.level!=y.level||
        x.canonical!=y.canonical||!same_bytes(x.desc,y.desc)||x.states!=y.states)return false;}return true;
}
}
struct BloomCorrectionReplay::State {
    bool enabled{},active{},failed{},open{},touched{},retire_requested{},releasing{},faulted{};
    unsigned operations{},completed{},logs{},logged_attempts{},attempt{};std::uint64_t token{},next_token{},reset_serial{},retire_serial{},device_creation{},witness{};
    std::uint64_t source_reads{},quad_reads{},validated_com_calls{},cpu_ticks{},query_guards{};LARGE_INTEGER tick_frequency{};
    BloomStageTiming stage_timing{};
    BloomSteadyProfile steady{};
    std::atomic<unsigned> leases{};DWORD owner{};IDirect3DDevice9* device{};
    AuxiliaryPostfxKey key{};BloomShaderSource source{};BloomQuadSource quad_source{};
    BloomEpisodeSet episode{};BloomEpisodePlan plan{};bool planned{};
    BloomDrawSnapshot native{};BloomDrawCall draw{};BloomQuadSnapshot quad{};
    Bindings before{},after{};Journal journal{};std::array<ShaderCache,15> shaders{};
    // No additional COM references: only the exact corresponding before slots
    // of this accepted draw may lend their already-proven owning-device fact.
    std::uint64_t owner_proof_token{};bool owner_proof_valid{};
    void revoke_owner_proofs() noexcept {owner_proof_valid=false;}
    const char* reason="none";const char* stage="none";DWORD actual_state{},actual_value{},expected_value{};HRESULT own_hresult{E_PENDING};
    struct Operation {State& s;explicit Operation(State& v):s(v){++s.operations;s.leases.fetch_add(1);}
        ~Operation(){if(s.operations==1&&s.retire_requested&&!s.journal.pending)s.clear();--s.operations;s.leases.fetch_sub(1);}};
    struct Timing {State& s;LARGE_INTEGER start{};bool enabled{};explicit Timing(State& v):s(v),enabled(v.attempt||v.logged_attempts<2){if(enabled&&!QueryPerformanceCounter(&start))enabled=false;}
        ~Timing(){LARGE_INTEGER end{};if(enabled&&QueryPerformanceCounter(&end)&&end.QuadPart>=start.QuadPart)s.cpu_ticks+=static_cast<std::uint64_t>(end.QuadPart-start.QuadPart);}};
    bool same_owner() const noexcept{return !owner||owner==GetCurrentThreadId();}
    void reject(const char* why) noexcept {revoke_owner_proofs();if(!failed)reason=why;failed=true;episode.invalidate();
        if(logs<48){++logs;if(steady.active)return;trace("t5_bloom_correction_rejected","producer=%llu generation=%llu xr_frame=%llu role=%u eye=%u reason=%s stage=%s state=%lu value=%lu expected=%lu touched=%u pending=%u own_hr=0x%08lx witness=%llu",key.producer,key.generation,key.xr_frame,native.role,native.eye,why,stage,actual_state,actual_value,expected_value,touched,journal.pending,static_cast<unsigned long>(own_hresult),indexed_draw_reentry_serial());}}
    bool witness_current() noexcept {const auto now=indexed_draw_reentry_serial();return witness!=indexed_draw_reentry_exhausted&&now==witness;}
    bool local_current() noexcept {return active&&!failed&&!faulted&&!retire_requested&&operations==1&&same_owner()&&witness_current();}
    bool alive() noexcept {if(!local_current()||!source.current)return false;
        const bool current=contained([&]{return source.current(source.context)?S_OK:E_FAIL;})==S_OK;
        return current&&local_current();}
    bool read(BloomDrawSnapshot& n,BloomReadStatus* why=nullptr) noexcept {
        ++source_reads;BloomEpisodePlan p{};if(!source.read_episode||!source.native_draw_return||
           contained([&]{return source.read_episode(source.context,episode.cursor(),&n,&p,why)?S_OK:E_FAIL;})!=S_OK)return false;
        // Reuse only the pure predicates of a fully accepted copied plan. Every
        // existing provider call still runs; equality covers every plan field,
        // including future-eye roles and the tint arithmetic's FP control.
        if((planned?p!=plan:!bloom_plan_valid(p))||!bloom_plan_matches(p,episode.cursor(),n)||n.pass_count!=1||n.pass_index||n.custom_sampler_flags){if(why)*why=BloomReadStatus::changed;return false;}
        if(!planned)for(unsigned e=0;e<2;++e)for(unsigned r=0;r<13;++r)if(bloom_role_enabled(r,p.enabled[e])&&p.roles[e][r].pass_count!=1){if(why)*why=BloomReadStatus::technique;return false;}
        if(!planned){plan=p;planned=true;}return true;
    }
    bool unchanged() noexcept {BloomDrawSnapshot n{};return alive()&&read(n)&&n==native&&alive();}
    bool quad_unchanged() noexcept {++quad_reads;BloomQuadSnapshot q{};return unchanged()&&quad_source.read&&
        contained([&]{return quad_source.read(quad_source.context,native,draw,&q)?S_OK:E_FAIL;})==S_OK&&q==quad&&unchanged();}
    template<class F>bool call(const F& f) noexcept {if(!unchanged())return false;++validated_com_calls;const auto result=contained(f);return result==D3D_OK&&unchanged();}
    bool query_unchanged() noexcept {
        const auto guard=source.query_guard_owned?source.query_guard_owned:source.query_guard;
        if(!guard)return unchanged();
        if(!alive())return false;++query_guards;
        const bool guarded=contained([&]{return guard(source.context,episode.cursor(),native,draw)?S_OK:E_FAIL;})==S_OK;
        return guarded&&alive();
    }
    // Only query collection uses this path. A false optional guard never falls
    // back or retries; complete native/quad barriers still surround the group.
    template<class F>bool query_call(const F& f) noexcept {if(!query_unchanged())return false;++validated_com_calls;const auto result=contained(f);return result==D3D_OK&&query_unchanged();}
    // Reentrant retirement cannot delete State or release a held object under
    // an outbound method: Operation remains counted through the final Release.
    void clear() noexcept {
        revoke_owner_proofs();if(releasing||journal.pending)return;releasing=true;active=false;open=false;source={};quad_source={};
        before.clear();after.clear();for(auto& s:shaders){release(s.vertex);release(s.pixel);}release(device);
        journal={};device_creation=0;retire_requested=false;releasing=false;
    }
    bool query_same_device(IUnknown* value) noexcept {
        IUnknown *a{},*b{};const bool ok=query_call([&]{return device->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&a));})&&a&&
            query_call([&]{return value->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&b));})&&b&&a==b;
        release(b);release(a);return ok&&query_unchanged();
    }
    bool owner_proof_current(std::uint64_t accepted) noexcept {
        return accepted&&owner_proof_valid&&owner_proof_token==accepted&&token==accepted&&
            (source.query_guard_owned||source.query_guard)&&!journal.pending&&local_current();
    }
    template<class T>bool query_owned_device(T* value,T* const& anchor,std::uint64_t accepted) noexcept {
        if(accepted){
            if(!owner_proof_current(accepted)){revoke_owner_proofs();return false;}
            if(value==anchor){
                // Owning-device identity is reused only while the corresponding
                // before reference remains owned. Keep both current guards;
                // removed GetDevice/QI/Release observations are intentional.
                if(!query_unchanged()||!owner_proof_current(accepted)||value!=anchor||
                   !query_unchanged()||!owner_proof_current(accepted)||value!=anchor){revoke_owner_proofs();return false;}
                return true;
            }
        }
        IDirect3DDevice9* found{};const bool ok=query_call([&]{return value->GetDevice(&found);})&&found&&query_same_device(found);release(found);
        const bool current=ok&&query_unchanged()&&(!accepted||owner_proof_current(accepted));
        if(!current)revoke_owner_proofs();return current;
    }
    bool query(Bindings& b,std::uint64_t accepted=0) noexcept {
        if(&b==&before)revoke_owner_proofs();
        // Only the two explicitly matched after queries request reuse. A null
        // guard source keeps its original complete owner-query path. A lost
        // previously published proof never falls back to that path.
        const bool reuse=accepted&&(source.query_guard_owned||source.query_guard||owner_proof_token==accepted);
        if(reuse&&(&b!=&after||!owner_proof_current(accepted))){revoke_owner_proofs();return false;}
        const auto proof=reuse?accepted:0;
        // Never erase the saved originals of a pending restoration. After the
        // existing Releases, reset only unowned query storage, including slots
        // unused by this draw. Release reentry is checked by quad_unchanged.
        if(journal.pending)return false;
        b.clear();b=Bindings{};stage="quad_device";if(!quad_unchanged()||address(device)!=quad.device)return false;
        stage="device_caps";
        D3DDEVICE_CREATION_PARAMETERS creation{};D3DCAPS9 caps{};
        if(!query_call([&]{return device->GetCreationParameters(&creation);})||(creation.BehaviorFlags&D3DCREATE_PUREDEVICE)||
           !query_call([&]{return device->GetDeviceCaps(&caps);})||caps.VertexShaderVersion<D3DVS_VERSION(3,0)||caps.PixelShaderVersion<D3DPS_VERSION(3,0)||
           caps.MaxVertexShaderConst<201||caps.MaxPrimitiveCount<2||caps.MaxStreamStride<32||!caps.NumSimultaneousRTs||caps.NumSimultaneousRTs>4)return false;
        stage="shader_binding";
        if(!query_call([&]{return device->GetVertexShader(&b.vertex);})||!b.vertex||address(b.vertex)!=native.vertex_shader||!query_owned_device(b.vertex,before.vertex,proof)||
           !query_call([&]{return device->GetPixelShader(&b.pixel);})||!b.pixel||address(b.pixel)!=native.pixel_shader||!query_owned_device(b.pixel,before.pixel,proof))return false;
        PostfxResolution scene{};stage="scene_resolution";
        if(!postfx_scene_resolution(native.eye,native.scene_viewport,scene))return false;
        stage="output_surface";
        if(!query_call([&]{return device->GetRenderTarget(0,&b.target);})||!b.target||address(b.target)!=native.native_target.color||!query_owned_device(b.target,before.target,proof)||
           !query_call([&]{return b.target->GetDesc(&b.desc);})||!bloom_replay_desc(b.desc,native.target,scene)||
           !query_call([&]{return b.target->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&b.canonical));})||!b.canonical)return false;
        stage="extra_render_targets";
        for(DWORD i=1;i<caps.NumSimultaneousRTs;++i){IDirect3DSurface9* extra{};if(!query_unchanged())return false;
            const auto hr=contained([&]{return device->GetRenderTarget(i,&extra);});const bool absent=hr==D3DERR_NOTFOUND&&!extra;release(extra);
            if(!absent||!query_unchanged())return false;}
        // Acquire the complete existing 18+6 set before policy evaluation.
        // These are sequential guarded copies, not an atomic device snapshot.
        stage="render_state_query";
        for(unsigned i=0;i<std::size(bloom_replay_states);++i){actual_state=bloom_replay_states[i].state;
            if(!query_call([&]{return device->GetRenderState(bloom_replay_states[i].state,&b.render[i]);}))return false;}
        for(unsigned i=0;i<std::size(bloom_replay_blend_states);++i){actual_state=bloom_replay_blend_states[i];
            if(!query_call([&]{return device->GetRenderState(bloom_replay_blend_states[i],&b.blend[i]);}))return false;}
        if(!quad_unchanged())return false;
        const auto value=[&](D3DRENDERSTATETYPE state){for(unsigned i=0;i<std::size(bloom_replay_states);++i)if(bloom_replay_states[i].state==state)return b.render[i];return DWORD{0xffffffff};};
        stage="render_state";DWORD policy_mask{};actual_state=actual_value=expected_value=0;
        for(unsigned i=0;i<std::size(bloom_replay_states);++i){const auto rule=bloom_replay_states[i];
            // The master FALSE disables stenciling; the front/CCW selector is
            // inactive. Keep its documented BOOL value in both exact snapshots.
            if(rule.state==D3DRS_TWOSIDEDSTENCILMODE&&value(D3DRS_STENCILENABLE)==FALSE&&b.render[i]<=TRUE)continue;
            if(rule.state==D3DRS_SEPARATEALPHABLENDENABLE&&value(D3DRS_ALPHABLENDENABLE)==FALSE&&b.render[i]<=TRUE)continue;
            if(rule.state==D3DRS_ANTIALIASEDLINEENABLE&&draw.type==D3DPT_TRIANGLELIST&&value(D3DRS_FILLMODE)==D3DFILL_SOLID&&b.render[i]<=TRUE)continue;
            // Culling remains active. The mandatory exact current native quad,
            // matrix and pinned VS path retain the verified winding under the
            // positive affine transform. CCW preserves both triangles; CW does not.
            if(rule.state==D3DRS_CULLMODE&&b.render[i]==D3DCULL_CCW&&draw.type==D3DPT_TRIANGLELIST&&value(D3DRS_FILLMODE)==D3DFILL_SOLID)continue;
            // Provisional state acceptance only: programs() must still prove
            // the exact pinned SM3 original AND derivative bytes before Set/UP.
            if(rule.state==D3DRS_FOGENABLE&&b.render[i]<=TRUE)continue;
            // D3D9 specifies these two fields have no effect on our proven
            // single-sample target. Still snapshot/recheck their actual values.
            if(rule.state==D3DRS_MULTISAMPLEANTIALIAS||rule.state==D3DRS_MULTISAMPLEMASK)continue;
            if(b.render[i]!=rule.value){if(!policy_mask){actual_state=rule.state;actual_value=b.render[i];expected_value=rule.value;}policy_mask|=DWORD{1}<<i;}}
        auto vector=state_vectors.load(std::memory_order_relaxed);
        while(vector<2&&!state_vectors.compare_exchange_weak(vector,vector+1,std::memory_order_relaxed)){}
        if(vector<2){char render[std::size(bloom_replay_states)*9]{},blend[std::size(bloom_replay_blend_states)*9]{};
            state_words(b.render,render);state_words(b.blend,blend);
            trace("t5_bloom_correction_state_vector","vector=%u producer=%llu generation=%llu xr_frame=%llu eye=%u role=%u target=%u phase=%s complete=1 render_count=18 render_ids=7,14,52,185,15,27,206,168,8,22,174,152,161,162,176,194,28,26 render_values=%s blend_count=6 blend_ids=19,20,171,207,208,209 blend_values=%s policy_mask=0x%08lx",
                vector+1,key.producer,key.generation,key.xr_frame,native.eye,native.role,native.target,&b==&before?"before_native":"after_native",render,blend,policy_mask);}
        if(policy_mask)return false;
        stage="viewport";if(!query_call([&]{return device->GetViewport(&b.viewport);}))return false;
        const bool affine=bloom_embedded::roles[native.role].affine;
        const D3DVIEWPORT9 expected=affine?D3DVIEWPORT9{0,0,native.width,native.height,0,1}:D3DVIEWPORT9{native.eye*scene.width/2,0,scene.width/2,scene.height,0,1};
        if(!same_bytes(expected,b.viewport))return false;
        stage="stream_index_declaration_binding";
        if(!query_call([&]{return device->GetStreamSource(0,&b.stream,&b.offset,&b.stride);})||!b.stream||address(b.stream)!=quad.stream_buffer||b.offset!=quad.stream_offset||b.stride!=32||b.stride!=quad.stream_stride||!query_owned_device(b.stream,before.stream,proof)||
           !query_call([&]{return device->GetStreamSourceFreq(0,&b.frequency);})||b.frequency!=1||
           !query_call([&]{return device->GetIndices(&b.indices);})||!b.indices||address(b.indices)!=quad.index_buffer||!query_owned_device(b.indices,before.indices,proof)||
           !query_call([&]{return device->GetVertexDeclaration(&b.declaration);})||!b.declaration||address(b.declaration)!=quad.declaration||!query_owned_device(b.declaration,before.declaration,proof))return false;
        stage="full_declaration";b.element_count=static_cast<UINT>(b.elements.size());
        if(!query_call([&]{return b.declaration->GetDeclaration(b.elements.data(),&b.element_count);}))return false;
        if(!bloom_replay_declaration(b.elements.data(),b.element_count)){
            if(logs<48){char hex[257]{};constexpr char digit[]="0123456789abcdef";const auto* bytes=reinterpret_cast<const unsigned char*>(b.elements.data());
                const unsigned amount=(b.element_count<=16?b.element_count:16)*8;for(unsigned i=0;i<amount;++i){hex[i*2]=digit[bytes[i]>>4];hex[i*2+1]=digit[bytes[i]&15];}
                trace("t5_bloom_correction_declaration","producer=%llu eye=%u role=%u elements=%u copied_hex=%s",key.producer,native.eye,native.role,b.element_count,hex);}
            actual_value=b.element_count;return false;
        }
        stage="current_matrix_and_reserved_constants";
        if(!query_call([&]{return device->GetVertexShaderConstantF(0,b.matrix.data(),4);}))return false;
        for(unsigned i=0;i<16;++i){DWORD word{};std::memcpy(&word,&b.matrix[i],4);if(word!=quad.matrix[i]){actual_state=i;actual_value=word;expected_value=quad.matrix[i];return false;}}
        if(!query_call([&]{return device->GetVertexShaderConstantF(200,b.vertex_constant.data(),1);}))return false;
        if(native.role==5||native.role==6){stage="current_blur_vertex_constants";
            for(unsigned row=0;row<2;++row){const DWORD reg=row?36:23;actual_state=reg;actual_value=expected_value=0;
                if(!query_call([&]{return device->GetVertexShaderConstantF(reg,b.uv_constants[row].data(),1);}))return false;
                for(unsigned i=0;i<4;++i){DWORD word{};std::memcpy(&word,&b.uv_constants[row][i],4);
                    if(word!=quad.uv_constants[row][i]){actual_value=word;expected_value=quad.uv_constants[row][i];return false;}}
            }
            actual_state=actual_value=expected_value=0;
        }
        // The exact pinned PS programs consume external float constants only
        // within this bank. Copy its complete raw state, including unused rows;
        // c200/c201 remain the separate owned restoration fields below.
        stage="native_pixel_constants";
        if(!query_call([&]{return device->GetPixelShaderConstantF(0,b.native_pixel_constants.data(),200);}))return false;
        const auto& role=bloom_embedded::roles[native.role];if(native.sampler_count!=role.sampler_count)return false;
        bool seen[2]{};
        for(unsigned i=0;i<native.sampler_count;++i){stage="sampler_mapping";const auto& s=native.samplers[i];if(s.destination>=role.sampler_count||seen[s.destination]||s.code!=role.codes[s.destination]||!s.image||!s.texture)return false;
            seen[s.destination]=true;auto& t=b.textures[s.destination];
            const auto source_target=bloom_code_target(native.role,plan.enabled[native.eye],s.code);
            stage="sampler_texture_level_alias";
            if(!source_target||source_target==native.target||!query_call([&]{return device->GetTexture(s.destination,&t.base);})||!t.base||address(t.base)!=s.texture||!query_owned_device(t.base,before.textures[s.destination].base,proof)||
               !query_call([&]{return t.base->QueryInterface(IID_IDirect3DTexture9,reinterpret_cast<void**>(&t.texture));})||!t.texture||
               !query_call([&]{return t.texture->GetSurfaceLevel(0,&t.level);})||!t.level||!query_owned_device(t.level,before.textures[s.destination].level,proof)||
               !query_call([&]{return t.level->GetDesc(&t.desc);})||!bloom_replay_desc(t.desc,source_target,scene)||
               !query_call([&]{return t.level->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&t.canonical));})||!t.canonical)return false;
            if(t.canonical==b.canonical){actual_state=s.destination;actual_value=static_cast<DWORD>(address(t.canonical));expected_value=static_cast<DWORD>(address(b.canonical));return false;}
            D3DSURFACE_DESC texture_desc{};if(!query_call([&]{return t.texture->GetLevelDesc(0,&texture_desc);})||!same_bytes(texture_desc,t.desc))return false;
            stage="sampler_state";
            for(unsigned k=0;k<std::size(bloom_replay_sampler_states);++k){if(!query_call([&]{return device->GetSamplerState(s.destination,bloom_replay_sampler_states[k],&t.states[k]);}))return false;
                // Exact MIN/MAG LINEAR above excludes anisotropic filtering.
                // Native sampler updates can retain this legal prior degree.
                if(bloom_replay_sampler_states[k]==D3DSAMP_MAXANISOTROPY&&t.states[k]>=1&&t.states[k]<=caps.MaxAnisotropy)continue;
                if(t.states[k]!=bloom_replay_sampler_values[k]){actual_state=bloom_replay_sampler_states[k];actual_value=t.states[k];expected_value=bloom_replay_sampler_states[k]==D3DSAMP_MAXANISOTROPY?caps.MaxAnisotropy:bloom_replay_sampler_values[k];return false;}}
            if(!query_call([&]{return device->GetPixelShaderConstantF(200+s.destination,b.pixel_constants[s.destination].data(),1);}))return false;
        }
        const bool current=quad_unchanged()&&(!reuse||owner_proof_current(accepted));
        if(!current)revoke_owner_proofs();return current;
    }
    template<class T>bool program_bytes(T* object,unsigned index,bool derivative=false,
                                        std::array<std::uint32_t,2048>* captured=nullptr) noexcept {
        const auto& p=bloom_embedded::programs[index];const auto bytes=derivative?p.derivative_bytes:p.original_bytes;
        std::array<std::uint32_t,2048> copied{};UINT n{};
        if(bytes>sizeof(copied)||!call([&]{return object->GetFunction(nullptr,&n);})||n!=bytes)return false;
        if(!call([&]{return object->GetFunction(copied.data(),&n);})||n!=bytes||
           !(derivative?bloom_embedded::derivative_matches(index,copied.data(),bytes):
                        bloom_embedded::original_matches(index,copied.data(),bytes)))return false;
        if(captured)*captured=copied;return true;
    }
    bool programs() noexcept {
        stage="exact_program_bytes";const auto& r=bloom_embedded::roles[native.role];
        std::array<std::uint32_t,2048> vertex_original{},pixel_original{},corrected{};
        if(!program_bytes(before.vertex,r.vertex,false,&vertex_original)||!program_bytes(before.pixel,r.pixel,false,&pixel_original))return false;
        std::size_t corrected_bytes{};
        if(r.affine&&!shaders[r.vertex].vertex){
            if(!bloom_embedded::reconstruct(r.vertex,vertex_original.data(),bloom_embedded::programs[r.vertex].original_bytes,
                                           corrected.data(),corrected.size(),&corrected_bytes)||
               corrected_bytes!=bloom_embedded::programs[r.vertex].derivative_bytes||
               !call([&]{return device->CreateVertexShader(reinterpret_cast<const DWORD*>(corrected.data()),&shaders[r.vertex].vertex);})||
               !shaders[r.vertex].vertex)return false;
        }
        if(!shaders[r.pixel].pixel){
            corrected_bytes=0;
            if(!bloom_embedded::reconstruct(r.pixel,pixel_original.data(),bloom_embedded::programs[r.pixel].original_bytes,
                                           corrected.data(),corrected.size(),&corrected_bytes)||
               corrected_bytes!=bloom_embedded::programs[r.pixel].derivative_bytes||
               !call([&]{return device->CreatePixelShader(reinterpret_cast<const DWORD*>(corrected.data()),&shaders[r.pixel].pixel);})||
               !shaders[r.pixel].pixel)return false;
        }
        if((r.affine&&!program_bytes(shaders[r.vertex].vertex,r.vertex,true))||!program_bytes(shaders[r.pixel].pixel,r.pixel,true))return false;
        journal.installed_vertex=r.affine?shaders[r.vertex].vertex:before.vertex;journal.installed_pixel=shaders[r.pixel].pixel;
        PostfxResolution scene{};if(!postfx_scene_resolution(native.eye,native.scene_viewport,scene))return false;
        journal.affine={static_cast<float>(native.width)/static_cast<float>(scene.width),
            static_cast<float>(native.height)/static_cast<float>(scene.height),static_cast<float>(native.eye*native.width/2),0};
        for(unsigned i=0;i<r.sampler_count;++i)if(!bloom_replay_bounds(native.eye,before.textures[i].desc.Width,before.textures[i].desc.Height,journal.clamp[i]))return false;
        return quad_unchanged();
    }
    template<class F>bool mutate(unsigned field,const F& f) noexcept {if(!quad_unchanged())return false;journal.pending|=field;touched=true;return call(f);}
    bool replay() noexcept {
        const auto& r=bloom_embedded::roles[native.role];
        if(r.affine&&(!mutate(vertex_constant_field,[&]{return device->SetVertexShaderConstantF(200,journal.affine.data(),1);})||
           !mutate(vertex_field,[&]{return device->SetVertexShader(journal.installed_vertex);})))return false;
        for(unsigned i=0;i<r.sampler_count;++i)if(!mutate(i?pixel1_field:pixel0_field,[&]{return device->SetPixelShaderConstantF(200+i,journal.clamp[i].data(),1);}))return false;
        if(!mutate(pixel_field,[&]{return device->SetPixelShader(journal.installed_pixel);})||!quad_unchanged())return false;
        // UP clears stream0 and indices even on an unsuccessful return. Both
        // belong to the journal BEFORE the invocation, not after its HRESULT.
        journal.pending|=stream_field|index_field;journal.up_attempted=true;touched=true;
        bool returned=false;const auto hr=contained([&]{const auto value=device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,4,2,quad.indices.data(),D3DFMT_INDEX16,quad.vertices.data(),32);returned=true;return value;});
        journal.up_returned=returned;own_hresult=hr;return returned&&hr==D3D_OK&&quad_unchanged();
    }
    // No source.current/episode predicate here: stop/reset requests may revoke
    // admission during a call, while this leased device still owns installed
    // fields. Actual current values determine per-field restoration authority.
    bool restore_interval(std::uint64_t serial) noexcept {if(serial==indexed_draw_reentry_exhausted||serial!=indexed_draw_reentry_serial()){reject("restoration_reentry");return false;}return true;}
    template<class T,class Get,class Set>void restore_object(unsigned field,T* original,T* installed,const Get& get,const Set& set,bool up_null=false) noexcept {
        if(!(journal.pending&field))return;const auto serial=indexed_draw_reentry_serial();if(!restore_interval(serial))return;T* current{};
        const auto got=contained([&]{return get(&current);});
        if(got!=D3D_OK||!restore_interval(serial)){release(current);return;}
        if(current==original){release(current);if(restore_interval(serial))journal.pending&=~field;return;}
        const bool owned=current==installed||(up_null&&!current);release(current);
        if(!restore_interval(serial))return;
        if(!owned){journal.pending&=~field;reject("verified_foreign_state");return;}
        // Even failed setters may have installed the original. Verification,
        // not HRESULT alone, decides whether our mutation is still pending.
        contained([&]{return set(original);});if(!restore_interval(serial))return;current=nullptr;
        const bool verified=contained([&]{return get(&current);})==D3D_OK&&current==original;
        release(current);if(restore_interval(serial)&&verified)journal.pending&=~field;
    }
    void restore_constant(unsigned field,bool vertex,unsigned reg,const std::array<float,4>& original,const std::array<float,4>& installed) noexcept {
        if(!(journal.pending&field))return;const auto serial=indexed_draw_reentry_serial();if(!restore_interval(serial))return;std::array<float,4> current{};
        auto get=[&]{return vertex?device->GetVertexShaderConstantF(reg,current.data(),1):device->GetPixelShaderConstantF(reg,current.data(),1);};
        if(contained(get)!=D3D_OK||!restore_interval(serial))return;
        if(same_bytes(current,original)){journal.pending&=~field;return;}
        // Each float component is independently attributable if a failed COM
        // setter changed only part of the vector. Preserve confirmed foreign
        // components, restore only components still equal to our installation.
        bool owned=false,foreign=false;auto desired=current;
        for(unsigned i=0;i<4;++i){std::uint32_t c{},o{},v{};std::memcpy(&c,&current[i],4);std::memcpy(&o,&original[i],4);std::memcpy(&v,&installed[i],4);
            if(c==o)continue;if(c==v){desired[i]=original[i];owned=true;}else foreign=true;}
        if(!owned){journal.pending&=~field;if(foreign)reject("verified_foreign_constant");return;}
        contained([&]{return vertex?device->SetVertexShaderConstantF(reg,desired.data(),1):device->SetPixelShaderConstantF(reg,desired.data(),1);});
        if(!restore_interval(serial))return;
        if(contained(get)==D3D_OK&&restore_interval(serial)&&same_bytes(current,desired)){journal.pending&=~field;if(foreign)reject("verified_foreign_constant");}
    }
    void restore_stream() noexcept {
        if(!(journal.pending&stream_field))return;const auto serial=indexed_draw_reentry_serial();if(!restore_interval(serial))return;IDirect3DVertexBuffer9* current{};UINT offset{},stride{};
        auto get=[&]{return device->GetStreamSource(0,&current,&offset,&stride);};
        if(contained(get)!=D3D_OK||!restore_interval(serial)){release(current);return;}
        if(current==before.stream&&offset==before.offset&&stride==before.stride){release(current);if(restore_interval(serial))journal.pending&=~stream_field;return;}
        const bool owned=!current;release(current);
        if(!restore_interval(serial))return;
        if(!owned){journal.pending&=~stream_field;reject("verified_foreign_stream");return;}
        contained([&]{return device->SetStreamSource(0,before.stream,before.offset,before.stride);});
        if(!restore_interval(serial))return;const bool verified=contained(get)==D3D_OK&&current==before.stream&&offset==before.offset&&stride==before.stride;
        release(current);if(restore_interval(serial)&&verified)journal.pending&=~stream_field;
    }
    void restore() noexcept {
        if(!journal.pending||!device)return;
        restore_object(vertex_field,before.vertex,journal.installed_vertex,[&](auto** p){return device->GetVertexShader(p);},[&](auto* p){return device->SetVertexShader(p);});
        restore_object(pixel_field,before.pixel,journal.installed_pixel,[&](auto** p){return device->GetPixelShader(p);},[&](auto* p){return device->SetPixelShader(p);});
        restore_constant(vertex_constant_field,true,200,before.vertex_constant,journal.affine);
        restore_constant(pixel0_field,false,200,before.pixel_constants[0],journal.clamp[0]);
        restore_constant(pixel1_field,false,201,before.pixel_constants[1],journal.clamp[1]);
        restore_stream();
        restore_object(index_field,before.indices,static_cast<IDirect3DIndexBuffer9*>(nullptr),[&](auto** p){return device->GetIndices(p);},[&](auto* p){return device->SetIndices(p);},true);
        if(journal.pending)reject("restoration_pending");
    }
    // The first successful query precedes attempt admission and is excluded.
    // Exhausted/unclaimed work bypasses every new clock helper. These wrappers
    // end explicitly only after normal return; there is no unwind destructor.
    template<class F>auto steady_call(BloomSteadyStage kind,const F& f) noexcept -> decltype(f()) {
        if(!steady.active)return f();BloomSteadyToken timing_token;
        bloom_steady_begin(steady,kind,timing_token);const auto result=f();bloom_steady_end(steady,timing_token);return result;
    }
    template<class F>void steady_void(BloomSteadyStage kind,const F& f) noexcept {
        if(!steady.active){f();return;}BloomSteadyToken timing_token;
        bloom_steady_begin(steady,kind,timing_token);f();bloom_steady_end(steady,timing_token);
    }
    bool timed_query(Bindings& b,std::uint64_t accepted=0) noexcept {
        if(steady.active)return steady_call(BloomSteadyStage::query,[&]{return query(b,accepted);});
        if(!attempt)return query(b,accepted);BloomStageToken timing;
        bloom_stage_begin(stage_timing,BloomStageKind::query,timing);
        const bool result=query(b,accepted);
        bloom_stage_end(stage_timing,timing,result?BloomStageReturn::normal_true:BloomStageReturn::normal_false);return result;
    }
    bool timed_programs() noexcept {
        if(steady.active)return steady_call(BloomSteadyStage::programs,[&]{return programs();});
        if(!attempt)return programs();BloomStageToken timing;
        bloom_stage_begin(stage_timing,BloomStageKind::programs,timing);
        const bool result=programs();
        bloom_stage_end(stage_timing,timing,result?BloomStageReturn::normal_true:BloomStageReturn::normal_false);return result;
    }
    bool timed_replay() noexcept {
        if(steady.active)return steady_call(BloomSteadyStage::replay,[&]{return replay();});
        if(!attempt)return replay();BloomStageToken timing;
        bloom_stage_begin(stage_timing,BloomStageKind::replay,timing);
        const bool result=replay();
        bloom_stage_end(stage_timing,timing,result?BloomStageReturn::normal_true:BloomStageReturn::normal_false);return result;
    }
    void timed_restore() noexcept {
        if(steady.active){steady_void(BloomSteadyStage::restore,[&]{restore();});return;}
        if(!attempt){restore();return;}BloomStageToken timing;
        bloom_stage_begin(stage_timing,BloomStageKind::restore,timing);restore();
        bloom_stage_end(stage_timing,timing,BloomStageReturn::normal_void);
    }
    void log_steady() noexcept {
        if(steady.active||steady.reported||steady.used!=steady.record_limit)return;
        steady.reported=true;
        for(unsigned sample=0;sample<steady.used;++sample){const auto& r=steady.records[sample];char vectors[4][224]{};
            for(unsigned v=0;v<4;++v){std::size_t offset=0;
                for(unsigned i=0;i<r.stages.size();++i){const auto& c=r.stages[i];
                    const auto value=v==0?c.entered:v==1?c.returned:v==2?c.ticks:c.invalid_clock;
                    const int written=_snprintf_s(vectors[v]+offset,sizeof(vectors[v])-offset,_TRUNCATE,i?",%llu":"%llu",value);
                    if(written<0)break;offset+=static_cast<std::size_t>(written);}}
            trace("t5_bloom_steady_profile","sample=%u warm=%u producer=%llu generation=%llu xr_frame=%llu complete=%u abandoned=%u views=%u draws=%u expected=%u width=%u height=%u pending=%u reason=%s stages=before,source,quad,query,programs,replay,restore,post,cleanup,after entered=%s returned=%s ticks=%s invalid=%s outcomes=%llu,%llu,%llu,%llu,%llu,%llu source_calls=%llu,%llu,%llu source_ticks=%llu,%llu,%llu source_reads=%llu quad_reads=%llu guards=%llu com_calls=%llu active_mask=%u nested=%u overflow=%u qpc_hz=%lld buffered=1",
                sample+1,steady.warm,r.producer,r.generation,r.frame,unsigned(r.complete),unsigned(r.abandoned),r.view_count,r.draws,r.expected,r.width,r.height,r.pending,r.reason,vectors[0],vectors[1],vectors[2],vectors[3],
                r.outcomes[0],r.outcomes[1],r.outcomes[2],r.outcomes[3],r.outcomes[4],r.outcomes[5],r.source_calls[0],r.source_calls[1],r.source_calls[2],r.source_ticks[0],r.source_ticks[1],r.source_ticks[2],r.source_reads,r.quad_reads,r.guards,r.com_calls,r.active_mask,unsigned(r.nested),unsigned(r.overflow),tick_frequency.QuadPart);
        }
    }
    void finish_steady(bool result) noexcept {
        if(steady.active){auto& r=steady.records[steady.used];r.complete=result;r.abandoned=faulted;
            r.view_count=plan.view_count;r.expected=plan.expected_draws;r.draws=completed;r.width=native.scene_viewport.width;r.height=native.scene_viewport.height;
            r.pending=journal.pending;r.reason=reason;r.source_reads=source_reads;r.quad_reads=quad_reads;r.guards=query_guards;r.com_calls=validated_com_calls;}
        steady.finish(result&&planned&&plan.view_count==2&&logged_attempts>=2);log_steady();
    }
    void log_stage_timing(bool complete) noexcept {
        char values[8][84]{};std::uint64_t words[8][4]{};
        for(unsigned i=0;i<4;++i){const auto& c=stage_timing.stages[i];
            words[0][i]=c.entered;words[1][i]=c.normal_true;words[2][i]=c.normal_false;words[3][i]=c.normal_void;
            auto unreturned=c.entered;
            for(const auto n:{c.normal_true,c.normal_false,c.normal_void}){
                if(n>unreturned){stage_timing.overlap=true;unreturned=0;break;}unreturned-=n;}
            words[4][i]=unreturned;words[5][i]=c.valid_pairs;words[6][i]=c.invalid_clock;words[7][i]=c.ticks;
        }
        for(unsigned i=0;i<8;++i)sprintf_s(values[i],"%llu,%llu,%llu,%llu",words[i][0],words[i][1],words[i][2],words[i][3]);
        trace("t5_bloom_stage_timing","attempt=%u producer=%llu generation=%llu xr_frame=%llu complete=%u coverage=after_claim first_preclaim_query_excluded=1 stage_order=query,programs,replay,restore entered=%s normal_true=%s normal_false=%s normal_void=%s unreturned=%s valid_pairs=%s invalid_clock=%s ticks=%s overflow_mask=%u overlap=%u qpc_hz=%lld",
            attempt,key.producer,key.generation,key.xr_frame,static_cast<unsigned>(complete),values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7],stage_timing.overflow_mask,static_cast<unsigned>(stage_timing.overlap),tick_frequency.QuadPart);
    }
};
BloomCorrectionReplay::BloomCorrectionReplay()=default;
BloomCorrectionReplay::~BloomCorrectionReplay()=default;
void BloomCorrectionReplay::configure(bool enabled) noexcept {
    if(!enabled&&!state_)return;Preserve preserve;
    try {if(!state_)state_=std::make_unique<State>();auto& s=*state_;if(!s.same_owner())return;s.revoke_owner_proofs();if(s.operations||s.journal.pending)return;
        if(!s.steady.configured){wchar_t value[3]{};const auto n=GetEnvironmentVariableW(L"BOVR_BLOOM_STEADY_PROFILE",value,3);s.steady.configure(n==1&&value[0]==L'1');}
        s.enabled=enabled;if(enabled){QueryPerformanceFrequency(&s.tick_frequency);trace("t5_bloom_correction_configured","enabled=1 route=post_native_success_up native_draw_unchanged=1 original_programs=15 active_draw_cap=32 logged_attempts=2 rejection_cap=48");}
    }catch(...){if(state_)state_->enabled=false;}
}
bool BloomCorrectionReplay::configured() const noexcept{return state_&&state_->enabled;}
bool BloomCorrectionReplay::begin(const AuxiliaryPostfxKey& key,std::uint64_t reset,IDirect3DDevice9* device,BloomShaderSource source,BloomQuadSource quad) noexcept {
    if(!state_||!state_->same_owner())return false;Preserve preserve;auto& s=*state_;s.revoke_owner_proofs();State::Operation operation(s);
    if(s.operations!=1||s.open||s.journal.pending){s.reject("begin_busy_or_pending");return false;}
    const auto retirement=s.retire_serial;
    if(s.device&&(s.device!=device||s.reset_serial!=reset||s.device_creation!=source.creation_serial)){s.clear();if(retirement!=s.retire_serial)return false;}
    if(!s.enabled||!device||reset==UINT64_MAX||key.owner!=GetCurrentThreadId()||!key.producer||!key.generation||!key.xr_frame||!source.current||!source.read_episode||!source.creation_serial||!source.creation_extended||!quad.read)return false;
    if(!s.device){const auto serial=s.retire_serial;bool acquired=false;
        contained([&]{device->AddRef();acquired=true;return S_OK;});
        if(!acquired){
            // A faulting AddRef has no documented ownership result. Never pair
            // it with Release or use the pointer as an acquired lease. Disable
            // this optional correction; an unknown increment may leak one ref.
            s.enabled=false;s.active=false;s.reject("device_AddRef_unconfirmed");return false;
        }
        s.device=device;
        if(serial!=s.retire_serial){s.reject("device_acquisition_reentry");return false;}}
    s.owner=key.owner;s.key=key;s.reset_serial=reset;s.device_creation=source.creation_serial;s.witness=indexed_draw_reentry_serial();s.source=source;s.quad_source=quad;s.active=true;s.open=false;s.failed=false;s.touched=false;s.faulted=false;s.retire_requested=false;
    s.plan={};s.planned=false;s.episode=BloomEpisodeSet{source.initial_eye};s.native={};s.draw={};s.quad={};s.journal={};s.completed=0;s.attempt=0;s.reason="none";s.stage="none";s.actual_state=0;s.actual_value=0;s.expected_value=0;s.own_hresult=E_PENDING;
    s.source_reads=0;s.quad_reads=0;s.validated_com_calls=0;s.cpu_ticks=0;s.query_guards=0;s.stage_timing={};
    s.steady.begin(source.initial_eye==0,s.logged_attempts>=2,key.producer,key.generation,key.xr_frame);return true;
}
std::uint64_t BloomCorrectionReplay::before(IDirect3DDevice9* device,const BloomDrawCall& draw) noexcept {
    if(!state_||!state_->same_owner())return 0;auto& s=*state_;s.revoke_owner_proofs();if(!s.enabled)return 0;
    Preserve preserve;State::Operation operation(s);State::Timing timing(s);
    return s.steady_call(BloomSteadyStage::before,[&]() noexcept ->std::uint64_t {
    if(s.operations!=1||s.open){s.steady.outcome(BloomSteadyOutcome::early);s.reject("nested_entry");return 0;}
    if(!s.alive()||s.journal.pending){s.steady.outcome(BloomSteadyOutcome::early);return 0;}
    if(s.episode.cursor().finished){s.steady.outcome(BloomSteadyOutcome::finished);return 0;}
    BloomReadStatus why{};BloomDrawSnapshot n{};
    const auto prior=s.steady.active?s.steady.records[s.steady.used].stages[1].ticks:0;
    const bool selected=s.steady_call(BloomSteadyStage::initial_source,[&]{return s.read(n,&why);});
    if(s.steady.active){auto& r=s.steady.records[s.steady.used];const auto category=selected?0u:why==BloomReadStatus::unrelated?1u:2u;
        ++r.source_calls[category];r.source_ticks[category]+=r.stages[1].ticks-prior;}
    if(!selected){s.steady.outcome(why==BloomReadStatus::unrelated?BloomSteadyOutcome::unrelated:BloomSteadyOutcome::changed);if(why!=BloomReadStatus::unrelated)s.reject("selected_native_read");return 0;}
    if(device!=s.device||draw.caller!=s.source.native_draw_return||draw.type!=D3DPT_TRIANGLELIST||draw.base_vertex||draw.min_vertex||draw.num_vertices!=4||draw.primitive_count!=2){s.reject("draw_call");return 0;}
    s.native=n;s.draw=draw;s.stage="initial_quad_read";s.actual_state=s.actual_value=s.expected_value=0;
    ++s.quad_reads;const bool copied=s.steady_call(BloomSteadyStage::initial_quad,[&]{return s.quad_source.read&&contained([&]{return s.quad_source.read(s.quad_source.context,n,draw,&s.quad)?S_OK:E_FAIL;})==S_OK;});
    if(!copied||!s.timed_query(s.before)){s.steady.outcome(BloomSteadyOutcome::quad_state);s.reject("current_quad_or_state");s.before.clear();return 0;}
    if(!s.attempt&&s.logged_attempts<2){s.attempt=++s.logged_attempts;trace("t5_bloom_correction_attempt","attempt=%u producer=%llu generation=%llu xr_frame=%llu expected=%u reset_success_serial=%llu witness=%llu",s.attempt,s.key.producer,s.key.generation,s.key.xr_frame,s.plan.expected_draws,s.reset_serial,s.witness);}
    s.open=true;s.token=++s.next_token;if(!s.token){s.reject("token_overflow");s.open=false;return 0;}
    if(s.source.query_guard_owned||s.source.query_guard){if(!s.local_current()){s.reject("owner_proof_publication");s.open=false;return 0;}
        s.owner_proof_token=s.token;s.owner_proof_valid=true;}s.steady.outcome(BloomSteadyOutcome::accepted);return s.token;
    });
}
void BloomCorrectionReplay::after(std::uint64_t token,HRESULT result) noexcept {
    if(!state_||!state_->same_owner())return;Preserve preserve;auto& s=*state_;State::Operation operation(s);State::Timing timing(s);
    s.steady_void(BloomSteadyStage::after,[&]() noexcept {
    if(s.operations!=1||!s.open||!token||token!=s.token){s.revoke_owner_proofs();if(token)s.reject("completion_token");return;}s.open=false;
    if(result!=D3D_OK){s.reject("native_hresult");s.before.clear();return;}
    if(!s.timed_query(s.after,token)||!equal_bindings(s.before,s.after)||!s.timed_programs()||!s.timed_query(s.after,token)||!equal_bindings(s.before,s.after)){s.reject("post_state_or_program");s.before.clear();s.after.clear();return;}
    s.revoke_owner_proofs();const bool replayed=s.timed_replay();
    // Every own COM exception was contained and has returned normally before
    // restoration. This is never the original native draw's exception route.
    s.timed_restore();
    // A detected reentrant native call can revoke one restoration interval
    // without leaving the safe normal callback. One bounded fresh-query pass
    // may resolve it. Persistent failures remain owned; no native call repeats.
    if(s.journal.pending&&indexed_draw_reentry_serial()!=indexed_draw_reentry_exhausted)s.timed_restore();
    if(!replayed)s.reject("own_replay_failure");
    const bool accepted=s.steady_call(BloomSteadyStage::post_validate,[&]{return replayed&&!s.journal.pending&&!s.failed&&s.quad_unchanged()&&s.episode.accept(s.plan,s.native);});
    if(accepted){++s.completed;
        if(s.attempt)trace("t5_bloom_correction_draw","attempt=%u producer=%llu generation=%llu xr_frame=%llu eye=%u role=%u target=%u draws=%u native_hresult=0 own_up=1 own_hresult=0x%08lx restored=1 witness=%llu",s.attempt,s.key.producer,s.key.generation,s.key.xr_frame,s.native.eye,s.native.role,s.native.target,s.completed,static_cast<unsigned long>(s.own_hresult),s.witness);}
    else s.reject("draw_transaction_incomplete");
    s.steady_void(BloomSteadyStage::cleanup,[&]{s.after.clear();if(!s.journal.pending){s.before.clear();s.journal={};}
        if(!s.witness_current())s.reject("nested_native_draw_witness");});
    });
}
void BloomCorrectionReplay::abandon(std::uint64_t token) noexcept {
    if(!state_||!state_->same_owner())return;auto& s=*state_;s.revoke_owner_proofs();if(token&&token!=s.token)return;
    s.active=false;s.open=false;s.failed=true;s.faulted=true;s.episode.invalidate();s.source={};s.quad_source={};s.retire_requested=false;
}
bool BloomCorrectionReplay::finish(bool staged) noexcept {
    if(!state_||!state_->enabled){if(state_&&state_->same_owner())state_->revoke_owner_proofs();return staged;}
    if(!state_->same_owner())return false;Preserve preserve;auto& s=*state_;s.revoke_owner_proofs();State::Operation operation(s);
    if(s.operations!=1){s.reject("nested_finish");return false;}
    if(s.planned&&!s.witness_current())s.reject("nested_native_draw_witness");
    const bool candidate=staged&&!s.failed&&!s.open&&!s.journal.pending&&(!s.planned||s.episode.complete());
    s.after.clear();if(!s.journal.pending){s.before.clear();s.journal={};}
    const bool result=candidate&&!s.failed&&!s.retire_requested&&(!s.planned||s.alive());
    if(candidate&&!result)s.reject("completion_release_or_current_changed");
    s.active=false;s.open=false;s.source={};s.quad_source={};
    if(s.attempt)s.log_stage_timing(result);
    if(s.attempt)trace("t5_bloom_query_counts","attempt=%u producer=%llu generation=%llu xr_frame=%llu query_guards=%llu",s.attempt,s.key.producer,s.key.generation,s.key.xr_frame,s.query_guards);
    if(s.attempt)trace("t5_bloom_correction_complete","attempt=%u producer=%llu generation=%llu xr_frame=%llu complete=%u draws=%u pending=%u witness=%llu owned_callback_ticks=%llu qpc_hz=%lld validated_com_calls=%llu source_reads=%llu quad_reads=%llu",s.attempt,s.key.producer,s.key.generation,s.key.xr_frame,result,s.completed,s.journal.pending,indexed_draw_reentry_serial(),s.cpu_ticks,s.tick_frequency.QuadPart,s.validated_com_calls,s.source_reads,s.quad_reads);
    s.finish_steady(result);
    s.attempt=0;return result;
}
void BloomCorrectionReplay::end_scope() noexcept {
    if(!state_||!state_->same_owner())return;Preserve preserve;auto& s=*state_;s.revoke_owner_proofs();State::Operation operation(s);
    if(s.active||s.open)s.reject("scope_exit_before_finish");
    s.active=false;s.open=false;s.source={};s.quad_source={};
    if(s.operations==1&&!s.journal.pending){s.before.clear();s.after.clear();s.journal={};}
}
bool BloomCorrectionReplay::retire() noexcept {
    if(!state_)return true;if(!state_->same_owner())return false;Preserve preserve;auto& s=*state_;s.revoke_owner_proofs();State::Operation operation(s);
    ++s.retire_serial;s.retire_requested=true;s.active=false;s.failed=true;return s.operations==1&&!s.journal.pending;
}
bool BloomCorrectionReplay::reconcile_normal(IDirect3DDevice9* device,bool usable) noexcept {
    if(!state_)return true;if(!state_->same_owner())return false;Preserve preserve;auto& s=*state_;s.revoke_owner_proofs();State::Operation operation(s);
    if(s.operations!=1||!usable||s.device!=device)return !s.journal.pending;
    s.restore();if(!s.journal.pending){s.before.clear();s.after.clear();s.journal={};}return !s.journal.pending;
}
bool BloomCorrectionReplay::busy() const noexcept{return state_&&state_->leases.load()!=0;}
bool BloomCorrectionReplay::pending() const noexcept{return state_&&state_->journal.pending!=0;}
bool BloomCorrectionReplay::rejected() const noexcept{return state_&&state_->failed;}
unsigned BloomCorrectionReplay::replayed() const noexcept{return state_?state_->completed:0;}
}
