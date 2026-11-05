// SPDX-License-Identifier: GPL-3.0-only
#include "t5/quit_gate.hpp"
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>
namespace {
unsigned failures{};
void check(bool ok, const char* text) { printf("%s %s\n",ok ? "PASS":"FAIL",text); if (!ok) ++failures; }
struct Context { unsigned calls{}; bool ready{}; bool throws{}; bovr::t5::QuitRetirementGate* gate{}; };
bool ready(void* value) {
    auto& c = *static_cast<Context*>(value); ++c.calls;
    if (c.gate) check(!c.gate->may_call_native(ready,value), "recursive quit remains deferred while callback runs");
    if (c.throws) throw std::runtime_error("test retirement failure");
    return c.ready;
}
}
int main() {
    using namespace bovr::t5;
    QuitRetirementGate flat;
    check(flat.may_call_native(nullptr,nullptr) && !flat.retirement_requested(), "default no callback permits original quit");
    Context inactive{0,true};
    check(flat.may_call_native(ready,&inactive) && inactive.calls==1, "inactive XR permits original once");
    check(!flat.acknowledge_retired(), "no spurious quit queue without pending request");
    QuitRetirementGate deferred;
    Context active{0,false,false,&deferred};
    check(!deferred.may_call_native(ready,&active) && deferred.retirement_requested() && active.calls==1, "first live-XR quit requests retirement and returns promptly");
    check(!deferred.may_call_native(ready,&active) && active.calls==1, "repeated quit does not reenter retirement callback");
    check(!deferred.may_call_native(nullptr,nullptr), "unbinding a callback cannot bypass pending retirement");
    std::atomic<unsigned> acknowledgements{};
    std::thread owner([&]{ if(deferred.acknowledge_retired()) ++acknowledgements; });
    std::thread duplicate([&]{ if(deferred.acknowledge_retired()) ++acknowledgements; });
    owner.join(); duplicate.join();
    check(acknowledgements==1 && !deferred.retirement_requested(), "exactly one retirement ACK wins across threads");
    check(deferred.may_call_native(ready,&active) && active.calls==1, "requeued quit passes original without touching retired XR context");
    QuitRetirementGate failed;
    Context throwing{0,false,true};
    check(!failed.may_call_native(ready,&throwing) && failed.retirement_requested(), "retirement exception cannot escape or run native teardown");
    check(failed.acknowledge_retired() && failed.may_call_native(nullptr,nullptr), "explicit retirement recovers failed callback");
    SetEnvironmentVariableW(L"BOVR_QUIT_GATE", L"1"); SetEnvironmentVariableW(L"BOVR_INSTRUMENT", L"1");
    check(!install_quit_gate(GetModuleHandleW(nullptr)), "all opt-ins still reject non-retail host before any patch");
    check(!set_quit_retirement_callback(ready,&inactive) && !acknowledge_quit_retired_and_queue(), "unsupported host cannot register or queue native quit");
    printf("native_game_commands=0 failures=%u\n",failures); return failures ? 1:0;
}
