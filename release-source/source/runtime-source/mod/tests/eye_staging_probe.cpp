// SPDX-License-Identifier: GPL-3.0-only
// Explicit hidden-device GPU test. No retail game, OpenXR, Steam or network.
#include "render/eye_staging.hpp"
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
unsigned checks{}, failures{};
void check(bool ok,const char* text,HRESULT hr=S_OK) {
    ++checks;
    if (!ok) { ++failures; printf("FAIL %s hr=0x%08lx\n",text,static_cast<unsigned long>(hr)); }
}
bool read_pixels(IDirect3DDevice9* device,IDirect3DSurface9* source,UINT width,UINT height,
                 DWORD left,DWORD right) {
    IDirect3DSurface9* readback{};
    auto hr=device->CreateOffscreenPlainSurface(width,height,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr);
    if (FAILED(hr)) return false;
    hr=device->GetRenderTargetData(source,readback);
    D3DLOCKED_RECT locked{};
    if (SUCCEEDED(hr)) hr=readback->LockRect(&locked,nullptr,D3DLOCK_READONLY);
    bool matches=SUCCEEDED(hr);
    if (matches) {
        for (UINT y=0;y<height;++y) {
            const auto* row=reinterpret_cast<const DWORD*>(static_cast<const unsigned char*>(locked.pBits)+y*locked.Pitch);
            for (UINT x=0;x<width;++x)
                if ((row[x]&0x00ffffff)!=((x<width/2?left:right)&0x00ffffff)) matches=false;
        }
        readback->UnlockRect();
    }
    readback->Release(); return matches;
}
bool surface_same(IDirect3DSurface9* a,IDirect3DSurface9* b) { return a==b; }
void run_size(IDirect3DDevice9Ex* device,bovr::render::EyeStaging& staging,UINT width,UINT height) {
    IDirect3DSurface9* backbuffer{};
    auto hr=device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer);
    check(SUCCEEDED(hr)&&backbuffer,"read backbuffer",hr); if (!backbuffer) return;
    const std::array<RECT,2> rects={RECT{0,0,static_cast<LONG>(width/2),static_cast<LONG>(height)},
        RECT{static_cast<LONG>(width/2),0,static_cast<LONG>(width),static_cast<LONG>(height)}};
    check(FAILED(staging.begin(device,0,rects)),"zero producer rejected");
    auto overlap=rects; overlap[1].left=0;
    check(FAILED(staging.begin(device,1,overlap)),"overlapping eye rectangles rejected");
    auto outside=rects; outside[1].right=static_cast<LONG>(width+1);
    check(FAILED(staging.begin(device,1,outside)),"out of bounds rectangle rejected");
    for (std::uint64_t frame=1;frame<=120;++frame) {
        const DWORD left=0x00ff0000u|static_cast<DWORD>(frame);
        const DWORD right=0x000000ffu|(static_cast<DWORD>(frame)<<8);
        const bool active_scene=(frame%2)==0;
        if (active_scene) { hr=device->BeginScene(); check(SUCCEEDED(hr),"native BeginScene",hr); }
        D3DVIEWPORT9 viewport{1,2,width-2,height-4,0.2f,0.9f};
        RECT scissor{3,4,static_cast<LONG>(width-3),static_cast<LONG>(height-4)};
        device->SetViewport(&viewport); device->SetScissorRect(&scissor);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
        hr=staging.begin(device,frame,rects); check(SUCCEEDED(hr),"begin immutable producer",hr);
        check(FAILED(staging.restore_pair(device,frame)),"empty pair rejected");
        check(FAILED(staging.save_eye(device,frame,1)),"right before left rejected");
        hr=device->ColorFill(backbuffer,nullptr,0); check(SUCCEEDED(hr),"first full-target clear",hr);
        hr=device->ColorFill(backbuffer,&rects[0],left); check(SUCCEEDED(hr),"left producer pixels",hr);
        hr=staging.save_eye(device,frame,0); check(SUCCEEDED(hr),"save left",hr);
        check(FAILED(staging.save_eye(device,frame,0)),"duplicate left rejected");
        check(FAILED(staging.restore_pair(device,frame)),"one-eye pair rejected");
        hr=device->ColorFill(backbuffer,nullptr,0); check(SUCCEEDED(hr),"second full-target clear erases left",hr);
        hr=device->ColorFill(backbuffer,&rects[1],right); check(SUCCEEDED(hr),"right producer pixels",hr);
        hr=staging.save_eye(device,frame,1); check(SUCCEEDED(hr),"save right",hr);
        if (frame==1) check(read_pixels(device,backbuffer,width,height,0,right),
                            "negative control confirms original left eye really was erased");
        check(staging.completed_eye_mask()==3,"both copied eyes required");
        check(FAILED(staging.restore_pair(device,frame+1)),"wrong producer rejected");
        // Simulate a later full-screen backend pass as well. Neither old
        // backbuffer half may supply the final image after recomposition.
        hr=device->ColorFill(backbuffer,nullptr,0x0000ff00); check(SUCCEEDED(hr),"late full-target overwrite",hr);
        hr=staging.restore_pair(device,frame); check(SUCCEEDED(hr),"restore exact stamped pair",hr);
        check(FAILED(staging.restore_pair(device,frame)),"consumed pair cannot replay");
        D3DVIEWPORT9 after{}; RECT after_scissor{}; DWORD scissor_enabled{};
        device->GetViewport(&after); device->GetScissorRect(&after_scissor);
        device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor_enabled);
        IDirect3DSurface9* current{}; device->GetRenderTarget(0,&current);
        check(memcmp(&viewport,&after,sizeof(viewport))==0 && memcmp(&scissor,&after_scissor,sizeof(scissor))==0 &&
              scissor_enabled==TRUE && surface_same(current,backbuffer),"native graphics state preserved");
        if (current) current->Release();
        if (active_scene) { hr=device->EndScene(); check(SUCCEEDED(hr),"native EndScene still balanced",hr); }
        check(read_pixels(device,backbuffer,width,height,left,right),"every pixel matches this frame after both clears");
        hr=device->PresentEx(nullptr,nullptr,nullptr,nullptr,0); check(SUCCEEDED(hr),"native Present remains healthy",hr);
    }
    hr=staging.begin(device,501,rects); check(SUCCEEDED(hr),"begin before foreign thread check",hr);
    bool rejected{};
    std::thread foreign([&]{ rejected=FAILED(staging.begin(device,502,rects)) && FAILED(staging.save_eye(device,501,0)); });
    foreign.join(); check(rejected,"foreign thread cannot mutate GPU staging");
    staging.invalidate(); check(FAILED(staging.restore_pair(device,501)),"invalidated pending frame rejected");
    backbuffer->Release();
}
}
int main(int argc,char** argv) {
    if (argc!=2 || strcmp(argv[1],"--hidden-device")!=0) { puts("Pass --hidden-device to run the explicit offline graphics test."); return 2; }
    HWND window=CreateWindowExW(0,L"STATIC",L"BlackOpsVR eye staging GPU proof",WS_OVERLAPPEDWINDOW,
        0,0,160,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if (!window) return 1;
    wchar_t path[MAX_PATH]{}; GetSystemDirectoryW(path,MAX_PATH); wcscat_s(path,L"\\d3d9.dll");
    HMODULE library=LoadLibraryW(path);
    using CreateEx=HRESULT(WINAPI*)(UINT,IDirect3D9Ex**);
    const auto create=reinterpret_cast<CreateEx>(GetProcAddress(library,"Direct3DCreate9Ex"));
    IDirect3D9Ex* factory{}; auto hr=create?create(D3D_SDK_VERSION,&factory):E_FAIL;
    check(SUCCEEDED(hr)&&factory,"system factory",hr);
    if (!factory) { DestroyWindow(window); return 1; }
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed=TRUE; pp.hDeviceWindow=window; pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth=128; pp.BackBufferHeight=64; pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9Ex* device{};
    hr=factory->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,nullptr,&device);
    check(SUCCEEDED(hr)&&device,"system Ex device",hr);
    if (device) {
        bovr::render::EyeStaging staging;
        run_size(device,staging,128,64);
        staging.release(); // default-pool retirement precedes the real reset
        pp.BackBufferWidth=96; pp.BackBufferHeight=48;
        hr=device->ResetEx(&pp,nullptr); check(SUCCEEDED(hr),"resize ResetEx after staging retirement",hr);
        if (SUCCEEDED(hr)) run_size(device,staging,96,48);
        staging.release();
        const auto refs=device->Release(); check(refs==0,"all staging and transient device references retired");
    }
    factory->Release(); FreeLibrary(library); DestroyWindow(window);
    printf("%s checks=%u failures=%u frames=240 active_scene_frames=120 pixel_comparisons=1536000\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
