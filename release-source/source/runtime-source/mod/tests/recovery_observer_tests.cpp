// SPDX-License-Identifier: GPL-3.0-only
// Exact observer implementation against fake COM slots; no D3D runtime or XR.
#include "../src/loader/d3d9_observer.cpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace {
unsigned checks{},failures{},cooperative_calls{},reset_calls{},reset_ex_calls{},get_calls{};
unsigned present_calls{},swap_calls{},ex_present_calls{};
std::vector<std::string> events;
std::array<void*,133> table{};
std::array<void*,10> swap_table{};
std::array<void*,21> factory_table{};
struct Fake {void** table;ULONG refs{5};};
Fake owned{table.data()},unknown{table.data()};
Fake chain{swap_table.data()},additional_chain{swap_table.data()};
Fake factory{factory_table.data()};
HRESULT native_cooperative=S_OK,native_reset=S_OK;
HRESULT native_present=S_OK;
IDirect3DSwapChain9* nested_swap{};
const RECT* seen_source{};const RECT* seen_destination{};HWND seen_window{};const RGNDATA* seen_dirty{};DWORD seen_flags{};
bool allow=true;unsigned before{},after{};
D3DPRESENT_PARAMETERS* expected_parameters{};
D3DDISPLAYMODEEX* expected_mode{};
DWORD original_entry_error{};
void check(bool value,const char* label){++checks;printf("%s %s\n",value?"PASS":"FAIL",label);if(!value)++failures;}
std::size_t event_count(const char* prefix){std::size_t n{};for(const auto& e:events)n+=e.starts_with(prefix);return n;}
bool contains(const char* text){for(const auto& e:events)if(e.find(text)!=std::string::npos)return true;return false;}
ULONG STDMETHODCALLTYPE fake_release(IUnknown* object){return --reinterpret_cast<Fake*>(object)->refs;}
HRESULT STDMETHODCALLTYPE fake_cooperative(IDirect3DDevice9*){++cooperative_calls;SetLastError(0xDEADBEEF);return native_cooperative;}
HRESULT STDMETHODCALLTYPE fake_get(IDirect3DDevice9*,UINT,IDirect3DSwapChain9** out){++get_calls;*out=nullptr;return E_FAIL;}
HRESULT STDMETHODCALLTYPE fake_reset(IDirect3DDevice9*,D3DPRESENT_PARAMETERS* p){
    ++reset_calls;original_entry_error=GetLastError();check(p==expected_parameters,"Reset preserves exact borrowed parameter pointer");
    if(p)p->BackBufferWidth=0;return native_reset;
}
HRESULT STDMETHODCALLTYPE fake_reset_ex(IDirect3DDevice9Ex*,D3DPRESENT_PARAMETERS* p,D3DDISPLAYMODEEX* m){
    ++reset_ex_calls;check(p==expected_parameters&&m==expected_mode,"ResetEx preserves both original pointers");return native_reset;
}
HRESULT STDMETHODCALLTYPE fake_swap_present(IDirect3DSwapChain9*,const RECT* a,const RECT* b,HWND window,const RGNDATA* dirty,DWORD flags){
    ++swap_calls;seen_source=a;seen_destination=b;seen_window=window;seen_dirty=dirty;seen_flags=flags;
    SetLastError(0x11223344);return native_present;
}
HRESULT STDMETHODCALLTYPE fake_present(IDirect3DDevice9*,const RECT* a,const RECT* b,HWND window,const RGNDATA* dirty){
    ++present_calls;seen_source=a;seen_destination=b;seen_window=window;seen_dirty=dirty;
    if(nested_swap)return nested_swap->Present(a,b,window,dirty,0);
    SetLastError(0x11223344);return native_present;
}
HRESULT STDMETHODCALLTYPE fake_present_ex(IDirect3DDevice9Ex*,const RECT* a,const RECT* b,HWND window,const RGNDATA* dirty,DWORD flags){
    ++ex_present_calls;
    if(nested_swap)return nested_swap->Present(a,b,window,dirty,flags);
    SetLastError(0x11223344);return native_present;
}
HRESULT STDMETHODCALLTYPE fake_create_swap(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*,IDirect3DSwapChain9**){return E_FAIL;}
HRESULT STDMETHODCALLTYPE fake_query(IUnknown* object,REFIID iid,void** out){
    if(iid!=IID_IDirect3DDevice9Ex){*out=nullptr;return E_NOINTERFACE;}
    ++reinterpret_cast<Fake*>(object)->refs;*out=object;return S_OK;
}
HRESULT STDMETHODCALLTYPE fake_create(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**){return E_FAIL;}
HRESULT STDMETHODCALLTYPE fake_create_ex(IDirect3D9Ex*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*,IDirect3DDevice9Ex** out){
    *out=reinterpret_cast<IDirect3DDevice9Ex*>(&owned);return S_OK;
}
HRESULT STDMETHODCALLTYPE unused_resource(){return E_FAIL;}
bool before_reset(void*,IDirect3DDevice9*){++before;return allow;}
void after_reset(void*,IDirect3DDevice9*,HRESULT){++after;}
}
namespace bovr {
void trace(const char* name,const char* format,...) noexcept {
    char text[2048]{};va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    events.push_back(std::string(name)+" "+text);SetLastError(0x7777);
}
}
int main(){
    using namespace bovr;
    auto* d=reinterpret_cast<IDirect3DDevice9*>(&owned);
    auto* other=reinterpret_cast<IDirect3DDevice9*>(&unknown);
    table[2]=reinterpret_cast<void*>(&fake_release);table[3]=reinterpret_cast<void*>(&fake_cooperative);
    table[13]=reinterpret_cast<void*>(&fake_create_swap);table[14]=reinterpret_cast<void*>(&fake_get);
    table[16]=reinterpret_cast<void*>(&fake_reset);table[17]=reinterpret_cast<void*>(&fake_present);
    table[121]=reinterpret_cast<void*>(&fake_present_ex);table[132]=reinterpret_cast<void*>(&fake_reset_ex);
    set_recovery_diagnostics(true);
    check(!recovery_diagnostics.load(),"unknown/inactive identity cannot enable diagnostics");
    set_observation_active(true);check(observe_device(d,true),"default observer installs fake device methods");
    check(table[3]==reinterpret_cast<void*>(&fake_cooperative),"default-off adds no cooperative slot hook");
    set_recovery_diagnostics(true);check(observe_device(d,true),"opt-in adds cooperative observer");
    record_device_creation(d,true);
    for(const HRESULT status:{S_OK,S_FALSE,D3DERR_DEVICELOST,D3DERR_DEVICENOTRESET,D3DERR_DRIVERINTERNALERROR}){
        native_cooperative=status;const auto prior=cooperative_calls;
        check(d->TestCooperativeLevel()==status&&cooperative_calls==prior+1,"every native cooperative HRESULT forwards with one original call");
        check(GetLastError()==0xDEADBEEF,"cooperative diagnostic preserves native LastError despite tracing");
    }
    const auto logged=event_count("recovery_cooperative");
    other->TestCooperativeLevel();
    check(event_count("recovery_cooperative")==logged,"shared-vtable unknown identity forwards without diagnostic");
    native_cooperative=S_OK;d->TestCooperativeLevel();
    const auto stable=event_count("recovery_cooperative");
    for(unsigned i=0;i<1000;++i)d->TestCooperativeLevel();
    check(event_count("recovery_cooperative")==stable,"steady native status does not emit per-frame logs");
    check(set_graphics_callbacks({nullptr,nullptr,before_reset,after_reset,nullptr}),"owner callback fixture enabled");
    D3DPRESENT_PARAMETERS parameters{};parameters.BackBufferWidth=1600;parameters.BackBufferHeight=900;parameters.Windowed=TRUE;
    expected_parameters=&parameters;allow=false;
    const auto queries=get_calls,calls=reset_calls,after_prior=after;
    check(d->Reset(&parameters)==D3DERR_DEVICELOST&&reset_calls==calls&&get_calls==queries&&after==after_prior,
          "denied retirement still forwards no Reset or post-failure device query");
    check(contains("callback_admitted=0 original_called=0 native_hr_valid=0"),"synthetic denial distinguished from a native result");
    const auto budget_before=event_count("recovery_cooperative");
    for(unsigned i=0;i<16;++i)check(d->TestCooperativeLevel()==S_OK,"Ex-style S_OK remains S_OK after denied Reset; retry defect is not hidden");
    check(event_count("recovery_cooperative")==budget_before+8,"exact bounded cooperative window follows Reset attempt");
    allow=true;native_reset=D3DERR_DEVICELOST;
    check(d->Reset(&parameters)==native_reset&&reset_calls==calls+1&&get_calls==queries,"real failed Reset observed without subsequent device query");
    check(contains("callback_admitted=1 original_called=1 native_hr_valid=1 hr=0x88760868"),"native failure recorded distinctly");
    check(contains("requested_width=1600 requested_height=900"),"requested dimensions survive native mutation of presentation parameters");
    parameters.BackBufferWidth=1280;parameters.BackBufferHeight=720;native_reset=S_OK;
    check(d->Reset(&parameters)==S_OK&&get_calls==queries+1,"successful Reset retains existing implicit-swap reacquisition");
    D3DDISPLAYMODEEX mode{};expected_mode=&mode;
    check(reinterpret_cast<IDirect3DDevice9Ex*>(d)->ResetEx(&parameters,&mode)==S_OK&&reset_ex_calls==1,"native ResetEx remains exact passthrough");
    const auto count_before=events.size();set_recovery_diagnostics(false);d->TestCooperativeLevel();
    check(events.size()==count_before,"disabling diagnostic preserves installed forwarding without logs");
    SetLastError(0x33445566);auto point=diagnostic_reset_begin(d,&parameters,false,123);
    check(!point.attempt&&GetLastError()==0x33445566,"disabled parameter observer preserves input LastError");
    set_recovery_diagnostics(true);SetLastError(0x33445566);point=diagnostic_reset_begin(d,reinterpret_cast<D3DPRESENT_PARAMETERS*>(1),false,123);
    check(point.attempt&&!point.parameters_valid&&GetLastError()==0x33445566,"unreadable diagnostic copy contained without changing input or LastError");
    const auto serial=point.attempt;owned.refs=1;d->Release();
    point=diagnostic_reset_begin(d,&parameters,false,123);
    check(!point.attempt&&serial>0,"released device leaves no reusable diagnostic record");
    check(contains("status_override=0"),"trace explicitly distinguishes observation from recovery policy");

    // Exercise the production COM wrappers, including a nested swapchain called
    // by PresentEx. No device/desktop/runtime calls and no simulated success for
    // actual native failures are involved.
    owned.refs=5;check(observe_device(d,true),"presentation fixture retracks device");
    record_device_creation(d,true);
    auto* swap=reinterpret_cast<IDirect3DSwapChain9*>(&chain);
    auto* additional=reinterpret_cast<IDirect3DSwapChain9*>(&additional_chain);
    swap_table[2]=reinterpret_cast<void*>(&fake_release);swap_table[3]=reinterpret_cast<void*>(&fake_swap_present);
    check(observe_swapchain(swap),"actual implicit swap Present hook installed");set_implicit_swap(d,swap);
    RECT source{1,2,3,4},destination{5,6,7,8};RGNDATA dirty{};HWND window=reinterpret_cast<HWND>(123);
    native_present=S_PRESENT_MODE_CHANGED;
    check(d->Present(&source,&destination,window,&dirty)==native_present,"direct Ex-created device legacy Present is not opted in by interface alone");
    check(swap->Present(nullptr,nullptr,nullptr,nullptr,0)==native_present,"direct Ex-created implicit chain remains native");
    check(other->Present(nullptr,nullptr,nullptr,nullptr)==native_present,"unknown shared-vtable device remains native");
    record_legacy_presentation(d,{true,true});
    const auto pc=present_calls,sc=swap_calls;
    check(d->Present(&source,&destination,window,&dirty)==D3D_OK&&present_calls==pc+1,"promoted windowed legacy Present translates exact continuing status once");
    check(seen_source==&source&&seen_destination==&destination&&seen_window==window&&seen_dirty==&dirty,"legacy Present preserves original arguments");
    check(GetLastError()==0x11223344,"presentation translation and trace preserve original LastError");
    check(swap->Present(&source,&destination,window,&dirty,5)==D3D_OK&&swap_calls==sc+1,"promoted implicit chain translates exact status with one native call");
    check(seen_source==&source&&seen_destination==&destination&&seen_window==window&&seen_dirty==&dirty&&seen_flags==5,"swap Present preserves all six original arguments");
    check(additional->Present(nullptr,nullptr,nullptr,nullptr,0)==native_present,"additional or unknown swapchains never translate");
    check(reinterpret_cast<IDirect3DDevice9Ex*>(d)->PresentEx(nullptr,nullptr,nullptr,nullptr,0)==native_present,"explicit PresentEx on promoted device keeps its Ex status");
    nested_swap=swap;const auto nested_sc=swap_calls,nested_ex=ex_present_calls;
    check(reinterpret_cast<IDirect3DDevice9Ex*>(d)->PresentEx(nullptr,nullptr,nullptr,nullptr,0)==native_present&&swap_calls==nested_sc+1&&ex_present_calls==nested_ex+1,
          "explicit Ex scope preserves inner implicit swap status and call counts");
    check(d->Present(nullptr,nullptr,nullptr,nullptr)==D3D_OK,"legacy Present still translates a nested implicit swap");nested_swap=nullptr;
    for(const HRESULT status:{S_OK,S_FALSE,S_PRESENT_OCCLUDED,D3DERR_DEVICELOST,D3DERR_DEVICENOTRESET,D3DERR_DEVICEHUNG,D3DERR_DEVICEREMOVED,D3DERR_OUTOFVIDEOMEMORY,E_FAIL}) {
        native_present=status;const auto a=present_calls,b=swap_calls;
        check(d->Present(nullptr,nullptr,nullptr,nullptr)==status&&present_calls==a+1,"all other legacy Present statuses preserve native value and one call");
        check(swap->Present(nullptr,nullptr,nullptr,nullptr,0)==status&&swap_calls==b+1,"all other swap Present statuses preserve native value and one call");
    }
    native_present=S_PRESENT_MODE_CHANGED;parameters.Windowed=FALSE;expected_parameters=&parameters;native_reset=S_OK;
    check(d->Reset(&parameters)==S_OK,"native fullscreen reset succeeds");
    check(d->Present(nullptr,nullptr,nullptr,nullptr)==native_present,"successful fullscreen reset revokes windowed compatibility");
    parameters.Windowed=TRUE;native_reset=E_FAIL;d->Reset(&parameters);
    check(d->Present(nullptr,nullptr,nullptr,nullptr)==native_present,"failed reset cannot publish requested windowed mode");
    native_reset=S_OK;d->Reset(&parameters);
    check(d->Present(nullptr,nullptr,nullptr,nullptr)==D3D_OK,"successful windowed reset restores proof");
    allow=false;d->Reset(&parameters);
    check(d->Present(nullptr,nullptr,nullptr,nullptr)==native_present,"denied reset invalidates mode until a successful owned reset");
    allow=true;d->Reset(&parameters);
    const auto old_reset=begin_legacy_reset(d),new_reset=begin_legacy_reset(d);
    finish_legacy_reset(d,old_reset,S_OK,{true,true});
    check(d->Present(nullptr,nullptr,nullptr,nullptr)==native_present,"older overlapping reset cannot republish stale mode");
    finish_legacy_reset(d,new_reset,S_OK,{false,true});
    check(d->Present(nullptr,nullptr,nullptr,nullptr)==native_present,"uncopyable mode remains unknown after successful native reset");
    const auto late_old_reset=begin_legacy_reset(d),early_new_reset=begin_legacy_reset(d);
    finish_legacy_reset(d,early_new_reset,S_OK,{true,true});
    check(d->Present(nullptr,nullptr,nullptr,nullptr)==D3D_OK,"newest reset completion initially proves its requested windowed mode");
    finish_legacy_reset(d,late_old_reset,S_OK,{true,false});
    check(d->Present(nullptr,nullptr,nullptr,nullptr)==native_present,"older native reset completing last retires newer mode proof");
    auto mode_token=begin_legacy_reset(d);finish_legacy_reset(d,mode_token,S_OK,{true,true});
    for(unsigned i=0;i<1000;++i)d->Present(nullptr,nullptr,nullptr,nullptr);
    check(event_count("present_legacy_status")==8,"continuing display status emits at most eight translation diagnostics per device");
    check(contains("original_hr=0x08760877 returned_hr=0x00000000 translated=1 promoted=1 windowed=1 original_calls=1"),"diagnostic retains exact native status and explicit narrow translation");
    mode_token=begin_legacy_reset(d);owned.refs=1;d->Release();owned.refs=5;observe_device(d,true);record_device_creation(d,true);record_legacy_presentation(d,{true,false});
    finish_legacy_reset(d,mode_token,S_OK,{true,true});
    check(d->Present(nullptr,nullptr,nullptr,nullptr)==native_present,"retired device incarnation cannot authorize replacement mode");
    check(!copy_present_mode(reinterpret_cast<D3DPRESENT_PARAMETERS*>(1)).valid,"invalid present mode copy is contained without native query");
    parameters.Windowed=TRUE;expected_mode=&mode;
    check(reinterpret_cast<IDirect3DDevice9Ex*>(d)->ResetEx(&parameters,&mode)==S_OK&&d->Present(nullptr,nullptr,nullptr,nullptr)==D3D_OK,
          "successful explicit ResetEx updates the same promoted device mode");
    parameters.Windowed=FALSE;reinterpret_cast<IDirect3DDevice9Ex*>(d)->ResetEx(&parameters,&mode);
    check(d->Present(nullptr,nullptr,nullptr,nullptr)==native_present,"fullscreen ResetEx revokes compatibility");
    // Exercise the production creation branch, not only its private metadata
    // fixture. The mock Ex factory verifies the interface and required slots.
    owned.refs=1;d->Release();owned.refs=5;table[0]=reinterpret_cast<void*>(&fake_query);
    for(unsigned slot=23;slot<=27;++slot)table[slot]=reinterpret_cast<void*>(&unused_resource);
    factory_table[2]=reinterpret_cast<void*>(&fake_release);factory_table[16]=reinterpret_cast<void*>(&fake_create);factory_table[20]=reinterpret_cast<void*>(&fake_create_ex);
    auto* factory9=reinterpret_cast<IDirect3D9*>(&factory);
    set_observation_active(false);check(observe_factory(factory9,true,true),"fake legacy factory promotion installed without optional resolution observation");set_observation_active(true);
    parameters.Windowed=TRUE;IDirect3DDevice9* created{};
    check(factory9->CreateDevice(0,D3DDEVTYPE_HAL,nullptr,0,&parameters,&created)==S_OK&&created==d,"actual promoted legacy CreateDevice succeeds with verified Ex identity");
    check(created->Present(nullptr,nullptr,nullptr,nullptr)==D3D_OK,"only successful promotion mints initial windowed proof");
    owned.refs=1;d->Release();owned.refs=5;IDirect3DDevice9Ex* direct_ex{};
    check(reinterpret_cast<IDirect3D9Ex*>(factory9)->CreateDeviceEx(0,D3DDEVTYPE_HAL,nullptr,0,&parameters,nullptr,&direct_ex)==S_OK,"direct CreateDeviceEx succeeds without legacy promotion metadata");
    check(direct_ex->Present(nullptr,nullptr,nullptr,nullptr)==native_present,"actual direct CreateDeviceEx result never inherits compatibility from pointer reuse");
    printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
