// Existing isolated legacy fixtures exercise aim-off input paths only.
#include "t5/native_command_aim_adapter.hpp"
namespace bovr::t5 {
bool exact_profile_aim_enabled() noexcept{return false;}
bool exact_profile_input_owner(const CommandContext&,bool) noexcept{return true;}
bool native_command_aim_enabled() noexcept{return false;}
void invalidate_native_command_aim() noexcept{}
void retire_native_command_aim_for_selection(const CommandContext&) noexcept{}
bool configure_native_command_aim(HMODULE,bool) noexcept{return false;}
void begin_native_command_aim(const CommandContext&,const wawvr::xr::ActionSnapshot&,std::uint64_t,const input::CommandRigSample&,bool) noexcept{}
void finish_native_command_aim(const CommandContext&,unsigned char*) noexcept{}
bool commit_native_command_aim_body_yaw(const CommandContext&,const input::CommandRigSample&,const wawvr::xr::Posef&,float,float) noexcept{return false;}
bool command_input_generation_current(const input::CommandRigSample&) noexcept{return false;}
bool commit_vr_body_yaw(const input::CommandRigSample&,const wawvr::xr::Posef&,float,float) noexcept{return false;}
bool commit_native_command_aim_room_scale(const CommandContext&,const input::CommandRigSample&,const wawvr::xr::Posef&,const RoomScaleView&) noexcept{return false;}
bool commit_vr_room_scale(const input::CommandRigSample&,const wawvr::xr::Posef&,const RoomScaleView&) noexcept{return false;}
void revoke_room_scale_view() noexcept{}
}
