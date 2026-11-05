// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/auxiliary_history_correction.hpp"
#include "t5/history_scalar_ownership.hpp"
#include "t5/postfx_resolution.hpp"
#include "loader/trace.hpp"
#include <algorithm>
#include <cstring>
#include <utility>
namespace bovr::t5 {
namespace {
struct Error {DWORD value{GetLastError()};~Error(){SetLastError(value);}};
template<class T>struct Com {
    T* p{};~Com(){reset();}void reset() noexcept {if(p){auto* old=p;p=nullptr;old->Release();}}
    T** put() noexcept {return &p;}Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
};
constexpr std::array<std::uint32_t,5> full_uv{0,0,0x3f800000,0x3f800000,1};
std::uint32_t bits(float v) noexcept {std::uint32_t b{};std::memcpy(&b,&v,4);return b;}
bool extra(std::uintptr_t base,const AuxiliaryViewLayout& layout,HistoryCorrectionSnapshot* out) noexcept {
    __try {
        for(unsigned eye=0;eye<2;++eye){const auto address=layout.slots[eye].address;
            std::memcpy(out->custom[eye].enabled.data(),reinterpret_cast<void*>(address+0x10b0),48);
            std::memcpy(out->custom[eye].material.data(),reinterpret_cast<void*>(address+0x10e0),192);}
        std::memcpy(&out->forced_dvar,reinterpret_cast<void*>(base+0x356f9e8),4);
        if(out->forced_dvar<0x10000||out->forced_dvar>UINT32_MAX-0x18)return false;
        std::memcpy(&out->forced,reinterpret_cast<void*>(out->forced_dvar+0x18),1);
        const auto image=out->native.postfx.targets[0].image;
        if(image<0x10000)return false;
        std::memcpy(&out->texture,reinterpret_cast<void*>(image),4);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    return out->texture>=0x10000;
}
bool same_scalar_scope(std::uintptr_t base,const AuxiliaryViewLayout& layout,const HistoryCorrectionSnapshot& initial) noexcept {
    // No invocation-stack callback is consulted during later owner retirement.
    // The native allocation/frame/array tuple must still identify this borrow.
    __try {
        AuxiliaryTarget target{};std::memcpy(&target,reinterpret_cast<const void*>(base+0x41eb1e8+20),sizeof(target));
        return target==initial.native.postfx.targets[0]&&target.image&&
            *reinterpret_cast<const std::uint32_t*>(target.image)==initial.texture&&
            *reinterpret_cast<const std::uint32_t*>(layout.slots[0].address+0x170)==initial.native.postfx.views[0].scene_def[0]&&
            *reinterpret_cast<const std::uint32_t*>(layout.slots[1].address+0x170)==initial.native.postfx.views[1].scene_def[0]&&
            *reinterpret_cast<const std::uint32_t*>(base+0x3a5b5f8)==layout.buffer&&
            *reinterpret_cast<const std::uint32_t*>(layout.buffer+0x15d7a0)==layout.frame&&
            *reinterpret_cast<const std::uint32_t*>(layout.buffer+0x16cc3c)==layout.count&&
            *reinterpret_cast<const std::uint32_t*>(layout.buffer+0x16cc40)==layout.view_array;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void restore_index(std::uint32_t* index,std::uint32_t value) noexcept {
    __try {*index=value;}__except(EXCEPTION_EXECUTE_HANDLER){}
}
bool identity(IUnknown* a,IUnknown* b) {
    if(!a||!b)return false;Com<IUnknown> x,y;
    return SUCCEEDED(a->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(x.put())))&&x.p&&
        SUCCEEDED(b->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(y.put())))&&y.p&&x.p==y.p;
}
bool description(const D3DSURFACE_DESC& a,const D3DSURFACE_DESC& b) noexcept {
    return a.Width==b.Width&&a.Height==b.Height&&a.Format==b.Format&&a.Type==b.Type&&a.Pool==b.Pool&&
        a.MultiSampleType==b.MultiSampleType&&a.MultiSampleQuality==b.MultiSampleQuality;
}
bool supported(const D3DSURFACE_DESC& d) noexcept {
    return postfx_resolution(d.Width,d.Height)&&d.Type==D3DRTYPE_SURFACE&&
        d.Pool==D3DPOOL_DEFAULT&&(d.Format==D3DFMT_A8R8G8B8||d.Format==D3DFMT_X8R8G8B8)&&
        d.MultiSampleType==D3DMULTISAMPLE_NONE&&!d.MultiSampleQuality;
}
std::array<std::uint32_t,5> uv(const AuxiliaryPostfxSnapshot& s) noexcept {
    std::array<std::uint32_t,5> result{};std::copy_n(s.saved_params.begin(),5,result.begin());return result;
}
bool remainder_equal(const AuxiliaryPostfxSnapshot& a,const AuxiliaryPostfxSnapshot& b) noexcept {
    return std::equal(a.saved_times.begin()+1,a.saved_times.end(),b.saved_times.begin()+1)&&
        std::equal(a.saved_params.begin()+5,a.saved_params.end(),b.saved_params.begin()+5);
}
}
bool read_history_correction(std::uintptr_t base,const AuxiliaryViewLayout& layout,unsigned selected,HistoryCorrectionSnapshot* out) noexcept {
    Error error;if(out)*out={};if(!out)return false;HistoryCorrectionSnapshot a{},b{};
    if(!read_auxiliary_history(base,layout,selected,&a.native)||!extra(base,layout,&a)||
       !read_auxiliary_history(base,layout,selected,&b.native)||!extra(base,layout,&b)||a!=b)return false;
    *out=b;return true;
}
struct AuxiliaryHistoryCorrection::State {
    enum class Reason : unsigned {none,context,unsupported,custom,borrow,allocation,preflight,order,copy,scalar,native_output,
        ordinary_staging,commit,rollback,fault,retired,count};
    std::array<std::uint64_t,static_cast<unsigned>(Reason::count)> counts{};
    std::uintptr_t base{};AuxiliaryViewLayout layout{};AuxiliaryPostfxKey key{};HistoryCurrent current{};
    HistoryCorrectionSnapshot initial{};D3DSURFACE_DESC desc{};
    Com<IDirect3DSurface9> previous,next,rollback,native_surface;
    Com<IDirect3DTexture9> texture;Com<IDirect3DDevice9> retained_device;
    IDirect3DDevice9* device{};std::array<RECT,2> rectangles{};
    std::uint64_t serial{},epoch{},last_serial{},last_epoch{},last_generation{},last_producer{},transactions{},cold_seeds{};
    std::uint32_t last_time{},previous_time{},last_texture{};AuxiliaryViewLayout last_layout{};AuxiliaryTarget last_history{};
    bool valid{},active{},failed{},touched{},cold{},latched{},native_open{},sampled{},retire_requested{},scalar_scope_expired{};
    unsigned operations{};
    HistoryScalarOwnership<std::uint8_t> blur_scalar;
    std::array<HistoryScalarOwnership<std::uint32_t>,6> scalars{};
    unsigned expected_eye=1,current_eye{};std::uint8_t mask{};std::array<std::uint32_t,5> installed_uv{};
    Reason reason=Reason::none;HRESULT failure_hr=S_OK;
    bool alive() const noexcept {return !retire_requested&&key.owner==GetCurrentThreadId()&&current.valid();}
    void log(Reason value,const char* label,HRESULT hr=S_OK) noexcept {
        const auto n=++counts[static_cast<unsigned>(value)];
        if(n<=3||n%240==0)trace("t5_history_correction","reason=%s count=%llu producer=%llu generation=%llu xr_frame=%llu backend=%llu native_frame=%u cold_seed=%u touched=%u hr=0x%08lx",
            label,n,key.producer,key.generation,key.xr_frame,serial,layout.frame,cold,touched,static_cast<unsigned long>(hr));
    }
    void fail(Reason value,const char* label,HRESULT hr=E_FAIL) noexcept {
        valid=false;if(!failed){failed=true;reason=value;failure_hr=hr;log(value,label,hr);}
    }
    bool read(unsigned selected,HistoryCorrectionSnapshot* out) noexcept {
        if(!alive()||!read_history_correction(base,layout,selected,out))return false;
        return out->native.postfx.views==initial.native.postfx.views&&out->native.postfx.targets==initial.native.postfx.targets&&
            out->custom==initial.custom&&out->forced_dvar==initial.forced_dvar&&out->forced==initial.forced&&
            out->texture==initial.texture&&remainder_equal(out->native.postfx,initial.native.postfx);
    }
    void release_borrow() noexcept {native_surface.reset();texture.reset();}
    bool pending_scalars() const noexcept {
        if(blur_scalar.pending)return true;for(const auto& field:scalars)if(field.pending)return true;return false;
    }
    bool restore_scalars() noexcept {
        if(!pending_scalars())return true;
        const bool same=!scalar_scope_expired&&same_scalar_scope(base,layout,initial);bool restored=blur_scalar.restore(same);
        for(auto& field:scalars)if(!field.restore(same))restored=false;
        if(!restored){latched=true;fail(Reason::scalar,"scalar_cleanup_pending");}return restored;
    }
    bool install_scalars(std::uint32_t expected_time,const std::array<std::uint32_t,5>& expected,
                         std::uint32_t time,const std::array<std::uint32_t,5>& selected) noexcept {
        if(pending_scalars())return false;
        if(!scalars[0].install(base+0x356a32c,expected_time,time))return false;
        for(unsigned i=0;i<5;++i)if(!scalars[i+1].install(base+0x356a33c+4*i,expected[i],selected[i]))return false;
        return true;
    }
    void release_resources() noexcept {
        release_borrow();previous.reset();next.reset();rollback.reset();retained_device.reset();
    }
    // A reset may reenter from an outbound COM call. Keep State and its COM
    // arguments alive until the outer mod operation returns normally.
    struct Operation {
        State& s;explicit Operation(State& v):s(v){++s.operations;}
        ~Operation(){if(s.operations==1&&s.retire_requested){s.restore_scalars();s.release_resources();s.active=false;s.current={};}--s.operations;}
    };
    HRESULT borrow(const HistoryCorrectionSnapshot& snapshot) {
        const auto p=snapshot.native.postfx.targets[0].color;if(!p)return E_POINTER;
        native_surface.p=reinterpret_cast<IDirect3DSurface9*>(p);native_surface.p->AddRef();
        HistoryCorrectionSnapshot acquired{};if(!read(2,&acquired)||acquired!=snapshot)return E_ABORT;
        Com<IDirect3DDevice9> owner;auto hr=native_surface.p->GetDevice(owner.put());
        if(SUCCEEDED(hr)&&!identity(device,owner.p))hr=E_INVALIDARG;
        D3DSURFACE_DESC d{};if(SUCCEEDED(hr))hr=native_surface.p->GetDesc(&d);
        const auto& t=snapshot.native.postfx.targets[0];
        if(SUCCEEDED(hr)&&(!supported(d)||d.Width!=t.width||d.Height!=t.height))hr=D3DERR_INVALIDCALL;
        if(FAILED(hr))return hr;
        // GetDevice/QI/GetDesc are outbound calls. Acquire no raw parent texture
        // after any of them has invalidated this exact native tuple.
        if(!read(2,&acquired)||acquired!=snapshot)return E_ABORT;
        auto* untyped=reinterpret_cast<IUnknown*>(snapshot.texture);
        hr=untyped->QueryInterface(IID_IDirect3DTexture9,reinterpret_cast<void**>(texture.put()));
        if(FAILED(hr)||!texture.p)return FAILED(hr)?hr:E_POINTER;
        if(!read(2,&acquired)||acquired!=snapshot)return E_ABORT;
        Com<IDirect3DSurface9> level;hr=texture.p->GetSurfaceLevel(0,level.put());
        if(SUCCEEDED(hr)&&(!level.p||!identity(level.p,native_surface.p)))hr=E_INVALIDARG;
        if(FAILED(hr))return hr;
        for(unsigned eye=0;eye<2;++eye){const auto& r=layout.slots[eye].scene;
            if(r.x!=static_cast<int>(eye*d.Width/2)||r.y||r.width!=static_cast<int>(d.Width/2)||r.height!=static_cast<int>(d.Height))return E_INVALIDARG;
            rectangles[eye]={r.x,0,r.x+r.width,r.height};}
        if(!description(desc,d)||device!=retained_device.p){previous.reset();next.reset();rollback.reset();retained_device.reset();valid=false;desc=d;
            device->AddRef();retained_device.p=device;}
        return alive()?S_OK:E_ABORT;
    }
    HRESULT allocate() {
        for(auto* p:{&previous,&next,&rollback})if(!p->p){if(!alive())return E_ABORT;const auto hr=device->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,p->put(),nullptr);
            if(FAILED(hr)||!p->p)return FAILED(hr)?hr:E_POINTER;}
        return S_OK;
    }
    HRESULT copy(IDirect3DSurface9* from,IDirect3DSurface9* to,const RECT* rectangle=nullptr) {
        if(!alive())return E_ABORT;
        return device->StretchRect(from,rectangle,to,rectangle,D3DTEXF_NONE);
    }
};
AuxiliaryHistoryCorrection::AuxiliaryHistoryCorrection() noexcept=default;
AuxiliaryHistoryCorrection::~AuxiliaryHistoryCorrection()=default;
void AuxiliaryHistoryCorrection::begin(bool enabled,std::uintptr_t base,const AuxiliaryViewLayout& layout,const AuxiliaryPostfxKey& key,
        std::uint64_t serial,std::uint64_t epoch,IDirect3DDevice9* device,HistoryCurrent current) noexcept {
    if(!enabled)return;Error error;
    try {
        if(key.owner!=GetCurrentThreadId()||!current.valid())return;
        if(state_&&state_->operations){state_->retire_requested=true;state_->valid=false;state_->failed=true;aborted_invocation_=true;return;}
        if(state_&&state_->epoch!=epoch)state_->scalar_scope_expired=true;
        if(state_&&state_->retire_requested&&!state_->pending_scalars())state_.reset();
        if(!state_)state_=std::make_unique<State>();auto& s=*state_;State::Operation operation(s);
        if(s.pending_scalars()&&!s.restore_scalars())return;
        s.scalar_scope_expired=false; // Only the now-retired old journal expired.
        aborted_invocation_=false;
        if(s.native_open){native_fault();return;}
        s.release_borrow();s.active=false;s.failed=false;s.touched=false;s.reason=State::Reason::none;s.failure_hr=S_OK;
        s.base=base;s.layout=layout;s.key=key;s.serial=serial;s.epoch=epoch;s.current=current;s.device=device;s.mask=0;s.expected_eye=1;
        if(s.latched){s.valid=false;s.log(State::Reason::retired,"latched_until_owner_reset");return;}
        HistoryCorrectionSnapshot snap{};
        if(!device||!key.producer||!key.generation||!key.xr_frame||!serial||!s.alive()||!read_history_correction(base,layout,2,&snap)){
            s.fail(State::Reason::context,"context_copy");return;}
        const auto now=snap.native.postfx.views[0].scene_def[0];
        if(history_kind(snap.native.postfx)!=HistoryKind::active||!now||now>INT32_MAX){s.fail(State::Reason::unsupported,"inactive_or_unsupported_effect");return;}
        if(snap.forced||custom_history_path(snap.custom[0])||custom_history_path(snap.custom[1])){s.fail(State::Reason::custom,"custom_postprocessor");return;}
        s.initial=snap;
        auto hr=s.borrow(snap);if(FAILED(hr)){s.fail(State::Reason::borrow,"native_history_borrow",hr);s.release_borrow();return;}
        hr=s.allocate();if(FAILED(hr)){s.fail(State::Reason::allocation,"private_allocation",hr);s.release_borrow();return;}
        HistoryCorrectionSnapshot after{};
        if(!s.read(2,&after)||after!=snap){s.fail(State::Reason::preflight,"preflight_changed");s.release_borrow();return;}
        s.cold=!(s.valid&&s.last_serial+1==serial&&s.last_epoch==epoch&&s.last_generation==key.generation&&
            s.last_producer<key.producer&&s.last_time<now&&snap.native.postfx.saved_times[0]==s.last_time&&uv(snap.native.postfx)==full_uv&&
            s.last_history==snap.native.postfx.targets[0]&&s.last_texture==snap.texture&&
            s.last_layout.targets==layout.targets&&s.last_layout.slots[0].scene==layout.slots[0].scene&&s.last_layout.slots[1].scene==layout.slots[1].scene);
        s.previous_time=s.last_time;s.valid=false;s.active=true;++s.transactions;
        s.sampled=s.transactions<=12||s.transactions%120==0||(s.cold&&++s.cold_seeds<=3);
        if(s.sampled)s.log(State::Reason::none,s.cold?"cold_seed_ready":"warm_ready");
    }catch(...){if(state_){State::Operation operation(*state_);state_->fail(State::Reason::preflight,"prepare_exception");state_->active=false;state_->release_borrow();}}
}
void AuxiliaryHistoryCorrection::before_native(unsigned eye) noexcept {
    if(!state_||!state_->active||state_->failed||state_->key.owner!=GetCurrentThreadId())return;Error error;auto& s=*state_;State::Operation operation(s);
    try {
        if(eye!=s.expected_eye||s.native_open){s.fail(State::Reason::order,"before_order");return;}
        HistoryCorrectionSnapshot before{};if(!s.read(eye?2:0,&before)){s.fail(State::Reason::context,"before_context");return;}
        s.current_eye=eye;
        if(s.cold){
            // Save remains enabled. Native positive-effects predicate therefore
            // still reaches both ordinary saves during the one-frame seed.
            s.touched=true;
            if(!s.blur_scalar.install(s.layout.slots[eye].address+0x21fc,1,0)){s.fail(State::Reason::scalar,"cold_input_changed");s.restore_scalars();return;}
        }else{
            s.touched=true;auto hr=s.copy(s.previous.p,s.native_surface.p);
            HistoryCorrectionSnapshot after{};
            if(FAILED(hr)||!s.read(eye?2:0,&after)||after!=before){s.fail(State::Reason::copy,"install_previous",FAILED(hr)?hr:E_ABORT);return;}
            auto selected=full_uv;selected[0]=bits(static_cast<float>(s.rectangles[eye].left)/static_cast<float>(s.desc.Width));
            s.installed_uv=selected;
            if(!s.install_scalars(before.native.postfx.saved_times[0],uv(before.native.postfx),s.previous_time,selected)){
                s.fail(State::Reason::scalar,"install_previous_scalar");s.restore_scalars();return;}
        }
        s.native_open=true;
        if(s.sampled)trace("t5_history_correction_eye","producer=%llu generation=%llu xr_frame=%llu native_frame=%u eye=%u cold_seed=%u previous_time=%u native_before_time=%u installed_time=%u scene_time=%u origin_s_bits=0x%08x span_s_bits=0x3f800000 rectangle=%ld,%ld,%ld,%ld",
            s.key.producer,s.key.generation,s.key.xr_frame,s.layout.frame,eye,s.cold,s.previous_time,before.native.postfx.saved_times[0],s.cold?before.native.postfx.saved_times[0]:s.previous_time,s.initial.native.postfx.views[eye].scene_def[0],s.cold?0:s.installed_uv[0],
            s.rectangles[eye].left,s.rectangles[eye].top,s.rectangles[eye].right,s.rectangles[eye].bottom);
    }catch(...){s.fail(State::Reason::copy,"before_exception");s.restore_scalars();}
}
void AuxiliaryHistoryCorrection::after_native(unsigned eye) noexcept {
    if(!state_||!state_->active||state_->key.owner!=GetCurrentThreadId())return;Error error;auto& s=*state_;State::Operation operation(s);s.restore_scalars();
    if(s.failed)return;
    try {
        if(!s.native_open||eye!=s.current_eye){s.fail(State::Reason::order,"after_order");return;}s.native_open=false;
        HistoryCorrectionSnapshot before{};
        if(!s.read(eye?2:0,&before)||before.native.postfx.saved_times[0]!=s.initial.native.postfx.views[eye].scene_def[0]||uv(before.native.postfx)!=full_uv){
            s.fail(State::Reason::native_output,"native_save_output");return;}
        auto hr=s.copy(s.native_surface.p,s.next.p,&s.rectangles[eye]);HistoryCorrectionSnapshot after{};
        if(FAILED(hr)||!s.read(eye?2:0,&after)||after!=before){s.fail(State::Reason::copy,"capture_successor",FAILED(hr)?hr:E_ABORT);return;}
        s.mask|=static_cast<std::uint8_t>(1u<<eye);s.expected_eye=0;
    }catch(...){s.native_open=false;s.fail(State::Reason::copy,"after_exception");}
}
bool AuxiliaryHistoryCorrection::finish(bool ordinary_staging_complete) noexcept {
    if(state_&&state_->key.owner!=GetCurrentThreadId())return false;
    if(aborted_invocation_){aborted_invocation_=false;return false;}
    if(!state_||!state_->active)return ordinary_staging_complete;
    if(state_->key.owner!=GetCurrentThreadId())return false;Error error;auto& s=*state_;State::Operation operation(s);s.restore_scalars();
    const bool touched=s.touched;
    try {
        if(!ordinary_staging_complete||s.mask!=3||s.native_open)s.fail(State::Reason::ordinary_staging,"ordinary_pair_incomplete");
        HistoryCorrectionSnapshot before{};
        if(!s.failed&&(!s.read(2,&before)||before.native.postfx.saved_times[0]!=s.initial.native.postfx.views[0].scene_def[0]||uv(before.native.postfx)!=full_uv))
            s.fail(State::Reason::context,"commit_context");
        if(!s.failed){
            auto hr=s.copy(s.native_surface.p,s.rollback.p);HistoryCorrectionSnapshot after{};
            if(FAILED(hr)||!s.read(2,&after)||after!=before)s.fail(State::Reason::copy,"commit_backup",FAILED(hr)?hr:E_ABORT);
            else {
                hr=s.copy(s.next.p,s.native_surface.p);
                if(FAILED(hr)||!s.read(2,&after)||after!=before){
                    s.fail(State::Reason::commit,"commit_failed",FAILED(hr)?hr:E_ABORT);
                    // One bounded rollback, only while the exact borrowed
                    // native tuple and owner context still belong to us.
                    if(s.read(2,&after)&&after==before){const auto restore=s.copy(s.rollback.p,s.native_surface.p);
                        if(FAILED(restore)||!s.read(2,&after)||after!=before){s.latched=true;s.log(State::Reason::rollback,"rollback_failed",restore);}
                        else s.log(State::Reason::rollback,"rollback_native_backup",restore);
                    }else {s.latched=true;s.log(State::Reason::rollback,"rollback_context_lost",E_ABORT);}
                }else {
                    std::swap(s.previous.p,s.next.p);s.valid=true;s.last_serial=s.serial;s.last_epoch=s.epoch;s.last_generation=s.key.generation;
                    s.last_producer=s.key.producer;s.last_time=before.native.postfx.saved_times[0];s.last_layout=s.layout;
                    s.last_history=before.native.postfx.targets[0];s.last_texture=before.texture;
                    if(s.sampled)trace("t5_history_correction_committed","producer=%llu generation=%llu xr_frame=%llu native_frame=%u cold_seed=%u saved_time=%u eyes=3 target=1 complete_after_ordinary_staging=1",
                        s.key.producer,s.key.generation,s.key.xr_frame,s.layout.frame,s.cold,s.last_time);
                }
            }
        }
    }catch(...){s.latched=true;s.fail(State::Reason::commit,"finish_exception");}
    s.active=false;s.native_open=false;s.current={};s.release_borrow();return ordinary_staging_complete&&(!touched||!s.failed)&&!s.retire_requested;
}
void AuxiliaryHistoryCorrection::native_fault() noexcept {
    if(!state_||state_->key.owner!=GetCurrentThreadId())return;Error error;auto& s=*state_;s.restore_scalars();s.native_open=false;s.active=false;s.valid=false;s.latched=true;
    s.current={};s.fail(State::Reason::fault,"native_unwind_resources_deferred");
}
void AuxiliaryHistoryCorrection::retire(std::uint64_t reset_epoch) noexcept {
    if(!state_||state_->key.owner!=GetCurrentThreadId())return;Error error;
    auto& s=*state_;if(s.active||s.native_open)aborted_invocation_=true;
    if(reset_epoch&&reset_epoch!=s.epoch)s.scalar_scope_expired=true;
    s.valid=false;s.failed=true;s.retire_requested=true;
    if(s.operations)return;
    {State::Operation operation(s);s.restore_scalars();}
    if(!s.pending_scalars())state_.reset();
}
bool AuxiliaryHistoryCorrection::selected() const noexcept {return state_&&state_->active;}
void restore_history_index(std::uint32_t* index,std::uint32_t value) noexcept {restore_index(index,value);}
void invoke_history_original(void(__cdecl* original)(),AuxiliaryHistoryCorrection* correction,AuxiliaryHistoryCapture* diagnostic,
                             bool* native_fault,std::atomic<bool>* stop,std::uint32_t* index,std::uint32_t original_index) {
    __try {original();}
    __finally {if(AbnormalTermination()){
        *native_fault=true;if(stop)stop->store(true);if(correction)correction->native_fault();if(diagnostic)diagnostic->abandon_native_fault();
        restore_index(index,original_index);
    }}
}
#include "t5/auxiliary_history_correction_guards.inc"
}
