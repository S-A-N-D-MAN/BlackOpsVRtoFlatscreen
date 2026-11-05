// SPDX-License-Identifier: GPL-3.0-only
// Explicit offline graphics probe, deliberately excluded from CTest. Creates a
// hidden 64x64 window/device, no retail executable, OpenXR, Steam or network.
#include "loader/d3d9_observer.hpp"
#include "loader/trace.hpp"
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
namespace {
unsigned failures{};
void check(bool ok, const char* message, HRESULT hr = S_OK) {
    printf("%s %s hr=0x%08lx\n", ok ? "PASS" : "FAIL", message, static_cast<unsigned long>(hr));
    if (!ok) ++failures;
}
void check_texture(IDirect3DDevice9* device, const char* name, bool extended) {
    IDirect3DTexture9* texture{};
    auto hr = device->CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);
    check(SUCCEEDED(hr) && texture, name, hr);
    if (!texture) return;
    D3DSURFACE_DESC description{};
    hr = texture->GetLevelDesc(0, &description);
    check(SUCCEEDED(hr) && description.Pool == (extended ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED), "actual texture pool", hr);
    D3DLOCKED_RECT locked{};
    hr = texture->LockRect(0, &locked, nullptr, 0);
    check(SUCCEEDED(hr), "ordinary LockRect flags accepted", hr);
    if (SUCCEEDED(hr)) { memset(locked.pBits, 0x44, 16 * 4); texture->UnlockRect(0); }
    texture->Release();
}
}
int main(int argc, char** argv) {
    if (argc != 2 || strcmp(argv[1], "--hidden-device") != 0) {
        puts("Use --hidden-device to explicitly create the offline graphics probe. No game/VR is launched."); return 2;
    }
    bovr::initialize_trace(GetModuleHandleW(nullptr));
    bovr::set_observation_active(true);
    HWND window = CreateWindowExW(0, L"STATIC", L"BlackOpsVR offline Ex probe", WS_OVERLAPPEDWINDOW,
                                 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) { printf("FAIL window error=%lu\n", GetLastError()); return 1; }
    wchar_t system_path[MAX_PATH]{};
    GetSystemDirectoryW(system_path, MAX_PATH); wcscat_s(system_path, L"\\d3d9.dll");
    HMODULE system = LoadLibraryW(system_path);
    using Create = IDirect3D9*(WINAPI*)(UINT);
    using CreateEx = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
    const auto create = reinterpret_cast<Create>(GetProcAddress(system, "Direct3DCreate9"));
    const auto create_ex = reinterpret_cast<CreateEx>(GetProcAddress(system, "Direct3DCreate9Ex"));
    if (!create || !create_ex) { puts("FAIL system D3D exports"); DestroyWindow(window); return 1; }
    D3DPRESENT_PARAMETERS params{};
    params.Windowed = TRUE; params.hDeviceWindow = window; params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    params.BackBufferWidth = 64; params.BackBufferHeight = 64; params.BackBufferFormat = D3DFMT_X8R8G8B8;
    params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    auto* legacy = create(D3D_SDK_VERSION);
    if (!legacy) { puts("FAIL Create9"); DestroyWindow(window); return 1; }
    IDirect3DDevice9* legacy_device{};
    auto hr = legacy->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &params, &legacy_device);
    check(SUCCEEDED(hr), "system legacy device creation", hr);
    if (legacy_device) {
        check(bovr::observe_device(legacy_device, false), "ordinary device hooks");
        check_texture(legacy_device, "ordinary managed texture remains supported", false);
        legacy_device->Release();
    }
    legacy->Release();
    IDirect3D9Ex* factory{};
    hr = create_ex(D3D_SDK_VERSION, &factory);
    check(SUCCEEDED(hr) && factory, "system Ex factory", hr);
    if (!factory) { DestroyWindow(window); return 1; }
    check(bovr::observe_factory(factory, true, true), "promoted factory hooks");
    IDirect3DDevice9* device{};
    hr = factory->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &params, &device);
    check(SUCCEEDED(hr) && device, "base CreateDevice promoted to actual Ex device", hr);
    if (device) {
        check_texture(device, "Ex managed texture migration", true);
        IDirect3DCubeTexture9* cube{};
        hr = device->CreateCubeTexture(16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube, nullptr);
        check(SUCCEEDED(hr) && cube, "Ex managed cube texture migration", hr);
        if (cube) {
            D3DLOCKED_RECT locked{};
            hr = cube->LockRect(D3DCUBEMAP_FACE_POSITIVE_X, 0, &locked, nullptr, 0);
            check(SUCCEEDED(hr), "migrated cube ordinary LockRect", hr);
            if (SUCCEEDED(hr)) cube->UnlockRect(D3DCUBEMAP_FACE_POSITIVE_X, 0);
            cube->Release();
        }
        IDirect3DVolumeTexture9* volume{};
        hr = device->CreateVolumeTexture(8, 8, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &volume, nullptr);
        check(SUCCEEDED(hr) && volume, "Ex managed volume texture migration", hr);
        if (volume) {
            D3DLOCKED_BOX locked{};
            hr = volume->LockBox(0, &locked, nullptr, 0);
            check(SUCCEEDED(hr), "migrated volume ordinary LockBox", hr);
            if (SUCCEEDED(hr)) volume->UnlockBox(0);
            volume->Release();
        }
        IDirect3DVertexBuffer9* vertex{};
        hr = device->CreateVertexBuffer(64, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vertex, nullptr);
        check(SUCCEEDED(hr) && vertex, "Ex managed vertex buffer migration", hr);
        if (vertex) vertex->Release();
        IDirect3DIndexBuffer9* index{};
        hr = device->CreateIndexBuffer(64, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &index, nullptr);
        check(SUCCEEDED(hr) && index, "Ex managed index buffer migration", hr);
        if (index) index->Release();
        IDirect3DTexture9* persistent{};
        hr = device->CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &persistent, nullptr);
        check(SUCCEEDED(hr) && persistent, "pre-ResetEx persistent texture", hr);
        hr = device->Reset(&params);
        check(SUCCEEDED(hr), "base Reset promoted to ResetEx", hr);
        if (SUCCEEDED(hr)) {
            if (persistent) {
                D3DLOCKED_RECT locked{};
                const auto lock_hr = persistent->LockRect(0, &locked, nullptr, 0);
                check(SUCCEEDED(lock_hr), "migrated texture survives ResetEx", lock_hr);
                if (SUCCEEDED(lock_hr)) persistent->UnlockRect(0);
            }
            hr = device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff224466, 1.0f, 0);
            check(SUCCEEDED(hr), "hidden desktop clear", hr);
            IDirect3DSwapChain9* swap{};
            if (SUCCEEDED(device->GetSwapChain(0, &swap)) && swap) {
                hr = swap->Present(nullptr, nullptr, nullptr, nullptr, 0);
                check(SUCCEEDED(hr), "hidden swap-chain Present seam", hr); swap->Release();
            } else check(false, "hidden swap-chain retrieval");
        }
        // Release is safe even following a failed ResetEx; no more device APIs.
        if (persistent) persistent->Release();
        device->Release();
    }
    factory->Release();
    DestroyWindow(window);
    printf("game_launches=0 xr_instances=0 failures=%u\n", failures);
    // System DLL retained until process exit because its real vtables are hooked.
    return failures ? 1 : 0;
}
