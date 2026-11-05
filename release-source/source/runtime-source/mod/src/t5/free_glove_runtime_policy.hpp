// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string_view>

namespace bovr::t5 {
// COD4 V86 compatibility basis for a free visual glove when the Pimax runtime
// has no valid palm pose. This must never change an input/action pose.
// https://github.com/jplakon/CallOfDuty4_VR/blob/bd18fbf5a50439338255231654af02a64e5d72f7/src/cgame/cg_weapons.cpp#L4942
// Runtime identity is cached on the XR owner. Do not infer this compatibility
// rule from a Pimax headset running under a different OpenXR provider.
inline bool pimax_free_glove_runtime(std::string_view runtime) noexcept {
    const auto lower=[](char c){return c>='A'&&c<='Z'?char(c+('a'-'A')):c;};
    const auto contains=[&](std::string_view needle){
        if(runtime.size()<needle.size())return false;
        for(std::size_t start=0;start<=runtime.size()-needle.size();++start){
            bool matched=true;
            for(std::size_t i=0;i<needle.size();++i)
                if(lower(runtime[start+i])!=needle[i]){matched=false;break;}
            if(matched)return true;
        }
        return false;
    };
    return contains("pimax")||contains("piopenxr");
}
}
