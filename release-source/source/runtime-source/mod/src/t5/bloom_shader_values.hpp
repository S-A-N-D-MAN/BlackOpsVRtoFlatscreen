// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/auxiliary_view.hpp"
#include <array>
#include <cstdint>
namespace bovr::t5 {
inline constexpr std::uint64_t bloom_process_bytes=4ull*1024*1024;
inline constexpr unsigned bloom_shader_bytes=64*1024;
// Validate only bounded SM2/SM3 token framing, not shader semantics. Exact
// returned bytes remain the evidence; no tokens are interpreted or executed.
inline bool bloom_token_stream(const std::uint32_t* words,std::size_t count,bool vertex) noexcept {
    if(!words||count<2||count>bloom_shader_bytes/4)return false;
    const auto v=words[0];if((v>>16)!=(vertex?0xfffeu:0xffffu)||((v>>8)&255)<2||((v>>8)&255)>3)return false;
    for(std::size_t i=1;i<count;){const auto op=words[i]&0xffff;
        if(op==0xffff)return words[i]==0xffff&&i+1==count;
        const auto length=op==0xfffe?((words[i]>>16)&0x7fff):((words[i]>>24)&15);
        if(op!=0xfffe&&op>96)return false;
        if(length>=count-i)return false;i+=1+length;
    }return false;
}
enum class BloomReadStatus : unsigned { unrelated, layout, current_view, scene, pixel_cost, material, technique, arguments, names, source, changed, accepted };
struct BloomDrawCall {std::uintptr_t caller{};std::uint32_t type{};std::int32_t base_vertex{};std::uint32_t min_vertex{},num_vertices{},start_index{},primitive_count{};};
struct BloomArgument {std::uint16_t type{},destination{};std::uint32_t value{};
    friend bool operator==(const BloomArgument&,const BloomArgument&)=default;};
static_assert(sizeof(BloomArgument)==8);
struct BloomSampler {
    std::uint32_t code{},destination{},image{},texture{},control{},control_value{};
    std::uint8_t sampler{};
    std::array<std::uint8_t,3> reserved{}; // Explicit zeros; no padding in output.
    friend bool operator==(const BloomSampler&,const BloomSampler&)=default;
};
static_assert(sizeof(BloomSampler)==28);
struct BloomConstant {std::uint16_t type{},destination{};
    friend bool operator==(const BloomConstant&,const BloomConstant&)=default;};
struct BloomDrawSnapshot {
    std::uint32_t buffer{},native_frame{},view_count{};
    std::uint32_t selected{},eye{},role{},target{},view{},source_view{},source_mode{},selector{},width{},height{};
    std::uint32_t material{},techset{},technique{},pass{},pass_index{},vertex_record{},pixel_record{},vertex_shader{},pixel_shader{},args_pointer{};
    std::uint16_t pass_count{},technique_flags{};
    std::array<std::uint8_t,3> argument_counts{};
    std::uint8_t custom_sampler_flags{};
    std::array<std::uint32_t,5> scene{};
    render::HudEyeViewport scene_viewport{},scissor{};
    std::array<std::uint32_t,4> viewport_constant{};
    AuxiliaryTarget native_target{};
    std::array<BloomArgument,64> arguments{};
    std::array<BloomSampler,16> samplers{};
    std::array<BloomConstant,8> constants{};
    std::uint32_t sampler_count{},constant_count{};
    std::array<std::array<char,128>,4> names{};
    friend bool operator==(const BloomDrawSnapshot&,const BloomDrawSnapshot&)=default;
};
static_assert(sizeof(BloomDrawSnapshot)==1696&&sizeof(BloomDrawCall)==28);
// Process-lifetime debit. Failed retrievals are spent, never refunded. Both
// eyes are charged independently; no identity/deduplication premise is used.
class BloomByteBudget {
public:
    bool debit(std::uint64_t bytes) noexcept {
        if(!bytes||bytes>bloom_shader_bytes||bytes>bloom_process_bytes-used_)return false;
        used_+=bytes;return true;
    }
    std::uint64_t used() const noexcept{return used_;}
private:std::uint64_t used_{};
};
inline bool bloom_argument_shape(const BloomArgument& a) noexcept {
    if(a.type>7)return false;
    if((a.type==3||a.type==5)&&(a.value&0xffff)==93)
        return ((a.value>>16)&255)==0&&(a.value>>24)==1&&a.destination<(a.type==3?256:224);
    if(a.type==4&&(a.value==34||a.value==35||a.value==36))return a.destination<16;
    return true;
}
// Exact selected pass sets must appear once in native order right then left.
class BloomPairSet {
public:
    bool add(const BloomDrawSnapshot& s) noexcept {
        if(failed_||s.eye>1||s.role>3||!s.pass_count||s.pass_count>4||s.pass_index>=s.pass_count)return fail();
        if(s.eye==0)left_started_=true;else if(left_started_)return fail();
        auto& n=counts_[s.eye][s.role];if(n&&n!=s.pass_count)return fail();n=s.pass_count;
        const auto bit=1u<<s.pass_index;auto& m=mask_[s.eye][s.role];if(m&bit)return fail();m|=bit;
        auto& r=records_[s.eye][s.role][s.pass_index];r={s.material,s.techset,s.technique,s.pass,s.vertex_shader,s.pixel_shader};
        return true;
    }
    bool complete() const noexcept {
        if(failed_)return false;
        for(unsigned r=0;r<4;++r){if(!counts_[0][r]||counts_[0][r]!=counts_[1][r])return false;
            const auto expected=(1u<<counts_[0][r])-1;
            if(mask_[0][r]!=expected||mask_[1][r]!=expected)return false;
            for(unsigned p=0;p<counts_[0][r];++p)if(records_[0][r][p]!=records_[1][r][p])return false;}
        return true;
    }
    void invalidate() noexcept {failed_=true;}
private:
    bool fail() noexcept {failed_=true;return false;}
    bool failed_{},left_started_{};
    std::array<std::array<unsigned,4>,2> counts_{},mask_{};
    std::array<std::array<std::array<std::array<std::uint32_t,6>,4>,4>,2> records_{};
};
}
