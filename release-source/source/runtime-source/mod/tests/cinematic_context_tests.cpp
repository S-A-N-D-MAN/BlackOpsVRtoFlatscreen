// SPDX-License-Identifier: GPL-3.0-only
// Private read seam exercises synthetic pages only; public activation still
// requires the exact retail executable and the independent opt-in flag.
#include "../src/t5/cinematic_context.cpp"
#include <cstdio>
#include <limits>
#include <vector>
namespace {
using namespace bovr::t5;
unsigned checks{},failures{};
void check(bool v,const char* why) {++checks;std::printf("%s %s\n",v?"PASS":"FAIL",why);if(!v) ++failures;}
template<class T>void put(void* p,T v) {std::memcpy(p,&v,sizeof(v));}
void append(std::vector<std::uint8_t>& b,unsigned id,unsigned size) {
    const auto n=b.size();b.resize(n+size);put(b.data()+n,static_cast<std::uint16_t>(size));b[n+2]=static_cast<std::uint8_t>(id);
}
std::vector<std::uint8_t> list(std::uintptr_t material) {
    std::vector<std::uint8_t> b;append(b,27,8);append(b,10,48);
    put(b.data()+12,static_cast<std::uint32_t>(material));put(b.data()+24,1.F);
    put(b.data()+28,1280.F);put(b.data()+32,720.F);put(b.data()+52,UINT32_MAX);append(b,0,4);return b;
}
}
int main() {
    using namespace bovr::t5;using S=CinematicCommandStatus;
    SetEnvironmentVariableW(L"BOVR_CINEMATIC_PROBE",L"0");
    check(!configure_native_cinematic_observer(GetModuleHandleW(nullptr)),"cinematic observer is independently disabled by default");
    NativeCinematicSnapshot before{};check(!read_native_cinematic_snapshot(&before)&&!before.valid,"inactive observer exposes no native snapshot");
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4400000,MEM_RESERVE,PAGE_NOACCESS));if(!image) return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    for(const auto rva:{name_rva,finished_rva,bink_rva,frame_rva,initialized_rva,material_rva,code_images_rva,std::uintptr_t{0x820838}})
        if(!VirtualAlloc(image+(rva&~std::uintptr_t{4095}),4096,MEM_COMMIT,PAGE_READWRITE)) return 1;
    std::array<unsigned char,32> dvar{};dvar[0x18]=1;
    put(image+0x820838,reinterpret_cast<std::uintptr_t>(dvar.data()));put<unsigned char>(image+initialized_rva,1);
    put<std::uint32_t>(image+bink_rva,0x12345678);put<std::uint32_t>(image+material_rva,0x23456789);put<int>(image+frame_rva,42);
    std::memcpy(image+name_rva,"five_intro",11);
    const auto set=base+texture_sets_rva;
    for(unsigned i=0;i<3;++i) put<std::uint32_t>(image+code_images_rva+i*4,static_cast<std::uint32_t>(set+0x1a0+i*0x34));
    check(snapshot_at(base,&before)&&before.valid&&before.active_image_frame==42&&before.bound_texture_set==0&&before.cg_fullscreen_known&&before.cg_fullscreen,"bounded native copy recognizes one complete mapped renderer texture set");
    const auto original=std::vector<unsigned char>(image+name_rva,image+name_rva+256);
    auto after=before;after.observed_ms=before.observed_ms+1;auto commands=list(before.cinematic_material);
    const auto classify=[&](const std::vector<std::uint8_t>& b) {return classify_cinematic_commands(b,before,after,1280,720);};
    auto proof=classify(commands);
    check(proof.status==S::full_canvas_movie_primitive&&proof.movie_primitives==1&&proof.full_canvas_primitives==1&&proof.consumed_bytes==60,"current same-image decoder and complete 2D native movie rectangle produce bounded evidence");
    check(std::memcmp(original.data(),image+name_rva,256)==0,"snapshot and classifier leave every native movie-name byte unchanged");
    auto changed=before;changed.finished=true;
    check(classify_cinematic_commands(commands,changed,changed,1280,720).status==S::no_current_movie,"finished decoder cannot classify stale commands as current movie");
    for(unsigned reason=0;reason<7;++reason) {
        changed=before;
        switch(reason) {case 0:changed.bink_handle=0;break;case 1:changed.current_name[0]=0;break;case 2:changed.current_name.fill('x');break;
        case 3:changed.active_image_frame=-1;break;case 4:changed.initialized=false;break;case 5:changed.renderer_images[0]+=4;break;case 6:changed.cinematic_material=0;break;}
        check(classify_cinematic_commands(commands,changed,changed,1280,720).status==S::no_current_movie,"missing live decoder/name/frame/material or wrong bound images rejects");
    }
    for(unsigned reason=0;reason<7;++reason) {
        changed=after;
        switch(reason) {case 0:++changed.bink_handle;break;case 1:changed.current_name[0]='x';break;case 2:++changed.active_image_frame;break;
        case 3:++changed.cinematic_material;break;case 4:++changed.thread_id;break;case 5:changed.observed_ms=before.observed_ms+251;break;case 6:changed.observed_ms=before.observed_ms-1;break;}
        check(classify_cinematic_commands(commands,before,changed,1280,720).status==S::changed_snapshot,"movie/frame/material/thread or bounded-time transition rejects");
    }
    changed=before;changed.cg_fullscreen=false;auto later=changed;++later.observed_ms;
    check(classify_cinematic_commands(commands,changed,later,1280,720).status==S::full_canvas_movie_primitive,"cg fullscreen preference is diagnostic and cannot replace actual primitive evidence");
    auto b=commands;put<std::uint32_t>(b.data()+12,0x33445566);
    check(classify(b).status==S::no_movie_primitive,"nonzero unrelated material cannot qualify as native movie");
    b=commands;b[11]=0x80;proof=classify(b);
    check(proof.status==S::no_movie_primitive&&proof.tagged_movie_primitives==1&&!proof.movie_primitives,"tagged movie command is skipped exactly as native execution");
    for(unsigned reason=0;reason<5;++reason) {
        b=commands;
        switch(reason) {case 0:put(b.data()+16,1.F);break;case 1:put(b.data()+20,80.F);break;case 2:put(b.data()+28,1279.F);break;
        case 3:put(b.data()+32,560.F);break;case 4:put(b.data()+24,2.F);break;}
        check(classify(b).status==S::partial_movie_primitive,"partial letterboxed displaced or nonunit-W draw remains unaccepted full canvas");
    }
    b=commands;b[3]=0x80;check(classify(b).status==S::partial_movie_primitive,"tagged projection cannot establish active 2D state");
    for(unsigned id:{6u,7u}) {
        std::vector<std::uint8_t> c;append(c,id,id==6?20:24);
        if(id==6) {put(c.data()+12,640);put(c.data()+16,720);} else {put(c.data()+4,1);put(c.data()+16,640);put(c.data()+20,720);}
        c.insert(c.end(),commands.begin(),commands.end());
        check(classify(c).status==S::partial_movie_primitive,"native viewport or scissor clipping prevents full canvas proof");
    }
    b=commands;put(b.data()+16,std::numeric_limits<float>::infinity());check(classify(b).status==S::malformed_list,"nonfinite native geometry rejects before classification");
    b=commands;b.pop_back();check(classify(b).status==S::malformed_list,"truncated terminator rejects complete-list claim");
    b=commands;b[10]=29;check(classify(b).status==S::malformed_list,"invalid native command ID rejects");
    b=commands;b[10]=8;check(classify(b).status==S::unsupported_list,"unreviewed active command prevents viewport/shader-state assumption");
    b=commands;put<std::uint16_t>(b.data()+8,44);check(classify(b).status==S::malformed_list,"incorrect movie record layout cannot borrow id10 geometry");
    b=commands;b.resize(56);check(classify(b).status==S::malformed_list,"unsealed list has no positive classification");
    check(classify_cinematic_commands(commands,before,after,0,720).status==S::invalid_canvas,"missing native source dimension rejects");
    auto empty=list(0x33445566);check(classify(empty).status==S::no_movie_primitive,"fullscreen preference alone never produces positive evidence");
    std::memset(image+name_rva,'x',256);check(!snapshot_at(base,&changed),"unterminated native movie name rejects bounded read");
    std::memcpy(image+name_rva,"five_intro",11);put<std::uint32_t>(image+0x820838,1);
    check(!snapshot_at(base,&changed),"invalid native diagnostic pointer fails safely without decoder dereference");
    check(!snapshot_at(1,&changed)&&!changed.valid,"inaccessible native memory is a rejected copied snapshot");
    SetEnvironmentVariableW(L"BOVR_CINEMATIC_PROBE",L"1");
    check(!configure_native_cinematic_observer(reinterpret_cast<HMODULE>(image)),"public activation rejects synthetic foreign image even when enabled");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("checks=%u failures=%u game_launches=0 runtime_calls=0 native_movie_calls=0 render_writes=0\n",checks,failures);
    return failures?1:0;
}
