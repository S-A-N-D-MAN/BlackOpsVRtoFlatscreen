// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3d9.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "loader/trace.hpp"

namespace bovr::render {
// Owner-thread D3D9 copy. The bridge fences its subsequent copy before D3D11
// samples it. A producer can only publish the image made in its own native pass.
class ScopeStaging {
public:
    ~ScopeStaging(){release();}
    ScopeStaging()=default;
    ScopeStaging(const ScopeStaging&)=delete;
    ScopeStaging& operator=(const ScopeStaging&)=delete;
    void invalidate() noexcept {producer_=0;}
    void release() noexcept {
        invalidate();if(surface_){surface_->Release();surface_=nullptr;}
        device_=nullptr;owner_=0;description_={};
    }
    bool capture(IDirect3DDevice9* device,std::uint64_t producer) noexcept {
        invalidate();
        if(!device||!producer||(owner_&&owner_!=GetCurrentThreadId()))return false;
        IDirect3DSurface9* source{};
        if(FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&source))||!source)return false;
        struct Release{IDirect3DSurface9* p;~Release(){p->Release();}} release_source{source};
        D3DSURFACE_DESC d{};
        if(FAILED(source->GetDesc(&d))||d.MultiSampleType!=D3DMULTISAMPLE_NONE)return false;
        if(surface_&&(device_!=device||d.Width!=description_.Width||d.Height!=description_.Height||d.Format!=description_.Format))release();
        if(!surface_){
            if(FAILED(device->CreateRenderTarget(d.Width,d.Height,d.Format,D3DMULTISAMPLE_NONE,0,FALSE,&surface_,nullptr))||!surface_)return false;
            device_=device;description_=d;owner_=GetCurrentThreadId();
        }
        if(FAILED(device->StretchRect(source,nullptr,surface_,nullptr,D3DTEXF_NONE)))return false;
        producer_=producer;diagnose_pixels();return true;
    }
    IDirect3DSurface9* get(IDirect3DDevice9* device,std::uint64_t producer) const noexcept {
        return producer&&producer==producer_&&device==device_&&owner_==GetCurrentThreadId()?surface_:nullptr;
    }
private:
    // Explicit simulator-only opt-in: capture the exact owned thermal surface,
    // once on its 30th successful copy. The process-wide budget survives device
    // reset and ScopeStaging replacement. Default-off does no GPU readback.
    void diagnose_pixels() noexcept {
        struct LastError {DWORD saved{GetLastError()};~LastError(){SetLastError(saved);}} preserve;
        struct Diagnostic {bool configured{},enabled{};unsigned copies{};std::wstring directory;};
        static Diagnostic diagnostic;
        std::wstring path;
        try {
            if(!diagnostic.configured){
                diagnostic.configured=true;
                wchar_t directory[32768]{};
                const auto length=GetEnvironmentVariableW(L"BOVR_SCOPE_PIXEL_DIRECTORY",directory,32768);
                if(!length||length>=32768)return;
                const bool drive=length>=3&&directory[1]==L':'&&(directory[2]==L'\\'||directory[2]==L'/');
                const bool unc=length>=4&&directory[0]==L'\\'&&directory[1]==L'\\';
                if(!drive&&!unc)return;
                const auto attributes=GetFileAttributesW(directory);
                if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY)==0)return;
                diagnostic.directory=directory;diagnostic.enabled=true;
            }
            if(!diagnostic.enabled||diagnostic.copies>=30||!surface_||!device_||!producer_||owner_!=GetCurrentThreadId())return;
            if(++diagnostic.copies!=30)return;
            path=diagnostic.directory+L"\\scope-source-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(producer_)+L".bmp";
            D3DSURFACE_DESC d{};
            HRESULT hr=surface_->GetDesc(&d);
            if(SUCCEEDED(hr)&&(!d.Width||!d.Height||d.Width>5120||d.Height>4096||d.MultiSampleType!=D3DMULTISAMPLE_NONE||
               (d.Format!=D3DFMT_A8R8G8B8&&d.Format!=D3DFMT_X8R8G8B8)))hr=D3DERR_INVALIDCALL;
            struct Surface {IDirect3DSurface9* value{};~Surface(){if(value)value->Release();}} system;
            if(SUCCEEDED(hr))hr=device_->CreateOffscreenPlainSurface(d.Width,d.Height,d.Format,D3DPOOL_SYSTEMMEM,&system.value,nullptr);
            if(SUCCEEDED(hr))hr=device_->GetRenderTargetData(surface_,system.value);
            bool written=false;
            D3DLOCKED_RECT locked{};
            if(SUCCEEDED(hr)){
                hr=system.value->LockRect(&locked,nullptr,D3DLOCK_READONLY);
                if(SUCCEEDED(hr)){
                    struct Unlock {IDirect3DSurface9* value;~Unlock(){value->UnlockRect();}} unlock{system.value};
                    if(!locked.pBits||locked.Pitch<static_cast<INT>(d.Width*4))hr=D3DERR_INVALIDCALL;
                    else {
                        const auto bytes=std::size_t(d.Width)*d.Height*4;
                        BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};
                        file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=static_cast<DWORD>(file.bfOffBits+bytes);
                        info.biSize=sizeof(info);info.biWidth=static_cast<LONG>(d.Width);info.biHeight=-static_cast<LONG>(d.Height);
                        info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;info.biSizeImage=static_cast<DWORD>(bytes);
                        std::vector<std::uint8_t> output(file.bfSize);
                        std::memcpy(output.data(),&file,sizeof(file));std::memcpy(output.data()+sizeof(file),&info,sizeof(info));
                        for(UINT y=0;y<d.Height;++y)std::memcpy(output.data()+file.bfOffBits+std::size_t(y)*d.Width*4,
                            static_cast<const std::uint8_t*>(locked.pBits)+std::size_t(y)*locked.Pitch,d.Width*4);
                        const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
                        if(handle==INVALID_HANDLE_VALUE)hr=HRESULT_FROM_WIN32(GetLastError());
                        else {
                            struct Close {HANDLE value;~Close(){CloseHandle(value);}} close{handle};
                            DWORD count{};
                            if(!WriteFile(handle,output.data(),static_cast<DWORD>(output.size()),&count,nullptr))hr=HRESULT_FROM_WIN32(GetLastError());
                            else if(count!=output.size())hr=HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
                            else written=true;
                        }
                    }
                }
            }
            trace("t5_scope_pixel_probe","producer=%llu sample=30 hr=0x%08lx written=%u path=%ls",producer_,static_cast<unsigned long>(hr),written,path.c_str());
        }catch(...){
            trace("t5_scope_pixel_probe","producer=%llu sample=30 hr=0x%08lx written=0 path=%ls exception=1",producer_,static_cast<unsigned long>(E_FAIL),path.c_str());
        }
    }
    IDirect3DSurface9* surface_{};
    IDirect3DDevice9* device_{};
    D3DSURFACE_DESC description_{};
    std::uint64_t producer_{};
    DWORD owner_{};
};
}
