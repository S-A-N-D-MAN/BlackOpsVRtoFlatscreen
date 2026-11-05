// SPDX-License-Identifier: GPL-3.0-only
#include "t5/cinematic_movie_frame.hpp"
#include "render/hud_commands.hpp"
#include "cinematic_movie_fixture.hpp"
#include <cstdio>
#include <functional>
#include <limits>
using namespace bovr::t5;
namespace {
unsigned checks{},failures{};
void check(bool v,const char* text){++checks;if(!v){++failures;std::printf("FAIL %s\n",text);}}
template<class T>void put(std::vector<std::uint8_t>& b,unsigned at,T v){std::memcpy(b.data()+at,&v,sizeof(v));}
struct Fixture {
    CinematicProductionStamp stamp{};NativeFinalUi native{};NativeCinematicSnapshot movie{};
    CinematicCanvas canvas{1280,720,true,true,true};std::shared_ptr<HudCommandPublication> allocation;
    Fixture(){
        native.valid=true;native.backend=0x3b40100;native.commands=0x20000000;native.native_frame=18;native.connection_state=5;
        stamp.epoch=7;stamp.native_epoch=2;stamp.begin.observed_ms=1000;stamp.end.observed_ms=1010;
        stamp.begin.thread=stamp.end.thread=19;stamp.begin.serial=4;stamp.end.serial=5;stamp.begin.generation=stamp.end.generation=4;
        stamp.end.context=native;auto& n=stamp.end.native;n.buffer=native.backend;n.shared_commands=native.commands;n.frame=native.native_frame;n.bytes=native.commands;n.used=136;
        stamp.begin.native=n;allocation=std::make_shared<HudCommandPublication>();allocation->native=n;
        allocation->bytes.assign(cinematic_movie_fixture::authored_movie_commands.begin(),cinematic_movie_fixture::authored_movie_commands.end());stamp.commands=allocation;
        movie.image_base=0x400000;movie.bink_handle=0x150000;movie.cinematic_material=cinematic_movie_fixture::movie_material;std::memcpy(movie.current_name.data(),"fixture_movie",14);
        movie.active_image_frame=12;movie.bound_texture_set=0;movie.initialized=movie.valid=movie.cg_fullscreen=movie.cg_fullscreen_known=true;
        movie.observed_ms=1010;movie.thread_id=17;movie.renderer_images={0x3e4cbcc+0x1a0,0x3e4cbcc+0x1d4,0x3e4cbcc+0x208};
        stamp.begin.movie=stamp.end.movie=movie;
    }
    void resize(unsigned width,unsigned height){
        canvas.width=width;canvas.height=height;
        // Both native image records use source-scaled virtual movie geometry.
        for(unsigned at:{36u,84u}){
            put(allocation->bytes,at+8,-0.5F*width/1280.F);
            put(allocation->bytes,at+20,1281.F*width/1280.F);
            put(allocation->bytes,at+24,static_cast<float>(height));
        }
    }
    CinematicCommandEvidence classify(){auto m=movie;m.observed_ms=1012;return classify_cinematic_movie_commands(allocation->bytes,movie,m,canvas.width,canvas.height);}
    bool begin(CinematicMovieFrameGate& g,std::uint64_t serial=1){return g.begin(serial,3,stamp,native,movie,canvas,true,1010);}
    CinematicCommandEvidence complete(CinematicMovieFrameGate& g){auto m=movie;m.observed_ms=1012;return g.complete(1,3,native,m,true,1012);}
    bool take(CinematicMovieFrameGate& g){auto m=movie;m.observed_ms=1015;CinematicMovieFrameStamp out;return g.take_at_present(1,3,4,native,m,1015,&out);}
};
}
int main(){
    using S=CinematicCommandStatus;
    {Fixture f;check(f.classify().status==S::full_canvas_movie_primitive,"authored136-byte clear plus overscan movie qualifies");
     check(bovr::render::census_hud_commands(f.allocation->bytes).unsupported_layout_mask==(1u<<5),"HUD clear rejection remains unchanged");
     auto m=f.movie;m.observed_ms=1012;check(classify_cinematic_commands(f.allocation->bytes,f.movie,m,1280,720).status==S::unsupported_list,"legacy C1 clear policy remains unchanged");}
    for(const auto size:{std::array<unsigned,2>{640,360},{1280,720},{1920,1080},{2560,1440},{3840,2160},{4096,2160}}){
        Fixture f;f.resize(size[0],size[1]);CinematicMovieFrameGate g;
        check(f.classify().status==S::full_canvas_movie_primitive,"native source-scaled movie rectangle covers the full canvas");
        check(f.begin(g)&&f.complete(g).status==S::full_canvas_movie_primitive&&f.take(g),"resized movie survives owned begin completion and present");
        check(!f.take(g),"resized movie completion remains single use");
        for(unsigned why=0;why<6;++why){Fixture bad;bad.resize(size[0],size[1]);auto& b=bad.allocation->bytes;
            switch(why){
                case 0:put(b,92,0.01F);break; // A real uncovered left edge.
                case 1:put(b,92,-1.01F*size[0]/1280.F);break;
                case 2:put(b,104,static_cast<float>(size[0]));break; // Overscan origin leaves a right gap.
                case 3:put(b,104,1283.F*size[0]/1280.F);break;
                case 4:put(b,96,0.01F);break;
                case 5:put(b,108,723.F*size[1]/720.F);break;
            }
            check(bad.classify().status==S::partial_movie_primitive,"resolution scaling does not admit gaps or excessive overscan");
        }
    }
    for(unsigned flag=0;flag<256;++flag){Fixture f;f.allocation->bytes[12]=static_cast<std::uint8_t>(flag);
        check((f.classify().status==S::full_canvas_movie_primitive)==(flag>=1&&flag<=7),"only native clear flags1..7 accepted");}
    for(unsigned at:{16u,20u,24u,28u,32u})for(float v:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        Fixture f;put(f.allocation->bytes,at,v);check(f.classify().status==S::malformed_list,"nonfinite depth/RGBA rejects");}
    for(float depth:{-1.F,1.01F}){Fixture f;put(f.allocation->bytes,16,depth);check(f.classify().status==S::malformed_list,"out-of-range depth rejects");}
    for(unsigned i=0;i<8;++i){Fixture f;auto& b=f.allocation->bytes;
        switch(i){case 0:put(b,92,0.01F);break;case 1:put(b,92,-1.01F);break;case 2:put(b,104,1279.F);break;case 3:put(b,104,1283.F);break;
            case 4:put(b,100,2.F);break;case 5:put(b,116,0.1F);break;case 6:put<std::uint32_t>(b,128,0x7fffffff);break;case 7:b[87]=0x80;break;}
        check(f.classify().status!=S::full_canvas_movie_primitive,"partial/oversized/nonunit/UV/faded/tagged movie rejects");}
    {Fixture f;auto& b=f.allocation->bytes;std::vector<std::uint8_t> clear(b.begin()+8,b.begin()+36);b.insert(b.end()-4,clear.begin(),clear.end());
     check(f.classify().status==S::partial_movie_primitive,"later color clear removes prior movie proof");b[b.size()-28]=2;
     check(f.classify().status==S::full_canvas_movie_primitive,"later depth-only clear preserves movie color evidence");}
    {Fixture f;auto& b=f.allocation->bytes;std::vector<std::uint8_t> image(b.begin()+36,b.begin()+84);b.insert(b.end()-4,image.begin(),image.end());
     check(f.classify().status==S::partial_movie_primitive,"later unrelated image cannot inherit movie proof");}
    {Fixture f;f.allocation->bytes[11]=0x80;f.allocation->bytes[12]=255;check(f.classify().status==S::full_canvas_movie_primitive,"tagged clear is skipped in final-window path");}
    for(unsigned id:{1u,2u,8u,9u,15u,18u,24u,26u}){Fixture f;f.allocation->bytes[10]=static_cast<std::uint8_t>(id);check(f.classify().status!=S::full_canvas_movie_primitive,"unmapped movie-path record rejects");}
    for(unsigned connection:{0u,1u,2u,5u,10u})for(DWORD producer:{17UL,29UL}){Fixture f;f.native.connection_state=static_cast<int>(connection);f.stamp.end.context=f.native;
        f.stamp.begin.thread=f.stamp.end.thread=producer;CinematicMovieFrameGate g;
        check(f.begin(g)&&f.complete(g).status==S::full_canvas_movie_primitive&&f.take(g),"current owned production succeeds independently of connection and menu event thread");
        check(!f.take(g),"each completion consumed once");}
    const std::function<void(Fixture&)> bad[]{
        [](auto&f){f.stamp.epoch=0;},[](auto&f){f.stamp.native_epoch=0;},[](auto&f){f.stamp.commands.reset();},
        [](auto&f){f.allocation->native.frame++;},[](auto&f){f.stamp.begin.observed_ms=0;},[](auto&f){f.stamp.begin.observed_ms=759;},
        [](auto&f){f.stamp.begin.observed_ms=1011;},[](auto&f){f.stamp.end.generation=0;},[](auto&f){f.stamp.begin.generation++;},
        [](auto&f){f.stamp.begin.thread++;},[](auto&f){f.stamp.begin.thread=f.stamp.end.thread=0;},[](auto&f){f.stamp.begin.serial=f.stamp.end.serial;},
        [](auto&f){f.stamp.begin.native.frame++;},[](auto&f){f.stamp.begin.native.buffer++;},[](auto&f){f.stamp.end.native.view_count=1;},
        [](auto&f){f.native.key_catchers=16;f.stamp.end.context=f.native;},[](auto&f){f.native.key_catchers=1;f.stamp.end.context=f.native;},
        [](auto&f){f.stamp.end.context.page.menu_type++;},[](auto&f){f.native.commands++;},[](auto&f){f.canvas.full_viewport=false;},
        [](auto&f){f.canvas.full_scissor=false;},[](auto&f){f.canvas.backbuffer_target=false;},[](auto&f){f.movie.cg_fullscreen=false;},
        [](auto&f){f.movie.finished=true;},[](auto&f){f.movie.thread_id=0;},[](auto&f){f.movie.observed_ms=759;}
    };
    for(auto& mutate:bad){Fixture f;mutate(f);CinematicMovieFrameGate g;check(!f.begin(g)&&!f.take(g),"invalid initial authority cannot produce movie");}
    for(unsigned why=0;why<11;++why){Fixture f;CinematicMovieFrameGate g;f.begin(g);auto n=f.native;auto m=f.movie;m.observed_ms=1012;
        bool owned=true;std::uint64_t serial=1,backend=3,now=1012;
        switch(why){case 0:n.backend++;break;case 1:n.commands++;break;case 2:n.native_frame++;break;case 3:n.key_catchers=16;break;case 4:n.page.menu_type++;break;
            case 5:m.active_image_frame++;break;case 6:owned=false;break;case 7:serial++;break;case 8:backend++;break;case 9:now=1251;break;case 10:now=999;break;}
        check(g.complete(serial,backend,n,m,owned,now).status!=S::full_canvas_movie_primitive&&!f.take(g),"changed final native completion cannot survive");}
    for(unsigned why=0;why<12;++why){Fixture f;CinematicMovieFrameGate g;f.begin(g);f.complete(g);auto n=f.native;auto m=f.movie;m.observed_ms=1015;
        std::uint64_t serial=1,backend=3,generation=4,now=1015;CinematicMovieFrameStamp out;
        switch(why){case 0:serial++;break;case 1:backend++;break;case 2:generation++;break;case 3:n.key_catchers=16;break;case 4:n.page.menu_type++;break;
            case 5:m.current_name[0]='x';break;case 6:m.active_image_frame++;break;case 7:m.finished=true;break;case 8:now=1251;break;case 9:now=1011;break;
            case 10:m.thread_id++;break;case 11:m.renderer_images[0]++;break;}
        check(!g.take_at_present(serial,backend,generation,n,m,now,&out)&&!out.final_serial&&!f.take(g),"intervening backend/final/reset/context/decoder/age fails once");}
    {Fixture f;CinematicMovieFrameGate g;f.begin(g);f.complete(g);f.native.key_catchers=16;check(!f.begin(g,2)&&!f.take(g),"later rejected UI owns input and invalidates old completed movie");}
    {Fixture f;CinematicMovieFrameGate g;f.begin(g);f.complete(g);g.invalidate();check(!f.take(g),"stop/reset owner invalidation retires owned copied list");}
    check(!validate_cinematic_movie_guards(GetModuleHandleW(nullptr)),"synthetic host cannot pass exact retail clear guard");
    std::printf("cinematic_movie_checks=%u failures=%u native_movie_calls=0 game_launches=0\n",checks,failures);return failures?1:0;
}
