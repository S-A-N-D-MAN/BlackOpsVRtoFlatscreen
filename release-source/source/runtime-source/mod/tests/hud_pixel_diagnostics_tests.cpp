// SPDX-License-Identifier: GPL-3.0-only
#include <windows.h>
#include <d3d9.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
// White-box copy tests the exact diagnostic implementation with a CPU-only
// COM ABI fixture. No D3D factory, GPU, native game or OpenXR calls are made.
#define private public
#define HudPixelDiagnostics TestedHudPixelDiagnostics
#include "render/hud_pixel_diagnostics.cpp"
#undef HudPixelDiagnostics
#undef private
namespace {
unsigned checks{},failures{};
void check(bool result,const char* why){++checks;if(!result){++failures;std::printf("FAIL %s\n",why);}}
template<class T>void put(std::vector<std::uint8_t>& b,std::size_t offset,T value){std::memcpy(b.data()+offset,&value,sizeof(value));}
template<class T>T get(const std::vector<std::uint8_t>& b,std::size_t offset){T value{};std::memcpy(&value,b.data()+offset,sizeof(value));return value;}
std::vector<std::uint8_t> file(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
struct Surface {
    void** table{};unsigned refs{},releases{},locks{},unlocks{};HRESULT lock_result{S_OK};
    D3DSURFACE_DESC desc{D3DFMT_X8R8G8B8,D3DRTYPE_SURFACE,0,D3DPOOL_DEFAULT,D3DMULTISAMPLE_NONE,0,2,2};
    std::array<std::uint8_t,24> bytes{1,2,3,4,5,6,7,8,91,92,93,94,9,10,11,12,13,14,15,16,95,96,97,98};
};
ULONG __stdcall release(Surface* s){++s->releases;return --s->refs;}
HRESULT __stdcall desc(Surface* s,D3DSURFACE_DESC* out){*out=s->desc;return S_OK;}
HRESULT __stdcall lock(Surface* s,D3DLOCKED_RECT* out,const RECT* r,DWORD flags){check(!r&&flags==D3DLOCK_READONLY,"readback uses whole surface readonly");++s->locks;if(FAILED(s->lock_result))return s->lock_result;out->Pitch=12;out->pBits=s->bytes.data();return S_OK;}
HRESULT __stdcall unlock(Surface* s){++s->unlocks;return S_OK;}
struct Device {
    void** table{};Surface bb{},system{};unsigned reads{},queries{},creates{};HRESULT read_result{S_OK},create_result{S_OK};
};
HRESULT __stdcall backbuffer(Device* d,UINT swap,UINT index,D3DBACKBUFFER_TYPE type,IDirect3DSurface9** out){check(!swap&&!index&&type==D3DBACKBUFFER_TYPE_MONO,"only swapchain0 backbuffer read");++d->queries;++d->bb.refs;*out=reinterpret_cast<IDirect3DSurface9*>(&d->bb);return S_OK;}
HRESULT __stdcall target(Device* d,DWORD index,IDirect3DSurface9** out){check(!index,"only RT0 inspected");++d->queries;++d->bb.refs;*out=reinterpret_cast<IDirect3DSurface9*>(&d->bb);return S_OK;}
HRESULT __stdcall viewport(Device* d,D3DVIEWPORT9* out){++d->queries;*out={1,2,3,4,0.25f,0.75f};return S_OK;}
HRESULT __stdcall scissor(Device* d,RECT* out){++d->queries;*out={3,4,5,6};return S_OK;}
HRESULT __stdcall renderstate(Device* d,D3DRENDERSTATETYPE state,DWORD* out){check(state==D3DRS_SCISSORTESTENABLE,"only scissor-enable state inspected");++d->queries;*out=TRUE;return S_OK;}
HRESULT __stdcall create(Device* d,UINT width,UINT height,D3DFORMAT format,D3DPOOL pool,IDirect3DSurface9** out,HANDLE* shared){check(width==2&&height==2&&format==D3DFMT_X8R8G8B8&&pool==D3DPOOL_SYSTEMMEM&&!shared,"only matching system-memory readback surface created");++d->creates;if(FAILED(d->create_result))return d->create_result;++d->system.refs;*out=reinterpret_cast<IDirect3DSurface9*>(&d->system);return S_OK;}
HRESULT __stdcall readback(Device* d,IDirect3DSurface9* source,IDirect3DSurface9* destination){check(source==reinterpret_cast<IDirect3DSurface9*>(&d->bb)&&destination==reinterpret_cast<IDirect3DSurface9*>(&d->system),"readback source/destination are borrowed BB and temporary system surface");++d->reads;return d->read_result;}
struct Fixture {
    std::array<void*,119> device_table{};std::array<void*,17> surface_table{};Device d{};
    Fixture(){surface_table[2]=reinterpret_cast<void*>(release);surface_table[12]=reinterpret_cast<void*>(desc);surface_table[13]=reinterpret_cast<void*>(lock);surface_table[14]=reinterpret_cast<void*>(unlock);
        device_table[18]=reinterpret_cast<void*>(backbuffer);device_table[38]=reinterpret_cast<void*>(target);device_table[48]=reinterpret_cast<void*>(viewport);device_table[76]=reinterpret_cast<void*>(scissor);device_table[58]=reinterpret_cast<void*>(renderstate);device_table[36]=reinterpret_cast<void*>(create);device_table[32]=reinterpret_cast<void*>(readback);
        d.table=device_table.data();d.bb.table=surface_table.data();d.system.table=surface_table.data();}
    IDirect3DDevice9* device(){return reinterpret_cast<IDirect3DDevice9*>(&d);}
};
}
int main(){
    using namespace bovr::render;
    const auto path=std::filesystem::temp_directory_path()/(L"bovr-hud-pixel-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    check(std::filesystem::create_directory(path),"unique CPU fixture directory created");
    std::vector<std::uint8_t> end{4,0,0,0};Fixture f;
    SetEnvironmentVariableW(L"BOVR_HUD_PIXEL_PROBE",nullptr);SetEnvironmentVariableW(L"BOVR_HUD_PIXEL_DIRECTORY",path.c_str());
    TestedHudPixelDiagnostics off;check(!off.begin(f.device(),{1,2,3,4,5},end,end,end)&&!f.d.queries,"default off never touches device");
    SetEnvironmentVariableW(L"BOVR_HUD_PIXEL_PROBE",L"1");SetEnvironmentVariableW(L"BOVR_HUD_PIXEL_DIRECTORY",L"relative");
    TestedHudPixelDiagnostics relative;check(!relative.begin(f.device(),{1,2,3,4,5},end,end,end)&&!f.d.queries,"relative path rejected before COM");
    SetEnvironmentVariableW(L"BOVR_HUD_PIXEL_DIRECTORY",path.c_str());
    TestedHudPixelDiagnostics probe;SetLastError(0x73421);
    check(probe.begin(f.device(),{1,2,3,4,5},end,end,end),"valid explicit diagnostic begins");check(GetLastError()==0x73421,"begin preserves LastError");
    check(f.d.reads==1&&f.d.bb.refs==0&&f.d.system.refs==0,"begin readback balances all COM references");
    const auto prefix=probe.prefix_;const auto bmp=file(prefix+L"-native_before_restore.bmp");
    check(bmp.size()==70&&get<WORD>(bmp,0)==0x4d42&&get<DWORD>(bmp,10)==54,"BMP has exact bounded header and packed size");
    check(get<LONG>(bmp,18)==2&&get<LONG>(bmp,22)==-2&&get<WORD>(bmp,28)==32,"BMP records top-down32-bit dimensions");
    const std::array<std::uint8_t,16> expected{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    check(!std::memcmp(bmp.data()+54,expected.data(),expected.size()),"actual row pitch handled without copying padding");
    check(f.d.system.bytes[8]==91&&f.d.system.bytes[20]==95,"source pixels/padding immutable");
    auto meta=file(prefix+L"-native_before_restore.txt");const std::string metadata(meta.begin(),meta.end());
    check(metadata.find("viewport=1,2,3,4")!=std::string::npos&&metadata.find("scissor=3,4,5,6")!=std::string::npos&&metadata.find("bb_rt_interface_pointer_equal=1")!=std::string::npos,"metadata records actual RT viewport and scissor");
    check(!write_bitmap(prefix+L"-native_before_restore.bmp",2,2,12,f.d.system.bytes.data())&&file(prefix+L"-native_before_restore.bmp")==bmp,"existing capture cannot be overwritten");
    const auto queries=f.d.queries;probe.capture(f.device(),99,"world_restored");probe.capture(f.device(),3,"../escape");
    check(f.d.queries==queries,"wrong producer/path injection never touches COM");
    std::thread foreign([&]{probe.capture(f.device(),3,"world_restored");probe.abandon(0,"foreign");});foreign.join();
    check(f.d.queries==queries&&probe.active_,"foreign thread cannot capture or retire owner sample");
    check(!probe.begin(f.device(),{1,2,4,4,6},end,end,end),"active producer cannot be replaced");
    probe.capture(f.device(),3,"world_restored");probe.capture(f.device(),3,"hud_completed");probe.capture(f.device(),3,"pre_present",true);
    check(!probe.active_&&f.d.reads==4&&f.d.system.locks==4&&f.d.system.unlocks==4&&f.d.system.refs==0&&f.d.bb.refs==0,"four stages retire read locks and COM refs without resource retention");
    check(!probe.begin(f.device(),{1,3,4,4,6},end,end,end),"rate cap blocks immediate next producer");
    probe.last_begin_=0;f.d.read_result=E_FAIL;check(probe.begin(f.device(),{1,3,4,4,6},end,end,end),"readback failure is diagnostic-only");
    check(f.d.system.locks==4&&f.d.system.refs==0&&f.d.bb.refs==0,"failed GPU readback releases references without lock");probe.abandon(0,"test");
    probe.last_begin_=0;f.d.read_result=S_OK;f.d.system.lock_result=E_FAIL;check(probe.begin(f.device(),{1,4,5,4,7},end,end,end),"lock failure is diagnostic-only");
    check(f.d.system.unlocks==4&&f.d.system.refs==0&&f.d.bb.refs==0,"failed lock is never unlocked or retained");probe.abandon(5,"test");
    probe.last_begin_=0;check(!probe.begin(f.device(),{1,5,6,4,8},end,end,end)&&probe.captured_==3,"strict three producer limit");
    check(!write_bitmap(path/L"bad.bmp",2,2,7,f.d.system.bytes.data())&&!write_bitmap(path/L"bad.bmp",5121,2,20484,f.d.system.bytes.data()),"invalid pitch and dimensions fail before pixel reads");
    std::vector<unsigned char> wide_pixels(5120*4*2,0x7f);
    check(write_bitmap(path/L"high-resolution.bmp",5120,2,5120*4,wide_pixels.data()),"supported high-resolution width retains bounded pixel capture");
    const char* name="hud_widget_name";std::vector<std::uint8_t> commands(52);put(commands,0,WORD(48));commands[2]=10;put(commands,4,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&name)));put(commands,8,100.0f);put(commands,16,2.0f);put(commands,20,40.0f);put(commands,48,WORD(4));
    const auto before=commands;const auto summary=summarize(commands);
    check(summary.find("name=hud_widget_name")!=std::string::npos&&summary.find("f16=2")!=std::string::npos&&summary.find("f20=40")!=std::string::npos,"actual material identity/name and non-unit W are recorded separately from width");
    check(commands==before,"command summaries preserve source bytes");
    put(commands,4,0x1234u);check(summarize(commands).find("<unreadable>")!=std::string::npos,"unreadable material metadata is contained");
    commands.resize(7);check(summarize(commands).find("invalid length")!=std::string::npos,"truncated command stops before payload read");
    SetEnvironmentVariableW(L"BOVR_HUD_PIXEL_PROBE",nullptr);SetEnvironmentVariableW(L"BOVR_HUD_PIXEL_DIRECTORY",nullptr);
    // Only our freshly minted, checked fixture directory is removed.
    const bool safe_cleanup=std::filesystem::equivalent(path.parent_path(),std::filesystem::temp_directory_path())&&path.filename().wstring().starts_with(L"bovr-hud-pixel-test-");
    check(safe_cleanup,"cleanup target remains this test's unique temp directory");
    if(safe_cleanup)std::filesystem::remove_all(path);
    std::printf("%s checks=%u failures=%u real_d3d_calls=0 retail_calls=0 xr_calls=0\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
