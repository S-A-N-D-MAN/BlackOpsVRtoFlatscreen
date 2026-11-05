// SPDX-License-Identifier: GPL-3.0-only
#include "loader/d3d9_observer.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <thread>
namespace {
unsigned failures{};
void check(bool ok, const char* message) { printf("%s %s\n", ok ? "PASS" : "FAIL", message); if (!ok) ++failures; }
struct Fake { void** table; ULONG refs{5}; };
std::array<void*, 133> device_table{};
std::array<void*, 10> swap_table{};
Fake device{device_table.data()}, ordinary{device_table.data()}, swap{swap_table.data()}, additional{swap_table.data()};
unsigned device_presents{}, swap_presents{}, gets{}, releases{}, get_device_calls{}, resets{}, ex_resets{};
bool forward_to_swap{true}, double_swap{}, fail_get_device{};
HRESULT reset_result = S_OK;
const RECT rect{1,2,3,4};
ULONG STDMETHODCALLTYPE release(IUnknown* object) {
    ++releases;
    auto* fake = reinterpret_cast<Fake*>(object);
    const auto count = --fake->refs;
    if (!count && fake == &device) fake->table = reinterpret_cast<void**>(1); // catch post-destruction dereferences
    return count;
}
HRESULT STDMETHODCALLTYPE get_swap(IDirect3DDevice9*, UINT index, IDirect3DSwapChain9** out) {
    ++gets; if (!out) return D3DERR_INVALIDCALL;
    auto* selected = index ? &additional : &swap;
    ++selected->refs; *out = reinterpret_cast<IDirect3DSwapChain9*>(selected); return S_OK;
}
HRESULT STDMETHODCALLTYPE create_swap(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9** out) {
    ++additional.refs; *out = reinterpret_cast<IDirect3DSwapChain9*>(&additional); return S_OK;
}
HRESULT STDMETHODCALLTYPE get_device(IDirect3DSwapChain9*, IDirect3DDevice9** out) {
    ++get_device_calls;
    if (fail_get_device) { *out = nullptr; return E_FAIL; }
    ++device.refs; *out = reinterpret_cast<IDirect3DDevice9*>(&device); return S_OK;
}
HRESULT STDMETHODCALLTYPE swap_present(IDirect3DSwapChain9*, const RECT* a, const RECT* b, HWND window, const RGNDATA* dirty, DWORD flags) {
    ++swap_presents;
    check(a == &rect && !b && window == reinterpret_cast<HWND>(123) && !dirty && flags == 7,
          "swap Present preserves every argument");
    return D3DERR_WASSTILLDRAWING;
}
HRESULT STDMETHODCALLTYPE present(IDirect3DDevice9*, const RECT* a, const RECT* b, HWND window, const RGNDATA* dirty) {
    ++device_presents;
    if (forward_to_swap) {
        auto* chain = reinterpret_cast<IDirect3DSwapChain9*>(&swap);
        chain->Present(a, b, window, dirty, 7);
        if (double_swap) chain->Present(a, b, window, dirty, 7);
    }
    return D3DERR_DEVICELOST;
}
HRESULT STDMETHODCALLTYPE present_ex(IDirect3DDevice9Ex*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE reset(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*) { ++resets; return reset_result; }
HRESULT STDMETHODCALLTYPE reset_ex(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*) { ++ex_resets; return reset_result; }
struct State {
    unsigned present{}, before{}, after{}, destroyed{}, foreign{};
    bool allow{true}, throw_present{}, throw_before{}, throw_after{}, reenter_present{}, reenter_reset{};
    bool blocked_unbind{};
    DWORD owner = GetCurrentThreadId();
    HRESULT after_hr{};
    std::uintptr_t destroyed_id{};
};
void before_present(void* context, IDirect3DDevice9* borrowed) {
    auto& s = *static_cast<State*>(context); ++s.present;
    check(borrowed == reinterpret_cast<IDirect3DDevice9*>(&device), "pre-Present borrows expected device");
    s.blocked_unbind = !bovr::set_graphics_callbacks({});
    if (s.reenter_present) {
        s.reenter_present = false;
        borrowed->Present(&rect, nullptr, reinterpret_cast<HWND>(123), nullptr);
    }
    if (s.throw_present) throw std::runtime_error("mock Present callback failure");
}
bool before_reset(void* context, IDirect3DDevice9* borrowed) {
    auto& s = *static_cast<State*>(context); ++s.before;
    if (GetCurrentThreadId() != s.owner) { ++s.foreign; return false; }
    if (s.reenter_reset) {
        s.reenter_reset = false;
        D3DPRESENT_PARAMETERS p{};
        check(borrowed->Reset(&p) == D3DERR_DEVICELOST, "recursive Reset is deferred without recursion");
    }
    if (s.throw_before) throw std::runtime_error("mock retirement failure");
    return s.allow;
}
void after_reset(void* context, IDirect3DDevice9*, HRESULT hr) {
    auto& s = *static_cast<State*>(context); ++s.after; s.after_hr = hr;
    if (s.throw_after) throw std::runtime_error("mock finalization failure");
}
void destroyed(void* context, std::uintptr_t identity) {
    auto& s = *static_cast<State*>(context); ++s.destroyed; s.destroyed_id = identity;
    check(!bovr::set_graphics_callbacks({}), "final-release context cannot unbind while notification in flight");
}
}
int main() {
    using namespace bovr;
    State state;
    const GraphicsCallbacks c{&state, &before_present, &before_reset, &after_reset, &destroyed};
    check(!set_graphics_callbacks(c), "callbacks cannot register before active identity-authorized observation");
    set_observation_active(true);
    device_table[2] = reinterpret_cast<void*>(&release);
    device_table[13] = reinterpret_cast<void*>(&create_swap); device_table[14] = reinterpret_cast<void*>(&get_swap);
    device_table[16] = reinterpret_cast<void*>(&reset); device_table[17] = reinterpret_cast<void*>(&present);
    device_table[121] = reinterpret_cast<void*>(&present_ex); device_table[132] = reinterpret_cast<void*>(&reset_ex);
    swap_table[2] = reinterpret_cast<void*>(&release); swap_table[3] = reinterpret_cast<void*>(&swap_present); swap_table[8] = reinterpret_cast<void*>(&get_device);
    auto* d = reinterpret_cast<IDirect3DDevice9*>(&device);
    auto* chain = reinterpret_cast<IDirect3DSwapChain9*>(&swap);
    check(observe_device(d, true), "device and implicit swap observed");
    const auto before_get = get_device_calls;
    check(chain->Present(&rect, nullptr, reinterpret_cast<HWND>(123), nullptr, 7) == D3DERR_WASSTILLDRAWING && get_device_calls == before_get,
          "default callback noops add no GetDevice call and preserve Present HRESULT");
    check(set_graphics_callbacks(c), "explicit callback registration");
    const auto refs = device.refs;
    const auto before_calls = state.present;
    double_swap = true;
    check(d->Present(&rect, nullptr, reinterpret_cast<HWND>(123), nullptr) == D3DERR_DEVICELOST && state.present == before_calls + 1 && device.refs == refs,
          "nested DevicePresent to repeated swap Present captures once with balanced device ref");
    double_swap = false;
    check(state.blocked_unbind, "callback context replacement refuses active lease");
    state.reenter_present = true;
    const auto recursive_before = state.present;
    chain->Present(&rect, nullptr, reinterpret_cast<HWND>(123), nullptr, 7);
    check(state.present == recursive_before + 1 && device.refs == refs, "callback reentrant Present forwards without second capture or ref leak");
    const auto separate_before = state.present;
    chain->Present(&rect, nullptr, reinterpret_cast<HWND>(123), nullptr, 7);
    chain->Present(&rect, nullptr, reinterpret_cast<HWND>(123), nullptr, 7);
    check(state.present == separate_before + 2, "independent swap Presents each receive a callback");
    IDirect3DSwapChain9* other{};
    d->GetSwapChain(1, &other);
    const auto additional_before = state.present;
    other->Present(&rect, nullptr, reinterpret_cast<HWND>(123), nullptr, 7); other->Release();
    check(state.present == additional_before && device.refs == refs, "additional swap-chain is not the authoritative capture source");
    fail_get_device = true;
    check(chain->Present(&rect, nullptr, reinterpret_cast<HWND>(123), nullptr, 7) == D3DERR_WASSTILLDRAWING && state.present == additional_before,
          "GetDevice failure skips callback and preserves original Present");
    fail_get_device = false;
    state.throw_present = true;
    check(chain->Present(&rect, nullptr, reinterpret_cast<HWND>(123), nullptr, 7) == D3DERR_WASSTILLDRAWING && device.refs == refs,
          "throwing Present callback is contained and reference balanced");
    state.throw_present = false;
    D3DPRESENT_PARAMETERS p{};
    state.allow = false;
    const auto ordinary_before = state.before;
    check(reinterpret_cast<IDirect3DDevice9*>(&ordinary)->Reset(&p) == S_OK && state.before == ordinary_before,
          "shared-vtable untracked device keeps stock Reset without callbacks");
    const auto reset_before = resets, gets_before = gets, after_before = state.after;
    check(d->Reset(&p) == D3DERR_DEVICELOST && resets == reset_before && gets == gets_before && state.after == after_before,
          "denied retirement skips Reset and after callback without device queries");
    state.allow = true; state.throw_before = true;
    check(d->Reset(&p) == D3DERR_DEVICELOST && resets == reset_before && state.after == after_before,
          "throwing retirement defers Reset");
    state.throw_before = false;
    HRESULT foreign_result{};
    std::thread foreign([&] { foreign_result = d->Reset(&p); }); foreign.join();
    check(foreign_result == D3DERR_DEVICELOST && state.foreign == 1 && resets == reset_before && state.after == after_before,
          "runtime owner denial on foreign reset thread preserves device");
    reset_result = D3DERR_DEVICELOST;
    check(d->Reset(&p) == reset_result && resets == reset_before + 1 && state.after == after_before + 1 && state.after_hr == reset_result && gets == gets_before,
          "real failed Reset reported once without subsequent device query");
    reset_result = S_OK; state.reenter_reset = true; state.throw_after = true;
    check(d->Reset(&p) == S_OK && resets == reset_before + 2 && gets == gets_before + 1,
          "real Reset result survives after callback failure and reacquires successful swap");
    state.throw_after = false;
    const auto ex_before = ex_resets, ex_after = state.after;
    state.allow = false;
    check(reinterpret_cast<IDirect3DDevice9Ex*>(d)->ResetEx(&p, nullptr) == D3DERR_DEVICELOST && ex_resets == ex_before && state.after == ex_after,
          "native ResetEx also honors retirement deferral");
    state.allow = true;
    check(reinterpret_cast<IDirect3DDevice9Ex*>(d)->ResetEx(&p, nullptr) == S_OK && ex_resets == ex_before + 1 && state.after == ex_after + 1,
          "native ResetEx admitted and reported once");
    const auto destroyed_before = state.destroyed;
    chain->Release();
    check(state.destroyed == destroyed_before, "swap Release never announces device destruction");
    device.refs = 1;
    check(d->Release() == 0 && state.destroyed == destroyed_before + 1 && state.destroyed_id == reinterpret_cast<std::uintptr_t>(d),
          "final device Release notifies opaque identity without dereference after destruction");
    check(set_graphics_callbacks({}), "callbacks unbind after all leases retire");
    set_observation_active(false);
    check(!set_graphics_callbacks(c), "disabled observation rejects later callback registration");
    printf("failures=%u\n", failures); return failures ? 1 : 0;
}
