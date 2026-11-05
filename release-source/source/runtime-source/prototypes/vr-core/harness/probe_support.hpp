#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// Standalone synthetic transport probe. No game addresses or game input.
#include "blackopsvr/core_config.hpp"
#include "d3d9ex_bridge.h"
#include "d3d11_compositor.h"
#include "input_focus_gate.hpp"
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using Microsoft::WRL::ComPtr;
namespace xr = blackopsvr::xr;
namespace fs = std::filesystem;

namespace {
constexpr unsigned kWidth = 1024, kHeight = 512;
std::ofstream log_file;
void log_callback(void*, xr::LogLevel, const char* message) {
    log_file << message << '\n';
    log_file.flush();
}
bool write_command(const fs::path& directory, const char* name, const char* json) {
    const auto target = directory / name;
    const auto temporary = directory / (std::string(name) + ".tmp");
    { std::ofstream output(temporary, std::ios::binary); output << json; }
    return MoveFileExW(temporary.c_str(), target.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}
void save_screenshot(const fs::path& simulator, const fs::path& output,
                     const char* label) {
    std::error_code ec;
    if (fs::exists(simulator / "screenshot.bmp"))
        fs::copy_file(simulator / "screenshot.bmp", output / (std::string(label) + ".bmp"),
                      fs::copy_options::overwrite_existing, ec);
    if (fs::exists(simulator / "screenshot_status.json"))
        fs::copy_file(simulator / "screenshot_status.json", output / (std::string(label) + "-status.json"),
                      fs::copy_options::overwrite_existing, ec);
}
struct TestSurface {
    HWND window{};
    ComPtr<IDirect3D9Ex> api;
    ComPtr<IDirect3DDevice9Ex> device;
    ~TestSurface() { device.Reset(); api.Reset(); if (window) DestroyWindow(window); }
    D3DPRESENT_PARAMETERS parameters() const {
        D3DPRESENT_PARAMETERS present{};
        present.BackBufferWidth=kWidth; present.BackBufferHeight=kHeight;
        present.BackBufferFormat=D3DFMT_A8R8G8B8; present.BackBufferCount=1;
        present.SwapEffect=D3DSWAPEFFECT_DISCARD; present.hDeviceWindow=window;
        present.Windowed=TRUE; present.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        return present;
    }
    HRESULT reset() { auto present=parameters(); return device->ResetEx(&present,nullptr); }
    bool initialize(ID3D11Device* consumer) {
        ComPtr<IDXGIDevice> dxgi_device;
        ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC description{};
        if (FAILED(consumer->QueryInterface(IID_PPV_ARGS(&dxgi_device))) ||
            FAILED(dxgi_device->GetAdapter(&adapter)) ||
            FAILED(adapter->GetDesc(&description)) ||
            FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION, &api))) return false;
        UINT selected = UINT_MAX;
        for (UINT candidate = 0; candidate < api->GetAdapterCount(); ++candidate) {
            LUID luid{};
            if (SUCCEEDED(api->GetAdapterLUID(candidate, &luid)) &&
                luid.HighPart == description.AdapterLuid.HighPart &&
                luid.LowPart == description.AdapterLuid.LowPart) selected = candidate;
        }
        if (selected == UINT_MAX) return false;
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"BlackOpsVrSyntheticD3D9Surface";
        RegisterClassW(&wc);
        window = CreateWindowW(wc.lpszClassName, L"Black Ops VR synthetic surface",
            WS_OVERLAPPEDWINDOW, 0, 0, kWidth, kHeight, nullptr, nullptr, wc.hInstance, nullptr);
        if (!window) return false;
        D3DPRESENT_PARAMETERS present{};
        present.BackBufferWidth = kWidth;
        present.BackBufferHeight = kHeight;
        present.BackBufferFormat = D3DFMT_A8R8G8B8;
        present.BackBufferCount = 1;
        present.SwapEffect = D3DSWAPEFFECT_DISCARD;
        present.hDeviceWindow = window;
        present.Windowed = TRUE;
        present.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        const HRESULT result = api->CreateDeviceEx(selected, D3DDEVTYPE_HAL, window,
            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE |
            D3DCREATE_MULTITHREADED, &present, nullptr, &device);
        log_file << "D3D9Ex CreateDevice result=" << std::hex << result << std::dec
                 << " adapter=" << selected << " LUID=" << description.AdapterLuid.HighPart
                 << ':' << description.AdapterLuid.LowPart << '\n';
        return SUCCEEDED(result);
    }
    bool draw() {
        D3DRECT left{0,0,kWidth/2,kHeight};
        D3DRECT right{kWidth/2,0,kWidth,kHeight};
        D3DRECT letter_l[] = {{64,64,96,384},{64,352,320,384}};
        D3DRECT letter_r[] = {{576,64,608,384},{576,64,832,96},
                              {800,64,832,224},{576,192,832,224},{736,224,768,384}};
        return SUCCEEDED(device->Clear(1,&left,D3DCLEAR_TARGET,D3DCOLOR_XRGB(220,30,30),1,0)) &&
               SUCCEEDED(device->Clear(1,&right,D3DCLEAR_TARGET,D3DCOLOR_XRGB(25,60,220),1,0)) &&
               SUCCEEDED(device->Clear(2,letter_l,D3DCLEAR_TARGET,D3DCOLOR_XRGB(245,245,245),1,0)) &&
               SUCCEEDED(device->Clear(5,letter_r,D3DCLEAR_TARGET,D3DCOLOR_XRGB(245,225,40),1,0));
    }
    bool draw_eye(unsigned eye,std::uint32_t producer_serial) {
        if (eye>1) return false;
        D3DRECT region{static_cast<LONG>(eye*kWidth/2),0,static_cast<LONG>((eye+1)*kWidth/2),kHeight};
        D3DRECT letter_l[]={{64,64,96,384},{64,352,320,384}};
        D3DRECT letter_r[]={{576,64,608,384},{576,64,832,96},{800,64,832,224},{576,192,832,224},{736,224,768,384}};
        const auto marker_x=eye?900:400;
        D3DRECT marker{marker_x,400,marker_x+16,416};
        const auto code=producer_serial^(eye?0x00aa55aau:0u);
        return SUCCEEDED(device->Clear(1,&region,D3DCLEAR_TARGET,
            eye?D3DCOLOR_XRGB(25,60,220):D3DCOLOR_XRGB(220,30,30),1,0)) &&
            SUCCEEDED(device->Clear(eye?5:2,eye?letter_r:letter_l,D3DCLEAR_TARGET,
                eye?D3DCOLOR_XRGB(245,225,40):D3DCOLOR_XRGB(245,245,245),1,0)) &&
            SUCCEEDED(device->Clear(1,&marker,D3DCLEAR_TARGET,0xff000000u|(code&0xffffffu),1,0));
    }
};
bool verify_shared_pixels(ID3D11Device* device, ID3D11DeviceContext* context,
                          const xr::D3D11SourceFrame& source) {
    D3D11_TEXTURE2D_DESC description{};
    source.texture->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&description, nullptr, &staging))) return false;
    context->CopyResource(staging.Get(), source.texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) return false;
    const auto pixel = [&](unsigned x, unsigned y) {
        return static_cast<const unsigned char*>(mapped.pData) + y*mapped.RowPitch + x*4;
    };
    const auto left = pixel(400,400);
    const auto right = pixel(900,400);
    const bool bgra = description.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                      description.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS;
    const unsigned red = bgra ? 2 : 0, blue = bgra ? 0 : 2;
    const bool matches = left[red] == 220 && left[1] == 30 && left[blue] == 30 &&
                         right[red] == 25 && right[1] == 60 && right[blue] == 220;
    log_file << "Shared pixel readback format=" << description.Format
             << " leftRGB=" << unsigned(left[red]) << ',' << unsigned(left[1]) << ',' << unsigned(left[blue])
             << " rightRGB=" << unsigned(right[red]) << ',' << unsigned(right[1]) << ',' << unsigned(right[blue])
             << " matches=" << matches << '\n';
    context->Unmap(staging.Get(),0);
    return matches;
}
} // namespace
