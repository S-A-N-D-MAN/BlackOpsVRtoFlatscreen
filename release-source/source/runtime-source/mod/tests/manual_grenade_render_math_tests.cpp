// SPDX-License-Identifier: GPL-3.0-only

#include "t5/manual_grenade_render_math.hpp"
#include "t5/manual_grenade_native_guards.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace {

namespace animation = bovr::t5::animation_detail;
namespace grenade = bovr::t5::grenade_render_detail;
namespace native = bovr::t5::manual_grenade_native_guards;

int checks = 0;
int failures = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

bool approximately(float actual, float expected, float tolerance = 0.001F) {
    return std::fabs(actual - expected) <= tolerance;
}

void test_primary_visual_ownership() {
    constexpr unsigned offhand = 9U;
    constexpr unsigned primary = 12U;

    for (int state = 21; state <= 26; ++state) {
        check(
            grenade::owns_primary_visual(
                true, 2U, state, offhand, offhand, primary, primary),
            "owned offhand state admits the preserved primary visual");
    }
    check(
        grenade::owns_primary_visual(
            true, 2U, 22, offhand, offhand, 0U, primary),
        "native zero weapon during offhand still owns the captured primary visual");
    check(
        grenade::owns_primary_visual(
            true, 0x102U, 22, offhand, offhand, primary, primary),
        "unrelated weapon flags do not block owned visual scope");

    check(
        !grenade::owns_primary_visual(
            false, 2U, 22, offhand, offhand, primary, primary),
        "invalid ticket rejects visual scope");
    check(
        !grenade::owns_primary_visual(
            true, 0U, 22, offhand, offhand, primary, primary),
        "missing offhand flag rejects visual scope");
    check(
        !grenade::owns_primary_visual(
            true, 4U, 22, offhand, offhand, primary, primary),
        "a different weapon flag rejects visual scope");
    check(
        !grenade::owns_primary_visual(
            true, 2U, 20, offhand, offhand, primary, primary),
        "state below the owned range rejects visual scope");
    check(
        !grenade::owns_primary_visual(
            true, 2U, 27, offhand, offhand, primary, primary),
        "state above the owned range rejects visual scope");
    check(
        !grenade::owns_primary_visual(
            true, 2U, 22, offhand + 1U, offhand, primary, primary),
        "offhand mismatch rejects visual scope");
    check(
        !grenade::owns_primary_visual(
            true, 2U, 22, offhand, offhand + 1U, primary, primary),
        "ticket weapon mismatch rejects visual scope");
    check(
        !grenade::owns_primary_visual(
            true, 2U, 22, offhand, offhand, 0U, 0U),
        "missing primary rejects visual scope");
    check(
        !grenade::owns_primary_visual(
            true, 2U, 22, offhand, offhand, primary, primary + 1U),
        "a different primary than the grab-edge model rejects visual scope");
}

void test_primary_word_substitution() {
    constexpr std::uint32_t original = 0xA5A50000U;
    constexpr unsigned primary = 0x1234U;
    constexpr std::uint32_t injected =
        grenade::replace_low_word(original, primary);
    check(injected == 0xA5A51234U,
        "primary injection preserves the neighboring upper player-state field");
    check(grenade::low_word_is(injected, primary),
        "injected player-state word reports the requested primary weapon");
    check(!grenade::low_word_is(injected, primary + 1U),
        "restore guard rejects a foreign low-word update");
    check(grenade::replace_low_word(0x5A5AFFFFU, original) == 0x5A5A0000U,
        "low-word restoration preserves a concurrently changed upper field");
}

void test_model_cache_selection() {
    struct Entry final {
        std::uintptr_t model{};
    };
    std::array<Entry, 3> entries{};

    check(
        grenade::claim_model_cache_slot(entries, 0U) ==
            grenade::no_model_cache_slot,
        "null projectile model cannot claim a cache slot");
    check(
        grenade::claim_model_cache_slot(entries, 0x1000U) == 0U &&
            entries[0].model == 0x1000U,
        "first projectile model claims the first empty slot");
    check(
        grenade::claim_model_cache_slot(entries, 0x1000U) == 0U &&
            entries[1].model == 0U,
        "the same projectile model reuses one cache slot");
    check(
        grenade::claim_model_cache_slot(entries, 0x2000U) == 1U &&
            grenade::claim_model_cache_slot(entries, 0x3000U) == 2U,
        "distinct projectile models fill the bounded cache");
    check(
        grenade::claim_model_cache_slot(entries, 0x4000U) ==
            grenade::no_model_cache_slot,
        "a full permanent cache refuses an additional model");
    check(
        grenade::projectile_model_cache_capacity == 256U,
        "production cache covers the bounded weapon table");
}

void test_axis_to_angles() {
    float angles[3]{99.0F, 99.0F, 99.0F};
    const animation::Axis identity{
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F}};
    check(grenade::axis_to_angles(identity, angles), "identity basis is valid");
    check(
        approximately(angles[0], 0.0F) && approximately(angles[1], 0.0F) &&
            approximately(angles[2], 0.0F),
        "identity basis converts to zero Euler angles");

    const animation::Axis yaw_90{
        {0.0F, 1.0F, 0.0F},
        {-1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F}};
    check(grenade::axis_to_angles(yaw_90, angles), "yaw basis is valid");
    check(
        approximately(angles[0], 0.0F) && approximately(angles[1], 90.0F) &&
            approximately(angles[2], 0.0F),
        "yaw basis converts in T5 pitch-yaw-roll order");

    constexpr float half_root_two = 0.7071067811865475F;
    const animation::Axis pitch_45{
        {half_root_two, 0.0F, -half_root_two},
        {0.0F, 1.0F, 0.0F},
        {half_root_two, 0.0F, half_root_two}};
    check(grenade::axis_to_angles(pitch_45, angles), "pitch basis is valid");
    check(
        approximately(angles[0], 45.0F) && approximately(angles[1], 0.0F) &&
            approximately(angles[2], 0.0F),
        "pitch basis converts in T5 pitch-yaw-roll order");

    const animation::Axis roll_minus_45{
        {1.0F, 0.0F, 0.0F},
        {0.0F, half_root_two, -half_root_two},
        {0.0F, half_root_two, half_root_two}};
    check(
        grenade::axis_to_angles(roll_minus_45, angles),
        "roll basis is valid");
    check(
        approximately(angles[0], 0.0F) && approximately(angles[1], 0.0F) &&
            approximately(angles[2], -45.0F),
        "roll basis converts in T5 pitch-yaw-roll order");

    const animation::Axis duplicate_axes{
        {1.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F}};
    angles[0] = angles[1] = angles[2] = 99.0F;
    check(
        !grenade::axis_to_angles(duplicate_axes, angles),
        "non-orthogonal basis is rejected");
    check(
        approximately(angles[0], 0.0F) && approximately(angles[1], 0.0F) &&
            approximately(angles[2], 0.0F),
        "invalid basis clears the angle output");

    const animation::Axis left_handed{
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, -1.0F}};
    check(
        !grenade::axis_to_angles(left_handed, angles),
        "left-handed basis is rejected");

    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    const animation::Axis non_finite{
        {nan, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F}};
    check(
        !grenade::axis_to_angles(non_finite, angles),
        "non-finite basis is rejected");
    check(
        !grenade::axis_to_angles(identity, nullptr),
        "null angle output is rejected");
}

void test_scene_slot_selection() {
    std::array<std::uint16_t, 8> xmodel{};
    std::array<std::uint16_t, 8> dobj{};
    xmodel.fill(grenade::empty_scene_entry);
    dobj.fill(grenade::empty_scene_entry);

    xmodel[7] = 4U;
    dobj[6] = 5U;
    std::uint32_t slot = grenade::no_scene_slot;
    check(
        grenade::find_scene_slot(xmodel, dobj, 2U, 7U, &slot) && slot == 5U,
        "slot search skips either kind of occupied entry from high to low");

    xmodel[5] = 6U;
    check(
        grenade::find_scene_slot(xmodel, dobj, 2U, 7U, &slot) && slot == 4U,
        "slot search continues downward after the highest empty slot changes");

    check(
        grenade::find_scene_slot(xmodel, dobj, 3U, 3U, &slot) && slot == 3U,
        "slot range is inclusive at both endpoints");

    xmodel[3] = 7U;
    slot = 11U;
    check(
        !grenade::find_scene_slot(xmodel, dobj, 3U, 3U, &slot) &&
            slot == grenade::no_scene_slot,
        "an XModel-only occupied slot is rejected");
    xmodel[3] = grenade::empty_scene_entry;
    dobj[3] = 8U;
    check(
        !grenade::find_scene_slot(xmodel, dobj, 3U, 3U, &slot),
        "a DObj-only occupied slot is rejected");

    check(
        !grenade::find_scene_slot(xmodel, dobj, 5U, 4U, &slot),
        "reversed slot range is rejected");
    check(
        !grenade::find_scene_slot(xmodel, dobj, 0U, 8U, &slot),
        "XModel out-of-range endpoint is rejected");
    const std::span<const std::uint16_t> short_dobj{dobj.data(), 4U};
    check(
        !grenade::find_scene_slot(xmodel, short_dobj, 0U, 4U, &slot),
        "DObj out-of-range endpoint is rejected");

    const std::span<const std::uint16_t> null_span{};
    check(
        !grenade::find_scene_slot(null_span, dobj, 0U, 0U, &slot),
        "null XModel span is rejected");
    check(
        !grenade::find_scene_slot(xmodel, null_span, 0U, 0U, &slot),
        "null DObj span is rejected");
    check(
        !grenade::find_scene_slot(xmodel, dobj, 0U, 0U, nullptr),
        "null slot output is rejected");
}

void test_native_entry_guards() {
    constexpr char fixture[]="BlackOpsVR native span guard test";
    constexpr char fixture_sha256[]="882ac6508bac81eb03f5c104cc91b51a00b0a3bdf2d587af5db94b00a3d6152a";
    check(bovr::native_guard::matches(fixture,sizeof(fixture)-1,fixture_sha256),
        "SHA-256 native guard accepts its complete synthetic fixture");
    std::array<unsigned char,sizeof(fixture)-1> changed{};
    std::memcpy(changed.data(),fixture,changed.size());
    for(auto& byte:changed){byte^=1;
        check(!bovr::native_guard::matches(changed.data(),changed.size(),fixture_sha256),
            "every synthetic native guard byte contributes to the digest");
        byte^=1;
    }
    check(!bovr::native_guard::matches(fixture,sizeof(fixture)-2,fixture_sha256)&&
        !bovr::native_guard::matches(fixture,sizeof(fixture)-1,"invalid"),
        "wrong native span length and malformed digest fail closed");
    const auto image_size=native::add_view_weapon_rva+native::add_view_weapon_entry_guard.size;
    std::vector<unsigned char> image(image_size,0);
    check(!native::execute_loaded(image.data())&&!native::renderer_loaded(image.data()),
        "synthetic zero image cannot satisfy retail native entry digests");
    check(!native::execute_loaded(nullptr)&&!native::renderer_loaded(nullptr),
        "null manual-grenade native image is rejected");
    auto* inaccessible=static_cast<unsigned char*>(VirtualAlloc(nullptr,image.size(),MEM_RESERVE,PAGE_NOACCESS));
    check(inaccessible!=nullptr,"private unreadable manual-grenade native reservation");
    if(inaccessible){
        check(!native::execute_loaded(inaccessible)&&!native::renderer_loaded(inaccessible),
            "protected manual-grenade native entry read faults are rejected");
        VirtualFree(inaccessible,0,MEM_RELEASE);
    }
}

} // namespace

int main() {
    test_primary_visual_ownership();
    test_primary_word_substitution();
    test_model_cache_selection();
    test_axis_to_angles();
    test_scene_slot_selection();
    test_native_entry_guards();
    if (failures != 0) {
        std::fprintf(stderr, "%d of %d checks failed\n", failures, checks);
        return 1;
    }
    std::printf("manual_grenade_render_math_tests passed (%d checks)\n", checks);
    return 0;
}
