// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include "input/command_rig_ledger.hpp"
#include "t5/command_probe.hpp"
namespace bovr::t5 {
// Passive association only. Configure at the serialized factory after the
// existing input callbacks and guarded consumed-command observer are ready.
// BOVR_COMMAND_RIG_PROBE=1 is an additional, default-off requirement.
bool configure_command_rig_adapter(HMODULE,bool input_adapter_ready,bool consumed_hook_ready) noexcept;
bool command_rig_enabled() noexcept;
// Input calls begin once with the SAME copied sample used by its ActionRouter.
// Finish runs after all existing analog edits, including neutral/early returns.
// Neither function reads a newer pose or writes any native command/shot field.
void begin_command_rig(const CommandContext&,const wawvr::xr::ActionSnapshot&,
                       std::uint64_t generation,const input::CommandRigSample&,bool actions_available) noexcept;
void finish_command_rig(const CommandContext&,const unsigned char* final_command) noexcept;
void cancel_command_rig(const char* reason) noexcept;
// Copies a matched rig only inside the actual same-thread native ClientThink
// scope. Hidden during before/after callbacks and after nested/unwound scopes.
// This is evidence of association, NOT weapon lifetime or shot-write authority.
bool read_current_command_rig(input::CommandRigMatch*) noexcept;
struct NativeLocalShot;
// Bounded copied evidence from inside the existing completed-bullet observer.
// Returns association availability only; never selects or changes a shot pose.
bool trace_native_shot_command_rig(const NativeLocalShot&) noexcept;
}
