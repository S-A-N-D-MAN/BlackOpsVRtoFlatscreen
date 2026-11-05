// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <windows.h>
#include <bcrypt.h>
#include <cstddef>
#include <cstdint>
#include <limits>

#if defined(_MSC_VER)
#pragma comment(lib, "bcrypt.lib")
#endif

namespace bovr::native_guard {

// A retail-code guard retains only the address, byte count and SHA-256.  The
// executable bytes are read from the already loaded image and are never copied
// into the mod's source or binary.
struct Span final {
    std::uintptr_t address{};
    std::size_t size{};
    const char* sha256{};
};

namespace detail {
inline unsigned hex_nibble(const char value) noexcept {
    if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<unsigned>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<unsigned>(value - 'A' + 10);
    return 0xffu;
}

inline bool matches_unchecked(const void* data, const std::size_t size,
                              const char* expected) noexcept {
    if (!data || !expected || size > (std::numeric_limits<ULONG>::max)()) return false;
    unsigned char actual[32]{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                   static_cast<PUCHAR>(const_cast<void*>(data)),
                   static_cast<ULONG>(size), actual,
                   static_cast<ULONG>(sizeof(actual))) < 0) return false;
    unsigned difference{};
    for (std::size_t index = 0; index < sizeof(actual); ++index) {
        const unsigned high = hex_nibble(expected[index * 2]);
        const unsigned low = hex_nibble(expected[index * 2 + 1]);
        if (high > 0x0fu || low > 0x0fu) return false;
        difference |= actual[index] ^ static_cast<unsigned char>((high << 4) | low);
    }
    return expected[64] == '\0' && difference == 0;
}
} // namespace detail

inline bool matches(const void* data, const std::size_t size,
                    const char* expected_sha256) noexcept {
#if defined(_MSC_VER)
    __try {
        return detail::matches_unchecked(data, size, expected_sha256);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    return detail::matches_unchecked(data, size, expected_sha256);
#endif
}

// Drop-in semantic replacement for memcmp(..., captured_bytes, size) at native
// guard sites: zero means the loaded span has the pinned digest.  Any malformed
// digest, crypto failure or unreadable memory reports a mismatch.
inline int compare(const void* data, const std::size_t size,
                   const char* expected_sha256) noexcept {
    return matches(data, size, expected_sha256) ? 0 : 1;
}

inline bool matches(const std::uintptr_t base, const Span& span) noexcept {
    if (!base || span.address > (std::numeric_limits<std::uintptr_t>::max)() - base)
        return false;
    return matches(reinterpret_cast<const void*>(base + span.address), span.size, span.sha256);
}

inline bool matches_absolute(const Span& span) noexcept {
    return matches(reinterpret_cast<const void*>(span.address), span.size, span.sha256);
}

} // namespace bovr::native_guard
