// SPDX-License-Identifier: GPL-3.0-only
#define install_weapon_lifecycle install_weapon_lifecycle_test_copy
#define read_weapon_lifetime read_weapon_lifetime_test_copy
#define BOVR_TEST_NATIVE_GUARDS 1
#include "../src/t5/weapon_lifecycle.cpp"
#undef BOVR_TEST_NATIVE_GUARDS
#undef install_weapon_lifecycle
#undef read_weapon_lifetime
#include <cstdio>

namespace {
using namespace bovr::t5;
unsigned checks{},failures{},native_creates{},native_frees{};
std::uint32_t received[5]{};
std::uintptr_t fake_dobj{};
void check(bool v,const char* name) {++checks;printf("%s %s\n",v?"PASS":"FAIL",name);if(!v) ++failures;}
template<class T> void put(std::uintptr_t p,T v) {memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));}
void* __cdecl create_native(void* models,int count,void* tree,int handle,int local) {
    ++native_creates;received[0]=reinterpret_cast<std::uint32_t>(models);received[1]=count;received[2]=reinterpret_cast<std::uint32_t>(tree);received[3]=handle;received[4]=local;
    put<short>(lifecycle_base+map_rva+1536*2,2);put<unsigned char>(lifecycle_base+allocated_rva+2,1);SetLastError(54321);return reinterpret_cast<void*>(fake_dobj);
}
void __cdecl free_native(int handle,int local) {++native_frees;received[0]=handle;received[1]=local;SetLastError(12345);}
}
int main() {
    using namespace bovr::t5;
    detail::WeaponLifetimeState state{};WeaponLifetime value{};
    check(!state.read(1,2,3,&value),"an existing pointer cannot mint a native lifetime");
    state.begin(1,3);check(!state.read(1,2,3,&value),"construction invalidates previous lifetime before native create");
    state.finish(1,2,3,true);check(state.read(1,2,3,&value)&&value.generation,"matching completed native creation mints verified generation");
    const auto first=value.generation;state.invalidate();check(!state.read(1,2,3,&value),"native free or owner reset invalidates immediately");
    state.begin(1,3);state.finish(1,2,3,true);check(state.read(1,2,3,&value)&&value.generation>first,"reused owner and DObj address receive a new allocation generation");
    check(!state.read(1,2,4,&value)&&!state.read(9,2,3,&value)&&!state.read(1,9,3,&value),"foreign thread owner or model cannot borrow generation");
    state.begin(1,3);state.finish(1,2,3,false);check(!state.read(1,2,3,&value),"native pool mismatch never verifies allocation");
    state.begin(1,3);state.begin(1,3);state.finish(1,2,3,true);state.finish(1,2,3,true);check(!state.read(1,2,3,&value),"nested local allocation remains unverified");
    state.begin(1,3);state.invalidate();state.finish(1,2,3,true);check(!state.read(1,2,3,&value),"pool reset during creation prevents stale completion");
    state.begin(1,3);state.finish(1,0,3,true);check(!state.read(1,2,3,&value),"failed native creation remains invalid");
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000000,MEM_RESERVE,PAGE_NOACCESS));if(!image) return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    for(auto rva:{owner_rva,pool_rva,map_rva+1536*2,allocated_rva}) if(!VirtualAlloc(image+(rva&~std::uintptr_t{0xfff}),4096,MEM_COMMIT,PAGE_READWRITE)) return 1;
    for(const auto& p:life_patches) if(!VirtualAlloc(image+(p.rva&~std::uintptr_t{0xfff}),4096,MEM_COMMIT,PAGE_READWRITE)) return 1;
    image[life_patches[0].rva]=1;check(!patch_lifecycle(base),"all lifecycle guards are checked before any patch");image[life_patches[0].rva]=0;
    check(patch_lifecycle(base),"complete exact six-site lifecycle guard installs once");
    const std::array<unsigned char,6> authored{};
    check(!memcmp(create_original,authored.data(),6)&&!memcmp(free_original,authored.data(),5),"entry trampolines preserve complete authored fixture spans");
    check(patch_lifecycle(base)&&!patch_lifecycle(base+4096),"lifecycle install is idempotent only for same image");
    std::uintptr_t owner[13]{};put(base+owner_rva,reinterpret_cast<std::uintptr_t>(owner));fake_dobj=base+pool_rva+2*0x7c;
    create_original=reinterpret_cast<void*>(&create_native);free_original=reinterpret_cast<void*>(&free_native);
    using Create=void*(__cdecl*)(void*,int,void*,int,int);using Free=void(__cdecl*)(int,int);
    auto create=reinterpret_cast<Create>(&create_detour);auto free=reinterpret_cast<Free>(&free_detour);
    void* result=create(reinterpret_cast<void*>(0x1234),2,reinterpret_cast<void*>(0x5678),1536,0);const DWORD created_error=GetLastError();
    check(native_creates==1&&result==reinterpret_cast<void*>(fake_dobj)&&received[0]==0x1234&&received[1]==2&&received[2]==0x5678&&received[3]==1536&&received[4]==0,"create wrapper preserves all five arguments and one native result");
    check(created_error==54321,"post allocation observer preserves native GetLastError");
    check(read_weapon_lifetime_test_copy(reinterpret_cast<std::uintptr_t>(owner),fake_dobj,&value),"actual wrapper verifies local pool slot and allocation result");
    const auto generation=value.generation;
    create(nullptr,1,nullptr,1535,0);check(read_weapon_lifetime_test_copy(reinterpret_cast<std::uintptr_t>(owner),fake_dobj,&value)&&value.generation==generation,"another native handle cannot replace local weapon lifetime");
    free(1536,1);check(!read_weapon_lifetime_test_copy(reinterpret_cast<std::uintptr_t>(owner),fake_dobj,&value),"native free ignores client argument so handle1536 always invalidates");
    create(nullptr,2,nullptr,1536,0);
    free(1536,0);const DWORD free_error=GetLastError();check(native_frees==2&&received[0]==1536&&received[1]==0&&!read_weapon_lifetime_test_copy(reinterpret_cast<std::uintptr_t>(owner),fake_dobj,&value),"local native free executes once and invalidates generation");
    check(free_error==12345,"free observer preserves native GetLastError");
    create(nullptr,2,nullptr,1536,0);check(read_weapon_lifetime_test_copy(reinterpret_cast<std::uintptr_t>(owner),fake_dobj,&value)&&value.generation>generation,"actual native pointer reuse receives fresh generation");
    create(nullptr,2,nullptr,1536,1);check(!read_weapon_lifetime_test_copy(reinterpret_cast<std::uintptr_t>(owner),fake_dobj,&value),"unsupported-client creation of same native slot invalidates without minting local lifetime");
    create(nullptr,2,nullptr,1536,0);
    put<unsigned char>(base+allocated_rva+2,0);check(!read_weapon_lifetime_test_copy(reinterpret_cast<std::uintptr_t>(owner),fake_dobj,&value),"unallocated pool slot rejects otherwise matching cached identity");
    put<unsigned char>(base+allocated_rva+2,1);owner_reset_before(nullptr);check(!read_weapon_lifetime_test_copy(reinterpret_cast<std::uintptr_t>(owner),fake_dobj,&value),"native VMI reset rejects reused memory without create");
    SetEnvironmentVariableW(L"BOVR_WEAPON_LIFECYCLE_PROBE",L"0");check(!install_weapon_lifecycle_test_copy(reinterpret_cast<HMODULE>(image)),"public lifecycle observer remains independently disabled by default");
    printf("checks=%u failures=%u game_launches=0 native_game_calls=0\n",checks,failures);return failures?1:0;
}
