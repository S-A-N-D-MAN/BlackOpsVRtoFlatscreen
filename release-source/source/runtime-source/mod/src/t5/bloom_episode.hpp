// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/bloom_shader_values.hpp"
#include <cstring>
#include <xmmintrin.h>
namespace bovr::t5 {
enum class BloomCaptureContract : unsigned { selected_four=1, complete_ldr=2 };
inline constexpr unsigned bloom_role_count=13;
inline constexpr std::array<unsigned,13> bloom_role_order{0,1,2,4,5,6,9,10,11,12,7,3,8};
inline constexpr std::array<std::uint32_t,13> bloom_material_rvas{
    0x356a398,0x356a390,0x356a3a0,0x356a3cc,0x356a3a4,0x356a3bc,0x356a3c0,
    0x356a3d0,0x356a2c0,0x356a3ac,0x356a3b0,0x356a3ac,0x356a3b4};
struct BloomPassPlan {
    std::uint32_t pass{},vertex_record{},pixel_record{},vertex_shader{},pixel_shader{},args_pointer{},argument_counts{};
    friend bool operator==(const BloomPassPlan&,const BloomPassPlan&)=default;
};
struct BloomRolePlan {
    std::uint32_t material{},techset{},technique{},pass_count{},technique_flags{};
    std::array<BloomPassPlan,4> passes{};
    friend bool operator==(const BloomRolePlan&,const BloomRolePlan&)=default;
};
// Independent versioned wire structure. No implicit padding or native pointers
// are exported; every address below is a copied uint32 equality value only.
struct BloomEpisodePlan {
    std::uint32_t version{},buffer{},native_frame{},view_count{};
    std::array<std::uint32_t,2> views{};
    std::array<std::array<std::uint32_t,6>,2> tint_bits{};
    std::array<std::uint32_t,2> enabled{},role_counts{};
    std::uint32_t expected_draws{},fp_control{};
    std::array<std::array<BloomRolePlan,bloom_role_count>,2> roles{};
    friend bool operator==(const BloomEpisodePlan&,const BloomEpisodePlan&)=default;
};
static_assert(sizeof(BloomPassPlan)==28&&sizeof(BloomRolePlan)==132&&sizeof(BloomEpisodePlan)==3528);
static_assert(offsetof(BloomEpisodePlan,roles)==96&&offsetof(BloomEpisodePlan,tint_bits)==24);
struct BloomEpisodeCursor {std::uint32_t eye{1},role{},pass{},finished{};};
inline bool bloom_finite(std::uint32_t b) noexcept {return (b&0x7f800000)!=0x7f800000;}
// Native uses ordered ADDSS under its current control. Preserve all MXCSR bits,
// including preexisting exception status. Unmasked exceptions are unsupported.
inline bool bloom_tint_enabled(const std::array<std::uint32_t,6>& words,unsigned& enabled) noexcept {
    const auto saved=_mm_getcsr();enabled=0;
    if((saved&0x1f80)!=0x1f80)return false;
    for(auto b:words)if(!bloom_finite(b))return false;
    bool ok=true;
    for(unsigned axis=0;axis<2;++axis){float a{},b{},c{};std::memcpy(&a,&words[axis*3],4);std::memcpy(&b,&words[axis*3+1],4);std::memcpy(&c,&words[axis*3+2],4);
        const auto x=_mm_add_ss(_mm_set_ss(a),_mm_set_ss(b));const auto y=_mm_add_ss(x,_mm_set_ss(c));
        const float value=_mm_cvtss_f32(y);std::uint32_t bits{};std::memcpy(&bits,&value,4);
        if(!bloom_finite(bits)){ok=false;break;}if(bits&0x7fffffff)enabled|=1u<<axis;
    }
    _mm_setcsr(saved);return ok;
}
inline bool bloom_role_enabled(unsigned role,unsigned mask) noexcept {
    return role<bloom_role_count&&((role!=9&&role!=10)||(mask&1))&&((role!=11&&role!=12)||(mask&2));
}
inline unsigned bloom_role_target(unsigned role,unsigned mask) noexcept {
    if(role<3)return 25+role;if(role==3||role==8)return 3;
    if(role==4||role==6)return 28;if(role==5)return 29;if(role==9||role==11)return 30;
    if(role==10)return 29;if(role==12)return (mask&1)?28:29;
    if(role==7)return ((!!(mask&1))^(!!(mask&2)))?28:29;return 0;
}
inline unsigned bloom_code_target(unsigned role,unsigned mask,unsigned code) noexcept {
    if(role==0&&code==34)return 6;if(role==1&&code==34)return 25;if(role==2&&code==34)return 26;
    if(role==4&&(code==34||code==35))return 27;if(role==5&&code==34)return 28;if(role==6&&code==34)return 29;
    if((role==9||role==11)&&code==34)return 27;
    if((role==10||role==12)&&code==34)return 30;
    if(role==10&&code==35)return 28;if(role==12&&code==35)return (mask&1)?29:28;
    if(role==7&&code==34)return bloom_role_target(role,mask)==28?29:28;
    if(role==3&&(code==34||code==36))return 6;if(role==3&&code==35)return bloom_role_target(7,mask);
    if(role==8&&code==10)return 6;return 0;
}
inline bool bloom_plan_valid(const BloomEpisodePlan& p) noexcept {
    if(p.version!=1||!p.buffer||!postfx_view_count(p.view_count)||!p.views[0]||!p.views[1]||p.views[0]==p.views[1]||
       p.enabled[0]>3||p.enabled[0]!=p.enabled[1]||(p.fp_control&~0xffc0u)||(p.fp_control&0x1f80)!=0x1f80)return false;
    const auto control=_mm_getcsr();_mm_setcsr(p.fp_control);
    unsigned a{},b{};const bool tint_ok=bloom_tint_enabled(p.tint_bits[0],a)&&bloom_tint_enabled(p.tint_bits[1],b);
    _mm_setcsr(control);if(!tint_ok||a!=p.enabled[0]||b!=p.enabled[1])return false;
    unsigned total=0;
    for(unsigned eye=0;eye<2;++eye){unsigned count=0;
        for(unsigned r=0;r<bloom_role_count;++r){const auto& v=p.roles[eye][r];
            if(!bloom_role_enabled(r,p.enabled[eye])){if(v!=BloomRolePlan{})return false;continue;}
            ++count;if(!v.material||!v.techset||!v.technique||!v.pass_count||v.pass_count>4||v.technique_flags>65535)return false;
            if(eye&&v!=p.roles[0][r])return false;
            for(unsigned i=0;i<4;++i){const auto& s=v.passes[i];if(i>=v.pass_count){if(s!=BloomPassPlan{})return false;continue;}
                if(s.pass!=v.technique+8+20*i||!s.vertex_record||!s.pixel_record||!s.vertex_shader||!s.pixel_shader)return false;
                const unsigned n=(s.argument_counts&255)+((s.argument_counts>>8)&255)+((s.argument_counts>>16)&255);
                if(n>64||(n&&!s.args_pointer))return false;
            }
            for(unsigned k=0;k<r;++k)if(bloom_role_enabled(k,p.enabled[eye])&&v.material==p.roles[eye][k].material&&!(r==11&&k==9))return false;
            if(r==11&&(p.enabled[eye]&1)&&v!=p.roles[eye][9])return false;
            total+=v.pass_count;
        }
        if(count!=p.role_counts[eye]||count!=9u+2u*!!(p.enabled[eye]&1)+2u*!!(p.enabled[eye]&2))return false;
    }
    return total==p.expected_draws&&total<=32;
}
inline bool bloom_plan_matches(const BloomEpisodePlan& p,const BloomEpisodeCursor& c,const BloomDrawSnapshot& n) noexcept {
    if(c.finished||c.eye>1||c.role>=bloom_role_count||!bloom_role_enabled(c.role,p.enabled[c.eye]))return false;
    const auto& r=p.roles[c.eye][c.role];if(c.pass>=r.pass_count)return false;const auto& s=r.passes[c.pass];
    std::uint32_t counts{};std::memcpy(&counts,n.argument_counts.data(),3);counts|=std::uint32_t(n.custom_sampler_flags)<<24;
    return n.buffer==p.buffer&&n.native_frame==p.native_frame&&n.view_count==p.view_count&&n.view==p.views[c.eye]&&n.source_view==n.view&&
        n.eye==c.eye&&n.selected==postfx_native_index(p.view_count,c.eye)&&n.role==c.role&&n.target==bloom_role_target(c.role,p.enabled[c.eye])&&
        n.material==r.material&&n.techset==r.techset&&n.technique==r.technique&&n.technique_flags==r.technique_flags&&n.pass_count==r.pass_count&&
        n.pass_index==c.pass&&n.pass==s.pass&&n.vertex_record==s.vertex_record&&n.pixel_record==s.pixel_record&&n.vertex_shader==s.vertex_shader&&
        n.pixel_shader==s.pixel_shader&&n.args_pointer==s.args_pointer&&counts==s.argument_counts;
}
class BloomEpisodeSet {
public:
    BloomEpisodeSet(unsigned first_eye=1) noexcept:cursor_{first_eye,0,0,0},first_eye_(first_eye){}
    const BloomEpisodeCursor& cursor() const noexcept{return cursor_;}
    bool accept(const BloomEpisodePlan& p,const BloomDrawSnapshot& n) noexcept {
        if(failed_||first_eye_!=postfx_first_eye(p.view_count)||!bloom_plan_valid(p)||(planned_&&p!=plan_)||!bloom_plan_matches(p,cursor_,n)||
           (scene_seen_[cursor_.eye]&&scenes_[cursor_.eye]!=n.scene)){failed_=true;return false;}
        if(!planned_){plan_=p;planned_=true;}
        scenes_[cursor_.eye]=n.scene;scene_seen_[cursor_.eye]=true;
        ++draws_;const auto& r=p.roles[cursor_.eye][cursor_.role];
        if(++cursor_.pass<r.pass_count)return true;cursor_.pass=0;
        unsigned pos=0;while(pos<bloom_role_order.size()&&bloom_role_order[pos]!=cursor_.role)++pos;
        do{++pos;}while(pos<bloom_role_order.size()&&!bloom_role_enabled(bloom_role_order[pos],p.enabled[cursor_.eye]));
        if(pos<bloom_role_order.size())cursor_.role=bloom_role_order[pos];
        else if(cursor_.eye==first_eye_){cursor_={1u-first_eye_,0,0,0};}else cursor_.finished=1;
        return true;
    }
    bool complete() const noexcept{return planned_&&!failed_&&cursor_.finished&&draws_==plan_.expected_draws;}
    void invalidate() noexcept{failed_=true;}
private:BloomEpisodeCursor cursor_{};BloomEpisodePlan plan_{};unsigned first_eye_{1},draws_{};bool planned_{},failed_{};
    std::array<std::array<std::uint32_t,5>,2> scenes_{};std::array<bool,2> scene_seen_{};
};
}
