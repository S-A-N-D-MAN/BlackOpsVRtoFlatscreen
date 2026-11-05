// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/auxiliary_view.hpp"
#include <cstring>
namespace bovr::t5 {
namespace {
template<class T>void av_read(std::uintptr_t address,T* value) noexcept {std::memcpy(value,reinterpret_cast<const void*>(address),sizeof(T));}
bool av_once(std::uintptr_t base,bool frontend,AuxiliaryViewLayout* out) noexcept {
    AuxiliaryViewLayout s{};
    __try {
        const auto global=base+(frontend?0x373708c:0x3a5b5f8);av_read(global,&s.buffer);
        const unsigned slot=s.buffer==base+0x3740100?0:s.buffer==base+0x38c6400?1:2;if(slot>1)return false;
        av_read(s.buffer+0x15d7a0,&s.frame);av_read(s.buffer+0x16cc38,&s.selected);av_read(s.buffer+0x16cc3c,&s.count);av_read(s.buffer+0x16cc40,&s.view_array);
        if((s.count!=2&&s.count!=3)||s.selected!=s.count-1||s.view_array!=base+0x420f730+slot*0xe380)return false;
        for(unsigned i=0;i<s.count;++i){auto& v=s.slots[i];v.address=s.view_array+i*0x38e0;
            av_read(v.address+0x140,&v.scene);av_read(v.address+0x150,&v.display);av_read(v.address+0x160,&v.scissor);
            av_read(v.address+0x35f0,&v.targets);av_read(v.address+0x38c4,&v.extra);}
        for(unsigned i=0;i<2;++i){auto& t=s.targets[i];const auto p=base+0x41eb1e8+(22+i)*20;
            av_read(p,&t.image);av_read(p+4,&t.color);av_read(p+8,&t.depth);av_read(p+12,&t.width);av_read(p+14,&t.height);}
        std::uintptr_t buffer{};std::uint32_t count{},index{},frame{};av_read(global,&buffer);av_read(s.buffer+0x16cc3c,&count);
        av_read(s.buffer+0x16cc38,&index);av_read(s.buffer+0x15d7a0,&frame);
        if(buffer!=s.buffer||count!=s.count||index!=s.selected||frame!=s.frame)return false;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    *out=s;return true;
}
}
bool read_auxiliary_layout(std::uintptr_t base,bool frontend,AuxiliaryViewLayout* out) noexcept {
    if(out)*out={};if(!out||!base||base>UINT32_MAX-auxiliary_read_extent)return false;
    AuxiliaryViewLayout a{},b{};if(!av_once(base,frontend,&a)||!av_once(base,frontend,&b)||a!=b)return false;*out=b;return true;
}

bool validate_auxiliary_native_guards(HMODULE module) noexcept {
 const auto base=reinterpret_cast<std::uintptr_t>(module);
 if(base!=0x400000||module!=GetModuleHandleW(nullptr))return false;
 // selected_view_and_extra_flag; pinned retail SP and prior loaded text.

 // extra_view_then_previous; pinned retail SP and prior loaded text.

 // two_descending_native_draws; pinned retail SP and prior loaded text.

 // ordinary_selected_native_draw; pinned retail SP and prior loaded text.

 // standard_path_arg_from_EAX; pinned retail SP and prior loaded text.

 // extra_camera_flag_and_floatz_id; pinned retail SP and prior loaded text.

 // extra_target_clear_branch; pinned retail SP and prior loaded text.

 // extra_target22_first_bind; pinned retail SP and prior loaded text.

 // ordinary_target_from_view; pinned retail SP and prior loaded text.

 // extra_target22_draw_bind; pinned retail SP and prior loaded text.

 // hud_skips_extra_flag; pinned retail SP and prior loaded text.

 // generator_append_native_count_and_index; pinned retail SP and prior loaded text.

 // generator_sixth_argument_extra_flag; pinned retail SP and prior loaded text.

 // generator_extra_flag_target22; pinned retail SP and prior loaded text.

 // generator_regular_flag_target3; pinned retail SP and prior loaded text.

 // scene_viewport_copy; pinned retail SP and prior loaded text.

 // display_viewport_copy; pinned retail SP and prior loaded text.

 // scissor_viewport_copy; pinned retail SP and prior loaded text.

 // scene_composition_extra_and_regular_ids; pinned retail SP and prior loaded text.

 // two_native_backend_allocations; pinned retail SP and prior loaded text.

 // native_frame_counter_publish; pinned retail SP and prior loaded text.

 // four_view_storage_per_smp_slot; pinned retail SP and prior loaded text.

 // target_table_image_stride20; pinned retail SP and prior loaded text.

 // target_table_surface_and_native_bind; pinned retail SP and prior loaded text.

 // target_table_u16_width_height; pinned retail SP and prior loaded text.

 // target_table_depth_pointer; pinned retail SP and prior loaded text.

 // extra_renderer_sets_sixth_argument_true; pinned retail SP and prior loaded text.

 // extra_scene_origin_and_dimensions; pinned retail SP and prior loaded text.

 __try {return !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2eb78a), 29, "60caab3ba0a7bebd29a6558525aca35d174007904a1015e47378376a6cabfa1d")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2eb7a7), 22, "55982d040fed1968b62ff988e4e9f3e7ff7f71cde73045b17fa912defda2e4ec")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2eb7c0), 24, "f8e83339e7e91ed74be878ec44f24249ada747b01cd229ef70bd54849e8d40c9")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2eb7df), 5, "c3d46a15bcd661250a1f78c540e85c84478df95440f76e83006886856d2801dc")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d2ed6), 11, "ff2400dc6ae17077593525a7beee23668cd4f17f352e83b163880964341d6dec")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d2111), 19, "825229932a94fe552cebfa572c19ae28113be8ac7f41ade025ad9096f93cce9d")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d22fc), 28, "d48318b72e0cf465fcea403c8ca3ca5dadbdff5666b2888629deb7248b398505")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d239c), 9, "091e20579d44fa9c3a9ff114a583ea97fcedab0ca2f962071f0abe1af2eba4e2")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d2458), 26, "c30ce04e923517b560a39e12989e3a85a4649e88c75096aee33f8019449d5a3f")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d25b6), 6, "1730430cbaf8e5085a09aef6d34e70e9e4b4c728ba055b23d5282d163035c0c2")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d2d11), 7, "6d58930f448fd797c4a651d3203289c4812852f12d65bb4a16a39347cbcb1cbc")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2c645c), 64, "73641e7d885acdd739e385596fdd1ebc1b5c967ce6c8d7113eb7b8573456428e")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2c649c), 19, "8c98b223f268ad5251e25fd15ad1b23419cfb2e70426948662d1cb008ee2b5c0")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2c67f5), 14, "5980203c81d614e1af3d0a61cd4cc7e034f7f4dc9a09666c91b911c427f35335")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2c6828), 14, "2e7ce0909066f62814b2656aee3d7989369b1ed632e98120bd6afa65ed433b6f")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2c66ee), 24, "7531ac755b7d5df31f0a27f85f99c309855eba9e1a20abf1d725a7089aef36ca")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2c671b), 37, "d4e681d84800ba804bb74cbb5baf3589a5f50cbcca68a9451c9788f1389b2366")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2c6751), 30, "b30cfce7e574f2647defe91b2cc0124b1b53d8af3ba26809f388a1e93fe34108")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2c6ec3), 70, "f4b46a47b5c3a764661ecb4affee02cfd183df56d126e233f900cb83f00486e8")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d5b24), 22, "6792b92c833569551a7638acbffb7e26cd69c7dd5e83c453f3f0d0e4b9aa5e60")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d5b71), 17, "d275805c18c0572c8140d8c52a4da54b4b05e4c195c7f1aede1ff3ee73acd281")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d5b93), 25, "14c4d2444c83e145d6d9991e4ef1eb9db47110abc4a5240ab69c96bb94a8a23a")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x32667b), 13, "da288405cd0ba02b406ce19d38ad6b6b63c3d72e06caef563afb74be9ffb3e24")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x324a07), 35, "01fbb60ff00d775a5f92695ba31e6cdc66739560d33267619f45a28c63d5b4a4")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x324a41), 20, "bbfe07ab5b237cb965f886c2fc1da4701fc9c2479268cfa05c21c0b287c323d6")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x324a84), 16, "c16f6fa6f70d33ccc7e04b245ef3e2c7deb60bce5448c509b1048995e282dd6f")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2c8fe0), 29, "4d1d1fe8f40a992fa17b57df432c204eb544ffe09de335b114651e7119305509")&&
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2c8555), 47, "bb266df6e68cf4428b223ca4816d101d33c993a10216b57f0ccfa2c3fc760cc9");}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
