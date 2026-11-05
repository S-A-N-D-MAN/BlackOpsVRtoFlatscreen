// SPDX-License-Identifier: GPL-3.0-only
namespace {
struct ClientBarrelLinkCopy {
    primary_weapon_tracking::Player predicted{};
    primary_weapon_tracking::View view{};
    primary_weapon_tracking::Server server{};
    std::uintptr_t client{},clients{},variant{},definition{};
    unsigned predicted_weapon{},server_weapon{},entity_weapon{};
    friend bool operator==(const ClientBarrelLinkCopy&,const ClientBarrelLinkCopy&)=default;
};
ClientBarrelLinkCopy copy_client_barrel_link(std::uintptr_t cg,unsigned weapon) {
    using namespace primary_weapon_tracking;
    ClientBarrelLinkCopy s{};
    const auto player=[](std::uintptr_t ps) {
        return Player{load<int>(ps+4),load<int>(ps+0x1c4),load<unsigned>(ps+0xc),
            load<unsigned>(ps+0x10),load<unsigned>(ps+0xe0),load<unsigned>(ps+0xe4)};
    };
    constexpr std::uintptr_t entity=0x1a796f8;
    const auto ps=cg+0x8a364;
    s.predicted=player(ps);
    s.view={load<int>(cg+0x8a360),load<int>(0x2910160),load<int>(0x2910164),
        load<int>(0x2911e2c)!=0,load<int>(cg+0xc)!=0,load<int>(cg+0xa9d44)!=0};
    s.client=load<std::uintptr_t>(entity+0x13c);s.clients=load<std::uintptr_t>(0x1c03140);
    if(!s.client||s.client!=s.clients)return s;
    s.server={player(s.client),load<int>(entity)==0&&!load<unsigned char>(s.client+0x130)&&
        !load<unsigned char>(ps+0x130),load<int>(entity+0x184),load<int>(s.client+0x1a2c),
        load<int>(s.client+0x1a18),load<unsigned>(s.client+0x1c0c),load<unsigned>(entity+0x298)};
    s.predicted_weapon=load<unsigned char>(ps+0x144);s.server_weapon=load<unsigned char>(s.client+0x144);
    s.entity_weapon=load<unsigned char>(entity+0xd4);
    s.variant=load<std::uintptr_t>(0xbe19a8+weapon*4);
    if(s.variant)s.definition=load<std::uintptr_t>(s.variant+8);
    return s;
}
bool current_client_barrel_link(const NativeWeaponContext& c,std::uintptr_t cg) {
    using namespace primary_weapon_tracking;
    const auto first=copy_client_barrel_link(cg,c.weapon_index);
    if(paired_mode(client_mode(first.predicted,first.view),server_mode(first.server))!=Mode::linked||
       first.server.link!=c.server_link_identity||first.predicted_weapon!=c.weapon_index||
       first.server_weapon!=c.weapon_index||first.entity_weapon!=c.weapon_index||
       !first.variant||first.variant!=c.variant_identity||!first.definition||first.definition!=c.definition_identity)return false;
    return first==copy_client_barrel_link(cg,c.weapon_index)&&load<std::uintptr_t>(0x2ff5354)==cg;
}
bool write_client_barrel_angles(const NativeWeaponContext& c,anim::Vec forward) noexcept {
    // Called only after this invocation's exact native placement is verified.
    // The original animation regenerates these two values on fallback. No new
    // call hook, view-angle change, server-field write or retained native pointer.
    client_aim::Angles angles{};
    if(!exact_profile_aim_enabled()||c.local_client||GetCurrentThreadId()!=c.native_thread||
       !c.valid||!primary_weapon_context(c)||!client_aim::from_forward(forward,&angles))return false;
    __try {
#if defined(BOVR_TEST_NATIVE_GUARDS)
        constexpr const char* pitch_guard="af5570f5a1810b7af78caf4bc70a660f0df51e42baf91d4de5b2328de0e83dfc";
        constexpr const char* yaw_guard=pitch_guard;
        constexpr std::uintptr_t guard_fixture_base=0x10000000;
#else
        constexpr const char* pitch_guard="42a62a3d66ac5b98b1c00ff0d403b61f8a36727964ecf6b7b513b999179c8319";
        constexpr const char* yaw_guard="4e6a228817bfafd1be4d82b97b72a9e10b25884446cea328f81b052f6006d907";
        constexpr std::uintptr_t guard_fixture_base=0;
#endif
        if(native_guard::compare(reinterpret_cast<void*>(guard_fixture_base+0x4816d8),8,pitch_guard)||
           native_guard::compare(reinterpret_cast<void*>(guard_fixture_base+0x4816e8),8,yaw_guard))return false;
        const auto cg=load<std::uintptr_t>(0x2ff5354);
        if(cg!=c.cg_identity||cg<0x10000||cg>UINT32_MAX-0xa9cec||
           load<unsigned char>(cg+0x8a364+0x144)!=c.weapon_index)return false;
        // The completed borrowed placement already proved the paired context.
        // Recheck linked ownership here, rather than treating any linked script
        // model as a local primary weapon. This changes client effects only.
        if(primary_weapon_mode(c)==primary_weapon_tracking::Mode::linked&&!current_client_barrel_link(c,cg))return false;
        const float before[2]{load<float>(cg+0xa9ce4),load<float>(cg+0xa9ce8)};
        if(!std::isfinite(before[0])||!std::isfinite(before[1]))return false;
        memcpy(reinterpret_cast<void*>(cg+0xa9ce4),&angles,sizeof(angles));
        static std::uint64_t count{};++count;
        if(count<=12||count%120==0)trace("t5_client_barrel_aim",
            "count=%llu command=%d native_time=%d weapon=%u previous=%.6f,%.6f selected=%.6f,%.6f forward=%.7f,%.7f,%.7f writes=8 viewangle_writes=0 new_hooks=0",
            count,c.command_number,c.native_time,c.weapon_index,before[0],before[1],angles.pitch,angles.yaw,forward.x,forward.y,forward.z);
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
