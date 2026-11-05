// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_resolution.hpp"
#include "loader/native_span_guard.hpp"
#include "loader/native_resolution_policy.hpp"
#include "loader/native_resolution_guards.hpp"
#include "loader/trace.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <intrin.h>

namespace bovr {
namespace {
namespace nr=native_resolution;
using MonitorFn=BOOL(WINAPI*)(HMONITOR,LPMONITORINFO);
constexpr std::uintptr_t import_slot=0x9a3340,custom_slot=0x3b1fe34,fullscreen_slot=0x3b1fa24,
    samples_slot=0x3b1fc80,factory_slot=0x3963444,adapter_slot=0x396344c;
struct Adapter {HMONITOR monitor{};nr::Size maximum{};};
struct State {std::uintptr_t factory{};nr::Size requested{};std::array<Adapter,16> adapters{};unsigned count{};bool enabled{};};
State resolution_state{};
SRWLOCK resolution_lock=SRWLOCK_INIT;
std::atomic<MonitorFn> original_monitor{};
std::atomic<bool> installed{};
std::atomic<std::uint64_t> monitor_calls{};
bool copy(std::uintptr_t address,void* target,std::size_t count) noexcept {
    if(address<0x10000||address>UINT32_MAX-count||!target)return false;
    __try {std::memcpy(target,reinterpret_cast<const void*>(address),count);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T> bool read(std::uintptr_t address,T* target) noexcept {return copy(address,target,sizeof(T));}
bool string(std::uintptr_t address,char* out,unsigned count) noexcept {
    if(!out||!count||address<0x10000||address>UINT32_MAX-count)return false;
    __try {for(unsigned i=0;i<count;++i){out[i]=*reinterpret_cast<const char*>(address+i);if(!out[i])return true;}return false;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool native_guards() noexcept {
    namespace g=nr::guards;
    return GetModuleHandleW(nullptr)==reinterpret_cast<HMODULE>(0x400000)&&
        native_guard::matches(reinterpret_cast<const void*>(g::monitor_dimensions_address),g::monitor_dimensions_size,g::monitor_dimensions_sha256)&&
        native_guard::matches(reinterpret_cast<const void*>(g::custom_resolution_address),g::custom_resolution_size,g::custom_resolution_sha256)&&
        native_guard::matches(reinterpret_cast<const void*>(g::window_parameters_address),g::window_parameters_size,g::window_parameters_sha256)&&
        native_guard::matches(reinterpret_cast<const void*>(g::device_native_cache_address),g::device_native_cache_size,g::device_native_cache_sha256);
}
struct Dvar {std::uintptr_t pointer{};std::array<std::uint32_t,14> words{};};
bool dvar(std::uintptr_t slot,const char* expected,unsigned type,Dvar* out) noexcept {
    Dvar value{};char name[32]{};std::uintptr_t again{};std::array<std::uint32_t,14> words{};
    if(!read(slot,&value.pointer)||!copy(value.pointer,value.words.data(),sizeof(value.words))||value.words[4]!=type||
       !string(value.words[0],name,sizeof(name))||std::strcmp(name,expected)||
       !copy(value.pointer,words.data(),sizeof(words))||words!=value.words||!read(slot,&again)||again!=value.pointer)return false;
    *out=value;return true;
}
struct NativeValues {
    std::uintptr_t factory{};unsigned adapter{};
    Dvar custom{},fullscreen{},samples{};
    std::array<char,32> current{},latched{};
    friend bool operator==(const NativeValues& a,const NativeValues& b) noexcept {
        return a.factory==b.factory&&a.adapter==b.adapter&&a.custom.pointer==b.custom.pointer&&a.custom.words==b.custom.words&&
            a.fullscreen.pointer==b.fullscreen.pointer&&a.fullscreen.words==b.fullscreen.words&&
            a.samples.pointer==b.samples.pointer&&a.samples.words==b.samples.words&&a.current==b.current&&a.latched==b.latched;
    }
};
bool native_values(NativeValues* out) noexcept {
    NativeValues value{};
    if(!read(factory_slot,&value.factory)||!read(adapter_slot,&value.adapter)||
       !dvar(custom_slot,"r_customMode",7,&value.custom)||!dvar(fullscreen_slot,"r_fullscreen",0,&value.fullscreen)||
       !dvar(samples_slot,"r_aaSamples",5,&value.samples)||
       !string(value.custom.words[6],value.current.data(),static_cast<unsigned>(value.current.size()))||
       !string(value.custom.words[10],value.latched.data(),static_cast<unsigned>(value.latched.size())))return false;
    *out=value;return true;
}
State state_copy() noexcept {
    AcquireSRWLockShared(&resolution_lock);const auto state=resolution_state;ReleaseSRWLockShared(&resolution_lock);return state;
}
bool replace_rectangle(LPMONITORINFO destination,const MONITORINFO& expected,nr::Rect replacement) noexcept {
    __try {
        if(std::memcmp(destination,&expected,sizeof(expected)))return false;
        // The exact caller passes this 40-byte stack local. Work area, flags,
        // monitor origin and any other USER32 consumer remain untouched.
        destination->rcMonitor.right=replacement.right;destination->rcMonitor.bottom=replacement.bottom;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
__declspec(noinline) BOOL WINAPI monitor_info(HMONITOR monitor,LPMONITORINFO info) {
    const auto return_slot=reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto original=original_monitor.load(std::memory_order_acquire);
    // Published before the import CAS; the proxy remains pinned for its lifetime.
    const BOOL result=original(monitor,info);const DWORD error=GetLastError();
    const auto state=state_copy();
    if(result&&state.enabled&&caller==nr::api_return&&
       reinterpret_cast<std::uintptr_t>(info)==return_slot+nr::monitor_stack_offset){
        std::uintptr_t helper_caller{};NativeValues values{},again{};MONITORINFO physical{};
        if(read(return_slot+nr::helper_return_stack_offset,&helper_caller)&&nr::known_caller(helper_caller)&&
           copy(reinterpret_cast<std::uintptr_t>(info),&physical,sizeof(physical))&&physical.cbSize==sizeof(physical)&&
           native_values(&values)&&values.adapter<state.count){
            const auto& adapter=state.adapters[values.adapter];
            nr::Context context{state.requested,nr::requested(values.current.data()),adapter.maximum,caller,helper_caller,
                true,values.current==values.latched&&(values.samples.words[6]==1&&values.samples.words[10]==1),
                (values.fullscreen.words[6]&0xff)==0&&(values.fullscreen.words[10]&0xff)==0,
                values.factory==state.factory,adapter.monitor==monitor,true};
            const auto decision=nr::plan(context,{physical.rcMonitor.left,physical.rcMonitor.top,physical.rcMonitor.right,
                physical.rcMonitor.bottom},physical.cbSize,result!=FALSE);
            bool changed{};
            if(decision.allowed&&native_values(&again)&&values==again)
                changed=decision.changed&&replace_rectangle(info,physical,decision.monitor);
            const auto count=++monitor_calls;
            if(count<=8||count%120==0)trace("native_render_resolution",
                "stage=monitor_allowance count=%llu allowed=%u changed=%u helper_return=0x%08lx requested=%ux%u physical=%ldx%ld caps=%ux%u adapter=%u engine_instruction_patches=0 desktop_mode_changes=0",
                count,decision.allowed,changed,static_cast<unsigned long>(helper_caller),state.requested.width,state.requested.height,
                physical.rcMonitor.right-physical.rcMonitor.left,physical.rcMonitor.bottom-physical.rcMonitor.top,
                adapter.maximum.width,adapter.maximum.height,values.adapter);
        }
    }
    SetLastError(error);return result;
}
bool install_import() noexcept {
    if(installed.load())return *reinterpret_cast<void**>(import_slot)==reinterpret_cast<void*>(&monitor_info);
    const auto module=GetModuleHandleW(L"user32.dll");
    const auto expected=module?reinterpret_cast<MonitorFn>(GetProcAddress(module,"GetMonitorInfoA")):nullptr;
    void* current{};
    if(!expected||!read(import_slot,&current)||current!=reinterpret_cast<void*>(expected))return false;
    DWORD old{};if(!VirtualProtect(reinterpret_cast<void*>(import_slot),sizeof(void*),PAGE_READWRITE,&old))return false;
    original_monitor.store(expected,std::memory_order_release);
    const bool exchanged=InterlockedCompareExchangePointer(reinterpret_cast<void**>(import_slot),reinterpret_cast<void*>(&monitor_info),current)==current;
    DWORD ignored{};const bool restored=VirtualProtect(reinterpret_cast<void*>(import_slot),sizeof(void*),old,&ignored)!=FALSE;
    if(exchanged&&restored){installed.store(true);return true;}
    // Failed installation never enables a virtual rectangle. Attempt exact owned
    // rollback only; never overwrite a replacement installed by another owner.
    DWORD temporary{};
    if(exchanged&&VirtualProtect(reinterpret_cast<void*>(import_slot),sizeof(void*),PAGE_READWRITE,&temporary)){
        InterlockedCompareExchangePointer(reinterpret_cast<void**>(import_slot),current,reinterpret_cast<void*>(&monitor_info));
        VirtualProtect(reinterpret_cast<void*>(import_slot),sizeof(void*),old,&ignored);
    }
    return false;
}
}
bool configure_native_resolution(IDirect3D9* factory,bool trusted_promoted) noexcept {
    const DWORD error=GetLastError();bool ok{};
    State state{};char requested[32]{};
    const DWORD length=GetEnvironmentVariableA("BOVR_NATIVE_RENDER_SIZE",requested,sizeof(requested));
    if(trusted_promoted&&factory&&length&&length<sizeof(requested)){
        state.requested=nr::requested(requested);
        if(nr::permitted(state.requested)&&native_guards()){
            state.count=factory->GetAdapterCount();state.factory=reinterpret_cast<std::uintptr_t>(factory);
            if(state.count&&state.count<=state.adapters.size()){
                for(unsigned i=0;i<state.count;++i){D3DCAPS9 caps{};
                    if(SUCCEEDED(factory->GetDeviceCaps(i,D3DDEVTYPE_HAL,&caps)))
                        state.adapters[i]={factory->GetAdapterMonitor(i),{caps.MaxTextureWidth,caps.MaxTextureHeight}};
                }
                ok=install_import();state.enabled=ok;
            }
        }
    }
    AcquireSRWLockExclusive(&resolution_lock);resolution_state=state;ReleaseSRWLockExclusive(&resolution_lock);
    if(length)trace("native_render_resolution","stage=configured enabled=%u requested=%s adapters=%u import_slot=0x%08lx engine_instruction_patches=0 desktop_mode_changes=0",
        ok,length<sizeof(requested)?requested:"invalid",state.count,static_cast<unsigned long>(import_slot));
    SetLastError(error);return ok;
}
void observe_native_resolution(IDirect3DDevice9* device,const D3DPRESENT_PARAMETERS& p,HRESULT result) noexcept {
    const DWORD error=GetLastError();const auto state=state_copy();
    if(state.requested.width){D3DSURFACE_DESC desc{};IDirect3DSurface9* surface{};
        if(SUCCEEDED(result)&&device&&SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&surface))&&surface){
            surface->GetDesc(&desc);surface->Release();}
        const bool selected=desc.Width==state.requested.width&&desc.Height==state.requested.height&&p.Windowed&&
            desc.MultiSampleType==D3DMULTISAMPLE_NONE;
        trace("native_render_resolution","stage=actual_source requested=%ux%u actual=%ux%u per_eye=%ux%u selected=%u allowance_enabled=%u windowed=%u multisample=%u create_hr=0x%08lx",
            state.requested.width,state.requested.height,desc.Width,desc.Height,desc.Width/2,desc.Height,selected,state.enabled,
            p.Windowed,static_cast<unsigned>(desc.MultiSampleType),static_cast<unsigned long>(result));
    }
    SetLastError(error);
}
}
