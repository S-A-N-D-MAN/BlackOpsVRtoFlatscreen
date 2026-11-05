// SPDX-License-Identifier: GPL-3.0-only
#include "t5/bloom_shader_probe.hpp"
#include "loader/trace.hpp"
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>
#include <atomic>
#include <intrin.h>
namespace bovr::t5 {
namespace {
struct ProcessBudget {
    static std::atomic<std::uint64_t> bytes;
    bool debit(std::uint64_t n) noexcept {if(!n||n>bloom_shader_bytes)return false;auto used=bytes.load();
        do{if(n>bloom_process_bytes-used)return false;}while(!bytes.compare_exchange_weak(used,used+n));return true;}
    std::uint64_t used() const noexcept{return bytes.load();}
};
std::atomic<std::uint64_t> ProcessBudget::bytes{};
struct Error {DWORD v{};DWORD reserved[3]{};alignas(16) unsigned char fp[512]{};
    Error() noexcept {_fxsave(fp);v=GetLastError();}
    ~Error(){SetLastError(v);_fxrstor(fp);}};
template<class F>HRESULT seh(const F& f) noexcept {__try{return f();}__except(EXCEPTION_EXECUTE_HANDLER){return E_FAIL;}}
template<class F>HRESULT safe(const F& f) noexcept {try{return seh(f);}catch(...){return E_FAIL;}}
template<class T>struct Com {
    T* p{};Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
    ~Com(){reset();}void reset() noexcept {auto* old=p;p=nullptr;if(old)safe([&]{old->Release();return S_OK;});}
    T** put() noexcept {return &p;}
};
struct ShaderBytes {std::vector<std::uint8_t> bytes;std::string hash;};
std::string hex(const void* data,std::size_t bytes) {
    const auto* p=static_cast<const unsigned char*>(data);std::string s(bytes*2,'0');constexpr char d[]="0123456789abcdef";
    for(std::size_t i=0;i<bytes;++i){s[2*i]=d[p[i]>>4];s[2*i+1]=d[p[i]&15];}return s;
}
std::string ascii_name(const std::wstring& w) {std::string s;s.reserve(w.size());for(wchar_t c:w)s.push_back(static_cast<char>(c));return s;}
template<class T>std::string raw(const T& v){return hex(&v,sizeof(v));}
std::string native_schema() {
    std::ostringstream j;j<<"{";bool comma=false;
#define BOVR_WIRE_FIELD(field) if(comma)j<<',';comma=true;j<<"\"" #field "\":["<<offsetof(BloomDrawSnapshot,field)<<','<<sizeof(BloomDrawSnapshot::field)<<']'
    BOVR_WIRE_FIELD(buffer);BOVR_WIRE_FIELD(native_frame);BOVR_WIRE_FIELD(view_count);
    BOVR_WIRE_FIELD(selected);BOVR_WIRE_FIELD(eye);BOVR_WIRE_FIELD(role);BOVR_WIRE_FIELD(target);
    BOVR_WIRE_FIELD(view);BOVR_WIRE_FIELD(source_view);BOVR_WIRE_FIELD(source_mode);BOVR_WIRE_FIELD(selector);
    BOVR_WIRE_FIELD(width);BOVR_WIRE_FIELD(height);BOVR_WIRE_FIELD(material);BOVR_WIRE_FIELD(techset);
    BOVR_WIRE_FIELD(technique);BOVR_WIRE_FIELD(pass);BOVR_WIRE_FIELD(pass_index);BOVR_WIRE_FIELD(vertex_record);
    BOVR_WIRE_FIELD(pixel_record);BOVR_WIRE_FIELD(vertex_shader);BOVR_WIRE_FIELD(pixel_shader);BOVR_WIRE_FIELD(args_pointer);
    BOVR_WIRE_FIELD(pass_count);BOVR_WIRE_FIELD(technique_flags);BOVR_WIRE_FIELD(argument_counts);BOVR_WIRE_FIELD(custom_sampler_flags);
    BOVR_WIRE_FIELD(scene);BOVR_WIRE_FIELD(scene_viewport);BOVR_WIRE_FIELD(scissor);BOVR_WIRE_FIELD(viewport_constant);
    BOVR_WIRE_FIELD(native_target);BOVR_WIRE_FIELD(arguments);BOVR_WIRE_FIELD(samplers);BOVR_WIRE_FIELD(constants);
    BOVR_WIRE_FIELD(sampler_count);BOVR_WIRE_FIELD(constant_count);BOVR_WIRE_FIELD(names);
#undef BOVR_WIRE_FIELD
    j<<'}';return j.str();
}
std::string episode_schema() {
    std::ostringstream j;j<<"{";bool comma=false;
#define BOVR_EPISODE_FIELD(field) if(comma)j<<',';comma=true;j<<"\"" #field "\":["<<offsetof(BloomEpisodePlan,field)<<','<<sizeof(BloomEpisodePlan::field)<<']'
    BOVR_EPISODE_FIELD(version);BOVR_EPISODE_FIELD(buffer);BOVR_EPISODE_FIELD(native_frame);BOVR_EPISODE_FIELD(view_count);
    BOVR_EPISODE_FIELD(views);BOVR_EPISODE_FIELD(tint_bits);BOVR_EPISODE_FIELD(enabled);BOVR_EPISODE_FIELD(role_counts);
    BOVR_EPISODE_FIELD(expected_draws);BOVR_EPISODE_FIELD(fp_control);BOVR_EPISODE_FIELD(roles);
#undef BOVR_EPISODE_FIELD
    j<<'}';return j.str();
}
bool write_new(const std::wstring& path,const void* data,std::size_t bytes) noexcept {
    if(bytes>MAXDWORD)return false;HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;DWORD n{};const bool ok=WriteFile(f,data,static_cast<DWORD>(bytes),&n,nullptr)&&n==bytes&&FlushFileBuffers(f);CloseHandle(f);return ok;
}
struct BoundSampler {
    Com<IDirect3DBaseTexture9> texture;D3DSURFACE_DESC desc{};
    std::array<DWORD,8> state{};
};
struct Bindings {
    Com<IDirect3DVertexShader9> vertex;Com<IDirect3DPixelShader9> pixel;Com<IDirect3DSurface9> target;
    D3DSURFACE_DESC desc{};D3DVIEWPORT9 viewport{};RECT scissor{};DWORD scissor_enable{};
    std::array<BoundSampler,16> samplers{};
    std::array<std::array<float,4>,8> constants{};
    void release() noexcept {for(auto& s:samplers)s.texture.reset();target.reset();pixel.reset();vertex.reset();}
};
bool same_desc(const D3DSURFACE_DESC& a,const D3DSURFACE_DESC& b) noexcept {
    return a.Format==b.Format&&a.Type==b.Type&&a.Usage==b.Usage&&a.Pool==b.Pool&&a.MultiSampleType==b.MultiSampleType&&
        a.MultiSampleQuality==b.MultiSampleQuality&&a.Width==b.Width&&a.Height==b.Height;
}
struct Record {
    BloomDrawSnapshot native{};BloomDrawCall draw{};HRESULT native_result{};D3DSURFACE_DESC target{};D3DVIEWPORT9 viewport{};RECT scissor{};DWORD scissor_enable{};
    std::array<D3DSURFACE_DESC,16> texture_desc{};
    std::array<std::array<DWORD,8>,16> sampler_state{};
    std::array<std::array<float,4>,8> constants{};
    ShaderBytes vertex,pixel;
};
}
struct BloomShaderProbe::State {
    bool enabled{},active{},open{},failed{},faulted{},retire_requested{},releasing{};
    const char* rejection_reason="none";unsigned backend_logs{};
    unsigned operations{},attempt_count{},attempt{},pre_failures{},rejection_logs{};
    std::uint64_t next_token{},token{},epoch{},native_matches{},completed_draws{};
    std::wstring directory;ProcessBudget budget;BloomPairSet pair;
    BloomCaptureContract contract{BloomCaptureContract::selected_four};BloomEpisodeSet episode;
    BloomEpisodePlan episode_plan{};bool episode_planned{};
    std::uint64_t retire_serial{},observed_creation{};DWORD observed_creation_thread{};bool observed_extended{};
    IDirect3DDevice9* capability_device{};std::uint64_t capability_creation{};D3DDEVICE_CREATION_PARAMETERS capability{};bool capability_valid{};
    AuxiliaryPostfxKey key{};BloomShaderSource source{};IDirect3DDevice9* device{};
    BloomDrawSnapshot current_native{};BloomDrawCall current_draw{};Bindings bound;std::vector<Record> records;
    struct Operation {State& s;explicit Operation(State& v):s(v){++s.operations;}
        ~Operation(){if(s.operations==1&&s.retire_requested)s.release();--s.operations;}};
    bool same_owner() const noexcept {return !key.owner||key.owner==GetCurrentThreadId();}
    bool alive() const noexcept {return active&&!failed&&!faulted&&!retire_requested&&key.owner==GetCurrentThreadId()&&source.current&&
        safe([&]{return source.current(source.context)?S_OK:E_FAIL;})==S_OK;}
    bool full() const noexcept{return contract==BloomCaptureContract::complete_ldr;}
    bool read(BloomDrawSnapshot* out,BloomReadStatus* why) noexcept {
        if(!full())return source.read&&safe([&]{return source.read(source.context,out,why)?S_OK:E_FAIL;})==S_OK;
        if(!source.read_episode||!source.native_draw_return){if(why)*why=BloomReadStatus::source;return false;}
        BloomEpisodePlan p{};
        if(safe([&]{return source.read_episode(source.context,episode.cursor(),out,&p,why)?S_OK:E_FAIL;})!=S_OK)return false;
        if(!bloom_plan_valid(p)||!bloom_plan_matches(p,episode.cursor(),*out)||(episode_planned&&p!=episode_plan)){
            if(why)*why=BloomReadStatus::changed;return false;}
        if(!episode_planned){episode_plan=p;episode_planned=true;}return true;
    }
    bool unchanged(const BloomDrawSnapshot& expected) noexcept {
        if(!alive())return false;BloomDrawSnapshot n{};return read(&n,nullptr)&&n==expected&&alive();
    }
    void reject(const char* why) noexcept {if(!failed)rejection_reason=why;failed=true;pair.invalidate();episode.invalidate();if(rejection_logs<32&&++rejection_logs)
        trace("t5_bloom_shader_rejected","attempt=%u producer=%llu reason=%s bytes_used=%llu diagnostic_only=1",attempt,key.producer,why,budget.used());}
    void release() noexcept {
        if(releasing)return;releasing=true;bound.release();open=false;active=false;source={};device=nullptr;records.clear();attempt=0;retire_requested=false;releasing=false;
    }
    template<class F>bool call(const BloomDrawSnapshot& n,const F& f) noexcept {
        if(!unchanged(n))return false;const auto hr=safe(f);return SUCCEEDED(hr)&&unchanged(n);
    }
    bool device_identity(IUnknown* d,const BloomDrawSnapshot& n) noexcept {
        Com<IUnknown> x,y;
        return call(n,[&]{return device->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(x.put()));})&&x.p&&
            call(n,[&]{return d->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(y.put()));})&&y.p&&x.p==y.p;
    }
    bool query(const BloomDrawSnapshot& n,Bindings& b) noexcept {
        if(!call(n,[&]{return device->GetVertexShader(b.vertex.put());})||!b.vertex.p||reinterpret_cast<std::uintptr_t>(b.vertex.p)!=n.vertex_shader)return false;
        if(!call(n,[&]{return device->GetPixelShader(b.pixel.put());})||!b.pixel.p||reinterpret_cast<std::uintptr_t>(b.pixel.p)!=n.pixel_shader)return false;
        Com<IDirect3DDevice9> vd,pd;
        if(!call(n,[&]{return b.vertex.p->GetDevice(vd.put());})||!vd.p||!device_identity(vd.p,n)||
           !call(n,[&]{return b.pixel.p->GetDevice(pd.put());})||!pd.p||!device_identity(pd.p,n))return false;
        if(!call(n,[&]{return device->GetRenderTarget(0,b.target.put());})||!b.target.p||reinterpret_cast<std::uintptr_t>(b.target.p)!=n.native_target.color)return false;
        if(!call(n,[&]{return b.target.p->GetDesc(&b.desc);})||b.desc.Type!=D3DRTYPE_SURFACE||b.desc.Width!=n.width||b.desc.Height!=n.height)return false;
        Com<IDirect3DDevice9> td;if(!call(n,[&]{return b.target.p->GetDevice(td.put());})||!td.p||!device_identity(td.p,n))return false;
        if(!call(n,[&]{return device->GetViewport(&b.viewport);})||!call(n,[&]{return device->GetScissorRect(&b.scissor);})||
           !call(n,[&]{return device->GetRenderState(D3DRS_SCISSORTESTENABLE,&b.scissor_enable);}))return false;
        constexpr std::array<D3DSAMPLERSTATETYPE,8> states{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE,D3DSAMP_MAXMIPLEVEL};
        for(unsigned i=0;i<n.sampler_count;++i){const auto& a=n.samplers[i];auto& s=b.samplers[i];
            if(!call(n,[&]{return device->GetTexture(a.destination,s.texture.put());})||!s.texture.p||reinterpret_cast<std::uintptr_t>(s.texture.p)!=a.texture)return false;
            Com<IDirect3DTexture9> tex;
            if(!call(n,[&]{return s.texture.p->QueryInterface(IID_IDirect3DTexture9,reinterpret_cast<void**>(tex.put()));})||!tex.p||
               !call(n,[&]{return tex.p->GetLevelDesc(0,&s.desc);})||!s.desc.Width||!s.desc.Height||s.desc.Width>5120||s.desc.Height>4096)return false;
            for(unsigned j=0;j<states.size();++j)if(!call(n,[&]{return device->GetSamplerState(a.destination,states[j],&s.state[j]);}))return false;
        }
        for(unsigned i=0;i<n.constant_count;++i){const auto a=n.constants[i];auto& v=b.constants[i];
            if(!call(n,[&]{return a.type==3?device->GetVertexShaderConstantF(a.destination,v.data(),1):device->GetPixelShaderConstantF(a.destination,v.data(),1);})||
               std::memcmp(v.data(),n.viewport_constant.data(),16))return false;
        }
        return unchanged(n);
    }
    bool equal(const BloomDrawSnapshot& n,const Bindings& a,const Bindings& b) noexcept {
        if(a.vertex.p!=b.vertex.p||a.pixel.p!=b.pixel.p||a.target.p!=b.target.p||!same_desc(a.desc,b.desc)||
           std::memcmp(&a.viewport,&b.viewport,sizeof(a.viewport))||std::memcmp(&a.scissor,&b.scissor,sizeof(a.scissor))||a.scissor_enable!=b.scissor_enable)return false;
        for(unsigned i=0;i<n.sampler_count;++i)if(a.samplers[i].texture.p!=b.samplers[i].texture.p||!same_desc(a.samplers[i].desc,b.samplers[i].desc)||a.samplers[i].state!=b.samplers[i].state)return false;
        for(unsigned i=0;i<n.constant_count;++i)if(std::memcmp(a.constants[i].data(),b.constants[i].data(),16))return false;return true;
    }
    template<class T>bool bytecode(T* shader,const BloomDrawSnapshot& n,bool vertex,ShaderBytes& out) {
        UINT size{};if(!call(n,[&]{return shader->GetFunction(nullptr,&size);})||size<8||(size&3)||!budget.debit(size))return false;
        out.bytes.resize(size);UINT got=size;if(!call(n,[&]{return shader->GetFunction(out.bytes.data(),&got);})||got!=size)return false;
        if(!bloom_token_stream(reinterpret_cast<const std::uint32_t*>(out.bytes.data()),size/4,vertex))return false;
        std::array<unsigned char,32> digest{};if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,out.bytes.data(),got,digest.data(),static_cast<ULONG>(digest.size()))<0)return false;
        out.hash=raw(digest);return unchanged(n);
    }
    bool save(bool complete,bool staged) {
        if(directory.empty())return false;
        const auto prefix=L"pair-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(attempt);
        std::ostringstream j;j<<"{\"schema\":"<<static_cast<unsigned>(contract)<<",\"complete\":"<<(complete?"true":"false")<<",\"native_fields\":"<<native_schema()<<",\"diagnostic_failed\":"<<failed<<",\"reason\":\""<<rejection_reason<<"\",\"ordinary_staged\":"<<(staged?"true":"false")<<",\"attempt\":"<<attempt<<",\"producer\":"<<key.producer<<",\"generation\":"<<key.generation<<",\"xr_frame\":"<<key.xr_frame<<",\"creation_serial\":"<<observed_creation<<",\"creation_thread\":"<<observed_creation_thread<<",\"creation_extended\":"<<observed_extended<<",\"creation_parameters_hex\":\""<<raw(capability)<<"\",\"epoch\":"<<epoch<<",\"bytes_debited\":"<<budget.used();
        if(full())j<<",\"contract\":\"complete_ldr\",\"episode_plan_version\":1,\"episode_plan_bytes\":"<<sizeof(episode_plan)<<",\"episode_fields\":"<<episode_schema()<<",\"episode_plan_hex\":\""<<raw(episode_plan)<<"\"";
        j<<",\"records\":[";
        for(unsigned i=0;i<records.size();++i){const auto& r=records[i];const auto& n=r.native;if(i)j<<',';
            const auto stem=prefix+L"-draw-"+std::to_wstring(i);const auto vs=stem+L"-vs.bin",ps=stem+L"-ps.bin";
            if(!write_new(directory+L"\\"+vs,r.vertex.bytes.data(),r.vertex.bytes.size())||!write_new(directory+L"\\"+ps,r.pixel.bytes.data(),r.pixel.bytes.size()))return false;
            j<<"{";if(full())j<<"\"episode_plan_version\":1,";j<<"\"eye\":"<<n.eye<<",\"role\":"<<n.role<<",\"pass\":"<<n.pass_index<<",\"pass_count\":"<<n.pass_count<<",\"buffer\":"<<n.buffer<<",\"native_frame\":"<<n.native_frame<<",\"view_count\":"<<n.view_count<<",\"native_hresult\":"<<static_cast<unsigned long>(r.native_result)<<",\"draw_call_hex\":\""<<raw(r.draw)<<"\",\"view\":"<<n.view<<",\"outer_index\":"<<n.selected<<",\"target\":"<<n.target<<",\"width\":"<<n.width<<",\"height\":"<<n.height<<",\"material\":"<<n.material<<",\"technique\":"<<n.technique<<",\"native_snapshot_hex\":\""<<raw(n)<<"\",\"native_snapshot_bytes\":"<<sizeof(n)<<",\"target_desc_hex\":\""<<raw(r.target)<<"\",\"viewport_hex\":\""<<raw(r.viewport)<<"\",\"scissor_hex\":\""<<raw(r.scissor)<<"\",\"scissor_enabled\":"<<r.scissor_enable<<",\"texture_descs_hex\":\""<<raw(r.texture_desc)<<"\",\"sampler_states_hex\":\""<<raw(r.sampler_state)<<"\",\"device_constants_hex\":\""<<raw(r.constants)<<"\",\"vs_file\":\""<<ascii_name(vs)<<"\",\"vs_sha256\":\""<<r.vertex.hash<<"\",\"vs_bytes\":"<<r.vertex.bytes.size()<<",\"ps_file\":\""<<ascii_name(ps)<<"\",\"ps_sha256\":\""<<r.pixel.hash<<"\",\"ps_bytes\":"<<r.pixel.bytes.size()<<'}';
        }
        j<<"]}\n";const auto text=j.str();return write_new(directory+L"\\"+prefix+L".json",text.data(),text.size());
    }
};
BloomShaderProbe::BloomShaderProbe()=default;
BloomShaderProbe::~BloomShaderProbe()=default;
void BloomShaderProbe::configure(bool enabled,const std::wstring& directory,BloomCaptureContract contract) noexcept {
    if(!enabled&&!state_)return;
    if(state_&&!state_->same_owner())return;
    Error e;try{if(!state_)state_=std::make_unique<State>();auto& s=*state_;State::Operation op(s);if(s.active||s.operations!=1)return;
        const auto attr=GetFileAttributesW(directory.c_str());s.enabled=enabled&&directory.size()>3&&directory[1]==L':'&&(directory[2]==L'\\'||directory[2]==L'/')&&attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_DIRECTORY)&&!(attr&FILE_ATTRIBUTE_REPARSE_POINT);
        s.enabled=s.enabled&&(contract==BloomCaptureContract::selected_four||contract==BloomCaptureContract::complete_ldr);s.contract=contract;s.directory=s.enabled?directory:L"";if(enabled)trace("t5_bloom_shader_configured","enabled=%u max_attempts=2 process_byte_cap=%llu native_pointer_com_calls=0",s.enabled,bloom_process_bytes);
    }catch(...){if(state_)state_->enabled=false;}
}
void BloomShaderProbe::begin(const AuxiliaryPostfxKey& key,std::uint64_t epoch,IDirect3DDevice9* device,BloomShaderSource source) noexcept {
    if(!state_||!state_->same_owner())return;
    Error e;auto& s=*state_;State::Operation op(s);if(s.open||s.operations!=1){s.retire_requested=true;s.reject("nested_begin");return;}
    const auto retirement=s.retire_serial;s.release();if(retirement!=s.retire_serial||!s.enabled||s.attempt_count>=2||s.pre_failures>=32||!device||key.owner!=GetCurrentThreadId()||!key.producer||!key.generation||!key.xr_frame||!source.creation_serial)return;
    if(s.capability_device!=device||s.capability_creation!=source.creation_serial)s.capability_valid=false;
    s.observed_creation=source.creation_serial;s.observed_creation_thread=source.creation_thread;s.observed_extended=source.creation_extended;
    s.key=key;s.epoch=epoch;s.device=device;s.source=source;s.active=true;s.failed=false;s.faulted=false;s.rejection_reason="none";s.attempt=0;s.pair={};s.episode=BloomEpisodeSet{source.initial_eye};s.episode_plan={};s.episode_planned=false;
    if(s.backend_logs<6){++s.backend_logs;trace("t5_bloom_shader_backend","producer=%llu generation=%llu xr_frame=%llu creation=%llu attempted_pairs=%u current_native_role_required=1",key.producer,key.generation,key.xr_frame,source.creation_serial,s.attempt_count);}
}
std::uint64_t BloomShaderProbe::before(IDirect3DDevice9* device,const BloomDrawCall& draw) noexcept {
    if(!state_||!state_->same_owner())return 0;
    Error e;auto& s=*state_;State::Operation op(s);if(!s.enabled||!s.active||s.failed||s.pre_failures>=32)return 0;
    if(s.open||s.operations!=1){s.reject("nested_draw");return 0;}
    try {
        if(device!=s.device||!s.alive())return 0;BloomDrawSnapshot n{};
        BloomReadStatus why{};
        if(!s.read(&n,&why)){
            if(why!=BloomReadStatus::unrelated&&s.rejection_logs<32&&++s.rejection_logs)trace("t5_bloom_shader_native_rejected","producer=%llu attempt=%u stage=%u no_optional_getters=1",s.key.producer,s.attempt,static_cast<unsigned>(why));
            if(why!=BloomReadStatus::unrelated){if(s.full()){++s.pre_failures;s.reject("selected_native_changed");}else if(s.attempt)s.reject("selected_native_changed");}return 0;
        }++s.native_matches;
        if(s.full()&&(draw.caller!=s.source.native_draw_return||draw.type!=D3DPT_TRIANGLELIST||draw.base_vertex||draw.min_vertex||draw.num_vertices!=4||draw.primitive_count!=2)){
            ++s.pre_failures;s.reject("native_draw_contract");return 0;}
        if(s.full()&&s.episode.cursor().finished)return 0;
        if(!s.capability_valid) {
            D3DDEVICE_CREATION_PARAMETERS cp{};
            if(!s.call(n,[&]{return device->GetCreationParameters(&cp);})||(cp.BehaviorFlags&D3DCREATE_PUREDEVICE)){++s.pre_failures;s.reject("creation_or_pure_device");return 0;}
            s.capability=cp;s.capability_device=device;s.capability_creation=s.source.creation_serial;s.capability_valid=true;
        }
        s.bound.release();if(!s.query(n,s.bound)||!s.unchanged(n)){++s.pre_failures;s.bound.release();s.reject("pre_tuple_or_binding");return 0;}
        if(!s.attempt){s.attempt=++s.attempt_count;trace("t5_bloom_shader_attempt","attempt=%u producer=%llu generation=%llu xr_frame=%llu epoch=%llu claim=first_fully_admitted_draw",s.attempt,s.key.producer,s.key.generation,s.key.xr_frame,s.epoch);}
        s.current_native=n;s.current_draw=draw;s.open=true;s.token=++s.next_token;if(!s.token){s.reject("token_overflow");s.open=false;return 0;}return s.token;
    }catch(...){s.reject("before_exception");s.bound.release();s.open=false;return 0;}
}
void BloomShaderProbe::after(std::uint64_t token,HRESULT native) noexcept {
    if(!state_||!state_->same_owner())return;
    Error e;auto& s=*state_;State::Operation op(s);if(!s.open||token!=s.token){if(token)s.reject("completion_token");return;}s.open=false;
    try {
        if(FAILED(native)){s.reject("native_hresult");s.bound.release();return;}
        Bindings after;const auto& n=s.current_native;
        if(!s.query(n,after)||!s.equal(n,s.bound,after)){s.reject("post_tuple_or_binding");s.bound.release();return;}
        Record r{};r.native=n;r.draw=s.current_draw;r.native_result=native;r.target=s.bound.desc;r.viewport=s.bound.viewport;r.scissor=s.bound.scissor;r.scissor_enable=s.bound.scissor_enable;r.constants=s.bound.constants;
        for(unsigned i=0;i<n.sampler_count;++i){r.texture_desc[i]=s.bound.samplers[i].desc;r.sampler_state[i]=s.bound.samplers[i].state;}
        if(!s.bytecode(s.bound.vertex.p,n,true,r.vertex)||!s.bytecode(s.bound.pixel.p,n,false,r.pixel)){s.reject("bytecode_or_process_budget");s.bound.release();return;}
        Bindings verified;if(!s.query(n,verified)||!s.equal(n,s.bound,verified)||!s.unchanged(n)){s.reject("retrieval_changed_binding");s.bound.release();return;}
        if(!(s.full()?s.episode.accept(s.episode_plan,n):s.pair.add(n))||s.records.size()>=32){s.reject("pair_order_or_duplicate");s.bound.release();return;}
        s.records.push_back(std::move(r));++s.completed_draws;s.bound.release();
    }catch(...){s.reject("after_exception");s.bound.release();}
}
void BloomShaderProbe::abandon(std::uint64_t token) noexcept {
    if(!state_||!state_->same_owner())return;
    auto& s=*state_;if(token&&token!=s.token)return;s.faulted=true;s.failed=true;s.active=false;s.open=false;s.pair.invalidate();s.episode.invalidate();s.source={};
    // An unrelated later callback must not perform automatic cleanup after a
    // native fault. Only explicit normal finish/begin/owner retirement releases.
    s.retire_requested=false;
}
void BloomShaderProbe::finish(bool staged) noexcept {
    if(!state_||!state_->same_owner())return;
    Error e;auto& s=*state_;State::Operation op(s);try {
        if(s.attempt){const bool complete=staged&&s.alive()&&!s.open&&(s.full()?s.episode.complete():s.pair.complete());const bool written=s.save(complete,staged);
            trace("t5_bloom_shader_complete","attempt=%u producer=%llu ordinary_staged=%u pair_complete=%u output_complete=%u draws=%zu bytes_debited=%llu",s.attempt,s.key.producer,staged,complete,written,s.records.size(),s.budget.used());}
    }catch(...){s.reject("output_exception");}s.release();
}
void BloomShaderProbe::retire() noexcept {
    if(!state_||!state_->same_owner())return;
    Error e;auto& s=*state_;State::Operation op(s);++s.retire_serial;s.retire_requested=true;s.active=false;s.failed=true;
}
std::uint64_t BloomShaderProbe::bytes_used() const noexcept{return state_?state_->budget.used():0;}
unsigned BloomShaderProbe::attempts() const noexcept{return state_?state_->attempt_count:0;}
bool BloomShaderProbe::configured() const noexcept{return state_&&state_->enabled;}
}
