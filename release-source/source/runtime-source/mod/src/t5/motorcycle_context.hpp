// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/command_probe.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace bovr::t5::motorcycle {
struct Host {
    void* context{};
    bool (*copy)(void*, std::uintptr_t, void*, std::size_t) noexcept{};
    bool (*pre)(void*, PreCommandSnapshot*) noexcept{};
    DWORD (*thread)(void*) noexcept{};
};

struct Facts {
    struct Vec3 { float x{}, y{}, z{}; };
    PreCommandSnapshot pre{};
    std::uintptr_t cg{}, ps{}, refdef{}, vehicle_entity{}, script_vehicle{}, info{};
    int native_time{}, health{}, pm_type{}, vehicle_pos{}, vehicle_type{}, viewlock{}, info_index{-1};
    std::uint32_t pm_flags{}, weapon_flags{}, entity_flags{}, entity_flags2{};
    std::int16_t vehicle_index{1023};
    std::uint8_t local{}, primary{};
    int offhand{};
    Vec3 origin{};
    std::array<int, 7> camera{};
    std::array<char, 64> vehicle_name{};
};

namespace detail {
constexpr std::uintptr_t image_base = 0x400000;
constexpr std::uintptr_t entities = 0x1a796f8, entity_stride = 0x34c;
constexpr std::uintptr_t infos = 0x1cc1480, info_stride = 0x1dc8;

inline bool bounded(std::uintptr_t p, std::size_t size) noexcept {
    return p >= 0x10000 && p <= UINT32_MAX && size && size <= UINT32_MAX && size - 1 <= UINT32_MAX - p;
}
inline bool bytes(const Host& h, std::uintptr_t at, void* out, std::size_t size) noexcept {
    return h.copy && out && bounded(at, size) && h.copy(h.context, at, out, size);
}
template<class T> inline bool value(const Host& h, std::uintptr_t base, std::uintptr_t offset, T& out) noexcept {
    return bounded(base, sizeof(T)) && offset <= UINT32_MAX - base && bytes(h, base + offset, &out, sizeof(T));
}
inline bool pointer(const Host& h, std::uintptr_t base, std::uintptr_t offset, std::uintptr_t& out) noexcept {
    std::uint32_t p{};
    if (!value(h, base, offset, p)) return false;
    out = p;
    return true;
}
inline bool same_command(const CommandContext& a, const CommandContext& b) noexcept {
    return a.valid == b.valid && a.local_client == b.local_client &&
        a.command_number == b.command_number && a.native_frame_time == b.native_frame_time &&
        a.native_thread == b.native_thread && a.key_catcher == b.key_catcher &&
        a.connection_state == b.connection_state && a.native_override == b.native_override &&
        !std::memcmp(a.viewangles, b.viewangles, sizeof(a.viewangles)) &&
        !std::memcmp(a.kickangles, b.kickangles, sizeof(a.kickangles));
}
inline bool same_pre(const PreCommandSnapshot& a, const PreCommandSnapshot& b) noexcept {
    return same_command(a.context, b.context) && a.sequence == b.sequence &&
        a.observed_ms == b.observed_ms && a.gameplay_epoch == b.gameplay_epoch &&
        a.native_cg_identity == b.native_cg_identity && a.native_game_time == b.native_game_time &&
        a.native_health == b.native_health && a.native_pm_type == b.native_pm_type &&
        a.native_pm_flags == b.native_pm_flags && a.native_weapon_flags == b.native_weapon_flags &&
        a.native_entity_flags == b.native_entity_flags && a.native_entity_flags2 == b.native_entity_flags2 &&
        a.native_gameplay_fields_valid == b.native_gameplay_fields_valid &&
        a.gameplay_active == b.gameplay_active && a.linked_weapon_active == b.linked_weapon_active;
}
inline bool same_facts(const Facts& a, const Facts& b) noexcept {
    return same_pre(a.pre, b.pre) && a.cg == b.cg && a.ps == b.ps && a.refdef == b.refdef &&
        a.vehicle_entity == b.vehicle_entity && a.script_vehicle == b.script_vehicle && a.info == b.info &&
        a.native_time == b.native_time && a.health == b.health && a.pm_type == b.pm_type &&
        a.vehicle_pos == b.vehicle_pos && a.vehicle_type == b.vehicle_type && a.viewlock == b.viewlock &&
        a.info_index == b.info_index && a.pm_flags == b.pm_flags && a.weapon_flags == b.weapon_flags &&
        a.entity_flags == b.entity_flags && a.entity_flags2 == b.entity_flags2 &&
        a.vehicle_index == b.vehicle_index && a.local == b.local && a.primary == b.primary &&
        a.offhand == b.offhand && !std::memcmp(&a.origin, &b.origin, sizeof(a.origin)) &&
        a.camera == b.camera && a.vehicle_name == b.vehicle_name;
}
inline bool read(const Host& h, std::uintptr_t base, const PreCommandSnapshot& pre, Facts& f) noexcept {
    f = {};
    f.pre = pre;
    if (base != image_base || !pre.native_gameplay_fields_valid || !pre.native_cg_identity ||
        !pointer(h, base, 0x2bf5354, f.cg) || f.cg != pre.native_cg_identity ||
        !bounded(f.cg, 0x8c160)) return false;
    f.ps = f.cg + 0x8a364;
    f.refdef = f.cg + 0x8c100;
    if (!value(h, f.cg, 0x8a348, f.native_time) ||
        !value(h, f.ps, 4, f.pm_type) || !value(h, f.ps, 0xc, f.pm_flags) ||
        !value(h, f.ps, 0x10, f.weapon_flags) || !value(h, f.ps, 0xe0, f.entity_flags) ||
        !value(h, f.ps, 0xe4, f.entity_flags2) || !value(h, f.ps, 0x130, f.local) ||
        !value(h, f.ps, 0x134, f.offhand) || !value(h, f.ps, 0x144, f.primary) ||
        !value(h, f.ps, 0x1c4, f.health) || !value(h, f.ps, 0x440, f.viewlock) ||
        !value(h, f.ps, 0x444, f.vehicle_index) || !value(h, f.ps, 0x448, f.vehicle_pos) ||
        !value(h, f.ps, 0x44c, f.vehicle_type) || !value(h, f.refdef, 0x20, f.origin)) return false;
    constexpr std::array<std::uintptr_t, 7> camera_offsets{0x8a360, 0xc, 0xa9d44, 0x1c, 0x14, 0xa4780, 0xa4680};
    for (unsigned i = 0; i < camera_offsets.size(); ++i)
        if (!value(h, f.cg, camera_offsets[i], f.camera[i])) return false;
    if (f.native_time != pre.native_game_time || f.health != pre.native_health ||
        f.pm_type != pre.native_pm_type || f.pm_flags != pre.native_pm_flags ||
        f.weapon_flags != pre.native_weapon_flags || f.entity_flags != pre.native_entity_flags ||
        f.entity_flags2 != pre.native_entity_flags2 || !std::isfinite(f.origin.x) ||
        !std::isfinite(f.origin.y) || !std::isfinite(f.origin.z)) return false;

    if (f.vehicle_index >= 0 && f.vehicle_index <= 1021) {
        f.vehicle_entity = entities + static_cast<std::uintptr_t>(f.vehicle_index) * entity_stride;
        int entity_number{};
        // scr_vehicle_s::infoIdx is a signed short. The next two bytes are
        // unowned padding (0x1ca8 in the captured Vorkuta landing), not part
        // of the index. Reading an int here rejects every linked motorcycle.
        std::int16_t info_index{};
        if (!value(h, f.vehicle_entity, 0, entity_number) || entity_number != f.vehicle_index ||
            !pointer(h, f.vehicle_entity, 0x148, f.script_vehicle) || !bounded(f.script_vehicle, 0x1f2) ||
            !value(h, f.script_vehicle, 0x1ec, info_index) || info_index < 0 || info_index >= 32) return false;
        f.info_index = info_index;
        f.info = infos + static_cast<std::uintptr_t>(f.info_index) * info_stride;
        if (!bytes(h, f.info, f.vehicle_name.data(), f.vehicle_name.size())) return false;
        if (!std::memchr(f.vehicle_name.data(), '\0', f.vehicle_name.size())) return false;
    }
    return true;
}
inline bool protected_copy(void*, std::uintptr_t at, void* out, std::size_t size) noexcept {
    if (!out || !bounded(at, size)) return false;
    __try { std::memcpy(out, reinterpret_cast<const void*>(at), size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool native_pre(void*, PreCommandSnapshot* p) noexcept { return read_pre_command_snapshot(p); }
inline DWORD native_thread(void*) noexcept { return GetCurrentThreadId(); }
} // namespace detail

inline bool sentinel_motorcycle(const Facts& f) noexcept {
    if (!f.pre.sequence || !f.pre.gameplay_epoch || f.native_time <= 0 || f.health <= 0 || f.local ||
        f.pm_type || (f.pm_flags & ~0x20u) || f.weapon_flags != 0x02000080u || f.entity_flags != 2u ||
        f.entity_flags2 || f.primary || f.offhand != 13 || f.viewlock || f.vehicle_index != 1023 ||
        f.vehicle_pos || f.vehicle_type) return false;
    for (const auto camera : f.camera) if (camera) return false;
    return true;
}

inline bool admission(const Facts& f) noexcept {
    if (!sentinel_motorcycle(f) || f.pm_flags) return false;
    return f.origin.x >= 9400.0f && f.origin.x <= 10050.0f &&
        f.origin.y >= 5300.0f && f.origin.y <= 6000.0f &&
        f.origin.z >= 1320.0f && f.origin.z <= 1510.0f;
}

inline bool named_vehicle(const Facts& f) noexcept {
    constexpr char expected[] = "motorcycle_player";
    return f.pre.sequence && f.pre.gameplay_epoch && f.native_time > 0 && f.health > 0 && !f.local &&
        (f.pm_type == 0 || f.pm_type == 1) && !(f.pm_flags & 0xc00) &&
        (f.entity_flags & 0x4000) && !(f.entity_flags & 0x300) &&
        !(f.entity_flags2 & 0x50040000) && !f.viewlock && f.vehicle_index >= 0 &&
        f.vehicle_index <= 1021 && f.vehicle_entity && f.script_vehicle && f.info &&
        !std::memcmp(f.vehicle_name.data(), expected, sizeof(expected));
}

// Vorkuta's escape truck seats the player at gunner position 1. It is not an
// ordinary viewlocked turret and has WEAP_DISABLE_WEAPONS while the vehicle
// weapon itself remains usable. Require its copied native identity and seat.
inline bool named_gunner(const Facts& f) noexcept {
    constexpr char expected[] = "truck_gaz63_player_single50";
    return f.pre.sequence && f.pre.gameplay_epoch && f.native_time > 0 && f.health > 0 && !f.local &&
        f.pm_type == 1 && !(f.pm_flags & 0xc00) &&
        (f.entity_flags & 0x4000) && !(f.entity_flags & 0x10300) &&
        !(f.entity_flags2 & 0x50040000) && !f.viewlock && f.vehicle_index >= 0 &&
        f.vehicle_index <= 1021 && f.vehicle_entity && f.script_vehicle && f.info &&
        f.vehicle_pos == 1 && f.vehicle_type == 0 &&
        !std::memcmp(f.vehicle_name.data(), expected, sizeof(expected));
}

// Rebirth's BTR is driven by the mission script. The local player occupies
// gunner seat 1; its native gun consumes ordinary attack, not driver attack.
// Keep this exception tied to the observed vehicle and first-person gunner
// camera instead of admitting every linked or EF_VEHICLE_ACTIVE player.
inline bool rebirth_apc_gunner(const Facts& f) noexcept {
    constexpr char expected[] = "apc_btr60_grenade";
    constexpr std::array<int, 7> gunner_camera{0, 0, 0, 0, 0, 0, 4};
    return f.pre.sequence && f.pre.gameplay_epoch && f.native_time > 0 && f.health > 0 && !f.local &&
        f.pm_type == 1 && !(f.pm_flags & 0xc00) && !(f.weapon_flags & 0x180) && !f.primary &&
        (f.entity_flags & 0x4000) && !(f.entity_flags & 0x10300) &&
        !(f.entity_flags2 & 0x50040000) && !f.viewlock && f.vehicle_index >= 0 &&
        f.vehicle_index <= 1021 && f.vehicle_entity && f.script_vehicle && f.info &&
        f.vehicle_pos == 1 && f.vehicle_type == 0 && f.camera == gunner_camera &&
        !std::memcmp(f.vehicle_name.data(), expected, sizeof(expected));
}

inline bool vehicle_gunner(const Facts& f) noexcept {
    return named_gunner(f) || rebirth_apc_gunner(f);
}

// Vorkuta briefly drives the player into a scripted mount state before it
// links the native motorcycle entity.  It keeps the sentinel vehicle index,
// but the otherwise unique admitted tuple changes to pm_type 1 / eFlag 0x80002.
// Retain this state only inside the already-admitted gameplay occurrence; it
// cannot independently select motorcycle controls elsewhere in the campaign.
inline bool mounting_weapon_flags(std::uint32_t flags) noexcept {
    // The landing transition adds only the 0x8 bit to the initial mount tuple.
    return flags == 0x02000080u || flags == 0x02000088u;
}

inline bool mounting_vehicle(const Facts& f) noexcept {
    return f.pre.sequence && f.pre.gameplay_epoch && f.native_time > 0 && f.health > 0 && !f.local &&
        f.pm_type == 1 && !(f.pm_flags & ~0x20u) && mounting_weapon_flags(f.weapon_flags) &&
        (f.entity_flags == 0x00080002u || f.entity_flags == 0x00084000u) &&
        !f.entity_flags2 && !f.primary && f.offhand == 13 &&
        !f.viewlock && f.vehicle_index == 1023 && !f.vehicle_pos && !f.vehicle_type;
}

// One command boundary during the scripted mount clears the player-state
// flags and refdef origin before publishing the pm_type 1 state above.  The
// native game clock, health, campaign occurrence and vehicle sentinel remain
// intact.  Like mounting_vehicle, this may only retain an existing admission.
inline bool mounting_blank(const Facts& f) noexcept {
    return f.pre.sequence && f.pre.gameplay_epoch && f.native_time > 0 && f.health > 0 && !f.local &&
        !f.pm_type && !f.pm_flags && !f.weapon_flags && !f.entity_flags && !f.entity_flags2 &&
        !f.primary && f.offhand == 13 && !f.viewlock && f.vehicle_index == 1023 &&
        !f.vehicle_pos && !f.vehicle_type && f.origin.x == 0.0f && f.origin.y == 0.0f && f.origin.z == 0.0f;
}

// The native command snapshot is captured at a stable hook boundary, while
// the larger protected copy may straddle Vorkuta's one-command mount rewrite.
// Bridge only the two exact transition tuples, and only for the already
// admitted cg/gameplay occurrence.  Neither tuple can start motorcycle mode.
inline bool retained_transition(const PreCommandSnapshot& p, std::uintptr_t retained_cg,
                                std::uint64_t retained_epoch) noexcept {
    if (!retained_cg || !retained_epoch || !p.sequence || !p.native_gameplay_fields_valid ||
        p.native_cg_identity != retained_cg || p.gameplay_epoch != retained_epoch ||
        p.native_game_time <= 0 || p.native_health <= 0 || (p.native_pm_flags & ~0x20u) ||
        p.native_entity_flags2) return false;
    const bool sentinel = !p.native_pm_type && p.native_weapon_flags == 0x02000080u &&
        p.native_entity_flags == 2u;
    const bool blank = !p.native_pm_type && !p.native_pm_flags &&
        !p.native_weapon_flags && !p.native_entity_flags;
    const bool mounting = p.native_pm_type == 1 && mounting_weapon_flags(p.native_weapon_flags) &&
        (p.native_entity_flags == 0x00080002u || p.native_entity_flags == 0x00084000u);
    return sentinel || blank || mounting;
}

// A headset frame can occasionally have a current OpenXR sample and native
// command while the pre-command snapshot reader misses that one command. Keep
// the already-proven motorcycle occurrence for at most 100 ms without
// acquiring or releasing native keys. Focus/menu/generation gates remain in
// the adapter; this helper only validates command continuity and the retained
// occurrence.
inline bool retained_snapshot_gap(const Facts& previous, const CommandContext& current,
                                  std::uint64_t now) noexcept {
    const auto& p = previous.pre;
    const bool recognized = admission(previous) || sentinel_motorcycle(previous) ||
        mounting_blank(previous) || mounting_vehicle(previous) || named_vehicle(previous) || vehicle_gunner(previous);
    if (!previous.cg || !p.gameplay_epoch || !p.sequence || !p.observed_ms ||
        now < p.observed_ms || now - p.observed_ms > 100 ||
        !recognized || !current.valid ||
        current.local_client || current.key_catcher || current.native_override ||
        current.connection_state < 9 || !current.native_thread ||
        current.native_thread != p.context.native_thread ||
        current.command_number <= p.context.command_number ||
        current.command_number > p.context.command_number + 32 ||
        current.native_frame_time < p.context.native_frame_time) return false;
    return true;
}

inline Facts transition_facts(const Facts& previous, const PreCommandSnapshot& p) noexcept {
    auto f = previous;
    f.pre = p;
    f.cg = p.native_cg_identity;
    f.ps = f.cg + 0x8a364;
    f.refdef = f.cg + 0x8c100;
    f.native_time = p.native_game_time;
    f.health = p.native_health;
    f.pm_type = p.native_pm_type;
    f.pm_flags = p.native_pm_flags;
    f.weapon_flags = p.native_weapon_flags;
    f.entity_flags = p.native_entity_flags;
    f.entity_flags2 = p.native_entity_flags2;
    if (!p.native_pm_type && !p.native_weapon_flags && !p.native_entity_flags) f.origin = {};
    return f;
}

inline bool eligible(const Facts& f, std::uintptr_t retained_cg = 0,
                     std::uint64_t retained_epoch = 0) noexcept {
    // Once linked, the copied native motorcycle_player graph proves identity
    // on its own. This also permits neutral rearming after pause/focus loss or
    // a checkpoint loaded during the ride, without revisiting the rooftop.
    if (admission(f) || (!retained_cg && !retained_epoch && (named_vehicle(f) || vehicle_gunner(f)))) return true;
    return retained_cg && retained_epoch && f.cg == retained_cg &&
        f.pre.gameplay_epoch == retained_epoch &&
        (sentinel_motorcycle(f) || mounting_blank(f) || mounting_vehicle(f) || named_vehicle(f) || vehicle_gunner(f));
}

inline bool same_motorcycle(const Facts& a, const Facts& b) noexcept {
    if (a.cg != b.cg || a.pre.gameplay_epoch != b.pre.gameplay_epoch) return false;
    if (vehicle_gunner(a) || vehicle_gunner(b))
        return vehicle_gunner(a) && vehicle_gunner(b) && a.vehicle_entity == b.vehicle_entity &&
            a.script_vehicle == b.script_vehicle && a.info == b.info && a.vehicle_index == b.vehicle_index &&
            a.vehicle_pos == b.vehicle_pos && a.vehicle_type == b.vehicle_type && a.vehicle_name == b.vehicle_name;
    const bool a_sentinel = sentinel_motorcycle(a), b_sentinel = sentinel_motorcycle(b);
    const bool a_blank = mounting_blank(a), b_blank = mounting_blank(b);
    const bool a_mount = mounting_vehicle(a), b_mount = mounting_vehicle(b);
    const bool a_vehicle = named_vehicle(a), b_vehicle = named_vehicle(b);
    if (a_sentinel && b_sentinel) return true;
    if ((a_sentinel && (b_blank || b_mount || b_vehicle)) ||
        ((a_blank || a_mount || a_vehicle) && b_sentinel)) return true;
    if ((a_blank && (b_blank || b_mount || b_vehicle)) ||
        ((a_mount || a_vehicle) && b_blank)) return true;
    if ((a_mount && (b_mount || b_vehicle)) || (a_vehicle && b_mount)) return true;
    return a_vehicle && b_vehicle && a.vehicle_entity == b.vehicle_entity &&
        a.script_vehicle == b.script_vehicle && a.info == b.info && a.vehicle_index == b.vehicle_index;
}

inline bool copy_current(const Host& host, std::uintptr_t base, const PreCommandSnapshot& pre,
                         const CommandContext& context, std::uint64_t now, Facts* out) noexcept {
    if (!out) return false;
    *out = {};
    if (base != detail::image_base || !host.copy || !host.pre || !host.thread || !context.valid ||
        context.local_client || !context.native_thread || host.thread(host.context) != context.native_thread ||
        context.command_number < 0 || context.native_frame_time < 0 || context.key_catcher ||
        context.native_override || context.connection_state < 9 || !pre.sequence || !pre.gameplay_epoch ||
        !pre.observed_ms || now < pre.observed_ms || now - pre.observed_ms > 100 ||
        !detail::same_command(pre.context, context)) return false;
    PreCommandSnapshot first{}, last{};
    Facts a{}, b{};
    if (!host.pre(host.context, &first) || !detail::same_pre(first, pre) ||
        !detail::read(host, base, pre, a) || !detail::read(host, base, pre, b) ||
        !detail::same_facts(a, b) || !host.pre(host.context, &last) ||
        !detail::same_pre(first, last) || host.thread(host.context) != context.native_thread) return false;
    *out = b;
    return true;
}

inline bool copy_current(std::uintptr_t base, const PreCommandSnapshot& pre,
                         const CommandContext& context, std::uint64_t now, Facts* out) noexcept {
    const auto error = GetLastError();
    const bool result = copy_current({nullptr, detail::protected_copy, detail::native_pre, detail::native_thread},
                                     base, pre, context, now, out);
    SetLastError(error);
    return result;
}
} // namespace bovr::t5::motorcycle
