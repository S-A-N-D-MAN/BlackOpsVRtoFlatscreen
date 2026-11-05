// SPDX-License-Identifier: GPL-3.0-only
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cwchar>
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    // This host does not match BlackOps.exe: even an inherited opt-in must not
    // enable observation/engine hooks. No device, window, game or XR is created.
    SetEnvironmentVariableW(L"BOVR_INSTRUMENT", L"1");
    SetEnvironmentVariableW(L"BOVR_D3D9EX", L"1");
    SetEnvironmentVariableW(L"BOVR_SCENE_PROBE", L"1");
    SetEnvironmentVariableW(L"BOVR_BACKEND_PROBE", L"1");
    SetEnvironmentVariableW(L"BOVR_XR_STEREO_PROBE", L"1");
    SetEnvironmentVariableW(L"BOVR_QUIT_GATE", L"1");
    HMODULE proxy = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!proxy) { printf("FAIL LoadLibrary error=%lu\n", GetLastError()); return 1; }
    unsigned failures{};
    for (unsigned ordinal = 16; ordinal <= 38; ++ordinal)
        if (!GetProcAddress(proxy, MAKEINTRESOURCEA(ordinal))) { ++failures; printf("FAIL ordinal=%u\n", ordinal); }
    wchar_t system_path[MAX_PATH]{};
    GetSystemDirectoryW(system_path, MAX_PATH); wcscat_s(system_path, L"\\d3d9.dll");
    HMODULE system = LoadLibraryW(system_path);
    const auto get_status = reinterpret_cast<DWORD(WINAPI*)()>(GetProcAddress(proxy, "D3DPERF_GetStatus"));
    const auto direct_status = reinterpret_cast<DWORD(WINAPI*)()>(GetProcAddress(system, "D3DPERF_GetStatus"));
    const auto begin = reinterpret_cast<int(WINAPI*)(D3DCOLOR,LPCWSTR)>(GetProcAddress(proxy, "D3DPERF_BeginEvent"));
    const auto end = reinterpret_cast<int(WINAPI*)()>(GetProcAddress(proxy, "D3DPERF_EndEvent"));
    if (!get_status || !direct_status || !begin || !end) return 1;
    // Exercise both zero-argument and stdcall-argument tail forwarding repeatedly.
    for (unsigned i = 0; i < 500; ++i) {
        if (get_status() != direct_status()) ++failures;
        begin(0xff00ff00u, L"BlackOpsVR offline export forwarding"); end();
    }
    const auto create = reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(proxy, "Direct3DCreate9"));
    const auto direct_create = reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(system, "Direct3DCreate9"));
    if (!create || !direct_create) return 1;
    // Invalid SDK returns no object in stock D3D9, keeping this test graphics-free.
    auto* result = create(0);
    auto* expected = direct_create(0);
    if ((result == nullptr) != (expected == nullptr)) ++failures;
    if (result) result->Release();
    if (expected) expected->Release();
    printf("exports=23 forwarding_iterations=500 game_launches=0 devices_created=0 failures=%u\n", failures);
    FreeLibrary(proxy); FreeLibrary(system);
    return failures ? 1 : 0;
}
