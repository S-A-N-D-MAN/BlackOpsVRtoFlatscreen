// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/cinematic_context.hpp"
#include "t5/identity.hpp"
#include "render/hud_commands.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwchar>
namespace bovr::t5 {
namespace {
std::atomic<std::uintptr_t> cinematic_base{};
constexpr std::uintptr_t name_rva=0x3a4c750,finished_rva=0x3a4ca64,bink_rva=0x3a4ca80;
constexpr std::uintptr_t frame_rva=0x3a4cbbc,initialized_rva=0x420c0b5,material_rva=0x356a25c;
constexpr std::uintptr_t texture_sets_rva=0x3a4cbcc,code_images_rva=0x4173a7c;
template<class T>T cine_load(std::uintptr_t p) noexcept {T v{};std::memcpy(&v,reinterpret_cast<void*>(p),sizeof(v));return v;}
bool image_guards(std::uintptr_t base) noexcept {
    // Built-in registration row {"cinematic", &rgp.cinematicMaterial}; the
    // native registration loop 6D4120 writes its result through row+4.
    const std::uint32_t row[]{static_cast<std::uint32_t>(base+0x5b9624),static_cast<std::uint32_t>(base+material_rva)};

    __try {
        return !std::memcmp(reinterpret_cast<void*>(base+0x74b6f8),row,8)&&
            !std::memcmp(reinterpret_cast<void*>(base+0x5b9624),"cinematic",10)&&
            !bovr::native_guard::compare(reinterpret_cast<void*>(base+0x2da210), 7, "b54fea2b286d1caab47cd6d31d89d712548d926e395dd7bc698a1479df86681d");
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool snapshot_once(std::uintptr_t base,NativeCinematicSnapshot* out) noexcept {
    NativeCinematicSnapshot s{};s.image_base=base;
    __try {
        s.initialized=cine_load<unsigned char>(base+initialized_rva)!=0;
        s.finished=cine_load<unsigned char>(base+finished_rva)!=0;
        s.bink_handle=cine_load<std::uint32_t>(base+bink_rva);
        s.cinematic_material=cine_load<std::uint32_t>(base+material_rva);
        s.active_image_frame=cine_load<int>(base+frame_rva);
        std::memcpy(s.current_name.data(),reinterpret_cast<void*>(base+name_rva),s.current_name.size());
        for(unsigned i=0;i<3;++i) s.renderer_images[i]=cine_load<std::uint32_t>(base+code_images_rva+i*4);
        const auto flag=cine_load<std::uint32_t>(base+0x820838);
        if(flag) {s.cg_fullscreen=cine_load<unsigned char>(flag+0x18)!=0;s.cg_fullscreen_known=true;}
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    if(!std::memchr(s.current_name.data(),0,s.current_name.size())) return false;
    // Native 6D8BE0 publishes these three draw-image addresses from one of two
    // 0x290-byte texture sets. Do not dereference/retain any image or BINK object.
    for(int set=0;set<2;++set) {
        const auto p=base+texture_sets_rva+set*0x290;
        if(s.renderer_images==std::array<std::uintptr_t,3>{p+0x1a0,p+0x1d4,p+0x208}) s.bound_texture_set=set;
    }
    s.valid=true;*out=s;return true;
}
bool same_values(const NativeCinematicSnapshot& a,const NativeCinematicSnapshot& b) noexcept {
    return a.valid&&b.valid&&a.image_base==b.image_base&&a.bink_handle==b.bink_handle&&
        a.cinematic_material==b.cinematic_material&&a.current_name==b.current_name&&a.renderer_images==b.renderer_images&&
        a.active_image_frame==b.active_image_frame&&a.bound_texture_set==b.bound_texture_set&&
        a.initialized==b.initialized&&a.finished==b.finished&&a.cg_fullscreen==b.cg_fullscreen&&a.cg_fullscreen_known==b.cg_fullscreen_known;
}
bool snapshot_at(std::uintptr_t base,NativeCinematicSnapshot* out) noexcept {
    if(out) *out={};if(!out) return false;
    NativeCinematicSnapshot first{},second{};
    if(!snapshot_once(base,&first)||!snapshot_once(base,&second)||!same_values(first,second)) return false;
    // Two equal bounded reads detect observed transitions, not arbitrary ABA or
    // atomicity. Decoder ownership is never inferred from these copied fields.
    second.observed_ms=GetTickCount64();second.thread_id=GetCurrentThreadId();*out=second;return true;
}
bool current(const NativeCinematicSnapshot& s) noexcept {
    if(!(s.valid&&s.image_base&&s.initialized&&!s.finished&&s.bink_handle&&s.cinematic_material&&
        s.current_name[0]&&std::memchr(s.current_name.data(),0,s.current_name.size())&&s.active_image_frame>=0&&
        s.bound_texture_set>=0&&s.bound_texture_set<2)) return false;
    const auto p=s.image_base+texture_sets_rva+s.bound_texture_set*0x290;
    return s.renderer_images==std::array<std::uintptr_t,3>{p+0x1a0,p+0x1d4,p+0x208};
}
template<class T>T value(const std::uint8_t* p) noexcept {T v{};std::memcpy(&v,p,sizeof(v));return v;}
bool same_pixel(float a,float b) noexcept {return std::isfinite(a)&&std::fabs(a-b)<=0.5F;}
}
bool configure_native_cinematic_observer(HMODULE executable) noexcept {
    wchar_t flag[8]{};GetEnvironmentVariableW(L"BOVR_CINEMATIC_PROBE",flag,8);if(std::wcscmp(flag,L"1")) return false;
    const auto base=reinterpret_cast<std::uintptr_t>(executable);
    if(base!=0x400000||executable!=GetModuleHandleW(nullptr)) return false;
    if(cinematic_base.load()) return cinematic_base.load()==base;
    try {
        std::string reason;
        if(!validate_path(executable_path()).accepted||!validate_loaded_headers(executable,reason)||!image_guards(base)) return false;
        cinematic_base.store(base);return true;
    } catch(...) {return false;}
}
bool read_native_cinematic_snapshot(NativeCinematicSnapshot* out) noexcept {
    if(out) *out={};const auto base=cinematic_base.load();return base&&snapshot_at(base,out);
}
CinematicCommandEvidence classify_cinematic_commands(std::span<const std::uint8_t> bytes,
    const NativeCinematicSnapshot& before,const NativeCinematicSnapshot& after,std::uint32_t width,std::uint32_t height) noexcept {
    using S=CinematicCommandStatus;CinematicCommandEvidence result{};
    if(!current(before)||!current(after)) return result;
    if(!same_values(before,after)||!before.thread_id||before.thread_id!=after.thread_id||!before.observed_ms||
       after.observed_ms<before.observed_ms||after.observed_ms-before.observed_ms>250) {result.status=S::changed_snapshot;return result;}
    if(!width||!height||width>32768||height>32768) {result.status=S::invalid_canvas;return result;}
    if(bytes.size()>0x180000) {result.status=S::malformed_list;return result;}
    const auto census=render::census_hud_commands(bytes);result.consumed_bytes=census.consumed_bytes;
    if(census.status!=render::CommandListStatus::complete||census.malformed_layout_mask) {result.status=S::malformed_list;return result;}
    if(census.unsupported_layout_mask) {result.status=S::unsupported_list;return result;}
    bool projection_2d=false,full_viewport=true,full_scissor=true;
    for(std::size_t offset=0;offset<census.consumed_bytes;) {
        const auto* p=bytes.data()+offset;const auto length=value<std::uint16_t>(p);const auto id=p[2];
        if(!id) break;
        if(p[3]&0x80) {
            if(id==10&&length==48&&value<std::uint32_t>(p+4)==before.cinematic_material) ++result.tagged_movie_primitives;
            offset+=length;continue;
        }
        if(id==27) projection_2d=value<int>(p+4)==0;
        if(id==6) full_viewport=value<int>(p+4)==0&&value<int>(p+8)==0&&value<int>(p+12)==static_cast<int>(width)&&value<int>(p+16)==static_cast<int>(height);
        if(id==7) full_scissor=value<int>(p+4)==0||(value<int>(p+8)==0&&value<int>(p+12)==0&&value<int>(p+16)>=static_cast<int>(width)&&value<int>(p+20)>=static_cast<int>(height));
        if(id==10&&value<std::uint32_t>(p+4)==before.cinematic_material) {
            ++result.movie_primitives;result.last_x=value<float>(p+8);result.last_y=value<float>(p+12);
            result.last_width=value<float>(p+20);result.last_height=value<float>(p+24);
            if(projection_2d&&full_viewport&&full_scissor&&value<float>(p+16)==1.F&&
               same_pixel(result.last_x,0)&&same_pixel(result.last_y,0)&&same_pixel(result.last_width,static_cast<float>(width))&&same_pixel(result.last_height,static_cast<float>(height))) ++result.full_canvas_primitives;
        }
        offset+=length;
    }
    result.status=result.full_canvas_primitives?S::full_canvas_movie_primitive:result.movie_primitives?S::partial_movie_primitive:S::no_movie_primitive;
    return result;
}
const char* cinematic_command_status_name(CinematicCommandStatus s) noexcept {
    switch(s) {
    case CinematicCommandStatus::no_current_movie:return "no_current_movie";case CinematicCommandStatus::changed_snapshot:return "changed_snapshot";
    case CinematicCommandStatus::invalid_canvas:return "invalid_canvas";case CinematicCommandStatus::malformed_list:return "malformed_list";
    case CinematicCommandStatus::unsupported_list:return "unsupported_list";case CinematicCommandStatus::no_movie_primitive:return "no_movie_primitive";
    case CinematicCommandStatus::partial_movie_primitive:return "partial_movie_primitive";case CinematicCommandStatus::full_canvas_movie_primitive:return "full_canvas_movie_primitive";
    }return "unknown";
}
CinematicCommandEvidence classify_cinematic_movie_commands(std::span<const std::uint8_t> bytes,
    const NativeCinematicSnapshot& before,const NativeCinematicSnapshot& after,std::uint32_t width,std::uint32_t height) noexcept {
    using S=CinematicCommandStatus;CinematicCommandEvidence result{};
    if(!current(before)||!current(after)||!before.cg_fullscreen_known||!before.cg_fullscreen||!after.cg_fullscreen_known||!after.cg_fullscreen)return result;
    if(!same_values(before,after)||!before.thread_id||before.thread_id!=after.thread_id||!before.observed_ms||
       after.observed_ms<before.observed_ms||after.observed_ms-before.observed_ms>250){result.status=S::changed_snapshot;return result;}
    if(!width||!height||width>32768||height>32768){result.status=S::invalid_canvas;return result;}
    if(bytes.size()>0x180000){result.status=S::malformed_list;return result;}
    const auto census=render::census_hud_commands(bytes);result.consumed_bytes=census.consumed_bytes;
    if(census.status!=render::CommandListStatus::complete||census.malformed_layout_mask){result.status=S::malformed_list;return result;}
    // Only the observed native movie path plus explicit viewport/scissor.
    // In particular, no ID5 permission is granted to the shared HUD parser.
    constexpr unsigned allowed=(1u<<5)|(1u<<6)|(1u<<7)|(1u<<10)|(1u<<17)|(1u<<27);
    for(unsigned id=1;id<29;++id)if(census.active[id]&&!(allowed&(1u<<id))){result.status=S::unsupported_list;return result;}
    bool projection=false,viewport=true,scissor=true;
    for(std::size_t offset=0;offset<census.consumed_bytes;){
        const auto* p=bytes.data()+offset;const auto length=value<std::uint16_t>(p);const auto id=p[2];if(!id)break;
        if(p[3]&0x80){if(id==10&&length==48&&value<std::uint32_t>(p+4)==before.cinematic_material)++result.tagged_movie_primitives;offset+=length;continue;}
        if(id==5){
            if(length!=28||!p[4]||(p[4]&~7u)||!std::isfinite(value<float>(p+8))||value<float>(p+8)<0||value<float>(p+8)>1){result.status=S::malformed_list;return result;}
            for(unsigned i=12;i<28;i+=4)if(!std::isfinite(value<float>(p+i))){result.status=S::malformed_list;return result;}
            // A later color clear erases earlier positive movie coverage. This
            // is conservative when native debug draw mode masks that bit.
            if(p[4]&1)result.full_canvas_primitives=0;
        }
        if(id==27)projection=value<int>(p+4)==0;
        if(id==6)viewport=value<int>(p+4)==0&&value<int>(p+8)==0&&value<int>(p+12)==static_cast<int>(width)&&value<int>(p+16)==static_cast<int>(height);
        if(id==7)scissor=value<int>(p+4)==0||(value<int>(p+8)==0&&value<int>(p+12)==0&&value<int>(p+16)>=static_cast<int>(width)&&value<int>(p+20)>=static_cast<int>(height));
        if(id==10){
            // Any later unrelated image can cover the movie. Text may overlay
            // it, but arbitrary image shaders do not inherit positive proof.
            result.full_canvas_primitives=0;
            if(value<std::uint32_t>(p+4)==before.cinematic_material){
                ++result.movie_primitives;const auto x=value<float>(p+8),y=value<float>(p+12),w=value<float>(p+20),h=value<float>(p+24);
                result.last_x=x;result.last_y=y;result.last_width=w;result.last_height=h;
                const double right=double(x)+w,bottom=double(y)+h;
                // Native movie geometry scales with the source canvas: the
                // captured 1280x720 rectangle (-0.5,0,1281,720.000061) becomes
                // (-1.5,0,3843,2160) at 3840x2160. Keep the same bounded
                // allowance in reference-canvas units, not physical pixels.
                const double margin_x=double(width)/1280,margin_y=double(height)/720;
                const bool coverage=x>=-margin_x&&x<=0.001F&&y>=-margin_y&&y<=0.001F&&
                    right>=double(width)-0.001&&right<=double(width)+margin_x+0.001&&
                    bottom>=double(height)-0.001&&bottom<=double(height)+margin_y+0.001;
                const bool uv=value<float>(p+28)==0&&value<float>(p+32)==0&&value<float>(p+36)==1&&value<float>(p+40)==1;
                if(projection&&viewport&&scissor&&coverage&&uv&&value<float>(p+16)==1.F&&value<std::uint32_t>(p+44)==UINT32_MAX)++result.full_canvas_primitives;
            }
        }
        offset+=length;
    }
    result.status=result.full_canvas_primitives?S::full_canvas_movie_primitive:result.movie_primitives?S::partial_movie_primitive:S::no_movie_primitive;return result;
}
}


namespace bovr::t5 {
bool validate_cinematic_movie_guards(HMODULE image) noexcept {
 const auto base=reinterpret_cast<std::uintptr_t>(image);if(base!=0x400000||image!=GetModuleHandleW(nullptr))return false;



 __try {return !bovr::native_guard::compare(reinterpret_cast<void*>(base+0x2eb6ee), 109, "a56438bfd0bd336b71aef7467289cb018bd10318ce3bc9b7de597967e991978f")&&
!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x2e6d50), 133, "6b83039c9ac9ac4688100a64d62be314aacf8e72be4bf9791d7b604c2db5adff")&&
!bovr::native_guard::compare(reinterpret_cast<void*>(base+0x324ab0), 258, "9ff5275042cc53bd9d5e129c13897a36bdee7f940fc80bc621a57509faf63f80")&&cine_load<std::uint32_t>(base+0x74af3c)==base+0x2e6d50; }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
