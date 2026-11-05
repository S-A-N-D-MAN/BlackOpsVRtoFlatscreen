// SPDX-License-Identifier: GPL-3.0-only
#include "t5/water_reflection_correction.hpp"
#include "t5/water_reflection_program.hpp"
#include "t5/vpos_eye_scope.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/trace.hpp"
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstring>
#include <vector>
namespace bovr::t5 {
namespace {
struct Preserve {DWORD error;alignas(16) unsigned char fp[512];Preserve() noexcept:error(GetLastError()){_fxsave(fp);}~Preserve(){_fxrstor(fp);SetLastError(error);}};
template<class T>bool copy(std::uintptr_t address,T& out) noexcept {
    if(address<0x10000||address>UINT32_MAX-sizeof(T))return false;
    __try{std::memcpy(&out,reinterpret_cast<const void*>(address),sizeof(T));return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class F>HRESULT seh(const F& f) noexcept {__try{return f();}__except(EXCEPTION_EXECUTE_HANDLER){return E_FAIL;}}
template<class F>HRESULT contained(const F& f) noexcept {try{return seh(f);}catch(...){return E_FAIL;}}
template<class T>void drop(T*& p) noexcept {auto* old=p;p=nullptr;if(old)contained([&]{old->Release();return S_OK;});}
struct Pass {
    std::uint32_t material{},technique{},pass{},index{},pixel_record{},pixel{},args{};
    unsigned program{};std::array<unsigned char,3> counts{};
    friend bool operator==(const Pass&,const Pass&)=default;
};
std::atomic<unsigned> candidate_reports{};
struct CandidateSeen {std::uint32_t material{},pass{};unsigned target{};};
std::array<CandidateSeen,12> candidate_seen{}; // observer's exact owner thread only
bool candidate_name(std::uint32_t address,std::array<char,128>& text) noexcept {
    for(unsigned i=0;i<text.size();++i){if(!copy(address+i,text[i]))return false;if(!text[i])return true;}return false;
}
void candidate_snapshot(std::uintptr_t base) noexcept {
    if(candidate_reports.load(std::memory_order_relaxed)>=12)return;
    std::uint8_t target{},type{};std::uint32_t pass{},record{},name{},material{},material_name{};
    std::array<char,128> text{},label{};
    if(!copy(base+0x417ff28,target)||!copy(base+0x417eed0,pass)||!copy(pass+8,record)||
       !copy(record,name)||!candidate_name(name,text)||!copy(base+0x417eec4,material)||
       !copy(material,material_name)||!candidate_name(material_name,label)||!std::strstr(label.data(),"water")||
       !copy(base+0x417eec8,type))return;
    for(const auto& old:candidate_seen)if(old.material==material&&old.pass==pass&&old.target==target)return;
    const auto sample=candidate_reports.fetch_add(1,std::memory_order_relaxed);if(sample>=12)return;
    candidate_seen[sample]={material,pass,target};
    bool known=false;for(const auto& program:water_reflection_programs)known=known||program.name==text.data();
    trace("t5_water_candidate_snapshot","sample=%u target=%u material=0x%x material_name=%s technique_type=%u pass=0x%x pixel_record=0x%x name=%s exact_name=%u correction_admission_unchanged=1",sample+1,unsigned(target),material,label.data(),unsigned(type),pass,record,text.data(),known);
}
// Cheap prefilter only. Native metadata never proves a GPU shader identity.
// Unrelated indexed draws make no COM getter, hash or allocation calls here.
bool candidate(std::uintptr_t base,Pass& out) noexcept {
    std::uint8_t target{};std::uint32_t record{},name{};std::array<char,8> prefix{};
    if(!copy(base+0x417ff28,target)||target!=3||!copy(base+0x417eed0,out.pass)||
       !copy(out.pass+8,record)||!copy(record,name)||!copy(name,prefix)||
       std::memcmp(prefix.data(),"sw_water",8))return false;
    std::array<char,80> text{};bool ended=false;
    for(unsigned i=0;i<text.size();++i){if(!copy(name+i,text[i]))return false;if(!text[i]){ended=true;break;}}
    if(!ended)return false;out.program=unsigned(water_reflection_programs.size());
    for(unsigned i=0;i<water_reflection_programs.size();++i)if(water_reflection_programs[i].name==text.data()){out.program=i;break;}
    if(out.program==water_reflection_programs.size())return false;
    out.pixel_record=record;
    return copy(record+4,out.pixel)&&out.pixel&&copy(base+0x417eec4,out.material)&&
        copy(base+0x417eecc,out.technique)&&copy(base+0x417eed4,out.index)&&
        copy(out.pass+12,out.counts)&&copy(out.pass+16,out.args);
}
bool pass_valid(const VPosEyeContext& eye,const Pass& p,std::uint32_t& texture) noexcept {
    if(eye.state!=eye.base+0x417ee00||p.index>3||p.pass!=p.technique+8+20*p.index)return false;
    std::uint32_t techset{},technique{};std::uint8_t type{};std::uint16_t count{};
    if(!copy(p.material+0xb0,techset)||!copy(eye.state+0xc8,type)||type>=130||
       !copy(techset+8+4*type,technique)||technique!=p.technique||!copy(p.technique+6,count)||!count||count>4||p.index>=count)return false;
    const unsigned n=p.counts[0]+p.counts[1]+p.counts[2];if(!n||n>64)return false;
    const auto& program=water_reflection_programs[p.program];unsigned found{},vpos{};
    for(unsigned i=0;i<n;++i){std::array<std::uint32_t,2> words{};if(!copy(p.args+8*i,words))return false;
        const unsigned kind=words[0]&65535,destination=words[0]>>16;
        if(kind==4&&words[1]==9){if(destination!=program.sampler)return false;++found;}
        if(kind==5)for(unsigned k=0;k<3;++k)if((words[1]&65535)==22+k){
            if(words[1]!=(0x01000000u|(22u+k))||destination!=program.vpos_registers[k]||(vpos&(1u<<k)))return false;vpos|=1u<<k;}
    }
    std::uint32_t image{},control{};
    return found==1&&vpos==7&&copy(eye.source+0x1528+8*9,control)&&!(control&0x100)&&
        copy(eye.source+0x1450+4*9,image)&&copy(image,texture)&&texture;
}
bool same_bits(const std::array<float,4>& a,const std::array<float,4>& b) noexcept {return std::memcmp(a.data(),b.data(),16)==0;}
struct UnderwaterGpu {
    std::array<float,28> vs{},ps{};std::array<float,8> fog20{},ps20{};std::array<float,16> fog64{};
    std::uintptr_t pixel{},vertex{},cube{},pixel_device{},vertex_device{},target{};
    std::uint32_t expected_vertex{},expected_target{};UINT pixel_bytes{},vertex_bytes{};
    D3DVIEWPORT9 viewport{};unsigned texture_type{},matches{};bool duplicate{};
    std::array<HRESULT,17> results{};std::array<char,65> pixel_sha{},vertex_sha{};
    bool operator==(const UnderwaterGpu& other) const noexcept {
        return vs==other.vs&&ps==other.ps&&ps20==other.ps20&&fog20==other.fog20&&fog64==other.fog64&&
            pixel==other.pixel&&vertex==other.vertex&&cube==other.cube&&pixel_device==other.pixel_device&&
            vertex_device==other.vertex_device&&target==other.target&&expected_vertex==other.expected_vertex&&
            expected_target==other.expected_target&&pixel_bytes==other.pixel_bytes&&vertex_bytes==other.vertex_bytes&&
            std::memcmp(&viewport,&other.viewport,sizeof(viewport))==0&&texture_type==other.texture_type&&matches==other.matches&&results==other.results&&pixel_sha==other.pixel_sha&&vertex_sha==other.vertex_sha;
    }
};
}
struct WaterReflectionCorrection::State {
    std::uintptr_t base{};bool configured{},uv_enabled{},vpos_enabled{},probe{},open{},faulted{};
    std::atomic<unsigned> leases{};DWORD owner{};IDirect3DDevice9* device{};std::uint64_t witness{},next_token{},token{},draws{},queries{};
    VPosEyeContext eye{};Pass pass{};unsigned pending_mask{},cache_slot{};
    std::array<std::array<float,4>,5> saved{};WaterReflectionUv uv{};
    struct Cached {IDirect3DPixelShader9* original{};IDirect3DPixelShader9* replacement{};unsigned program{};bool rejected{};};
    std::array<Cached,16> cache{};std::array<std::array<unsigned,4>,2> logs{};unsigned rejection_logs{};
    bool underwater_open{};WaterDrawOwnerSnapshot underwater_native{};UnderwaterGpu underwater_gpu{};
    Pass underwater_selected{};std::array<unsigned,2> underwater_reports{},underwater_attempts{};
    struct UnderwaterSeen {std::uintptr_t pixel{},vertex{};unsigned eyes{};bool water{};
        std::array<unsigned,2> samples{};std::array<std::uint64_t,2> last_producer{};};
    std::array<UnderwaterSeen,128> underwater_seen{};
    struct Operation {State& s;explicit Operation(State& value):s(value){s.leases.fetch_add(1);}~Operation(){s.leases.fetch_sub(1);}};
    bool current() noexcept {
        if(faulted||GetCurrentThreadId()!=owner||indexed_draw_reentry_serial()!=witness||witness==indexed_draw_reentry_exhausted)return false;
        VPosEyeContext now{};Pass selected{};
        return read_vpos_eye_context(&now)&&now==eye&&candidate(base,selected)&&selected==pass;
    }
    template<class F>bool call(const F& f) noexcept {
        if(!current())return false;++queries;const auto hr=contained(f);return hr==D3D_OK&&current();
    }
    void reject(const char* why) noexcept {if(probe&&rejection_logs++<12)trace("t5_water_shader_rejected","reason=%s eye=%u program=%u producer=%llu pending=%u",why,eye.eye,pass.program,eye.producer,pending_mask);}
    bool underwater_read(IDirect3DDevice9* gpu,const Pass& selected,const WaterDrawOwnerSnapshot& native,UnderwaterGpu& out,bool new_only=false) noexcept {
        const auto serial=indexed_draw_reentry_serial();
        const auto valid=[&]() noexcept {WaterDrawOwnerSnapshot n{};return GetCurrentThreadId()==owner&&gpu==device&&
            serial!=indexed_draw_reentry_exhausted&&indexed_draw_reentry_serial()==serial&&
            read_water_draw_owner(&n)&&n==native;};
        // Diagnostics retain mismatched state for diagnosis. This function has
        // no setters and does not authorize the production correction.
        const auto invoke=[&](unsigned stage,const auto& f) noexcept {
            const auto hr=valid()?contained(f):E_ABORT;out.results[stage]=hr;return hr==D3D_OK&&valid();};
        out.results.fill(E_ABORT);
        IDirect3DPixelShader9* ps{};IDirect3DVertexShader9* vs{};IDirect3DSurface9* target{};IDirect3DBaseTexture9* cube{};
        IDirect3DDevice9* shader_device{};std::uint32_t vertex_record{};
        if(!valid())return false;
        copy(selected.pass+4,vertex_record);copy(vertex_record+4,out.expected_vertex);
        copy(base+0x41eb1e8+3*20+4,out.expected_target);
        invoke(0,[&]{return gpu->GetPixelShader(&ps);});
        invoke(1,[&]{return gpu->GetVertexShader(&vs);});
        out.pixel=reinterpret_cast<std::uintptr_t>(ps);out.vertex=reinterpret_cast<std::uintptr_t>(vs);
        if(new_only){
            bool room=false;for(const auto& seen:underwater_seen){
                room=room||!seen.eyes;
                if(seen.eyes&&seen.pixel==out.pixel&&seen.vertex==out.vertex){
                    if(seen.eyes&(1u<<native.owner.eye))out.duplicate=!(seen.water&&seen.samples[native.owner.eye]<3&&
                        native.owner.producer>=seen.last_producer[native.owner.eye]+120);
                    room=true;break;
                }
            }
            if(out.duplicate||!room){out.duplicate=true;drop(vs);drop(ps);return valid();}
        }
        // GetFunction's size argument does not safely bound a too-small
        // destination. Query first, then allocate for the actual owned object.
        const auto fingerprint=[&](auto* shader,unsigned size_stage,unsigned data_stage,UINT& size,std::array<char,65>& sha,const char* expected,unsigned bit) {
            std::vector<std::uint32_t> code;
            const bool captured=water_shader_capture([&](void* destination,unsigned* bytes) noexcept {
                if(destination)trace("t5_underwater_bytecode_read","producer=%llu eye=%u shader_stage=%u bytes=%u bounded_allocation=1",native.owner.producer,native.owner.eye,data_stage,*bytes);
                return invoke(destination?data_stage:size_stage,[&]{return shader->GetFunction(destination,bytes);});
            },&code,&size);
            if(captured&&water_shader_sha256(code,&sha)&&std::string_view(sha.data())==expected)out.matches|=bit;
        };
        if(ps){
            invoke(2,[&]{return ps->GetDevice(&shader_device);});out.pixel_device=reinterpret_cast<std::uintptr_t>(shader_device);drop(shader_device);
            fingerprint(ps,14,4,out.pixel_bytes,out.pixel_sha,"8a87ac3e2fea17456f5783db48ffb44821e954debe4dd01a9edf9b66e2d6172f",1);
        }
        if(vs){
            invoke(3,[&]{return vs->GetDevice(&shader_device);});out.vertex_device=reinterpret_cast<std::uintptr_t>(shader_device);drop(shader_device);
            fingerprint(vs,15,5,out.vertex_bytes,out.vertex_sha,"16880398eebaa1ad45ede03d4ed4bde2b1f041c9a7c9b11cb16abe6c3af200e3",2);
        }
        invoke(6,[&]{return gpu->GetViewport(&out.viewport);});
        invoke(7,[&]{return gpu->GetRenderTarget(0,&target);});out.target=reinterpret_cast<std::uintptr_t>(target);
        bool water=std::string_view(out.pixel_sha.data())=="8a87ac3e2fea17456f5783db48ffb44821e954debe4dd01a9edf9b66e2d6172f";
        for(const auto& program:water_reflection_programs)water=water||std::string_view(out.pixel_sha.data())==program.sha256;
        water=water||std::string_view(out.pixel_sha.data())=="c3c7a1fc757395f7f2a87eda9fd2a5bf2e926e1c48118c9be00b9b18a5650dd2"||
            std::string_view(out.pixel_sha.data())=="55a80dd475df6804affd31e424782e457e2a156a84358c254eb116beb603c760";
        const bool hdr_reference=std::string_view(out.pixel_sha.data())=="7f93618c471da034193e58a5df00c64689c4cc68bbdc6b5c2f6ba482f555c3d9";
        if(water||hdr_reference){
            invoke(8,[&]{return gpu->GetVertexShaderConstantF(0,out.vs.data(),7);});
            invoke(9,[&]{return gpu->GetPixelShaderConstantF(5,out.ps.data(),7);});
            invoke(10,[&]{return gpu->GetTexture(0,&cube);});out.cube=reinterpret_cast<std::uintptr_t>(cube);
            if(cube)invoke(11,[&]{out.texture_type=static_cast<unsigned>(cube->GetType());return D3D_OK;});
            invoke(12,[&]{return gpu->GetVertexShaderConstantF(20,out.fog20.data(),2);});
            invoke(13,[&]{return gpu->GetVertexShaderConstantF(64,out.fog64.data(),4);});
            invoke(16,[&]{return gpu->GetPixelShaderConstantF(20,out.ps20.data(),2);});
            out.matches|=128;if(hdr_reference)out.matches|=256;
        }
        if(out.pixel==selected.pixel&&out.vertex==out.expected_vertex)out.matches|=4;
        if(out.pixel_device==reinterpret_cast<std::uintptr_t>(gpu)&&out.vertex_device==reinterpret_cast<std::uintptr_t>(gpu))out.matches|=8;
        if(out.viewport.X==unsigned(native.owner.viewport.x)&&out.viewport.Y==unsigned(native.owner.viewport.y)&&
           out.viewport.Width==unsigned(native.owner.viewport.width)&&out.viewport.Height==unsigned(native.owner.viewport.height)&&
           out.viewport.MinZ==0.f&&out.viewport.MaxZ==1.f)out.matches|=16;
        if(out.target&&out.target==out.expected_target)out.matches|=32;
        if(out.texture_type==D3DRTYPE_CUBETEXTURE)out.matches|=64;
        drop(shader_device);drop(cube);drop(target);drop(vs);drop(ps);return valid();
    }
    void underwater_record(const char* phase,const WaterDrawOwnerSnapshot& n,const UnderwaterGpu& g) noexcept {
        if(g.matches&128){
        trace("t5_underwater_draw","phase=%s detail_role=%s producer=%llu frame=%u eye=%u view=0x%lx material=0x%x pass=0x%x ps=0x%lx vs=0x%lx cube=0x%lx diagnostic_binding_matches=%u native_context_current=0 selected_origin=%.9g,%.9g,%.9g gpu_world_translation=%.9g,%.9g,%.9g world_translation_residual=%.9g,%.9g,%.9g native_writes=0 gpu_writes=0",
            phase,(g.matches&256)?"world_hdr_reference":"water_shader",n.owner.producer,n.owner.native_frame,n.owner.eye,static_cast<unsigned long>(n.owner.view),underwater_selected.material,underwater_selected.pass,
            static_cast<unsigned long>(g.pixel),static_cast<unsigned long>(g.vertex),static_cast<unsigned long>(g.cube),g.matches,
            n.selected_view_origin[0],n.selected_view_origin[1],n.selected_view_origin[2],g.vs[19],g.vs[23],g.vs[27],
            g.vs[19]+n.selected_view_origin[0],g.vs[23]+n.selected_view_origin[1],g.vs[27]+n.selected_view_origin[2]);
        }
        trace("t5_underwater_gpu_identity","phase=%s producer=%llu eye=%u pixel=0x%lx vertex=0x%lx native_metadata_current=0 expected_ps=0x%x expected_vs=0x%x pixel_bytes=%u vertex_bytes=%u pixel_sha=%s vertex_sha=%s expected_device=0x%lx pixel_device=0x%lx vertex_device=0x%lx expected_target=0x%x target=0x%lx viewport=%lu,%lu,%lu,%lu depth=%.9g,%.9g texture_type=%u matches=%u",
            phase,n.owner.producer,n.owner.eye,static_cast<unsigned long>(g.pixel),static_cast<unsigned long>(g.vertex),underwater_selected.pixel,g.expected_vertex,g.pixel_bytes,g.vertex_bytes,g.pixel_sha.data(),g.vertex_sha.data(),
            static_cast<unsigned long>(n.owner.device),static_cast<unsigned long>(g.pixel_device),static_cast<unsigned long>(g.vertex_device),g.expected_target,static_cast<unsigned long>(g.target),
            g.viewport.X,g.viewport.Y,g.viewport.Width,g.viewport.Height,g.viewport.MinZ,g.viewport.MaxZ,g.texture_type,g.matches);
        if(!(g.matches&128))return;
        constexpr std::array<const char*,17> stages{"pixel_shader","vertex_shader","pixel_device","vertex_device","pixel_bytecode","vertex_bytecode","viewport","target","vertex_constants","pixel_constants","cube_texture","cube_type","fog20","fog64","pixel_bytecode_size","vertex_bytecode_size","pixel20"};
        for(unsigned i=0;i<stages.size();++i)if(g.results[i]!=D3D_OK)trace("t5_underwater_getter_result","phase=%s producer=%llu eye=%u stage=%s hresult=0x%08lx",phase,n.owner.producer,n.owner.eye,stages[i],static_cast<unsigned long>(g.results[i]));
        for(unsigned row=0;row<2;++row)trace("t5_underwater_gpu_pixel_constant","phase=%s producer=%llu eye=%u ps_register=%u value=%.9g,%.9g,%.9g,%.9g",phase,n.owner.producer,n.owner.eye,20+row,g.ps20[row*4],g.ps20[row*4+1],g.ps20[row*4+2],g.ps20[row*4+3]);
        for(unsigned row=0;row<6;++row){const auto* v=row<2?g.fog20.data()+4*row:g.fog64.data()+4*(row-2);
            trace("t5_underwater_fog","phase=%s producer=%llu eye=%u vs_register=%u value=%.9g,%.9g,%.9g,%.9g",phase,n.owner.producer,n.owner.eye,row<2?20+row:62+row,v[0],v[1],v[2],v[3]);}
        for(unsigned row=0;row<7;++row)trace("t5_underwater_gpu_constants","phase=%s producer=%llu eye=%u vs_register=%u vs=%.9g,%.9g,%.9g,%.9g ps_register=%u ps=%.9g,%.9g,%.9g,%.9g",
            phase,n.owner.producer,n.owner.eye,row,g.vs[row*4],g.vs[row*4+1],g.vs[row*4+2],g.vs[row*4+3],row+5,g.ps[row*4],g.ps[row*4+1],g.ps[row*4+2],g.ps[row*4+3]);
    }
    std::uint64_t underwater_begin(IDirect3DDevice9* gpu,const Pass& selected) noexcept {
        if(underwater_attempts[0]>=300000&&underwater_attempts[1]>=300000)return 0;
        Operation operation(*this);WaterDrawOwnerSnapshot native{};UnderwaterGpu values{};
        if(!read_water_draw_owner(&native))return 0;
        if(native.owner.base!=base||native.owner.device!=reinterpret_cast<std::uintptr_t>(gpu)){reject("underwater_owner_identity");return 0;}
        if(underwater_attempts[native.owner.eye]>=300000)return 0;
        ++underwater_attempts[native.owner.eye];
        if((owner&&owner!=GetCurrentThreadId())||(device&&device!=gpu))return 0;owner=GetCurrentThreadId();device=gpu;
        if(!underwater_read(gpu,selected,native,values,true)){reject("underwater_getter_witness");return 0;}
        if(values.duplicate)return 0;
        for(auto& seen:underwater_seen)if(!seen.eyes||(seen.pixel==values.pixel&&seen.vertex==values.vertex)){
            seen.pixel=values.pixel;seen.vertex=values.vertex;seen.eyes|=1u<<native.owner.eye;seen.water=(values.matches&128)!=0;
            ++seen.samples[native.owner.eye];seen.last_producer[native.owner.eye]=native.owner.producer;break;}
        // Before-only passive capture. Returning no token lets the existing
        // native/bloom/hand callback chain run unchanged after observation.
        underwater_selected=selected;++underwater_reports[native.owner.eye];underwater_record("before",native,values);
        return 0;
    }
    bool owner_of(IDirect3DPixelShader9* object) noexcept {
        IDirect3DDevice9* returned{};const bool okay=call([&]{return object->GetDevice(&returned);})&&returned==device;drop(returned);return okay&&current();
    }
    bool acquire_shader(IDirect3DPixelShader9*& bound) noexcept {
        for(unsigned i=0;i<cache.size();++i)if(cache[i].original==bound&&cache[i].program==pass.program){cache_slot=i;return !cache[i].rejected&&(!uv_enabled||cache[i].replacement);}
        unsigned i=0;while(i<cache.size()&&cache[i].original)++i;if(i==cache.size())return false;
        const auto reject_object=[&]() noexcept {if(current()){cache[i]={bound,nullptr,pass.program,true};bound=nullptr;}return false;};
        UINT bytes{};if(!call([&]{return bound->GetFunction(nullptr,&bytes);}))return false;
        if(bytes!=water_reflection_programs[pass.program].bytes)return reject_object();
        std::vector<std::uint32_t> original,derivative;
        try{original.resize(bytes/4);}catch(...){return false;}
        UINT copied=bytes;
        if(!call([&]{return bound->GetFunction(original.data(),&copied);})||copied!=bytes)return false;
        if(!water_reflection_make_derivative(pass.program,original,&derivative))return reject_object();
        IDirect3DPixelShader9* replacement{};
        if(uv_enabled&&(!call([&]{return device->CreatePixelShader(reinterpret_cast<const DWORD*>(derivative.data()),&replacement);})||!replacement||!owner_of(replacement))){drop(replacement);return reject_object();}
        if(!current()){drop(replacement);return false;}
        cache[i]={bound,replacement,pass.program,false};bound=nullptr;cache_slot=i;return true;
    }
    bool restore() noexcept {
        if(!pending_mask)return true;
        if(!current()){faulted=true;return false;}
        // Reverse journal. Every bit is set BEFORE its corresponding setter.
        if(pending_mask&32){if(!call([&]{return device->SetPixelShader(cache[cache_slot].original);}))return fail_restore();pending_mask&=~32u;}
        for(int i=4;i>=0;--i)if(pending_mask&(1u<<i)){
            const unsigned reg=i<3?water_reflection_programs[pass.program].vpos_registers[i]:water_affine_register+unsigned(i)-3;
            if(!call([&]{return device->SetPixelShaderConstantF(reg,saved[i].data(),1);}))return fail_restore();pending_mask&=~(1u<<i);
        }
        return true;
    }
    bool fail_restore() noexcept {faulted=true;reject("restore_failed");return false;}
    void record(bool applied,const D3DSURFACE_DESC& target,const D3DSURFACE_DESC& texture) noexcept {
        if(!probe||eye.eye>1||logs[eye.eye][pass.program]++>=4)return;
        const auto& p=water_reflection_programs[pass.program];
        trace("t5_water_shader_witness","producer=%llu generation=%llu xr_frame=%llu native_frame=%u eye=%u program=%u original_sha256=%s source=0x%lx state=0x%lx view=0x%lx material=0x%x pass=0x%x sampler=%u native_texture=1 target=%ux%u target_format=%u texture=%ux%u texture_format=%u viewport=%d,%d,%d,%d vpos_exact_native=1 vpos_registers=%u,%u,%u original_x=%.9g,%.9g,%.9g,%.9g original_y=%.9g,%.9g,%.9g,%.9g original_one=%.9g,%.9g,%.9g,%.9g corrected_x=%.9g,%.9g,%.9g,%.9g corrected_y=%.9g,%.9g,%.9g,%.9g corrected_one=%.9g,%.9g,%.9g,%.9g affine=%.9g,%.9g,%.9g,%.9g bounds=%.9g,%.9g,%.9g,%.9g uv_enabled=%u vpos_enabled=%u applied=%u native_writes=0 extra_draws=0",
            eye.producer,eye.generation,eye.xr_frame,eye.native_frame,eye.eye,pass.program,p.sha256.data(),static_cast<unsigned long>(eye.source),static_cast<unsigned long>(eye.state),static_cast<unsigned long>(eye.view),pass.material,pass.pass,p.sampler,
            target.Width,target.Height,unsigned(target.Format),texture.Width,texture.Height,unsigned(texture.Format),eye.viewport.x,eye.viewport.y,eye.viewport.width,eye.viewport.height,p.vpos_registers[0],p.vpos_registers[1],p.vpos_registers[2],
            saved[0][0],saved[0][1],saved[0][2],saved[0][3],saved[1][0],saved[1][1],saved[1][2],saved[1][3],saved[2][0],saved[2][1],saved[2][2],saved[2][3],
            eye.corrected_constants.x[0],eye.corrected_constants.x[1],eye.corrected_constants.x[2],eye.corrected_constants.x[3],eye.corrected_constants.y[0],eye.corrected_constants.y[1],eye.corrected_constants.y[2],eye.corrected_constants.y[3],eye.corrected_constants.one[0],eye.corrected_constants.one[1],eye.corrected_constants.one[2],eye.corrected_constants.one[3],
            uv.affine[0],uv.affine[1],uv.affine[2],uv.affine[3],uv.bounds[0],uv.bounds[1],uv.bounds[2],uv.bounds[3],uv_enabled,vpos_enabled,applied);
    }
};
WaterReflectionCorrection::WaterReflectionCorrection() noexcept=default;
WaterReflectionCorrection::~WaterReflectionCorrection()=default;
void WaterReflectionCorrection::configure(std::uintptr_t base) noexcept {
    Preserve preserve;try{if(!state_)state_=std::make_unique<State>();auto& s=*state_;if(s.configured)return;
        s.base=base;s.configured=base>=0x10000&&base<=UINT32_MAX-0x4240000;
        wchar_t value[4]{};
        const auto uv=GetEnvironmentVariableW(L"BOVR_WATER_REFLECTION_CORRECTION",value,4);s.uv_enabled=uv==1&&value[0]==L'1';
        const auto vp=GetEnvironmentVariableW(L"BOVR_WATER_VPOS_CORRECTION",value,4);s.vpos_enabled=vp==1&&value[0]==L'1';
        const auto probe=GetEnvironmentVariableW(L"BOVR_WATER_REFLECTION_PROBE",value,4);s.probe=probe==1&&value[0]==L'1';
        trace("t5_water_shader_configured","configured=%u uv=%u vpos=%u probe=%u exact_programs=4 cache_limit=16 native_writes=0 extra_draws=0",s.configured,s.uv_enabled,s.vpos_enabled,s.probe);
    }catch(...){}
}
std::uint64_t WaterReflectionCorrection::before(IDirect3DDevice9* device,const IndexedDrawCall& draw) noexcept {
    if(!state_)return 0;auto& s=*state_;
    if(!s.configured||s.faulted||s.open||s.pending_mask||s.leases.load()||(!s.uv_enabled&&!s.vpos_enabled&&!s.probe)||draw.type!=D3DPT_TRIANGLELIST)return 0;
    // Once both eye budgets are consumed, disabled legacy corrections have no
    // remaining work. Do not keep reading ownership or native shader names on
    // every world draw merely because the diagnostic flag remains enabled.
    const bool observe=s.probe&&(s.underwater_attempts[0]<300000||s.underwater_attempts[1]<300000);
    if(!s.uv_enabled&&!s.vpos_enabled&&!observe)return 0;
    Preserve preserve;
    if(observe)candidate_snapshot(s.base);
    if(observe){
        Pass observed{};copy(s.base+0x417eed0,observed.pass);copy(observed.pass+8,observed.pixel_record);
        copy(observed.pixel_record+4,observed.pixel);copy(s.base+0x417eec4,observed.material);
        copy(s.base+0x417eecc,observed.technique);copy(s.base+0x417eed4,observed.index);
        s.underwater_begin(device,observed);
    }
    Pass selected{};if(!candidate(s.base,selected))return 0;
    if((s.owner&&s.owner!=GetCurrentThreadId())||(s.device&&s.device!=device)){s.reject("owner_or_device_changed");return 0;}
    State::Operation operation(s);s.owner=GetCurrentThreadId();s.device=device;s.pass=selected;s.witness=indexed_draw_reentry_serial();
    if(!read_vpos_eye_context(&s.eye)){trace_vpos_eye_refusal();s.reject("eye_scope");return 0;}
    if(s.eye.base!=s.base||s.eye.device!=reinterpret_cast<std::uintptr_t>(device)||!s.current()){s.reject("eye_identity");return 0;}
    if(!s.uv_enabled&&!s.vpos_enabled&&s.logs[s.eye.eye][s.pass.program]>=4)return 0;
    std::uint32_t expected_texture{},expected_target{};
    if(!pass_valid(s.eye,s.pass,expected_texture)){s.reject("native_pass_or_arguments");return 0;}
    if(!copy(s.base+0x41eb1e8+3*20+4,expected_target)||!expected_target){s.reject("native_target3");return 0;}
    IDirect3DPixelShader9* bound{};IDirect3DSurface9* target{};IDirect3DBaseTexture9* texture{};
    IDirect3DTexture9* texture2d{};IDirect3DDevice9* object_device{};
    const auto cleanup=[&]{drop(object_device);drop(texture2d);drop(texture);drop(target);drop(bound);};
    D3DVIEWPORT9 viewport{};D3DSURFACE_DESC target_desc{},texture_desc{};
    const auto& p=water_reflection_programs[s.pass.program];bool okay=false;const char* stage="bound_pixel_shader";
    try {
        do {
            if(!s.call([&]{return device->GetPixelShader(&bound);})||!bound||reinterpret_cast<std::uintptr_t>(bound)!=s.pass.pixel||!s.owner_of(bound))break;
            if(!s.acquire_shader(bound)){s.reject("shader_fingerprint_or_cache");break;}
            stage="gpu_viewport";
            if(!s.call([&]{return device->GetViewport(&viewport);})||viewport.X!=unsigned(s.eye.viewport.x)||viewport.Y!=unsigned(s.eye.viewport.y)||
               viewport.Width!=unsigned(s.eye.viewport.width)||viewport.Height!=unsigned(s.eye.viewport.height)||viewport.MinZ!=0.f||viewport.MaxZ!=1.f)break;
            stage="gpu_target3";std::uint32_t reread_target{};
            if(!s.call([&]{return device->GetRenderTarget(0,&target);})||!target||reinterpret_cast<std::uintptr_t>(target)!=expected_target||
               !copy(s.base+0x41eb1e8+3*20+4,reread_target)||reread_target!=expected_target||!s.call([&]{return target->GetDesc(&target_desc);})||
               target_desc.Width!=s.eye.target_width||target_desc.Height!=s.eye.target_height||target_desc.Type!=D3DRTYPE_SURFACE||
               target_desc.Usage!=D3DUSAGE_RENDERTARGET||target_desc.Pool!=D3DPOOL_DEFAULT||target_desc.MultiSampleType!=D3DMULTISAMPLE_NONE)break;
            stage="target_device";
            if(!s.call([&]{return target->GetDevice(&object_device);})||object_device!=device)break;drop(object_device);
            stage="resolved_texture_binding";
            if(!s.call([&]{return device->GetTexture(p.sampler,&texture);})||!texture||reinterpret_cast<std::uintptr_t>(texture)!=expected_texture||
               !s.call([&]{return texture->QueryInterface(__uuidof(IDirect3DTexture9),reinterpret_cast<void**>(&texture2d));})||!texture2d||
               !s.call([&]{return texture2d->GetDevice(&object_device);})||object_device!=device)break;drop(object_device);
            stage="resolved_texture_desc";
            if(!s.call([&]{return texture2d->GetLevelDesc(0,&texture_desc);})||texture_desc.Type!=D3DRTYPE_SURFACE||
               texture_desc.Usage!=D3DUSAGE_RENDERTARGET||texture_desc.Pool!=D3DPOOL_DEFAULT||texture_desc.MultiSampleType!=D3DMULTISAMPLE_NONE)break;
            stage="eye_uv_mapping";
            if(!water_reflection_uv({s.eye.viewport.x,s.eye.viewport.y,s.eye.viewport.width,s.eye.viewport.height},target_desc.Width,target_desc.Height,texture_desc.Width,texture_desc.Height,&s.uv))break;
            stage="gpu_vpos_constants";
            bool values=true;for(unsigned i=0;i<3;++i)values=values&&s.call([&]{return device->GetPixelShaderConstantF(p.vpos_registers[i],s.saved[i].data(),1);});
            if(!values||!same_bits(s.saved[0],s.eye.constants.x)||!same_bits(s.saved[1],s.eye.constants.y)||!same_bits(s.saved[2],s.eye.constants.one)){s.reject("gpu_vpos_differs_from_native");break;}
            stage="reserved_constants";
            if(s.uv_enabled&&(!s.call([&]{return device->GetPixelShaderConstantF(water_affine_register,s.saved[3].data(),1);})||
               !s.call([&]{return device->GetPixelShaderConstantF(water_bounds_register,s.saved[4].data(),1);}) ))break;
            stage="post_probe_identity";if(!s.current())break;okay=true;
        }while(false);
    }catch(...){okay=false;}
    cleanup();if(!okay||!s.current()){s.reject(stage);return 0;}
    if(!s.uv_enabled&&!s.vpos_enabled){s.record(false,target_desc,texture_desc);return 0;}
    if(s.next_token==UINT64_MAX){s.faulted=true;return 0;}s.token=++s.next_token;s.open=true;
    if(s.vpos_enabled){const std::array<VPosVector,3> values{s.eye.corrected_constants.x,s.eye.corrected_constants.y,s.eye.corrected_constants.one};
        for(unsigned i=0;i<3;++i){s.pending_mask|=1u<<i;if(!s.call([&]{return device->SetPixelShaderConstantF(p.vpos_registers[i],values[i].data(),1);})){s.restore();s.open=false;return 0;}}}
    if(s.uv_enabled){s.pending_mask|=8;if(!s.call([&]{return device->SetPixelShaderConstantF(water_affine_register,s.uv.affine.data(),1);})){s.restore();s.open=false;return 0;}
        s.pending_mask|=16;if(!s.call([&]{return device->SetPixelShaderConstantF(water_bounds_register,s.uv.bounds.data(),1);})){s.restore();s.open=false;return 0;}
        s.pending_mask|=32;if(!s.call([&]{return device->SetPixelShader(s.cache[s.cache_slot].replacement);})){s.restore();s.open=false;return 0;}}
    s.record(true,target_desc,texture_desc);return s.token;
}
void WaterReflectionCorrection::after(std::uint64_t token,HRESULT result) noexcept {
    Preserve preserve;if(!state_)return;auto& s=*state_;State::Operation operation(s);
    if(!s.open||token!=s.token||s.leases.load()!=1){s.faulted=true;return;}
    if(s.underwater_open){
        UnderwaterGpu values{};const bool checked=result==D3D_OK&&s.underwater_read(s.device,s.underwater_selected,s.underwater_native,values);
        if(checked)s.underwater_record("after",s.underwater_native,values);
        trace("t5_underwater_draw_complete","producer=%llu eye=%u native_hresult=0x%08lx checked=%u gpu_unchanged=%u native_writes=0 gpu_writes=0",s.underwater_native.owner.producer,s.underwater_native.owner.eye,static_cast<unsigned long>(result),checked,checked&&values==s.underwater_gpu);
        s.underwater_open=false;s.open=false;return;
    }
    if(result==D3DERR_DEVICELOST||result==D3DERR_DEVICENOTRESET){s.faulted=true;s.open=false;return;}
    const bool restored=s.restore();s.open=false;++s.draws;
    if(s.probe&&(s.draws<=8||s.draws%1200==0))trace("t5_water_shader_restored","draws=%llu producer=%llu eye=%u program=%u native_hresult=0x%08lx restored=%u pending=%u queries=%llu",s.draws,s.eye.producer,s.eye.eye,s.pass.program,static_cast<unsigned long>(result),restored,s.pending_mask,s.queries);
}
void WaterReflectionCorrection::abandon(std::uint64_t) noexcept {if(state_){state_->underwater_open=false;state_->open=false;state_->faulted=true;}}
bool WaterReflectionCorrection::reconcile_normal(IDirect3DDevice9* device,bool usable) noexcept {
    Preserve preserve;if(!state_)return true;auto& s=*state_;
    if(s.open||s.leases.load()||(s.owner&&s.owner!=GetCurrentThreadId())||(s.device&&s.device!=device))return false;
    if(!s.pending_mask)return true;if(!usable)return false;
    State::Operation operation(s);const auto serial=indexed_draw_reentry_serial();
    const auto identity=[&]() noexcept {return s.leases.load()==1&&GetCurrentThreadId()==s.owner&&device==s.device&&serial!=indexed_draw_reentry_exhausted&&indexed_draw_reentry_serial()==serial;};
    const auto invoke=[&](const auto& f) noexcept {return identity()&&contained(f)==D3D_OK&&identity();};
    // The eye scope may have ended or the device may have completed ResetEx.
    // Reconcile only actual owned GPU values; never replay a stale eye context.
    if(s.pending_mask&32){
        IDirect3DPixelShader9* actual{};
        if(!invoke([&]{return device->GetPixelShader(&actual);})){drop(actual);return false;}
        const bool ours=actual==s.cache[s.cache_slot].replacement;drop(actual);if(!identity())return false;
        if(ours){
            if(!invoke([&]{return device->SetPixelShader(s.cache[s.cache_slot].original);})||
               !invoke([&]{return device->GetPixelShader(&actual);})){drop(actual);return false;}
            const bool restored=actual==s.cache[s.cache_slot].original;drop(actual);if(!restored||!identity())return false;
        }
        s.pending_mask&=~32u;
    }
    const std::array<VPosVector,5> written{s.eye.corrected_constants.x,s.eye.corrected_constants.y,s.eye.corrected_constants.one,s.uv.affine,s.uv.bounds};
    for(int i=4;i>=0;--i)if(s.pending_mask&(1u<<i)){
        const unsigned reg=i<3?water_reflection_programs[s.pass.program].vpos_registers[i]:water_affine_register+unsigned(i)-3;
        VPosVector actual{};if(!invoke([&]{return device->GetPixelShaderConstantF(reg,actual.data(),1);}))return false;
        if(same_bits(actual,written[i])&&!same_bits(actual,s.saved[i])){
            if(!invoke([&]{return device->SetPixelShaderConstantF(reg,s.saved[i].data(),1);})||
               !invoke([&]{return device->GetPixelShaderConstantF(reg,actual.data(),1);})||!same_bits(actual,s.saved[i]))return false;
        }
        // Already restored, or a later native write replaced our temporary value.
        s.pending_mask&=~(1u<<i);
    }
    s.faulted=false;if(s.probe)trace("t5_water_shader_reconciled","pending=%u actual_state_checked=1",s.pending_mask);return true;
}
bool WaterReflectionCorrection::before_reset(IDirect3DDevice9* device) noexcept {
    Preserve preserve;if(!state_)return true;auto& s=*state_;
    if(s.open||s.leases.load()||(s.owner&&s.owner!=GetCurrentThreadId())||(s.device&&s.device!=device))return false;
    // Pixel shader objects survive Reset/ResetEx and are not default-pool
    // surfaces. Keep their identities while native Reset is allowed to run.
    return s.pending_mask?true:retire();
}
void WaterReflectionCorrection::after_reset(IDirect3DDevice9* device,HRESULT result) noexcept {
    if(result!=D3D_OK)return;
    if(reconcile_normal(device,true))retire();
}
bool WaterReflectionCorrection::retire_terminal(IDirect3DDevice9* device) noexcept {
    Preserve preserve;if(!state_)return true;auto& s=*state_;
    if(s.open||s.leases.load()||(s.owner&&s.owner!=GetCurrentThreadId())||(s.device&&s.device!=device))return false;
    // Only an accepted native game quit can invoke this: no future device draw.
    if(s.pending_mask)trace("t5_water_shader_terminal_abandon","pending=%u restored=0 terminal_native_quit=1",s.pending_mask);
    s.pending_mask=0;return retire();
}
bool WaterReflectionCorrection::retire() noexcept {
    Preserve preserve;if(!state_)return true;auto& s=*state_;
    if(s.leases.load()||s.open||s.pending_mask||(s.owner&&GetCurrentThreadId()!=s.owner))return false;
    State::Operation operation(s);for(auto& item:s.cache){drop(item.replacement);drop(item.original);item={};}
    s.device=nullptr;s.owner=0;s.faulted=false;s.underwater_open=false;return true;
}
bool WaterReflectionCorrection::pending() const noexcept {return state_&&state_->pending_mask;}
bool WaterReflectionCorrection::busy() const noexcept {return state_&&(state_->open||state_->leases.load());}
bool WaterReflectionCorrection::rejected() const noexcept {return state_&&state_->faulted;}
}
