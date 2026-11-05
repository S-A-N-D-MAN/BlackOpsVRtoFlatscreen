// SPDX-License-Identifier: GPL-3.0-only
#include "loader/d3d9_observer.hpp"
#include "loader/d3d9ex_policy.hpp"
#include "loader/native_resolution.hpp"
#include "loader/trace.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <intrin.h>

namespace bovr {
namespace {
// The original COM objects and IUnknown identity remain intact. A pointer-sized
// compare/exchange changes only documented interface slots. No instruction bytes
// or T4/T5 globals are patched. Records live until process exit and the proxy pins
// itself before installation. Never hold this lock while calling any COM method.
struct Entry { void** table{}; unsigned slot{}; void* original{}; void* detour{}; };
std::array<Entry, 256> entries{};
unsigned count{};
SRWLOCK registry_lock = SRWLOCK_INIT;
std::atomic<bool> active{false};
std::atomic<bool> recovery_diagnostics{false};
std::atomic<std::uint64_t> recovery_attempts{};
std::atomic<std::uint64_t> device_frames{}, swap_frames{}, ex_frames{}, resets{};
GraphicsCallbacks callbacks{};
SRWLOCK callback_lock = SRWLOCK_INIT;
std::atomic<unsigned> callbacks_in_flight{};
thread_local unsigned callback_depth{};
IndexedDrawCallbacks indexed_callbacks{};
SRWLOCK indexed_callback_lock=SRWLOCK_INIT;
std::atomic<unsigned> indexed_in_flight{};
std::atomic<bool> indexed_enabled{};
thread_local unsigned indexed_depth{};
thread_local std::uint64_t indexed_reentries{};
struct CallbackLease {
    GraphicsCallbacks value{};
    bool held{};
    explicit CallbackLease(bool eligible = true) noexcept {
        AcquireSRWLockShared(&callback_lock);
        if (eligible && active.load()) { value = callbacks; ++callbacks_in_flight; held = true; }
        ReleaseSRWLockShared(&callback_lock);
    }
    ~CallbackLease() { if (held) --callbacks_in_flight; }
    CallbackLease(const CallbackLease&) = delete;
    CallbackLease& operator=(const CallbackLease&) = delete;
};
struct CallbackScope {
    CallbackScope() noexcept { ++callback_depth; }
    ~CallbackScope() { --callback_depth; }
};
thread_local unsigned present_depth{};
thread_local bool present_notified{};
thread_local unsigned explicit_ex_present_depth{};
struct ExplicitExPresentScope {
    ExplicitExPresentScope() noexcept { ++explicit_ex_present_depth; }
    ~ExplicitExPresentScope() { --explicit_ex_present_depth; }
};
struct PresentScope {
    PresentScope() noexcept { if (!present_depth++) present_notified = false; }
    ~PresentScope() { if (!--present_depth) present_notified = false; }
};
struct RecoveryRecord {
    std::uint64_t attempt{},cooperative_calls{};
    HRESULT last_cooperative{};
    unsigned cooperative_budget{};
    bool has_cooperative{};
};
struct LegacyPresentation {
    bool promoted{},mode_valid{},windowed{};
    std::uint64_t reset_generation{},translations{};
};
struct DeviceRecord { IDirect3DDevice9* device{}; IDirect3DSwapChain9* implicit_swap{}; DeviceCreationInfo creation{}; RecoveryRecord recovery{}; LegacyPresentation presentation{}; };
std::array<DeviceRecord, 128> devices{};
std::uint64_t device_creation_serial{};
SRWLOCK device_lock = SRWLOCK_INIT;
void record_device_creation(IDirect3DDevice9* device, bool extended) noexcept {
    AcquireSRWLockExclusive(&device_lock);
    for (auto& entry : devices) if (entry.device == device) {
        // An existing retained identity cannot be made new by re-observation.
        if (!entry.creation.serial && device_creation_serial != UINT64_MAX)
            entry.creation = {++device_creation_serial, GetCurrentThreadId(), extended};
        break;
    }
    ReleaseSRWLockExclusive(&device_lock);
}
struct PresentModeCopy { bool valid{},windowed{}; };
PresentModeCopy copy_present_mode(const D3DPRESENT_PARAMETERS* parameters) noexcept {
    PresentModeCopy result{};
    __try { if(parameters) { result.windowed=parameters->Windowed!=FALSE;result.valid=true; } }
    __except(EXCEPTION_EXECUTE_HANDLER) { result={}; }
    return result;
}
// Only the legacy CreateDevice -> CreateDeviceEx bridge calls this after a
// successful, identity-verified creation. Direct CreateDeviceEx never opts in.
void record_legacy_presentation(IDirect3DDevice9* device,PresentModeCopy mode) noexcept {
    AcquireSRWLockExclusive(&device_lock);
    for(auto& entry:devices)if(entry.device==device&&entry.creation.serial&&entry.creation.extended) {
        if(!entry.presentation.promoted)entry.presentation={true,mode.valid,mode.windowed,0,0};
        break;
    }
    ReleaseSRWLockExclusive(&device_lock);
}
struct PresentResetToken { std::uint64_t creation{},generation{}; };
PresentResetToken begin_legacy_reset(IDirect3DDevice9* device) noexcept {
    PresentResetToken token{};
    AcquireSRWLockExclusive(&device_lock);
    for(auto& entry:devices)if(entry.device==device&&entry.presentation.promoted) {
        auto& state=entry.presentation;state.mode_valid=false;
        if(state.reset_generation<UINT64_MAX-1)token={entry.creation.serial,++state.reset_generation};
        else state.reset_generation=UINT64_MAX;
        break;
    }
    ReleaseSRWLockExclusive(&device_lock);return token;
}
void finish_legacy_reset(IDirect3DDevice9* device,PresentResetToken token,HRESULT result,PresentModeCopy mode) noexcept {
    AcquireSRWLockExclusive(&device_lock);
    for(auto& entry:devices)if(entry.device==device&&entry.presentation.promoted) {
        auto& state=entry.presentation;
        if(token.generation&&token.creation==entry.creation.serial&&token.generation==state.reset_generation&&SUCCEEDED(result)) {
            state.mode_valid=mode.valid;state.windowed=mode.windowed;
        } else if(token.creation==entry.creation.serial) {
            // A reentrant/overlapping older Reset may finish after a newer one.
            // Its final native mode is no longer ordered by our begin token.
            // Retire proof; never retain the newer mode or publish the old one.
            state.mode_valid=false;
        }
        break;
    }
    ReleaseSRWLockExclusive(&device_lock);
}
HRESULT legacy_present_result(IDirect3DDevice9* device,IDirect3DSwapChain9* swap,HRESULT result) noexcept {
    if(result!=S_PRESENT_MODE_CHANGED||explicit_ex_present_depth)return result;
    IDirect3DDevice9* owner{};std::uint64_t serial{},translated_count{};
    AcquireSRWLockExclusive(&device_lock);
    for(auto& entry:devices)if((device&&entry.device==device)||(swap&&entry.implicit_swap==swap)) {
        auto& state=entry.presentation;
        if(entry.creation.serial&&entry.creation.extended&&state.promoted&&state.mode_valid&&state.windowed) {
            owner=entry.device;serial=entry.creation.serial;
            if(state.translations!=UINT64_MAX)++state.translations;
            translated_count=state.translations;
        }
        break;
    }
    ReleaseSRWLockExclusive(&device_lock);
    if(!owner)return result;
    // Ex documents this as a continuing-render status, not a lost device. The
    // retail legacy caller treats every nonzero Present result as fatal. Keep
    // its D3D9 contract for this exact promoted-windowed status only. No Reset,
    // retries, desktop changes, failure masking, or explicit Ex translation.
    if(translated_count<=8)trace("present_legacy_status","device=%p swap=%p creation=%llu count=%llu original_hr=0x%08lx returned_hr=0x%08lx translated=1 promoted=1 windowed=1 original_calls=1",
        owner,swap,serial,translated_count,static_cast<unsigned long>(result),static_cast<unsigned long>(D3D_OK));
    return D3D_OK;
}
bool track_device(IDirect3DDevice9* device) noexcept {
    bool stored{};
    AcquireSRWLockExclusive(&device_lock);
    for (const auto& entry : devices) if (entry.device == device) { stored = true; break; }
    if (!stored) for (auto& entry : devices) if (!entry.device) { entry.device = device; stored = true; break; }
    ReleaseSRWLockExclusive(&device_lock); return stored;
}
bool tracked_device(void* device) noexcept {
    bool found{};
    AcquireSRWLockShared(&device_lock);
    for (const auto& entry : devices) if (entry.device == device) { found = true; break; }
    ReleaseSRWLockShared(&device_lock); return found;
}
void set_implicit_swap(IDirect3DDevice9* device, IDirect3DSwapChain9* swap) noexcept {
    AcquireSRWLockExclusive(&device_lock);
    for (auto& entry : devices) if (entry.device == device) { entry.implicit_swap = swap; break; }
    ReleaseSRWLockExclusive(&device_lock);
}
bool implicit_swap(IDirect3DDevice9* device, IDirect3DSwapChain9* swap) noexcept {
    bool found{};
    AcquireSRWLockShared(&device_lock);
    for (const auto& entry : devices) if (entry.device == device && entry.implicit_swap == swap) { found = true; break; }
    ReleaseSRWLockShared(&device_lock); return found;
}
void forget_device(void* device) noexcept {
    AcquireSRWLockExclusive(&device_lock);
    for (auto& entry : devices) if (entry.device == device) entry = {};
    ReleaseSRWLockExclusive(&device_lock);
}
void pre_swap_present(IDirect3DSwapChain9* swap) noexcept {
    if (callback_depth || present_notified) return;
    CallbackLease lease;
    if (!lease.value.before_present) return;
    IDirect3DDevice9* device{};
    const HRESULT hr = swap->GetDevice(&device);
    if (SUCCEEDED(hr) && device) {
        if (implicit_swap(device, swap)) {
            present_notified = true; // Publish before user code can reenter Present.
            CallbackScope scope;
            try { lease.value.before_present(lease.value.context, device); }
            catch (...) { trace("graphics_callback_failed", "kind=before_present desktop_forwarding=1"); }
        }
        device->Release(); // GetDevice's reference is balanced even after a throw.
    } else {
        trace("graphics_callback_skipped", "kind=before_present reason=swap_get_device_failed hr=0x%08lx", static_cast<unsigned long>(hr));
    }
}
bool pre_reset(CallbackLease& lease, IDirect3DDevice9* device) noexcept {
    if (!lease.value.before_reset) return true;
    if (callback_depth) return false;
    CallbackScope scope;
    try { return lease.value.before_reset(lease.value.context, device); }
    catch (...) { trace("graphics_callback_failed", "kind=before_reset reset_deferred=1"); return false; }
}
void post_reset(CallbackLease& lease, IDirect3DDevice9* device, HRESULT hr) noexcept {
    if (!lease.value.after_reset) return;
    CallbackScope scope;
    try { lease.value.after_reset(lease.value.context, device, hr); }
    catch (...) { trace("graphics_callback_failed", "kind=after_reset original_hr=0x%08lx", static_cast<unsigned long>(hr)); }
}
// Unlike vtables, promotion is an object property. Shared driver vtables must not
// cause ordinary devices to receive Ex calls or managed-pool rewriting.
struct ExObject { void* object{}; bool factory{}; };
std::array<ExObject, 128> ex_objects{};
SRWLOCK ex_lock = SRWLOCK_INIT;
bool marked_ex(void* object, bool factory) noexcept {
    bool found{};
    AcquireSRWLockShared(&ex_lock);
    for (const auto& entry : ex_objects) if (entry.object == object && entry.factory == factory) { found = true; break; }
    ReleaseSRWLockShared(&ex_lock);
    return found;
}
bool mark_ex(void* object, bool factory) noexcept {
    bool stored{};
    AcquireSRWLockExclusive(&ex_lock);
    for (const auto& entry : ex_objects) if (entry.object == object && entry.factory == factory) { stored = true; break; }
    if (!stored) for (auto& entry : ex_objects) if (!entry.object) { entry = {object, factory}; stored = true; break; }
    ReleaseSRWLockExclusive(&ex_lock);
    return stored;
}
void forget_ex(void* object) noexcept {
    AcquireSRWLockExclusive(&ex_lock);
    for (auto& entry : ex_objects) if (entry.object == object) entry = {};
    ReleaseSRWLockExclusive(&ex_lock);
}

void** table_of(void* object) noexcept { return *static_cast<void***>(object); }
void* original(void* object, unsigned slot) noexcept {
    if (!object) return nullptr;
    void* result{}; auto** table = table_of(object);
    AcquireSRWLockShared(&registry_lock);
    for (unsigned i = 0; i < count; ++i)
        if (entries[i].table == table && entries[i].slot == slot) { result = entries[i].original; break; }
    ReleaseSRWLockShared(&registry_lock);
    return result;
}
bool install(void* object, unsigned slot, void* detour) noexcept {
    if (!object) return false;
    auto** table = table_of(object);
    AcquireSRWLockExclusive(&registry_lock);
    for (unsigned i = 0; i < count; ++i) {
        if (entries[i].table == table && entries[i].slot == slot) {
            const bool intact = table[slot] == detour;
            ReleaseSRWLockExclusive(&registry_lock);
            return intact;
        }
    }
    MEMORY_BASIC_INFORMATION info{};
    void* const before = table[slot];
    const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (count == entries.size() || !before || !VirtualQuery(before, &info, sizeof(info)) ||
        info.State != MEM_COMMIT || !(info.Protect & executable) || (info.Protect & PAGE_GUARD)) {
        ReleaseSRWLockExclusive(&registry_lock); return false;
    }
    DWORD old_protect{};
    if (!VirtualProtect(table + slot, sizeof(void*), PAGE_READWRITE, &old_protect)) {
        ReleaseSRWLockExclusive(&registry_lock); return false;
    }
    entries[count] = {table, slot, before, detour};
    ++count; // Publish the original before any caller can enter the detour.
    const bool installed = InterlockedCompareExchangePointer(table + slot, detour, before) == before;
    if (!installed) { --count; entries[count] = {}; }
    DWORD ignored{};
    const bool protected_again = VirtualProtect(table + slot, sizeof(void*), old_protect, &ignored) != FALSE;
    ReleaseSRWLockExclusive(&registry_lock);
    trace("com_slot", "table=%p slot=%u original=%p installed=%u protection_restored=%u",
        table, slot, before, installed, protected_again);
    return installed;
}
template<class T> T method(void* object, unsigned slot) noexcept {
    return reinterpret_cast<T>(original(object, slot));
}
using IndexedDrawFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
// POD lease: explicit finally owns retirement for both C++ and native SEH.
// No C++ destructor or noexcept boundary spans the original COM invocation.
struct IndexedLease { IndexedDrawCallbacks value{};bool held{}; };
IndexedLease acquire_indexed(IDirect3DDevice9* device) noexcept {
    IndexedLease lease{};
    if(indexed_depth) {
        // Saturation is poison, not a wraparound/reset permission. This runs
        // inside the wrapper's saved FP/LastError boundary before native entry.
        if(indexed_reentries!=indexed_draw_reentry_exhausted)++indexed_reentries;
        return lease;
    }
    if(!indexed_enabled.load(std::memory_order_acquire)||!active.load()||!tracked_device(device))return lease;
    AcquireSRWLockShared(&indexed_callback_lock);
    if(active.load()&&indexed_enabled.load(std::memory_order_relaxed)) {
        lease.value=indexed_callbacks;lease.held=true;++indexed_in_flight;++indexed_depth;
    }
    ReleaseSRWLockShared(&indexed_callback_lock);return lease;
}
std::uint64_t indexed_before(const IndexedDrawCallbacks& c,IDirect3DDevice9* d,const IndexedDrawCall& call) noexcept {
    __try {return c.before(c.context,d,call);}
    __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
void indexed_after(const IndexedDrawCallbacks& c,std::uint64_t token,HRESULT result) noexcept {
    __try {c.after(c.context,token,result);}
    __except(EXCEPTION_EXECUTE_HANDLER){}
}
void indexed_abandon(const IndexedDrawCallbacks& c,std::uint64_t token) noexcept {
    __try {c.abandon(c.context,token);}
    __except(EXCEPTION_EXECUTE_HANDLER){}
}
__declspec(noinline) HRESULT STDMETHODCALLTYPE indexed_draw(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,
    INT base_vertex,UINT min_vertex,UINT num_vertices,UINT start_index,UINT primitive_count) {
    // FXSAVE includes all x87 slots/control/status and XMM0..7/MXCSR. Restore
    // entry state before native; preserve native return state around callbacks.
    __declspec(align(16)) unsigned char entry_fp[512],exit_fp[512];
    _fxsave(entry_fp);const auto entry_error=GetLastError();
    const auto fn=method<IndexedDrawFn>(device,82);
    if(!fn){_fxrstor(entry_fp);SetLastError(entry_error);return D3DERR_INVALIDCALL;}
    const IndexedDrawCall call{reinterpret_cast<std::uintptr_t>(_ReturnAddress()),type,base_vertex,
        min_vertex,num_vertices,start_index,primitive_count};
    const auto lease=acquire_indexed(device);
    const auto token=lease.held?indexed_before(lease.value,device,call):0;
    const bool completion_required=lease.held&&token;
    HRESULT result{};DWORD exit_error{};bool returned=false;
    __try {
        _fxrstor(entry_fp);SetLastError(entry_error);
        result=fn(device,type,base_vertex,min_vertex,num_vertices,start_index,primitive_count);
        if(completion_required){_fxsave(exit_fp);exit_error=GetLastError();}
        returned=true;
        if(completion_required)indexed_after(lease.value,token,result);
    } __finally {
        if(!returned&&completion_required) {
            _fxsave(exit_fp);exit_error=GetLastError();
            indexed_abandon(lease.value,token);
        }
        // Keep the context lease through the last callback, including abandon.
        // No COM call, heap release or native retry occurs during unwinding.
        if(lease.held){--indexed_depth;--indexed_in_flight;}
        // Token zero permits no completion callback. Its return path performs
        // integer-only lease cleanup, preserving native FP/LastError in place.
        if(completion_required){_fxrstor(exit_fp);SetLastError(exit_error);}
    }
    return result;
}
bool sampled(std::uint64_t n, HRESULT result) noexcept { return n <= 6 || n % 300 == 0 || FAILED(result); }
struct ResetDiagnostic {
    std::uint64_t attempt{};
    DeviceCreationInfo creation{};
    D3DPRESENT_PARAMETERS parameters{};
    bool parameters_valid{},extended{};
};
bool copy_reset_parameters(D3DPRESENT_PARAMETERS* input,D3DPRESENT_PARAMETERS* output) noexcept {
    if(!input||!output)return false;
    __try {*output=*input;return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
ResetDiagnostic diagnostic_reset_begin(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS* parameters,
                                       bool extended,std::uintptr_t caller) noexcept {
    const auto saved=GetLastError();ResetDiagnostic result{};
    if(active.load()&&recovery_diagnostics.load()) {
        AcquireSRWLockExclusive(&device_lock);
        for(auto& item:devices)if(item.device==device) {
            result.attempt=++recovery_attempts;result.creation=item.creation;result.extended=extended;
            item.recovery.attempt=result.attempt;item.recovery.cooperative_budget=8;break;
        }
        ReleaseSRWLockExclusive(&device_lock);
        if(result.attempt) {
            result.parameters_valid=copy_reset_parameters(parameters,&result.parameters);
            trace("recovery_reset_attempt","attempt=%llu device=%p caller=%p thread=%lu creation=%llu creation_thread=%lu creation_ex=%u api_ex=%u promoted=%u parameters_valid=%u width=%u height=%u windowed=%u aa=%u hwnd=%p",
                  result.attempt,device,reinterpret_cast<void*>(caller),GetCurrentThreadId(),result.creation.serial,
                  result.creation.thread_id,result.creation.extended,extended,marked_ex(device,false),result.parameters_valid,
                  result.parameters.BackBufferWidth,result.parameters.BackBufferHeight,result.parameters.Windowed,
                  static_cast<unsigned>(result.parameters.MultiSampleType),result.parameters.hDeviceWindow);
        }
    }
    SetLastError(saved);return result;
}
void diagnostic_reset_end(IDirect3DDevice9* device,const ResetDiagnostic& point,bool admitted,HRESULT result) noexcept {
    if(!point.attempt)return;
    const auto saved=GetLastError();
    trace("recovery_reset_outcome","attempt=%llu device=%p thread=%lu callback_admitted=%u original_called=%u native_hr_valid=%u hr=0x%08lx requested_width=%u requested_height=%u status_override=0",
          point.attempt,device,GetCurrentThreadId(),admitted,admitted,admitted,
          static_cast<unsigned long>(result),point.parameters.BackBufferWidth,point.parameters.BackBufferHeight);
    SetLastError(saved);
}
using CooperativeFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
HRESULT STDMETHODCALLTYPE cooperative_level(IDirect3DDevice9* device) noexcept {
    const auto fn=method<CooperativeFn>(device,3);
    if(!fn)return D3DERR_INVALIDCALL;
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    // Exactly the engine-requested call. Do not add CheckDeviceState or any
    // other D3D operation after a failed ResetEx, and never synthesize a status.
    const auto result=fn(device);const auto saved=GetLastError();
    RecoveryRecord observed{};DeviceCreationInfo creation{};bool emit=false;
    if(active.load()&&recovery_diagnostics.load()) {
        AcquireSRWLockExclusive(&device_lock);
        for(auto& item:devices)if(item.device==device) {
            auto& state=item.recovery;++state.cooperative_calls;
            emit=state.cooperative_calls<=3||state.cooperative_budget||!state.has_cooperative||state.last_cooperative!=result;
            if(state.cooperative_budget)--state.cooperative_budget;
            state.has_cooperative=true;state.last_cooperative=result;observed=state;creation=item.creation;break;
        }
        ReleaseSRWLockExclusive(&device_lock);
        if(emit)trace("recovery_cooperative","device=%p caller=%p thread=%lu creation=%llu creation_thread=%lu creation_ex=%u call=%llu latest_observed_reset_attempt=%llu original_hr=0x%08lx status_override=0 original_calls=1",
            device,reinterpret_cast<void*>(caller),GetCurrentThreadId(),creation.serial,creation.thread_id,creation.extended,
            observed.cooperative_calls,observed.attempt,static_cast<unsigned long>(result));
    }
    SetLastError(saved);return result;
}
using ReleaseFn = ULONG(STDMETHODCALLTYPE*)(IUnknown*);
using CreateFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                            D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using CreateExFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD,
                                              D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
using SwapGetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, IDirect3DSwapChain9**);
using SwapCreateFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9**);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using SwapPresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
using PresentExFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using ResetExFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*);

ULONG STDMETHODCALLTYPE release(IUnknown* object) noexcept {
    const auto fn = method<ReleaseFn>(object, 2);
    if (!fn) return 0; // Cannot occur for an installed slot: original was published first.
    void* const identity = object;
    const bool was_device = tracked_device(identity);
    const ULONG refs = fn(object);
    if (!refs) {
        forget_ex(identity);
        if (was_device) {
            forget_device(identity);
            CallbackLease lease;
            if (lease.value.device_released) {
                CallbackScope scope;
                try { lease.value.device_released(lease.value.context, reinterpret_cast<std::uintptr_t>(identity)); }
                catch (...) { trace("graphics_callback_failed", "kind=device_released"); }
            }
        }
    }
    if (!refs && active.load()) trace("com_release_zero", "object=%p", identity);
    return refs;
}
HRESULT STDMETHODCALLTYPE get_swap(IDirect3DDevice9* device, UINT index, IDirect3DSwapChain9** output) noexcept {
    const auto fn = method<SwapGetFn>(device, 14);
    if (!fn) return D3DERR_INVALIDCALL;
    const HRESULT hr = fn(device, index, output);
    if (SUCCEEDED(hr) && output && *output && active.load()) {
        if (index == 0) set_implicit_swap(device, *output);
        observe_swapchain(*output);
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE create_swap(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* p, IDirect3DSwapChain9** output) noexcept {
    const auto fn = method<SwapCreateFn>(device, 13);
    if (!fn) return D3DERR_INVALIDCALL;
    const HRESULT hr = fn(device, p, output);
    if (SUCCEEDED(hr) && output && *output && active.load()) observe_swapchain(*output);
    return hr;
}
HRESULT STDMETHODCALLTYPE present(IDirect3DDevice9* device, const RECT* a, const RECT* b, HWND window, const RGNDATA* dirty) noexcept {
    const auto fn = method<PresentFn>(device, 17);
    if (!fn) return D3DERR_INVALIDCALL;
    PresentScope scope;
    const auto n = ++device_frames;
    const HRESULT hr = fn(device, a, b, window, dirty);
    const DWORD saved=GetLastError();
    const HRESULT result=legacy_present_result(device,nullptr,hr);
    if (active.load() && sampled(n, hr)) trace("device_present", "serial=%llu object=%p hr=0x%08lx", n, device, static_cast<unsigned long>(hr));
    SetLastError(saved);return result;
}
HRESULT STDMETHODCALLTYPE swap_present(IDirect3DSwapChain9* swap, const RECT* a, const RECT* b, HWND window, const RGNDATA* dirty, DWORD flags) noexcept {
    const auto fn = method<SwapPresentFn>(swap, 3);
    if (!fn) return D3DERR_INVALIDCALL;
    PresentScope scope;
    const auto n = ++swap_frames;
    pre_swap_present(swap);
    const HRESULT hr = fn(swap, a, b, window, dirty, flags);
    const DWORD saved=GetLastError();
    const HRESULT result=legacy_present_result(nullptr,swap,hr);
    if (active.load() && sampled(n, hr)) trace("swapchain_present", "serial=%llu object=%p flags=0x%lx hr=0x%08lx", n, swap, flags, static_cast<unsigned long>(hr));
    SetLastError(saved);return result;
}
HRESULT STDMETHODCALLTYPE present_ex(IDirect3DDevice9Ex* device, const RECT* a, const RECT* b, HWND window, const RGNDATA* dirty, DWORD flags) noexcept {
    const auto fn = method<PresentExFn>(device, 121);
    if (!fn) return D3DERR_INVALIDCALL;
    PresentScope scope;
    ExplicitExPresentScope explicit_scope;
    const auto n = ++ex_frames;
    const HRESULT hr = fn(device, a, b, window, dirty, flags);
    const DWORD saved=GetLastError();
    if (active.load() && sampled(n, hr)) trace("device_present_ex", "serial=%llu object=%p hr=0x%08lx", n, device, static_cast<unsigned long>(hr));
    SetLastError(saved);return hr;
}
void reacquire_swap(IDirect3DDevice9* device) noexcept {
    IDirect3DSwapChain9* swap{};
    if (SUCCEEDED(device->GetSwapChain(0, &swap)) && swap) { observe_swapchain(swap); swap->Release(); }
}
HRESULT STDMETHODCALLTYPE reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* p) noexcept {
    const auto fn = method<ResetFn>(device, 16);
    if (!fn) return D3DERR_INVALIDCALL;
    const auto present_mode=copy_present_mode(p);
    const auto present_token=begin_legacy_reset(device);
    const auto diagnostic=diagnostic_reset_begin(device,p,false,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    CallbackLease lease(tracked_device(device));
    if (!pre_reset(lease, device)) {
        diagnostic_reset_end(device,diagnostic,false,D3DERR_DEVICELOST);
        trace("reset_deferred", "object=%p original_called=0 hr=0x%08lx", device, static_cast<unsigned long>(D3DERR_DEVICELOST));
        return D3DERR_DEVICELOST;
    }
    set_implicit_swap(device, nullptr);
    const auto n = ++resets;
    if (active.load()) trace("reset_begin", "serial=%llu object=%p width=%u height=%u windowed=%u", n, device,
        p ? p->BackBufferWidth : 0, p ? p->BackBufferHeight : 0, p ? p->Windowed : 0);
    HRESULT hr{};
    if (marked_ex(device, false)) {
        const auto ex_fn = method<ResetExFn>(device, 132);
        auto mode = p ? ex_display_mode(*p) : D3DDISPLAYMODEEX{};
        hr = ex_fn ? ex_fn(static_cast<IDirect3DDevice9Ex*>(device), p, p && !p->Windowed ? &mode : nullptr) : D3DERR_INVALIDCALL;
        trace("reset_promoted_ex", "serial=%llu hr=0x%08lx", n, static_cast<unsigned long>(hr));
    } else {
        hr = fn(device, p);
    }
    finish_legacy_reset(device,present_token,hr,present_mode);
    post_reset(lease, device, hr);
    diagnostic_reset_end(device,diagnostic,true,hr);
    if (active.load()) {
        trace("reset_end", "serial=%llu object=%p hr=0x%08lx", n, device, static_cast<unsigned long>(hr));
        if (SUCCEEDED(hr)) reacquire_swap(device);
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE reset_ex(IDirect3DDevice9Ex* device, D3DPRESENT_PARAMETERS* p, D3DDISPLAYMODEEX* mode) noexcept {
    const auto fn = method<ResetExFn>(device, 132);
    if (!fn) return D3DERR_INVALIDCALL;
    const auto present_mode=copy_present_mode(p);
    const auto present_token=begin_legacy_reset(device);
    const auto diagnostic=diagnostic_reset_begin(device,p,true,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    CallbackLease lease(tracked_device(device));
    if (!pre_reset(lease, device)) {
        diagnostic_reset_end(device,diagnostic,false,D3DERR_DEVICELOST);
        trace("reset_deferred", "object=%p original_called=0 ex=1 hr=0x%08lx", device, static_cast<unsigned long>(D3DERR_DEVICELOST));
        return D3DERR_DEVICELOST;
    }
    set_implicit_swap(device, nullptr);
    const HRESULT hr = fn(device, p, mode);
    finish_legacy_reset(device,present_token,hr,present_mode);
    post_reset(lease, device, hr);
    diagnostic_reset_end(device,diagnostic,true,hr);
    if (active.load()) {
        trace("reset_ex", "object=%p hr=0x%08lx", device, static_cast<unsigned long>(hr));
        if (SUCCEEDED(hr)) reacquire_swap(device);
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE create(IDirect3D9* factory, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
                                 D3DPRESENT_PARAMETERS* p, IDirect3DDevice9** output) noexcept {
    const auto fn = method<CreateFn>(factory, 16);
    if (!fn) return D3DERR_INVALIDCALL;
    if (marked_ex(factory, true)) {
        if (!p || !output) return D3DERR_INVALIDCALL;
        *output = nullptr;
        // A single D3DDISPLAYMODEEX is insufficient for an adapter group. Do not
        // fabricate its array or modify the caller's flags to force it through.
        if (flags & D3DCREATE_ADAPTERGROUP_DEVICE) {
            trace("device_ex_rejected", "reason=adapter_group_unsupported"); return D3DERR_NOTAVAILABLE;
        }
        const auto ex_fn = method<CreateExFn>(factory, 20);
        if (!ex_fn) return D3DERR_NOTAVAILABLE;
        auto mode = ex_display_mode(*p);
        const auto present_mode=copy_present_mode(p);
        IDirect3DDevice9Ex* extended{};
        HRESULT hr = ex_fn(static_cast<IDirect3D9Ex*>(factory), adapter, type, window, flags, p,
                           p->Windowed ? nullptr : &mode, &extended);
        if (SUCCEEDED(hr) && extended) {
            IDirect3DDevice9Ex* verified{};
            const HRESULT qi = extended->QueryInterface(IID_IDirect3DDevice9Ex, reinterpret_cast<void**>(&verified));
            const bool verified_identity = SUCCEEDED(qi) && verified == extended;
            if (verified) verified->Release();
            if (verified_identity && observe_device(extended, true, true)) {
                record_device_creation(extended, true);
                record_legacy_presentation(extended,present_mode);
                *output = extended;
            } else {
                extended->Release(); extended = nullptr; hr = D3DERR_NOTAVAILABLE;
                trace("device_ex_rejected", "reason=interface_or_required_hooks_failed qi=0x%08lx", static_cast<unsigned long>(qi));
            }
        } else if (SUCCEEDED(hr)) {
            hr = D3DERR_NOTAVAILABLE;
        }
        trace("device_promoted_ex", "object=%p adapter=%u flags=0x%lx hr=0x%08lx width=%u height=%u windowed=%u",
              extended, adapter, flags, static_cast<unsigned long>(hr), p->BackBufferWidth, p->BackBufferHeight, p->Windowed);
        observe_native_resolution(extended,*p,hr);
        return hr;
    }
    const HRESULT hr = fn(factory, adapter, type, window, flags, p, output);
    if (active.load()) {
        trace("create_device", "adapter=%u flags=0x%lx hr=0x%08lx object=%p width=%u height=%u windowed=%u", adapter, flags,
            static_cast<unsigned long>(hr), output && SUCCEEDED(hr) ? *output : nullptr,
            p ? p->BackBufferWidth : 0, p ? p->BackBufferHeight : 0, p ? p->Windowed : 0);
        // Inherited CreateDevice does not prove an Ex device interface. Only the
        // documented CreateDeviceEx result authorizes access to the longer table.
        if (SUCCEEDED(hr) && output && *output && observe_device(*output, false)) record_device_creation(*output, false);
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE create_ex(IDirect3D9Ex* factory, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
                                     D3DPRESENT_PARAMETERS* p, D3DDISPLAYMODEEX* mode, IDirect3DDevice9Ex** output) noexcept {
    const auto fn = method<CreateExFn>(factory, 20);
    if (!fn) return D3DERR_INVALIDCALL;
    const HRESULT hr = fn(factory, adapter, type, window, flags, p, mode, output);
    if (active.load()) {
        trace("create_device_ex", "adapter=%u flags=0x%lx hr=0x%08lx", adapter, flags, static_cast<unsigned long>(hr));
        if (SUCCEEDED(hr) && output && *output && observe_device(*output, true)) record_device_creation(*output, true);
    }
    return hr;
}
ResourcePolicy resource_policy(IDirect3DDevice9* device, ResourceKind kind, DWORD usage, D3DPOOL pool) noexcept {
    if (!marked_ex(device, false)) return {usage, pool, false, false};
    return ex_resource_policy(kind, usage, pool);
}
void resource_result(const char* kind, const ResourcePolicy& policy, DWORD before, HRESULT hr) noexcept {
    static std::atomic<std::uint64_t> events{};
    const auto n = ++events;
    if (policy.unsupported_managed || FAILED(hr) || (policy.changed && sampled(n, hr)))
        trace("ex_resource", "kind=%s serial=%llu changed=%u unsupported_managed=%u usage_before=0x%lx usage_after=0x%lx pool_after=%u hr=0x%08lx",
            kind, n, policy.changed, policy.unsupported_managed, before, policy.usage, policy.pool, static_cast<unsigned long>(hr));
}
HRESULT STDMETHODCALLTYPE create_texture(IDirect3DDevice9* device, UINT width, UINT height, UINT levels, DWORD usage,
    D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9** output, HANDLE* shared) noexcept {
    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
    const auto fn = method<Fn>(device, 23);
    if (!fn) return D3DERR_INVALIDCALL;
    const auto p = resource_policy(device, ResourceKind::texture, usage, pool);
    const HRESULT hr = fn(device, width, height, levels, p.usage, format, p.pool, output, shared);
    resource_result("texture", p, usage, hr); return hr;
}
HRESULT STDMETHODCALLTYPE create_volume_texture(IDirect3DDevice9* device, UINT width, UINT height, UINT depth, UINT levels, DWORD usage,
    D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture9** output, HANDLE* shared) noexcept {
    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9**, HANDLE*);
    const auto fn = method<Fn>(device, 24);
    if (!fn) return D3DERR_INVALIDCALL;
    const auto p = resource_policy(device, ResourceKind::volume_texture, usage, pool);
    const HRESULT hr = fn(device, width, height, depth, levels, p.usage, format, p.pool, output, shared);
    resource_result("volume_texture", p, usage, hr); return hr;
}
HRESULT STDMETHODCALLTYPE create_cube_texture(IDirect3DDevice9* device, UINT edge, UINT levels, DWORD usage,
    D3DFORMAT format, D3DPOOL pool, IDirect3DCubeTexture9** output, HANDLE* shared) noexcept {
    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DCubeTexture9**, HANDLE*);
    const auto fn = method<Fn>(device, 25);
    if (!fn) return D3DERR_INVALIDCALL;
    const auto p = resource_policy(device, ResourceKind::cube_texture, usage, pool);
    const HRESULT hr = fn(device, edge, levels, p.usage, format, p.pool, output, shared);
    resource_result("cube_texture", p, usage, hr); return hr;
}
HRESULT STDMETHODCALLTYPE create_vertex_buffer(IDirect3DDevice9* device, UINT length, DWORD usage, DWORD fvf,
    D3DPOOL pool, IDirect3DVertexBuffer9** output, HANDLE* shared) noexcept {
    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9**, HANDLE*);
    const auto fn = method<Fn>(device, 26);
    if (!fn) return D3DERR_INVALIDCALL;
    const auto p = resource_policy(device, ResourceKind::vertex_buffer, usage, pool);
    const HRESULT hr = fn(device, length, p.usage, fvf, p.pool, output, shared);
    resource_result("vertex_buffer", p, usage, hr); return hr;
}
HRESULT STDMETHODCALLTYPE create_index_buffer(IDirect3DDevice9* device, UINT length, DWORD usage, D3DFORMAT format,
    D3DPOOL pool, IDirect3DIndexBuffer9** output, HANDLE* shared) noexcept {
    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer9**, HANDLE*);
    const auto fn = method<Fn>(device, 27);
    if (!fn) return D3DERR_INVALIDCALL;
    const auto p = resource_policy(device, ResourceKind::index_buffer, usage, pool);
    const HRESULT hr = fn(device, length, p.usage, format, p.pool, output, shared);
    resource_result("index_buffer", p, usage, hr); return hr;
}
}
void set_observation_active(bool enabled) noexcept { active.store(enabled); }
bool read_device_creation(IDirect3DDevice9* device, DeviceCreationInfo* output) noexcept {
    if (!output) return false;
    *output = {};
    if (!device) return false;
    AcquireSRWLockShared(&device_lock);
    for (const auto& entry : devices) if (entry.device == device) { *output = entry.creation; break; }
    ReleaseSRWLockShared(&device_lock);
    return output->serial != 0 && output->thread_id != 0;
}
bool set_graphics_callbacks(const GraphicsCallbacks& value) noexcept {
    const bool nonempty = value.before_present || value.before_reset || value.after_reset || value.device_released;
    AcquireSRWLockExclusive(&callback_lock);
    const bool allowed = callbacks_in_flight.load() == 0 && (!nonempty || active.load());
    if (allowed) callbacks = value;
    ReleaseSRWLockExclusive(&callback_lock); return allowed;
}
std::uint64_t indexed_draw_reentry_serial() noexcept { return indexed_reentries; }
bool set_indexed_draw_callbacks(const IndexedDrawCallbacks& value) noexcept {
    const bool nonempty=value.context||value.before||value.after||value.abandon;
    const bool complete=value.before&&value.after&&value.abandon;
    AcquireSRWLockExclusive(&indexed_callback_lock);
    const bool allowed=indexed_in_flight.load()==0&&(!nonempty||(active.load()&&complete));
    if(allowed){indexed_callbacks=value;indexed_enabled.store(nonempty,std::memory_order_release);}
    ReleaseSRWLockExclusive(&indexed_callback_lock);return allowed;
}
bool observe_swapchain(IDirect3DSwapChain9* swapchain) noexcept {
    if (!swapchain) return false;
    const bool p = install(swapchain, 3, reinterpret_cast<void*>(&swap_present));
    const bool r = install(swapchain, 2, reinterpret_cast<void*>(&release));
    return p && r;
}
bool observe_device(IDirect3DDevice9* device, bool extended, bool migrate) noexcept {
    if (!device) return false;
    if (migrate && !extended) return false;
    if (!track_device(device)) return false;
    if (!install(device, 2, reinterpret_cast<void*>(&release))) {
        forget_device(device); return false;
    }
    bool ok = true;
    if(active.load()&&recovery_diagnostics.load())
        ok=install(device,3,reinterpret_cast<void*>(&cooperative_level))&&ok;
    if(active.load()&&indexed_enabled.load(std::memory_order_acquire))
        ok=install(device,82,reinterpret_cast<void*>(&indexed_draw))&&ok;
    ok = install(device, 13, reinterpret_cast<void*>(&create_swap)) && ok;
    ok = install(device, 14, reinterpret_cast<void*>(&get_swap)) && ok;
    ok = install(device, 16, reinterpret_cast<void*>(&reset)) && ok;
    ok = install(device, 17, reinterpret_cast<void*>(&present)) && ok;
    if (extended) {
        ok = install(device, 121, reinterpret_cast<void*>(&present_ex)) && ok;
        ok = install(device, 132, reinterpret_cast<void*>(&reset_ex)) && ok;
    }
    if (migrate) {
        ok = install(device, 23, reinterpret_cast<void*>(&create_texture)) && ok;
        ok = install(device, 24, reinterpret_cast<void*>(&create_volume_texture)) && ok;
        ok = install(device, 25, reinterpret_cast<void*>(&create_cube_texture)) && ok;
        ok = install(device, 26, reinterpret_cast<void*>(&create_vertex_buffer)) && ok;
        ok = install(device, 27, reinterpret_cast<void*>(&create_index_buffer)) && ok;
        if (ok) ok = mark_ex(device, false);
    }
    reacquire_swap(device);
    trace("device_observed", "object=%p extended=%u migrate=%u complete=%u", device, extended, migrate, ok);
    return ok;
}
bool observe_factory(IDirect3D9* factory, bool extended, bool promote) noexcept {
    if (!factory) return false;
    if (promote && !extended) return false;
    bool ok = install(factory, 16, reinterpret_cast<void*>(&create));
    if (extended) ok = install(factory, 20, reinterpret_cast<void*>(&create_ex)) && ok;
    if (promote) {
        ok = install(factory, 2, reinterpret_cast<void*>(&release)) && ok;
        if (ok) ok = mark_ex(factory, true);
    }
    trace("factory_observed", "object=%p extended=%u promote=%u complete=%u", factory, extended, promote, ok);
    if(ok&&active.load()&&extended&&promote)configure_native_resolution(factory,true);
    return ok;
}
void set_recovery_diagnostics(bool enabled) noexcept {
    recovery_diagnostics=enabled&&active.load();
}
bool recovery_diagnostics_active() noexcept {return recovery_diagnostics.load()&&active.load();}
}
