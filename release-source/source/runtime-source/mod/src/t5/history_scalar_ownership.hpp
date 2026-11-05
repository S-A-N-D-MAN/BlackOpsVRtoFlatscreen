// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstdint>
#include <type_traits>
namespace bovr::t5 {
// One aligned x86 store per field. Ownership is recorded before the attempted
// store, including a faulting store. A bulk copy cannot leave a torn UV record.
template<class T> struct HistoryScalarOwnership {
    static_assert(std::is_same_v<T,std::uint32_t>||std::is_same_v<T,std::uint8_t>);
    std::uintptr_t address{};T original{},installed{};bool pending{};
    static bool read(std::uintptr_t p,T* out) noexcept {
        if(!p||p%alignof(T))return false;
        __try {*out=*reinterpret_cast<volatile const T*>(p);}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}return true;
    }
    static bool write(std::uintptr_t p,T value) noexcept {
        if(!p||p%alignof(T))return false;
        __try {*reinterpret_cast<volatile T*>(p)=value;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}return true;
    }
    bool install(std::uintptr_t p,T expected,T value) noexcept {
        if(pending)return false;T now{};
        if(!read(p,&now)||now!=expected)return false;
        if(value==expected)return true;
        address=p;original=expected;installed=value;pending=true;
        return write(p,value)&&read(p,&now)&&now==value;
    }
    bool restore(bool same_scope) noexcept {
        if(!pending)return true;T now{};
        if(!read(address,&now))return false;
        // Confirmed original, or an observed foreign value: no write is owned.
        if(now!=installed){pending=false;return true;}
        if(!same_scope||!write(address,original)||!read(address,&now))return false;
        if(now==original||now!=installed){pending=false;return true;}return false;
    }
};
}
