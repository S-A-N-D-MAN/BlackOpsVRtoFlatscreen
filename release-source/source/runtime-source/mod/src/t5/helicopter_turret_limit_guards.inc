// SPDX-License-Identifier: GPL-3.0-only
// Retail instruction contracts: RVA, byte count and SHA-256 only.
#if defined(BOVR_TEST_NATIVE_GUARDS)
inline constexpr bovr::native_guard::Span limit_caller_guard{0x40c050,2114,"a058fb13c4281fa503ee51168abdc918a226643b43ab8c81ff0f7d039e89907e"};
inline constexpr bovr::native_guard::Span limit_callee_guard{0x290950,229,"7ae035ef862a151fe1714c5ba3c2b73c765ee3fda11d2f1f64b5a839f89987da"};
inline constexpr bovr::native_guard::Span limit_normalize_guard{0x1f8370,56,"d4817aa5497628e7c77e6b606107042bbba3130888c5f47a375e6179be789fbb"};
#else
inline constexpr bovr::native_guard::Span limit_caller_guard{0x40c050,2114,"94516c1a28aefdbc782d0a82c8c193f83a9f8211e0476521152a3eebeb88226e"};
inline constexpr bovr::native_guard::Span limit_callee_guard{0x290950,229,"83642e3baed0cd267d75bed45c2e7463e3fff5e509d11bec0c76cceb3ad39078"};
inline constexpr bovr::native_guard::Span limit_normalize_guard{0x1f8370,56,"5b35f55978301686ea5ac73100b829ca8450e6ab4635f48b95a347b0d0980003"};
#endif
