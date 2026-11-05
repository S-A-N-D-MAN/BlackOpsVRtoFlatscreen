// SPDX-License-Identifier: GPL-3.0-only
// Exercises the six-byte cdecl trampoline against a synthetic in-process image.
// No retail process, executable file, game assets or runtime is loaded or edited.
#define BOVR_TEST_NATIVE_GUARDS 1
#define install_backend_probe install_backend_probe_test_copy
#include "../src/t5/backend_probe.cpp"
#undef install_backend_probe
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace {
volatile unsigned executed{};
unsigned failures{};
void check(bool ok, const char* text) { printf("%s %s\n", ok ? "PASS" : "FAIL", text); if (!ok) ++failures; }
void write_backend_fixture(unsigned char* entry) {
    // Authored equivalent of the six-byte stack prologue: align to 16 instead
    // of the retail alignment, then count one call and return normally.
    entry[0]=0x55;entry[1]=0x8b;entry[2]=0xec;entry[3]=0x83;entry[4]=0xe4;entry[5]=0xf0;
    entry[6]=0xff;entry[7]=0x05;
    const auto counter_address=reinterpret_cast<std::uint32_t>(&executed);memcpy(entry+8,&counter_address,4);
    entry[12]=0x8b;entry[13]=0xe5;entry[14]=0x5d;entry[15]=0xc3;
}
}
int main() {
    constexpr std::size_t draw_rva = 0x2eb760, backend_pointer_rva = 0x3a5b5f8;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, 69693440, MEM_RESERVE, PAGE_NOACCESS));
    if (!image) return 1;
    if (!VirtualAlloc(image + (draw_rva & ~0xfffU), 4096, MEM_COMMIT, PAGE_READWRITE) ||
        !VirtualAlloc(image + (backend_pointer_rva & ~0xfffU), 4096, MEM_COMMIT, PAGE_READWRITE)) return 1;
    auto* entry = image + draw_rva;
    SetEnvironmentVariableW(L"BOVR_BACKEND_PROBE", L"0");
    check(!bovr::t5::install_backend_probe_test_copy(reinterpret_cast<HMODULE>(image)), "backend probe opt-in disabled by default");
    SetEnvironmentVariableW(L"BOVR_BACKEND_PROBE", L"1");
    check(!bovr::t5::install_backend_probe_test_copy(reinterpret_cast<HMODULE>(image)), "wrong six-byte prologue rejected without patch");
    write_backend_fixture(entry);constexpr std::size_t code_size=16;
    DWORD previous{};
    if (!VirtualProtect(entry,code_size,PAGE_EXECUTE_READ,&previous)) return 1;
    FlushInstructionCache(GetCurrentProcess(),entry,code_size);
    using SyntheticDraw = void(__cdecl*)();
    const auto draw = reinterpret_cast<SyntheticDraw>(entry);
    draw();
    check(executed == 1, "synthetic original cdecl body executes once");
    check(bovr::t5::install_backend_probe_test_copy(reinterpret_cast<HMODULE>(image)), "guarded six-byte backend trampoline installed");
    check(entry[0] == 0xe9 && entry[5] == 0x90, "five-byte entry jump plus sixth-byte NOP occupies exact prologue");
    draw();
    check(executed == 2, "hook executes original exactly once and preserves cdecl stack");
    check(bovr::t5::install_backend_probe_test_copy(reinterpret_cast<HMODULE>(image)), "startup installation idempotent");
    draw();
    check(executed == 3, "idempotent installation does not double-wrap original");
    VirtualFree(image, 0, MEM_RELEASE);
    printf("retail_processes=0 failures=%u\n", failures); return failures ? 1 : 0;
}
