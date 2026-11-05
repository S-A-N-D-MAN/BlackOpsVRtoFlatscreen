// SPDX-License-Identifier: GPL-3.0-only
// Retail instruction contracts: RVA, byte count and SHA-256 only.
#if defined(BOVR_TEST_NATIVE_GUARDS)
inline constexpr bovr::native_guard::Span trace_caller_guard{0x4113cf,198,"2075dd73b3a675f9e1ce0bc40af44138d3fc1fb44dd3a6f5992de933afd140c8"};
inline constexpr bovr::native_guard::Span trace_callee_guard{0x1666c0,103,"37446575700829a11278ad3a550f244f45d5ae4fe1552778fa4f041f9eaeecf6"};
#else
inline constexpr bovr::native_guard::Span trace_caller_guard{0x4113cf,198,"2194aa3c1b46db13fd64c2419f561d8ef0b43a449f59ee0b0a7055a597379269"};
inline constexpr bovr::native_guard::Span trace_callee_guard{0x1666c0,103,"30994064a5a1cebdcc221b17332b740430f92f1dbaeef8ef2b4c56a4ff00ef44"};
#endif
