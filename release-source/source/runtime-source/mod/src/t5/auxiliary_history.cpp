// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/auxiliary_history.hpp"
#include "loader/trace.hpp"
#include <cstring>
#include <sstream>
#include <vector>
namespace bovr::t5 {
namespace {
struct Error {DWORD value{GetLastError()};~Error(){SetLastError(value);}};
template<class T>struct Com {
    T* p{};~Com(){if(p)p->Release();}Com()=default;Com(const Com&)=delete;
    Com& operator=(const Com&)=delete;T** put(){return &p;}
};
bool copy_scalars(std::uintptr_t base,HistoryScalars* out) noexcept {
    HistoryScalars s{};
    __try {
        std::memcpy(&s.view_mode,reinterpret_cast<void*>(base+0x3ccbfa4),4);
        std::memcpy(&s.viewport_behavior,reinterpret_cast<void*>(base+0x3ccbfec),4);
        std::memcpy(&s.target_width,reinterpret_cast<void*>(base+0x3ccbff0),4);
        std::memcpy(&s.target_height,reinterpret_cast<void*>(base+0x3ccbff4),4);
        std::memcpy(&s.viewport_dirty,reinterpret_cast<void*>(base+0x3ccbff8),1);
        std::memcpy(&s.scissor_dirty,reinterpret_cast<void*>(base+0x3ccbff9),1);
        std::memcpy(&s.scissor,reinterpret_cast<void*>(base+0x3ccbfcc),sizeof(s.scissor));
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    *out=s;return true;
}
bool one_snapshot(std::uintptr_t base,const AuxiliaryViewLayout& layout,unsigned selected,AuxiliaryHistorySnapshot* out) noexcept {
    AuxiliaryHistorySnapshot a{},b{};
    if(!read_auxiliary_postfx(base,layout,selected,&a.postfx)||!copy_scalars(base,&a.source)||
       !read_auxiliary_postfx(base,layout,selected,&b.postfx)||!copy_scalars(base,&b.source)||a!=b)return false;
    *out=b;return true;
}
bool same_device(IDirect3DDevice9* a,IDirect3DDevice9* b) {
    if(!a||!b)return false;Com<IUnknown> x,y;
    return SUCCEEDED(a->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(x.put())))&&x.p&&
        SUCCEEDED(b->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(y.put())))&&y.p&&x.p==y.p;
}
unsigned pixel_bytes(D3DFORMAT f) noexcept {
    switch(f){case D3DFMT_A8R8G8B8:case D3DFMT_X8R8G8B8:case D3DFMT_A2R10G10B10:case D3DFMT_G16R16F:return 4;
    case D3DFMT_A16B16G16R16F:case D3DFMT_A16B16G16R16:return 8;default:return 0;}
}
bool supported(const D3DSURFACE_DESC& d) noexcept {
    return d.Type==D3DRTYPE_SURFACE&&d.Pool==D3DPOOL_DEFAULT&&d.Width&&d.Height&&
        d.Width<=1920&&d.Height<=1080&&d.MultiSampleType==D3DMULTISAMPLE_NONE&&!d.MultiSampleQuality&&pixel_bytes(d.Format);
}
bool equal_desc(const D3DSURFACE_DESC& a,const D3DSURFACE_DESC& b) noexcept {
    return a.Format==b.Format&&a.Width==b.Width&&a.Height==b.Height&&a.MultiSampleType==b.MultiSampleType&&a.MultiSampleQuality==b.MultiSampleQuality&&a.Pool==b.Pool&&a.Type==b.Type;
}
bool write_new(const std::wstring& path,const void* bytes,std::size_t count) noexcept {
    if(count>MAXDWORD)return false;
    const auto f=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;DWORD n{};const bool ok=WriteFile(f,bytes,static_cast<DWORD>(count),&n,nullptr)&&n==count;CloseHandle(f);return ok;
}
bool directory(std::wstring* out) {
    wchar_t path[32768]{};const auto n=GetEnvironmentVariableW(L"BOVR_AUXILIARY_HISTORY_DIRECTORY",path,32768);
    if(n<3||n>=32768||path[1]!=L':'||(path[2]!=L'\\'&&path[2]!=L'/'))return false;
    const auto attr=GetFileAttributesW(path);if(attr==INVALID_FILE_ATTRIBUTES||!(attr&FILE_ATTRIBUTE_DIRECTORY)||(attr&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    *out=path;return true;
}
template<class T>void hex(std::ostream& text,const T& value) {
    constexpr char digits[]="0123456789abcdef";const auto* p=reinterpret_cast<const unsigned char*>(&value);
    for(unsigned i=0;i<sizeof(T);++i)text<<digits[p[i]>>4]<<digits[p[i]&15];
}
struct GpuImage {Com<IDirect3DSurface9> gpu,system;D3DSURFACE_DESC desc{};HRESULT copy_hr=E_PENDING,read_hr=E_PENDING;bool raw{},bitmap{};};
HRESULT allocate(IDirect3DDevice9* d,const D3DSURFACE_DESC& desc,GpuImage* out) {
    out->desc=desc;
    auto hr=d->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,out->gpu.put(),nullptr);
    if(SUCCEEDED(hr)&&!out->gpu.p)hr=E_POINTER;
    if(SUCCEEDED(hr))hr=d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,out->system.put(),nullptr);
    if(SUCCEEDED(hr)&&!out->system.p)hr=E_POINTER;return hr;
}
bool bitmap(const std::wstring& path,const D3DSURFACE_DESC& d,const std::vector<unsigned char>& bytes) {
    if(d.Format!=D3DFMT_A8R8G8B8&&d.Format!=D3DFMT_X8R8G8B8)return false;
    BITMAPFILEHEADER f{};BITMAPINFOHEADER i{};f.bfType=0x4d42;f.bfOffBits=sizeof(f)+sizeof(i);f.bfSize=static_cast<DWORD>(f.bfOffBits+bytes.size());
    i.biSize=sizeof(i);i.biWidth=d.Width;i.biHeight=-static_cast<LONG>(d.Height);i.biPlanes=1;i.biBitCount=32;i.biCompression=BI_RGB;i.biSizeImage=static_cast<DWORD>(bytes.size());
    std::vector<unsigned char> b(f.bfSize);std::memcpy(b.data(),&f,sizeof f);std::memcpy(b.data()+sizeof f,&i,sizeof i);std::memcpy(b.data()+f.bfOffBits,bytes.data(),bytes.size());
    return write_new(path,b.data(),b.size());
}
void readback(IDirect3DDevice9* d,GpuImage& image,const std::wstring& prefix) {
    if(FAILED(image.copy_hr))return;
    image.read_hr=d->GetRenderTargetData(image.gpu.p,image.system.p);if(FAILED(image.read_hr))return;
    D3DLOCKED_RECT lock{};image.read_hr=image.system.p->LockRect(&lock,nullptr,D3DLOCK_READONLY);if(FAILED(image.read_hr))return;
    struct Unlock {IDirect3DSurface9* p;~Unlock(){p->UnlockRect();}} unlock{image.system.p};
    const auto row=image.desc.Width*pixel_bytes(image.desc.Format);
    if(!lock.pBits||lock.Pitch<0||static_cast<unsigned>(lock.Pitch)<row){image.read_hr=E_FAIL;return;}
    std::vector<unsigned char> bytes(std::size_t(row)*image.desc.Height);
    for(unsigned y=0;y<image.desc.Height;++y)std::memcpy(bytes.data()+std::size_t(y)*row,static_cast<unsigned char*>(lock.pBits)+std::size_t(y)*lock.Pitch,row);
    image.raw=write_new(prefix+L".raw",bytes.data(),bytes.size());
    image.bitmap=bitmap(prefix+L".bmp",image.desc,bytes);
}
}
bool read_auxiliary_history(std::uintptr_t base,const AuxiliaryViewLayout& layout,unsigned selected,AuxiliaryHistorySnapshot* out) noexcept {
    Error e;if(out)*out={};if(!out||!base||base>UINT32_MAX-auxiliary_read_extent)return false;
    return one_snapshot(base,layout,selected,out);
}
struct AuxiliaryHistoryCapture::State {
    std::uintptr_t base{};AuxiliaryViewLayout layout{};AuxiliaryPostfxKey key{};HistoryKind kind{};HistoryCurrent current{};
    IDirect3DDevice9* device{};Com<IDirect3DDevice9> retained_device;
    std::wstring prefix;std::array<AuxiliaryHistorySnapshot,3> snapshots{};
    std::array<D3DVIEWPORT9,3> viewports{};std::array<RECT,3> scissors{};std::array<DWORD,3> scissor_enabled{};
    std::array<std::array<GpuImage,3>,3> images;GpuImage pair;
    std::array<ULONGLONG,3> begin_ms{},end_ms{};std::array<ULONGLONG,3> copy_ms{};
    ULONGLONG prepared_ms{},completed_ms{},readback_ms{};unsigned next{};bool failed{},finished{};const char* reason="none";HRESULT failure_hr=S_OK;
    bool alive() const noexcept {return key.owner==GetCurrentThreadId()&&current.valid();}
    void fail(const char* why,HRESULT hr=E_FAIL) noexcept {if(!failed){failed=true;reason=why;failure_hr=hr;}}
    bool snapshot(unsigned stage,AuxiliaryHistorySnapshot* out) noexcept {
        return alive()&&read_auxiliary_history(base,layout,stage==2?0:2,out)&&out->postfx.views==snapshots[0].postfx.views&&
            out->postfx.targets==snapshots[0].postfx.targets;
    }
    HRESULT borrow(const AuxiliaryHistorySnapshot& snap,unsigned index,Com<IDirect3DSurface9>& surface,D3DSURFACE_DESC* desc) {
        if(!alive())return E_ABORT;HRESULT hr=S_OK;
        if(index==2)hr=device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,surface.put());
        else {
            const auto& t=snap.postfx.targets[index==0?0:2];
            if(!t.color||!t.width||!t.height)return E_POINTER;
            surface.p=reinterpret_cast<IDirect3DSurface9*>(t.color);surface.p->AddRef();
        }
        if(FAILED(hr)||!surface.p)return FAILED(hr)?hr:E_POINTER;
        // Recheck the native tuple immediately after acquisition and before
        // further COM use. This synchronous borrow is not a model generation.
        AuxiliaryHistorySnapshot acquired{};
        if(!alive()||!read_auxiliary_history(base,layout,snap.postfx.layout.selected,&acquired)||acquired!=snap)return E_ABORT;
        Com<IDirect3DDevice9> owner;hr=surface.p->GetDevice(owner.put());
        if(SUCCEEDED(hr)&&(!owner.p||!same_device(device,owner.p)))hr=E_INVALIDARG;
        if(SUCCEEDED(hr))hr=surface.p->GetDesc(desc);
        if(SUCCEEDED(hr)&&!supported(*desc))hr=D3DERR_INVALIDCALL;
        if(SUCCEEDED(hr)&&index<2){const auto& t=snap.postfx.targets[index==0?0:2];if(t.width!=desc->Width||t.height!=desc->Height)hr=E_INVALIDARG;}
        if(!alive())hr=E_ABORT;return hr;
    }
    void output(bool native_complete) {
        std::ostringstream text;text<<"version=1\nproducer="<<key.producer<<" generation="<<key.generation<<" xr_frame="<<key.xr_frame<<" native_frame="<<layout.frame
            <<" buffer="<<layout.buffer<<" view_array="<<layout.view_array<<" count="<<layout.count<<" owner="<<key.owner<<" attempt="<<key.attempt<<" kind="<<static_cast<unsigned>(kind)<<'\n';
        text<<"native_complete="<<native_complete<<" stages_copied="<<next<<" diagnostic_complete="<<(!failed&&native_complete&&next==3&&SUCCEEDED(pair.copy_hr))<<" reason="<<reason
            <<" failure_hr="<<static_cast<unsigned long>(failure_hr)<<" prepared_ms="<<prepared_ms<<" completed_ms="<<completed_ms<<" readback_ms="<<readback_ms<<" capture_calls_change_timing=1\n";
        for(unsigned s=0;s<3;++s){text<<"stage="<<s<<" begin_ms="<<begin_ms[s]<<" end_ms="<<end_ms[s]<<" copy_ms="<<copy_ms[s]
            <<" source_scalars_hex=";const auto& v=snapshots[s].source;
            const std::array<std::uint32_t,6> words{v.view_mode,v.viewport_behavior,v.target_width,v.target_height,v.viewport_dirty,v.scissor_dirty};
            hex(text,words);text<<" source_scissor_hex=";hex(text,v.scissor);text<<" saved_times_hex=";hex(text,snapshots[s].postfx.saved_times);text<<" saved_params_hex=";hex(text,snapshots[s].postfx.saved_params);
            text<<" source_time_hex=";hex(text,snapshots[s].postfx.source_scene_def);text<<" source_viewport_hex=";hex(text,snapshots[s].postfx.source_viewport);
            text<<" source_target="<<unsigned(snapshots[s].postfx.source_target)<<" selected="<<snapshots[s].postfx.layout.selected;
            for(unsigned eye=0;eye<2;++eye){const auto& view=snapshots[s].postfx.views[eye];text<<" view"<<eye<<"_scene_hex=";hex(text,view.scene_def);
                text<<" view"<<eye<<"_full="<<view.full_screen<<" view"<<eye<<"_mode="<<view.rendering_mode;}
            text<<" effects0_hex=";hex(text,snapshots[s].postfx.views[0].effects);text<<" effects1_hex=";hex(text,snapshots[s].postfx.views[1].effects);
            text<<" targets_hex=";hex(text,snapshots[s].postfx.targets);text<<" d3d_viewport_hex=";hex(text,viewports[s]);text<<" d3d_scissor_hex=";hex(text,scissors[s]);text<<" scissor_enabled="<<scissor_enabled[s]<<'\n';}
        auto describe=[&](unsigned stage,unsigned image,const GpuImage& x){text<<"image_stage="<<stage<<" image="<<image<<" width="<<x.desc.Width<<" height="<<x.desc.Height<<" format="<<unsigned(x.desc.Format)
            <<" bpp="<<pixel_bytes(x.desc.Format)<<" copy_hr="<<static_cast<unsigned long>(x.copy_hr)<<" read_hr="<<static_cast<unsigned long>(x.read_hr)<<" raw_written="<<x.raw<<" bmp_written="<<x.bitmap<<'\n';};
        for(unsigned s=0;s<3;++s)for(unsigned i=0;i<3;++i)describe(s,i,images[s][i]);describe(3,2,pair);
        const auto value=text.str();const bool report=!prefix.empty()&&write_new(prefix+L".txt",value.data(),value.size());
        trace("t5_auxiliary_history_complete","attempt=%llu producer=%llu generation=%llu xr_frame=%llu native_frame=%u native_complete=%u copied_stages=%u diagnostic_failed=%u reason=%s report_written=%u output_prefix=%ls native_target_writes=0",key.attempt,key.producer,key.generation,key.xr_frame,layout.frame,native_complete,next,failed,reason,report,prefix.c_str());
    }
};
AuxiliaryHistoryCapture::AuxiliaryHistoryCapture() noexcept=default;
AuxiliaryHistoryCapture::~AuxiliaryHistoryCapture() {
    if(!state_)return;
    Error e;
    if(state_&&!state_->finished)trace("t5_auxiliary_history_abandoned","attempt=%llu producer=%llu stages=%u reason=invocation_unwind_or_missing_finish",state_->key.attempt,state_->key.producer,state_->next);
    state_.reset();
}
void AuxiliaryHistoryCapture::begin(bool enabled,AuxiliaryHistoryBudget& budget,std::uintptr_t base,const AuxiliaryViewLayout& layout,
        const AuxiliaryPostfxKey& key,IDirect3DDevice9* device,HistoryCurrent current) noexcept {
    if(!enabled)return;
    Error e;if(state_||budget.exhausted()||!device||!current.valid()||!key.producer||!key.generation||!key.xr_frame||key.owner!=GetCurrentThreadId())return;
    try {
        AuxiliaryHistorySnapshot snap{};if(!read_auxiliary_history(base,layout,2,&snap))return;
        const auto kind=history_kind(snap.postfx);const auto attempt=budget.claim(kind,GetTickCount64());if(!attempt)return;
        state_=std::make_unique<State>();auto& s=*state_;s.base=base;s.layout=layout;s.key=key;s.key.attempt=attempt;s.kind=kind;s.current=current;s.device=device;s.snapshots[0]=snap;
        s.device->AddRef();s.retained_device.p=device;std::wstring dir;
        if(!directory(&dir)){s.fail("directory");return;}
        s.prefix=dir+L"\\history-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(attempt)+L"-"+std::to_wstring(key.producer);
        // Allocate every owned destination before either native call.
        for(unsigned i=0;i<3&&!s.failed;++i){Com<IDirect3DSurface9> source;D3DSURFACE_DESC d{};auto hr=s.borrow(snap,i,source,&d);
            if(SUCCEEDED(hr))for(unsigned stage=0;stage<3&&SUCCEEDED(hr);++stage)hr=allocate(device,d,&s.images[stage][i]);
            if(SUCCEEDED(hr)&&i==2)hr=allocate(device,d,&s.pair);
            if(FAILED(hr))s.fail("surface_or_allocation",hr);}
        AuxiliaryHistorySnapshot after{};if(!s.snapshot(0,&after)||after!=snap)s.fail("prepare_identity_changed");
        s.prepared_ms=GetTickCount64();
        trace("t5_auxiliary_history_begin","attempt=%llu producer=%llu generation=%llu xr_frame=%llu native_frame=%u kind=%u ready=%u output_prefix=%ls active_cap=2 inactive_cap=1 capture_surfaces=10",attempt,key.producer,key.generation,key.xr_frame,layout.frame,unsigned(kind),!s.failed,s.prefix.c_str());
        if(!s.failed)stage(0);
    }catch(...){if(state_)state_->fail("prepare_exception");}
}
void AuxiliaryHistoryCapture::stage(unsigned stage_index) noexcept {
    if(!state_||state_->failed)return;Error e;auto& s=*state_;
    try {
        if(stage_index>2||stage_index!=s.next||!s.alive()){s.fail("stage_order_or_lifecycle");return;}
        const auto stage=stage_index;s.begin_ms[stage]=GetTickCount64();AuxiliaryHistorySnapshot snap{};
        if(!s.snapshot(stage,&snap)){s.fail("stage_identity");return;}
        s.snapshots[stage]=snap;
        auto hr=s.device->GetViewport(&s.viewports[stage]);if(SUCCEEDED(hr))hr=s.device->GetScissorRect(&s.scissors[stage]);
        if(SUCCEEDED(hr))hr=s.device->GetRenderState(D3DRS_SCISSORTESTENABLE,&s.scissor_enabled[stage]);
        if(FAILED(hr)){s.fail("stage_state_read",hr);return;}
        for(unsigned i=0;i<3;++i){Com<IDirect3DSurface9> source;D3DSURFACE_DESC d{};auto& image=s.images[stage][i];
            hr=s.borrow(snap,i,source,&d);if(SUCCEEDED(hr)&&!equal_desc(d,image.desc))hr=E_INVALIDARG;
            AuxiliaryHistorySnapshot before{};if(SUCCEEDED(hr)&&(!s.snapshot(stage,&before)||before!=snap))hr=E_ABORT;
            const auto start=GetTickCount64();if(SUCCEEDED(hr))hr=s.device->StretchRect(source.p,nullptr,image.gpu.p,nullptr,D3DTEXF_NONE);s.copy_ms[stage]+=GetTickCount64()-start;image.copy_hr=hr;
            AuxiliaryHistorySnapshot after{};if(FAILED(hr)||!s.snapshot(stage,&after)||after!=snap){s.fail("stage_copy_or_identity",FAILED(hr)?hr:E_ABORT);return;}}
        s.end_ms[stage]=GetTickCount64();++s.next;
    }catch(...){s.fail("stage_exception");}
}
void AuxiliaryHistoryCapture::finish(bool native_complete,const render::EyeStaging& staging) noexcept {
    if(!state_||state_->finished)return;Error e;auto& s=*state_;s.finished=true;
    try {
        if(!native_complete||s.next!=3||!s.alive())s.fail("native_incomplete_or_lifecycle");
        AuxiliaryHistorySnapshot last{};
        if(!s.failed&&(!read_auxiliary_history(s.base,s.layout,2,&last)||last.postfx.views!=s.snapshots[0].postfx.views||last.postfx.targets!=s.snapshots[0].postfx.targets))s.fail("completion_identity");
        if(!s.failed){s.pair.copy_hr=staging.copy_completed_pair(s.device,s.key.producer,s.pair.gpu.p);if(FAILED(s.pair.copy_hr)||!s.alive())s.fail("completed_pair_copy",FAILED(s.pair.copy_hr)?s.pair.copy_hr:E_ABORT);}
        s.completed_ms=GetTickCount64();const auto start=s.completed_ms;
        // Readback occurs only after the native two-call plan and ownership checks.
        if(!s.failed){for(unsigned stage=0;stage<3&&!s.failed;++stage)for(unsigned i=0;i<3&&!s.failed;++i){
                if(!s.alive()){s.fail("readback_lifecycle");break;}
                readback(s.device,s.images[stage][i],s.prefix+L"-stage"+std::to_wstring(stage)+L"-image"+std::to_wstring(i));
                if(FAILED(s.images[stage][i].read_hr)||!s.images[stage][i].raw||!s.alive())s.fail("readback_or_file");}
            if(!s.failed){readback(s.device,s.pair,s.prefix+L"-pair");if(FAILED(s.pair.read_hr)||!s.pair.raw||!s.alive())s.fail("pair_readback_or_file");}}
        s.readback_ms=GetTickCount64()-start;s.output(native_complete);
    }catch(...){s.fail("finish_exception");trace("t5_auxiliary_history_failed","attempt=%llu producer=%llu reason=finish_exception",s.key.attempt,s.key.producer);}
}
void AuxiliaryHistoryCapture::abandon_native_fault() noexcept {
    if(!state_||state_->key.owner!=GetCurrentThreadId())return;Error e;state_->fail("native_unwind_cleanup_deferred");state_->finished=true;
}
void AuxiliaryHistoryCapture::retire() noexcept {
    if(!state_||state_->key.owner!=GetCurrentThreadId())return;Error e;state_.reset();
}
// Generated from the exact pinned helper/scissor/viewport spans.
#include "t5/auxiliary_history_guards.inc"
}
