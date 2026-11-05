// SPDX-License-Identifier: GPL-3.0-only
// Retail contracts: RVA, byte count, SHA-256 only. Full pinned evidence:
// evidence/native-dynamic-scene-pins.json, exe SHA256 4e0cdb35...d12c03.
#if defined(BOVR_TEST_NATIVE_GUARDS)
inline constexpr bovr::native_guard::Span dynamic_guards[]{
    {0x2b22f0,531,"0df08da543aa95e9920584e7d4b38aa6060ea0960c1e8c2619ada4e94a987398"},
    {0x2bf820,533,"1cb8da180d9f02476b6ba26dbfa3d70d0b6845690f7199ca41bfa6b4b1f850c0"},
    {0x2b1790,252,"8e29186dec2a8709d0e307a9802bca1aa14510bd25c0be16dc0b645e4024dc9f"},
    {0x2b5040,344,"3d164f0340a5edcc8e538209b168f2c13028d78f1c3c20c4eeae7d341fab97e7"},
    {0x2c7391,9,"347fc96685f2019b6f797902b08f75a09ff1ae5ecfa19ef8ed23f08592a2f43c"}
};
#else
inline constexpr bovr::native_guard::Span dynamic_guards[]{
    {0x2b22f0,531,"042dbca8f16f49e9b5f98486dc1e553b4058f3903f552bdcae38b68e0dbba4ec"},
    {0x2bf820,533,"46dd3fdf9a168d4797df7d685a2c3e448b9c9ab21c19a3ed824ade93687ea063"},
    {0x2b1790,252,"55f5b389e888adf6630081878a6f3b7099202ce129ad59291264900814384f68"},
    {0x2b5040,344,"67a99391f75ae5467175cd445c5ccb25670a77215523ff1fdf7a1a7338c3b406"},
    {0x2c7391,9,"83eff39755ef7432043645445318a267f76ea09f00d23ae289d89d488a31912b"}
};
#endif
