// Standalone native provider tests do not initialize the feedback adapter.
#include "t5/shot_haptic_adapter.hpp"
namespace bovr::t5 {
float predicted_shot_kickback(std::uint64_t,std::uint64_t,unsigned,std::uint64_t) noexcept {return 0.f;}
}
