// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cstdint>

namespace bovr::t5::reload_transfer {
inline constexpr int maximum_capacity=4096,maximum_reserve=100000;
struct AmmoValues {
    int clip{},reserve{},capacity{},reload_add{},state{};
    unsigned minimum_missing{1};
    bool fuel_exchange{};
};
struct Plan {unsigned rounds{},discarded{};bool allowed{},fuel_exchange{};};
inline bool valid(const AmmoValues& a) noexcept {
    return a.capacity>0&&a.capacity<=maximum_capacity&&a.clip>=0&&a.clip<=a.capacity&&
        a.reserve>=0&&a.reserve<=maximum_reserve&&a.reload_add>=0&&a.reload_add<=maximum_capacity&&
        a.minimum_missing>0&&a.minimum_missing<=static_cast<unsigned>(a.capacity);
}
// Mirrors the already guarded PM_ReloadClip normal-state amount calculation.
// Native reload-start states use another increment and are not admitted here.
inline Plan plan(const AmmoValues& a,unsigned requested) noexcept {
    if(!valid(a)||!requested||requested>static_cast<unsigned>(maximum_capacity)||
       (a.state!=0&&a.state!=6&&a.state!=7&&a.state!=8)||!a.reserve)return {};
    const int missing=a.fuel_exchange?a.capacity:a.capacity-a.clip;
    if(!a.fuel_exchange&&missing<static_cast<int>(a.minimum_missing))return {};
    int amount=std::min(missing,a.reserve);
    if(a.reload_add&&a.reload_add<a.capacity)amount=std::min(amount,a.reload_add);
    if(amount<=0||static_cast<unsigned>(amount)!=requested)return {};
    return {requested,a.fuel_exchange?static_cast<unsigned>(a.clip):0U,true,a.fuel_exchange};
}
inline bool completed(const Plan& p,const AmmoValues& before,const AmmoValues& after) noexcept {
    if(!p.allowed||!valid(before)||!valid(after)||before.capacity!=after.capacity||
       before.reload_add!=after.reload_add||before.minimum_missing!=after.minimum_missing||
       before.fuel_exchange!=after.fuel_exchange||p.fuel_exchange!=before.fuel_exchange)return false;
    const int amount=static_cast<int>(p.rounds);
    return before.reserve-after.reserve==amount&&
        after.clip==(p.fuel_exchange?amount:before.clip+amount)&&
        (!p.fuel_exchange||p.discarded==static_cast<unsigned>(before.clip));
}
} // namespace bovr::t5::reload_transfer
