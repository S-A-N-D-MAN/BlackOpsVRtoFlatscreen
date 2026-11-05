// SPDX-License-Identifier: GPL-3.0-only
// Reuse the real protected native-layout fixture and trace stub, not its main.
#define main inherited_postfx_main
#include "auxiliary_postfx_tests.cpp"
#undef main
#include "t5/auxiliary_history.hpp"
#include <fstream>
#include <functional>
#include <map>
#include <thread>
namespace history_fixture {
using namespace bovr::t5;
struct Device;
struct Surface {
    void** table{};Device* owner{};unsigned refs=1;bool native{};
    D3DSURFACE_DESC desc{D3DFMT_X8R8G8B8,D3DRTYPE_SURFACE,0,D3DPOOL_DEFAULT,D3DMULTISAMPLE_NONE,0,4,2};
    std::vector<unsigned char> pixels=std::vector<unsigned char>(32);
};
struct Device {
    void** table{};unsigned refs=1;std::array<void*,17> st{};
    Surface bb{},one{},six{};std::vector<std::unique_ptr<Surface>> owned;
    std::map<std::string,unsigned> calls;std::string failed_op;unsigned failed_number=1;
    bool current=true,expire_copy{},bad_pitch{},foreign_device{},throw_create{};
    std::function<void()> after_acquire;
    D3DVIEWPORT9 viewport{0,0,4,2,0,1};RECT scissor{0,0,4,2};DWORD scissor_enabled=1;
    bool fail(const char* op){SetLastError(0x9876);return ++calls[op]==failed_number&&failed_op==op;}
    IDirect3DDevice9* api(){return reinterpret_cast<IDirect3DDevice9*>(this);}
    Surface* make(UINT w,UINT h,D3DFORMAT f,D3DPOOL p){auto x=std::make_unique<Surface>();x->table=st.data();x->owner=this;x->desc.Width=w;x->desc.Height=h;x->desc.Format=f;x->desc.Pool=p;
        x->pixels.resize(std::size_t(w)*h*(f==D3DFMT_A16B16G16R16F?8:4));auto* s=x.get();owned.push_back(std::move(x));return s;}
};
ULONG __stdcall dadd(Device* d){return ++d->refs;}
ULONG __stdcall drelease(Device* d){check(d->refs>1,"diagnostic never releases native owning device reference");SetLastError(0x9999);return --d->refs;}
HRESULT __stdcall dquery(Device* d,REFIID id,void** out){if(d->fail("query"))return E_FAIL;check(id==IID_IUnknown,"canonical device identity only");*out=d;dadd(d);return S_OK;}
ULONG __stdcall sadd(Surface* s){check(s->refs>0,"surface borrowed only while native or mod reference lives");++s->refs;if(s->native&&s->owner->after_acquire){auto work=std::move(s->owner->after_acquire);s->owner->after_acquire={};work();}return s->refs;}
ULONG __stdcall srelease(Surface* s){check(s->refs>0,"owned surface reference released exactly once");SetLastError(0x9999);return --s->refs;}
HRESULT __stdcall sdevice(Surface* s,IDirect3DDevice9** out){if(s->owner->fail("getdevice"))return E_FAIL;*out=s->owner->api();dadd(s->owner);return S_OK;}
HRESULT __stdcall desc(Surface* s,D3DSURFACE_DESC* out){if(s->owner->fail("desc"))return E_FAIL;*out=s->desc;return S_OK;}
HRESULT __stdcall lock(Surface* s,D3DLOCKED_RECT* out,const RECT* rect,DWORD flags){if(s->owner->fail("lock"))return E_FAIL;
    check(s->desc.Pool==D3DPOOL_SYSTEMMEM&&!rect&&flags==D3DLOCK_READONLY,"read-only lock only on owned system image");
    out->pBits=s->pixels.data();out->Pitch=s->owner->bad_pitch?1:static_cast<INT>(s->pixels.size()/s->desc.Height);return S_OK;}
HRESULT __stdcall unlock(Surface* s){s->owner->fail("unlock");return S_OK;}
HRESULT __stdcall bb(Device* d,UINT chain,UINT index,D3DBACKBUFFER_TYPE type,IDirect3DSurface9** out){if(d->fail("backbuffer"))return E_FAIL;
    check(!chain&&!index&&type==D3DBACKBUFFER_TYPE_MONO,"actual device backbuffer borrowed at fixed slot");sadd(&d->bb);*out=reinterpret_cast<IDirect3DSurface9*>(&d->bb);return S_OK;}
HRESULT __stdcall create(Device* d,UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE ms,DWORD q,BOOL l,IDirect3DSurface9** out,HANDLE* shared){
    if(d->throw_create)throw std::runtime_error("diagnostic allocation fixture");if(d->fail("create"))return E_FAIL;
    check(ms==D3DMULTISAMPLE_NONE&&!q&&!l&&!shared,"private nonshared single-sample GPU copy");*out=reinterpret_cast<IDirect3DSurface9*>(d->make(w,h,f,D3DPOOL_DEFAULT));return S_OK;}
HRESULT __stdcall system(Device* d,UINT w,UINT h,D3DFORMAT f,D3DPOOL p,IDirect3DSurface9** out,HANDLE* shared){if(d->fail("system"))return E_FAIL;
    check(p==D3DPOOL_SYSTEMMEM&&!shared,"private matching system image");*out=reinterpret_cast<IDirect3DSurface9*>(d->make(w,h,f,p));return S_OK;}
HRESULT __stdcall stretch(Device* d,IDirect3DSurface9* src,const RECT* sr,IDirect3DSurface9* dst,const RECT* dr,D3DTEXTUREFILTERTYPE filter){
    if(d->fail("stretch"))return E_FAIL;auto* from=reinterpret_cast<Surface*>(src);auto* to=reinterpret_cast<Surface*>(dst);
    check(!to->native&&to->desc.Pool==D3DPOOL_DEFAULT&&filter==D3DTEXF_NONE,"every capture and eye copy writes mod target only");
    check((sr==nullptr)==(dr==nullptr),"whole or paired identical rectangles only");
    if(!sr)to->pixels=from->pixels;
    else {check(!std::memcmp(sr,dr,sizeof(RECT)),"eye copy retains rectangle without scaling");for(LONG y=sr->top;y<sr->bottom;++y)for(LONG x=sr->left;x<sr->right;++x)
        std::memcpy(to->pixels.data()+(y*4+x)*4,from->pixels.data()+(y*4+x)*4,4);}
    if(d->expire_copy)d->current=false;return S_OK;
}
HRESULT __stdcall read(Device* d,IDirect3DSurface9* src,IDirect3DSurface9* dst){if(d->fail("read"))return E_FAIL;
    auto* from=reinterpret_cast<Surface*>(src);auto* to=reinterpret_cast<Surface*>(dst);check(!from->native&&!to->native&&to->desc.Pool==D3DPOOL_SYSTEMMEM,"readback uses owned GPU snapshot, never live native source");to->pixels=from->pixels;return S_OK;}
HRESULT __stdcall viewport(Device* d,D3DVIEWPORT9* out){if(d->fail("viewport"))return E_FAIL;*out=d->viewport;return S_OK;}
HRESULT __stdcall scissor(Device* d,RECT* out){if(d->fail("scissor"))return E_FAIL;*out=d->scissor;return S_OK;}
HRESULT __stdcall renderstate(Device* d,D3DRENDERSTATETYPE type,DWORD* out){if(d->fail("renderstate"))return E_FAIL;check(type==D3DRS_SCISSORTESTENABLE,"only copied scissor-enabled scalar read");*out=d->scissor_enabled;return S_OK;}
void color(Surface& s,unsigned n){for(std::size_t p=0;p<s.pixels.size();p+=4)std::memcpy(s.pixels.data()+p,&n,4);}
struct Fixture {
    std::array<void*,119> table{};Device d;Memory m;AuxiliaryViewLayout layout;std::wstring dir;
    Fixture(bool active=true,unsigned slot=0){
        d.st[1]=reinterpret_cast<void*>(sadd);d.st[2]=reinterpret_cast<void*>(srelease);d.st[3]=reinterpret_cast<void*>(sdevice);d.st[12]=reinterpret_cast<void*>(desc);d.st[13]=reinterpret_cast<void*>(lock);d.st[14]=reinterpret_cast<void*>(unlock);
        table[0]=reinterpret_cast<void*>(dquery);table[1]=reinterpret_cast<void*>(dadd);table[2]=reinterpret_cast<void*>(drelease);table[18]=reinterpret_cast<void*>(bb);table[28]=reinterpret_cast<void*>(create);table[32]=reinterpret_cast<void*>(read);table[34]=reinterpret_cast<void*>(stretch);table[36]=reinterpret_cast<void*>(system);table[48]=reinterpret_cast<void*>(viewport);table[58]=reinterpret_cast<void*>(renderstate);table[76]=reinterpret_cast<void*>(scissor);d.table=table.data();
        for(auto* s:{&d.bb,&d.one,&d.six}){s->table=d.st.data();s->owner=&d;s->native=true;}
        layout=fill(m,slot);
        for(unsigned i=0;i<2;++i){auto& v=layout.slots[i];v.scene=v.display=v.scissor={int(i)*2,0,2,2};m.write(v.address+0x140,v.scene);m.write(v.address+0x150,v.display);m.write(v.address+0x160,v.scissor);
            m.write(v.address+0x170,std::array<std::uint32_t,5>{1000,0x3f800000,0,0,0});m.write(v.address+0x18c,0u);m.write(v.address+0x3620,0u);
            std::array<std::uint32_t,21> fx{};if(active){fx[5]=1;fx[6]=85;fx[9]=fx[10]=0x3f800000;fx[12]=1;}m.write(v.address+0x21cc,fx);}
        for(unsigned i=0;i<2;++i){const auto id=i?6u:1u;auto* s=i?&d.six:&d.one;AuxiliaryTarget t{0x11110000+id,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(s)),0x33330000+id,4,2};m.write(m.base+0x41eb1e8+id*20,t);}
        m.write(m.base+0x3ccbfa4,0u);m.write(m.base+0x3ccbfec,1u);m.write(m.base+0x3ccbff0,4u);m.write(m.base+0x3ccbff4,2u);m.write(m.base+0x3ccbff8,std::uint8_t{1});m.write(m.base+0x3ccbff9,std::uint8_t{1});
        m.write(m.base+0x3ccbfcc,bovr::render::HudEyeViewport{0,0,4,2});m.write(m.base+0x356a32c,std::array<std::uint32_t,4>{985,0,0,0});stage(0);
        wchar_t tmp[MAX_PATH]{};GetTempPathW(MAX_PATH,tmp);static unsigned serial{};
        dir=std::wstring(tmp)+L"bovr-c9-fixture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial);check(CreateDirectoryW(dir.c_str(),nullptr)!=0,"unique fixture output directory created");SetEnvironmentVariableW(L"BOVR_AUXILIARY_HISTORY_DIRECTORY",dir.c_str());
    }
    ~Fixture(){check(d.refs==1&&d.bb.refs==1&&d.one.refs==1&&d.six.refs==1,"all borrowed native and retained device refs returned");for(const auto& s:d.owned)check(s->refs==0,"all invocation GPU/system surfaces released");
        WIN32_FIND_DATAW data{};const auto find=FindFirstFileW((dir+L"\\*").c_str(),&data);if(find!=INVALID_HANDLE_VALUE){do{if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))DeleteFileW((dir+L"\\"+data.cFileName).c_str());}while(FindNextFileW(find,&data));FindClose(find);}RemoveDirectoryW(dir.c_str());}
    void stage(unsigned n){color(d.one,0x11000011+n);color(d.six,0x66000011+n);color(d.bb,0xbb000011+n);
        if(n){m.write(m.base+0x356a32c,1000u);m.write(m.base+0x3ccbfa8,1000u);}m.write(m.base+0x417ff28,std::uint8_t(n?3:2));
        const unsigned x=n==1?2:0,w=n?2:4;m.write(m.base+0x3ccbfbc,bovr::render::HudEyeViewport{int(x),0,int(w),2});d.viewport={x,0,w,2,0,1};d.scissor={LONG(x),0,LONG(x+w),2};}
    AuxiliaryPostfxKey key(){return {0,123,2,99,GetCurrentThreadId()};}
    HistoryCurrent current(){return {&d,[](void* p)noexcept{return static_cast<Device*>(p)->current;}};}
    std::wstring prefix(){return dir+L"\\history-"+std::to_wstring(GetCurrentProcessId())+L"-1-123";}
    std::string text(){std::ifstream f(prefix()+L".txt",std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
    std::vector<unsigned char> bytes(const std::wstring& suffix){std::ifstream f(prefix()+suffix,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
};
const std::array<RECT,2> rects{{{0,0,2,2},{2,0,4,2}}};
void run(Fixture& f,AuxiliaryHistoryCapture& capture,bovr::render::EyeStaging& staging,bool native_throw=false){
    bool started=false;unsigned n=0,copies=0;
    try {const auto complete=execute_stereo_draw_plan(auxiliary_draw_plan(),[&](unsigned x){f.m.write(f.layout.buffer+0x16cc38,x);},[&]{
        ++n;if(native_throw)throw std::runtime_error("native fault");f.stage(n);SetLastError(0x5511+n);capture.stage(n);check(GetLastError()==0x5511+n,"stage preserves original native LastError");},
        [&](unsigned eye){++copies;return SUCCEEDED(staging.save_eye(f.d.api(),123,eye));},&started);
        SetLastError(0x6655);capture.finish(complete,staging);check(GetLastError()==0x6655,"finish preserves original LastError through readback/files");
        check(complete&&started&&n==2&&copies==2&&staging.completed_eye_mask()==3,"diagnostics do not consume completion or change two native calls/copies");}
    catch(const std::runtime_error&){check(native_throw&&started&&n==1&&!copies,"native exception forwards without after-copy or second native call");throw;}
}
}
#ifndef BOVR_HISTORY_CAPTURE_TEST_MAIN
#define BOVR_HISTORY_CAPTURE_TEST_MAIN main
#endif
int BOVR_HISTORY_CAPTURE_TEST_MAIN(){std::setvbuf(stdout,nullptr,_IONBF,0);using namespace history_fixture;using bovr::render::EyeStaging;
    {AuxiliaryHistoryBudget b;check(!b.claim(HistoryKind::active,0)&&!b.claim(HistoryKind::unsupported,1),"invalid and unsupported frames spend nothing");
        check(b.claim(HistoryKind::active,1000)==1&&!b.claim(HistoryKind::active,1499)&&!b.claim(HistoryKind::inactive,999),"minimum spacing and no time regression");
        check(b.claim(HistoryKind::inactive,1500)==2&&!b.claim(HistoryKind::inactive,9999)&&b.claim(HistoryKind::active,2000)==3&&b.exhausted(),"exact two active plus one inactive budget");
        for(unsigned i=0;i<100;++i)check(!b.claim(HistoryKind::active,3000+i*1000)&&!b.claim(HistoryKind::inactive,3000+i*1000),"exhaustion permanent");}
    for(unsigned slot=0;slot<2;++slot){Fixture f(true,slot);AuxiliaryHistorySnapshot s{};SetLastError(123);check(read_auxiliary_history(f.m.base,f.layout,2,&s)&&GetLastError()==123&&history_kind(s.postfx)==HistoryKind::active,"both actual native allocation slots and added scalar copy accepted");
        check(s.source.viewport_behavior==1&&s.source.target_width==4&&s.source.target_height==2&&s.source.viewport_dirty==1&&s.source.scissor_dirty==1&&s.source.scissor.width==4,"exact source scalar offsets copied");
        for(unsigned j=0;j<11;++j){auto p=s;switch(j){case 0:p.postfx.views[1].effects[12]=0;break;case 1:p.postfx.views[1].scene_def[0]++;break;case 2:p.postfx.views[0].rendering_mode=1;break;case 3:p.postfx.views[0].full_screen=1;break;case 4:for(auto& v:p.postfx.views)v.effects[4]=1;break;case 5:for(auto& v:p.postfx.views)v.effects[6]=0;break;case 6:for(auto& v:p.postfx.views)v.effects[6]=10001;break;case 7:for(auto& v:p.postfx.views)v.effects[9]=0;break;case 8:for(auto& v:p.postfx.views)v.effects[20]=1;break;case 9:for(auto& v:p.postfx.views)v.effects[7]=1;break;case 10:for(auto& v:p.postfx.views)v.effects[8]=1;break;}check(history_kind(p.postfx)==HistoryKind::unsupported,"unsupported or mismatched effects rejected before GPU");}
        for(auto address:{f.m.base+0x3ccbfa4,f.m.base+0x3ccbfec,f.m.base+0x3ccbff8,f.m.base+0x3ccbfcc}){DWORD old{};auto* page=reinterpret_cast<void*>(address&~std::uintptr_t(4095));VirtualProtect(page,4096,PAGE_NOACCESS,&old);check(!read_auxiliary_history(f.m.base,f.layout,2,&s)&&s==AuxiliaryHistorySnapshot{},"protected source scalar fault returns no stale record");DWORD ignored{};VirtualProtect(page,4096,old,&ignored);}}
    for(bool active:{true,false}){Fixture f(active);AuxiliaryHistoryBudget budget;EyeStaging staging;check(SUCCEEDED(staging.begin(f.d.api(),123,rects,{1,0})),"real eye staging begins");
        {AuxiliaryHistoryCapture cap;SetLastError(100);cap.begin(true,budget,f.m.base,f.layout,f.key(),f.d.api(),f.current());check(GetLastError()==100,"prepare preserves LastError");run(f,cap,staging);
            const auto report=f.text();check(report.find("diagnostic_complete=1")!=std::string::npos&&report.find("stages_copied=3")!=std::string::npos,"ten images complete with one exact key");
            for(unsigned s=0;s<3;++s)for(unsigned i=0;i<3;++i){const auto bytes=f.bytes(L"-stage"+std::to_wstring(s)+L"-image"+std::to_wstring(i)+L".raw");check(bytes.size()==32,"exact raw surface row extent");const unsigned expected=(i==0?0x11000011:i==1?0x66000011:0xbb000011)+s;
                for(unsigned p=0;p<8&&bytes.size()==32;++p){unsigned value{};std::memcpy(&value,bytes.data()+p*4,4);check(value==expected,"each same-key stage retains immutable source pixels");}}
            const auto pair=f.bytes(L"-pair.raw");for(unsigned p=0;p<8&&pair.size()==32;++p){unsigned value{};std::memcpy(&value,pair.data()+p*4,4);check(value==((p%4)<2?0xbb000013u:0xbb000012u),"completed pair preserves right from first native and left from second");}
            check(pair.size()==32&&f.d.calls["read"]==10&&f.d.calls["lock"]==10&&f.d.calls["unlock"]==10,"only ten owned GPU readbacks after pair completion");}
        staging.release();}
    for(const char* op:{"create","system","backbuffer","getdevice","query","desc","viewport","scissor","renderstate","stretch","read","lock"}){
        Fixture f;AuxiliaryHistoryBudget b;EyeStaging staging;check(SUCCEEDED(staging.begin(f.d.api(),123,rects,{1,0})),"failure fixture staging begins before diagnostic");f.d.calls.clear();f.d.failed_op=op;
        {AuxiliaryHistoryCapture cap;cap.begin(true,b,f.m.base,f.layout,f.key(),f.d.api(),f.current());run(f,cap,staging);check(f.text().find("diagnostic_complete=0")!=std::string::npos,"injected API failure remains diagnostic failure while native rendering succeeds");
            if(std::string(op)=="create"||std::string(op)=="system")check(f.text().find("reason=surface_or_allocation failure_hr=2147500037")!=std::string::npos,"original allocation HRESULT and reason survive finish failure");}
        staging.release();}
    for(unsigned why=0;why<9;++why){Fixture f;AuxiliaryHistoryBudget b;EyeStaging staging;staging.begin(f.d.api(),123,rects,{1,0});
        {AuxiliaryHistoryCapture cap;if(why==0)SetEnvironmentVariableW(L"BOVR_AUXILIARY_HISTORY_DIRECTORY",nullptr);if(why==1)f.d.throw_create=true;if(why==2)f.d.expire_copy=true;
            if(why==3)f.d.bad_pitch=true;cap.begin(true,b,f.m.base,f.layout,f.key(),f.d.api(),f.current());
            if(why==4)f.d.current=false;if(why==5)f.m.write(f.m.base+0x41eb1e8+20,AuxiliaryTarget{});if(why==6)f.m.write(f.layout.buffer+0x15d7a0,43u);
            if(why==7){std::ofstream file(f.prefix()+L"-stage0-image0.raw",std::ios::binary);file<<"original";}
            if(why==8)cap.stage(2);run(f,cap,staging);const auto report=f.text();check(why==0?report.empty():report.find("diagnostic_complete=0")!=std::string::npos,"directory allocation lifecycle extent order and output failures never become accepted capture");
            if(why==7){auto bytes=f.bytes(L"-stage0-image0.raw");check(std::string(bytes.begin(),bytes.end())=="original","CREATE_NEW preserves existing artifact bytes");}}
        staging.release();}
    {Fixture f;AuxiliaryHistoryBudget b;EyeStaging staging;staging.begin(f.d.api(),123,rects,{1,0});try{AuxiliaryHistoryCapture cap;cap.begin(true,b,f.m.base,f.layout,f.key(),f.d.api(),f.current());run(f,cap,staging,true);check(false,"native throw expected");}catch(const std::runtime_error&){unsigned index{};std::memcpy(&index,reinterpret_cast<void*>(f.layout.buffer+0x16cc38),4);check(index==2&&f.d.calls["read"]==0&&f.text().empty(),"native throw retains existing index unwind and produces no fake completion/readback");}staging.release();}
    for(bool tuple:{true,false}){Fixture f;AuxiliaryHistoryBudget b;EyeStaging staging;staging.begin(f.d.api(),123,rects,{1,0});f.d.calls.clear();
        f.d.after_acquire=[&]{if(tuple)f.m.write(f.m.base+0x41eb1e8+20,AuxiliaryTarget{});else f.d.current=false;};
        {AuxiliaryHistoryCapture cap;cap.begin(true,b,f.m.base,f.layout,f.key(),f.d.api(),f.current());check(f.d.calls["getdevice"]==0&&f.d.calls["desc"]==0&&f.d.calls["query"]==0,"tuple or owner change immediately after acquired ref rejects before further COM use");run(f,cap,staging);check(f.text().find("diagnostic_complete=0")!=std::string::npos,"post-acquisition failure has no capture authority");}staging.release();}
    {Fixture f;AuxiliaryHistoryBudget b;EyeStaging staging;const auto before=f.d.calls;{AuxiliaryHistoryCapture cap;cap.begin(false,b,f.m.base,f.layout,f.key(),f.d.api(),f.current());cap.stage(0);cap.finish(true,staging);}check(f.d.calls==before&&f.text().empty(),"default off makes no COM or output operations");
        auto key=f.key();std::thread t([&]{AuxiliaryHistoryCapture cap;cap.begin(true,b,f.m.base,f.layout,key,f.d.api(),f.current());});t.join();check(f.d.calls==before,"foreign thread cannot borrow native COM");}
    {Fixture f;EyeStaging s;s.begin(f.d.api(),123,rects,{1,0});auto* out=f.d.make(4,2,D3DFMT_X8R8G8B8,D3DPOOL_DEFAULT);const auto start=f.d.calls;check(FAILED(s.copy_completed_pair(f.d.api(),123,reinterpret_cast<IDirect3DSurface9*>(out)))&&start==f.d.calls,"incomplete pair diagnostic rejected without COM");s.save_eye(f.d.api(),123,1);s.save_eye(f.d.api(),123,0);
        auto before=f.d.calls;check(FAILED(s.copy_completed_pair(f.d.api(),124,reinterpret_cast<IDirect3DSurface9*>(out)))&&before==f.d.calls,"wrong producer cannot copy pair");
        f.d.failed_op="stretch";f.d.failed_number=f.d.calls["stretch"]+1;SetLastError(0x744);check(FAILED(s.copy_completed_pair(f.d.api(),123,reinterpret_cast<IDirect3DSurface9*>(out)))&&s.completed_eye_mask()==3&&GetLastError()==0x744,"diagnostic pair copy failure preserves production completion and LastError");
        std::thread t([&]{check(FAILED(s.copy_completed_pair(f.d.api(),123,reinterpret_cast<IDirect3DSurface9*>(out))),"foreign pair reader rejects");});t.join();srelease(out);s.release();}
    check(!validate_auxiliary_history_guards(nullptr)&&!validate_auxiliary_history_guards(GetModuleHandleW(nullptr)),"fixture executable fails pinned native scalar guards");
    std::printf("auxiliary_history_checks=%u failures=%u real_gpu_calls=0 game_launches=0\n",checks,failures);return failures?1:0;
}
