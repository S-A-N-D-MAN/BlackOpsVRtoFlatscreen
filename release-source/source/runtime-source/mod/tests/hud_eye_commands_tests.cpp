// SPDX-License-Identifier: GPL-3.0-only
#include "render/hud_eye_commands.hpp"
#include "render/hud_commands.hpp"
#include "t5/hud_hind_material.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
namespace {
using namespace bovr::render;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value){std::printf("FAIL %s\n",why);std::exit(1);}}
template<class T>void put(std::vector<std::uint8_t>& bytes,std::size_t offset,T value){std::memcpy(bytes.data()+offset,&value,sizeof(value));}
template<class T>T get(const std::vector<std::uint8_t>& bytes,std::size_t offset){T v{};std::memcpy(&v,bytes.data()+offset,sizeof(v));return v;}
std::vector<std::uint8_t> command(unsigned id,unsigned length) {
    std::vector<std::uint8_t> bytes(length);put(bytes,0,static_cast<std::uint16_t>(length));bytes[2]=static_cast<std::uint8_t>(id);return bytes;
}
std::vector<std::uint8_t> image() {
    auto bytes=command(10,48);put(bytes,4,0x12345678u);put(bytes,8,100.0f);put(bytes,12,200.0f);put(bytes,16,1.0f);
    put(bytes,20,200.0f);put(bytes,24,100.0f);put(bytes,28,0.2f);put(bytes,32,0.3f);put(bytes,36,0.8f);put(bytes,40,0.9f);put(bytes,44,0x80000000u);return bytes;
}
std::vector<std::uint8_t> quad() {
    auto bytes=command(15,48);put(bytes,4,0x76543210u);
    const float xy[]{800,500,900,550,850,650,750,600};std::memcpy(bytes.data()+8,xy,sizeof(xy));put(bytes,40,1.0f);put(bytes,44,0xffffffffu);return bytes;
}
std::vector<std::uint8_t> text() {
    auto bytes=command(17,104);put(bytes,4,1100.0f);put(bytes,8,680.0f);put(bytes,12,1.0f);put(bytes,16,15.0f);
    put(bytes,20,0x11223344u);put(bytes,24,0.4f);put(bytes,28,0.5f);
    for(unsigned i=32;i<92;++i)bytes[i]=static_cast<std::uint8_t>(i+31);
    std::memcpy(bytes.data()+92,"HUD 123",8);return bytes;
}
void append(std::vector<std::uint8_t>& list,const std::vector<std::uint8_t>& value){list.insert(list.end(),value.begin(),value.end());}
std::vector<std::uint8_t> terminated(std::vector<std::uint8_t> bytes){append(bytes,command(0,4));return bytes;}
HudEyeCommands fit(const std::vector<std::uint8_t>& bytes,HudEyeViewport viewport={0,0,640,720}) {
    return fit_hud_commands_to_eye(bytes,1280,720,viewport);
}
bool equal(float a,float b){return std::fabs(a-b)<0.0001f;}
bool compass(void*,std::uint32_t material) noexcept{return material==0x76543210u;}
struct ReticlePolicy {
    static constexpr std::uint32_t material=0x13579bdf;
    unsigned queries{};bool accept=true;
    static bool resolve(void* context,std::uint32_t value) noexcept {
        auto& p=*static_cast<ReticlePolicy*>(context);++p.queries;
        return p.accept&&value==material;
    }
};
std::pair<float,float> center(const std::vector<std::uint8_t>& bytes,unsigned at,unsigned id) {
    float x{},y{};
    if(id==15)for(unsigned offset=8;offset<=32;offset+=8){x+=get<float>(bytes,at+offset)*.25f;y+=get<float>(bytes,at+offset+4)*.25f;}
    else {x=get<float>(bytes,at+8)+get<float>(bytes,at+20)*.5f;y=get<float>(bytes,at+12)+get<float>(bytes,at+24)*.5f;}
    return {x,y};
}
void hind_reticle_fit() {
    // Tangent extents give the optical axis independently of the fitted HUD:
    // X=wide/(wide+narrow) in the left eye, narrow/(wide+narrow) in the right;
    // Y=up/(up+down). None of these are the final texture's 50% coordinate.
    constexpr float wide=1.5f,narrow=1.f,up=1.f,down=1.4f;
    constexpr float sx=2*wide/(wide+narrow),sy=2*down/(up+down);
    const std::array<HudEyeDestination,2> destinations{{{0,1-sy,sx,sy},{1-sx,1-sy,sx,sy}}};
    HudVisibleRegion visible{};
    check(common_visible_hud_region(destinations,&visible),"Hind reticle fixture has a genuine common asymmetric visible region");
    for(unsigned id:{10u,12u,15u}) {
        auto primitive=id==15?quad():image();
        if(id==12){primitive.resize(52);put(primitive,0,std::uint16_t{52});primitive[2]=12;
            put(primitive,16,1328.35205f);put(primitive,48,37.5f);}
        put(primitive,4,ReticlePolicy::material);
        auto list=primitive;append(list,image());append(list,text());list=terminated(list);const auto original=list;
        for(const auto region:{HudVisibleRegion{},visible})for(unsigned eye=0;eye<2;++eye) {
            // Odd eye dimensions exercise the inward clip rounding and half pixel centre.
            const HudEyeViewport viewport{static_cast<int>(eye*643),0,643,719};
            const auto baseline=fit_hud_commands_to_eye(list,1286,719,viewport,region);
            ReticlePolicy policy;
            const auto adjusted=fit_hud_commands_to_eye(list,1286,719,viewport,region,{&policy,nullptr,ReticlePolicy::resolve});
            check(baseline.status==HudEyeStatus::accepted&&adjusted.status==HudEyeStatus::accepted&&
                adjusted.draws==3&&adjusted.centered_reticles==1&&policy.queries==2,
                "only verified reticle image/rotated image/quad is centered; ordinary image is queried but retained");
            check(adjusted.bytes.size()==baseline.bytes.size()&&adjusted.uniform_scale==baseline.uniform_scale&&
                adjusted.offset_x==baseline.offset_x&&adjusted.offset_y==baseline.offset_y,
                "reticle translation does not alter shared HUD fit or native record layout");
            bool other_bytes_equal=true;
            for(std::size_t at=0;at<baseline.bytes.size();++at) {
                const bool reticle_xy=at>=28+8&&at<28+(id==15?40:16);
                if(!reticle_xy&&baseline.bytes[at]!=adjusted.bytes[at])other_bytes_equal=false;
            }
            check(other_bytes_equal,"all non-XY reticle bytes, dimensions, depth, rotation, UV/color and other commands stay exact");
            const auto [cx,cy]=center(adjusted.bytes,28,id);
            const float source_x=(get<int>(adjusted.bytes,4)-viewport.x+cx)/static_cast<float>(viewport.width);
            const float source_y=(get<int>(adjusted.bytes,8)-viewport.y+cy)/static_cast<float>(viewport.height);
            check(equal(source_x,.5f)&&equal(source_y,.5f),"clip-local reticle center reconstructs the source eye optical axis in both eyes");
            const auto& d=destinations[eye];
            const float final_x=d.x+d.width*source_x,final_y=d.y+d.height*source_y;
            check(equal(final_x,(eye?narrow:wide)/(wide+narrow))&&equal(final_y,up/(up+down))&&
                std::fabs(final_x-.5f)>.05f&&std::fabs(final_y-.5f)>.05f,
                "existing asymmetric compositor maps reticle to optical axis, not final-output center");
            const auto [old_x,old_y]=center(baseline.bytes,28,id);
            check(!equal(cx,old_x)||!equal(cy,old_y),"off-axis source reticle actually moves");
            if(id==15)for(unsigned at=16;at<=32;at+=8) {
                check(equal(get<float>(adjusted.bytes,28+at)-get<float>(adjusted.bytes,36),
                            get<float>(baseline.bytes,28+at)-get<float>(baseline.bytes,36))&&
                      equal(get<float>(adjusted.bytes,28+at+4)-get<float>(adjusted.bytes,40),
                            get<float>(baseline.bytes,28+at+4)-get<float>(baseline.bytes,40)),
                    "quad corner deltas preserve size, shape and rotation under common translation");
            }
            policy={};policy.accept=false;
            const auto refused=fit_hud_commands_to_eye(list,1286,719,viewport,region,{&policy,nullptr,ReticlePolicy::resolve});
            const auto absent=fit_hud_commands_to_eye(list,1286,719,viewport,region,{&policy,nullptr,nullptr});
            check(refused.status==baseline.status&&refused.bytes==baseline.bytes&&!refused.centered_reticles&&
                absent.bytes==baseline.bytes&&!absent.centered_reticles&&policy.queries==2,
                "false or absent reticle proof preserves original fitted bytes exactly");
            check(list==original,"all reticle forms leave native source bytes unchanged");
        }
    }
    auto overlay=image();put(overlay,4,ReticlePolicy::material);put(overlay,8,0.f);put(overlay,12,0.f);
    put(overlay,20,1280.f);put(overlay,24,720.f);overlay=terminated(overlay);
    ReticlePolicy policy;
    const auto rejected=fit_hud_commands_to_eye(overlay,1280,720,{0,0,640,720},{},{&policy,nullptr,ReticlePolicy::resolve});
    check(rejected.status==HudEyeStatus::ambiguous_overlay&&rejected.bytes.empty()&&!policy.queries&&!rejected.centered_reticles,
        "reticle resolver never bypasses whole-list overlay refusal");
    auto malformed=terminated(image());put(malformed,8,std::numeric_limits<float>::quiet_NaN());policy={};
    const auto bad=fit_hud_commands_to_eye(malformed,1280,720,{0,0,640,720},{},{&policy,nullptr,ReticlePolicy::resolve});
    check(bad.status!=HudEyeStatus::accepted&&bad.bytes.empty()&&!policy.queries,
        "malformed native geometry refuses before the reticle callback");
}
void hind_material_reader() {
    struct Reader {
        std::array<std::uint8_t,192> material{};
        std::array<char,14> name{};
        unsigned calls{},fail_at{};bool drift{},throws{};
        Reader(){const std::uint32_t p=0x12000000;std::memcpy(material.data(),&p,4);
            for(unsigned i=4;i<material.size();++i)material[i]=static_cast<std::uint8_t>(i);
            std::memcpy(name.data(),"hind_reticle",sizeof("hind_reticle"));}
        bool operator()(std::uint32_t address,void* out,std::size_t size) {
            ++calls;if(throws)throw 1;if(calls==fail_at)return false;
            if(address==0x11000000&&size==material.size())std::memcpy(out,material.data(),size);
            else if(address==0x12000000&&size==sizeof("hind_reticle"))std::memcpy(out,name.data(),size);
            else return false;
            if(drift&&calls==1)material.back()^=1;return true;
        }
    };
    Reader r;const auto material=r.material;const auto name=r.name;
    check(bovr::t5::is_hud_hind_material(0x11000000,r)&&r.calls==4&&r.material==material&&r.name==name,
        "actual Hind resolver double-copies full192-byte material and exact terminated name without mutation");
    for(unsigned failure=1;failure<=4;++failure){r=Reader{};r.fail_at=failure;
        check(!bovr::t5::is_hud_hind_material(0x11000000,r),"each required material/name read is mandatory");}
    r=Reader{};r.name[0]='H';check(!bovr::t5::is_hud_hind_material(0x11000000,r),"case-different material name is not exact Hind reticle");
    r=Reader{};r.name[12]='X';check(!bovr::t5::is_hud_hind_material(0x11000000,r),"longer material name with Hind prefix is refused");
    r=Reader{};r.drift=true;check(!bovr::t5::is_hud_hind_material(0x11000000,r),"last material-header byte drifting between copies invalidates identity");
    r=Reader{};r.throws=true;check(!bovr::t5::is_hud_hind_material(0x11000000,r),"reader exception cannot grant cosmetic material proof");
    for(const std::uint32_t pointer:{0xffffu,0xffffff80u}){r=Reader{};
        check(!bovr::t5::is_hud_hind_material(pointer,r)&&!r.calls,"low or overflowing material span refuses before read");}
    r=Reader{};const std::uint32_t low=0xffff;std::memcpy(r.material.data(),&low,4);
    check(!bovr::t5::is_hud_hind_material(0x11000000,r)&&r.calls==1,"low native name pointer refused after bounded header copy");
}
}
int main() {
    auto source=image();append(source,quad());append(source,text());source=terminated(source);const auto original=source;
    const auto left=fit(source),right=fit(source,{640,0,640,720});
    check(left.status==HudEyeStatus::accepted&&right.status==HudEyeStatus::accepted&&left.draws==3&&right.draws==3,"both eyes accept the same validated native image/quad/text list");
    check(source==original,"native source bytes remain immutable");
    check(equal(left.uniform_scale,0.5f)&&equal(left.offset_x,0)&&equal(left.offset_y,180),"full native canvas fits without changing aspect");
    check(left.bytes.size()==source.size()+28,"only viewport/projection prefix added");
    check(get<std::uint16_t>(left.bytes,0)==20&&left.bytes[2]==6&&get<std::int32_t>(left.bytes,12)==640&&get<std::int32_t>(left.bytes,16)==720,
          "native viewport record installs each eye's actual dimensions");
    check(get<std::int32_t>(right.bytes,4)==640&&get<std::int32_t>(left.bytes,4)==0,"hardware viewport carries the right eye origin");
    check(left.bytes[22]==27&&get<std::uint32_t>(left.bytes,24)==0,"native projection2D command follows viewport cache invalidation");
    check(std::equal(left.bytes.begin()+28,left.bytes.end(),right.bytes.begin()+28),"right geometry remains eye-local with no double origin translation");
    const unsigned image_at=28,quad_at=76,text_at=124;
    check(equal(get<float>(left.bytes,image_at+8),50)&&equal(get<float>(left.bytes,image_at+12),280)&&
          equal(get<float>(left.bytes,image_at+20),100)&&equal(get<float>(left.bytes,image_at+24),50),"image XY and dimensions receive uniform fit");
    check(get<std::uint32_t>(left.bytes,image_at+4)==0x12345678&&get<std::uint32_t>(left.bytes,image_at+44)==0x80000000&&
          equal(get<float>(left.bytes,image_at+16),1)&&std::memcmp(left.bytes.data()+image_at+28,original.data()+28,16)==0,
          "material, homogeneous W, UVs and alpha/black color remain exact");
    for(unsigned offset=8;offset<=32;offset+=8) {
        check(equal(get<float>(left.bytes,quad_at+offset),get<float>(source,48+offset)*0.5f)&&
              equal(get<float>(left.bytes,quad_at+offset+4),get<float>(source,48+offset+4)*0.5f+180),"each rotated quad corner keeps the same uniform transform");
    }
    check(equal(get<float>(left.bytes,quad_at+40),1)&&get<std::uint32_t>(left.bytes,quad_at+44)==0xffffffff,"quad W/color are not treated as coordinates");
    check(equal(get<float>(left.bytes,text_at+4),550)&&equal(get<float>(left.bytes,text_at+8),520)&&
          equal(get<float>(left.bytes,text_at+24),0.2f)&&equal(get<float>(left.bytes,text_at+28),0.25f),"text position and both font scales use the same fit");
    check(std::memcmp(left.bytes.data()+text_at+32,source.data()+96+32,104-32)==0&&
          get<std::uint32_t>(left.bytes,text_at+20)==0x11223344&&equal(get<float>(left.bytes,text_at+16),15),
          "inline text, font identity, rotation, timing and native style bytes remain exact");
    const auto census=census_hud_commands(left.bytes);
    check(census.status==CommandListStatus::complete&&census.all_active_layouts_recognized&&census.active[6]==1&&census.active[27]==1&&census.active[0]==1,
          "generated native records remain a complete bounded list");
    for(const unsigned id:{1u,5u,6u,7u,8u,9u,11u,12u,13u,14u,16u,18u,21u,22u,23u,24u,25u,26u,28u}) {
        auto bad=image();append(bad,command(id,4));bad=terminated(bad);auto before=bad;const auto result=fit(bad);
        check(result.status!=HudEyeStatus::accepted&&result.bytes.empty()&&bad==before,"unsupported command rejects the whole list before output/native mutation");
    }
    auto unknown=image();append(unknown,command(8,4));unknown=terminated(unknown);
    const auto unknown_rejection=fit(unknown);
    check(unknown_rejection.status==HudEyeStatus::unsupported_command&&unknown_rejection.bytes.empty()&&
          unknown_rejection.rejected_record&&unknown_rejection.rejected_id==8&&unknown_rejection.rejected_offset==48&&
          unknown_rejection.rejected_length==4&&!unknown_rejection.rejected_word4&&!unknown_rejection.rejected_material,
          "bounded rejection telemetry identifies header-only command without reading another record");
    auto opaque=command(8,8);put(opaque,4,0x12345678u);opaque=terminated(opaque);
    const auto opaque_rejection=fit(opaque);
    check(opaque_rejection.rejected_record&&opaque_rejection.rejected_offset==0&&opaque_rejection.rejected_word4==0x12345678u&&
          !opaque_rejection.rejected_material&&opaque_rejection.bytes.empty(),"unknown payload remains opaque diagnostic data rather than material authority");
    auto tagged=image();auto skipped=command(8,4);skipped[3]=0x80;append(tagged,skipped);tagged=terminated(tagged);
    auto tag_result=fit(tagged);
    check(tag_result.status==HudEyeStatus::accepted&&tag_result.skipped_tagged==1&&tag_result.bytes.size()==48+4+28,
          "native null-window tagged callback remains skipped rather than replayed");
    auto bad=source;put(bad,0,std::uint16_t{3});check(fit(bad).status==HudEyeStatus::invalid_list,"unaligned malformed native length rejected");
    bad=source;bad.pop_back();check(fit(bad).status==HudEyeStatus::invalid_list,"truncated terminator rejected");
    bad=source;append(bad,command(0,4));check(fit(bad).status==HudEyeStatus::invalid_list,"caller must supply one exact list extent");
    for(unsigned offset:{8u,12u,16u,20u,24u,28u,32u,36u,40u}) {
        bad=source;put(bad,offset,std::numeric_limits<float>::quiet_NaN());check(fit(bad).status==HudEyeStatus::invalid_list,"nonfinite image input cannot produce native output");
    }
    auto overlay=image();put(overlay,8,0.0f);put(overlay,12,0.0f);put(overlay,20,1280.0f);put(overlay,24,720.0f);
    check(fit(terminated(overlay)).status==HudEyeStatus::ambiguous_overlay,"full-canvas image/fade is not silently fitted into a partial eye strip");
    auto full_quad=quad();const float corners[]{0,0,1280,0,1280,720,0,720};std::memcpy(full_quad.data()+8,corners,sizeof(corners));
    check(fit(terminated(full_quad)).status==HudEyeStatus::ambiguous_overlay,"full-canvas quad also needs deliberate cover-eye classification");
    auto massive=image();put(massive,8,1.0e30f);check(fit(terminated(massive)).status==HudEyeStatus::unsupported_geometry,"finite but unbounded native geometry rejected");
    check(fit(terminated(command(27,8))).status==HudEyeStatus::no_draws,"projection-only list cannot masquerade as visible HUD");
    for(HudEyeViewport invalid: {HudEyeViewport{-1,0,640,720},HudEyeViewport{0,0,0,720},HudEyeViewport{0,0,640,-1},HudEyeViewport{16300,0,640,720}})
        check(fit(source,invalid).status==HudEyeStatus::invalid_canvas,"invalid/overflowing eye viewport rejected");
    check(fit_hud_commands_to_eye(source,0,720,{0,0,640,720}).status==HudEyeStatus::invalid_canvas,"zero source canvas rejected");
    const auto portrait=fit_hud_commands_to_eye(source,1280,720,{0,0,720,640});
    check(portrait.status==HudEyeStatus::accepted&&equal(portrait.uniform_scale,0.5625f)&&equal(portrait.offset_y,117.5f),"alternate aspect keeps one uniform scale and centered fit");
    for(auto primitive:{image(),quad()}) {
        const unsigned id=primitive[2],w_offset=id==10?16:id==15?40:12;
        put(primitive,w_offset,2.0f);const auto bytes=terminated(primitive);
        check(fit(bytes).status==HudEyeStatus::unsupported_geometry,"non-unit homogeneous W rejects before native output");
    }
    HudVisibleRegion visible{};
    const float wide=std::tan(54.0f*3.14159265358979323846f/180),narrow=std::tan(40.0f*3.14159265358979323846f/180);
    const float up=std::tan(43.98f*3.14159265358979323846f/180),down=std::tan(54.27f*3.14159265358979323846f/180);
    const float sx=2*wide/(wide+narrow),sy=2*down/(up+down);
    const std::array<HudEyeDestination,2> quest{{{0,1-sy,sx,sy},{1-sx,1-sy,sx,sy}}};
    check(common_visible_hud_region(quest,&visible),"actual Quest3 asymmetric profile admits common visible source region");
    check(equal(visible.x*2+visible.width,1)&&equal(visible.y*2+visible.height,1),"common region remains centered rather than divergent per-eye placement");
    check(sx*(1133.0f*0.5f/640)>1&&1-sx+sx*(1133.0f*0.5f/640)<1,"captured V13 score coordinate reproduces left-only clipping under exact profile mapping");
    std::array<HudEyeCommands,2> visible_commands;
    for(unsigned eye=0;eye<2;++eye) {
        visible_commands[eye]=fit_hud_commands_to_eye(source,1280,720,{static_cast<int>(eye*640),0,640,720},visible);
        check(visible_commands[eye].status==HudEyeStatus::accepted,"both eyes use same visible-region command fit");
        const auto& bytes=visible_commands[eye].bytes;
        const float x=static_cast<float>(get<int>(bytes,4)-eye*640)/640,y=static_cast<float>(get<int>(bytes,8))/720;
        const float w=static_cast<float>(get<int>(bytes,12))/640,h=static_cast<float>(get<int>(bytes,16))/720;
        for(const auto& d:quest)for(float px:{x,x+w})for(float py:{y,y+h})
            check(d.x+d.width*px>=-0.000001f&&d.x+d.width*px<=1.000001f&&d.y+d.height*py>=-0.000001f&&d.y+d.height*py<=1.000001f,"every native clipping-rectangle corner survives both compositor destinations");
    }
    check(std::equal(visible_commands[0].bytes.begin()+28,visible_commands[0].bytes.end(),visible_commands[1].bytes.begin()+28),"logical HUD geometry stays identical between eyes");
    for(auto bad_destination:{HudEyeDestination{0,0,0,1},HudEyeDestination{0,0,1,-1},HudEyeDestination{1,0,1,1},
         HudEyeDestination{0,0,std::numeric_limits<float>::quiet_NaN(),1},HudEyeDestination{0,std::numeric_limits<float>::infinity(),1,1}})
        check(!common_visible_hud_region({quest[0],bad_destination},&visible),"both destination dimensions and visible centered intersection must be valid");
    check(!common_visible_hud_region(quest,nullptr),"missing output rejected");
    check(common_visible_hud_region({HudEyeDestination{0,0,1,1},HudEyeDestination{0,0,1,1}},&visible)&&equal(visible.width,1)&&equal(visible.height,1),"symmetric projection retains original full source region");
    auto rotated=image();rotated.resize(52);put(rotated,0,std::uint16_t{52});rotated[2]=12;
    put(rotated,16,1328.35205f);put(rotated,48,263.3045f);put(rotated,44,0xfffff000u);
    const auto rotation_source=terminated(rotated),rotation_original=rotation_source;
    const auto rotation_fit=fit(rotation_source);
    check(rotation_fit.status==HudEyeStatus::accepted&&rotation_fit.draws==1,"native rotated objective arrow accepts positive nonunit W");
    check(get<float>(rotation_fit.bytes,28+8)==50&&get<float>(rotation_fit.bytes,28+12)==280&&
          get<float>(rotation_fit.bytes,28+20)==100&&get<float>(rotation_fit.bytes,28+24)==50,"rotated rectangle uses uniform XYWH fit before native rotation");
    for(unsigned at:{16u,28u,32u,36u,40u,44u,48u})
        check(get<std::uint32_t>(rotation_fit.bytes,28+at)==get<std::uint32_t>(rotation_source,at),"rotation, depth, UV and opaque color preserved bit for bit");
    check(rotation_source==rotation_original,"rotated source remains untouched");
    auto objective_text=text();put(objective_text,12,1870.87976f);
    const auto objective_fit=fit(terminated(objective_text));
    check(objective_fit.status==HudEyeStatus::accepted&&get<float>(objective_fit.bytes,28+12)==get<float>(objective_text,12)&&
          get<float>(objective_fit.bytes,28+4)==550&&get<float>(objective_fit.bytes,28+8)==520,"objective text retains native depth with pixel fit");
    for(float w:{0.f,-1.f,1.0e30f,std::numeric_limits<float>::denorm_min()}) {auto b=objective_text;put(b,12,w);check(fit(terminated(b)).bytes.empty(),"invalid objective text depth rejects");}
    for(float angle:{-1.f,360.f,1.0e30f}) {auto b=rotated;put(b,48,angle);check(fit(terminated(b)).bytes.empty(),"unbounded/non-normalized rotation rejects");}
    for(float w:{0.f,-1.f,1.0e30f,std::numeric_limits<float>::denorm_min()}) {auto b=rotated;put(b,16,w);check(fit(terminated(b)).bytes.empty(),"unsafe rotated depth rejects");}
    auto rotated_overlay=rotated;put(rotated_overlay,8,0.f);put(rotated_overlay,12,0.f);put(rotated_overlay,20,1280.f);put(rotated_overlay,24,720.f);put(rotated_overlay,48,0.f);
    check(fit(terminated(rotated_overlay)).status==HudEyeStatus::ambiguous_overlay,"full-canvas rotated command remains excluded");
    auto rect=command(1,24);put(rect,4,183u);put(rect,8,1139.f);put(rect,12,577.5f);put(rect,16,156.f);put(rect,20,156.f);
    auto compass_list=rect;append(compass_list,quad());compass_list=terminated(compass_list);const auto compass_original=compass_list;
    auto fit_compass=[&](const auto& list,HudEyeViewport view,HudVisibleRegion region=HudVisibleRegion{}) {
        return fit_hud_commands_to_eye(list,1280,720,view,region,{nullptr,compass});
    };
    const auto cl=fit_compass(compass_list,{0,0,640,720}),cr=fit_compass(compass_list,{640,0,640,720});
    check(cl.status==HudEyeStatus::accepted&&cr.status==HudEyeStatus::accepted&&cl.draws==1,"exact verified final compass constant/draw pair accepts both eyes");
    check(get<float>(cl.bytes,36)==569.5f&&get<float>(cr.bytes,36)==1209.5f&&get<float>(cl.bytes,40)==468.75f&&get<float>(cr.bytes,40)==468.75f,
          "VPOS rectangle includes right eye origin while Y includes centered aspect fit");
    check(get<float>(cl.bytes,44)==78&&get<float>(cl.bytes,48)==78,"VPOS rectangle dimensions scale uniformly");
    check(std::memcmp(cl.bytes.data()+52,cr.bytes.data()+52,48)==0,"compass quad geometry remains eye local");
    check(cl.bytes.size()==compass_list.size()+28+24&&std::memcmp(cl.bytes.data()+100,rect.data(),24)==0&&
          std::memcmp(cr.bytes.data()+100,rect.data(),24)==0,"native original constant restored after draw flush before end");
    check(compass_list==compass_original,"native compass list untouched");
    const auto clipped_compass=fit_compass(compass_list,{640,20,640,720},{.1f,.1f,.8f,.8f});
    const auto& cb=clipped_compass.bytes;
    check(clipped_compass.status==HudEyeStatus::accepted&&
          equal(get<float>(cb,36),1139.f*clipped_compass.uniform_scale+clipped_compass.offset_x+get<int>(cb,4))&&
          equal(get<float>(cb,40),577.5f*clipped_compass.uniform_scale+clipped_compass.offset_y+get<int>(cb,8)),"VPOS uses clipped absolute viewport origin including vertical offset");
    const auto unproved_compass=fit(compass_list);
    check(unproved_compass.status==HudEyeStatus::unsupported_command,"constant cannot replay without native shader proof");
    check(unproved_compass.rejected_record&&unproved_compass.rejected_id==1&&unproved_compass.rejected_word4==183&&
          !unproved_compass.rejected_material,"constant telemetry identifies native index without calling it a material");
    for(unsigned index:{0u,182u,184u,197u,0xffffffffu}) {auto b=compass_list;put(b,4,index);check(fit_compass(b,{0,0,640,720}).bytes.empty(),"unproved custom constant index rejects");}
    for(unsigned at:{16u,20u})for(float value:{0.f,-1.f,1.0e30f}) {auto b=compass_list;put(b,at,value);check(fit_compass(b,{0,0,640,720}).bytes.empty(),"invalid reciprocal rectangle dimensions reject");}
    auto wrong_material=compass_list;put(wrong_material,24+4,0x76543211u);check(fit_compass(wrong_material,{0,0,640,720}).bytes.empty(),"different material has no constant meaning inferred");
    auto later_draw=rect;append(later_draw,quad());append(later_draw,image());
    check(fit_compass(terminated(later_draw),{0,0,640,720}).status==HudEyeStatus::accepted,
          "verified compass can precede later supported draw with scoped native constant restoration");
    auto two_rects=rect;append(two_rects,rect);append(two_rects,quad());check(fit_compass(terminated(two_rects),{0,0,640,720}).bytes.empty(),"multiple custom constants remain unsupported");
    struct CompassProof {unsigned calls{};bool accepts{true};};
    const auto proof=[](void* context,std::uint32_t material) noexcept {
        auto& p=*static_cast<CompassProof*>(context);++p.calls;
        return p.accepts&&material==0x76543210u;
    };
    const auto diagnosed=[&](const auto& list,CompassProof& p) {
        return fit_hud_commands_to_eye(list,1280,720,{0,0,640,720},{},{&p,proof});
    };
    {
        CompassProof p;const auto result=diagnosed(compass_list,p);
        check(result.status==HudEyeStatus::accepted&&result.bytes==cl.bytes&&p.calls==1&&
              result.compass_rejection==HudCompassRejection::none&&result.compass_proof_called&&result.compass_proved,
              "diagnostics preserve accepted compass bytes and exactly one material proof");
    }
    check(unproved_compass.compass_rejection==HudCompassRejection::missing_policy&&
          unproved_compass.compass_next_present&&unproved_compass.compass_next_id==15&&
          unproved_compass.compass_next_material==0x76543210u&&unproved_compass.compass_after_present&&
          unproved_compass.compass_after_id==0&&!unproved_compass.compass_proof_called&&!unproved_compass.compass_proved,
          "missing material policy is distinct from a failed proof and reports exact following records");
    {
        CompassProof p;p.accepts=false;const auto result=diagnosed(compass_list,p);
        check(result.status==HudEyeStatus::unsupported_command&&result.bytes.empty()&&p.calls==1&&
              result.compass_rejection==HudCompassRejection::material_unproved&&result.compass_proof_called&&!result.compass_proved,
              "actual material rejection is reported without a second probe");
    }
    for(const auto& trial:{std::pair{terminated(two_rects),HudCompassRejection::draw_not_quad},
                          std::pair{terminated(rect),HudCompassRejection::draw_not_quad}}) {
        CompassProof p;const auto before=trial.first;const auto result=diagnosed(trial.first,p);
        check(result.status==HudEyeStatus::unsupported_command&&result.bytes.empty()&&trial.first==before&&p.calls==0&&
              result.compass_rejection==trial.second&&!result.compass_proof_called&&!result.compass_proved,
              "order diagnostics preserve short-circuit rejection without material reads or input writes");
    }
    {
        CompassProof p;const auto result=diagnosed(terminated(later_draw),p);
        check(result.status==HudEyeStatus::accepted&&result.compass_rejection==HudCompassRejection::none&&
              result.compass_next_id==15&&result.compass_next_material==0x76543210u&&
              result.compass_after_present&&result.compass_after_id==10&&p.calls==1,
              "nonfinal compass retains exact native shader proof and actual following record identity");
    }
    {
        CompassProof p;const auto result=diagnosed(terminated(rect),p);
        check(result.compass_next_present&&result.compass_next_id==0&&!result.compass_next_material&&
              !result.compass_after_present,"terminal-only next command is never read as a material or beyond the list");
    }
    {
        auto bad_index=compass_list;put(bad_index,4,184u);CompassProof p;const auto result=diagnosed(bad_index,p);
        check(result.compass_rejection==HudCompassRejection::wrong_index&&p.calls==0&&
              result.compass_next_id==15&&result.compass_after_id==0,"wrong constant index remains the first failed guard");
    }
    {
        auto tagged_order=rect;auto ignored=command(8,4);ignored[3]=0x80;append(tagged_order,ignored);
        append(tagged_order,quad());append(tagged_order,ignored);append(tagged_order,image());
        CompassProof p;const auto result=diagnosed(terminated(tagged_order),p);
        CompassProof q;const auto untagged=diagnosed(terminated(later_draw),q);
        check(result.status==HudEyeStatus::accepted&&result.compass_rejection==HudCompassRejection::none&&
              result.compass_next_id==15&&result.compass_next_material==0x76543210u&&result.compass_after_id==10&&p.calls==1&&
              result.bytes==untagged.bytes&&q.calls==1,
              "scoped restoration uses active records and safely omits tagged opaque headers");
    }
    {
        auto malformed=compass_list;put(malformed,24,std::uint16_t{4});CompassProof p;const auto result=diagnosed(malformed,p);
        check(result.status==HudEyeStatus::invalid_list&&!result.rejected_record&&!result.compass_next_present&&p.calls==0,
              "malformed following record fails census before diagnostic neighbor reads");
    }
    {
        // Retail slingshot failure: constant183 -> compass quad -> ordinary
        // image (ID10), with objective text also using the same native list.
        auto list=image();append(list,rect);append(list,quad());append(list,image());append(list,text());list=terminated(list);
        const auto original=list;
        for(const HudEyeViewport view:{HudEyeViewport{0,0,640,720},HudEyeViewport{640,0,640,720}}) {
            const auto result=fit_compass(list,view);
            check(result.status==HudEyeStatus::accepted&&result.draws==4&&result.bytes.size()==list.size()+28+24&&list==original,
                  "retail-order compass and later image/text compose both eyes with bounded copy and no source writes");
            const unsigned compass_at=28+48+24,restore_at=compass_at+48,later_at=restore_at+24;
            check(result.bytes[compass_at+2]==15&&result.bytes[restore_at+2]==1&&result.bytes[later_at+2]==10&&
                  std::memcmp(result.bytes.data()+restore_at,rect.data(),24)==0,
                  "original native constant is restored immediately after proved quad before the next draw");
            auto tail=image();append(tail,text());tail=terminated(tail);const auto tail_fit=fit(tail,view);
            check(std::memcmp(result.bytes.data()+later_at,tail_fit.bytes.data()+28,tail_fit.bytes.size()-28)==0,
                  "later image and text retain their independently fitted bytes and original native constant state");
            auto prefix=terminated(image());const auto prefix_fit=fit(prefix,view);
            check(std::memcmp(result.bytes.data()+28,prefix_fit.bytes.data()+28,48)==0,
                  "draw before compass retains its original fitted bytes");
        }
    }
    {
        auto repeated=rect;append(repeated,quad());append(repeated,rect);append(repeated,quad());repeated=terminated(repeated);
        CompassProof p;const auto result=diagnosed(repeated,p);
        check(result.status==HudEyeStatus::unsupported_command&&result.bytes.empty()&&
              result.compass_rejection==HudCompassRejection::prior_constant&&result.rejected_offset==72&&p.calls==1&&
              !result.compass_proof_called&&!result.compass_proved,
              "second constant after verified pair rejects whole list before output without a second material query");
    }
    {
        auto unknown_after=rect;append(unknown_after,quad());append(unknown_after,command(8,4));unknown_after=terminated(unknown_after);
        CompassProof p;const auto result=diagnosed(unknown_after,p);
        check(result.status==HudEyeStatus::unsupported_command&&result.bytes.empty()&&result.rejected_id==8&&p.calls==1,
              "scoped constant does not admit unsupported later commands or partially execute the prefix");
        auto overlay_after=rect;append(overlay_after,quad());append(overlay_after,overlay);overlay_after=terminated(overlay_after);
        CompassProof q;const auto rejected=diagnosed(overlay_after,q);
        check(rejected.status==HudEyeStatus::ambiguous_overlay&&rejected.bytes.empty()&&rejected.rejected_id==10&&q.calls==1,
              "ambiguous later full canvas overlay still rejects the entire list");
        p={};p.accepts=false;const auto unproved=diagnosed(terminated(later_draw),p);
        check(unproved.status==HudEyeStatus::unsupported_command&&unproved.bytes.empty()&&
              unproved.compass_rejection==HudCompassRejection::material_unproved&&p.calls==1,
              "later supported draws cannot bypass strict compass shader identity");
    }
    hind_reticle_fit();hind_material_reader();
    std::printf("hud_eye_command_checks=%u failures=0 native_calls=0 source_mutations=0\n",checks);return 0;
}
