// SPDX-License-Identifier: GPL-3.0-only
#include "t5/water_vpos_correction.hpp"
#include "t5/water_vpos_program.hpp"
#include "t5/water_vpos_journal.hpp"
#include "t5/water_owned_call.hpp"
#include "t5/water_reflection_program.hpp"
#include "t5/vpos_eye_scope.hpp"
#include "loader/trace.hpp"
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <vector>
namespace bovr::t5 {
namespace {
struct Preserve {DWORD error;alignas(16) unsigned char fp[512];Preserve() noexcept:error(GetLastError()){_fxsave(fp);}~Preserve(){_fxrstor(fp);SetLastError(error);}};
template<class F>HRESULT seh(const F& f) noexcept {__try{return f();}__except(EXCEPTION_EXECUTE_HANDLER){return E_FAIL;}}
template<class F>HRESULT contained(const F& f) noexcept {try{return seh(f);}catch(...){return E_FAIL;}}
template<class T>void drop(T*& p) noexcept {auto* old=p;p=nullptr;if(old)contained([&]{old->Release();return S_OK;});}
template<class T>bool copy(std::uintptr_t address,T& out) noexcept {
    if(address<0x10000||address>UINT32_MAX-sizeof(T))return false;
    __try{std::memcpy(&out,reinterpret_cast<const void*>(address),sizeof(T));return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
struct WaterVPosCorrection::State {
    std::uintptr_t base{};bool configured{},enabled{},probe{},open{},faulted{},qualified{};
    std::atomic<unsigned> leases{};DWORD owner{};IDirect3DDevice9* device{};
    IUnknown* device_identity{};std::uint64_t witness{},next_token{},token{},draws{};
    WaterShadingEyeContext eye{};WaterVPosJournal journal{};unsigned program{},rejections{};
    std::array<std::array<unsigned,3>,2> reports{};
    std::array<std::array<std::uint64_t,3>,2> last_report{};bool reported_draw{};
    struct Pixel {IDirect3DPixelShader9* object{};IDirect3DPixelShader9* derivative{};int program{-1};bool device_verified{};};
    Pixel* active_pixel{};
    IDirect3DPixelShader9* active_derivative{};
    WaterReflectionUv uv{};D3DSURFACE_DESC reflection_desc{};
    std::uintptr_t reflection_texture{},reflection_surface{};
    // Open-addressed retained identities. Every unknown object is classified
    // once; an ordinary unrelated draw needs just GetPixelShader + this lookup.
    std::array<Pixel,1024> pixels{};unsigned pixel_count{};
    struct Vertex {IDirect3DVertexShader9* object{};int program{-1};bool device_verified{};};
    std::array<Vertex,64> vertices{};
    struct Operation {State& s;explicit Operation(State& value):s(value){s.leases.fetch_add(1);}~Operation(){s.leases.fetch_sub(1);}};
    bool basic() const noexcept {
        return GetCurrentThreadId()==owner&&leases.load()==1&&witness!=indexed_draw_reentry_exhausted&&indexed_draw_reentry_serial()==witness;
    }
    bool current() noexcept {
        if(!basic())return false;if(!qualified)return true;
        return water_shading_eye_current(eye);
    }
    bool full_current() noexcept {
        if(!basic())return false;if(!qualified)return true;
        WaterShadingEyeContext now{};return read_water_shading_eye(&now)&&now==eye;
    }
    template<class F>bool call(const F& f) noexcept {
        return water_owned_call([&]() noexcept {return current();},[&]() noexcept {return contained(f)==D3D_OK;});
    }
    bool identity(IUnknown* a,IUnknown* b) noexcept {
        IUnknown* ia{},*ib{};
        const bool okay=a&&b&&call([&]{return a->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&ia));})&&ia&&
            call([&]{return b->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&ib));})&&ib&&ia==ib;
        drop(ib);drop(ia);return okay&&current();
    }
    template<class Object>bool device_of(Object* object) noexcept {
        IDirect3DDevice9* returned{};IUnknown* identity{};
        const bool okay=call([&]{return object->GetDevice(&returned);})&&returned&&
            call([&]{return returned->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&identity));})&&identity==device_identity;
        drop(identity);drop(returned);return okay&&current();
    }
    template<class Shader>int classify(Shader* shader,bool pixel) noexcept {
        UINT bytes{};if(!call([&]{return shader->GetFunction(nullptr,&bytes);}))return -2;
        bool possible=false;for(const auto& p:water_vpos_programs())possible=possible||(bytes==(pixel?p.pixel_bytes:p.vertex_bytes));
        if(!possible)return -1;
        std::vector<std::uint32_t> code;try{code.resize(bytes/4);}catch(...){return -2;}
        UINT actual=bytes;if(!call([&]{return shader->GetFunction(code.data(),&actual);})||actual!=bytes)return -2;
        std::array<char,65> sha{};if(!water_shader_sha256(code,&sha))return -2;
        for(unsigned i=0;i<water_vpos_programs().size();++i){const auto& p=water_vpos_programs()[i];
            if(bytes==(pixel?p.pixel_bytes:p.vertex_bytes)&&std::strcmp(sha.data(),pixel?p.pixel_sha256:p.vertex_sha256)==0)return int(i);}
        return -1;
    }
    int pixel_program(IDirect3DPixelShader9*& object) noexcept {
        active_derivative=nullptr;active_pixel=nullptr;
        unsigned index=unsigned((reinterpret_cast<std::uintptr_t>(object)>>4)*2654435761u)&1023u;
        for(unsigned n=0;n<pixels.size();++n,index=(index+1)&1023u){auto& entry=pixels[index];
            if(entry.object==object){active_pixel=&entry;active_derivative=entry.derivative;return entry.program;}
            if(!entry.object){if(pixel_count>=512)return -1;const int matched=classify(object,true);if(matched==-2)return -1;
                IDirect3DPixelShader9* derivative{};int accepted=matched;
                if(matched>=0){std::vector<std::uint32_t> original,modified;unsigned bytes{};
                    const bool ready=water_shader_capture([&](void* data,unsigned* size) noexcept {
                        return call([&]{return object->GetFunction(data,size);});},&original,&bytes)&&
                        water_vpos_make_derivative(unsigned(matched),original,&modified)&&
                        call([&]{return device->CreatePixelShader(reinterpret_cast<const DWORD*>(modified.data()),&derivative);})&&derivative;
                    if(!ready){drop(derivative);accepted=-1;if(probe)trace("t5_water_reflection_derivative_refused","program=%d cached=1",matched);}
                }
                entry={object,derivative,accepted,false};object=nullptr;++pixel_count;active_pixel=&entry;active_derivative=derivative;return accepted;}
        }
        return -1;
    }
    bool vertex_program(IDirect3DVertexShader9*& object) noexcept {
        for(auto& entry:vertices){if(entry.object==object){
                if(entry.program!=int(program))return false;
                if(!entry.device_verified)entry.device_verified=device_of(entry.object);
                return entry.device_verified&&current();}
            if(!entry.object){const int matched=classify(object,false);if(matched==-2)return false;
                entry={object,matched,false};object=nullptr;
                if(matched!=int(program))return false;
                entry.device_verified=device_of(entry.object);return entry.device_verified&&current();}}
        return false;
    }
    void reject(const char* why) noexcept {
        if(probe&&rejections++<16)trace("t5_water_actual_vpos_rejected","reason=%s program=%u producer=%llu eye=%u pending=%u",why,program,eye.draw.owner.producer,eye.draw.owner.eye,journal.mask);
    }
    unsigned constant_register(unsigned i) const noexcept {
        return i<3?water_vpos_programs()[program].registers[i]:(i==3?water_affine_register:water_bounds_register);
    }
    bool set_constant(unsigned i,const VPosVector& value) noexcept {
        return call([&]{return device->SetPixelShaderConstantF(constant_register(i),value.data(),1);});
    }
    bool set_shader(std::uintptr_t value) noexcept {
        return call([&]{return device->SetPixelShader(reinterpret_cast<IDirect3DPixelShader9*>(value));});
    }
    bool get_shader(std::uintptr_t& value) noexcept {
        IDirect3DPixelShader9* shader{};
        const bool okay=call([&]{return device->GetPixelShader(&shader);});
        value=reinterpret_cast<std::uintptr_t>(shader);drop(shader);return okay&&current();
    }
    bool restore() noexcept {
        if(journal.mask&&!full_current()){faulted=true;reject("restore_owner");return false;}
        const bool okay=journal.restore([&](unsigned i,const VPosVector& value) noexcept {return set_constant(i,value);},
                                       [&](std::uintptr_t shader) noexcept {return set_shader(shader);});
        if(!okay){faulted=true;reject("restore");}return okay;
    }
    void record(bool applied) noexcept {
        const auto& n=eye.draw.owner;reported_draw=false;if(!probe||n.eye>1)return;
        auto& count=reports[n.eye][program];auto& last=last_report[n.eye][program];
        if(count>=3||(count&&n.producer<last+120))return;++count;last=n.producer;reported_draw=true;
        const auto& p=water_vpos_programs()[program];
        trace("t5_water_actual_vpos_witness","producer=%llu generation=%llu xr_frame=%llu native_frame=%u eye=%u program=%u pixel_sha=%s vertex_sha=%s target=%ux%u viewport=%d,%d,%d,%d enabled=%u applied=%u native_writes=0 shader_replacement=1 extra_draws=0",
            n.producer,n.generation,n.xr_frame,n.native_frame,n.eye,program,p.pixel_sha256,p.vertex_sha256,n.target_width,n.target_height,n.viewport.x,n.viewport.y,n.viewport.width,n.viewport.height,enabled,applied);
        for(unsigned i=0;i<3;++i){const auto& a=journal.saved[i];const auto& b=journal.written[i];VPosVector gpu{};
            const bool read=call([&]{return device->GetPixelShaderConstantF(p.registers[i],gpu.data(),1);});
            const bool verified=read&&WaterVPosJournal::same(gpu,applied?b:a);
            trace("t5_water_actual_vpos_constant","producer=%llu eye=%u program=%u register=%u native=%.9g,%.9g,%.9g,%.9g corrected=%.9g,%.9g,%.9g,%.9g gpu=%.9g,%.9g,%.9g,%.9g applied_readback=%u w_preserved=%u",n.producer,n.eye,program,p.registers[i],a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3],gpu[0],gpu[1],gpu[2],gpu[3],verified,std::memcmp(&a[3],&b[3],4)==0);}
        std::uintptr_t actual_shader{};const bool shader_verified=get_shader(actual_shader)&&
            actual_shader==(applied?journal.written_shader:journal.saved_shader);
        bool uv_verified=true;for(unsigned i=3;i<5;++i){VPosVector actual{};
            uv_verified=call([&]{return device->GetPixelShaderConstantF(constant_register(i),actual.data(),1);})&&
                WaterVPosJournal::same(actual,applied?journal.written[i]:journal.saved[i])&&uv_verified;}
        trace("t5_water_reflection_uv_witness","producer=%llu eye=%u program=%u sampler=%u texture=0x%lx surface=0x%lx texture_size=%ux%u format=%u levels=1 native_rt5=1 viewport=%d,%d,%d,%d affine=%.9g,%.9g,%.9g,%.9g bounds=%.9g,%.9g,%.9g,%.9g original_ps=0x%lx derivative_ps=0x%lx actual_ps=0x%lx shader_readback=%u uv_readback=%u applied=%u",
            n.producer,n.eye,program,p.reflection_sampler,reflection_texture,reflection_surface,reflection_desc.Width,reflection_desc.Height,unsigned(reflection_desc.Format),
            n.viewport.x,n.viewport.y,n.viewport.width,n.viewport.height,uv.affine[0],uv.affine[1],uv.affine[2],uv.affine[3],
            uv.bounds[0],uv.bounds[1],uv.bounds[2],uv.bounds[3],journal.saved_shader,journal.written_shader,actual_shader,shader_verified,uv_verified,applied);
    }
};
WaterVPosCorrection::WaterVPosCorrection() noexcept=default;
WaterVPosCorrection::~WaterVPosCorrection()=default;
void WaterVPosCorrection::configure(std::uintptr_t base) noexcept {
    Preserve preserve;try{if(!state_)state_=std::make_unique<State>();auto& s=*state_;if(s.configured)return;
        s.base=base;s.configured=base>=0x10000&&base<=UINT32_MAX-0x4240000;
        wchar_t value[4]{};const auto enabled=GetEnvironmentVariableW(L"BOVR_WATER_ACTUAL_VPOS",value,4);s.enabled=!(enabled==1&&value[0]==L'0');
        const auto probe=GetEnvironmentVariableW(L"BOVR_WATER_REFLECTION_PROBE",value,4);s.probe=probe==1&&value[0]==L'1';
        trace("t5_water_actual_vpos_configured","configured=%u enabled=%u probe=%u exact_program_pairs=3 cached_pixel_limit=512 constant_vectors=5 native_writes=0 shader_replacement=1 reflection_uv_rt5=1 ownership_recheck=draw_local_exact shader_device_cache=retained_identity",s.configured,s.enabled,s.probe);
    }catch(...){}
}
std::uint64_t WaterVPosCorrection::before(IDirect3DDevice9* device,const IndexedDrawCall& draw) noexcept {
    if(!state_)return 0;auto& s=*state_;
    if(!s.configured||(!s.enabled&&!s.probe)||s.faulted||s.open||s.journal.mask||s.leases.load()||draw.type!=D3DPT_TRIANGLELIST||!device)return 0;
    Preserve preserve;
    if((s.owner&&s.owner!=GetCurrentThreadId())||(s.device&&s.device!=device))return 0;
    State::Operation operation(s);s.owner=GetCurrentThreadId();s.device=device;s.witness=indexed_draw_reentry_serial();s.qualified=false;
    IDirect3DPixelShader9* pixel{};IDirect3DVertexShader9* vertex{};IDirect3DSurface9* target{},*reflection_surface{};
    IDirect3DBaseTexture9* sampled{};IDirect3DTexture9* reflection_texture{};
    const auto cleanup=[&]{drop(reflection_surface);drop(reflection_texture);drop(sampled);drop(target);drop(vertex);drop(pixel);};
    // The only COM getter on cached unrelated draws. No stale native metadata.
    if(!s.call([&]{return device->GetPixelShader(&pixel);})||!pixel){cleanup();return 0;}
    auto* const matched_pixel=pixel;const int found=s.pixel_program(pixel);if(found<0){cleanup();return 0;}s.program=unsigned(found);
    if(!read_water_shading_eye(&s.eye)||s.eye.draw.owner.base!=s.base||s.eye.draw.owner.device!=reinterpret_cast<std::uintptr_t>(device)){cleanup();s.reject("eye_owner");return 0;}
    s.qualified=true;const auto& n=s.eye.draw.owner;const auto& p=water_vpos_programs()[s.program];
    D3DVIEWPORT9 viewport{};D3DSURFACE_DESC desc{};VPosMatrix projection{};
    std::uint32_t native_target{},native_reflection{};VPosWorldConstants actual{},corrected{};
    UINT reflection_levels{};D3DRESOURCETYPE reflection_type{};DWORD min_filter{},mag_filter{};
    s.reflection_desc={};
    bool okay=false;const char* stage="device_identity";
    do {
        if(!s.device_identity&&(!s.call([&]{return device->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&s.device_identity));})||!s.device_identity))break;
        // A cache insertion took ownership of pixel's getter reference. Query
        // the current object again only for a recognized water draw.
        if(!pixel&&(!s.call([&]{return device->GetPixelShader(&pixel);})||!pixel))break;
        if(pixel!=matched_pixel)break;
        // Shader ownership cannot change while these COM objects are retained.
        // Current binding is still queried every draw, and retirement/reset
        // releases both objects and this device-scoped immutable fact.
        if(!s.active_pixel||s.active_pixel->object!=pixel||!s.active_derivative)break;
        if(!s.active_pixel->device_verified){
            if(!s.device_of(pixel)||!s.device_of(s.active_derivative))break;
            s.active_pixel->device_verified=true;
        }
        stage="vertex_program";if(!s.call([&]{return device->GetVertexShader(&vertex);})||!vertex||!s.vertex_program(vertex))break;
        stage="viewport";if(!s.call([&]{return device->GetViewport(&viewport);})||viewport.X!=unsigned(n.viewport.x)||viewport.Y!=unsigned(n.viewport.y)||
            viewport.Width!=unsigned(n.viewport.width)||viewport.Height!=unsigned(n.viewport.height)||viewport.MinZ!=0.015625f||viewport.MaxZ!=1.f)break;
        stage="target3";if(!copy(s.base+0x41eb1e8+3*20+4,native_target)||!native_target||
            !s.call([&]{return device->GetRenderTarget(0,&target);})||!target||
            !s.identity(target,reinterpret_cast<IUnknown*>(native_target))||!s.device_of(target))break;
        std::uint32_t repeated{};if(!copy(s.base+0x41eb1e8+3*20+4,repeated)||repeated!=native_target||
            !s.call([&]{return target->GetDesc(&desc);})||desc.Width!=n.target_width||desc.Height!=n.target_height||
            desc.Type!=D3DRTYPE_SURFACE||desc.Usage!=D3DUSAGE_RENDERTARGET||desc.Pool!=D3DPOOL_DEFAULT||desc.MultiSampleType!=D3DMULTISAMPLE_NONE)break;
        stage="reflection_rt5";
        if(!copy(s.base+0x41eb1e8+5*20+4,native_reflection)||!native_reflection||
            !s.call([&]{return device->GetTexture(p.reflection_sampler,&sampled);})||!sampled||
            !s.call([&]{reflection_type=sampled->GetType();return reflection_type==D3DRTYPE_TEXTURE?D3D_OK:E_FAIL;})||
            !s.call([&]{return sampled->QueryInterface(__uuidof(IDirect3DTexture9),reinterpret_cast<void**>(&reflection_texture));})||!reflection_texture||
            !s.device_of(reflection_texture)||!s.call([&]{reflection_levels=reflection_texture->GetLevelCount();return D3D_OK;})||
            !s.call([&]{return reflection_texture->GetSurfaceLevel(0,&reflection_surface);})||!reflection_surface||
            !s.identity(reflection_surface,reinterpret_cast<IUnknown*>(native_reflection))||!s.device_of(reflection_surface)||
            !s.call([&]{return reflection_surface->GetDesc(&s.reflection_desc);}))break;
        const auto& rd=s.reflection_desc;
        stage="reflection_descriptor";
        if(reflection_levels!=1||rd.Width!=desc.Width||rd.Height!=desc.Height||rd.Type!=D3DRTYPE_SURFACE||
            rd.Usage!=D3DUSAGE_RENDERTARGET||rd.Pool!=D3DPOOL_DEFAULT||rd.MultiSampleType!=D3DMULTISAMPLE_NONE||
            (rd.Format!=D3DFMT_A8R8G8B8&&rd.Format!=D3DFMT_X8R8G8B8)||
            !copy(s.base+0x41eb1e8+5*20+4,repeated)||repeated!=native_reflection||
            !water_reflection_uv({n.viewport.x,n.viewport.y,n.viewport.width,n.viewport.height},
                                desc.Width,desc.Height,rd.Width,rd.Height,&s.uv))break;
        stage="reflection_filter";
        if(!s.call([&]{return device->GetSamplerState(p.reflection_sampler,D3DSAMP_MINFILTER,&min_filter);})||
            !s.call([&]{return device->GetSamplerState(p.reflection_sampler,D3DSAMP_MAGFILTER,&mag_filter);})||
            (min_filter!=D3DTEXF_POINT&&min_filter!=D3DTEXF_LINEAR)||
            (mag_filter!=D3DTEXF_POINT&&mag_filter!=D3DTEXF_LINEAR))break;
        s.reflection_texture=reinterpret_cast<std::uintptr_t>(sampled);
        s.reflection_surface=reinterpret_cast<std::uintptr_t>(reflection_surface);
        stage="gpu_constants";if(!s.call([&]{return device->GetVertexShaderConstantF(0,projection.data(),4);}))break;
        if(!s.call([&]{return device->GetPixelShaderConstantF(p.registers[0],actual.x.data(),1);})||
            !s.call([&]{return device->GetPixelShaderConstantF(p.registers[1],actual.y.data(),1);})||
            !s.call([&]{return device->GetPixelShaderConstantF(p.registers[2],actual.one.data(),1);}))break;
        stage="full_target_ray_proof";if(!water_vpos_gpu_constants(projection,actual,n.target_width,n.target_height,n.viewport,&corrected))break;
        VPosVector previous_affine{},previous_bounds{};
        stage="uv_constants";
        if(!s.call([&]{return device->GetPixelShaderConstantF(water_affine_register,previous_affine.data(),1);})||
            !s.call([&]{return device->GetPixelShaderConstantF(water_bounds_register,previous_bounds.data(),1);}))break;
        s.journal.saved={actual.x,actual.y,actual.one,previous_affine,previous_bounds};
        s.journal.written={corrected.x,corrected.y,corrected.one,s.uv.affine,s.uv.bounds};
        s.journal.saved_shader=reinterpret_cast<std::uintptr_t>(matched_pixel);
        s.journal.written_shader=reinterpret_cast<std::uintptr_t>(s.active_derivative);okay=s.current();
    }while(false);
    if(!okay&&s.probe&&s.rejections<16&&std::strncmp(stage,"reflection_",11)==0)
        trace("t5_water_reflection_binding_refused","stage=%s producer=%llu eye=%u program=%u sampler=%u expected_rt5=0x%x texture=0x%lx surface=0x%lx type=%u levels=%u size=%ux%u format=%u usage=%lu pool=%u min_filter=%lu mag_filter=%lu expected_size=%ux%u",
              stage,n.producer,n.eye,s.program,p.reflection_sampler,native_reflection,reinterpret_cast<std::uintptr_t>(sampled),
              reinterpret_cast<std::uintptr_t>(reflection_surface),unsigned(reflection_type),reflection_levels,
              s.reflection_desc.Width,s.reflection_desc.Height,unsigned(s.reflection_desc.Format),s.reflection_desc.Usage,
              unsigned(s.reflection_desc.Pool),min_filter,mag_filter,desc.Width,desc.Height);
    cleanup();if(!okay||!s.full_current()){s.reject(stage);return 0;}
    if(!s.enabled){s.record(false);return 0;}
    if(s.next_token==UINT64_MAX){s.faulted=true;return 0;}s.token=++s.next_token;s.open=true;
    if(!s.journal.apply([&](unsigned i,const VPosVector& value) noexcept {return s.set_constant(i,value);},
                        [&](std::uintptr_t shader) noexcept {return s.set_shader(shader);})){
        s.restore();s.open=false;return 0;
    }
    s.record(true);return s.token;
}
void WaterVPosCorrection::after(std::uint64_t token,HRESULT result) noexcept {
    Preserve preserve;if(!state_)return;auto& s=*state_;State::Operation operation(s);
    if(!s.open||token!=s.token||s.leases.load()!=1){s.faulted=true;return;}
    if(result==D3DERR_DEVICELOST||result==D3DERR_DEVICENOTRESET){s.faulted=true;s.open=false;return;}
    const bool restored=s.restore();s.open=false;++s.draws;
    if(s.reported_draw){bool verified=restored;std::uintptr_t actual_shader{};
        verified=verified&&s.get_shader(actual_shader)&&actual_shader==s.journal.saved_shader;
        for(unsigned i=0;i<5&&verified;++i){VPosVector actual{};verified=s.call([&]{return s.device->GetPixelShaderConstantF(s.constant_register(i),actual.data(),1);})&&WaterVPosJournal::same(actual,s.journal.saved[i]);}
        trace("t5_water_actual_vpos_restore_readback","producer=%llu eye=%u program=%u restored=%u actual_restored=%u pending=%u",s.eye.draw.owner.producer,s.eye.draw.owner.eye,s.program,restored,verified,s.journal.mask);s.reported_draw=false;}
    if(s.probe&&(s.draws<=8||s.draws%1200==0))trace("t5_water_actual_vpos_restored","draws=%llu producer=%llu eye=%u program=%u result=0x%08lx restored=%u pending=%u",s.draws,s.eye.draw.owner.producer,s.eye.draw.owner.eye,s.program,static_cast<unsigned long>(result),restored,s.journal.mask);
}
void WaterVPosCorrection::abandon(std::uint64_t) noexcept {if(state_){state_->open=false;state_->faulted=true;}}
bool WaterVPosCorrection::reconcile_normal(IDirect3DDevice9* device,bool usable) noexcept {
    Preserve preserve;if(!state_)return true;auto& s=*state_;
    if(s.open||s.leases.load()||(s.owner&&s.owner!=GetCurrentThreadId())||(s.device&&s.device!=device))return false;
    if(!s.journal.mask)return true;if(!usable)return false;
    State::Operation operation(s);s.witness=indexed_draw_reentry_serial();s.qualified=false;
    const bool okay=s.journal.reconcile(
        [&](unsigned i,VPosVector& value) noexcept {return s.call([&]{return device->GetPixelShaderConstantF(s.constant_register(i),value.data(),1);});},
        [&](unsigned i,const VPosVector& value) noexcept {return s.set_constant(i,value);},
        [&](std::uintptr_t& shader) noexcept {return s.get_shader(shader);},
        [&](std::uintptr_t shader) noexcept {return s.set_shader(shader);});
    if(okay)s.faulted=false;return okay;
}
bool WaterVPosCorrection::before_reset(IDirect3DDevice9* device) noexcept {
    Preserve preserve;if(!state_)return true;auto& s=*state_;
    if(s.open||s.leases.load()||(s.owner&&s.owner!=GetCurrentThreadId())||(s.device&&s.device!=device))return false;
    return s.journal.mask?true:retire();
}
void WaterVPosCorrection::after_reset(IDirect3DDevice9* device,HRESULT result) noexcept {if(result==D3D_OK&&reconcile_normal(device,true))retire();}
bool WaterVPosCorrection::retire_terminal(IDirect3DDevice9* device) noexcept {
    Preserve preserve;if(!state_)return true;auto& s=*state_;
    if(s.open||s.leases.load()||(s.owner&&s.owner!=GetCurrentThreadId())||(s.device&&s.device!=device))return false;
    if(s.journal.mask)trace("t5_water_actual_vpos_terminal_abandon","pending=%u terminal_native_quit=1",s.journal.mask);
    s.journal.mask=0;return retire();
}
bool WaterVPosCorrection::retire() noexcept {
    Preserve preserve;if(!state_)return true;auto& s=*state_;
    if(s.leases.load()||s.open||s.journal.mask||(s.owner&&GetCurrentThreadId()!=s.owner))return false;
    State::Operation operation(s);for(auto& p:s.pixels){drop(p.derivative);drop(p.object);p={};}for(auto& v:s.vertices){drop(v.object);v={};}
    drop(s.device_identity);s.pixel_count=0;s.active_pixel=nullptr;s.active_derivative=nullptr;s.device=nullptr;s.owner=0;s.faulted=false;s.qualified=false;return true;
}
bool WaterVPosCorrection::pending() const noexcept {return state_&&state_->journal.mask;}
bool WaterVPosCorrection::busy() const noexcept {return state_&&(state_->open||state_->leases.load());}
}
