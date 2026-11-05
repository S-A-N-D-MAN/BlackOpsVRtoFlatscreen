// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
namespace bovr::render {
// Synchronous borrowed pointer substitution only. The owned command vectors
// outlive this scope; no native command allocation or resource is edited.
class HudViewCommandBinding {
public:
    HudViewCommandBinding()=default;
    ~HudViewCommandBinding(){restore();}
    HudViewCommandBinding(const HudViewCommandBinding&)=delete;
    HudViewCommandBinding& operator=(const HudViewCommandBinding&)=delete;
    bool apply(std::array<std::uintptr_t,2> fields,std::array<std::uintptr_t,2> originals,
               std::array<std::uintptr_t,2> replacements) noexcept {
        if(pending_||!fields[0]||!fields[1]||fields[0]==fields[1]||!replacements[0]||!replacements[1])return false;
        fields_=fields;originals_=originals;replacements_=replacements;
        bool good=true;
        __try {
            for(unsigned i=0;i<2;++i){std::uintptr_t current{};std::memcpy(&current,reinterpret_cast<void*>(fields_[i]),sizeof(current));if(current!=originals_[i])return false;}
            pending_=3;
            for(unsigned i=0;i<2;++i)std::memcpy(reinterpret_cast<void*>(fields_[i]),&replacements_[i],sizeof(std::uintptr_t));
        } __except(EXCEPTION_EXECUTE_HANDLER){good=false;}
        if(!good)restore();return good;
    }
    bool restore() noexcept {
        if(!pending_)return true;
        bool good=true;
        for(unsigned i=0;i<2;++i) {
            if(!(pending_&(1u<<i)))continue;
            __try {
                std::uintptr_t current{};std::memcpy(&current,reinterpret_cast<void*>(fields_[i]),sizeof(current));
                if(current==replacements_[i])std::memcpy(reinterpret_cast<void*>(fields_[i]),&originals_[i],sizeof(std::uintptr_t));
                else if(current!=originals_[i])good=false; // Never overwrite an unrelated writer.
                // Either restoration succeeded or this slot provably stopped
                // referencing our storage. An access/write fault leaves its bit.
                pending_&=~(1u<<i);
            } __except(EXCEPTION_EXECUTE_HANDLER){good=false;}
        }
        return good;
    }
    // Caller MUST retain both binding and owned command vectors while true,
    // including failed apply/rollback. Retry after the native field is usable.
    bool holds_pointers() const noexcept {return pending_!=0;}
private:
    unsigned pending_{};
    std::array<std::uintptr_t,2> fields_{},originals_{},replacements_{};
};
} // namespace bovr::render
