// SPDX-License-Identifier: GPL-3.0-only
// Generated exact PE/historical stock guards; no instruction patch.
bool validate_bloom_shader_guards(HMODULE module) noexcept {
 const auto error=GetLastError();const auto base=reinterpret_cast<std::uintptr_t>(module);
 if(base!=0x400000||module!=GetModuleHandleW(nullptr)){SetLastError(error);return false;}
 // quad material technique selection

 // tessellation pass loop

 // setup pass and pixel shader

 // update vertex declaration and shader

 // code vertex constant

 // object arguments

 // stable vertex and texture arguments

 // stable pixel arguments

 // native image sampler binding

 // begin view complete

 // set3D complete

 // ordinary view begin call

 // global view begin call

 // split filter complete

 // set2D complete

 bool result=false;__try {result=
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x30b610), 101, "570634b596ccbfb776ef6beff0de8386d7fa8962a83fa8173853557c1cf20efc")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x30b430), 469, "9fb0de98d76f58ea422a7c3967684345c8d0fd0db22d36f07b0558a0484c3066")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x3279e0), 239, "611b9ea09427683cbb6b095868bf967f047c26862740d2f6ea8258458edce337")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x327930), 162, "d4bc4a4446eea61b7f16ee1efb5cc902266b69a06e48786da16a0a0410b3e725")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x327010), 254, "1e3daf7ac9be96a32321c99e408cbd72bfcb6511976fb6c5ec1e18aeb1a35691")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x327110), 263, "8f1b208d27a8149aeacf01037dde15dff977c7958762a2e386cbbe099febfbc6")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x327690), 451, "1d7614b06327b15dc202bc343daaf503e92519d4e35e97da0a87015ce1d68f56")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x327290), 344, "e7b4fde86cfe17e91a636d833c49ffc0d0aec752c88382f37dd67249a7f0dba3")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x325bc0), 140, "02687ad0c637da17e5d5491b16405408fccb1549b501102bea3e54f60d2a5c2f")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x31ee10), 216, "17f3965b7ba8d5b65ccfbe46c560a903f0825ffe16906486cf1e91f531edf267")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x31ec30), 167, "c686178b988ca24da06791083b15d036b6325550fe90f778dd241adb570d991d")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2d084a), 13, "0adb4591e7760c26029a452c29c3ab09498214cb8a09ce167d8cdb012a6bf1c9")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2cfbd9), 18, "675fe1d1877f3b0e1e07f7b41f2234d2ccb90b05fa0e77c7e5f366ee52c454a0")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2e4b90), 373, "b4cb6248f1fd204010257742322a19ffde1556f9729d593fbf884a49c1709874")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x31eb30), 105, "544406dae2617c2074751461acdf4bca757e86cb8d65ac51f8271e264f9987a0")
 ;}__except(EXCEPTION_EXECUTE_HANDLER){result=false;}SetLastError(error);return result;
}
