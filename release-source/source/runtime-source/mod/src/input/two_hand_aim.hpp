// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_math.h"
namespace bovr::input {
enum class SupportAimReason : unsigned {disabled,pair,hand,focus,squeeze,tracking,anchor,separation,behind,basis,pole};
struct SupportAimBasis {
    wawvr::xr::Basis3f local{};
    SupportAimReason reason{SupportAimReason::disabled};
    float separation_meters{};
    bool paired{};
};
// Stateless current-sample diagnostic. Returned pair basis is IW relative to
// this original tracking anchor; no body/head, clock, native lease or latch.
// Every non-pair result instructs the caller to retain its exact old right path.
SupportAimBasis two_hand_aim_basis(const wawvr::xr::ActionSnapshot&,
    const wawvr::xr::Posef& anchor,wawvr::xr::Hand,bool enabled) noexcept;
// Exact added consumed fields; compares floating bits (including invalid NaN)
// so equal unusable inputs can still choose the ordinary right-hand fallback.
bool same_support_actions(const wawvr::xr::ActionSnapshot&,
    const wawvr::xr::ActionSnapshot&) noexcept;
}
