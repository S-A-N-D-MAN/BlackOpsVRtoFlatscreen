// SPDX-License-Identifier: GPL-3.0-only
// Actual active class and documented typed interfaces, private CPU vtables.
#include "t5/bloom_correction_replay.hpp"
#include "t5/bloom_correction_policy.hpp"
#include "t5/bloom_stage_timing.hpp"
#include "t5/bloom_steady_profile.hpp"
#include "../src/loader/d3d9_observer.cpp"
#include <cstdio>
#include <cstdarg>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <stdexcept>
#include <thread>
namespace {
using namespace bovr;using namespace bovr::t5;
unsigned checks{},failures{};
void check(bool b,const char* s){++checks;if(!b){++failures;std::printf("FAIL %s\n",s);}}
std::vector<std::string> logs;
std::vector<std::string> state_vector_logs(){std::vector<std::string> out;for(const auto& line:logs)if(line.starts_with("t5_bloom_correction_state_vector "))out.push_back(line);return out;}
std::string state_values(const std::map<D3DRENDERSTATETYPE,DWORD>& states,bool blend){std::string out;
    const auto append=[&](D3DRENDERSTATETYPE state){char word[10]{};sprintf_s(word,"%08lx",states.at(state));if(!out.empty())out+=',';out+=word;};
    if(blend){for(auto state:bloom_replay_blend_states)append(state);}else for(const auto& rule:bloom_replay_states)append(rule.state);return out;}
struct Device;
struct OwnCppException {unsigned* destroyed;~OwnCppException(){++*destroyed;}};
struct Object {void** table{};Device* device{};unsigned refs{1};unsigned kind{},target{};Object* level{};Object* canonical{};const std::uint32_t* words{};std::size_t bytes{};bool derivative{};std::vector<std::uint32_t> owned_words{};};
struct Device {
    void** table{};unsigned refs{1};unsigned resolution_scale{1};BloomCorrectionReplay correction;BloomDrawSnapshot n{};BloomQuadSnapshot q{};BloomEpisodePlan plan{};
    std::array<Object,15> programs{};std::array<std::array<std::uint32_t,3>,15> program_words{};
    std::array<Object,31> surfaces{},textures{};Object vb{},ib{},declaration{};
    std::vector<std::unique_ptr<Object>> created;
    // C12j fixture-only returned interfaces; null keeps every inherited getter.
    std::array<Object*,8> returned{};bool track_owners{},preserve_level_canonical{};std::vector<Object*> owner_values;
    Object *vs{},*ps{},*stream{},*indices{};UINT offset{32},stride{32};
    std::array<float,4> vc{7,8,9,10};std::array<std::array<float,4>,2> pc{{{11,12,13,14},{15,16,17,18}}};
    std::array<std::array<std::uint32_t,4>,2> uv{};
    std::array<std::uint32_t,800> native_pc{};
    std::map<D3DRENDERSTATETYPE,DWORD> states;
    std::array<D3DVERTEXELEMENT9,5> elements{{{0,0,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},
        {0,20,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,28,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_NORMAL,0},D3DDECL_END()}};
    unsigned declaration_count{5};
    std::map<std::string,unsigned> calls;std::string failure,throwing,seh_failure,hook_name;unsigned fail_at{},partial_at{},hook_at{},anisotropy{1},expected_device_refs{1};bool add_throw_before{};
    std::function<void()> hook;bool current{true},readable{true},pure{},bad_desc{},bad_matrix{},alias{},extra_rt{},unsupported_sampler{},foreign_shader{},native_geometry{};
    unsigned episode_reads{};std::function<void(const BloomEpisodeCursor&,BloomDrawSnapshot&,BloomEpisodePlan&)> episode_hook;
    bool query_guard_enabled{},query_guard_ok{true};unsigned query_guard_calls{},guard_episode_reads{};
    unsigned quad_copies{},guard_action_at{};std::function<void()> guard_action;
    std::function<void(const BloomEpisodeCursor&,const BloomDrawSnapshot&,const BloomDrawCall&)> query_guard_hook;
    unsigned own_draws{},native_draws{},native_mode{},copies{},cpp_destroyed{},current_calls{},first_set_current{},retire_on_current{};bool backend{};std::uintptr_t caller{0x70b5d3};
    IDirect3DDevice9* api(){return reinterpret_cast<IDirect3DDevice9*>(this);}
    bool step(const char* name){++calls[name];SetLastError(0x12121212);
        if(!first_set_current&&std::strncmp(name,"set_",4)==0)first_set_current=current_calls;
        if(hook&&hook_name==name&&(!hook_at||hook_at==calls[name])){auto cb=std::move(hook);hook={};cb();}
        if(throwing==name)throw OwnCppException{&cpp_destroyed};if(seh_failure==name)RaiseException(0xe012c012,0,0,nullptr);
        return failure!=name||(fail_at&&calls[name]!=fail_at);}
};
std::uint32_t ptr(const void* p){return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));}
ULONG __stdcall add(Device* d){if(d->add_throw_before)throw OwnCppException{&d->cpp_destroyed};++d->refs;d->step("device_add");return d->refs;}
ULONG __stdcall rel(Device* d){check(d->refs>1,"only owned device refs released");--d->refs;d->step("device_release");return d->refs;}
HRESULT __stdcall qi(Device* d,REFIID id,void** out){check(id==IID_IUnknown,"leased device canonical only");*out=d;add(d);return d->step("device_query")?S_OK:E_FAIL;}
ULONG __stdcall oa(Object* o){check(o->refs>0,"live object AddRef");return ++o->refs;}
ULONG __stdcall orl(Object* o){check(o->refs>(o->derivative?0u:1u),"only owned object ref released");--o->refs;o->device->step("object_release");return o->refs;}
HRESULT __stdcall oq(Object* o,REFIID id,void** out){check(o->refs>1,"only lease may QI resource");auto* result=id==IID_IUnknown?(o->canonical?o->canonical:o):o;
    check(id==IID_IUnknown||id==IID_IDirect3DTexture9,"typed texture or canonical QI");*out=result;oa(result);return o->device->step("object_query")?S_OK:E_FAIL;}
HRESULT __stdcall od(Object* o,IDirect3DDevice9** out){check(o->refs>1,"GetDevice under lease");*out=o->device->api();add(o->device);if(o->device->track_owners)o->device->owner_values.push_back(o);return o->device->step("object_device")?S_OK:E_FAIL;}
HRESULT __stdcall function(Object* o,void* data,UINT* bytes){check(o->refs>1||o->derivative,"program lease");if(!o->device->step(data?"function_data":"function_size"))return E_FAIL;
    if(!data){*bytes=static_cast<UINT>(o->bytes);return S_OK;}if(*bytes<o->bytes)return E_FAIL;std::memcpy(data,o->words,o->bytes);*bytes=static_cast<UINT>(o->bytes);return S_OK;}
D3DSURFACE_DESC description(unsigned target,unsigned scale=1){const auto w=target==3||target==6?1280u:target==25?640u:target==26?320u:160u;
    return {target==3||target==6||target==30?D3DFMT_A8R8G8B8:D3DFMT_A16B16G16R16,D3DRTYPE_SURFACE,D3DUSAGE_RENDERTARGET,D3DPOOL_DEFAULT,D3DMULTISAMPLE_NONE,0,w*scale,w*9/16*scale};}
HRESULT __stdcall desc(Object* o,D3DSURFACE_DESC* out){*out=description(o->target,o->device->resolution_scale);if(o->device->bad_desc)out->MultiSampleType=D3DMULTISAMPLE_2_SAMPLES;return o->device->step("desc")?S_OK:E_FAIL;}
HRESULT __stdcall level_desc(Object* o,UINT i,D3DSURFACE_DESC* out){check(i==0,"exact level0");return desc(o,out);}
HRESULT __stdcall level_surface(Object* o,UINT i,IDirect3DSurface9** out){check(i==0&&o->refs>1,"typed retained 2D level0");*out=reinterpret_cast<IDirect3DSurface9*>(o->level);oa(o->level);return o->device->step("texture_level")?S_OK:E_FAIL;}
HRESULT __stdcall decl(Object* o,D3DVERTEXELEMENT9* out,UINT* amount){auto& d=*o->device;check(*amount>=d.declaration_count&&d.declaration_count<=d.elements.size(),"bounded declaration output capacity");*amount=d.declaration_count;std::memcpy(out,d.elements.data(),d.declaration_count*sizeof(D3DVERTEXELEMENT9));return d.step("declaration_elements")?S_OK:E_FAIL;}
HRESULT __stdcall cp(Device* d,D3DDEVICE_CREATION_PARAMETERS* out){*out={};out->BehaviorFlags=d->pure?D3DCREATE_PUREDEVICE:0;return d->step("creation")?S_OK:E_FAIL;}
HRESULT __stdcall caps(Device* d,D3DCAPS9* out){*out={};out->VertexShaderVersion=D3DVS_VERSION(3,0);out->PixelShaderVersion=D3DPS_VERSION(3,0);out->MaxVertexShaderConst=256;out->MaxPrimitiveCount=8192;out->MaxStreamStride=256;out->NumSimultaneousRTs=4;out->MaxAnisotropy=16;return d->step("caps")?S_OK:E_FAIL;}
HRESULT __stdcall gv(Device* d,IDirect3DVertexShader9** out){auto* o=d->returned[0]?d->returned[0]:d->vs;*out=reinterpret_cast<IDirect3DVertexShader9*>(o);if(o)oa(o);return d->step("get_vs")?S_OK:E_FAIL;}
HRESULT __stdcall gp(Device* d,IDirect3DPixelShader9** out){auto* o=d->returned[1]?d->returned[1]:d->ps;*out=reinterpret_cast<IDirect3DPixelShader9*>(o);if(o)oa(o);return d->step("get_ps")?S_OK:E_FAIL;}
HRESULT __stdcall sv(Device* d,IDirect3DVertexShader9* in){d->vs=reinterpret_cast<Object*>(in);return d->step("set_vs")?S_OK:E_FAIL;}
HRESULT __stdcall sp(Device* d,IDirect3DPixelShader9* in){d->ps=reinterpret_cast<Object*>(in);return d->step("set_ps")?S_OK:E_FAIL;}
HRESULT __stdcall gvc(Device* d,UINT reg,float* out,UINT amount){check((reg==0&&amount==4)||((reg==200||reg==23||reg==36)&&amount==1),"only WVP, native blur rows and reserved VS constant");
    if(reg==0){std::memcpy(out,d->q.matrix.data(),64);if(d->bad_matrix)out[0]+=1;}
    else if(reg==23||reg==36)std::memcpy(out,d->uv[reg==23?0:1].data(),16);else std::memcpy(out,d->vc.data(),16);
    return d->step(reg==0?"get_matrix":reg==23?"get_uv23":reg==36?"get_uv36":"get_vc")?S_OK:E_FAIL;}
HRESULT __stdcall gpc(Device* d,UINT reg,float* out,UINT amount){check((reg==0&&amount==200)||(reg>=200&&reg<=201&&amount==1),"complete PS bank or separate reserved PS row only");
    if(reg==0)std::memcpy(out,d->native_pc.data(),sizeof d->native_pc);else std::memcpy(out,d->pc[reg-200].data(),16);
    return d->step(reg==0?"get_native_pc":reg==200?"get_pc0":"get_pc1")?S_OK:E_FAIL;}
HRESULT __stdcall svc(Device* d,UINT reg,const float* in,UINT amount){check(reg==200&&amount==1,"one reserved VS row");
    std::memcpy(d->vc.data(),in,d->partial_at==d->calls["set_vc"]+1?4:16);return d->step("set_vc")?S_OK:E_FAIL;}
HRESULT __stdcall spc(Device* d,UINT reg,const float* in,UINT amount){check(reg>=200&&reg<=201&&amount==1,"one reserved PS row");
    const char* name=reg==200?"set_pc0":"set_pc1";std::memcpy(d->pc[reg-200].data(),in,d->partial_at==d->calls[name]+1?4:16);return d->step(name)?S_OK:E_FAIL;}
HRESULT __stdcall target(Device* d,DWORD i,IDirect3DSurface9** out){if(i&&!d->extra_rt){*out=nullptr;return D3DERR_NOTFOUND;}auto* o=d->returned[2]?d->returned[2]:&d->surfaces[d->n.target];*out=reinterpret_cast<IDirect3DSurface9*>(o);oa(o);return d->step("get_target")?S_OK:E_FAIL;}
HRESULT __stdcall vp(Device* d,D3DVIEWPORT9* out){*out=bloom_embedded::roles[d->n.role].affine?D3DVIEWPORT9{0,0,d->n.width,d->n.height,0,1}:D3DVIEWPORT9{d->n.eye*640*d->resolution_scale,0,640*d->resolution_scale,720*d->resolution_scale,0,1};return d->step("viewport")?S_OK:E_FAIL;}
HRESULT __stdcall rs(Device* d,D3DRENDERSTATETYPE type,DWORD* out){*out=d->states[type];return d->step("render_state")?S_OK:E_FAIL;}
HRESULT __stdcall tex(Device* d,DWORD slot,IDirect3DBaseTexture9** out){const auto target=bloom_code_target(d->n.role,d->plan.enabled[d->n.eye],bloom_embedded::roles[d->n.role].codes[slot]);auto* o=d->returned[6+slot]?d->returned[6+slot]:&d->textures[target];
    if(!d->preserve_level_canonical)o->level->canonical=d->alias?&d->surfaces[d->n.target]:nullptr;*out=reinterpret_cast<IDirect3DBaseTexture9*>(o);oa(o);return d->step("get_texture")?S_OK:E_FAIL;}
HRESULT __stdcall ss(Device* d,DWORD slot,D3DSAMPLERSTATETYPE type,DWORD* out){check(slot<2,"known shader sampler");
    unsigned i=0;while(i<std::size(bloom_replay_sampler_states)&&bloom_replay_sampler_states[i]!=type)++i;check(i<std::size(bloom_replay_sampler_states),"known sampler state");*out=bloom_replay_sampler_values[i];if(type==D3DSAMP_MAXANISOTROPY)*out=d->anisotropy;if(d->unsupported_sampler&&type==D3DSAMP_ADDRESSU)*out=D3DTADDRESS_WRAP;return d->step("sampler_state")?S_OK:E_FAIL;}
HRESULT __stdcall gs(Device* d,UINT stream,IDirect3DVertexBuffer9** out,UINT* offset,UINT* stride){check(stream==0,"stream0 only");auto* o=d->returned[3]?d->returned[3]:d->stream;*out=reinterpret_cast<IDirect3DVertexBuffer9*>(o);*offset=d->offset;*stride=d->stride;if(o)oa(o);return d->step("get_stream")?S_OK:E_FAIL;}
HRESULT __stdcall setstream(Device* d,UINT stream,IDirect3DVertexBuffer9* in,UINT offset,UINT stride){check(stream==0,"restore stream0 only");d->stream=reinterpret_cast<Object*>(in);d->offset=offset;d->stride=stride;return d->step("set_stream")?S_OK:E_FAIL;}
HRESULT __stdcall freq(Device* d,UINT stream,UINT* out){check(stream==0,"referenced stream frequency");*out=1;return d->step("frequency")?S_OK:E_FAIL;}
HRESULT __stdcall gi(Device* d,IDirect3DIndexBuffer9** out){auto* o=d->returned[4]?d->returned[4]:d->indices;*out=reinterpret_cast<IDirect3DIndexBuffer9*>(o);if(o)oa(o);return d->step("get_indices")?S_OK:E_FAIL;}
HRESULT __stdcall si(Device* d,IDirect3DIndexBuffer9* in){d->indices=reinterpret_cast<Object*>(in);return d->step("set_indices")?S_OK:E_FAIL;}
HRESULT __stdcall gd(Device* d,IDirect3DVertexDeclaration9** out){auto* o=d->returned[5]?d->returned[5]:&d->declaration;*out=reinterpret_cast<IDirect3DVertexDeclaration9*>(o);oa(o);return d->step("get_declaration")?S_OK:E_FAIL;}
HRESULT create(Device* d,const DWORD* words,void** out,bool vertex){auto o=std::make_unique<Object>();o->table=d->programs[0].table;o->device=d;o->derivative=true;
    bool found=false;for(unsigned i=0;i<std::size(bloom_embedded::programs);++i){const auto& p=bloom_embedded::programs[i];
        if(p.vertex==vertex&&bloom_embedded::derivative_matches(i,words,p.derivative_bytes)){o->bytes=p.derivative_bytes;found=true;break;}}
    if(found){o->owned_words.assign(words,words+o->bytes/4);o->words=o->owned_words.data();}
    check(found,"only an exact reconstructed derivative can be created");*out=o.get();d->created.push_back(std::move(o));return d->step(vertex?"create_vs":"create_ps")?S_OK:E_FAIL;}
HRESULT __stdcall cv(Device* d,const DWORD* p,IDirect3DVertexShader9** out){return create(d,p,reinterpret_cast<void**>(out),true);}
HRESULT __stdcall cps(Device* d,const DWORD* p,IDirect3DPixelShader9** out){return create(d,p,reinterpret_cast<void**>(out),false);}
HRESULT __stdcall up(Device* d,D3DPRIMITIVETYPE type,UINT min,UINT vertices,UINT primitives,const void* indices,D3DFORMAT format,const void* data,UINT stride){++d->own_draws;
    check(type==D3DPT_TRIANGLELIST&&min==0&&vertices==4&&primitives==2&&format==D3DFMT_INDEX16&&stride==32,"own UP exact independent geometry contract");
    check(!std::memcmp(indices,d->q.indices.data(),12)&&!std::memcmp(data,d->q.vertices.data(),128),"own UP uses owned 140 copied bytes");
    const auto& r=bloom_embedded::roles[d->n.role];check(d->ps->derivative&&(r.affine?d->vs->derivative:!d->vs->derivative),"exact corrected program selection, original final VS");
    for(unsigned i=0;i<r.sampler_count;++i){std::array<float,4> expected{};const auto target=bloom_code_target(d->n.role,d->plan.enabled[d->n.eye],r.codes[i]);auto ds=description(target,d->resolution_scale);bloom_replay_bounds(d->n.eye,ds.Width,ds.Height,expected);check(!std::memcmp(expected.data(),d->pc[i].data(),16),"per-input inward half-texel constants use current ping/pong plan");}
    if(r.affine){const float w=float(1280*d->resolution_scale),h=float(720*d->resolution_scale);
        check(d->vc==std::array<float,4>{d->n.width/w,d->n.height/h,float(d->n.eye*d->n.width/2),0},
            "affine maps native eye geometry to the current packed mip, independent of source resolution");}
    d->stream=nullptr;d->indices=nullptr;d->offset=0;d->stride=0;return d->step("up")?S_OK:E_FAIL;}
struct Fixture {
    std::array<void*,119> dt{};std::array<void*,5> shader{},decltab{},buffer{};std::array<void*,17> surface{};std::array<void*,22> texture{};Device d;
    Fixture(unsigned mask=3){
        dt[0]=reinterpret_cast<void*>(qi);dt[1]=reinterpret_cast<void*>(add);dt[2]=reinterpret_cast<void*>(rel);dt[7]=reinterpret_cast<void*>(caps);dt[9]=reinterpret_cast<void*>(cp);dt[38]=reinterpret_cast<void*>(target);dt[48]=reinterpret_cast<void*>(vp);dt[58]=reinterpret_cast<void*>(rs);dt[64]=reinterpret_cast<void*>(tex);dt[68]=reinterpret_cast<void*>(ss);dt[84]=reinterpret_cast<void*>(up);
        dt[88]=reinterpret_cast<void*>(gd);dt[91]=reinterpret_cast<void*>(cv);dt[92]=reinterpret_cast<void*>(sv);dt[93]=reinterpret_cast<void*>(gv);dt[94]=reinterpret_cast<void*>(svc);dt[95]=reinterpret_cast<void*>(gvc);dt[100]=reinterpret_cast<void*>(setstream);dt[101]=reinterpret_cast<void*>(gs);dt[103]=reinterpret_cast<void*>(freq);dt[104]=reinterpret_cast<void*>(si);dt[105]=reinterpret_cast<void*>(gi);dt[106]=reinterpret_cast<void*>(cps);dt[107]=reinterpret_cast<void*>(sp);dt[108]=reinterpret_cast<void*>(gp);dt[109]=reinterpret_cast<void*>(spc);dt[110]=reinterpret_cast<void*>(gpc);
        for(auto* t:{shader.data(),decltab.data(),buffer.data(),surface.data(),texture.data()}){t[0]=reinterpret_cast<void*>(oq);t[1]=reinterpret_cast<void*>(oa);t[2]=reinterpret_cast<void*>(orl);t[3]=reinterpret_cast<void*>(od);}shader[4]=reinterpret_cast<void*>(function);decltab[4]=reinterpret_cast<void*>(decl);surface[12]=reinterpret_cast<void*>(desc);texture[17]=reinterpret_cast<void*>(level_desc);texture[18]=reinterpret_cast<void*>(level_surface);
        d.table=dt.data();for(unsigned i=0;i<15;++i){std::size_t bytes{};
            check(bloom_embedded::authored_test_original(i,d.program_words[i].data(),d.program_words[i].size(),&bytes),"authored shader fixture generated");
            d.programs[i]={shader.data(),&d,1,0,0,nullptr,nullptr,d.program_words[i].data(),bytes,false};}
        for(unsigned i=0;i<31;++i){d.surfaces[i]={surface.data(),&d,1,0,i};d.textures[i]={texture.data(),&d,1,0,i,&d.surfaces[i]};}
        d.vb={buffer.data(),&d};d.ib={buffer.data(),&d};d.declaration={decltab.data(),&d};d.stream=&d.vb;d.indices=&d.ib;
        for(const auto& rule:bloom_replay_states)d.states[rule.state]=rule.value;
        auto& p=d.plan;p.version=1;p.buffer=0x1000;p.native_frame=2;p.view_count=3;p.views={0x2000,0x6000};p.fp_control=0x1f80;p.enabled={mask,mask};
        for(unsigned e=0;e<2;++e){if(mask&1)p.tint_bits[e][0]=0x3f800000;if(mask&2)p.tint_bits[e][3]=0x3f800000;
            for(unsigned r=0;r<13;++r){if(!bloom_role_enabled(r,mask))continue;++p.role_counts[e];auto& v=p.roles[e][r];const unsigned alias=r==11?9:r;v.material=0x10000+alias*0x100;v.techset=v.material+4;v.technique=v.material+8;v.pass_count=1;
                const auto& sh=bloom_embedded::roles[r];v.passes[0]={v.technique+8,v.material+0x20,v.material+0x24,ptr(&d.programs[sh.vertex]),ptr(&d.programs[sh.pixel]),0,0};++p.expected_draws;}}
        d.q.device=ptr(&d);d.q.stream_buffer=ptr(&d.vb);d.q.stream_offset=32;d.q.stream_stride=32;d.q.index_buffer=ptr(&d.ib);d.q.declaration=ptr(&d.declaration);
        d.q.indices={3,0,2,2,0,1};for(unsigned i=0;i<32;++i)d.q.vertices[i]=0x10000+i;
        for(unsigned i=0;i<4;++i)d.q.matrix[i*5]=0x3f800000;select(1,0);
    }
    ~Fixture(){d.failure.clear();d.throwing.clear();d.seh_failure.clear();d.hook={};d.correction.reconcile_normal(d.api(),true);d.correction.retire();
        check(d.refs==d.expected_device_refs,"all confirmed device references returned; ambiguous AddRef never fabricated a Release");for(auto& o:d.programs)check(o.refs==1,"original shader refs unchanged");for(auto& o:d.created)check(o->refs==0,"owned derivative retired");
        for(auto& o:d.surfaces)check(o.refs==1,"target/level refs returned");for(auto& o:d.textures)check(o.refs==1,"typed/base texture refs returned");check(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1,"saved binding/declaration refs returned");}
    void select(unsigned eye,unsigned role){auto& n=d.n;n={};n.buffer=d.plan.buffer;n.native_frame=d.plan.native_frame;n.view_count=3;n.view=d.plan.views[eye];n.source_view=n.view;n.eye=eye;n.selected=eye?2:0;n.role=role;n.target=bloom_role_target(role,d.plan.enabled[eye]);const auto desc=description(n.target,d.resolution_scale);n.width=desc.Width;n.height=desc.Height;
        const auto& r=d.plan.roles[eye][role];const auto& pass=r.passes[0];n.material=r.material;n.techset=r.techset;n.technique=r.technique;n.pass_count=1;n.pass=pass.pass;n.vertex_record=pass.vertex_record;n.pixel_record=pass.pixel_record;n.vertex_shader=pass.vertex_shader;n.pixel_shader=pass.pixel_shader;
        n.native_target={n.target==3?0u:0x7000u,ptr(&d.surfaces[n.target]),0,static_cast<std::uint16_t>(n.width),static_cast<std::uint16_t>(n.height)};n.scene={1,2,3,4,5};n.scene_viewport={static_cast<int>(eye*640*d.resolution_scale),0,static_cast<int>(640*d.resolution_scale),static_cast<int>(720*d.resolution_scale)};
        const auto& sh=bloom_embedded::roles[role];n.sampler_count=sh.sampler_count;for(unsigned i=0;i<sh.sampler_count;++i){const auto target=bloom_code_target(role,d.plan.enabled[eye],sh.codes[i]);n.samplers[i]={sh.codes[i],i,0x8000+target,ptr(&d.textures[target]),0,0,0};}
        d.vs=&d.programs[sh.vertex];d.ps=&d.programs[sh.pixel];
        d.q.uv_constants={};d.q.uv_versions={};d.q.uv_bind_cache={};
        if(role==5||role==6){d.q.uv_constants=d.uv;d.q.uv_versions={7,9};d.q.uv_bind_cache={{{7,0x01000079},{9,0x0100007a}}};}
        if(d.native_geometry){
            // Populate the copied geometry/matrix used by this active-class
            // fixture from the native recipe, not the older tag-word stand-in.
            // Actual protected native-reader validation remains its own target.
            const auto div=[](float a,float b){return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(a),_mm_set_ss(b)));};
            const auto mul=[](float a,float b){return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a),_mm_set_ss(b)));};
            const auto add=[](float a,float b){return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a),_mm_set_ss(b)));};
            const auto sub=[](float a,float b){return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a),_mm_set_ss(b)));};
            const auto bits=[](float f){std::uint32_t word{};std::memcpy(&word,&f,4);return word;};
            const float scene_w=640.f*d.resolution_scale,scene_h=720.f*d.resolution_scale;
            const float sx=div(1,float(n.width)),sy=div(1,float(n.height)),u0=mul(sx,float(eye)*scene_w),u1=add(mul(sx,scene_w),u0),v1=add(mul(sy,scene_h),0);
            const std::array<std::array<float,4>,4> corners{{{0,0,u0,0},{scene_w,0,u1,0},{scene_w,scene_h,u1,v1},{0,scene_h,u0,v1}}};
            for(unsigned i=0;i<4;++i){const auto& c=corners[i];const std::array<std::uint32_t,8> words{bits(c[0]),bits(c[1]),0,0x3f800000,0xffffffff,bits(c[2]),bits(c[3]),0x3ffe7f7f};std::memcpy(d.q.vertices.data()+i*8,words.data(),32);}
            d.q.indices={3,0,2,2,0,1};d.q.vertex_count=4;d.q.index_count=6;
            const bool final=!sh.affine;const float iw=div(1,final?scene_w:float(n.width)),ih=div(1,final?scene_h:float(n.height));
            d.q.matrix={};d.q.matrix[0]=bits(mul(iw,2));d.q.matrix[3]=bits(sub(-1,iw));d.q.matrix[5]=bits(mul(ih,-2));d.q.matrix[7]=bits(add(ih,1));d.q.matrix[11]=d.q.matrix[15]=0x3f800000;
            n.source_mode=2;n.selector=final?0:1;
        }}
    void use_native_geometry(){d.native_geometry=true;select(d.n.eye,d.n.role);}
    BloomShaderSource source(){BloomShaderSource s{};s.context=&d;s.current=[](void* p)noexcept{auto& d=*static_cast<Device*>(p);++d.current_calls;
        if(d.retire_on_current==d.current_calls)d.correction.retire();return d.current;};s.creation_serial=7;s.creation_thread=GetCurrentThreadId();s.creation_extended=true;s.native_draw_return=d.caller;
        s.read_episode=[](void* p,const BloomEpisodeCursor& cursor,BloomDrawSnapshot* n,BloomEpisodePlan* plan,BloomReadStatus* why)noexcept{auto& d=*static_cast<Device*>(p);++d.episode_reads;if(!d.readable){if(why)*why=BloomReadStatus::unrelated;return false;}*n=d.n;*plan=d.plan;
            if(d.episode_hook){auto hook=std::move(d.episode_hook);d.episode_hook={};hook(cursor,*n,*plan);}if(why)*why=BloomReadStatus::accepted;return true;};
        if(d.query_guard_enabled)s.query_guard=[](void* p,const BloomEpisodeCursor& cursor,const BloomDrawSnapshot& n,const BloomDrawCall& call) noexcept {
            auto& d=*static_cast<Device*>(p);++d.query_guard_calls;d.guard_episode_reads=d.episode_reads;
            if(d.guard_action&&d.guard_action_at==d.query_guard_calls){auto action=std::move(d.guard_action);d.guard_action={};action();}
            if(d.query_guard_hook){auto hook=std::move(d.query_guard_hook);d.query_guard_hook={};hook(cursor,n,call);}return d.query_guard_ok;};return s;}
    bool begin(bool enabled=true,std::uint64_t reset=1){d.correction.configure(enabled);return d.correction.begin({0,12,2,9,GetCurrentThreadId()},reset,d.api(),source(),{&d,[](void* p,const BloomDrawSnapshot&,const BloomDrawCall&,BloomQuadSnapshot* q)noexcept{auto& d=*static_cast<Device*>(p);++d.quad_copies;*q=d.q;return true;}});}
    BloomDrawCall draw(){return {d.caller,D3DPT_TRIANGLELIST,0,0,4,16,2};}
    std::uint64_t before(){SetLastError(0x1234abcd);const auto t=d.correction.before(d.api(),draw());check(GetLastError()==0x1234abcd,"before LastError preserved");return t;}
    void after(std::uint64_t t,HRESULT hr=D3D_OK){SetLastError(0xabcd1234);d.correction.after(t,hr);check(GetLastError()==0xabcd1234,"native result LastError preserved");}
    bool restored(){return d.vs==&d.programs[bloom_embedded::roles[d.n.role].vertex]&&d.ps==&d.programs[bloom_embedded::roles[d.n.role].pixel]&&d.stream==&d.vb&&d.offset==32&&d.stride==32&&d.indices==&d.ib&&d.vc==std::array<float,4>{7,8,9,10}&&d.pc==std::array<std::array<float,4>,2>{{{11,12,13,14},{15,16,17,18}}};}
    void pair(){for(unsigned eye:{1u,0u})for(auto role:bloom_role_order)if(bloom_role_enabled(role,d.plan.enabled[eye])){select(eye,role);auto t=before();check(t!=0,"complete chain before admitted");after(t);check(restored()&&!d.correction.pending(),"every role restored before ordinary native progression");}}
};
bool fresh_scope(Fixture& f,unsigned producer,std::uint64_t reset=1){
    ++f.d.plan.native_frame;f.select(1,0);
    return f.d.correction.begin({0,producer,2,producer+5,GetCurrentThreadId()},reset,f.d.api(),f.source(),
        {&f.d,[](void* p,const BloomDrawSnapshot&,const BloomDrawCall&,BloomQuadSnapshot* q)noexcept{auto& d=*static_cast<Device*>(p);++d.quad_copies;*q=d.q;return true;}});
}
void prefix_to_role(Fixture& f,unsigned selected){
    for(auto role:bloom_role_order)if(bloom_role_enabled(role,f.d.plan.enabled[1])){
        f.select(1,role);if(role==selected)return;const auto token=f.before();check(token!=0,"prefix current draw admitted");f.after(token);
        check(!f.d.correction.rejected()&&f.restored(),"prefix current draw restored");
    }check(false,"selected role exists in current plan");
}
void test_blur_current_rows(){
    // This is the actual typed class with a copied provider. The separate
    // protected-reader fixture proves native acquisition and argument coverage.
    for(unsigned role:{5u,6u})for(unsigned special:{0u,1u}){
        Fixture f;f.use_native_geometry();f.d.uv={{{0x3f000000,0x3e800000,0x3e000000,0x3f800000},{0x3d800000,0x3d000000,0x3c800000,0}}};
        if(special)f.d.uv={{{0x80000000,0,0x7fc01234,0xffc05678},{0x7f800000,0xff800000,1,0x807fffff}}};
        f.begin();prefix_to_role(f,role);const auto original=f.d.uv;const auto own=f.d.own_draws;auto token=f.before();check(token!=0,"matching exact native blur words including NaN payloads and signed zeros copy without numeric normalization");
        f.after(token);check(f.d.own_draws==own+1&&!f.d.correction.rejected()&&f.restored()&&f.d.uv==original,"current blur rows stay unchanged during owned replay and restoration");
    }
    for(unsigned role:{5u,6u})for(unsigned word=0;word<8;++word){
        Fixture f;f.begin();prefix_to_role(f,role);f.d.uv[word/4][word%4]^=1;
        const auto setters=f.d.calls["set_vc"],own=f.d.own_draws;
        check(!f.before()&&f.d.own_draws==own&&f.d.calls["set_vc"]==setters,"each actual VS blur word must equal protected current quad word before any setter");
    }
    for(unsigned role:{5u,6u})for(const char* op:{"get_uv23","get_uv36"})for(unsigned phase=1;phase<=3;++phase)for(unsigned mode=0;mode<3;++mode){
        Fixture f;f.begin();prefix_to_role(f,role);const auto own=f.d.own_draws,setters=f.d.calls["set_vc"];
        f.d.hook_name=op;f.d.hook_at=f.d.calls[op]+phase;
        f.d.hook=[&]{if(mode==0)f.d.failure=op;else if(mode==1)throw OwnCppException{&f.d.cpp_destroyed};else RaiseException(0xe012c012,0,0,nullptr);};
        auto token=f.before();if(token)f.after(token);
        check(f.d.correction.rejected()&&f.d.own_draws==own&&f.d.calls["set_vc"]==setters&&!f.d.correction.pending(),"either typed blur getter fails closed before, after or final acquisition for HRESULT/C++/SEH");
        if(mode==1)check(f.d.cpp_destroyed>0,"blur getter C++ exception follows normal contained cleanup");
    }
    for(unsigned role:{5u,6u})for(unsigned mode=0;mode<5;++mode){
        Fixture f;f.begin();prefix_to_role(f,role);const auto own=f.d.own_draws,setters=f.d.calls["set_vc"];f.d.hook_name="get_uv36";
        f.d.hook=[&]{if(mode==0)f.d.uv[0][0]^=0x80000000;else if(mode==1)f.d.q.uv_constants[1][3]^=1;else if(mode==2)f.d.current=false;else if(mode==3)f.d.correction.retire();else f.d.correction.before(f.d.api(),f.draw());};
        auto token=f.before();if(token)f.after(token);
        check(f.d.correction.rejected()&&f.d.own_draws==own&&f.d.calls["set_vc"]==setters,"blur query mutation/current/retirement/nested entry never authorizes a setter");
    }
    {Fixture f;f.begin();prefix_to_role(f,5);const auto calls=f.d.calls;f.d.correction.end_scope();
        check(f.d.correction.begin({0,700,2,705,GetCurrentThreadId()},1,f.d.api(),f.source(),{nullptr,[](void*,const BloomDrawSnapshot&,const BloomDrawCall&,BloomQuadSnapshot*)noexcept{return false;}}),"diagnostic label fixture begins fresh borrowed scope");
        f.select(1,0);check(!f.before()&&f.d.calls==calls,"initial quad refusal executes no device getter");
        check(!logs.empty()&&logs.back().find("stage=initial_quad_read state=0 value=0 expected=0")!=std::string::npos,"initial native quad failure cannot reuse prior sampler label");}
}
void test_query_storage_recovery(){
    for(unsigned mask=0;mask<4;++mask)for(unsigned mode=0;mode<4;++mode){
        Fixture f(mask);f.use_native_geometry();check(f.begin(),"storage reuse initial scope");unsigned first_two=99;
        for(auto role:bloom_role_order)if(bloom_role_enabled(role,mask)&&bloom_embedded::roles[role].sampler_count==2){first_two=role;break;}
        prefix_to_role(f,first_two);auto token=f.before();check(token!=0&&f.d.n.sampler_count==2,"current two-sampler before filled all saved metadata and constants");
        if(mode==0)f.after(token,E_FAIL);
        else if(mode==1){f.d.failure="get_vs";f.after(token);f.d.failure.clear();}
        else if(mode==2){f.d.failure="get_texture";f.d.fail_at=f.d.calls["get_texture"]+2;f.after(token);f.d.failure.clear();f.d.fail_at=0;}
        else f.after(token);
        check((mode==3?!f.d.correction.rejected():f.d.correction.rejected())&&!f.d.correction.pending()&&f.restored(),"interrupted/control two-sampler state owns no pending restoration");
        check(!f.d.correction.finish(true),"partial current episode remains incomplete");f.d.correction.end_scope();
        for(unsigned cycle=0;cycle<3;++cycle){
            if(cycle==2)check(f.d.correction.retire(),"normal retirement before storage reuse");
            check(fresh_scope(f,800+cycle,cycle?2:1),"same object new producer/reset/retirement scope begins");const auto own=f.d.own_draws;
            f.pair();check(f.d.correction.finish(true)&&f.d.own_draws==own+f.d.plan.expected_draws,"fresh entire plan recovers at each same-reset/changed-reset/retirement boundary after interrupted two-sampler storage");
            f.d.correction.end_scope();
        }
    }
    {Fixture f;f.begin();prefix_to_role(f,10);auto token=f.before();check(token!=0,"five-element declaration saved before interrupted two-sampler query");f.after(token,E_FAIL);f.d.correction.finish(false);f.d.correction.end_scope();
        f.d.elements[1]=f.d.elements[2];f.d.elements[2]=D3DDECL_END();f.d.declaration_count=3;
        check(fresh_scope(f,890),"same object shorter declaration begins");const auto own=f.d.own_draws;f.pair();
        check(f.d.correction.finish(true)&&f.d.own_draws==own+f.d.plan.expected_draws&&f.restored(),"actual GetDeclaration three-element copies clear prior five-element unused tail and recover entire plan");}
    {Fixture f;f.begin();prefix_to_role(f,10);auto token=f.before();f.d.hook_name="up";f.d.hook=[&]{f.d.failure="get_pc1";};f.after(token);
        check(f.d.correction.pending(),"two-sampler original constant retained on failed restoration");const auto calls=f.d.calls;const auto pc=f.d.pc;
        check(!f.before()&&!f.d.correction.begin({0,900,2,905,GetCurrentThreadId()},1,f.d.api(),f.source(),{}),"pending journal prevents both query and fresh scope admission");
        check(f.d.calls==calls&&f.d.pc==pc,"query storage reset cannot erase or touch pending original constant ownership");f.d.failure.clear();
        check(f.d.correction.reconcile_normal(f.d.api(),true)&&f.restored(),"normal restoration uses retained original constants after refused begin");
        check(fresh_scope(f,901),"fresh scope after pending journal resolved");f.pair();check(f.d.correction.finish(true),"complete plan recovers only after pending originals restored");}
    {Fixture f;f.begin();const auto token=f.before();f.d.correction.abandon(token);check(fresh_scope(f,950),"abandoned references retained for next normal query");
        const auto creates=f.d.calls["creation"];f.d.hook_name="object_release";f.d.hook=[&]{f.d.current=false;};
        check(!f.before()&&f.d.calls["creation"]==creates&&!f.d.own_draws,"Release reentry during unowned reset still fails current guard before acquisition");}
}
void test_native_pixel_bank(){
    static_assert(sizeof(BloomDrawSnapshot)==1696&&sizeof(BloomEpisodePlan)==3528);
    for(unsigned mask=0;mask<4;++mask){Fixture f(mask);
        // Distinct raw values across every row include signed zero, denormals,
        // infinities and NaN payloads. Identity does not assert useful pixels.
        for(unsigned i=0;i<f.d.native_pc.size();++i)f.d.native_pc[i]=i%4==0?0x7fc00000+i:i%4==1?0x80000000+i:i%4==2?0x3f000000+i:i;
        const auto bank=f.d.native_pc;f.begin();f.pair();
        check(f.d.correction.finish(true)&&f.d.native_pc==bank&&f.d.calls["get_native_pc"]==f.d.plan.expected_draws*3,"every role snapshots all200 native PS rows three times and never sets them");}
    for(unsigned word=0;word<800;++word){Fixture f;f.d.native_pc[word]=word%2?0x80000000:0x7fc01234;f.begin();auto token=f.before();check(token!=0,"complete native PS raw bank before admitted");
        f.d.native_pc[word]^=1;f.after(token);check(!f.d.own_draws&&!f.d.calls["set_vc"]&&!f.d.calls["set_pc0"]&&f.d.correction.rejected(),"each raw PS bank word including unused/W and NaN payload rejects a before/after change");}
    for(unsigned phase=1;phase<=3;++phase)for(unsigned mode=0;mode<6;++mode){Fixture f;f.begin();f.d.hook_name="get_native_pc";f.d.hook_at=phase;
        f.d.hook=[&]{if(mode==0)f.d.failure="get_native_pc";else if(mode==1)throw OwnCppException{&f.d.cpp_destroyed};else if(mode==2)RaiseException(0xe012c012,0,0,nullptr);else if(mode==3)f.d.current=false;else if(mode==4)f.d.correction.retire();else f.d.correction.before(f.d.api(),f.draw());};
        auto token=f.before();if(token)f.after(token);
        check(f.d.correction.rejected()&&!f.d.own_draws&&!f.d.calls["set_vc"]&&!f.d.calls["set_pc0"]&&!f.d.correction.pending(),"native PS bank getter failure/exception/current/retire/reentry refuses every before/after/final-query setter");
        if(mode==1)check(f.d.cpp_destroyed>0,"native PS bank C++ throw reaches normal contained cleanup");}
}
void test_exact_plan_predicate_reuse(){
    // These are the real replay class and typed CPU interfaces with copied
    // provider output. Native recipe acquisition has its own protected fixture.
    static_assert(sizeof(BloomEpisodePlan)==882*sizeof(std::uint32_t));
    for(unsigned word=0;word<882;++word){Fixture f;check(f.begin(),"plan word fixture begins");
        const auto token=f.before();check(token!=0,"first draw fully validates plan before reuse");f.after(token);f.select(1,1);
        const auto calls=f.d.calls;const auto reads=f.d.episode_reads;const auto own=f.d.own_draws;
        f.d.episode_hook=[word](const BloomEpisodeCursor& cursor,BloomDrawSnapshot&,BloomEpisodePlan& p){
            check(cursor.eye==1&&cursor.role==1&&!cursor.pass&&!cursor.finished,"cached read keeps current reducer cursor");
            auto* bytes=reinterpret_cast<unsigned char*>(&p);std::uint32_t value{};std::memcpy(&value,bytes+word*4,4);value^=1;std::memcpy(bytes+word*4,&value,4);};
        check(!f.before()&&f.d.episode_reads==reads+1&&f.d.calls==calls&&f.d.own_draws==own,"every full-plan word transient mutation rejects at existing next provider boundary before COM");
        const auto after_reads=f.d.episode_reads;check(!f.before()&&f.d.episode_reads==after_reads&&!f.d.correction.finish(true),"restored provider output cannot resurrect refused cached scope");
    }
    for(unsigned change=0;change<3;++change){Fixture f;f.begin();f.after(f.before());f.select(1,1);
        f.d.episode_hook=[change](const BloomEpisodeCursor&,BloomDrawSnapshot&,BloomEpisodePlan& p){
            if(change==0){++p.roles[0][8].technique_flags;++p.roles[1][8].technique_flags;}
            if(change==1)p.fp_control=0x3f80;
            if(change==2){p.tint_bits[0][0]=p.tint_bits[1][0]=0x40000000;}
            check(bloom_plan_valid(p),"changed future role/tint/FP plan is independently valid, so exact equality is necessary");};
        const auto own=f.d.own_draws;check(!f.before()&&f.d.own_draws==own&&!f.d.correction.finish(true),"valid replacement plan never replaces current accepted plan");
        check(fresh_scope(f,1200+change,change+1),"new backend scope may establish its own plan after refusal");f.pair();
        check(f.d.correction.finish(true)&&f.d.own_draws==own+f.d.plan.expected_draws&&f.restored(),"same State recovers full plan after refused cache across reset serials");
    }
    for(unsigned change=0;change<5;++change){Fixture f;f.begin();f.after(f.before());f.select(1,1);
        f.d.episode_hook=[change](const BloomEpisodeCursor&,BloomDrawSnapshot& n,BloomEpisodePlan&){
            if(change==0)n.eye=0;if(change==1)n.role=0;if(change==2)n.pass_index=1;if(change==3)n.pass_count=2;if(change==4)n.custom_sampler_flags=1;};
        const auto calls=f.d.calls;check(!f.before()&&f.d.calls==calls&&!f.d.correction.finish(true),"unchanged plan never bypasses current eye/role/pass/custom draw predicates");
    }
    {Fixture f;const auto original=f.d.plan;
        for(unsigned eye=0;eye<2;++eye){auto& role=f.d.plan.roles[eye][8];role.pass_count=2;role.passes[1]=role.passes[0];role.passes[1].pass+=20;}
        f.d.plan.expected_draws+=2;check(bloom_plan_valid(f.d.plan),"valid future multi-pass plan exercises active single-pass restriction separately");f.begin();
        check(!f.before()&&!f.d.calls["creation"]&&!f.d.correction.finish(true),"first plan checks every future role for active single-pass admission");
        f.d.plan=original;check(fresh_scope(f,1300),"new scope after initial validation refusal");f.pair();check(f.d.correction.finish(true),"initial refusal never installs a reusable plan verdict");}
    for(unsigned control=0;control<16;++control){Fixture f(0);f.begin();
        const unsigned mxcsr=0x1fa1|((control&3)<<13)|((control&4)?0x40:0)|((control&8)?0x8000:0);
        _mm_setcsr(mxcsr);const auto token=f.before();check(token!=0&&_mm_getcsr()==mxcsr,"first full validation preserves complete entry MXCSR in copied-provider fixture");f.after(token);
        check(_mm_getcsr()==mxcsr&&f.restored(),"first own replay restores entry MXCSR");f.select(1,1);const auto cached=f.before();
        check(cached!=0&&_mm_getcsr()==mxcsr,"reused pure verdict preserves all rounding DAZ FTZ and status bits");f.after(cached);
        check(_mm_getcsr()==mxcsr&&f.restored(),"cached draw restoration preserves entry MXCSR");_mm_setcsr(0x1f80);f.d.correction.finish(false);}
    for(unsigned mask=0;mask<4;++mask){Fixture f(mask);f.begin();f.pair();check(f.d.correction.finish(true),"complete plan succeeds before cache retirement");
        f.d.correction.retire();check(fresh_scope(f,1400+mask,2),"retired State can establish new full plan");f.pair();check(f.d.correction.finish(true),"all four masks fully recover after cache retirement");}
}
unsigned owner_hits(const BloomEpisodePlan& plan){unsigned samplers{};for(unsigned eye=0;eye<2;++eye)for(auto role:bloom_role_order)
    if(bloom_role_enabled(role,plan.enabled[eye]))samplers+=bloom_embedded::roles[role].sampler_count;return 2*(6*plan.expected_draws+2*samplers);}
void owner_method_delta(const std::map<std::string,unsigned>& legacy,const std::map<std::string,unsigned>& reused,unsigned hits){
    auto expected=legacy;for(const auto& [name,multiple]:std::array<std::pair<const char*,unsigned>,4>{{{"object_device",1},{"device_query",2},{"device_add",3},{"device_release",3}}}){
        check(expected[name]>=multiple*hits,"owner proof delta has enough actual calls");expected[name]-=multiple*hits;}
    check(expected==reused,"only GetDevice, device canonical QI and their acquired temporary references are removed");}
void test_query_group_class_contract(){
    // This callback is an explicit copied-provider control seam. The actual
    // protected factory's owned-key guard has independent native/integration tests.
    for(unsigned mask=0;mask<4;++mask){Fixture full(mask),grouped(mask);grouped.d.query_guard_enabled=true;
        check(full.begin()&&grouped.begin(),"null and supplied query guard begin identical copied plans");
        full.pair();grouped.pair();check(full.d.correction.finish(true)&&grouped.d.correction.finish(true),"both query contracts complete every optional two-eye plan");
        const auto hits=owner_hits(full.d.plan);owner_method_delta(full.d.calls,grouped.d.calls,hits);
        check(full.d.query_guard_calls==0&&grouped.d.query_guard_calls>0&&full.d.episode_reads==grouped.d.episode_reads+grouped.d.query_guard_calls+6*hits,"null guard retains old full reads; grouped reads and omitted owner guards are counted separately");
        check(full.d.current_calls==grouped.d.current_calls+12*hits&&full.d.quad_copies==grouped.d.quad_copies,"copied provider current delta is measured while all full quad barriers remain");
        check(full.d.own_draws==grouped.d.own_draws&&full.restored()&&grouped.restored(),"owner proof reuse retains every owned draw and final graphics state");}
    {Fixture f;f.d.query_guard_enabled=true;f.d.query_guard_ok=false;f.begin();
        check(!f.before()&&f.d.query_guard_calls==1&&f.d.episode_reads==f.d.guard_episode_reads&&!f.d.calls["creation"]&&!f.d.own_draws,"false supplied guard refuses once without native-read fallback or first typed getter");}
    {Fixture f;f.d.query_guard_enabled=true;f.begin();f.d.query_guard_hook=[&](const auto& cursor,const auto& n,const auto& call){
        check(cursor.eye==1&&cursor.role==0&&n==f.d.n&&call.caller==f.d.caller&&call.type==4&&call.base_vertex==0&&call.min_vertex==0&&call.num_vertices==4&&call.start_index==16&&call.primitive_count==2,"class forwards exact cursor accepted draw and seven-field original call");};
        const auto token=f.before();check(token!=0,"valid explicit query guard grants current query only");f.after(token);check(f.restored(),"normal query guard path restores current state");}
    for(unsigned mutation=0;mutation<3;++mutation){Fixture f;f.d.query_guard_enabled=true;f.begin();f.d.query_guard_hook=[&](const auto&,const auto&,const auto&){
        if(mutation==0)f.d.current=false;if(mutation==1)f.d.correction.retire();if(mutation==2)indexed_reentries=indexed_reentries+1;};
        check(!f.before()&&!f.d.calls["creation"]&&!f.d.own_draws,"current retirement or witness revocation inside supplied guard cannot authorize next getter");}
    for(unsigned phase:{1u,2u,3u}){Fixture f;f.d.query_guard_enabled=true;f.begin();f.d.hook_name="get_vs";f.d.hook_at=phase;f.d.hook=[&]{f.d.n.scene[0]^=1;};
        const auto token=f.before();if(token)f.after(token);
        check(f.d.correction.rejected()&&!f.d.own_draws&&!f.d.calls["set_vc"]&&!f.d.calls["set_ps"],"persistent copied native mutation at each query rejects at full endpoint before any setter or UP");}
    for(bool grouped:{false,true}){Fixture f;f.d.query_guard_enabled=grouped;f.begin();const auto scene=f.d.n.scene[0];
        f.d.hook_name="get_vs";f.d.hook=[&]{f.d.n.scene[0]^=1;f.d.hook_name="get_ps";f.d.hook=[&]{f.d.n.scene[0]=scene;};};
        const auto token=f.before();check((token!=0)==grouped,"mutation restored before next full barrier is accepted only by explicitly grouped contract");
        if(token){f.after(token);check(f.d.own_draws==1&&f.restored(),"documented transient acceptance still performs normal restored own draw");}}
    for(const char* name:{"get_vs","get_target","render_state","get_native_pc","sampler_state"}){Fixture f;f.d.query_guard_enabled=true;f.d.failure=name;f.begin();
        check(!f.before()&&!f.d.own_draws&&!f.d.calls["set_ps"],"grouped typed getter HRESULT remains a hard refusal");}
    {Fixture f;f.d.query_guard_enabled=true;f.begin();auto token=f.before();f.d.hook_name="up";f.d.hook=[&]{f.d.failure="get_pc0";};f.after(token);
        check(f.d.correction.pending()&&!f.d.correction.finish(true),"grouped success never erases a subsequent pending restore journal");
        f.d.failure.clear();check(f.d.correction.reconcile_normal(f.d.api(),true)&&f.restored(),"pending grouped draw reconciles at existing normal boundary");
        check(fresh_scope(f,1901,2),"same grouped State starts new scope after restored pending journal");f.pair();check(f.d.correction.finish(true),"same State fully recovers grouped plan after normal reconciliation");}
}
// C12j tests the actual public class. No State-layout access, owner-proof setter,
// callback-pointer patch or synthetic claim of protected native geometry.
void test_owner_proof_class_contract(){const auto initial=checks;
    for(unsigned mask=0;mask<4;++mask){Fixture f(mask);f.d.query_guard_enabled=true;f.d.track_owners=true;f.begin();
        for(unsigned eye:{1u,0u})for(auto role:bloom_role_order)if(bloom_role_enabled(role,mask)){f.select(eye,role);const auto first=f.d.owner_values.size();
            const auto token=f.before();check(token!=0,"fresh draw publishes only after its complete before query");
            std::vector<Object*> expected{f.d.vs,f.d.ps,&f.d.surfaces[f.d.n.target],&f.d.vb,&f.d.ib,&f.d.declaration};
            for(unsigned slot=0;slot<f.d.n.sampler_count;++slot){auto* t=reinterpret_cast<Object*>(static_cast<std::uintptr_t>(f.d.n.samplers[slot].texture));expected.push_back(t);expected.push_back(t->level);}
            check(std::vector<Object*>(f.d.owner_values.begin()+first,f.d.owner_values.end())==expected,"every before proves all corresponding slots in exact order, including each sampler");
            const auto proved=f.d.owner_values.size();f.after(token);check(f.d.owner_values.size()==proved&&f.restored(),"both after queries reuse corresponding living owner facts without any new GetDevice");
            check(f.d.vb.refs==1&&f.d.ib.refs==1&&f.d.declaration.refs==1,"normal draw releases all binding anchors before the next draw");}
        check(f.d.correction.finish(true),"all four plans and both eyes complete with fresh per-draw proofs");
        // End a real fake-COM object lifetime and reconstruct at the same address.
        // With no outstanding anchor this is permitted, but cannot inherit proof.
        check(f.d.vb.refs==1,"object lifetime ends only after all class references return");const auto saved=f.d.vb;
        --f.d.vb.refs;check(f.d.vb.refs==0,"fixture owner drops its sole baseline buffer reference before destruction");
        std::destroy_at(&f.d.vb);std::construct_at(&f.d.vb,saved);f.d.owner_values.clear();check(fresh_scope(f,2200+mask),"new scope after actual same-address object lifetime replacement");
        f.d.failure="object_device";f.d.fail_at=f.d.calls["object_device"]+4;
        check(!f.before()&&f.d.owner_values.size()==4&&f.d.owner_values.back()==&f.d.vb,"reconstructed same-address buffer must execute its own fresh GetDevice and observe failure");}
    // Directly native-bound addresses cannot manufacture an owner miss: the
    // existing copied native/quad identity rejects before the owner lookup.
    for(unsigned slot=0;slot<8;++slot){Fixture f;f.d.query_guard_enabled=true;f.begin();prefix_to_role(f,3);const auto token=f.before();const auto draws=f.d.own_draws;
        auto* source=slot==0?f.d.vs:slot==1?f.d.ps:slot==2?&f.d.surfaces[f.d.n.target]:slot==3?&f.d.vb:slot==4?&f.d.ib:slot==5?&f.d.declaration:
            reinterpret_cast<Object*>(static_cast<std::uintptr_t>(f.d.n.samplers[slot-6].texture));
        Object different=*source;different.refs=1;different.canonical=source;f.d.returned[slot]=&different;const auto owners=f.d.calls["object_device"];
        f.after(token);check(f.d.own_draws==draws&&f.d.calls["object_device"]==owners&&!f.d.correction.finish(true),"different typed pointer cannot borrow a proof by canonical identity or matching descriptor");
        f.d.returned={};check(different.refs==1&&f.restored(),"newly returned nonmatching typed interface is normally released");}
    // GetSurfaceLevel is genuinely free to return a different typed interface;
    // it takes the original full owner path, even with the same canonical ID.
    for(unsigned slot=0;slot<2;++slot)for(unsigned mode=0;mode<8;++mode){Fixture f;f.d.query_guard_enabled=true;f.begin();prefix_to_role(f,3);const auto token=f.before();const auto draws=f.d.own_draws;
        auto* texture=reinterpret_cast<Object*>(static_cast<std::uintptr_t>(f.d.n.samplers[slot].texture));auto* old=texture->level;
        Object alternate=*old;alternate.refs=1;alternate.canonical=old;f.d.preserve_level_canonical=true;
        auto* other=reinterpret_cast<Object*>(static_cast<std::uintptr_t>(f.d.n.samplers[1-slot].texture))->level;
        texture->level=mode==7?other:&alternate;f.d.track_owners=true;f.d.owner_values.clear();
        if(mode>=1&&mode<=3){if(mode==1)f.d.failure="object_device";if(mode==2)f.d.throwing="object_device";if(mode==3)f.d.seh_failure="object_device";}
        if(mode>=4&&mode<=6){if(mode==4)f.d.failure="device_query";if(mode==5)f.d.throwing="device_query";if(mode==6)f.d.seh_failure="device_query";}
        f.after(token);check(f.d.owner_values==std::vector<Object*>{texture->level},"level miss performs its own GetDevice; another sampler anchor never lends a proof");
        check(f.d.own_draws==draws&&!f.d.correction.finish(true),"level mismatch or full owner-path fault refuses before any new UP");
        texture->level=old;f.d.failure.clear();f.d.throwing.clear();f.d.seh_failure.clear();check(alternate.refs==1&&f.restored(),"partial owner outputs and alternate canonical references return normally");
        if(mode==2||mode==5)check(f.d.cpp_destroyed>0,"miss-side C++ exception reaches the normal own catcher");}
    for(const char* method:{"object_device","device_query"})for(unsigned mode=0;mode<3;++mode)for(bool grouped:{false,true}){Fixture f;f.d.query_guard_enabled=grouped;f.begin();const auto token=f.before();
        if(mode==0)f.d.failure=method;if(mode==1)f.d.throwing=method;if(mode==2)f.d.seh_failure=method;f.after(token);
        check(f.d.own_draws==(grouped?1u:0u)&&f.restored(),"documented difference: fault only on a removed owner call is no longer sampled on a hit");
        check((f.d.cpp_destroyed!=0)==(!grouped&&mode==1),"removed-call exception is not claimed to have run or been caught");f.d.failure.clear();f.d.throwing.clear();f.d.seh_failure.clear();}
    for(unsigned nth=1;nth<=10;++nth){Fixture f;f.d.query_guard_enabled=true;f.begin();prefix_to_role(f,3);f.d.failure="object_device";f.d.fail_at=f.d.calls["object_device"]+nth;const auto draws=f.d.own_draws;
        check(!f.before(),"every partial before owner acquisition refuses publication");const auto methods=f.d.calls;f.after(1234);check(methods==f.d.calls&&f.d.own_draws==draws,"failed before cannot be completed by a fabricated token");
        f.d.failure.clear();check(fresh_scope(f,2300+nth),"failed partial publication releases into a fresh scope");f.pair();check(f.d.correction.finish(true),"same State fully recovers after partial owner proof failure");}
    for(unsigned bad=0;bad<3;++bad){Fixture f;f.d.query_guard_enabled=true;f.begin();const auto token=f.before();
        if(bad<2)f.after(bad?token+1:0);else f.d.correction.abandon(token+1);
        auto expected=f.d.calls;const auto reads=f.d.episode_reads,guards=f.d.query_guard_calls,current=f.d.current_calls,quad=f.d.quad_copies;
        // The outer matched after still owns all eleven before references and
        // releases exactly those anchors after rejecting the invalid proof.
        expected["object_release"]+=11;f.after(token);
        check(!f.d.own_draws&&expected==f.d.calls&&reads==f.d.episode_reads&&guards==f.d.query_guard_calls&&current==f.d.current_calls&&quad==f.d.quad_copies,
            "restored token permits only exact normal anchor cleanup, with zero owner/getter/current/query/guard/setter/UP fallback");
        for(const auto& o:f.d.programs)check(o.refs==1,"revoked token returns each original shader anchor");
        for(const auto& o:f.d.surfaces)check(o.refs==1,"revoked token returns each target/level/canonical anchor");
        for(const auto& o:f.d.textures)check(o.refs==1,"revoked token returns each typed/base texture anchor");
        check(f.d.vb.refs==1&&f.d.ib.refs==1&&f.d.declaration.refs==1,"revoked token returns all saved binding anchors");}
    for(unsigned boundary:{1u,2u})for(unsigned action=0;action<5;++action){Fixture f;f.d.query_guard_enabled=true;f.begin();const auto token=f.before();
        f.d.hook_name="get_vs";f.d.hook_at=f.d.calls["get_vs"]+1;f.d.hook=[&]{f.d.guard_action_at=f.d.query_guard_calls+1+boundary;f.d.guard_action=[&]{
            if(action==0)f.d.query_guard_ok=false;else if(action==1)f.d.correction.retire();else if(action==2)f.d.correction.after(0,S_OK);
            else if(action==3)f.d.current=false;else f.d.correction.before(f.d.api(),f.draw());};};
        f.after(token);check(!f.d.own_draws&&!f.d.first_set_current,"either hit guard can revoke before any replay mutation");f.d.query_guard_ok=true;f.d.current=true;
        const auto calls=f.d.calls;f.after(token);check(calls==f.d.calls&&!f.d.own_draws,"restored guard/current values cannot revive the failed proof");}
    for(const char* method:{"creation","caps","get_vs","get_ps","get_target","viewport","render_state","get_stream","frequency","get_indices","get_declaration","declaration_elements","get_matrix","get_vc","get_native_pc","get_texture","texture_level","desc","sampler_state","get_pc0"})
        for(unsigned phase:{1u,2u}){Fixture f;f.d.query_guard_enabled=true;f.begin();const auto token=f.before();const unsigned per=std::strcmp(method,"render_state")==0?24:std::strcmp(method,"sampler_state")==0?static_cast<unsigned>(std::size(bloom_replay_sampler_states)):std::strcmp(method,"desc")==0?3:1;
            f.d.failure=method;f.d.fail_at=f.d.calls[method]+(phase-1)*per+1;f.after(token);check(!f.d.own_draws&&!f.d.first_set_current,"every retained getter still fails in either after query before any setter");}
    for(unsigned mode=0;mode<4;++mode){Fixture f;f.d.query_guard_enabled=true;f.begin();const auto token=f.before();bool entered=false;
        f.d.hook_name="object_release";f.d.hook=[&]{entered=true;const auto calls=f.d.calls;f.d.correction.after(token,S_OK);check(calls==f.d.calls,"anchor Release reentry cannot execute an after query under the active cleanup lease");};
        if(mode==0)f.d.correction.end_scope();if(mode==1)f.d.correction.retire();if(mode==2)f.after(token,E_FAIL);if(mode==3)f.d.correction.finish(false);
        check(entered&&!f.d.own_draws,"all normal anchor cleanup paths revoke before an outbound Release");}
    for(bool enabled:{false,true})for(bool staged:{false,true}){Fixture f;f.begin();f.d.correction.retire();f.d.correction.configure(enabled);const auto calls=f.d.calls;
        bool foreign=true;std::thread thread([&]{foreign=f.d.correction.finish(staged);});thread.join();check(foreign==(!enabled&&staged)&&calls==f.d.calls,"disabled foreign finish retains original staged pass-through without COM or owner metadata writes");
        check(f.d.correction.finish(staged)==(!enabled&&staged),"disabled owner finish retains exact staged result; enabled retired scope refuses");}
    std::printf("owner_proof_class_checks=%u failures=%u copied_quad_not_protected=1\n",checks-initial,failures);
}
// C12i observes only the new diagnostic. These helpers do not replace the
// actual State clock and do not add a production injection seam.
struct StageLog {
    std::map<std::string,std::string> fields;
    std::array<std::uint64_t,4> vector(const char* name) const {
        std::array<std::uint64_t,4> out{};const auto found=fields.find(name);
        check(found!=fields.end(),"stage diagnostic has required vector");if(found==fields.end())return out;
        std::size_t pos{};for(unsigned i=0;i<4;++i){const auto comma=found->second.find(',',pos);
            check((comma==std::string::npos)==(i==3),"stage vector has exactly four decimal components");
            const auto word=found->second.substr(pos,comma==std::string::npos?comma:comma-pos);
            check(!word.empty()&&word.find_first_not_of("0123456789")==std::string::npos,"stage words are unsigned decimal");
            if(!word.empty())out[i]=std::stoull(word);pos=comma==std::string::npos?found->second.size():comma+1;}return out;
    }
    std::uint64_t scalar(const char* name) const {const auto i=fields.find(name);check(i!=fields.end(),"stage diagnostic has required scalar");return i==fields.end()?0:std::stoull(i->second);}
};
std::vector<StageLog> stage_logs(std::size_t from=0){std::vector<StageLog> result;
    for(std::size_t i=from;i<logs.size();++i){const auto& line=logs[i];constexpr const char* prefix="t5_bloom_stage_timing ";if(!line.starts_with(prefix))continue;
        StageLog row;std::size_t pos=std::strlen(prefix);while(pos<line.size()){const auto end=line.find(' ',pos);const auto word=line.substr(pos,end==std::string::npos?end:end-pos);const auto equal=word.find('=');
            check(equal!=std::string::npos&&row.fields.emplace(word.substr(0,equal),word.substr(equal+1)).second,"stage schema has unique key/value fields");if(end==std::string::npos)break;pos=end+1;}
        check(row.fields.size()==19&&row.fields["coverage"]=="after_claim"&&row.fields["first_preclaim_query_excluded"]=="1"&&row.fields["stage_order"]=="query,programs,replay,restore","exact stage schema states after-claim scope");
        check(row.scalar("attempt")>=1&&row.scalar("attempt")<=2&&row.scalar("producer")&&row.scalar("generation")&&row.scalar("xr_frame")&&row.scalar("qpc_hz"),"stage record uses claimed exact key and clock frequency");
        result.push_back(std::move(row));}return result;
}
void expect_stage(const StageLog& row,std::array<std::uint64_t,4> entered,std::array<std::uint64_t,4> yes,
    std::array<std::uint64_t,4> no,std::array<std::uint64_t,4> returned_void,bool complete){
    check(row.vector("entered")==entered&&row.vector("normal_true")==yes&&row.vector("normal_false")==no&&row.vector("normal_void")==returned_void,"actual stage entry and normal-return vectors match executed path");
    check(row.vector("unreturned")==std::array<std::uint64_t,4>{}&&row.vector("valid_pairs")==entered&&row.vector("invalid_clock")==std::array<std::uint64_t,4>{},"normal actual QPC pairs contain no inferred fault or missing return");
    const auto ticks=row.vector("ticks");for(unsigned i=0;i<4;++i)if(!entered[i])check(ticks[i]==0,"unentered stage has no accumulated ticks");
    check(row.scalar("complete")==static_cast<unsigned>(complete)&&!row.scalar("overlap")&&!row.scalar("overflow_mask"),"timing validity is distinct from correction completion");
}
std::map<std::string,unsigned> call_difference(const std::map<std::string,unsigned>& after,const std::map<std::string,unsigned>& before){
    std::map<std::string,unsigned> result;for(const auto& [name,amount]:after){const auto i=before.find(name);const auto old=i==before.end()?0:i->second;check(amount>=old,"typed method counts remain monotonic");if(amount!=old)result[name]=amount-old;}return result;
}
struct StageTestClock {
    inline static std::array<unsigned,2> modes{};inline static std::array<LONGLONG,2> values{100,140};
    inline static unsigned calls{},destroyed{};
    static bool read(LARGE_INTEGER* out){const unsigned index=calls++;const auto slot=index<2?index:1;out->QuadPart=values[slot];
        SetLastError(0xf123abcd);_mm_setcsr(0x7f80);
        __asm {fninit}
        __asm {pxor xmm0,xmm0}
        if(modes[slot]==1)return false;if(modes[slot]==2)RaiseException(0xe0121c10,0,0,nullptr);
        if(modes[slot]==3)throw OwnCppException{&destroyed};if(modes[slot]==4)out->QuadPart=0;if(modes[slot]==5)out->QuadPart=-1;return true;
    }
    static void reset(unsigned first=0,unsigned last=0){modes={first,last};values={100,140};calls=destroyed=0;}
};
void stage_fp_equal(const unsigned char* wanted,const unsigned char* actual){
    for(unsigned i=0;i<5;++i)check(wanted[i]==actual[i],"clock helper preserves entry x87 environment");
    for(unsigned i=24;i<28;++i)check(wanted[i]==actual[i],"clock helper preserves entry MXCSR");
    for(unsigned r=0;r<8;++r)for(unsigned i=0;i<10;++i)check(wanted[32+16*r+i]==actual[32+16*r+i],"clock helper preserves entry x87 values");
    for(unsigned i=160;i<288;++i)check(wanted[i]==actual[i],"clock helper preserves entry XMM values");
}
void test_stage_clock_contract(){const auto initial=checks;
    alignas(16) unsigned char saved[512],entry[512],operation[512],observed[512];_fxsave(saved);
    __asm {fninit}
    __asm {fld1}
    __asm {fldpi}
    _fxsave(entry);std::memcpy(operation,entry,512);
    for(unsigned i=160;i<288;++i){entry[i]=static_cast<unsigned char>(i*7+3);operation[i]=static_cast<unsigned char>(i*11+1);}
    const DWORD first_mx=0x1f80,last_mx=0x5f80;std::memcpy(entry+24,&first_mx,4);std::memcpy(operation+24,&last_mx,4);_fxrstor(saved);
    for(unsigned kind=0;kind<4;++kind)for(unsigned returned=0;returned<3;++returned)for(unsigned fault=0;fault<12;++fault){
        const unsigned start=fault>=1&&fault<=5?fault:0,end=fault>=6&&fault<=10?fault-5:0;StageTestClock::reset(start,end);if(fault==11)StageTestClock::values[1]=99;
        BloomStageTiming record{};BloomStageToken token;SetLastError(0x1288ab01);_fxrstor(entry);
        bloom_stage_begin<StageTestClock>(record,static_cast<BloomStageKind>(kind),token);_fxsave(observed);const auto before_error=GetLastError();_fxrstor(saved);
        check(before_error==0x1288ab01,"begin clock cannot leak LastError even on fault");stage_fp_equal(entry,observed);
        // An operation is allowed to change its own environment. End must
        // preserve this new image, not restore the earlier begin image.
        SetLastError(0x1288ab02);_fxrstor(operation);
        bloom_stage_end<StageTestClock>(record,token,static_cast<BloomStageReturn>(returned));_fxsave(observed);const auto after_error=GetLastError();_fxrstor(saved);
        check(after_error==0x1288ab02,"end clock preserves measured operation LastError");stage_fp_equal(operation,observed);
        const auto& counter=record.stages[kind];check(StageTestClock::calls==2&&counter.entered==1&&counter.normal_true==(returned==0)&&counter.normal_false==(returned==1)&&counter.normal_void==(returned==2),"each direct stage clocks twice and records its actual normal return kind");
        check(counter.valid_pairs==(fault==0)&&counter.invalid_clock==(fault!=0)&&counter.ticks==(fault==0?40u:0u)&&!record.depth&&!record.overlap&&!record.overflow_mask,"false/fault/zero/negative/backward clocks affect telemetry only");
        check(StageTestClock::destroyed==static_cast<unsigned>((start==3)+(end==3)),"clock C++ object unwinds into the outer own-operation catcher");
        for(unsigned i=0;i<4;++i)if(i!=kind)check(record.stages[i].entered==0,"direct helper writes only selected stage");
    }
    for(unsigned kind=0;kind<4;++kind)for(unsigned field=0;field<7;++field){BloomStageTiming record{};auto& c=record.stages[kind];
        std::uint64_t* fields[]{&c.entered,&c.normal_true,&c.normal_false,&c.normal_void,&c.valid_pairs,&c.invalid_clock,&c.ticks};*fields[field]=UINT64_MAX;
        StageTestClock::reset(0,field==5?1:0);BloomStageToken token;bloom_stage_begin<StageTestClock>(record,static_cast<BloomStageKind>(kind),token);
        bloom_stage_end<StageTestClock>(record,token,field==2?BloomStageReturn::normal_false:field==3?BloomStageReturn::normal_void:BloomStageReturn::normal_true);
        check(*fields[field]==UINT64_MAX&&record.overflow_mask==(1u<<kind)&&!record.depth,"each counter and tick sum saturates with a stage-local invalidity bit");}
    {StageTestClock::reset();BloomStageTiming r{};BloomStageToken t;StageTestClock::values={100,100};bloom_stage_begin<StageTestClock>(r,BloomStageKind::query,t);bloom_stage_end<StageTestClock>(r,t,BloomStageReturn::normal_false);
        check(r.stages[0].valid_pairs==1&&!r.stages[0].ticks&&!r.stages[0].invalid_clock,"equal positive clock values are a valid zero-duration pair");}
    {StageTestClock::reset();BloomStageTiming r{};BloomStageToken a,b;bloom_stage_begin<StageTestClock>(r,BloomStageKind::query,a);bloom_stage_begin<StageTestClock>(r,BloomStageKind::programs,b);
        bloom_stage_end<StageTestClock>(r,b,BloomStageReturn::normal_true);bloom_stage_end<StageTestClock>(r,a,BloomStageReturn::normal_true);check(r.overlap&&!r.depth,"nested scopes are diagnosed even when they return in stack order");}
    {StageTestClock::reset();BloomStageTiming r{};BloomStageToken a,b;bloom_stage_begin<StageTestClock>(r,BloomStageKind::query,a);bloom_stage_begin<StageTestClock>(r,BloomStageKind::query,b);
        bloom_stage_end<StageTestClock>(r,a,BloomStageReturn::normal_true);bloom_stage_end<StageTestClock>(r,b,BloomStageReturn::normal_true);check(r.overlap&&!r.depth,"wrong-depth normal return cannot masquerade as disjoint timing");}
    {StageTestClock::reset();BloomStageTiming r{};r.depth=UINT_MAX;BloomStageToken t;bloom_stage_begin<StageTestClock>(r,BloomStageKind::restore,t);bloom_stage_end<StageTestClock>(r,t,BloomStageReturn::normal_void);
        check(r.overlap&&r.overflow_mask==8&&r.depth==UINT_MAX-1,"depth exhaustion saturates and makes telemetry invalid without wrap");}
    {StageTestClock::reset();BloomStageTiming r{};BloomStageToken t;bloom_stage_begin<StageTestClock>(r,BloomStageKind::count,t);bloom_stage_end<StageTestClock>(r,t,BloomStageReturn::normal_void);
        check(r.overlap&&!StageTestClock::calls&&!r.depth,"invalid stage token cannot call clock or access a stage array");}
    {StageTestClock::reset();BloomStageTiming r{};{BloomStageToken t;bloom_stage_begin<StageTestClock>(r,BloomStageKind::replay,t);}
        check(r.stages[2].entered==1&&!r.stages[2].normal_true&&!r.stages[2].valid_pairs&&r.depth==1&&StageTestClock::calls==1,"leaving an unreturned token issues no destructor clock or synthetic normal return");}
    _fxrstor(saved);std::printf("stage_clock_checks=%u failures=%u injected_gpu_calls=0\n",checks-initial,failures);
}
void test_steady_clock_contract(){const auto initial=checks;
    BloomSteadyProfile gate;gate.configure(true);
    for(unsigned i=0;i<30;++i){gate.begin(true,true,12,2,i+1);check(!gate.active,"steady profile retains thirty ordinary warm completions");gate.finish(true);}
    gate.begin(false,true,12,2,31);check(!gate.active,"auxiliary scope cannot arm ordinary timing");
    gate.begin(true,false,12,2,31);check(!gate.active,"detailed logging must be exhausted before timing");
    for(unsigned i=0;i<8;++i){gate.begin(true,true,12,2,32+i);check(gate.active&&gate.used==i,"bounded steady sample claims a fresh slot");gate.finish(i!=3);}
    gate.begin(true,true,12,2,40);check(!gate.active&&gate.used==8,"failed sample still spends its slot and exhausted budget cannot restart");
    alignas(16) unsigned char saved[512],entry[512],observed[512];_fxsave(saved);__asm {fninit}__asm {fldpi}_fxsave(entry);_fxrstor(saved);
    for(unsigned fault=0;fault<7;++fault){BloomSteadyProfile p;p.configure(true);p.warm=30;p.begin(true,true,12,2,9);
        StageTestClock::reset(fault<6?fault:0,0);if(fault==6)StageTestClock::values[1]=99;BloomSteadyToken token;
        _fxrstor(entry);SetLastError(0x12349876);bloom_steady_begin<StageTestClock>(p,BloomSteadyStage::query,token);_fxsave(observed);
        check(GetLastError()==0x12349876,"steady begin preserves LastError despite injected clock");stage_fp_equal(entry,observed);
        _fxrstor(entry);SetLastError(0x98761234);bloom_steady_end<StageTestClock>(p,token);_fxsave(observed);
        check(GetLastError()==0x98761234,"steady end preserves its own LastError footprint");stage_fp_equal(entry,observed);
        const auto& c=p.records[0].stages[3];check(c.entered==1&&c.returned==1&&!p.records[0].active_mask&&c.invalid_clock==(fault?1u:0u),"clock failure or reversal affects diagnostics only");}
    {BloomSteadyProfile p;StageTestClock::reset();BloomSteadyToken t;bloom_steady_begin<StageTestClock>(p,BloomSteadyStage::before,t);bloom_steady_end<StageTestClock>(p,t);check(!StageTestClock::calls,"disabled diagnostic never reads a clock");}
    {BloomSteadyProfile p;p.configure(true);p.warm=30;p.begin(true,true,12,2,9);StageTestClock::reset();BloomSteadyToken a,b;
        bloom_steady_begin<StageTestClock>(p,BloomSteadyStage::before,a);bloom_steady_begin<StageTestClock>(p,BloomSteadyStage::query,b);
        bloom_steady_end<StageTestClock>(p,b);bloom_steady_end<StageTestClock>(p,a);check(!p.records[0].nested&&!p.records[0].active_mask,"parent callback and child stage intervals are valid nesting");
        bloom_steady_begin<StageTestClock>(p,BloomSteadyStage::before,a);bloom_steady_begin<StageTestClock>(p,BloomSteadyStage::before,b);check(p.records[0].nested,"same-stage reentry is explicitly diagnosed");p.finish(false);check(p.records[0].active_mask!=0,"unreturned stages remain explicit without destructor clocks");}
    {BloomSteadyProfile p;p.configure(true);p.warm=30;p.begin(true,true,12,2,9);p.records[0].stages[3].entered=UINT64_MAX;p.records[0].stages[3].ticks=UINT64_MAX;StageTestClock::reset();BloomSteadyToken t;
        bloom_steady_begin<StageTestClock>(p,BloomSteadyStage::query,t);bloom_steady_end<StageTestClock>(p,t);check(p.records[0].overflow&&p.records[0].stages[3].ticks==UINT64_MAX,"steady telemetry saturates instead of wrapping");}
    _fxrstor(saved);std::printf("steady_clock_checks=%u failures=%u\n",checks-initial,failures);
}
void test_stage_class_contract(){const auto initial=checks;
    for(unsigned mask=0;mask<4;++mask){Fixture f(mask);f.d.query_guard_enabled=true;const auto start=logs.size();std::map<std::string,unsigned> warm_calls;unsigned warm_reads{},warm_current{},warm_guard{};
        for(unsigned attempt=1;attempt<=3;++attempt){check(f.begin(),"stage fixture starts current scope");const auto calls=f.d.calls;const auto reads=f.d.episode_reads,current=f.d.current_calls,guards=f.d.query_guard_calls;
            f.pair();check(f.d.correction.finish(true)&&f.restored()&&!f.d.correction.pending(),"timed and untimed actual class completes exact restored pair");const auto delta=call_difference(f.d.calls,calls);
            if(attempt==2){warm_calls=delta;warm_reads=f.d.episode_reads-reads;warm_current=f.d.current_calls-current;warm_guard=f.d.query_guard_calls-guards;}
            if(attempt==3)check(delta==warm_calls&&f.d.episode_reads-reads==warm_reads&&f.d.current_calls-current==warm_current&&f.d.query_guard_calls-guards==warm_guard,"warm measured2 and unmeasured3 retain every typed/source/current/guard boundary");}
        const auto rows=stage_logs(start);check(rows.size()==2,"stage details share original per-State two-attempt budget");
        for(unsigned i=0;i<rows.size();++i){const std::uint64_t d=f.d.plan.expected_draws;expect_stage(rows[i],{3*d-1,d,d,d},{3*d-1,d,d,0},{},{0,0,0,d},true);check(rows[i].scalar("attempt")==i+1,"stage attempt ordinal matches original claim");}}
    {Fixture f;const auto start=logs.size();f.d.failure="get_vs";f.begin();check(!f.before()&&!f.d.correction.finish(true)&&stage_logs(start).empty(),"preclaim query refusal produces no stage record");
        f.d.failure.clear();check(fresh_scope(f,2100),"preclaim failure permits fresh owner scope");f.pair();check(f.d.correction.finish(true),"preclaim failure does not consume first measured attempt");const auto rows=stage_logs(start);check(rows.size()==1&&rows[0].scalar("attempt")==1,"first actual admission alone claims timing budget");}
    for(unsigned phase=0;phase<3;++phase){Fixture f;const auto start=logs.size();f.begin();const auto token=f.before();
        if(phase==0){f.d.failure="get_vs";f.after(token);}else if(phase==1){f.d.failure="function_data";f.after(token);}else{f.d.hook_name="create_ps";f.d.hook=[&]{f.d.failure="get_vs";};f.after(token);}
        check(!f.d.own_draws&&!f.d.correction.finish(true),"timed query/program refusal retains zero own writes");const auto rows=stage_logs(start);check(rows.size()==1,"partial normal finish emits one partial stage record");
        if(!rows.empty())expect_stage(rows[0],phase==0?std::array<std::uint64_t,4>{1,0,0,0}:phase==1?std::array<std::uint64_t,4>{1,1,0,0}:std::array<std::uint64_t,4>{2,1,0,0},
            phase==0?std::array<std::uint64_t,4>{}:phase==1?std::array<std::uint64_t,4>{1,0,0,0}:std::array<std::uint64_t,4>{1,1,0,0},phase==1?std::array<std::uint64_t,4>{0,1,0,0}:std::array<std::uint64_t,4>{1,0,0,0},{},false);}
    for(unsigned mode=0;mode<3;++mode){Fixture f;const auto start=logs.size();f.begin();const auto token=f.before();if(mode==0)f.d.failure="up";if(mode==1)f.d.throwing="up";if(mode==2)f.d.seh_failure="up";f.after(token);
        check(f.restored()&&!f.d.correction.pending()&&!f.d.correction.finish(true),"contained own-UP failure returns to normal restored timing boundary");const auto rows=stage_logs(start);check(rows.size()==1,"own failure partial timing is retained");if(!rows.empty())expect_stage(rows[0],{2,1,1,1},{2,1,0,0},{0,0,1,0},{0,0,0,1},false);}
    {Fixture f;const auto start=logs.size();f.begin();const auto token=f.before();f.d.hook_name="up";f.d.hook=[&]{f.d.failure="get_pc0";};f.after(token);
        check(f.d.correction.pending()&&!f.d.correction.finish(true),"two normal restoration attempts can leave a pending journal");const auto rows=stage_logs(start);check(rows.size()==1,"pending attempt logs normal restoration returns");if(!rows.empty())expect_stage(rows[0],{2,1,1,2},{2,1,1,0},{},{0,0,0,2},false);
        f.d.failure.clear();check(f.d.correction.reconcile_normal(f.d.api(),true)&&f.restored()&&stage_logs(start).size()==1,"later normal reconciliation restores journal without adding stage coverage");}
    for(unsigned mode=0;mode<3;++mode){Fixture f;const auto start=logs.size();f.begin();const auto token=f.before();const auto calls=f.d.calls;
        if(mode==0)f.after(token,E_FAIL);if(mode==1)f.d.correction.abandon(token);if(mode==2){bool result=true;std::thread foreign([&]{f.d.correction.after(token,S_OK);result=f.d.correction.finish(true);});foreign.join();check(!result&&calls==f.d.calls&&stage_logs(start).empty(),"foreign callbacks touch neither typed methods nor stage record");f.d.correction.abandon(token);}
        check(!f.d.correction.finish(true),"native failure/abandon cannot complete the measured pair");const auto rows=stage_logs(start);check(rows.size()==1,"normal owner finish may disclose a claimed abandoned attempt");if(!rows.empty())expect_stage(rows[0],{},{},{},{},false);}
    std::printf("stage_class_checks=%u failures=%u injected_gpu_calls=0\n",checks-initial,failures);
}
}
namespace bovr {void trace(const char* event,const char* format,...) noexcept {try{char text[2048]{};va_list args;va_start(args,format);vsnprintf_s(text,sizeof text,_TRUNCATE,format,args);va_end(args);logs.push_back(std::string(event)+" "+text);}catch(...){}}}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);_mm_setcsr(0x1f80);
    // Before spending either process-wide vector, every getter failure and
    // late authority revocation must prove that partial storage is not logged.
    for(unsigned position=1;position<=24;++position){Fixture f;f.d.failure="render_state";f.d.fail_at=position;f.begin();
        check(!f.before()&&!f.d.own_draws&&f.d.calls["render_state"]==position&&state_vector_logs().empty(),"each failed typed state getter aborts without complete vector or setter");}
    for(unsigned position:{1u,24u})for(unsigned mode=0;mode<7;++mode){Fixture f;f.d.hook_name="render_state";f.d.hook_at=position;
        f.d.hook=[&]{if(mode==0)throw OwnCppException{&f.d.cpp_destroyed};if(mode==1)RaiseException(0xe012c012,0,0,nullptr);
            if(mode==2)f.d.current=false;if(mode==3)++f.d.n.scene[0];if(mode==4)f.d.correction.retire();if(mode==5)f.d.correction.before(f.d.api(),f.draw());if(mode==6)++f.d.q.vertices[0];};
        f.begin();check(!f.before()&&!f.d.own_draws&&!f.d.calls["set_vc"]&&state_vector_logs().empty(),"early/late getter exception or current/retire/nested reentry never emits a complete vector");}
    {Fixture f;f.d.hook_name="render_state";f.d.hook_at=24;f.d.hook=[&]{f.d.retire_on_current=f.d.current_calls+3;};f.begin();
        check(!f.before()&&f.d.calls["render_state"]==24&&state_vector_logs().empty(),"explicit post-complete current check rejects revocation after all24 getter intervals");}
    {Fixture f;for(unsigned i=0;i<std::size(bloom_replay_blend_states);++i)f.d.states[bloom_replay_blend_states[i]]=0xabc00000+i;
        f.d.states[D3DRS_SEPARATEALPHABLENDENABLE]=TRUE;f.d.states[D3DRS_ANTIALIASEDLINEENABLE]=TRUE;f.d.states[D3DRS_FOGENABLE]=TRUE;
        const auto initial=f.d.states;f.begin();const auto token=f.before();check(token!=0,"first complete candidate has all narrowly inactive BOOLs");
        auto vectors=state_vector_logs();check(vectors.size()==1,"one complete before vector consumes one process token");
        if(vectors.size()==1){const auto& line=vectors[0];check(line.find("vector=1 producer=12 generation=2 xr_frame=9 eye=1 role=0 target=25 phase=before_native complete=1")!=std::string::npos,"vector has exact native key/role/eye/phase and complete marker");
            check(line.find("render_count=18 render_ids=7,14,52,185,15,27,206,168,8,22,174,152,161,162,176,194,28,26 render_values="+state_values(initial,false))!=std::string::npos,"all18 ordered IDs and raw render DWORDs copied exactly");
            check(line.find("blend_count=6 blend_ids=19,20,171,207,208,209 blend_values="+state_values(initial,true)+" policy_mask=0x00000000")!=std::string::npos,"all6 raw blend fields retained despite inactive master; zero mask is candidate policy only");
            check(line.size()<1536,"complete diagnostic fits actual production trace details capacity");}
        f.d.states[D3DRS_ZENABLE]=TRUE;f.d.states[D3DRS_DITHERENABLE]=9;f.d.states[D3DRS_BLENDOPALPHA]=0xdeadbeef;
        f.after(token);vectors=state_vector_logs();check(vectors.size()==2&&f.d.calls["render_state"]==48,"first policy mismatch does not hide any later state getter");
        if(vectors.size()==2){const auto& line=vectors[1];check(line.find("vector=2 producer=12 generation=2 xr_frame=9 eye=1 role=0 target=25 phase=after_native complete=1")!=std::string::npos,"second vector labels actual after-native query");
            check(line.find("render_values="+state_values(f.d.states,false))!=std::string::npos&&line.find("blend_values="+state_values(f.d.states,true))!=std::string::npos,"multiple-policy-failure vector contains every late raw field");
            check(line.find("policy_mask=0x00020001")!=std::string::npos,"fixed mask records both first and last render-policy failures");}
        check(!f.d.own_draws&&!f.d.calls["set_vc"]&&!f.d.correction.finish(true),"complete rejected vector never implies active or program acceptance");}
    {Fixture f;f.begin();f.pair();check(f.d.correction.finish(true)&&state_vector_logs().size()==2,"new object/full episode proceeds after process diagnostic exhaustion");
        f.d.correction.retire();f.begin();f.pair();check(f.d.correction.finish(true)&&state_vector_logs().size()==2,"retirement and fresh begin cannot reset two-vector process cap");}
    for(unsigned mask=0;mask<4;++mask){Fixture f(mask);f.begin();f.pair();check(f.d.correction.finish(true)&&f.d.own_draws==f.d.plan.expected_draws,"actual corrected full chain all four current ping/pong plans");}
    {Fixture f;f.begin(false);check(!f.before()&&f.d.calls.empty()&&f.d.correction.finish(true),"default-off zero COM/state allocation path");}
    for(auto state:{D3DRS_ALPHABLENDENABLE,D3DRS_ZENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE}){Fixture f;f.d.states[state]=1;f.begin();check(!f.before()&&!f.d.own_draws,"unknown interfering state rejects before Set");check(!f.d.correction.finish(true),"selected state rejection discards exact pair");}
    {Fixture f;f.d.states[D3DRS_MULTISAMPLEANTIALIAS]=TRUE;f.d.states[D3DRS_MULTISAMPLEMASK]=0x12345678;f.begin();auto t=f.before();check(t!=0,"inactive MSAA flags accepted only with actual single-sample descriptor");f.after(t);check(f.restored(),"inactive MSAA flags do not affect restoration");}
    for(DWORD selector:{FALSE,TRUE}){Fixture f;f.d.states[D3DRS_TWOSIDEDSTENCILMODE]=selector;
        for(auto state:bloom_replay_blend_states)f.d.states.emplace(state,0);const auto states=f.d.states;
        check(f.begin(),"inactive stencil fixture begins actual owned scope");f.pair();check(f.d.correction.finish(true),"actual full chain accepts queried BOOL two-sided selector only with master stencil disabled");
        check(f.d.own_draws==26,"inactive stencil full plan replays every ordinary role exactly once");check(states==f.d.states,"actual replay never forces any queried render state");}
    for(DWORD selector:std::array<DWORD,4>{FALSE,TRUE,2u,0xffffffffu}){Fixture f;f.d.states[D3DRS_STENCILENABLE]=TRUE;f.d.states[D3DRS_TWOSIDEDSTENCILMODE]=selector;f.begin();check(!f.before()&&!f.d.own_draws&&!f.d.calls["set_vc"]&&!f.d.correction.finish(true),"enabled master stencil rejects every selector before any active setter");}
    for(DWORD selector:{2u,0xffffffffu}){Fixture f;f.d.states[D3DRS_TWOSIDEDSTENCILMODE]=selector;f.begin();check(!f.before()&&!f.d.own_draws,"inactive selector still requires a documented BOOL value");}
    {Fixture f;f.d.states[D3DRS_TWOSIDEDSTENCILMODE]=TRUE;f.begin();const auto t=f.before();check(t!=0,"copied inactive selector admitted before original draw");f.d.states[D3DRS_TWOSIDEDSTENCILMODE]=FALSE;f.after(t);check(!f.d.own_draws&&!f.d.calls["set_vc"]&&!f.d.correction.finish(true),"inactive selector remains exact across before/after snapshots and mutation rejects before setters");}
    {Fixture f;f.d.states[D3DRS_TWOSIDEDSTENCILMODE]=TRUE;f.d.failure="render_state";f.d.fail_at=4;f.begin();check(!f.before()&&!f.d.own_draws,"inactive selector getter failure remains an admission failure");}
    for(DWORD value:{FALSE,TRUE}){Fixture f;for(auto state:bloom_replay_blend_states)f.d.states.emplace(state,0x12345678);
        for(auto state:{D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_ANTIALIASEDLINEENABLE})f.d.states[state]=value;const auto initial=f.d.states;
        f.begin();f.pair();check(f.d.correction.finish(true)&&f.d.own_draws==26&&f.d.states==initial,"all13 roles both eyes accept reviewed inactive BOOLs without forcing any render/blend state");}
    for(auto state:{D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_ANTIALIASEDLINEENABLE}){
        for(DWORD value:{2u,0xffffffffu}){Fixture f;f.d.states[state]=value;f.begin();check(!f.before()&&!f.d.own_draws&&!f.d.calls["set_vc"],"newly inactive states still reject undocumented BOOL values");}
        for(DWORD value:{FALSE,TRUE}){Fixture f;f.d.states[state]=value;f.begin();auto token=f.before();check(token!=0,"inactive BOOL mutation fixture admitted before native call");f.d.states[state]=value?FALSE:TRUE;f.after(token);
            check(!f.d.own_draws&&!f.d.calls["set_vc"]&&!f.d.correction.finish(true),"inactive BOOL remains exact across before/after and never permits changed state replay");}
        for(unsigned phase:{1u,25u,49u}){Fixture f;f.d.states[state]=TRUE;f.d.hook_name="render_state";f.d.hook_at=phase;f.d.hook=[&]{++f.d.n.scene[0];};f.begin();auto token=f.before();if(token)f.after(token);
            check(!f.d.own_draws&&!f.d.calls["set_vc"]&&!f.d.correction.finish(true),"current tuple mutation in either post-state checkpoint still refuses all active setters");}}
    for(DWORD master:{1u,2u})for(DWORD separate:{FALSE,TRUE}){Fixture f;f.d.states[D3DRS_ALPHABLENDENABLE]=master;f.d.states[D3DRS_SEPARATEALPHABLENDENABLE]=separate;f.begin();check(!f.before()&&!f.d.own_draws&&!f.d.calls["set_vc"],"active/unknown blend master rejects both subordinate BOOLs");}
    for(auto fill:{D3DFILL_POINT,D3DFILL_WIREFRAME}){Fixture f;f.d.states[D3DRS_FILLMODE]=fill;f.d.states[D3DRS_ANTIALIASEDLINEENABLE]=TRUE;f.begin();check(!f.before()&&!f.d.own_draws,"line-AA allowance never expands fill-mode admission");}
    for(auto type:{D3DPT_LINELIST,D3DPT_LINESTRIP}){Fixture f;f.d.states[D3DRS_ANTIALIASEDLINEENABLE]=TRUE;f.begin();auto draw=f.draw();draw.type=type;check(!f.d.correction.before(f.d.api(),draw)&&!f.d.own_draws&&!f.d.calls["render_state"],"line-AA allowance never expands actual native primitive contract");}
    for(bool vertex:{false,true})for(unsigned mutation:{0u,1u}){Fixture f;f.d.states[D3DRS_FOGENABLE]=TRUE;auto& original=vertex?*f.d.vs:*f.d.ps;
        std::vector<std::uint32_t> changed(original.words,original.words+original.bytes/4);if(mutation==0)changed[0]=vertex?D3DVS_VERSION(2,0):D3DPS_VERSION(2,0);else changed[changed.size()-2]^=1;
        original.words=changed.data();f.begin();auto token=f.before();check(token!=0,"candidate fog state may be copied before exact current program verification");f.after(token);
        check(!f.d.own_draws&&!f.d.calls["set_vc"]&&!f.d.calls["set_vs"]&&!f.d.calls["set_pc0"]&&!f.d.calls["set_ps"]&&!f.d.correction.finish(true),"SM3 caps and fog BOOL never authorize wrong-version or changed actual shader bytes");}
    for(unsigned mask=0;mask<4;++mask){Fixture f(mask);f.use_native_geometry();f.d.states[D3DRS_CULLMODE]=D3DCULL_CCW;
        for(auto state:bloom_replay_blend_states)f.d.states.emplace(state,0);const auto initial=f.d.states;f.begin();f.pair();
        check(f.d.correction.finish(true)&&f.d.own_draws==f.d.plan.expected_draws&&f.d.states==initial,"CCW active class accepts every current plan with per-role native quad/matrix recipe and unchanged render state");}
    for(DWORD cull:{0u,2u,4u,0xffffffffu}){Fixture f;f.use_native_geometry();f.d.states[D3DRS_CULLMODE]=cull;f.begin();check(!f.before()&&!f.d.own_draws&&!f.d.calls["set_vc"],"CW and unknown cull values reject before any active setter");}
    for(DWORD cull:{1u,3u}){Fixture f;f.use_native_geometry();f.d.states[D3DRS_CULLMODE]=cull;f.begin();auto token=f.before();check(token!=0,"NONE or verified CCW candidate admitted");f.d.states[D3DRS_CULLMODE]=cull==1?3:1;f.after(token);
        check(!f.d.own_draws&&!f.d.calls["set_vc"]&&!f.d.correction.finish(true),"NONE to CCW and CCW to NONE mutation still rejects exact raw before/after mismatch");}
    {Fixture f;f.use_native_geometry();f.d.states[D3DRS_CULLMODE]=D3DCULL_CCW;f.d.failure="render_state";f.d.fail_at=10;f.begin();check(!f.before()&&!f.d.own_draws&&!f.d.calls["set_vc"],"CCW getter failure never gains admission");}
    for(auto fill:{D3DFILL_POINT,D3DFILL_WIREFRAME}){Fixture f;f.use_native_geometry();f.d.states[D3DRS_CULLMODE]=D3DCULL_CCW;f.d.states[D3DRS_FILLMODE]=fill;f.begin();check(!f.before()&&!f.d.own_draws,"verified CCW winding does not expand fill mode");}
    for(unsigned mode=0;mode<8;++mode){Fixture f;if(mode==0)f.d.elements[3].Stream=1;if(mode==1)f.d.elements[2].Type=D3DDECLTYPE_FLOAT3;if(mode==2)f.d.bad_desc=true;if(mode==3)f.d.bad_matrix=true;if(mode==4)f.d.alias=true;if(mode==5)f.d.extra_rt=true;if(mode==6)f.d.unsupported_sampler=true;if(mode==7)f.d.pure=true;
        f.begin();check(!f.before()&&!f.d.own_draws,"declaration/MRT/alias/descriptor/matrix/sampler/pure rejection before writes");}
    for(unsigned mode=0;mode<3;++mode){Fixture f;f.d.elements[3].Type=D3DDECLTYPE_UBYTE4;if(mode==1)f.d.elements[3].Stream=1;if(mode==2)f.d.elements[3].Type=D3DDECLTYPE_FLOAT2;
        f.begin();auto t=f.before();if(mode==0){check(t!=0,"unused packed UBYTE4 normal fits exact 32-byte stream0 declaration");f.after(t);check(f.d.own_draws==1&&f.restored(),"unused normal encoding leaves actual replay and restoration identical");}
        else check(!t&&!f.d.own_draws,"unused normal still requires stream0 and exact nonoverlapping four-byte field");}
    for(const char* op:{"get_vs","get_ps","get_target","object_device","object_query","desc","texture_level","get_stream","get_indices","get_declaration","declaration_elements","get_vc","get_pc0","render_state","sampler_state"})for(unsigned mode=0;mode<3;++mode){Fixture f;f.begin();if(mode==0)f.d.failure=op;if(mode==1)f.d.throwing=op;if(mode==2)f.d.seh_failure=op;check(!f.before()&&!f.d.own_draws,"before partial-acquisition HRESULT/C++/SEH never writes");}
    for(const char* op:{"set_vc","set_vs","set_pc0","set_ps","up"})for(unsigned mode=0;mode<3;++mode){Fixture f;f.begin();const auto t=f.before();check(t!=0,"failure fixture starts from admitted current draw");if(mode==0){f.d.failure=op;f.d.fail_at=1;}if(mode==1)f.d.throwing=op;if(mode==2)f.d.seh_failure=op;f.after(t);
        check(f.restored()&&!f.d.correction.pending()&&!f.d.correction.finish(true),"own failed/throwing/SEH call restores all actual changes after containment");
        if(mode==1)check(f.d.cpp_destroyed>0,"MSVC C++ exception reaches own outer catcher and destroys exception object");}
    for(const char* op:{"set_vc","set_pc0"}){Fixture f;f.begin();auto t=f.before();f.d.partial_at=1;f.d.failure=op;f.d.fail_at=1;f.after(t);check(f.restored()&&!f.d.correction.pending(),"partial failed constant vector setter restored by component attribution");}
    for(const char* op:{"get_ps","get_vc","get_pc0","get_stream","get_indices"}){Fixture f;f.begin();auto t=f.before();f.d.hook_name="up";f.d.hook=[&]{f.d.failure=op;};f.after(t);check(f.d.correction.pending()&&!f.d.correction.finish(true),"failed normal restoration retains journal and rejects pair");
        const auto calls=f.d.calls;check(!f.d.correction.reconcile_normal(f.d.api(),false)&&calls==f.d.calls,"lost ResetEx state never queried for cleanup");check(!f.d.correction.retire(),"retirement cannot lose pending state ownership");f.d.failure.clear();check(f.d.correction.reconcile_normal(f.d.api(),true)&&f.restored(),"successful normal recovery checks/restores preserved ResetEx state");}
    for(const char* op:{"get_vs","desc","function_data","set_ps","up","object_release"}){Fixture f;f.begin();auto t=f.before();f.d.hook_name=op;f.d.hook=[&]{check(f.d.correction.busy(),"outbound call retains operation lease through Release");f.d.correction.retire();};f.after(t);check(!f.d.correction.finish(true)&&!f.d.correction.pending(),"retirement reentry invalidates without losing normal rollback");}
    {Fixture f;f.begin();auto t=f.before();const auto calls=f.d.calls;f.d.correction.abandon(t);check(calls==f.d.calls&&f.d.programs[0].refs>1,"native unwind only abandons scalar authority, holds references");check(!f.d.correction.finish(true),"faulted native draw never becomes complete");}
    for(auto hr:{E_FAIL,S_FALSE,D3DERR_DEVICELOST}){Fixture f;f.begin();auto t=f.before();const auto creates=f.d.calls["create_ps"];f.after(t,hr);check(!f.d.own_draws&&f.d.calls["create_ps"]==creates,"only exact original D3D_OK permits own replay");}
    {unsigned last_current{};{Fixture control;control.begin();control.after(control.before());last_current=control.d.first_set_current;check(last_current>0,"observe final current callback before first setter");}
        Fixture f;f.d.retire_on_current=last_current;f.begin();f.after(f.before());check(!f.d.calls["set_vc"]&&!f.d.own_draws&&!f.d.correction.finish(true),"last source.current reentry cannot return true authority after retirement");}
    for(const char* op:{"get_vs","desc","get_texture","function_data","create_ps"})for(unsigned mode=0;mode<3;++mode){Fixture f;f.begin();auto t=f.before();f.d.hook_name=op;f.d.hook=[&]{if(mode==0)++f.d.n.scene[0];else if(mode==1)++f.d.plan.roles[0][3].material;else ++f.d.q.vertices[0];};f.after(t);
        check(!f.d.own_draws&&!f.d.calls["set_vc"]&&!f.d.correction.finish(true),"post-getter native plan/quad mutation refuses all setters");}
    {Fixture f;f.begin();f.before();f.d.correction.end_scope();f.d.current=false;const auto n=f.d.current_calls;check(!f.before()&&f.d.current_calls==n,"caught backend failure end_scope clears every borrowed callback before stack dies");}
    {Fixture f;f.d.q.device^=4;f.begin();check(!f.before()&&!f.d.calls["creation"],"copied native primitive device must equal actual retained device before any COM");}
    for(unsigned degree:{1u,2u,8u,16u,17u}){Fixture f;f.d.anisotropy=degree;f.begin();auto t=f.before();check((t!=0)==(degree<=16),"retained legal anisotropy degree allowed only with actual LINEAR filtering and device cap");if(t)f.after(t);}
    for(bool before_increment:{false,true}){Fixture f;f.d.add_throw_before=before_increment;if(!before_increment){f.d.throwing="device_add";f.d.expected_device_refs=2;}f.begin();check(!f.d.correction.configured()&&!f.before()&&!f.d.calls["device_release"],"faulting AddRef disables correction and never releases unconfirmed ownership");}
    {unsigned final_release{};{Fixture control(0);control.begin();control.pair();final_release=control.d.calls["object_release"];check(control.d.correction.finish(true),"final-release control full pair");}
        Fixture f(0);f.begin();f.d.hook_name="object_release";f.d.hook_at=final_release;f.d.hook=[&]{f.d.correction.retire();};f.pair();check(!f.d.correction.finish(true),"final resource Release retirement cannot become accepted exact completion");}
    {Fixture f(0);f.begin();f.pair();f.d.current=false;check(!f.d.correction.finish(true),"epoch revocation after final draw rejects active completion itself");}
    {Fixture f;f.begin();f.pair();check(f.d.correction.finish(true),"first bounded logged pair");f.begin();f.pair();check(f.d.correction.finish(true),"second bounded logged pair");
        const auto before_logs=logs.size();f.begin();const auto configured_logs=logs.size();f.pair();check(f.d.correction.finish(true)&&logs.size()==configured_logs&&configured_logs==before_logs+1,"active third pair continues while two-attempt detailed logs remain bounded");}
    for(unsigned scale:{1u,2u,3u,4u})for(unsigned mask=0;mask<4;++mask){Fixture f(mask);f.d.resolution_scale=scale;f.use_native_geometry();
        check(f.begin(),"known source resolution begins independent bloom episode");f.pair();
        check(f.d.correction.finish(true),"all bloom roles restore and complete both eyes at every supported resolution");}
    for(unsigned scale:{2u,3u}){Fixture f;f.d.resolution_scale=scale;f.use_native_geometry();++f.d.n.scene_viewport.x;f.begin();
        check(!f.before()&&!f.d.own_draws,"mismatched scene extent refuses replay before setters");}
    for(unsigned index=0;index<4;++index)for(unsigned eye:{0u,1u}){std::array<float,4> clamp{};
        const auto inward=[](double exact,bool lower){const float rounded=static_cast<float>(exact);
            return (lower?static_cast<double>(rounded)<exact:static_cast<double>(rounded)>exact)?
                std::nextafter(rounded,lower?INFINITY:-INFINITY):rounded;};
        const double dx=1.0/static_cast<double>(320u<<index),dy=1.0/static_cast<double>(180u<<index);
        const std::array<float,4> expected=eye?std::array<float,4>{inward(.5+dx,true),inward(dy,true),inward(1-dx,false),inward(1-dy,false)}:
            std::array<float,4>{inward(dx,true),inward(dy,true),inward(.5-dx,false),inward(1-dy,false)};
        check(bloom_replay_bounds(eye,160u<<index,90u<<index,clamp)&&clamp==expected,
            "computed inward clamp follows the half-texel pyramid formula");}
    for(unsigned scale:{1u,2u,3u,4u})for(unsigned divisor:{1u,2u,4u,8u})for(unsigned eye:{0u,1u}){
        const unsigned width=1280*scale/divisor,height=720*scale/divisor;std::array<float,4> clamp{};
        check(bloom_replay_bounds(eye,width,height,clamp),"every known pyramid has safe clamp constants");
        const std::array<double,4> exact{(double(eye)*width/2+.5)/width,.5/height,(double(eye+1)*width/2-.5)/width,(height-.5)/height};
        for(unsigned i=0;i<4;++i){const bool lower=i<2;const auto outward=std::nextafter(clamp[i],lower?-INFINITY:INFINITY);
            check((lower?double(clamp[i])>=exact[i]:double(clamp[i])<=exact[i])&&
                (lower?double(outward)<exact[i]:double(outward)>exact[i]),"each clamp is the nearest inward binary32 half-texel bound");}
        PostfxResolution scene{1280*scale,720*scale};auto desc=description(divisor==1?3:divisor==2?25:divisor==4?26:27,scale);
        check(bloom_replay_desc(desc,divisor==1?3:divisor==2?25:divisor==4?26:27,scene),"exact sized target descriptor accepted");
        ++desc.Width;check(!bloom_replay_desc(desc,3,scene),"changed descriptor size fails closed");
    }
    for(auto size:std::array<std::array<unsigned,2>,3>{{{1280,721},{1920,1080},{6400,3600}}}){
        std::array<float,4> clamp{};
        check(!postfx_resolution(size[0],size[1]),"unsupported full scene resolution refused");
        if(size[0]!=1920)check(!bloom_replay_bounds(0,size[0],size[1],clamp),"unsupported pyramid extent refused");
    }
    test_blur_current_rows();test_query_storage_recovery();test_native_pixel_bank();test_exact_plan_predicate_reuse();test_query_group_class_contract();
    test_stage_clock_contract();test_steady_clock_contract();test_stage_class_contract();test_owner_proof_class_contract();
    // Isolated CPU poison injection; the production accessor has no reset API.
    std::thread poison([]{indexed_reentries=indexed_draw_reentry_exhausted;Fixture f;f.begin();check(!f.before()&&!f.d.own_draws,"equal saturated witness values never authorize correction");});poison.join();
    std::printf("bloom_correction_checks=%u failures=%u gpu_calls=0\n",checks,failures);return failures?1:0;
}
