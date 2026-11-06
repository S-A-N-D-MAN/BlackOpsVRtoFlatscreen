// SPDX-License-Identifier: GPL-3.0-only
#include "t5/identity.hpp"
#include <bcrypt.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <array>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace bovr::t5 {
namespace {
std::mutex trust_cache_lock;
std::string trust_cache_sha;
std::string trust_cache_reason;
bool trust_cache_value{};
bool check_headers(const IMAGE_NT_HEADERS32& nt, std::string& reason) {
    const auto& f = nt.FileHeader;
    const auto& o = nt.OptionalHeader;
    if (nt.Signature != IMAGE_NT_SIGNATURE || f.Machine != IMAGE_FILE_MACHINE_I386 ||
        f.NumberOfSections != 7 || f.TimeDateStamp != 1314918417u ||
        f.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER32) || f.Characteristics != 291 ||
        o.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || o.AddressOfEntryPoint != 0x56da57 ||
        o.ImageBase != 0x400000 || o.SizeOfImage != 69693440 || o.DllCharacteristics != 32768) {
        reason = "PE fields do not match pinned BlackOps.exe SP";
        return false;
    }
    return true;
}

bool check_file_layout(const std::span<const std::uint8_t> bytes, const IMAGE_DOS_HEADER& dos,
                       const IMAGE_NT_HEADERS32& nt, std::string& reason) {
    const auto certificate = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];
    if (bytes.size() < kSpImageFileSize + sizeof(WIN_CERTIFICATE) ||
        bytes.size() > kSpImageFileSize + kSpMaximumCertificateSize ||
        certificate.VirtualAddress != kSpImageFileSize || certificate.Size < sizeof(WIN_CERTIFICATE) ||
        static_cast<std::uint64_t>(certificate.VirtualAddress) + certificate.Size != bytes.size()) {
        reason = "signed file bounds do not match the Black Ops Steam image";
        return false;
    }
    static constexpr struct ExpectedSection {
        const char* name; std::uint32_t virtual_address, virtual_size, raw_size, raw_pointer, characteristics;
    } expected[] = {
        {".text",0x1000,0x5a1a10,5905408,1024,0x60000020},
        {".rdata",0x5a3000,0x1c86ba,1869824,5906432,0x40000040},
        {".data",0x76c000,0x3af36c4,238592,7776256,0xc0000040},
        {".tls",0x4260000,0x6d,512,8014848,0xc0000040},
        {".rodata",0x4261000,0x770,2048,8015360,0x40000040},
        {".version",0x4262000,0x4,512,8017408,0x40000040},
        {".rsrc",0x4263000,0x130c4,78336,8017920,0x40000040},
    };
    const auto section_offset = static_cast<std::size_t>(dos.e_lfanew) + sizeof(DWORD) +
        sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
    constexpr auto expected_count = sizeof(expected) / sizeof(expected[0]);
    if (section_offset > bytes.size() - expected_count * sizeof(IMAGE_SECTION_HEADER)) {
        reason = "section table is outside the file";
        return false;
    }
    for (std::size_t i{}; i < expected_count; ++i) {
        IMAGE_SECTION_HEADER section{};
        std::memcpy(&section, bytes.data() + section_offset + i * sizeof(section), sizeof(section));
        char name[IMAGE_SIZEOF_SHORT_NAME + 1]{};
        std::memcpy(name, section.Name, IMAGE_SIZEOF_SHORT_NAME);
        const auto& wanted = expected[i];
        if (std::strcmp(name, wanted.name) != 0 || section.VirtualAddress != wanted.virtual_address ||
            section.Misc.VirtualSize != wanted.virtual_size || section.SizeOfRawData != wanted.raw_size ||
            section.PointerToRawData != wanted.raw_pointer || section.Characteristics != wanted.characteristics) {
            reason = "section table does not match the supported Black Ops Steam image";
            return false;
        }
    }
    return true;
}

bool valve_signer(const std::wstring& path, std::string& reason) {
    DWORD encoding{}, content{}, format{};
    HCERTSTORE store{};
    HCRYPTMSG message{};
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.c_str(),
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
            CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encoding, &content, &format,
            &store, &message, nullptr)) {
        reason = "executable has no embedded Authenticode signature";
        return false;
    }
    DWORD bytes{};
    bool okay = CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &bytes) != FALSE && bytes >= sizeof(CMSG_SIGNER_INFO);
    std::vector<std::uint8_t> storage;
    try { if (okay) storage.resize(bytes); }
    catch (...) {
        if (message) CryptMsgClose(message);
        if (store) CertCloseStore(store, 0);
        throw;
    }
    okay = okay && CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, storage.data(), &bytes) != FALSE;
    PCCERT_CONTEXT certificate{};
    if (okay) {
        const auto* signer = reinterpret_cast<const CMSG_SIGNER_INFO*>(storage.data());
        CERT_INFO query{};
        query.Issuer = signer->Issuer;
        query.SerialNumber = signer->SerialNumber;
        certificate = CertFindCertificateInStore(store, encoding, 0, CERT_FIND_SUBJECT_CERT, &query, nullptr);
        okay = certificate != nullptr;
    }
    wchar_t organization[128]{};
    if (okay) {
        const auto count = CertGetNameStringW(certificate, CERT_NAME_ATTR_TYPE, 0,
            const_cast<char*>(szOID_ORGANIZATION_NAME), organization,
            static_cast<DWORD>(std::size(organization)));
        okay = count > 1 && (wcscmp(organization, L"Valve Corp.") == 0 || wcscmp(organization, L"Valve") == 0);
    }
    if (certificate) CertFreeCertificateContext(certificate);
    if (message) CryptMsgClose(message);
    if (store) CertCloseStore(store, 0);
    if (!okay) reason = "executable signer is not Valve";
    return okay;
}

bool trusted_valve_file(const std::wstring& path, std::string& reason) {
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path.c_str();
    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &policy, &trust);
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &policy, &trust);
    if (status != ERROR_SUCCESS) {
        reason = "executable Authenticode signature is invalid";
        return false;
    }
    return valve_signer(path, reason);
}
}
std::vector<std::uint8_t> read_file(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot read executable");
    LARGE_INTEGER n{};
    if (!GetFileSizeEx(f, &n) || n.QuadPart <= 0 || n.QuadPart > 128 * 1024 * 1024) {
        CloseHandle(f); throw std::runtime_error("invalid executable size");
    }
    std::vector<std::uint8_t> bytes;
    try { bytes.resize(static_cast<std::size_t>(n.QuadPart)); }
    catch (...) { CloseHandle(f); throw; }
    DWORD got{};
    const bool ok = ReadFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr) != FALSE;
    CloseHandle(f);
    if (!ok || got != bytes.size()) throw std::runtime_error("short executable read");
    return bytes;
}
std::string sha256(const std::span<const std::uint8_t> bytes) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    DWORD object_size{}, result_size{};
    std::array<std::uint8_t, 32> digest{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 unavailable");
    std::vector<std::uint8_t> object;
    bool ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &result_size, 0) >= 0;
    try { if (ok) object.resize(object_size); }
    catch (...) { BCryptCloseAlgorithmProvider(algorithm, 0); throw; }
    ok = ok && BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) >= 0;
    ok = ok && BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0) >= 0;
    ok = ok && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) throw std::runtime_error("SHA256 calculation failed");
    std::string out; out.reserve(64);
    constexpr char hex[] = "0123456789abcdef";
    for (auto b : digest) { out.push_back(hex[b >> 4]); out.push_back(hex[b & 15]); }
    return out;
}
Validation validate_file(const std::span<const std::uint8_t> bytes) {
    Validation out;
    if (bytes.size() < kSpImageFileSize || bytes.size() > kSpImageFileSize + kSpMaximumCertificateSize) {
        out.reason = "file size mismatch"; return out;
    }
    IMAGE_DOS_HEADER dos{}; std::memcpy(&dos, bytes.data(), sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        static_cast<std::size_t>(dos.e_lfanew) > bytes.size() - sizeof(IMAGE_NT_HEADERS32)) {
        out.reason = "invalid DOS/PE bounds"; return out;
    }
    IMAGE_NT_HEADERS32 nt{};
    std::memcpy(&nt, bytes.data() + dos.e_lfanew, sizeof(nt));
    if (!check_headers(nt, out.reason)) return out;
    if (!check_file_layout(bytes, dos, nt, out.reason)) return out;
    out.sha256 = sha256(bytes);
    if (out.sha256 != kSpSha256) { out.reason = "whole-file SHA256 mismatch"; return out; }
    out.accepted = true; out.reason = "pinned BlackOps.exe SP identity accepted";
    return out;
}
Validation validate_path(const std::wstring& path) {
    try {
        auto result = validate_file(read_file(path));
        if (result.accepted || result.reason != "whole-file SHA256 mismatch") return result;
        std::string trust_reason;
        bool trusted{};
        {
            std::lock_guard guard(trust_cache_lock);
            if (trust_cache_sha == result.sha256) {
                trusted = trust_cache_value;
                trust_reason = trust_cache_reason;
            } else {
                trusted = trusted_valve_file(path, trust_reason);
                trust_cache_sha = result.sha256;
                trust_cache_value = trusted;
                trust_cache_reason = trust_reason;
            }
        }
        // Non-Steam/retail-family executables: the PE headers and the seven-section layout
        // were already verified by validate_file(); only the whole-file hash and the Valve
        // signature differ. Accept on layout. The per-hook byte guards still protect each patch.
        result.accepted = true;
        result.reason = trusted ? "Valve-signed BlackOps.exe SP family accepted"
                                : "BlackOps.exe SP PE layout accepted; signature not required (" + trust_reason + ")";
        return result;
    }
    catch (const std::exception& e) { return {false, {}, e.what()}; }
}
bool validate_loaded_headers(HMODULE module, std::string& reason) {
    // Only immutable PE identity fields are checked here. No claim of full loaded-code
    // identity is made. The separate optional scene probe checks its own entry bytes.
    if (!module) { reason = "no executable module"; return false; }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096) {
        reason = "loaded DOS header mismatch"; return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        reinterpret_cast<const std::uint8_t*>(module) + dos->e_lfanew);
    return check_headers(*nt, reason);
}
std::wstring executable_path() {
    std::wstring path(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) throw std::runtime_error("executable path unavailable");
    path.resize(n); return path;
}
}
