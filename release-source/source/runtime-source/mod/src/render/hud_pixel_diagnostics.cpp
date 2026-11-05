// SPDX-License-Identifier: GPL-3.0-only
#include "render/hud_pixel_diagnostics.hpp"
#include "loader/trace.hpp"
#include <array>
#include <cstring>
#include <sstream>
#include <vector>
namespace bovr::render {
namespace {
struct LastError {DWORD value{GetLastError()};~LastError(){SetLastError(value);}};
template<class T>struct Com {T* value{};~Com(){if(value)value->Release();}T** put(){return &value;}};
template<class T>T read(const std::uint8_t* p){T v{};memcpy(&v,p,sizeof(v));return v;}
bool absolute_directory(const std::wstring& path) {
    if(path.size()<3||path.size()>30000||path[1]!=L':'||(path[2]!=L'\\'&&path[2]!=L'/'))return false;
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
bool write_new(const std::wstring& path,std::span<const std::uint8_t> bytes) {
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written{};const bool ok=bytes.size()<=MAXDWORD&&WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size();
    CloseHandle(file);return ok;
}
bool write_text(const std::wstring& path,const std::string& value){return write_new(path,{reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
bool write_bitmap(const std::wstring& path,UINT width,UINT height,INT pitch,const void* bits) {
    if(!width||!height||width>5120||height>4096||!bits||pitch<static_cast<INT>(width*4))return false;
    const auto size=std::size_t(width)*height*4;
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};
    file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=static_cast<DWORD>(file.bfOffBits+size);
    info.biSize=sizeof(info);info.biWidth=width;info.biHeight=-static_cast<LONG>(height);info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;info.biSizeImage=static_cast<DWORD>(size);
    std::vector<std::uint8_t> output(file.bfSize);memcpy(output.data(),&file,sizeof(file));memcpy(output.data()+sizeof(file),&info,sizeof(info));
    for(UINT y=0;y<height;++y)memcpy(output.data()+file.bfOffBits+std::size_t(y)*width*4,static_cast<const std::uint8_t*>(bits)+std::size_t(y)*pitch,width*4);
    return write_new(path,output);
}
bool copy_material_name(std::uintptr_t material,char* output,unsigned capacity) noexcept {
    __try {
        const auto* name=*reinterpret_cast<const char* const*>(material);
        if(!name)return false;
        for(unsigned i=0;i<capacity-1;++i){const char c=name[i];if(!c)break;output[i]=(c>=32&&c<127&&c!='\t')?c:'?';}
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    return true;
}
std::string material_name(std::uintptr_t material) {
    char output[256]{};
    if(!copy_material_name(material,output,sizeof(output)))return "<unreadable>";
    return output;
}
std::string summarize(std::span<const std::uint8_t> bytes) {
    std::ostringstream text;text<<"offset\tid\ttag\tlength\tdetails\n";
    for(std::size_t offset=0,count=0;offset+4<=bytes.size()&&count<4096;++count) {
        const auto* p=bytes.data()+offset;const auto length=read<std::uint16_t>(p);const unsigned id=p[2];
        text<<offset<<'\t'<<id<<'\t'<<unsigned(p[3])<<'\t'<<length<<'\t';
        if(length<4||(length&3)||length>bytes.size()-offset){text<<"invalid length\n";break;}
        if((id==10||id==15)&&length==48) {
            const auto material=read<std::uint32_t>(p+4);text<<"material=0x"<<std::hex<<material<<std::dec<<" name="<<material_name(material);
            for(unsigned i=8;i<=40;i+=4)text<<" f"<<i<<'='<<read<float>(p+i);
            text<<" color=0x"<<std::hex<<read<std::uint32_t>(p+44)<<std::dec;
        } else if(id==17&&length>=96) {
            text<<"font_identity=0x"<<std::hex<<read<std::uint32_t>(p+20)<<std::dec;
            for(auto i:{4,8,12,16,24,28})text<<" f"<<i<<'='<<read<float>(p+i);
            text<<" text=";for(unsigned i=92;i<length&&p[i];++i)text<<((p[i]>=32&&p[i]<127)?char(p[i]):'?');
        } else if(id==6&&length==20)for(unsigned i=4;i<=16;i+=4)text<<" i"<<i<<'='<<read<int>(p+i);
        text<<'\n';if(id==0)break;offset+=length;
    }
    return text.str();
}
bool stage_name(const char* stage) {
    return stage&&(!strcmp(stage,"native_before_restore")||!strcmp(stage,"world_restored")||
        !strcmp(stage,"hud_completed")||!strcmp(stage,"pre_present"));
}
void describe(std::ostringstream& text,const char* name,IDirect3DSurface9* surface) {
    D3DSURFACE_DESC d{};const auto hr=surface?surface->GetDesc(&d):E_POINTER;
    text<<name<<"_identity="<<surface<<" hr="<<std::hex<<static_cast<unsigned long>(hr)<<std::dec
        <<" width="<<d.Width<<" height="<<d.Height<<" format="<<unsigned(d.Format)<<" multisample="<<unsigned(d.MultiSampleType)<<'\n';
}
}
bool HudPixelDiagnostics::begin(IDirect3DDevice9* device,const HudPixelIdentity& identity,
    std::span<const std::uint8_t> source,std::span<const std::uint8_t> left,std::span<const std::uint8_t> right) noexcept {
    LastError preserve;
    try {
        if(!configured_) {
            configured_=true;wchar_t option[8]{},path[32768]{};
            GetEnvironmentVariableW(L"BOVR_HUD_PIXEL_PROBE",option,8);
            const auto length=GetEnvironmentVariableW(L"BOVR_HUD_PIXEL_DIRECTORY",path,32768);
            enabled_=!wcscmp(option,L"1")&&length&&length<32768&&absolute_directory(path);
            if(enabled_){directory_=path;owner_=GetCurrentThreadId();}
        }
        const auto now=GetTickCount64();
        if(!enabled_||active_||captured_>=3||GetCurrentThreadId()!=owner_||!device||!identity.producer||
           !identity.generation||!identity.xr_frame||!identity.backend||source.empty()||source.size()>4u*0x18000u||
           left.size()>4u*0x18000u+28||right.size()>4u*0x18000u+28||
           (captured_&&(now<last_begin_||now-last_begin_<1500)))return false;
        identity_=identity;last_begin_=now;++captured_;active_=true;
        prefix_=directory_+L"\\hud-pixels-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(captured_)+L"-"+std::to_wstring(identity.producer);
        const std::array<std::pair<const wchar_t*,std::span<const std::uint8_t>>,3> lists{{{L"source",source},{L"left",left},{L"right",right}}};
        unsigned lists_written{},summaries_written{};
        for(const auto& [name,bytes]:lists){lists_written+=write_new(prefix_+L"-"+name+L".bin",bytes);summaries_written+=write_text(prefix_+L"-"+name+L".tsv",summarize(bytes));}
        trace("t5_hud_pixel_probe_begin","sample=%u generation=%llu xr_frame=%llu producer=%llu native_frame=%lu backend=0x%lx output_prefix=%ls max_samples=3",
              captured_,identity.generation,identity.xr_frame,identity.producer,identity.native_frame,static_cast<unsigned long>(identity.backend),prefix_.c_str());
        trace("t5_hud_pixel_probe_lists","producer=%llu binary_files=%u summary_files=%u expected_each=3",identity.producer,lists_written,summaries_written);
        capture(device,identity.producer,"native_before_restore");return true;
    } catch(...) {active_=false;return false;}
}
void HudPixelDiagnostics::capture(IDirect3DDevice9* device,std::uint64_t producer,const char* stage,bool final) noexcept {
    LastError preserve;
    if(!active_||producer!=identity_.producer||GetCurrentThreadId()!=owner_||!device||!stage_name(stage))return;
    try {
        std::ostringstream text;text<<"stage="<<stage<<" tick="<<GetTickCount64()<<" thread="<<GetCurrentThreadId()
            <<" generation="<<identity_.generation<<" xr_frame="<<identity_.xr_frame<<" producer="<<identity_.producer
            <<" backend="<<std::hex<<identity_.backend<<std::dec<<" native_frame="<<identity_.native_frame<<'\n';
        Com<IDirect3DSurface9> bb,rt,system;D3DSURFACE_DESC d{};D3DVIEWPORT9 viewport{};RECT scissor{};DWORD scissor_enabled{};
        auto hr=device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,bb.put());
        const auto rt_hr=device->GetRenderTarget(0,rt.put());const auto viewport_hr=device->GetViewport(&viewport);
        const auto scissor_hr=device->GetScissorRect(&scissor);const auto enabled_hr=device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor_enabled);
        describe(text,"backbuffer",bb.value);describe(text,"render_target",rt.value);
        text<<"bb_rt_interface_pointer_equal="<<(bb.value&&bb.value==rt.value)<<" rt_hr="<<std::hex<<static_cast<unsigned long>(rt_hr)
            <<" viewport_hr="<<static_cast<unsigned long>(viewport_hr)<<" scissor_hr="<<static_cast<unsigned long>(scissor_hr)
            <<" enabled_hr="<<static_cast<unsigned long>(enabled_hr)<<std::dec<<" viewport="<<viewport.X<<','<<viewport.Y<<','<<viewport.Width<<','<<viewport.Height
            <<" scissor="<<scissor.left<<','<<scissor.top<<','<<scissor.right<<','<<scissor.bottom<<" scissor_enabled="<<scissor_enabled<<'\n';
        if(SUCCEEDED(hr)&&bb.value)hr=bb.value->GetDesc(&d);
        if(SUCCEEDED(hr)&&(!d.Width||!d.Height||d.Width>5120||d.Height>4096||d.MultiSampleType!=D3DMULTISAMPLE_NONE||
           (d.Format!=D3DFMT_A8R8G8B8&&d.Format!=D3DFMT_X8R8G8B8)))hr=D3DERR_INVALIDCALL;
        if(SUCCEEDED(hr))hr=device->CreateOffscreenPlainSurface(d.Width,d.Height,d.Format,D3DPOOL_SYSTEMMEM,system.put(),nullptr);
        if(SUCCEEDED(hr))hr=device->GetRenderTargetData(bb.value,system.value);
        std::wstring suffix;for(const char* p=stage;*p;++p)suffix+=wchar_t(*p);
        bool written{};D3DLOCKED_RECT locked{};
        if(SUCCEEDED(hr)) {
            hr=system.value->LockRect(&locked,nullptr,D3DLOCK_READONLY);
            if(SUCCEEDED(hr)) {
                // Unlock even if allocation or file output throws.
                try {written=write_bitmap(prefix_+L"-"+suffix+L".bmp",d.Width,d.Height,locked.Pitch,locked.pBits);}
                catch(...) {system.value->UnlockRect();throw;}
                system.value->UnlockRect();
            }
        }
        text<<"readback_hr="<<std::hex<<static_cast<unsigned long>(hr)<<std::dec<<" bitmap_written="<<written<<" native_draws=0 target_binds=0\n";
        write_text(prefix_+L"-"+suffix+L".txt",text.str());
        trace("t5_hud_pixel_probe_stage","producer=%llu stage=%s hr=0x%08lx bitmap_written=%u",producer,stage,static_cast<unsigned long>(hr),written);
    } catch(...) {trace("t5_hud_pixel_probe_failed","producer=%llu stage=%s reason=exception",producer,stage);}
    if(final)active_=false;
}
void HudPixelDiagnostics::abandon(std::uint64_t producer,const char* reason) noexcept {
    if(active_&&(!producer||producer==identity_.producer)&&GetCurrentThreadId()==owner_) {
        active_=false;trace("t5_hud_pixel_probe_abandoned","producer=%llu reason=%s",producer,reason);
    }
}
}
