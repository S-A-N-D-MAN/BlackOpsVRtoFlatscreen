// SPDX-License-Identifier: GPL-3.0-only
#include "t5/primary_weapon_tracking.hpp"
#include <cstdio>
#include <initializer_list>

namespace p=bovr::t5::primary_weapon_tracking;
namespace {
unsigned checks{},failures{};
void check(bool value,const char* label) {++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
constexpr p::View view{0,0,10,false,false,false};
constexpr p::Player ordinary{0,100,0,0,0,0};
// R44 Cuba enabled pistol: linked, unfrozen, ordinary first-person camera.
// The PM/entity bits below are observed values, not a pistol/map allowlist.
constexpr p::Player cuba{1,100,0x1800000,0x8,0x80002,0};
constexpr p::Server server(const p::Player& player,std::uint32_t link) {
    return {player,true,100,2,0,0,link};
}
void ordinary_unchanged() {
    for(int mode:{0,2}) {
        auto v=view;v.camera_mode=mode;
        check(p::client_mode(ordinary,v)==p::Mode::ordinary,"ordinary first-person and player-shadow cameras stay accepted");
    }
    auto prior=ordinary;prior.pm_flags=0x400;
    check(p::client_mode(prior,view)==p::Mode::ordinary,"existing ordinary PM0x400 policy is not silently changed");
    prior=ordinary;prior.weapon_flags=0x100;
    check(p::client_mode(prior,view)==p::Mode::ordinary&&
          p::server_mode(server(prior,0))==p::Mode::ordinary,
          "ordinary primary reacquires after a script leaves offhand disabled");
    prior.weapon_flags=0x2000100;
    check(p::client_mode(prior,view)==p::Mode::ordinary&&
          p::server_mode(server(prior,0))==p::Mode::ordinary,
          "observed post-breach state remains an ordinary tracked primary");
    prior.weapon_flags=0x180;
    check(p::client_mode(prior,view)==p::Mode::rejected,
          "weapon-update disable still rejects the ordinary primary");
    check(p::server_mode(server(ordinary,0))==p::Mode::ordinary,"ordinary server requires no entity link");
    check(p::server_mode(server(ordinary,0x6500000))==p::Mode::rejected,"ordinary server cannot borrow linked identity");
}
void linked_acceptance() {
    for(auto flags:{0U,8U,0x100U,0x108U}) {
        auto c=cuba;c.weapon_flags=flags;
        for(int camera:{0,2}) {
            auto v=view;v.camera_mode=camera;
            const auto a=p::client_mode(c,v),b=p::server_mode(server(c,0x6500000));
            check(a==p::Mode::linked&&b==p::Mode::linked&&p::paired_mode(a,b)==p::Mode::linked,
                  "linked enabled primary permits independent offhand-disable and both native first-person camera values");
        }
    }
    auto generic=cuba;generic.pm_flags=generic.entity_flags=0;
    check(p::client_mode(generic,view)==p::Mode::linked,"linked eligibility does not depend on incidental Cuba flags or a weapon name");
    generic.weapon_flags=0x2000300;
    check(p::client_mode(generic,view)==p::Mode::linked&&
          p::server_mode(server(generic,0x6500000))==p::Mode::linked,
          "observed breach handoff state remains a linked tracked primary");
    check(p::paired_mode(p::Mode::ordinary,p::Mode::linked)==p::Mode::rejected&&
          p::paired_mode(p::Mode::linked,p::Mode::ordinary)==p::Mode::rejected,
          "client and server PM-mode handoff cannot form an admitted pair");
    check(p::paired_mode(p::Mode::rejected,p::Mode::rejected)==p::Mode::rejected,
          "matching rejection is never eligibility");
}
void refusals() {
    for(unsigned n=0;n<15;++n) {
        auto c=cuba;
        switch(n) {
        case 0:c.health=0;break;case 1:c.health=-1;break;case 2:c.pm_type=-1;break;
        case 3:c.pm_type=2;break;case 4:c.pm_type=10;break;
        case 5:c.pm_flags|=0x800;break;case 6:c.pm_flags|=0x400;break;
        case 7:c.weapon_flags|=0x80;break;case 8:c.weapon_flags|=0x180;break;
        case 9:c.entity_flags|=0x100;break;case 10:c.entity_flags|=0x200;break;
        case 11:c.entity_flags|=0x4000;break;case 12:c.entity_flags2|=0x40000;break;
        case 13:c.entity_flags2|=0x40000000;break;case 14:c.pm_type=4;break;
        }
        check(p::client_mode(c,view)==p::Mode::rejected&&
              p::server_mode(server(c,0x6500000))==p::Mode::rejected,
              "linked client and server retain death freeze weapon-update disabled mounted and remote exclusions");
    }
    for(unsigned n=0;n<8;++n) {
        auto v=view;
        switch(n) {
        case 0:v.camera_mode=1;break;case 1:v.camera_mode=3;break;
        case 2:v.camera_mode=-1;break;case 3:v.native_override=true;break;
        case 4:v.cubemap=true;break;case 5:v.draw_suppressed=true;break;
        case 6:v.key_catcher=16;break;case 7:v.connection_state=8;break;
        }
        check(p::client_mode(cuba,v)==p::Mode::rejected&&p::client_mode(ordinary,v)==p::Mode::rejected,
              "both modes retain existing camera override menu and connection exclusions");
    }
    for(unsigned n=0;n<10;++n) {
        auto s=server(cuba,0x6500000);
        switch(n) {
        case 0:s.local_identity=false;break;case 1:s.entity_health=0;break;
        case 2:s.connected=1;break;case 3:s.session=1;break;
        case 4:s.flags=1;break;case 5:s.flags=2;break;case 6:s.flags=4;break;case 7:s.flags=0x10;break;
        case 8:s.link=0;break;case 9:s.link=0x6500001;break;
        }
        check(p::server_mode(s)==p::Mode::rejected,"linked server requires current local identity session health and bounded link");
    }
    for(auto link:{0U,1U,0xfffcU,0xffffU,0x6500001U,0xffffffffU})
        check(!p::bounded_link(link),"invalid copied link cannot become an eligible linked scope");
}
void scope_identity() {
    check(p::same_link_mode(p::Mode::ordinary,0,p::Mode::ordinary,0),"ordinary stable scope retains no link");
    check(p::same_link_mode(p::Mode::linked,0x6500000,p::Mode::linked,0x6500000),"linked scope requires identical copied link identity");
    check(!p::same_link_mode(p::Mode::linked,0x6500000,p::Mode::linked,0x6500100),"parent-link identity replacement retires the occurrence");
    check(!p::same_link_mode(p::Mode::ordinary,0,p::Mode::linked,0x6500000)&&
          !p::same_link_mode(p::Mode::linked,0x6500000,p::Mode::ordinary,0),"both directions of PM transition require a fresh scope");
    check(!p::same_link_mode(p::Mode::linked,0,p::Mode::linked,0)&&
          !p::same_link_mode(p::Mode::ordinary,0x6500000,p::Mode::ordinary,0x6500000)&&
          !p::same_link_mode(p::Mode::rejected,0,p::Mode::rejected,0),"same invalid mode or link never counts as stable eligibility");
}
}
int main() {
    ordinary_unchanged();linked_acceptance();refusals();scope_identity();
    std::printf("primary_weapon_tracking_checks=%u failures=%u native_calls=0 native_writes=0\n",checks,failures);
    return failures?1:0;
}
