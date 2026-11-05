// SPDX-License-Identifier: GPL-3.0-only
// Generated from exact disk and historical stock loaded text.
bool validate_auxiliary_history_guards(HMODULE module) noexcept {
 const auto error=GetLastError();const auto base=reinterpret_cast<std::uintptr_t>(module);
 if(base!=0x400000||module!=GetModuleHandleW(nullptr)){SetLastError(error);return false;}
 // whole surface resolve complete

 // set2D complete

 // viewport selection complete

 // 2D projection complete

 // viewport COM submission complete

 // tess flush viewport and draw complete

 bool result=false;__try {result=
  !bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x2e4ec0), 760, "681866d4abea332376fdc889214cb468485916a8fffa46e0900614ef71fe5122")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x31eb30), 105, "544406dae2617c2074751461acdf4bca757e86cb8d65ac51f8271e264f9987a0")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x325f60), 220, "f9ad895bddca2da17a9031528ca7c0d96fdb5ac8a5c9514d51b3285d8128019a")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x31e9d0), 343, "f6879509e07653c2be43cd359976103cee7455646705d95a0e9d4875525bc3db")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x326040), 152, "932dad9e43ade9eb2225a36c46bce48f4663bdf780f03f7a81a60ec37e444aa4")
  &&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x30b680), 256, "effa4142d130417fec59ac04dda58874012cb35d7c60a99511fdd1969c36f2be")
 ;}__except(EXCEPTION_EXECUTE_HANDLER){result=false;}SetLastError(error);return result;
}
