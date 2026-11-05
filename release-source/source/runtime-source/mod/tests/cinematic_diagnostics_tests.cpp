// SPDX-License-Identifier: GPL-3.0-only
#include "t5/cinematic_diagnostics.hpp"
#include <cstdio>
#include <functional>
#include <cstring>
namespace {
using namespace bovr::t5;
unsigned checks{},failures{};
void check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}}
template<class T>void put(std::vector<std::uint8_t>& b,unsigned at,T v){std::memcpy(b.data()+at,&v,sizeof(v));}
struct Fixture {
    CinematicDiagnosticStart s{};std::shared_ptr<HudCommandPublication> allocation;
    Fixture(){
        s.sampled=s.movie_copied=s.canvas_copied=s.owned_native_bytes=true;s.serial=7;s.backend_serial=9;s.started_ms=1010;
        s.canvas={1280,720,true,true,true};auto& n=s.native;n.valid=true;n.backend=0x3b40100;n.commands=0x20000000;n.native_frame=18;n.connection_state=5;
        auto& p=s.production;p.epoch=3;p.native_epoch=2;p.begin.published_ms=1000;p.end.published_ms=1010;p.end.context=n;p.end.generation=4;p.end.revision=8;
        auto& f=p.end.native;f.buffer=n.backend;f.shared_commands=n.commands;f.frame=n.native_frame;f.bytes=n.commands;f.used=60;
        allocation=std::make_shared<HudCommandPublication>();allocation->native=f;allocation->bytes.resize(60);p.commands=allocation;
        auto& b=allocation->bytes;put<std::uint16_t>(b,0,8);b[2]=27;put<std::uint16_t>(b,8,48);b[10]=10;
        put<std::uint32_t>(b,12,0x180000);put<float>(b,24,1);put<float>(b,28,1280);put<float>(b,32,720);put<std::uint16_t>(b,56,4);
        auto& m=s.movie;m.valid=m.initialized=true;m.image_base=0x400000;m.bink_handle=0x150000;m.cinematic_material=0x180000;
        std::memcpy(m.current_name.data(),"cuba_load",10);m.active_image_frame=0;m.bound_texture_set=0;m.thread_id=17;m.observed_ms=1010;
        m.renderer_images={0x3e4cbcc+0x1a0,0x3e4cbcc+0x1d4,0x3e4cbcc+0x208};
    }
    std::uint32_t begin(){s.reasons=cinematic_diagnostic_reasons(s,true,9);return s.reasons;}
    std::uint32_t complete(){return cinematic_diagnostic_finish_reasons(s,s.native,7,9,true,4,8,true,true,1012);}
};
}
int main(){
    Fixture f;check(f.begin()==0&&f.complete()==0,"complete source ownership has no missing prerequisites");
    for(int catcher:{0,1,16,17,32}){
        Fixture a;a.s.native.key_catchers=catcher;a.s.production.end.context=a.s.native;
        check(a.begin()==(catcher?diagnostic_input_capture:0U),"actual input capture appears in pre-filter reason mask");
        check(cinematic_diagnostic_can_classify(a.complete()),"passive owned primitive diagnosis is independent of input routing");
        auto after=a.s.movie;after.observed_ms=1012;
        check(classify_cinematic_commands(published_shared_list(*a.s.production.commands),a.s.movie,after,1280,720).status==CinematicCommandStatus::full_canvas_movie_primitive,"owned full-canvas native movie classified for diagnostic evidence");
        CinematicFrameGate original;
        check(original.begin(7,9,a.s.production,a.s.native,a.s.movie,a.s.canvas,true,1010)==(catcher==0),"C1 admission remains unchanged and captured input still owns menu path");
    }
    const std::pair<std::uint32_t,std::function<void(Fixture&)>> mutations[]{
        {diagnostic_no_epoch,[](auto&a){a.s.production.epoch=0;}},
        {diagnostic_no_epoch,[](auto&a){a.s.production.native_epoch=0;}},
        {diagnostic_native_bytes,[](auto&a){a.s.owned_native_bytes=false;}},
        {diagnostic_backend_epoch,[](auto&a){a.s.backend_serial=8;}},
        {diagnostic_native_context,[](auto&a){a.s.native.valid=false;}},
        {diagnostic_decoder_copy,[](auto&a){a.s.movie_copied=false;}},
        {diagnostic_canvas_copy,[](auto&a){a.s.canvas_copied=false;}},
        {diagnostic_viewport,[](auto&a){a.s.canvas.full_viewport=false;}},
        {diagnostic_scissor,[](auto&a){a.s.canvas.full_scissor=false;}},
        {diagnostic_target,[](auto&a){a.s.canvas.backbuffer_target=false;}},
        {diagnostic_publication,[](auto&a){a.s.production.commands.reset();}},
        {diagnostic_publication,[](auto&a){a.allocation->native.frame++;}},
        {diagnostic_publication,[](auto&a){a.s.production.end.context.page.focused_menu++;}},
        {diagnostic_publication,[](auto&a){a.s.native.commands++;}},
        {diagnostic_age,[](auto&a){a.s.production.begin.published_ms=0;}},
        {diagnostic_age,[](auto&a){a.s.production.begin.published_ms=759;}},
        {diagnostic_age,[](auto&a){a.s.production.begin.published_ms=1011;}}
    };
    for(const auto& [reason,mutate]:mutations){Fixture a;mutate(a);const auto mask=a.begin();check((mask&reason)!=0,"specific missing prerequisite is named");check(!cinematic_diagnostic_can_classify(mask),"unowned/stale/invalid canvas cannot claim primitive proof");}
    check(cinematic_diagnostic_reasons(f.s,false,9)&diagnostic_no_device,"missing retained device named");
    {Fixture a;a.s.production.end.native.view_count=2;a.allocation->native=a.s.production.end.native;
        check(a.begin()==diagnostic_world_views,"native world views remain an explicit C1 admission blocker");
        check(cinematic_diagnostic_can_classify(a.complete()),"world-view command primitive may be observed without routing it");}
    for(unsigned change=0;change<12;++change){Fixture a;a.begin();auto native=a.s.native;std::uint64_t serial=7,backend=9,generation=4,revision=8,now=1012;bool pending=true,owned=true,copied=true;
        switch(change){case 0:serial++;break;case 1:backend++;break;case 2:generation++;break;case 3:revision++;break;
        case 4:pending=false;break;case 5:owned=false;break;case 6:copied=false;break;case 7:now=1251;break;
        case 8:now=999;break;case 9:native.page.focused_menu++;break;case 10:native.native_frame++;break;case 11:native.backend++;break;}
        check(!cinematic_diagnostic_can_classify(cinematic_diagnostic_finish_reasons(a.s,native,serial,backend,pending,generation,revision,owned,copied,now)),"final/backend/generation/revision/native context or original-age change rejects passive pairing");}
    {CinematicDiagnosticBudget b;unsigned samples{};for(unsigned ms=1;ms<=1000;++ms)samples+=b.sample(ms);
        check(samples==10,"sample work bounded to ten per second");check(!b.sample(1)&&!b.sample(0),"clock regression cannot refresh sampling");}
    {CinematicDiagnosticBudget b;CinematicDiagnosticKey key{};for(unsigned i=0;i<6;++i)check(b.report(100+i*100,key),"first six records retained");
        check(!b.report(800,key),"identical context is not logged every observed frame");key.catchers=16;
        check(!b.report(700,key)&&b.report(850,key),"context change is bounded by250ms");check(!b.report(100,key),"report clock regression rejected");
        check(b.report(2850,key),"unchanged context periodically reported after2s");}
    {CinematicDiagnosticBudget b;CinematicDiagnosticKey key{};unsigned count{};for(unsigned i=0;i<1000;++i)count+=b.report(1+2000ULL*i,key);
        check(count==512&&b.reports()==512,"process log ceiling is512 records");check(!b.sample(3000000),"completed log budget also stops diagnostic work");}
    {CinematicDiagnosticBudget b;unsigned count{};for(unsigned i=0;i<13000;++i)count+=b.sample(1+100ULL*i);check(count==12000,"work ceiling also applies without logging");}
    std::printf("cinematic_diagnostic_checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
