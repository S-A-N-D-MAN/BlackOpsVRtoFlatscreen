// SPDX-License-Identifier: GPL-3.0-only
#include "ui/menu_panel.hpp"
#include "menu_panel_logic.hpp"
#include "xr_math.h"
#include <cmath>
namespace bovr::ui {
bool MenuPanel::update(const wawvr::xr::Posef& head,std::uint64_t generation,OpaqueContext context,
                       std::uint32_t width,std::uint32_t height,wawvr::xr::QuadLayer* output) noexcept {
    if (output) *output={};
    const auto& p=head.position; const auto& q=head.orientation;
    const float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    if (!output || !generation || context==OpaqueContext::none || !width || !height || width>16384 || height>16384 ||
        !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(norm) || norm<1e-8f) return false;
    if (!valid_ || generation!=generation_ || context!=context_) {
        const auto forward=wawvr::xr::Rotate(wawvr::xr::Normalize(q),{0,0,-1});
        if (forward.x*forward.x+forward.z*forward.z<1e-6f) return false;
        const float yaw=std::atan2(-forward.x,-forward.z);
        wawvr::xr::Posef leveled=head;
        leveled.orientation={0,std::sin(yaw*0.5f),0,std::cos(yaw*0.5f)};
        if (!wawvr::mod::build_world_menu_panel(leveled,&panel_)) return false;
        valid_=true; generation_=generation; context_=context;
    }
    // Full source pixels fill the panel. Physical dimensions restore the
    // native image aspect after the intermediate square XR swapchain copy.
    panel_.size_meters.y=panel_.size_meters.x*static_cast<float>(height)/static_cast<float>(width);
    *output=panel_; return true;
}
void MenuPanel::clear() noexcept { valid_=false; generation_=0; context_=OpaqueContext::none; panel_={}; }
}
