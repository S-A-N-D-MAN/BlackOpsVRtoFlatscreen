// Existing isolated native-copy fixtures keep Q1 disabled.
#include "t5/native_command_aim_adapter.hpp"
namespace bovr::t5 {
bool exact_profile_aim_enabled() noexcept{return false;}
bool native_command_dual_current(const CommandContext&) noexcept{return false;}
bool native_command_hitscan_current(const CommandContext&) noexcept{return false;}
bool native_command_ordinary_attack_inhibited(const CommandContext&) noexcept{return false;}
void invalidate_native_command_aim() noexcept{}
bool read_command_profile_admission(const animation_detail::ClearComparisonKey&,
    input::profile_admission::Token*,command_profile::Copy*) noexcept{return false;}
bool command_profile_admission_witness(input::profile_admission::Token) noexcept{return false;}
}
