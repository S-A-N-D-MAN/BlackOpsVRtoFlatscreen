// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string_view>

namespace bovr::t5 {
inline bool ascii_iequal(char a,char b) noexcept {
    if(a>='A'&&a<='Z')a=static_cast<char>(a-'A'+'a');
    if(b>='A'&&b<='Z')b=static_cast<char>(b-'A'+'a');
    return a==b;
}
inline bool ascii_icontains(std::string_view value,std::string_view token) noexcept {
    if(token.empty()||token.size()>value.size())return false;
    for(std::size_t i=0;i+token.size()<=value.size();++i){
        std::size_t n=0;for(;n<token.size()&&ascii_iequal(value[i+n],token[n]);++n){}
        if(n==token.size())return true;
    }
    return false;
}
inline bool ascii_istarts_with(std::string_view value,std::string_view token) noexcept {
    if(token.size()>value.size())return false;
    for(std::size_t i=0;i<token.size();++i)if(!ascii_iequal(value[i],token[i]))return false;
    return true;
}
// Vorkuta's prison knife deliberately shares the ordinary AK-47 definition.
// Ballistic knives and launchers retain trigger firing and stay out of this mode.
inline bool primary_knife_variant(std::string_view internal,std::string_view localized={}) noexcept {
    if(ascii_icontains(internal,"ballistic")||ascii_icontains(internal,"launcher")||
       ascii_icontains(localized,"BALLISTIC")||ascii_icontains(localized,"LAUNCHER"))return false;
    const bool internal_knife=internal=="knife"||ascii_istarts_with(internal,"knife_")||
        ascii_icontains(internal,"_knife_")||
        (internal.size()>=6&&ascii_icontains(internal.substr(internal.size()-6),"_knife"));
    return internal_knife||ascii_istarts_with(localized,"WEAPON_KNIFE_");
}
}
