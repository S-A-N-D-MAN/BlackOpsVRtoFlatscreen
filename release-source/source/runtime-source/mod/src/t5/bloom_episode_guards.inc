// SPDX-License-Identifier: GPL-3.0-only
// Pinned full C11c role/predicate/pass-loop guards. No instruction writes.
bool validate_bloom_episode_guards(HMODULE module) noexcept {
 const auto error=GetLastError();const auto base=reinterpret_cast<std::uintptr_t>(module);
 if(base!=0x400000||module!=GetModuleHandleW(nullptr)){SetLastError(error);return false;}



 bool ok=false;__try {ok=!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x330700), 2802, "a6a4a5afa473b9626504e0d8b55e458c63950ec8cd91667ca6e50e83fead45b5")&&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x32dd10), 7371, "dc595536ae0945d258210853370bdcef0676a72b11f077ae8f0b2171dc9bac03")&&!bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x30b430), 469, "9fb0de98d76f58ea422a7c3967684345c8d0fd0db22d36f07b0558a0484c3066");}__except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
 SetLastError(error);return ok;
}
