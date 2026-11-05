// SPDX-License-Identifier: GPL-3.0-only
#include "render/hud_commands.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
using namespace bovr::render;
namespace {
unsigned checks{};void check(bool pass,const char* why){++checks;if(!pass){std::printf("FAIL %s\n",why);std::exit(1);}}
template<class T>void put(std::vector<std::uint8_t>& bytes,std::size_t offset,T value){std::memcpy(bytes.data()+offset,&value,sizeof(T));}
std::vector<std::uint8_t> record(unsigned id,unsigned size,unsigned flags=0) {
    std::vector<std::uint8_t> result(size);put(result,0,static_cast<std::uint16_t>(size));result[2]=static_cast<std::uint8_t>(id);result[3]=static_cast<std::uint8_t>(flags);return result;
}
void end(std::vector<std::uint8_t>& bytes){const auto e=record(0,4);bytes.insert(bytes.end(),e.begin(),e.end());}
}
int main(){
    check(census_hud_commands({}).status==CommandListStatus::empty,"empty array rejects");
    auto bytes=record(0,4);check(census_hud_commands(bytes).status==CommandListStatus::complete,"complete empty native list accepts");
    for(unsigned bad=0;bad<4;++bad){auto malformed=record(10,48);put(malformed,0,static_cast<std::uint16_t>(bad));
        check(census_hud_commands(malformed).status==CommandListStatus::invalid_length,"short/non-progressing header rejected");}
    auto malformed=record(10,48);put(malformed,0,std::uint16_t{46});check(census_hud_commands(malformed).status==CommandListStatus::invalid_length,"nonaligned length rejects");
    malformed=record(10,48);put(malformed,0,std::uint16_t{65532});check(census_hud_commands(malformed).status==CommandListStatus::overrun,"oversized command cannot read out of copied bytes");
    malformed=record(29,4);check(census_hud_commands(malformed).status==CommandListStatus::invalid_id,"dispatch index29 rejects");
    malformed={4,0,10};check(census_hud_commands(malformed).status==CommandListStatus::truncated_header,"truncated header rejects");
    malformed=record(3,4);check(census_hud_commands(malformed).status==CommandListStatus::missing_terminator,"no inferred terminator past used bytes");
    malformed=record(0,8);check(census_hud_commands(malformed).status==CommandListStatus::invalid_length,"known end form requires exact4bytes");
    bytes=record(10,48);put(bytes,4,std::uint32_t{0x1234});
    put(bytes,8,100.f);put(bytes,12,200.f);put(bytes,16,1.f);put(bytes,20,80.f);put(bytes,24,40.f);end(bytes);
    const auto original=bytes;auto result=census_hud_commands(bytes);
    check(result.status==CommandListStatus::complete&&result.all_active_layouts_recognized&&result.active[10]==1&&result.records==2,"exact native image shape recognized");
    check(bytes==original,"census preserves every source byte");
    for(unsigned offset:{8u,12u,16u,20u,24u,28u,32u,36u,40u}){auto invalid=bytes;put(invalid,offset,std::numeric_limits<float>::quiet_NaN());
        check(census_hud_commands(invalid).malformed_layout_mask==(1u<<10),"nonfinite image value rejected without dereferencing material");}
    auto invalid=bytes;put(invalid,16,0.f);check(census_hud_commands(invalid).malformed_layout_mask==(1u<<10),"homogeneous W is independently validated at+10");
    invalid=bytes;put(invalid,20,-1.f);check(census_hud_commands(invalid).malformed_layout_mask==(1u<<10),"pixel width is+14 and conservative layout rejects negative width");
    // Exact retail ID15 is a four-corner image, not the same-sized ID10 image.
    // A rotated/offscreen shape with zero second X is legal copied layout data.
    auto quad=record(15,48);put(quad,4,std::uint32_t{0xfffffff0});
    const float corners[]{-20.f,0.f,0.f,-10.f,30.f,0.f,0.f,40.f};
    std::memcpy(quad.data()+8,corners,sizeof(corners));put(quad,40,1.f);
    put(quad,44,std::uint32_t{0x7fc00000}); // Packed color is not a float/NaN.
    end(quad);const auto original_quad=quad;result=census_hud_commands(quad);
    check(result.status==CommandListStatus::complete&&result.active[15]==1&&result.records==2&&
          result.shortest[15]==48&&result.longest[15]==48&&result.consumed_bytes==52&&
          result.all_active_layouts_recognized&&!result.unsupported_layout_mask&&!result.malformed_layout_mask,
          "exact four-corner quad recognized without dereferencing material or interpreting color");
    check(quad==original_quad,"quad census preserves every copied byte");
    for(unsigned offset:{8u,12u,16u,20u,24u,28u,32u,36u,40u}) {
        for(float nonfinite:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
            auto bad=quad;put(bad,offset,nonfinite);const auto r=census_hud_commands(bad);
            check(r.status==CommandListStatus::complete&&r.malformed_layout_mask==(1u<<15)&&
                  !r.unsupported_layout_mask&&!r.all_active_layouts_recognized,
                  "each quad coordinate and homogeneous W rejects nonfinite values");
        }
    }
    for(float bad_w:{0.f,-0.f,-1.f}) {auto bad=quad;put(bad,40,bad_w);
        check(census_hud_commands(bad).malformed_layout_mask==(1u<<15),"quad W must be positive at+28, not the second XY pair");}
    auto bad_quad=quad;put(bad_quad,4,std::uint32_t{0});
    check(census_hud_commands(bad_quad).malformed_layout_mask==(1u<<15),"null quad material rejected as a layout prerequisite");
    for(unsigned size:{4u,44u,52u}) {auto bad=record(15,size);end(bad);const auto r=census_hud_commands(bad);
        check(r.status==CommandListStatus::complete&&r.malformed_layout_mask==(1u<<15)&&!r.unsupported_layout_mask,
              "quad requires exact48bytes before any field reads");}
    auto rotate=record(12,52);put(rotate,4,std::uint32_t{0x9a7f0000});
    const float rotate_floats[]{8.f,16.f,32.f,40.f,48.f,56.f,64.f,72.f,80.f,88.f};
    std::memcpy(rotate.data()+8,rotate_floats,sizeof(rotate_floats));
    put(rotate,44,0x7fc00000u);
    put(rotate,48,1.75f);
    end(rotate);const auto original_rotate=rotate;result=census_hud_commands(rotate);
    check(result.status==CommandListStatus::complete,"rotated-xy image status is complete");
    check(result.active[12]==1,"rotated-xy recognized as active command");
    check(result.records==2,"rotated-xy command list records are counted");
    check(result.shortest[12]==52&&result.longest[12]==52,"rotated-xy command has exact length 52");
    check(result.consumed_bytes==56,"rotated-xy plus terminator consumes 56 bytes");
    check(result.all_active_layouts_recognized&&!result.unsupported_layout_mask&&!result.malformed_layout_mask,
          "rotated-xy image has only supported layouts");
    check(rotate==original_rotate,"rotate-census preserves every copied byte");
    for(unsigned offset:{8u,12u,16u,20u,24u,28u,32u,36u,40u,48u}) {
        for(float nonfinite:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
            auto bad=rotate;put(bad,offset,nonfinite);const auto r=census_hud_commands(bad);
            check(r.status==CommandListStatus::complete&&r.malformed_layout_mask==(1u<<12)&&
                  !r.unsupported_layout_mask&&!r.all_active_layouts_recognized,
                  "each rotated-xy coordinate and scalar rejects nonfinite values");
        }
    }
    invalid=rotate;put(invalid,16,0.f);check(census_hud_commands(invalid).malformed_layout_mask==(1u<<12),"rotated pic W at+16 requires positive");
    invalid=rotate;put(invalid,20,-1.f);check(census_hud_commands(invalid).malformed_layout_mask==(1u<<12),"rotated pic width at+20 must be non-negative");
    invalid=rotate;put(invalid,24,-1.f);check(census_hud_commands(invalid).malformed_layout_mask==(1u<<12),"rotated pic height at+24 must be non-negative");
    invalid=rotate;put(invalid,4,std::uint32_t{0});check(census_hud_commands(invalid).malformed_layout_mask==(1u<<12),"rotated pic requires nonzero material");
    auto bad=record(12,4);end(bad);{
        const auto r=census_hud_commands(bad);
        check(r.status==CommandListStatus::complete&&r.malformed_layout_mask==(1u<<12)&&!r.unsupported_layout_mask,
              "rotated pic size 4 is structurally valid but malformed for non-matching layout");
    }
    bad=record(12,48);end(bad);{
        const auto r=census_hud_commands(bad);
        check(r.status==CommandListStatus::complete&&r.malformed_layout_mask==(1u<<12)&&!r.unsupported_layout_mask,
              "rotated pic requires exact52bytes before any field reads");
    }
    for(unsigned size:{51u,53u}) {auto bad_length=record(12,size);end(bad_length);const auto r=census_hud_commands(bad_length);
        check(r.status==CommandListStatus::invalid_length,"rotated pic rejects non-4-byte lengths");}
    for(std::uint32_t color:{0x00000000u,0x7fc00000u,0xffffffffu,0x12345678u}) {
        auto color_rotate=record(12,52,0x80);put(color_rotate,4,std::uint32_t{0x9a7f0000});
        std::memcpy(color_rotate.data()+8,rotate_floats,sizeof(rotate_floats));
        put(color_rotate,44,color);put(color_rotate,48,1.75f);
        end(color_rotate);
        auto tagged=census_hud_commands(color_rotate);
        check(tagged.status==CommandListStatus::complete&&tagged.tagged[12]==1&&!tagged.active[12]&&
              !tagged.unsupported_layout_mask&&!tagged.malformed_layout_mask,
              "tagged rotated-xy commands are layout-free for census");
    }
    bad_quad=quad;bad_quad.resize(47);
    check(census_hud_commands(bad_quad).status==CommandListStatus::overrun,"truncated quad cannot read packed color outside owned span");
    bad_quad=record(15,44,0x80);end(bad_quad);result=census_hud_commands(bad_quad);
    check(result.status==CommandListStatus::complete&&result.tagged[15]==1&&!result.active[15]&&
          !result.malformed_layout_mask&&result.all_active_layouts_recognized,
          "tagged quad is structurally bounded but skipped by native null-window layout census");
    bad_quad=quad;bad_quad[3]=0x7f;put(bad_quad,40,0.f);
    check(census_hud_commands(bad_quad).malformed_layout_mask==(1u<<15),"low quad flag bits do not bypass layout validation");
    auto four_quads=record(27,8);
    for(unsigned i=0;i<4;++i) four_quads.insert(four_quads.end(),quad.begin(),quad.begin()+48);
    end(four_quads);result=census_hud_commands(four_quads);
    check(result.status==CommandListStatus::complete&&result.active[15]==4&&result.active[27]==1&&
          result.records==6&&result.consumed_bytes==204&&result.all_active_layouts_recognized,
          "projection plus four active native quad records and terminator preserves census counts");
    auto text=record(17,100);put(text,20,std::uint32_t{0x4321});put(text,12,1.f);put(text,24,1.f);put(text,28,1.f);
    std::memcpy(text.data()+92,"HUD",4);end(text);check(census_hud_commands(text).all_active_layouts_recognized,"bounded inline native text recognized");
    for(unsigned i=92;i<100;++i)text[i]='A';check(census_hud_commands(text).malformed_layout_mask==(1u<<17),"terminator in next command cannot terminate text record");
    for(unsigned id:{5u,8u,9u,14u,18u,21u,22u,23u,24u,25u,26u,28u}){
        auto command=record(id,4);end(command);auto r=census_hud_commands(command);
        check(r.status==CommandListStatus::complete&&r.unsupported_layout_mask==(1u<<id)&&!r.all_active_layouts_recognized,
              "side-effect/unmapped command rejects whole active replay layout candidate");
        command[3]=0x80;r=census_hud_commands(command);
        check(r.tagged[id]==1&&r.active[id]==0&&!r.unsupported_layout_mask,"native null-window pass skips signed UI3D tag");
    }
    auto command=record(5,4,0x7f);end(command);check(census_hud_commands(command).active[5]==1,"low UI flag bits alone do not mean tagged");
    command=record(27,8);put(command,4,std::int32_t{1});end(command);
    check(census_hud_commands(command).malformed_layout_mask==(1u<<27),"3D projection cannot qualify for pure2D layout");
    bytes=record(0,4,0x80);const auto dangerous=record(5,4);bytes.insert(bytes.end(),dangerous.begin(),dangerous.end());
    result=census_hud_commands(bytes);check(result.consumed_bytes==4&&result.records==1&&result.tagged[0]==1,"native ID0 terminates before UI3D window filtering");
    std::printf("hud_command_checks=%u failures=0 native_calls=0 source_mutations=0\n",checks);
}
