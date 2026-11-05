// SPDX-License-Identifier: GPL-3.0-only
#include "t5/cinematic_frame.hpp"
#include <cstdio>
#include <cstring>
#include <functional>
namespace {
using namespace bovr::t5;
unsigned checks{},failures{};
void check(bool v,const char* text){++checks;if(!v){++failures;std::printf("FAIL %s\n",text);}}
template<class T>void put(std::vector<std::uint8_t>& b,unsigned at,T value){std::memcpy(b.data()+at,&value,sizeof(value));}
struct Fixture {
    FinalizedMenuStamp stamp{};NativeFinalUi native{};NativeCinematicSnapshot movie{};
    CinematicCanvas canvas{1280,720,true,true,true};
    std::shared_ptr<HudCommandPublication> allocation;
    Fixture(){
        native.valid=true;native.backend=0x3b40100;native.commands=0x20000000;native.native_frame=18;native.connection_state=5;
        stamp.epoch=7;stamp.native_epoch=2;stamp.begin.published_ms=1000;stamp.end.published_ms=1010;
        stamp.end.context=native;stamp.end.generation=4;stamp.end.revision=9;
        auto& n=stamp.end.native;n.buffer=native.backend;n.shared_commands=native.commands;n.frame=native.native_frame;n.bytes=native.commands;n.used=60;
        allocation=std::make_shared<HudCommandPublication>();allocation->native=n;allocation->bytes.resize(60);
        auto& b=allocation->bytes;put<std::uint16_t>(b,0,8);b[2]=27;
        put<std::uint16_t>(b,8,48);b[10]=10;put<std::uint32_t>(b,12,0x180000);put<float>(b,24,1);put<float>(b,28,1280);put<float>(b,32,720);
        put<std::uint16_t>(b,56,4);stamp.commands=allocation;
        movie.image_base=0x400000;movie.bink_handle=0x150000;movie.cinematic_material=0x180000;std::memcpy(movie.current_name.data(),"cuba_intro",11);
        movie.active_image_frame=12;movie.bound_texture_set=0;movie.initialized=movie.valid=true;movie.observed_ms=1010;movie.thread_id=17;
        movie.renderer_images={0x3e4cbcc+0x1a0,0x3e4cbcc+0x1d4,0x3e4cbcc+0x208};
    }
    bool begin(CinematicFrameGate& gate,std::uint64_t serial=1){return gate.begin(serial,3,stamp,native,movie,canvas,true,1010);}
    CinematicCommandEvidence complete(CinematicFrameGate& gate,std::uint64_t serial=1){auto m=movie;m.observed_ms=1012;return gate.complete(serial,3,native,m,true,1012);}
    bool take(CinematicFrameGate& gate,CinematicFrameStamp* out=nullptr){CinematicFrameStamp result{};auto m=movie;m.observed_ms=1015;return gate.take_at_present(1,3,4,9,native,m,1015,out?out:&result);}
};
}
int main(){
    using S=CinematicCommandStatus;
    for(int connection:{0,1,2,5,10}){Fixture f;CinematicFrameGate gate;f.native.connection_state=connection;f.stamp.end.context=f.native;
        check(f.begin(gate),"owned current zero-world-view list can be inspected independently of connection state");
        check(f.complete(gate).status==S::full_canvas_movie_primitive,"actual full-canvas material gives positive current movie evidence");
        CinematicFrameStamp output{};check(f.take(gate,&output)&&output.production.epoch==7&&output.production.begin.published_ms==1000&&output.final_serial==1,"only exact same native completion reaches pre-Present with original production time");
        check(!f.take(gate),"completed movie frame is consumed once");
    }
    const std::function<void(Fixture&)> begin_bad[]{
        [](auto&f){f.stamp.epoch=0;},[](auto&f){f.stamp.native_epoch=0;},[](auto&f){f.stamp.commands.reset();},
        [](auto&f){f.allocation->native.frame++;},
        [](auto&f){f.stamp.begin.published_ms=0;},[](auto&f){f.stamp.end.generation=0;},[](auto&f){f.stamp.end.revision=0;},
        [](auto&f){f.stamp.end.context.valid=false;},[](auto&f){f.stamp.end.context.connection_state++;},
        [](auto&f){f.stamp.end.native.buffer++;},[](auto&f){f.stamp.end.native.frame++;},[](auto&f){f.stamp.end.native.shared_commands++;},
        [](auto&f){f.stamp.end.native.view_count=1;},[](auto&f){f.stamp.end.native.view_count=2;},
        [](auto&f){f.canvas.width=0;},[](auto&f){f.canvas.height=0;},[](auto&f){f.canvas.full_viewport=false;},
        [](auto&f){f.canvas.full_scissor=false;},[](auto&f){f.canvas.backbuffer_target=false;},
        [](auto&f){f.stamp.begin.published_ms=759;},[](auto&f){f.stamp.begin.published_ms=1011;},
        [](auto&f){f.movie.thread_id=0;},[](auto&f){f.movie.observed_ms=1011;},[](auto&f){f.movie.observed_ms=759;}
    };
    for(const auto& mutate:begin_bad){Fixture f;mutate(f);CinematicFrameGate gate;
        check(!f.begin(gate),"unsupported or changed initial epoch/canvas rejects before movie routing");
        check(f.complete(gate).status!=S::full_canvas_movie_primitive&&!f.take(gate),"rejected initial observation leaves no prior completed result");}
    {Fixture f;CinematicFrameGate gate;check(!gate.begin(1,3,f.stamp,f.native,f.movie,f.canvas,false,1010),"native allocation mismatch prevents initial evidence");}
    for(unsigned reason=0;reason<10;++reason){Fixture f;CinematicFrameGate gate;check(f.begin(gate),"start final-loop change case");auto n=f.native;auto m=f.movie;bool copied=true;std::uint64_t serial=1,now=1012;
        switch(reason){case 0:n.native_frame++;break;case 1:n.backend++;break;case 2:n.commands++;break;case 3:n.key_catchers=16;break;case 4:n.page.focused_menu++;break;
        case 5:m.active_image_frame++;break;case 6:copied=false;break;case 7:serial++;break;case 8:now=1251;break;case 9:now=999;break;}
        check(gate.complete(serial,3,n,m,copied,now).status!=S::full_canvas_movie_primitive&&!f.take(gate),"changed final loop cannot claim or retain movie pixels");}
    for(unsigned reason=0;reason<12;++reason){Fixture f;CinematicFrameGate gate;f.begin(gate);f.complete(gate);auto n=f.native;auto m=f.movie;std::uint64_t serial=1,generation=4,revision=9,now=1015;CinematicFrameStamp out{};
        switch(reason){case 0:serial++;break;case 1:generation++;break;case 2:revision++;break;case 3:n.connection_state++;break;case 4:n.menu_count++;break;
        case 5:n.page.focused_menu++;break;case 6:m.current_name[0]='x';break;case 7:m.finished=true;break;case 8:m.renderer_images[0]++;break;case 9:now=1251;break;case 10:now=1011;break;case 11:m.thread_id++;break;}
        check(!gate.take_at_present(serial,3,generation,revision,n,m,now,&out)&&!out.final_serial,"changed current context, decoder, generation or original age rejects at capture seam");
        check(!f.take(gate),"a rejected pre-Present attempt still consumes the old native completion");}
    for(unsigned reason=0;reason<7;++reason){Fixture f;CinematicFrameGate gate;f.begin(gate);f.complete(gate);
        auto bad=f.stamp;auto canvas=f.canvas;bool copied=true;std::uint64_t serial=2;
        switch(reason){case 0:bad.epoch=0;break;case 1:bad.end.native.view_count=3;break;case 2:canvas.full_scissor=false;break;case 3:copied=false;break;case 4:serial=1;break;case 5:serial=0;break;case 6:bad.commands.reset();break;}
        check(!gate.begin(serial,3,bad,f.native,f.movie,canvas,copied,1015),"every later rejected FinalUi observation retires previous movie result");
        check(!f.take(gate),"rejected later source cannot revive previous native movie pixels");}
    for(unsigned reason=0;reason<6;++reason){Fixture f;CinematicFrameGate gate;
        switch(reason){case 0:put<float>(f.allocation->bytes,32,540);break;case 1:put<std::uint32_t>(f.allocation->bytes,12,0x190000);break;
        case 2:f.allocation->bytes[11]=128;break;case 3:f.allocation->bytes[3]=128;break;case 4:f.movie.finished=true;break;case 5:f.allocation->bytes.pop_back();break;}
        check(f.begin(gate),"current frame ownership does not fabricate primitive evidence");
        check(f.complete(gate).status!=S::full_canvas_movie_primitive&&!f.take(gate),"partial, unrelated, tagged, stale or malformed movies remain rejected despite state5");}
    for(int catcher:{1,16,17,32}){Fixture f;CinematicFrameGate gate;f.native.key_catchers=catcher;f.native.menu_count=4;f.stamp.end.context=f.native;
        check(!f.begin(gate),"movie primitive cannot take ownership from native menu/console input capture");
        check(f.complete(gate).status!=S::full_canvas_movie_primitive&&!f.take(gate),"active menu over movie stays on the unchanged menu route");
        if(catcher&16)check(f.native.valid&&(f.native.key_catchers&16)&&f.native.menu_count>0,"existing interactive menu predicate remains true after cinema rejection");}
    {Fixture f;CinematicFrameGate gate;f.begin(gate);f.complete(gate);CinematicFrameStamp out{};
        check(!gate.take_at_present(1,4,4,9,f.native,f.movie,1015,&out),"new backend with null shared list and no FinalUi cannot present the earlier completed movie");
        check(!f.take(gate),"null-list successor consumes stale candidate without reviving old pixels");}
    {Fixture f;CinematicFrameGate gate;f.begin(gate);
        check(gate.complete(1,4,f.native,f.movie,true,1012).status!=S::full_canvas_movie_primitive,"foreign/intervening backend invalidates current original-loop association");}
    {Fixture f;CinematicFrameGate gate;
        check(!gate.begin(1,0,f.stamp,f.native,f.movie,f.canvas,true,1010),"a native backend epoch is mandatory before FinalUi admission");}
    {Fixture f;CinematicFrameGate gate;f.begin(gate);f.complete(gate);gate.invalidate();check(!f.take(gate),"explicit owner reset invalidates pending final completion");}
    std::printf("cinematic_frame_checks=%u failures=%u native_calls=0 GPU_calls=0\n",checks,failures);return failures?1:0;
}
