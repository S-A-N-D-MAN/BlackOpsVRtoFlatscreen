// SPDX-License-Identifier: GPL-3.0-only
#include "t5/cinematic_production.hpp"
#include "render/hud_commands.hpp"
#include <cstdio>
#include <functional>
#include <cstring>
using namespace bovr::t5;
namespace {
constexpr std::uintptr_t base=0x400000;
unsigned checks{},failures{};
void check(bool v,const char* why){++checks;if(!v){++failures;std::printf("FAIL %s\n",why);}}
CinematicProductionMarker marker(unsigned slot=0) {
    CinematicProductionMarker m{};m.valid=true;m.thread=17;m.serial=1;m.generation=2;m.observed_ms=1000;
    m.context.valid=true;m.context.connection_state=5;m.context.key_catchers=0;m.context.menu_count=126;m.context.page={0x1234,1,8};
    auto& n=m.native;n.buffer=base+(slot?0x38c6400:0x3740100);n.array=base+0x3737090+24*slot;
    n.bytes=0x10000000+slot*0x20000;n.frame=10;n.capacity=0x18000;n.configured_views=1;n.renderer_active=1;
    auto& c=m.movie;c.valid=true;c.image_base=base;c.thread_id=m.thread;c.observed_ms=m.observed_ms;c.initialized=true;
    c.bink_handle=0x1111;c.cinematic_material=0x2222;std::memcpy(c.current_name.data(),"cuba_load",10);
    c.active_image_frame=0;c.bound_texture_set=0;c.cg_fullscreen_known=c.cg_fullscreen=true;
    c.renderer_images={base+0x3a4cbcc+0x1a0,base+0x3a4cbcc+0x1d4,base+0x3a4cbcc+0x208};return m;
}
std::shared_ptr<HudCommandPublication> finish(CinematicProductionMarker& m) {
    ++m.serial;m.observed_ms+=10;m.movie.observed_ms=m.observed_ms;
    m.native.shared_commands=m.native.bytes+m.native.used;m.native.used+=12;m.native.last=static_cast<unsigned>(m.native.bytes+m.native.used-4);
    m.context.backend=m.native.buffer;m.context.commands=m.native.shared_commands;m.context.native_frame=m.native.frame;
    auto p=std::make_shared<HudCommandPublication>();p->native=m.native;p->bytes.resize(m.native.used);
    const auto offset=m.native.shared_commands-m.native.bytes;p->bytes[offset]=8;p->bytes[offset+2]=27;p->bytes[offset+8]=4;return p;
}
bool match(CinematicProductionEpoch& p,const CinematicProductionMarker& m) {
    CinematicProductionStamp out{};auto movie=m.movie;movie.thread_id=29;
    return p.match(m.context,movie,m.generation,m.observed_ms+1,&out);
}
}
int main(){
    using S=CinematicProductionStatus;
    for(unsigned slot=0;slot<2;++slot)for(DWORD thread:{17UL,29UL}){
        auto b=marker(slot);b.thread=b.movie.thread_id=thread;b.native.used=b.native.critical=16;
        CinematicProductionEpoch policy(base);
        check(policy.begin(b,true)==S::accepted,"exact native producer thread accepted without menu event authority");
        auto e=b;auto p=finish(e);check(policy.complete(e,base+0x3a18af,p)==S::accepted,"same invocation thread/frame/extent seals one movie interval");
        check(match(policy,e),"backend may consume copied production from either observed native thread");
        p.reset();check(match(policy,e),"publication ownership survives producer return");
        check(policy.complete(e,base+0x3a18af,{})==S::identity_changed&&!match(policy,e),"duplicate end retires old proof");
    }
    const std::function<void(CinematicProductionMarker&)> invalid[]{
        [](auto&v){v.valid=false;},[](auto&v){v.thread=0;},[](auto&v){v.serial=0;},[](auto&v){v.generation=0;},
        [](auto&v){v.observed_ms=0;},[](auto&v){v.context.valid=false;},[](auto&v){v.native.buffer+=4;},
        [](auto&v){v.native.array+=24;},[](auto&v){v.native.bytes|=1;},[](auto&v){v.native.bytes=0xffff0000;},
        [](auto&v){v.native.capacity=0;},[](auto&v){v.native.configured_views=5;},[](auto&v){v.native.used=1;},
        [](auto&v){v.native.critical=4;},[](auto&v){v.native.used=v.native.capacity;},[](auto&v){v.native.renderer_active=0;},
        [](auto&v){v.movie.image_base+=4;},[](auto&v){v.movie.thread_id++;},[](auto&v){v.movie.observed_ms=0;},
        [](auto&v){v.movie.observed_ms=1001;},[](auto&v){v.movie.observed_ms=749;},[](auto&v){v.movie.valid=false;},
        [](auto&v){v.movie.initialized=false;},[](auto&v){v.movie.finished=true;},[](auto&v){v.movie.bink_handle=0;},
        [](auto&v){v.movie.cinematic_material=0;},[](auto&v){v.movie.current_name.fill('x');},[](auto&v){v.movie.current_name[0]=0;},
        [](auto&v){v.movie.active_image_frame=-1;},[](auto&v){v.movie.bound_texture_set=2;},[](auto&v){v.movie.renderer_images[0]++;},
        [](auto&v){v.movie.cg_fullscreen=false;},[](auto&v){v.movie.cg_fullscreen_known=false;}
    };
    for(const auto& mutate:invalid){CinematicProductionEpoch policy(base);auto b=marker();mutate(b);
        check(policy.begin(b,true)!=S::accepted,"invalid native/movie begin cannot mint publication");auto e=marker();auto p=finish(e);
        check(policy.complete(e,base+0x3a18af,p)!=S::accepted,"no fallback completion after rejected begin");}
    const std::function<void(CinematicProductionMarker&)> changed[]{
        [](auto&v){v.thread=v.movie.thread_id=19;},[](auto&v){++v.generation;},[](auto&v){++v.native.frame;},
        [](auto&v){v.native.bytes+=4;},[](auto&v){v.native.view_count=1;},[](auto&v){++v.context.connection_state;},
        [](auto&v){v.context.key_catchers=16;},[](auto&v){++v.context.page.focused_menu;},[](auto&v){++v.context.page.menu_type;},
        [](auto&v){++v.context.menu_count;},[](auto&v){++v.movie.bink_handle;},[](auto&v){++v.movie.cinematic_material;},
        [](auto&v){v.movie.current_name[0]='X';},[](auto&v){v.observed_ms=v.movie.observed_ms=999;},
        [](auto&v){v.observed_ms=v.movie.observed_ms=1251;}
    };
    for(const auto& mutate:changed){CinematicProductionEpoch policy(base);auto b=marker();policy.begin(b,true);auto e=b;auto p=finish(e);mutate(e);p->native=e.native;
        check(policy.complete(e,base+0x3a18af,p)!=S::accepted&&!match(policy,e),"thread/frame/context/decoder/age/real-world-view changes reject");}
    for(unsigned test=0;test<6;++test){CinematicProductionEpoch policy(base);auto b=marker();b.native.used=16;policy.begin(b,true);auto e=b;auto p=finish(e);
        switch(test){case 0:p.reset();break;case 1:p->native.frame++;break;case 2:e.native.shared_commands-=4;p->native=e.native;break;
            case 3:p->bytes.resize(20);break;case 4:p->bytes[18]=29;break;case 5:p->bytes[26]=27;break;}
        check(policy.complete(e,base+0x3a18af,p)==S::invalid_extent,"finalized immutable list envelope and source range verified");}
    for(bool start:{false,true}){CinematicProductionEpoch policy(base);auto b=marker();policy.begin(b,start);auto e=b;auto p=finish(e);
        check(policy.complete(e,start?base+0x3a18b0:base+0x3a18af,p)!=S::accepted,"both exact ordinary caller identities required");}
    {CinematicProductionEpoch policy(base);auto b=marker();policy.begin(b,true);auto e=b;auto p=finish(e);policy.complete(e,base+0x3a18af,p);
        CinematicProductionStamp out{};check(!policy.match(e.context,e.movie,e.generation,1251,&out)&&!out.epoch,"original publication age never refreshed by backend");
        auto next=marker(1);next.serial=3;next.native.frame=1;policy.begin(next,true);check(!match(policy,e),"native frame regression retires other SMP slot");}
    {CinematicProductionEpoch policy(base);auto b=marker();policy.begin(b,true);auto e=b;auto p=finish(e);policy.complete(e,base+0x3a18af,p);
        auto next=marker(1);next.serial=3;next.generation=3;policy.begin(next,true);check(!match(policy,e),"XR generation replacement retires all old native sources");}
    {CinematicProductionEpoch policy(base);auto b=marker();policy.begin(b,true);auto e=b;auto p=finish(e);e.movie.active_image_frame=1;
        check(policy.complete(e,base+0x3a18af,p)==S::accepted,"decoder image advance is copied identity evidence only, no pixel-frame claim");}
    {CinematicProductionEpoch policy(base);auto b=marker();b.context.key_catchers=16;b.context.page.menu_type=4;policy.begin(b,true);auto e=b;auto p=finish(e);
        check(policy.complete(e,base+0x3a18af,p)==S::accepted,"native input capture may be diagnosed without granting input or cinematic routing");
        std::array<std::uint8_t,8> unknown{4,0,5,0,4,0,0,0};const auto census=bovr::render::census_hud_commands(unknown);
        check(census.status==bovr::render::CommandListStatus::complete&&(census.unsupported_layout_mask&(1U<<5)),"unknown active ID remains unsupported; census never expands parser admission");}
    std::printf("cinematic_production_checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
