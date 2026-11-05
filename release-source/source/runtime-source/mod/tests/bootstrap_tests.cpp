// SPDX-License-Identifier: GPL-3.0-only
#include "loader/d3d9_observer.hpp"
#include "t5/identity.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
unsigned failures{}, present_calls{}, swap_calls{}, reset_calls{}, release_calls{}, create_calls{};
void check(bool ok, const char* name) { printf("%s %s\n", ok ? "PASS" : "FAIL", name); if (!ok) ++failures; }
struct Fake { void** vtable; };
std::array<void*, 133> device_table{};
std::array<void*, 10> swap_table{};
std::array<void*, 21> factory_table{};
Fake device{device_table.data()}, swap{swap_table.data()}, factory{factory_table.data()};
const RECT rect{1,2,3,4};
ULONG STDMETHODCALLTYPE release(IUnknown*) { ++release_calls; return 8; }
HRESULT STDMETHODCALLTYPE present(IDirect3DDevice9* p, const RECT* a, const RECT* b, HWND w, const RGNDATA* d) {
    ++present_calls;
    check(p == reinterpret_cast<IDirect3DDevice9*>(&device) && a == &rect && b == nullptr && w == reinterpret_cast<HWND>(123) && d == nullptr, "Present arguments preserved");
    return D3DERR_DEVICELOST;
}
HRESULT STDMETHODCALLTYPE swap_present(IDirect3DSwapChain9* p, const RECT* a, const RECT*, HWND, const RGNDATA*, DWORD flags) {
    ++swap_calls; check(p == reinterpret_cast<IDirect3DSwapChain9*>(&swap) && a == &rect && flags == 7, "swap Present arguments preserved");
    return D3DERR_WASSTILLDRAWING;
}
HRESULT STDMETHODCALLTYPE get_swap(IDirect3DDevice9*, UINT index, IDirect3DSwapChain9** output) {
    if (index != 0 || !output) return D3DERR_INVALIDCALL;
    *output = reinterpret_cast<IDirect3DSwapChain9*>(&swap); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE create_swap(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9** output) {
    *output = reinterpret_cast<IDirect3DSwapChain9*>(&swap); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE reset(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*) { ++reset_calls; return D3DERR_DEVICELOST; }
HRESULT STDMETHODCALLTYPE create(IDirect3D9*, UINT adapter, D3DDEVTYPE, HWND, DWORD flags, D3DPRESENT_PARAMETERS*, IDirect3DDevice9** output) {
    ++create_calls; check(adapter == 2 && flags == 33, "CreateDevice arguments preserved");
    *output = reinterpret_cast<IDirect3DDevice9*>(&device); return D3D_OK;
}
}
int main() {
    using namespace bovr;
    set_observation_active(true);
    const std::array<std::uint8_t,3> abc{'a','b','c'};
    check(t5::sha256(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 known vector");
    check(!t5::validate_file(abc).accepted, "wrong length rejected");
    std::vector<std::uint8_t> malformed(t5::kSpFileSize);
    check(!t5::validate_file(malformed).accepted, "malformed DOS header rejected");
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(malformed.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x7fffffff;
    check(!t5::validate_file(malformed).accepted, "out-of-bounds PE rejected");
    // An executable-shaped impostor with the right public PE identity must still
    // be rejected by its complete file hash, even though header checks pass.
    dos->e_lfanew = sizeof(IMAGE_DOS_HEADER);
    IMAGE_NT_HEADERS32 nt{}; nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386; nt.FileHeader.NumberOfSections = 7;
    nt.FileHeader.TimeDateStamp = 1314918417u; nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    nt.FileHeader.Characteristics = 291; nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt.OptionalHeader.AddressOfEntryPoint = 0x56da57; nt.OptionalHeader.ImageBase = 0x400000;
    nt.OptionalHeader.SizeOfImage = 69693440; nt.OptionalHeader.DllCharacteristics = 32768;
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].VirtualAddress = static_cast<DWORD>(t5::kSpImageFileSize);
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size = static_cast<DWORD>(t5::kSpFileSize-t5::kSpImageFileSize);
    std::memcpy(malformed.data() + dos->e_lfanew, &nt, sizeof(nt));
    struct ExpectedSection { const char* name; DWORD va,vsize,raw,pointer,characteristics; };
    const ExpectedSection sections[] = {
        {".text",0x1000,0x5a1a10,5905408,1024,0x60000020}, {".rdata",0x5a3000,0x1c86ba,1869824,5906432,0x40000040},
        {".data",0x76c000,0x3af36c4,238592,7776256,0xc0000040}, {".tls",0x4260000,0x6d,512,8014848,0xc0000040},
        {".rodata",0x4261000,0x770,2048,8015360,0x40000040}, {".version",0x4262000,0x4,512,8017408,0x40000040},
        {".rsrc",0x4263000,0x130c4,78336,8017920,0x40000040}
    };
    auto section_offset=static_cast<std::size_t>(dos->e_lfanew)+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+sizeof(IMAGE_OPTIONAL_HEADER32);
    for(std::size_t i{};i<sizeof(sections)/sizeof(sections[0]);++i){IMAGE_SECTION_HEADER section{};std::memcpy(section.Name,sections[i].name,std::strlen(sections[i].name));
        section.VirtualAddress=sections[i].va;section.Misc.VirtualSize=sections[i].vsize;section.SizeOfRawData=sections[i].raw;
        section.PointerToRawData=sections[i].pointer;section.Characteristics=sections[i].characteristics;
        std::memcpy(malformed.data()+section_offset+i*sizeof(section),&section,sizeof(section));}
    const auto impostor = t5::validate_file(malformed);
    check(!impostor.accepted && impostor.reason == "whole-file SHA256 mismatch" && impostor.sha256.size() == 64,
          "matching PE identity cannot bypass whole-file hash");

    device_table[2] = reinterpret_cast<void*>(&release);
    device_table[13] = reinterpret_cast<void*>(&create_swap);
    device_table[14] = reinterpret_cast<void*>(&get_swap);
    device_table[16] = reinterpret_cast<void*>(&reset);
    device_table[17] = reinterpret_cast<void*>(&present);
    swap_table[2] = reinterpret_cast<void*>(&release);
    swap_table[3] = reinterpret_cast<void*>(&swap_present);
    factory_table[16] = reinterpret_cast<void*>(&create);
    check(!observe_factory(nullptr, false), "null factory rejected");
    check(observe_factory(reinterpret_cast<IDirect3D9*>(&factory), false), "factory slot installed");
    check(observe_factory(reinterpret_cast<IDirect3D9*>(&factory), false), "factory installation idempotent");
    IDirect3DDevice9* created{};
    D3DPRESENT_PARAMETERS params{};
    auto* f = reinterpret_cast<IDirect3D9*>(&factory);
    check(f->CreateDevice(2, D3DDEVTYPE_HAL, nullptr, 33, &params, &created) == D3D_OK &&
          created == reinterpret_cast<IDirect3DDevice9*>(&device) && create_calls == 1, "CreateDevice result and object identity preserved");
    check(created->Present(&rect, nullptr, reinterpret_cast<HWND>(123), nullptr) == D3DERR_DEVICELOST && present_calls == 1,
          "lost-device Present returned unchanged once");
    IDirect3DSwapChain9* chain{};
    check(created->GetSwapChain(0, &chain) == D3D_OK && chain == reinterpret_cast<IDirect3DSwapChain9*>(&swap), "GetSwapChain identity preserved");
    check(chain->Present(&rect, nullptr, nullptr, nullptr, 7) == D3DERR_WASSTILLDRAWING && swap_calls == 1, "swap Present returned unchanged once");
    check(created->Reset(&params) == D3DERR_DEVICELOST && reset_calls == 1, "failed Reset returned unchanged once");
    set_observation_active(false);
    check(created->Present(&rect, nullptr, reinterpret_cast<HWND>(123), nullptr) == D3DERR_DEVICELOST && present_calls == 2,
          "passive observer continues stock call");
    check(created->Release() == 8 && release_calls >= 1, "Release result preserved");
    factory_table[16] = reinterpret_cast<void*>(&release);
    check(!observe_factory(reinterpret_cast<IDirect3D9*>(&factory), false), "foreign slot replacement not overwritten");
    printf("failures=%u\n", failures);
    return failures ? 1 : 0;
}
