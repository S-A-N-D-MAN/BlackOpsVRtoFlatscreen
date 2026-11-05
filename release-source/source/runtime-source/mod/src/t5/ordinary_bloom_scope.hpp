// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/auxiliary_view.hpp"

namespace bovr::t5 {
struct OrdinaryBloomOwner {
    void* context{};
    bool (*current)(void*) noexcept{};
};
bool validate_ordinary_bloom_guards(HMODULE) noexcept;
// Backend-only journal. The canonical layout is copied before selecting eye0;
// no frontend state, dvar, shader, alpha, streak or film field is changed.
class OrdinaryBloomScope {
public:
    OrdinaryBloomScope()=default;
    OrdinaryBloomScope(const OrdinaryBloomScope&)=delete;
    OrdinaryBloomScope& operator=(const OrdinaryBloomScope&)=delete;
    ~OrdinaryBloomScope();
    bool begin(std::uintptr_t base,const AuxiliaryViewLayout&,unsigned eye,OrdinaryBloomOwner) noexcept;
    bool finish() noexcept;
    // End the stack owner's lifetime, including native SEH that skips C++
    // destruction. Unresolved writes poison this thread; no stale TLS pointer.
    void close() noexcept;
    bool good() const noexcept{return good_;}
    bool restored() const noexcept{return restored_;}
    bool pending() const noexcept{return touched_!=0;}
    const char* failure() const noexcept{return failure_;}
private:
    bool identity(bool selected) const noexcept;
    bool owner() const noexcept;
    std::uintptr_t base_{},view_{};
    AuxiliaryViewLayout layout_{};
    OrdinaryBloomOwner owner_{};
    std::array<std::uint32_t,8> before_{};
    DWORD thread_{};
    unsigned eye_{},touched_{};
    bool claimed_{},good_{},restored_{},reentered_{};
    const char* failure_="none";
};
// Calls the existing native body exactly once, including refused journals.
// Restores on normal, C++ and SEH exits without consuming native exceptions.
void invoke_ordinary_bloom_native(OrdinaryBloomScope*,void(*)(void*),void*);
}
