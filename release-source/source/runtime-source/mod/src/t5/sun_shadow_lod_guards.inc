// SPDX-License-Identifier: GPL-3.0-only
// Retail instruction contracts: RVA, byte count and SHA-256 only.
#if defined(BOVR_TEST_NATIVE_GUARDS)
inline constexpr bovr::native_guard::Span callee_guard{0x329b10,909,"482a8032b73d2d473c5298e88b4c0c7c31d716816338949d810e595bfabc0109"};
inline constexpr bovr::native_guard::Span calls_guard{0x2c7436,39,"390310d3fd5cdbfef5dffd3791d5934b9b514143e1a3283c0f2e24cec7cf7a1a"};
inline constexpr bovr::native_guard::Span updater_localclient_guard{0x2c8da6,21,"c90232586b801f9558a76f2f963eccd831d9fe6775e4c8f1446b2331aa2132f2"};
inline constexpr bovr::native_guard::Span spot_callee_guard{0x329ea0,782,"e16e5cdde33bbb23c75025c7b38fc122bb5e344d82b5d1a160451413fc116d90"};
#else
inline constexpr bovr::native_guard::Span callee_guard{0x329b10,909,"3faa35cdc660d502e08dae497d933eafb225bc9e17bf87b36c9ca8ae666ab17e"};
inline constexpr bovr::native_guard::Span calls_guard{0x2c7436,39,"de41e02694fb7286f904369927cda9ea363143b175fd1c1f20a942f9c4fd94ae"};
inline constexpr bovr::native_guard::Span updater_localclient_guard{0x2c8da6,21,"e1037a50be1a7b917ceebdb6d80f38dfa8e76b89037e1b2d8e0cfc7c82c48196"};
inline constexpr bovr::native_guard::Span spot_callee_guard{0x329ea0,782,"41cee745f355c4742c951f0fbab738013f036f50b97601c4d65cecaac3a5932e"};
#endif
// Includes the caller's sole consumed-argument push and its add esp,4 discard.
inline constexpr bovr::native_guard::Span spot_call_guard{0x3355c9,17,"1971304e59fb4cc57ec83872d37958f7359aa05dd6ea35130dbbb8545cae3150"};
