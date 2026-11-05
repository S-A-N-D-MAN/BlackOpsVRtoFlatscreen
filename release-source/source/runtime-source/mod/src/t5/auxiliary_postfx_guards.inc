// SPDX-License-Identifier: GPL-3.0-only
// Generated from pinned disk and identical historical stock loaded text.
bool validate_auxiliary_postfx_guards(HMODULE module) noexcept {
 const auto error=GetLastError();const auto base=reinterpret_cast<std::uintptr_t>(module);
 if(base!=0x400000||module!=GetModuleHandleW(nullptr)){SetLastError(error);return false;}
 // standard_view_complete

 // standard_post_effects_complete

 // late_post_effects_complete

 // process_post_effects_complete

 // save_screen_blur_complete

 // save_screen_flash_body

 // save_screen_complete

 // resolve_complete

 // fullscreen_colored_filter_complete

 // fullscreen_filter_complete

 // split_filter_complete

 // bloom_initial_target_and_filter_selection

 // native_scene_def_copy

 bool result=false;__try {result=
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d2100), 3018, "2e0e1206c51edd57ed6f5253efeadf7e418290f6d3803240f95065bea74bbc32")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d1d60), 369, "22dacfed7ad05d39530f2f8f9833447c8212146dccbd7a8eb5eed694a4f26a46")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d1800), 178, "9d8aaf2acef5b1b644adecd46275dac8d05aedd2443b84de4ccef6b81fe37c0a")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x334280), 536, "4cd257f58a1f2a5dd98224cde9fdc3367e0f1c7a755a3a90deae622cf6887644")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2ec310), 536, "89e376a9c955a4a78effdd73c8f45ac6931e9a06dbbc9b73f59a09760e1a7ecf")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2ec530), 512, "252e18db1b416debfba589592663dc6beb7a6853b2d98070b260026ddb8e81d5")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2ec730), 694, "17b352fa302ac7fc57d72736c6e2cfb94947a949ebe940d01cb2e47dc0423b97")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2e4ec0), 760, "681866d4abea332376fdc889214cb468485916a8fffa46e0900614ef71fe5122")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2e4a70), 118, "72530416a1aa515767fc7fd6a8e73e80bad3d234a4fcc13dbd0cff317ca94714")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2e4af0), 153, "a6bf198bf4b37a2965531ac3d14f46780d60063df19ac3c5f76e0328608520ff")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2e4b90), 373, "b4cb6248f1fd204010257742322a19ffde1556f9729d593fbf884a49c1709874")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x330700), 404, "ed915a5544dcfae5401f1dda5ce2566727b5d96409c2881fad473e9bff0f07cc")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x31ee10), 34, "33f0f46fd978de61eea6f6ae50f271cb0562c719ae0fef05017c78b6b5dd3836")
 ;}__except(EXCEPTION_EXECUTE_HANDLER){result=false;}SetLastError(error);return result;
}
