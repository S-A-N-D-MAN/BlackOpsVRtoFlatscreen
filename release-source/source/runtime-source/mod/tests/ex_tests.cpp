// SPDX-License-Identifier: GPL-3.0-only
#include "loader/d3d9_observer.hpp"
#include "loader/d3d9ex_policy.hpp"
#include <array>
#include <cstdio>
namespace {
unsigned failures{}, legacy_creates{}, ex_creates{}, legacy_resets{}, ex_resets{}, swap_gets{}, textures{};
bool reset_failure{}, expect_migration{true};
void check(bool ok, const char* message) { printf("%s %s\n", ok ? "PASS" : "FAIL", message); if (!ok) ++failures; }
struct Fake { void** table; };
std::array<void*, 133> device_table{};
std::array<void*, 21> factory_table{};
std::array<void*, 10> swap_table{};
Fake device{device_table.data()}, ordinary{device_table.data()}, factory{factory_table.data()}, swap{swap_table.data()};
Fake* next_created=&device;
ULONG STDMETHODCALLTYPE release(IUnknown*) { return 2; }
HRESULT STDMETHODCALLTYPE qi(IUnknown* object, REFIID id, void** output) {
    if (id != IID_IDirect3DDevice9Ex || !output) return E_NOINTERFACE;
    *output = object; return S_OK;
}
HRESULT STDMETHODCALLTYPE present(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*) { return D3D_OK; }
HRESULT STDMETHODCALLTYPE present_ex(IDirect3DDevice9Ex*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD) { return D3D_OK; }
HRESULT STDMETHODCALLTYPE swap_present(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD) { return D3D_OK; }
HRESULT STDMETHODCALLTYPE get_swap(IDirect3DDevice9*, UINT, IDirect3DSwapChain9** output) { ++swap_gets; *output = reinterpret_cast<IDirect3DSwapChain9*>(&swap); return S_OK; }
HRESULT STDMETHODCALLTYPE create_swap(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9** output) { *output = reinterpret_cast<IDirect3DSwapChain9*>(&swap); return S_OK; }
HRESULT STDMETHODCALLTYPE reset(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*) { ++legacy_resets; return D3DERR_DEVICELOST; }
HRESULT STDMETHODCALLTYPE reset_ex(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS* p, D3DDISPLAYMODEEX* mode) {
    ++ex_resets; check(p && p->Windowed && mode == nullptr, "ResetEx windowed null display mode");
    return reset_failure ? D3DERR_DEVICELOST : D3D_OK;
}
HRESULT STDMETHODCALLTYPE create(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**) { ++legacy_creates; return E_FAIL; }
HRESULT STDMETHODCALLTYPE create_ex(IDirect3D9Ex* object, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
                                    D3DPRESENT_PARAMETERS* p, D3DDISPLAYMODEEX* mode, IDirect3DDevice9Ex** output) {
    ++ex_creates;
    check(object == reinterpret_cast<IDirect3D9Ex*>(&factory) && adapter == 3 && type == D3DDEVTYPE_HAL &&
          window == reinterpret_cast<HWND>(123) && flags == D3DCREATE_HARDWARE_VERTEXPROCESSING,
          "promoted CreateDeviceEx preserves adapter type window flags");
    check(p && p->Windowed && mode == nullptr, "CreateDeviceEx windowed null display mode");
    *output = reinterpret_cast<IDirect3DDevice9Ex*>(next_created); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE texture(IDirect3DDevice9*, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT format,
                                  D3DPOOL pool, IDirect3DTexture9** output, HANDLE* shared) {
    ++textures;
    check(w == 32 && h == 16 && levels == 1 && format == D3DFMT_A8R8G8B8 && !shared, "texture dimensions format shared handle preserved");
    check(usage == (expect_migration ? static_cast<DWORD>(D3DUSAGE_DYNAMIC) : 0UL) && pool == (expect_migration ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED),
          "only promoted object receives resource migration despite shared vtable");
    *output = nullptr; return D3DERR_OUTOFVIDEOMEMORY;
}
HRESULT STDMETHODCALLTYPE volume(IDirect3DDevice9*, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9**, HANDLE*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE cube(IDirect3DDevice9*, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DCubeTexture9**, HANDLE*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE vertex(IDirect3DDevice9*, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9**, HANDLE*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE index(IDirect3DDevice9*, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer9**, HANDLE*) { return E_NOTIMPL; }
}
int main() {
    using namespace bovr;
    set_observation_active(true);
    for (const auto kind : {ResourceKind::texture, ResourceKind::volume_texture, ResourceKind::cube_texture}) {
        const auto p = ex_resource_policy(kind, 0, D3DPOOL_MANAGED);
        check(p.changed && p.pool == D3DPOOL_DEFAULT && p.usage == D3DUSAGE_DYNAMIC, "managed static texture family migrates");
    }
    const auto vertex_policy = ex_resource_policy(ResourceKind::vertex_buffer, D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING, D3DPOOL_MANAGED);
    check(vertex_policy.changed && vertex_policy.usage == (D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING), "vertex migration preserves original usage flags");
    const auto unsupported = ex_resource_policy(ResourceKind::texture, D3DUSAGE_AUTOGENMIPMAP, D3DPOOL_MANAGED);
    check(unsupported.unsupported_managed && !unsupported.changed && unsupported.pool == D3DPOOL_MANAGED,
          "unimplemented autogen migration remains explicit and unchanged");
    const auto unchanged = ex_resource_policy(ResourceKind::index_buffer, D3DUSAGE_WRITEONLY, D3DPOOL_SYSTEMMEM);
    check(!unchanged.changed && unchanged.pool == D3DPOOL_SYSTEMMEM && unchanged.usage == D3DUSAGE_WRITEONLY, "system-memory resources untouched");
    D3DPRESENT_PARAMETERS p{}; p.Windowed = TRUE; p.BackBufferWidth = 800; p.BackBufferHeight = 600;
    p.BackBufferFormat = D3DFMT_X8R8G8B8; p.FullScreen_RefreshRateInHz = 60;
    const auto mode = ex_display_mode(p);
    check(mode.Size == sizeof(mode) && mode.Width == 800 && mode.Height == 600 && mode.RefreshRate == 60 && mode.Format == D3DFMT_X8R8G8B8,
          "fullscreen mode preserves caller dimensions refresh and format");
    device_table[0] = reinterpret_cast<void*>(&qi); device_table[2] = reinterpret_cast<void*>(&release);
    device_table[13] = reinterpret_cast<void*>(&create_swap); device_table[14] = reinterpret_cast<void*>(&get_swap);
    device_table[16] = reinterpret_cast<void*>(&reset); device_table[17] = reinterpret_cast<void*>(&present);
    device_table[23] = reinterpret_cast<void*>(&texture); device_table[24] = reinterpret_cast<void*>(&volume);
    device_table[25] = reinterpret_cast<void*>(&cube); device_table[26] = reinterpret_cast<void*>(&vertex); device_table[27] = reinterpret_cast<void*>(&index);
    device_table[121] = reinterpret_cast<void*>(&present_ex); device_table[132] = reinterpret_cast<void*>(&reset_ex);
    factory_table[2] = reinterpret_cast<void*>(&release); factory_table[16] = reinterpret_cast<void*>(&create); factory_table[20] = reinterpret_cast<void*>(&create_ex);
    swap_table[2] = reinterpret_cast<void*>(&release); swap_table[3] = reinterpret_cast<void*>(&swap_present);
    auto* f = reinterpret_cast<IDirect3D9*>(&factory);
    check(!observe_factory(f, false, true), "non-Ex factory promotion rejected");
    check(observe_factory(f, true, true), "promoted factory installed");
    IDirect3DDevice9* created{};
    const auto hr = f->CreateDevice(3, D3DDEVTYPE_HAL, reinterpret_cast<HWND>(123), D3DCREATE_HARDWARE_VERTEXPROCESSING, &p, &created);
    check(hr == D3D_OK && created == reinterpret_cast<IDirect3DDevice9*>(&device) && ex_creates == 1 && legacy_creates == 0,
          "promoted call returns verified Ex identity and calls Ex once");
    if (!created) return 1;
    DeviceCreationInfo observed{};
    check(read_device_creation(created,&observed) && observed.serial && observed.extended && observed.thread_id==GetCurrentThreadId(),
          "successful factory creation records exact caller and monotonic identity");
    DeviceCreationInfo unknown{99,99,true};
    check(!read_device_creation(reinterpret_cast<IDirect3DDevice9*>(&ordinary),&unknown) && !unknown.serial,
          "a shared vtable does not manufacture creation evidence");
    check(observe_device(created,true,true),"repeat device observation remains supported");
    DeviceCreationInfo repeated{};
    check(read_device_creation(created,&repeated) && repeated.serial==observed.serial && repeated.thread_id==observed.thread_id,
          "repeat observation cannot mint a replacement identity");
    const auto before_swap = swap_gets;
    check(created->Reset(&p) == D3D_OK && ex_resets == 1 && legacy_resets == 0 && swap_gets == before_swap + 1,
          "Reset routes once to ResetEx and reacquires successful swap");
    reset_failure = true;
    const auto before_failed_swap = swap_gets;
    check(created->Reset(&p) == D3DERR_DEVICELOST && swap_gets == before_failed_swap && ex_resets == 2,
          "failed ResetEx performs no further device calls");
    reset_failure = false;
    check(created->Reset(&p) == D3D_OK, "successful ResetEx recovers before resource work");
    IDirect3DTexture9* out{};
    check(created->CreateTexture(32, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &out, nullptr) == D3DERR_OUTOFVIDEOMEMORY && textures == 1,
          "resource HRESULT forwarded without retry");
    expect_migration = false;
    reinterpret_cast<IDirect3DDevice9*>(&ordinary)->CreateTexture(32, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &out, nullptr);
    IDirect3DDevice9* rejected = created;
    check(f->CreateDevice(3, D3DDEVTYPE_HAL, nullptr, D3DCREATE_ADAPTERGROUP_DEVICE, &p, &rejected) == D3DERR_NOTAVAILABLE && !rejected && ex_creates == 1,
          "unsupported adapter group rejected before device call");
    auto* other=reinterpret_cast<IDirect3DDevice9*>(&ordinary);
    check(observe_device(other,true),"existing unknown device observation succeeds");
    check(!read_device_creation(other,&unknown),"generic observation cannot manufacture factory creation proof");
    next_created=&ordinary;IDirect3DDevice9* replacement{};
    check(f->CreateDevice(3,D3DDEVTYPE_HAL,reinterpret_cast<HWND>(123),D3DCREATE_HARDWARE_VERTEXPROCESSING,&p,&replacement)==D3D_OK,
          "second observed factory result succeeds");
    check(replacement==other && read_device_creation(other,&unknown) && unknown.serial>observed.serial &&
          unknown.thread_id==observed.thread_id && unknown.extended,"distinct factory result receives newer creation serial on exact same thread");
    printf("failures=%u\n", failures); return failures ? 1 : 0;
}
