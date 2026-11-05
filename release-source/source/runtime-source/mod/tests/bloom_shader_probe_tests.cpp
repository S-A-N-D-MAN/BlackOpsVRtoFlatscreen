// SPDX-License-Identifier: GPL-3.0-only
#include "t5/bloom_shader_probe.hpp"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <functional>
#include <fstream>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {unsigned checks{},failures{};std::vector<std::string> logs;
void check(bool b,const char* s){++checks;if(!b){++failures;std::printf("FAIL %s\n",s);}}}
namespace bovr {void trace(const char* e,const char* f,...) noexcept {try {char b[2048]{};va_list a;va_start(a,f);vsnprintf_s(b,sizeof b,_TRUNCATE,f,a);va_end(a);logs.push_back(std::string(e)+" "+b);}catch(...){}}}
namespace {
using namespace bovr::t5;
struct Device;
struct Object {void** table{};Device* device{};unsigned refs{1};bool vertex{};};
struct Device {
    void** table{};unsigned refs{1};Object vs,ps,surface,texture;
    BloomDrawSnapshot snapshot{};bool current{true},readable{true},pure{},null_vertex{},bad_constant{},bad_length{},oversize{},bad_tokens{},source_seh_after_vertex{};
    unsigned shader_size{8},target_width{640},target_height{360};D3DRESOURCETYPE target_type{D3DRTYPE_SURFACE};
    std::map<std::string,unsigned> calls;std::string failure,throwing,seh_failure,hook_name;
    std::function<void()> hook;BloomShaderProbe* probe{};
    bool step(const char* n){SetLastError(0x9988);++calls[n];if(hook&&hook_name==n){auto cb=std::move(hook);hook={};cb();}
        if(throwing==n)throw std::runtime_error("getter fixture");if(seh_failure==n)RaiseException(0xe1249999,0,0,nullptr);return failure!=n;}
    IDirect3DDevice9* api(){return reinterpret_cast<IDirect3DDevice9*>(this);}
};
ULONG __stdcall add(Device* d){return ++d->refs;}
ULONG __stdcall release(Device* d){check(d->refs>1,"device returned references stay owned through Release");--d->refs;d->step("device_release");return d->refs;}
HRESULT __stdcall query(Device* d,REFIID id,void** p){if(!d->step("device_query"))return E_FAIL;check(id==IID_IUnknown,"only canonical leased-device QI");*p=d;add(d);return S_OK;}
ULONG __stdcall oadd(Object* o){check(o->refs>0,"never acquire dead resource");return ++o->refs;}
ULONG __stdcall orelease(Object* o){check(o->refs>1,"return only getter-owned resource reference");--o->refs;o->device->step("resource_release");return o->refs;}
HRESULT __stdcall oquery(Object* o,REFIID id,void** p){check(o->refs>1,"QI only on already leased texture");if(!o->device->step("texture_query"))return E_FAIL;check(id==IID_IDirect3DTexture9,"typed texture QI");*p=o;oadd(o);return S_OK;}
HRESULT __stdcall getdevice(Object* o,IDirect3DDevice9** p){check(o->refs>1,"GetDevice only on leased shader/surface");if(!o->device->step("resource_device"))return E_FAIL;*p=o->device->api();add(o->device);return S_OK;}
HRESULT __stdcall function(Object* o,void* p,UINT* bytes){check(o->refs>1,"GetFunction only on device-returned typed shader");auto& d=*o->device;if(!d.step(p?"function_data":"function_size"))return E_FAIL;
    if(!p){*bytes=d.oversize?65540:d.shader_size;return S_OK;}if(d.bad_length){*bytes-=4;return S_OK;}
    check(*bytes==d.shader_size,"retrieval capacity equals bounded advertised size");std::memset(p,0,*bytes);const std::uint32_t v=o->vertex?0xfffe0300:0xffff0300,end=0xffff;
    std::memcpy(p,&v,4);std::memcpy(static_cast<char*>(p)+*bytes-4,&end,4);if(d.bad_tokens){const std::uint32_t malformed=0x0f000001;std::memcpy(static_cast<char*>(p)+4,&malformed,4);}return S_OK;}
HRESULT __stdcall creation(Device* d,D3DDEVICE_CREATION_PARAMETERS* p){if(!d->step("creation"))return E_FAIL;*p={};p->BehaviorFlags=d->pure?D3DCREATE_PUREDEVICE:0;return S_OK;}
HRESULT __stdcall vertex(Device* d,IDirect3DVertexShader9** p){if(!d->step("vertex"))return E_FAIL;*p=d->null_vertex?nullptr:reinterpret_cast<IDirect3DVertexShader9*>(&d->vs);if(*p)oadd(&d->vs);return S_OK;}
HRESULT __stdcall pixel(Device* d,IDirect3DPixelShader9** p){if(!d->step("pixel"))return E_FAIL;*p=reinterpret_cast<IDirect3DPixelShader9*>(&d->ps);oadd(&d->ps);return S_OK;}
HRESULT __stdcall target(Device* d,DWORD i,IDirect3DSurface9** p){check(i==0,"bound RT0 only");if(!d->step("target"))return E_FAIL;*p=reinterpret_cast<IDirect3DSurface9*>(&d->surface);oadd(&d->surface);return S_OK;}
D3DSURFACE_DESC descriptor(){return {D3DFMT_A16B16G16R16,D3DRTYPE_SURFACE,D3DUSAGE_RENDERTARGET,D3DPOOL_DEFAULT,D3DMULTISAMPLE_NONE,0,640,360};}
HRESULT __stdcall desc(Object* o,D3DSURFACE_DESC* p){if(!o->device->step("target_desc"))return E_FAIL;*p=descriptor();p->Width=o->device->target_width;p->Height=o->device->target_height;p->Type=o->device->target_type;return S_OK;}
HRESULT __stdcall level(Object* o,UINT i,D3DSURFACE_DESC* p){check(i==0&&o->refs>1,"bound texture level0 leased");if(!o->device->step("texture_desc"))return E_FAIL;*p=descriptor();return S_OK;}
HRESULT __stdcall viewport(Device* d,D3DVIEWPORT9* p){if(!d->step("viewport"))return E_FAIL;*p={0,0,640,360,0,1};return S_OK;}
HRESULT __stdcall scissor(Device* d,RECT* p){if(!d->step("scissor"))return E_FAIL;*p={0,0,640,360};return S_OK;}
HRESULT __stdcall state(Device* d,D3DRENDERSTATETYPE t,DWORD* p){check(t==D3DRS_SCISSORTESTENABLE,"only diagnostic state getter");if(!d->step("state"))return E_FAIL;*p=0;return S_OK;}
HRESULT __stdcall texture(Device* d,DWORD i,IDirect3DBaseTexture9** p){check(i==2,"code sampler destination actually used");if(!d->step("texture"))return E_FAIL;*p=reinterpret_cast<IDirect3DBaseTexture9*>(&d->texture);oadd(&d->texture);return S_OK;}
HRESULT __stdcall sampler(Device* d,DWORD i,D3DSAMPLERSTATETYPE t,DWORD* p){check(i==2,"current texture stage");if(!d->step("sampler"))return E_FAIL;*p=static_cast<DWORD>(t);return S_OK;}
HRESULT __stdcall constant(Device* d,UINT i,float* p,UINT n){check((i==9||i==6)&&n==1,"exact mapped constant register/row");if(!d->step("constant"))return E_FAIL;std::memcpy(p,d->snapshot.viewport_constant.data(),16);if(d->bad_constant)p[0]+=1;return S_OK;}
struct Fixture {
    std::array<void*,119> dt{};std::array<void*,5> shader{};std::array<void*,17> st{};std::array<void*,22> tt{};
    Device d;BloomShaderProbe probe;std::wstring dir;bool preserve_outputs{},surface_scene{};
    Fixture(){dt[0]=reinterpret_cast<void*>(query);dt[1]=reinterpret_cast<void*>(add);dt[2]=reinterpret_cast<void*>(release);dt[9]=reinterpret_cast<void*>(creation);dt[38]=reinterpret_cast<void*>(target);dt[48]=reinterpret_cast<void*>(viewport);dt[58]=reinterpret_cast<void*>(state);dt[64]=reinterpret_cast<void*>(texture);dt[68]=reinterpret_cast<void*>(sampler);dt[76]=reinterpret_cast<void*>(scissor);dt[93]=reinterpret_cast<void*>(vertex);dt[95]=reinterpret_cast<void*>(constant);dt[108]=reinterpret_cast<void*>(pixel);dt[110]=reinterpret_cast<void*>(constant);d.table=dt.data();
        shader[1]=st[1]=tt[1]=reinterpret_cast<void*>(oadd);shader[2]=st[2]=tt[2]=reinterpret_cast<void*>(orelease);shader[3]=st[3]=tt[3]=reinterpret_cast<void*>(getdevice);shader[4]=reinterpret_cast<void*>(function);st[12]=reinterpret_cast<void*>(desc);tt[0]=reinterpret_cast<void*>(oquery);tt[17]=reinterpret_cast<void*>(level);
        d.vs={shader.data(),&d,1,true};d.ps={shader.data(),&d,1,false};d.surface={st.data(),&d,1,false};d.texture={tt.data(),&d,1,false};d.probe=&probe;
        auto& n=d.snapshot;n.width=640;n.height=360;n.vertex_shader=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&d.vs));n.pixel_shader=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&d.ps));n.native_target={0xdead1000,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&d.surface)),0,640,360};n.pass_count=1;n.sampler_count=1;n.constant_count=2;n.samplers[0]={34,2,0xdead2000,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&d.texture)),0,0,0};n.constants[0]={3,9};n.constants[1]={5,6};n.viewport_constant={0,0,0x3f800000,0x3f800000};select(1,0,0,1);
        wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);static unsigned serial{};dir=std::wstring(temp)+L"bovr-c11-fixture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial);check(CreateDirectoryW(dir.c_str(),nullptr)!=0,"unique owned fixture output");
    }
    ~Fixture(){probe.retire();check(d.refs==1&&d.vs.refs==1&&d.ps.refs==1&&d.surface.refs==1&&d.texture.refs==1,"all getter references released, native owning refs unchanged");
        if(preserve_outputs)return;WIN32_FIND_DATAW w{};auto h=FindFirstFileW((dir+L"\\*").c_str(),&w);if(h!=INVALID_HANDLE_VALUE){do{if(!(w.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))DeleteFileW((dir+L"\\"+w.cFileName).c_str());}while(FindNextFileW(h,&w));FindClose(h);}RemoveDirectoryW(dir.c_str());}
    void select(unsigned eye,unsigned role,unsigned pass,unsigned count){auto& n=d.snapshot;n.eye=eye;n.selected=eye?2:0;n.role=role;n.target=role==3?3:25+role;n.material=0x111000+100*role;n.techset=n.material+4;n.technique=n.material+8;n.pass=n.material+16+20*pass;n.pass_index=pass;n.pass_count=static_cast<std::uint16_t>(count);
        if(surface_scene){n.width=role==3?1280u:640u;n.height=role==3?720u:360u;n.native_target.image=role==3?0u:0xdead1000u;
            n.native_target.width=static_cast<std::uint16_t>(n.width);n.native_target.height=static_cast<std::uint16_t>(n.height);d.target_width=n.width;d.target_height=n.height;}}
    BloomShaderSource source(){return {&d,[](void* p)noexcept{auto& d=*static_cast<Device*>(p);if(d.source_seh_after_vertex&&d.calls["vertex"])RaiseException(0xe0c11000,0,0,nullptr);return d.current;},[](void* p,BloomDrawSnapshot* out,BloomReadStatus* why)noexcept{auto& x=*static_cast<Device*>(p);if(!x.readable){if(why)*why=BloomReadStatus::unrelated;return false;}*out=x.snapshot;if(why)*why=BloomReadStatus::accepted;return true;},7,GetCurrentThreadId(),true};}
    void begin(){probe.configure(true,dir);probe.begin({0,123,2,99,GetCurrentThreadId()},7,d.api(),source());}
    std::uint64_t before(){SetLastError(0x7755);auto t=probe.before(d.api());check(GetLastError()==0x7755,"before diagnostics preserve caller LastError");return t;}
    void after(std::uint64_t t,HRESULT hr=S_OK){SetLastError(0x8877);probe.after(t,hr);check(GetLastError()==0x8877,"after diagnostics preserve native LastError");}
    std::string manifest(unsigned attempt=1){std::ifstream f(dir+L"\\pair-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(attempt)+L".json",std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
    void pair(unsigned count=1){for(unsigned eye:{1u,0u})for(unsigned role=0;role<4;++role)for(unsigned p=0;p<count;++p){select(eye,role,p,count);auto t=before();check(t!=0,"admitted current typed bindings");after(t);}}
};
}
int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc==2&&std::strcmp(argv[1],"--emit-wire-fixture")==0){Fixture f;f.begin();f.pair(4);f.probe.finish(true);f.preserve_outputs=true;std::printf("fixture_directory=%ls\n",f.dir.c_str());return failures?1:0;}
    if(argc==2&&std::strcmp(argv[1],"--emit-surface-wire-fixture")==0){Fixture f;f.surface_scene=true;f.begin();f.pair();f.probe.finish(true);f.preserve_outputs=true;std::printf("fixture_directory=%ls\n",f.dir.c_str());return failures?1:0;}
    {Fixture f;f.surface_scene=true;f.begin();f.pair();f.probe.finish(true);
        check(f.probe.attempts()==1&&f.manifest().find("\"complete\":true")!=std::string::npos,"synthetic eight-draw pair accepts both surface-only scene outputs through actual typed getters");}
    for(unsigned failure=0;failure<5;++failure){Fixture f;f.surface_scene=true;f.select(1,3,0,1);
        if(failure==0)f.d.snapshot.native_target.color=0;
        if(failure==1)++f.d.snapshot.native_target.color;
        if(failure==2)--f.d.target_width;
        if(failure==3)--f.d.target_height;
        if(failure==4)f.d.target_type=D3DRTYPE_TEXTURE;
        f.begin();check(!f.before()&&!f.probe.attempts(),"surface-only output still requires matching retained RT0 and exact surface descriptor");f.probe.finish(true);}
    {Fixture f,other;f.surface_scene=true;f.select(1,3,0,1);other.d.target_width=1280;other.d.target_height=720;f.d.surface.device=&other.d;f.begin();
        check(!f.before()&&!f.probe.attempts(),"surface-only output rejects a retained target reporting another canonical device");
        f.d.surface.device=&f.d;f.probe.finish(true);}
    for(const char* op:{"vertex","target","target_desc","texture","function_size","function_data"})for(unsigned field=0;field<2;++field){
        Fixture f;f.surface_scene=true;f.begin();f.select(1,0,0,1);f.after(f.before()); // Claim before testing a later selected draw.
        f.select(1,3,0,1);f.d.hook_name=op;f.d.hook=[&]{if(field==0)f.d.snapshot.native_target.image=0xdead1234;else ++f.d.snapshot.native_target.color;};
        const auto t=f.before();if(t)f.after(t);f.probe.finish(true);
        check(f.manifest().find("\"complete\":false")!=std::string::npos&&f.manifest().find("\"diagnostic_failed\":1")!=std::string::npos,"getter mutation of surface-only image/color poisons claimed attempt, never skips failure");
    }
    for(unsigned field=0;field<2;++field){Fixture f;f.surface_scene=true;f.select(1,3,0,1);f.begin();const auto t=f.before();check(t!=0,"surface-only output owns a real lease before native-return mutation");
        if(field==0)f.d.snapshot.native_target.image=0xdead1234;else ++f.d.snapshot.native_target.color;f.after(t);f.probe.finish(true);
        check(f.manifest().find("\"complete\":false")!=std::string::npos&&f.manifest().find("\"reason\":\"post_tuple_or_binding\"")!=std::string::npos,"native-return image/color mutation cannot become accepted final-role evidence");}
    {Fixture f;f.probe.configure(false,L"");f.probe.begin({0,123,2,99,GetCurrentThreadId()},7,f.d.api(),f.source());check(!f.before()&&f.d.calls.empty()&&!f.probe.attempts(),"disabled path performs zero COM and claims nothing");}
    {Fixture f;f.begin();for(unsigned i=0;i<50;++i){f.d.readable=false;check(!f.before()&&!f.probe.attempts(),"unrelated frame does not consume attempt");f.probe.finish(true);f.probe.begin({0,123+i,2,99,GetCurrentThreadId()},7,f.d.api(),f.source());}f.d.readable=true;f.pair(4);f.probe.finish(true);check(f.probe.attempts()==1&&f.manifest().find("\"complete\":true")!=std::string::npos,"complete two-eye four-role actual shader records saved");
        f.probe.begin({0,200,2,100,GetCurrentThreadId()},7,f.d.api(),f.source());f.select(1,0,0,1);f.after(f.before());f.probe.finish(false);check(f.manifest(2).find("\"complete\":false")!=std::string::npos,"partial attempt preserved honestly despite failed ordinary staging");f.probe.begin({0,201,2,101,GetCurrentThreadId()},7,f.d.api(),f.source());check(!f.before()&&f.probe.attempts()==2,"two claimed attempts cannot renew");}
    for(const char* op:{"creation","vertex","pixel","resource_device","device_query","target","target_desc","viewport","scissor","state","texture","texture_query","texture_desc","sampler","constant"})for(unsigned mode=0;mode<3;++mode){Fixture f;f.begin();if(mode==0)f.d.failure=op;if(mode==1)f.d.throwing=op;if(mode==2)f.d.seh_failure=op;check(!f.before()&&!f.probe.attempts(),"getter HRESULT/C++/SEH failure contained before attempt claim");f.d.throwing.clear();f.d.seh_failure.clear();f.probe.finish(true);check(f.manifest().empty(),"no fabricated accepted output on getter failure");}
    for(const char* op:{"vertex","pixel","resource_device","device_query","target","target_desc","viewport","scissor","state","texture","texture_query","texture_desc","sampler","constant"})for(unsigned mode=0;mode<3;++mode){Fixture f;f.begin();f.d.hook_name=op;f.d.hook=[&]{if(mode==0)f.d.current=false;else if(mode==1)++f.d.snapshot.material;else f.probe.retire();};check(!f.before()&&!f.probe.attempts(),"owner/native tuple/retirement during each getter rejects before next use");f.probe.finish(true);}
    for(unsigned why=0;why<9;++why){Fixture f;f.begin();auto t=f.before();check(t!=0,"post mismatch starts with actual admitted lease");if(why==0)++f.d.snapshot.pass;if(why==1)f.d.current=false;if(why==2)f.d.bad_constant=true;if(why==3)f.d.null_vertex=true;if(why==4)f.d.bad_length=true;if(why==5)f.d.oversize=true;if(why==6)f.d.bad_tokens=true;if(why==7)f.d.failure="function_data";if(why==8)f.d.failure="function_size";f.after(t);f.probe.finish(true);check(f.manifest().find("\"complete\":false")!=std::string::npos,"post/binary failure records incomplete attempt");}
    {Fixture f;f.d.pure=true;f.begin();check(!f.before()&&!f.d.calls["vertex"],"pure device rejects before unsupported shader getter");}
    {Fixture f;f.begin();auto t=f.before();const auto before=f.d.calls;f.probe.abandon(t);check(before==f.d.calls&&f.d.vs.refs>1,"native unwind abandonment does no COM and retains leased shader");
        check(!f.before()&&before==f.d.calls&&f.d.vs.refs>1,"a later ignored callback cannot implicitly release an abandoned native scope");
        f.probe.retire();check(f.d.vs.refs==1,"normal owner retirement eventually releases abandoned lease");}
    for(bool retry_begin:{false,true}){Fixture f;f.begin();auto t=f.before();f.d.hook_name="resource_release";f.d.hook=[&]{f.probe.retire();if(retry_begin)f.probe.begin({0,800,2,101,GetCurrentThreadId()},8,f.d.api(),f.source());};f.after(t);check(!f.before(),"Release reentry cannot resurrect/reuse a retired transaction");f.probe.retire();}
    {Fixture f;f.begin();auto t=f.before();f.after(t,E_FAIL);f.probe.finish(true);check(f.d.calls["vertex"]==1&&f.manifest().find("\"complete\":false")!=std::string::npos,"native failure never followed by post getter or fake completion");}
    {Fixture f;f.begin();auto t=f.before();const auto calls=f.d.calls;
        std::thread foreign([&]{check(!f.probe.before(f.d.api()),"foreign draw cannot use owner getter scope");f.probe.after(t,S_OK);f.probe.finish(true);f.probe.abandon(t);f.probe.retire();});foreign.join();
        check(calls==f.d.calls&&f.d.vs.refs>1,"foreign lifecycle methods preserve owner references and token");f.after(t);f.probe.finish(false);}
    // Charge across independent probe objects until the one process-wide cap.
    {Fixture f;f.begin();f.d.shader_size=65536;for(unsigned eye:{1u,0u})for(unsigned r=0;r<4;++r)for(unsigned p=0;p<4;++p){f.select(eye,r,p,4);auto t=f.before();if(t)f.after(t);}f.probe.finish(true);Fixture g;g.begin();g.d.shader_size=65536;for(unsigned eye:{1u,0u})for(unsigned r=0;r<4;++r)for(unsigned p=0;p<4;++p){g.select(eye,r,p,4);auto t=g.before();if(t)g.after(t);}g.probe.finish(true);check(g.probe.bytes_used()<=bloom_process_bytes&&bloom_process_bytes-g.probe.bytes_used()<65536&&g.probe.bytes_used()==f.probe.bytes_used(),"all probe objects share one enforced process budget");}
    std::printf("bloom_shader_probe_checks=%u failures=%u native_or_gpu_calls=0\n",checks,failures);return failures?1:0;
}
