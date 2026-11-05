// SPDX-License-Identifier: GPL-3.0-only
#include "render/hud_view_binding.hpp"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
namespace {
unsigned checks{};
void* rollback_page{};void* failed_write_page{};
LONG CALLBACK force_rollback_failure(EXCEPTION_POINTERS* exception) {
    if(exception->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION&&
       exception->ExceptionRecord->NumberParameters>=2&&
       exception->ExceptionRecord->ExceptionInformation[1]==reinterpret_cast<ULONG_PTR>(failed_write_page)) {
        DWORD previous{};VirtualProtect(rollback_page,4096,PAGE_READONLY,&previous);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void check(bool value,const char* why){++checks;if(!value){std::printf("FAIL %s\n",why);std::exit(1);}}
}
int main() {
    using bovr::render::HudViewCommandBinding;
    std::array<std::uintptr_t,4> storage{0x11111111,0,0x77777777,0x88888888};
    const auto before=storage;const std::array<std::uintptr_t,2> fields{reinterpret_cast<std::uintptr_t>(&storage[0]),reinterpret_cast<std::uintptr_t>(&storage[1])};
    const std::array<std::uintptr_t,2> original{storage[0],storage[1]},replacement{0x33333333,0x44444444};
    unsigned calls{};
    {
        HudViewCommandBinding binding;check(binding.apply(fields,original,replacement),"known two native pointer slots accept owned command copies");
        ++calls;check(storage[0]==replacement[0]&&storage[1]==replacement[1],"synchronous native pass sees both owned lists");
        check(!binding.apply(fields,original,replacement),"active pointer scope cannot be overwritten by nesting");
        check(storage[2]==before[2]&&storage[3]==before[3],"adjacent native fields remain exact");
        check(binding.restore()&&binding.restore()&&storage==before,"explicit pointer restoration is exact and idempotent");
    }
    check(calls==1&&storage==before,"one outer pass leaves original left pointer and null right pointer intact");
    try {HudViewCommandBinding binding;check(binding.apply(fields,original,replacement),"exception test binds");++calls;throw std::runtime_error("synthetic callback exception");}
    catch(const std::runtime_error&){}
    check(calls==2&&storage==before,"C++ stack unwinding restores pointers before owned buffers would be released");
    {
        HudViewCommandBinding binding;auto wrong=original;wrong[1]=1;
        check(!binding.apply(fields,wrong,replacement)&&storage==before,"mismatched second original rejects before either write");
        check(!binding.apply({fields[0],fields[0]},original,replacement)&&storage==before,"aliased view fields rejected");
        check(!binding.apply(fields,original,{0,replacement[1]})&&storage==before,"empty replacement cannot enter native execution");
        check(binding.apply(fields,original,replacement),"foreign-write case binds");storage[1]=0x99999999;
        check(!binding.restore()&&storage[0]==original[0]&&storage[1]==0x99999999,"restore fixes our first pointer but never clobbers an unrelated writer");
    }
    storage=before;
    auto* page=static_cast<std::uintptr_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(page!=nullptr,"isolated pointer page allocated");*page=0;
    DWORD previous{},unused{};check(VirtualProtect(page,4096,PAGE_READONLY,&previous)!=FALSE,"second pointer page made readonly");
    {
        HudViewCommandBinding binding;const std::array<std::uintptr_t,2> fault_fields{fields[0],reinterpret_cast<std::uintptr_t>(page)};
        check(!binding.apply(fault_fields,original,replacement)&&storage==before&&*page==0,
              "second write fault restores first pointer immediately and preserves untouched page");
    }
    VirtualProtect(page,4096,PAGE_NOACCESS,&unused);
    {HudViewCommandBinding binding;check(!binding.apply({fields[0],reinterpret_cast<std::uintptr_t>(page)},original,replacement)&&storage==before,
          "unreadable second pointer rejects before the first pointer changes");}
    VirtualProtect(page,4096,previous,&unused);VirtualFree(page,0,MEM_RELEASE);
    auto* first_page=static_cast<std::uintptr_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    auto* second_page=static_cast<std::uintptr_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(first_page&&second_page,"isolated rollback fault pages allocated");
    *first_page=original[0];*second_page=original[1];
    const std::array<std::uintptr_t,2> protected_fields{reinterpret_cast<std::uintptr_t>(first_page),reinterpret_cast<std::uintptr_t>(second_page)};
    {
        HudViewCommandBinding binding;check(binding.apply(protected_fields,original,replacement),"final restore failure binds two valid live pointers");
        VirtualProtect(second_page,4096,PAGE_READONLY,&unused);
        check(!binding.restore()&&binding.holds_pointers()&&*first_page==original[0]&&*second_page==replacement[1],
              "failed final restore retains unresolved pointer ownership and original command storage must stay alive");
        check(!binding.apply(protected_fields,original,replacement),"pending rollback cannot be overwritten by another scope");
        VirtualProtect(second_page,4096,PAGE_READWRITE,&unused);
        check(binding.restore()&&!binding.holds_pointers()&&*second_page==original[1],"later safe retry resolves ownership before command storage release");
    }
    {
        *first_page=original[0];*second_page=original[1];rollback_page=first_page;failed_write_page=second_page;
        VirtualProtect(second_page,4096,PAGE_READONLY,&unused);
        void* handler=AddVectoredExceptionHandler(1,force_rollback_failure);check(handler!=nullptr,"synthetic rollback fault handler installed");
        HudViewCommandBinding binding;
        check(!binding.apply(protected_fields,original,replacement)&&binding.holds_pointers()&&*first_page==replacement[0]&&*second_page==original[1],
              "failed apply plus failed rollback retains the first replacement instead of reporting cleared ownership");
        RemoveVectoredExceptionHandler(handler);rollback_page=failed_write_page=nullptr;
        VirtualProtect(first_page,4096,PAGE_READWRITE,&unused);VirtualProtect(second_page,4096,PAGE_READWRITE,&unused);
        check(binding.restore()&&!binding.holds_pointers()&&*first_page==original[0],"failed-apply rollback is safely retried with storage still owned");
    }
    {
        HudViewCommandBinding binding;check(binding.apply(protected_fields,original,replacement),"unreadable final restore test binds");
        VirtualProtect(second_page,4096,PAGE_NOACCESS,&unused);
        check(!binding.restore()&&binding.holds_pointers(),"unreadable pointer remains unresolved, never presumed absent");
        VirtualProtect(second_page,4096,PAGE_READWRITE,&unused);*second_page=0x99999999;
        check(!binding.restore()&&!binding.holds_pointers()&&*second_page==0x99999999,
              "proved foreign takeover releases our pending lifetime without overwriting new owner");
    }
    VirtualFree(first_page,0,MEM_RELEASE);VirtualFree(second_page,0,MEM_RELEASE);
    std::printf("hud_view_binding_checks=%u failures=0 native_game_calls=0\n",checks);return 0;
}
