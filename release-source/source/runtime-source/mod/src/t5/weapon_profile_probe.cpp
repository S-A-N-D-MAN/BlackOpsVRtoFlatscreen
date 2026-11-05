// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/weapon_profile_probe.hpp"
#include "t5/profile_record_output.hpp"
#include "t5/identity.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <array>
#include <cstring>

#if !defined(_M_IX86)
#error Native profile observation requires the pinned x86 ABI.
#endif
namespace bovr::t5 {
namespace {
std::atomic<bool> profile_enabled{};
std::atomic_flag running=ATOMIC_FLAG_INIT;
std::wstring output_directory;
std::uint64_t last_attempt{},attempts{};
constexpr std::uint64_t interval_ms=1000,max_attempts=64;
bool precommand(void*,PreCommandSnapshot* out) noexcept {return read_pre_command_snapshot(out);}
std::uint64_t now(void*) noexcept {return GetTickCount64();}
bool guards() noexcept {
    // Exact unaffected consumers/getters underpin the copied SP field layout.
    __try {
        return !bovr::native_guard::compare(reinterpret_cast<void*>(0x5f4600), 7, "70d1bff2376999b64365e947e0c0a8bab942f981962111fc8728487516761fc8")&&
          !bovr::native_guard::compare(reinterpret_cast<void*>(0x415910), 9, "3f8af0efcf6921d390e61677ea8466f9870d1bde974aacb7f000107d493cf17a")&&
          !bovr::native_guard::compare(reinterpret_cast<void*>(0x656a70), 8, "20df7e34a72812daee9c9435e05f29337d0be30e3cc8d4844f8aeb4d4b9e55a4")&&
          !bovr::native_guard::compare(reinterpret_cast<void*>(0x687530), 30, "d1c2beabed9679d26c574b5d6f6f7ec22df27a0c15874f2e0b121762b15969ca")&&
          !bovr::native_guard::compare(reinterpret_cast<void*>(0x45b02e), 8, "0b97f1aca1caa08189a4918c6314c29f8e364c02933b22e70be9518343437055")&&
          !bovr::native_guard::compare(reinterpret_cast<void*>(0x45b049), 9, "f9bf0134506796dd34dfb21a7d8220d1d688d113b9b40ea690996be88a1c8f2d")&&
          !bovr::native_guard::compare(reinterpret_cast<void*>(0x869fa1), 8, "87ad114174d2d3ec02ce55ac29e7880f1ea2e8a90d52938784e030143736e34f")&&
          !bovr::native_guard::compare(reinterpret_cast<void*>(0x869ffb), 4, "810dfd89c46198f511a41d87f73cbd18d124f11fc21288a3a3c5e5283f2b8652");
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool directory_from_environment(std::wstring* out) {
    wchar_t path[32768]{};const auto count=GetEnvironmentVariableW(L"BOVR_PROFILE_OUTPUT_DIR",path,32768);
    // Explicit local absolute case directory; do not create or redirect it.
    if(count<3||count>=32768||path[1]!=L':'||(path[2]!=L'\\'&&path[2]!=L'/'))return false;
    wchar_t full[32768]{};const auto n=GetFullPathNameW(path,32768,full,nullptr);
    if(!n||n>=32768)return false;
    const auto attr=GetFileAttributesW(full);if(attr==INVALID_FILE_ATTRIBUTES||!(attr&FILE_ATTRIBUTE_DIRECTORY))return false;
    *out=full;return true;
}
}
bool configure_weapon_profile_probe(HMODULE image,bool hooks) noexcept {
    const auto error=GetLastError();bool ok=false;
    try {
        wchar_t flag[8]{};
        if(!profile_enabled.load(std::memory_order_acquire)&&GetEnvironmentVariableW(L"BOVR_WEAPON_PROFILE_PROBE",flag,8)==1&&flag[0]==L'1'&&hooks&&
           image==GetModuleHandleW(nullptr)&&reinterpret_cast<std::uintptr_t>(image)==0x400000) {
            std::wstring directory;std::string reason;
            if(directory_from_environment(&directory)&&validate_path(executable_path()).accepted&&validate_loaded_headers(image,reason)&&guards()) {
                output_directory=std::move(directory);profile_enabled.store(true,std::memory_order_release);ok=true;
                trace("t5_weapon_profile_configured","passive=1 interval_ms=1000 max_attempts=64 extra_patches=0 native_calls=0 writes=0 canonical_schema=1 assembly_extension=diagnostic_only");
            }
        }
    }catch(...){}
    SetLastError(error);return ok;
}
bool weapon_profile_probe_enabled() noexcept {return profile_enabled.load(std::memory_order_acquire);}
void observe_weapon_profile_after(const NativeWeaponContext& c) noexcept {
    const auto error=GetLastError();
    if(!weapon_profile_probe_enabled()||!profile_detail::supported_context(c,GetCurrentThreadId())||running.test_and_set(std::memory_order_acquire)){SetLastError(error);return;}
    // Own-only exception boundary: protected memcpy, pure values and file I/O.
    try {
        const auto begin=GetTickCount64();
        if(attempts<max_attempts&&(!last_attempt||begin<last_attempt||begin-last_attempt>=interval_ms)) {
            last_attempt=begin;const auto observation=++attempts;profile_detail::ProfileRecord record;
            const profile_detail::ReadHost host{nullptr,profile_detail::protected_profile_copy,now,precommand};
            const auto status=profile_detail::read_profile(host,c,GetCurrentThreadId(),begin,&record);
            profile_detail::RecordOutput files{};
            if(status==profile_detail::ReadStatus::accepted)files=profile_detail::write_record(output_directory,GetCurrentProcessId(),observation,record);
            trace("t5_weapon_profile","observation=%llu status=%u identity_status=%u evidence_complete=%u models=%zu epoch=%llu borrow=%llu native_time=%d weapon=%u selected_model=%u selected_bone=%u asset_sha256=%s ordered_models_sha256=%s pointer_retention=0 allocation_generation_verified=0 grip_profile_fitted=0 native_calls=0 writes=0",
                observation,static_cast<unsigned>(status),static_cast<unsigned>(record.identity_status),files.complete,record.copies.after.size(),
                record.copies.after_stamp.native_epoch,c.call_serial,c.native_time,c.weapon_index,record.canonical.selected_model,record.canonical.selected_local_bone,
                files.selected_asset_sha256.c_str(),files.ordered_models_sha256.c_str());
        }
    }catch(...){trace("t5_weapon_profile_error","own_evidence_exception=1 native_calls=0 writes=0");}
    running.clear(std::memory_order_release);SetLastError(error);
}
}
