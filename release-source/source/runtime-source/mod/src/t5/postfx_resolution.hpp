// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <initializer_list>
#include "render/hud_eye_commands.hpp"
namespace bovr::t5 {
struct PostfxResolution {unsigned width{},height{};};
inline constexpr bool postfx_resolution(unsigned width,unsigned height) noexcept {
    return (width==1280&&height==720)||(width==2560&&height==1440)||(width==3840&&height==2160)||
           (width==5120&&height==2880);
}
inline bool postfx_scene_resolution(unsigned eye,const render::HudEyeViewport& scene,PostfxResolution& out) noexcept {
    out={};
    if(eye>1||scene.width<=0||scene.width>2560||scene.height<=0||scene.y||
       scene.x!=static_cast<int>(eye)*scene.width||
       !postfx_resolution(static_cast<unsigned>(scene.width)*2,static_cast<unsigned>(scene.height)))return false;
    out={static_cast<unsigned>(scene.width)*2,static_cast<unsigned>(scene.height)};return true;
}
inline bool postfx_target_resolution(unsigned target,PostfxResolution scene,PostfxResolution& out) noexcept {
    out={};if(!postfx_resolution(scene.width,scene.height))return false;
    const unsigned divisor=target==3||target==6?1:target==25?2:target==26?4:target>=27&&target<=30?8:0;
    if(!divisor)return false;out={scene.width/divisor,scene.height/divisor};return true;
}
inline constexpr bool postfx_pyramid_resolution(unsigned width,unsigned height) noexcept {
    for(unsigned scale=1;scale<=4;++scale)for(unsigned divisor:{1u,2u,4u,8u})
        if(width==1280*scale/divisor&&height==720*scale/divisor)return true;
    return false;
}
}
