// SPDX-License-Identifier: GPL-3.0-only
// Generated from the pinned and historical native proof.
bool validate_history_correction_guards(HMODULE module) noexcept {
 const auto error=GetLastError();const auto base=reinterpret_cast<std::uintptr_t>(module);
 if(base!=0x400000||module!=GetModuleHandleW(nullptr)){SetLastError(error);return false;}
 // standard view complete

 // standard post source initialization

 // late post wrapper

 // postprocessing complete

 // postprocessing positive effects predicate

 // custom pass predicate complete

 // bloom complete including final split filter

 // blur complete

 // save complete

 // native split filter complete

 // quad builder complete

 // whole surface resolve complete

 // set 2D complete

 // 2D projection complete

 // get viewport complete

 // submit viewport complete

 // tessellation flush complete

 // set render target size complete

 // set render target complete

 bool result=false;__try {result=
  !bovr::native_guard::compare(reinterpret_cast<void*>(base+0x2d2100), 3018, "2e0e1206c51edd57ed6f5253efeadf7e418290f6d3803240f95065bea74bbc32")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x2d1d60), 369, "22dacfed7ad05d39530f2f8f9833447c8212146dccbd7a8eb5eed694a4f26a46")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x2d1800), 178, "9d8aaf2acef5b1b644adecd46275dac8d05aedd2443b84de4ccef6b81fe37c0a")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x334280), 536, "4cd257f58a1f2a5dd98224cde9fdc3367e0f1c7a755a3a90deae622cf6887644")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x331270), 194, "bd0cff76cec6288a9b44220b7c7390aa3d2e682899054aeabc3704ed1db0759f")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x32d620), 71, "6498f4a12b49c43b99c976cd685f4260ad721fedeba581a034d64e02aa8ee2bf")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x330700), 2802, "a6a4a5afa473b9626504e0d8b55e458c63950ec8cd91667ca6e50e83fead45b5")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x2ec310), 536, "89e376a9c955a4a78effdd73c8f45ac6931e9a06dbbc9b73f59a09760e1a7ecf")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x2ec730), 694, "17b352fa302ac7fc57d72736c6e2cfb94947a949ebe940d01cb2e47dc0423b97")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x2e4b90), 373, "b4cb6248f1fd204010257742322a19ffde1556f9729d593fbf884a49c1709874")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x2e4090), 468, "e817f199ddc20cd039a012da02574486338d709ae020625a6402ef10b2fc9e51")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x2e4ec0), 760, "681866d4abea332376fdc889214cb468485916a8fffa46e0900614ef71fe5122")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x31eb30), 105, "544406dae2617c2074751461acdf4bca757e86cb8d65ac51f8271e264f9987a0")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x31e9d0), 343, "f6879509e07653c2be43cd359976103cee7455646705d95a0e9d4875525bc3db")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x325f60), 220, "f9ad895bddca2da17a9031528ca7c0d96fdb5ac8a5c9514d51b3285d8128019a")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x326040), 152, "932dad9e43ade9eb2225a36c46bce48f4663bdf780f03f7a81a60ec37e444aa4")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x30b680), 256, "effa4142d130417fec59ac04dda58874012cb35d7c60a99511fdd1969c36f2be")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x3265e0), 103, "7d2909ced9d26c4e7ff78d61c50bc7fe5af5cc74ccf74dd2052fdaabb9f06da9")
  &&!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x326650), 119, "bc4acb059f0b3aa9502374353b5e58028fd551703b3041011f1c79075d565798")
 ;}__except(EXCEPTION_EXECUTE_HANDLER){result=false;}SetLastError(error);return result;
}
