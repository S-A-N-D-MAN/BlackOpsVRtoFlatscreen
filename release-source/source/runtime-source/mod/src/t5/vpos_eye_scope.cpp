// SPDX-License-Identifier: GPL-3.0-only
#include "t5/vpos_eye_scope.hpp"
#include "t5/identity.hpp"
#include "t5/native_read_recipe.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <cstring>
#include <cmath>
namespace bovr::t5 {
namespace {
constexpr std::uintptr_t first_buffer=0x3740100,buffer_stride=0x186300;
constexpr std::uintptr_t first_views=0x420f730,views_stride=0xe380;
std::atomic<std::uintptr_t> configured_base{};
thread_local VPosEyeScope* active_scope{};
thread_local std::uint64_t next_scope_serial{};
thread_local const char* read_reason="unscoped";
bool diagnostic_enabled{};std::atomic<unsigned> refusal_reports{};
template<class T>bool get(std::uintptr_t p,T* out) noexcept {
    if(p<0x10000||p>UINT32_MAX-sizeof(T))return false;
    __try{std::memcpy(out,reinterpret_cast<const void*>(p),sizeof(T));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool frame_current(const VPosEyeContext& o) noexcept {
    if(o.base<0x10000||o.base>UINT32_MAX-0x4240000||o.eye>1||!o.device||!o.producer||!o.generation||!o.xr_frame||
       (o.buffer!=o.base+first_buffer&&o.buffer!=o.base+first_buffer+buffer_stride))return false;
    const auto views=o.base+first_views+(o.buffer==o.base+first_buffer?0:views_stride);
    if(o.view!=views+o.eye*0x38e0)return false;
    std::uint32_t backend{},frame{},count{},index{},array{},parms{},data{},client{},full{},mode{};
    std::uint8_t extra{};VPosViewport viewport{};
    return get(o.base+0x3a5b5f8,&backend)&&backend==o.buffer&&
        get(o.buffer+0x15d7a0,&frame)&&frame==o.native_frame&&get(o.buffer+0x16cc3c,&count)&&count==2&&
        get(o.buffer+0x16cc38,&index)&&index==o.eye&&get(o.buffer+0x16cc40,&array)&&array==views&&
        get(o.buffer+0x16cbe0,&parms)&&parms==2&&get(o.view+0x3510,&data)&&data==o.buffer&&
        get(o.view+0x188,&client)&&client==0&&get(o.view+0x18c,&full)&&full==0&&
        get(o.view+0x3620,&mode)&&!(mode&7)&&get(o.view+0x38c4,&extra)&&!extra&&
        get(o.view+0x140,&viewport)&&viewport==o.viewport;
}
bool source_current(const VPosEyeContext& o,std::uintptr_t source,std::uintptr_t state,
                    const VPosViewport& effective,VPosEyeContext* result) noexcept {
    read_reason="frame_or_effective_viewport";
    if(!frame_current(o)||!source||!state||effective!=o.viewport)return false;
    std::uint32_t view{},data{},mode{},behavior{},depth{},device{},cost{};
    std::uint8_t target{},dirty{};VPosViewport viewport{},hardware{};
    std::array<std::uint32_t,5> scene{},expected_scene{};
    std::array<std::uint16_t,2> cached{},matrix{};VPosMatrix expected_projection{};
    VPosEyeContext n=o;n.source=source;n.state=state;VPosWorldInput input{};
    read_reason="source_state_camera";
    if(!get(source+0x1a08,&view)||view!=o.view||!get(source+0x1680,&data)||data!=o.buffer||
       !get(source+0x1a34,&mode)||!get(source+0x1a7c,&behavior)||!get(state+0x1128,&target)||
       !vpos_camera_correction_allowed({o.eye,2,mode,behavior,target,true,true,false,false})||
       !get(source+0x1a0c,&depth)||depth||!get(source+0x1a88,&dirty)||dirty||
       !get(state+0x90,&device)||device!=o.device||!get(o.base+0x420c124,&cost)||cost||
       !get(source+0x1a4c,&viewport)||viewport!=effective||!get(state+0x1104,&hardware)||hardware!=effective||
       !get(source+0x1a38,&scene)||!get(o.view+0x170,&expected_scene)||scene!=expected_scene)return false;
    read_reason="matrix_cache_or_projection";
    if(!get(source+0x1a80,&input.target_width)||!get(source+0x1a84,&input.target_height)||
       !get(source+0x200,&input.projection)||!get(source+0x180,&input.inverse_view)||
       !get(o.view+0x40,&expected_projection)||expected_projection!=input.projection||
       !get(source+0x19aa,&cached[0])||!get(source+0x19a6,&cached[1])||
       !get(source+0x19de,&matrix[0])||!get(source+0x19dc,&matrix[1])||
       !matrix[0]||!matrix[1]||cached!=matrix||
       !get(source+0x960,&n.constants))return false;
    std::array<std::uint16_t,2> size{};
    read_reason="target_dimensions";
    if(!get(o.base+0x41eb1e8+3*20+12,&size)||size[0]!=input.target_width||size[1]!=input.target_height)return false;
    n.target_width=input.target_width;n.target_height=input.target_height;input.viewport=effective;
    // The GPU transform below preserves exactly the native submitted vectors.
    // Until an asymmetric native camera is separately proved, refuse it.
    read_reason="asymmetric_projection";if(input.projection[8]!=0.f||input.projection[9]!=0.f)return false;
    VPosWorldConstants validation{};
    read_reason="matrix_or_vector_shape";
    if(!vpos_world_constants(input,&validation)||!vpos_viewport_constants(n.constants,
        input.target_width,input.target_height,effective,&n.corrected_constants))return false;
    auto full=input;full.viewport={0,0,static_cast<int>(input.target_width),static_cast<int>(input.target_height)};
    read_reason="native_vpos_not_current_matrix";
    if(!vpos_world_constants(full,&validation))return false;
    // Retail uses separately rounded binary32 operations. The tolerance only
    // admits their final ULP differences, not a stale camera or packed ray.
    const std::array<const VPosVector*,3> actual{&n.constants.x,&n.constants.y,&n.constants.one};
    const std::array<const VPosVector*,3> expected{&validation.x,&validation.y,&validation.one};
    for(unsigned v=0;v<3;++v)for(unsigned c=0;c<3;++c)
        if(std::fabs((*actual[v])[c]-(*expected[v])[c])>2.e-6f*(1.f+std::fabs((*expected[v])[c])))return false;
    read_reason="accepted";
    *result=n;return true;
}
bool water_pair_valid(const WaterEyeOrigins& pair) noexcept {
    double squared{};
    for(unsigned axis=0;axis<3;++axis){
        if(!std::isfinite(pair[0][axis])||!std::isfinite(pair[1][axis]))return false;
        const double delta=double(pair[0][axis])-double(pair[1][axis]);squared+=delta*delta;
    }
    // Eight engine inches is a conservative 20 cm upper bound, admitting a
    // zero-separation diagnostic view but refusing unrelated camera origins.
    return std::isfinite(squared)&&squared<=64.;
}
bool camera_owner_current(const VPosEyeContext& owner,VPosEyeContext* out) noexcept {
    read_reason="camera_owner_frame";if(!frame_current(owner))return false;
    std::array<std::uint32_t,2> context{};std::uint32_t view{},data{},mode{},selector{},depth{},device{};
    std::uint8_t target{},dirty{};VPosViewport viewport{},hardware{};
    std::array<std::uint32_t,5> scene{},selected_scene{};std::array<std::uint16_t,2> target_size{};
    auto value=owner;
    read_reason="camera_owner_immediate_context";
    if(!get(owner.base+0x747880,&context)||context[0]!=owner.base+0x3cca570||context[1]!=owner.base+0x417ee00)return false;
    value.source=context[0];value.state=context[1];
    read_reason="camera_owner_source_or_view";
    if(!get(value.source+0x1a08,&view)||view!=owner.view||!get(value.source+0x1680,&data)||data!=owner.buffer||
       !get(value.source+0x1a34,&mode)||mode!=1||!get(value.source+0x1a7c,&selector)||selector||
       !get(value.source+0x1a0c,&depth)||depth||!get(value.source+0x1a88,&dirty)||dirty||
       !get(value.state+0x90,&device)||device!=owner.device||!get(value.state+0x1128,&target)||target!=3||
       !get(value.source+0x1a4c,&viewport)||viewport!=owner.viewport||
       !get(value.state+0x1104,&hardware)||hardware!=owner.viewport||
       !get(value.source+0x1a38,&scene)||!get(owner.view+0x170,&selected_scene)||scene!=selected_scene)return false;
    read_reason="camera_owner_dimensions";
    if(!get(value.source+0x1a80,&value.target_width)||!get(value.source+0x1a84,&value.target_height)||
       !get(owner.base+0x41eb1e8+3*20+12,&target_size)||target_size[0]!=value.target_width||target_size[1]!=value.target_height||
       !value.target_width||!value.target_height||value.target_width>16384||value.target_height>16384||
       viewport.x<0||viewport.y<0||viewport.width<=0||viewport.height<=0||
       std::uint64_t(viewport.x)+static_cast<unsigned>(viewport.width)>value.target_width||
       std::uint64_t(viewport.y)+static_cast<unsigned>(viewport.height)>value.target_height)return false;
    value.constants={};value.corrected_constants={};*out=value;read_reason="camera_owner_accepted";return true;
}
}
bool configure_vpos_eye_scope(HMODULE executable) noexcept {
    NativeReadPreserve preserve;bool okay{};
    try{std::string reason;okay=validate_path(executable_path()).accepted&&validate_loaded_headers(executable,reason);}
    catch(...){okay=false;}
    if(okay){configured_base.store(reinterpret_cast<std::uintptr_t>(executable),std::memory_order_release);
        wchar_t flag[8]{};diagnostic_enabled=GetEnvironmentVariableW(L"BOVR_WATER_REFLECTION_PROBE",flag,8)==1&&flag[0]==L'1';}
    trace("t5_water_eye_scope_configured","accepted=%u native_writes=0 native_hooks_added=0 scope=ordinary_rt3",okay);return okay;
}
bool VPosEyeScope::begin(std::uintptr_t base,std::uintptr_t buffer,unsigned frame,unsigned eye,VPosViewport viewport,
                        std::uintptr_t device,std::uint64_t producer,std::uint64_t generation,std::uint64_t xr_frame,VPosEyeOwner current,
                        const WaterEyeOrigins* water_origins) noexcept {
    NativeReadPreserve preserve;
    if(active_||active_scope){if(active_scope)active_scope->blocked_=true;return false;}
    if(next_scope_serial==UINT64_MAX)return false;
    VPosEyeContext o{};o.base=base;o.buffer=buffer;o.native_frame=frame;o.eye=eye;o.viewport=viewport;o.device=device;
    o.producer=producer;o.generation=generation;o.xr_frame=xr_frame;
    o.view=base+first_views+(buffer==base+first_buffer?0:views_stride)+eye*0x38e0;
    if(base!=configured_base.load(std::memory_order_acquire)||!current.valid()||!frame_current(o))return false;
    owner_=o;current_=current;water_origins_=water_origins?*water_origins:WaterEyeOrigins{};
    water_pair_valid_=water_origins&&water_pair_valid(water_origins_);
    thread_=GetCurrentThreadId();scope_serial_=++next_scope_serial;active_=true;blocked_=false;active_scope=this;return true;
}
bool read_vpos_eye_context(VPosEyeContext* output) noexcept {
    NativeReadPreserve preserve;if(output)*output={};auto* s=active_scope;
    read_reason="unscoped_or_retired";
    if(!output||!s||!s->active_||s->blocked_||s->thread_!=GetCurrentThreadId()||!s->current_.valid())return false;
    // Only the retail immediate global context is qualified. A local/worker
    // context, prepass, auxiliary view or changed camera remains untouched.
    std::array<std::uint32_t,2> context{},again{};VPosEyeContext first{},second{};
    read_reason="immediate_context";
    if(!get(s->owner_.base+0x747880,&context)||context[0]!=s->owner_.base+0x3cca570||
       context[1]!=s->owner_.base+0x417ee00||
       !source_current(s->owner_,context[0],context[1],s->owner_.viewport,&first)||
       !source_current(s->owner_,context[0],context[1],s->owner_.viewport,&second)||first!=second||
       !get(s->owner_.base+0x747880,&again)||context!=again||!s->current_.valid())return false;
    *output=second;return true;
}
bool read_vpos_eye_owner(VPosEyeContext* output) noexcept {
    NativeReadPreserve preserve;if(output)*output={};auto* scope=active_scope;
    read_reason="camera_owner_unscoped_or_retired";
    if(!output||!scope||!scope->active_||scope->blocked_||scope->thread_!=GetCurrentThreadId()||!scope->current_.valid())return false;
    VPosEyeContext first{},second{};
    if(!camera_owner_current(scope->owner_,&first)||!camera_owner_current(scope->owner_,&second)||first!=second||
       !scope->current_.valid())return false;
    *output=second;return true;
}
bool read_water_eye_native(WaterEyeNativeSnapshot* output) noexcept {
    NativeReadPreserve preserve;if(!output)return false;*output={};
    auto once=[](WaterEyeNativeSnapshot& value) noexcept {
        if(!read_vpos_eye_owner(&value.owner))return false;
        const auto source=value.owner.source,view=value.owner.view;
        if(!get(source+0x19f0,&value.source_eye_offset)||!get(source+0x1790,&value.source_view_origin)||
           !get(view+0x100,&value.selected_view_origin)||!get(source,&value.source_world)||
           !get(source+0x80,&value.source_transpose_world)||!get(source+0x480,&value.source_transpose_view_projection)||
           !get(source+0xa00,&value.source_game_time)||!get(source+0x1090,&value.source_hdr)||
           !get(source+0x199e,&value.matrix_versions[0])||!get(source+0x19da,&value.matrix_versions[1])||
           !get(source+0x19be,&value.matrix_versions[2])||!get(source+0x19e2,&value.matrix_versions[3])||
           !get(source+0x1a30,&value.source_camera_view))return false;
        VPosEyeContext after{};return read_vpos_eye_owner(&after)&&after==value.owner;
    };
    WaterEyeNativeSnapshot first{},second{};
    if(!once(first)||!once(second)||first!=second)return false;
    *output=second;return true;
}
bool read_water_draw_owner(WaterDrawOwnerSnapshot* output) noexcept {
    NativeReadPreserve preserve;if(output)*output={};auto* scope=active_scope;
    read_reason="draw_owner_unscoped_or_retired";
    if(!output||!scope||!scope->active_||scope->blocked_||scope->thread_!=GetCurrentThreadId()||!scope->current_.valid())return false;
    auto once=[&](WaterDrawOwnerSnapshot& value) noexcept {
        value.owner=scope->owner_;const auto& owner=value.owner;
        read_reason="draw_owner_frame";if(!frame_current(owner))return false;
        // The RT3 allocation is independent of the inactive immediate source.
        // It is only expected dimensions; the caller still checks actual RT0.
        std::array<std::uint16_t,2> size{};
        read_reason="draw_owner_size_or_origin";
        if(!get(owner.base+0x41eb1e8+3*20+12,&size)||!size[0]||!size[1]||size[0]>16384||size[1]>16384||
           owner.viewport.x<0||owner.viewport.y<0||owner.viewport.width<=0||owner.viewport.height<=0||
           std::uint64_t(owner.viewport.x)+static_cast<unsigned>(owner.viewport.width)>size[0]||
           std::uint64_t(owner.viewport.y)+static_cast<unsigned>(owner.viewport.height)>size[1]||
           !get(owner.view+0x100,&value.selected_view_origin))return false;
        for(const auto v:value.selected_view_origin)if(!std::isfinite(v))return false;
        value.owner.target_width=size[0];value.owner.target_height=size[1];
        return frame_current(owner);
    };
    WaterDrawOwnerSnapshot first{},second{};
    if(!once(first)||!once(second)||first!=second||!scope->current_.valid())return false;
    *output=second;read_reason="draw_owner_accepted_no_native_source";return true;
}
bool read_water_shading_eye(WaterShadingEyeContext* output) noexcept {
    NativeReadPreserve preserve;if(output)*output={};auto* scope=active_scope;
    read_reason="water_shading_missing_pair";
    if(!output||!scope||!scope->water_pair_valid_)return false;
    WaterShadingEyeContext value{};value.scope_serial=scope->scope_serial_;
    if(!read_water_draw_owner(&value.draw))return false;
    value.eye_origins=scope->water_origins_;
    const auto& owner=value.draw.owner;
    read_reason="water_shading_native_origin_mismatch";
    if(!water_pair_valid(value.eye_origins)||value.draw.selected_view_origin!=value.eye_origins[owner.eye])return false;
    const auto views=owner.view-owner.eye*0x38e0;
    // Both native camera origins must still match the one immutable producer.
    // No source pointer or matrix-cache value from the inactive global context
    // is used to authorize the draw-local adjustment.
    WaterEyeOrigins native_origins{};
    if(!get(views+0x100,&native_origins[0])||!get(views+0x38e0+0x100,&native_origins[1])||native_origins!=value.eye_origins)return false;
    for(unsigned axis=0;axis<3;++axis)
        value.reflection_offset[axis]=.5f*(value.eye_origins[owner.eye][axis]-value.eye_origins[1-owner.eye][axis]);
    WaterDrawOwnerSnapshot after{};WaterEyeOrigins native_after{};
    if(!read_water_draw_owner(&after)||after!=value.draw||
       !get(views+0x100,&native_after[0])||!get(views+0x38e0+0x100,&native_after[1])||native_after!=native_origins||
       active_scope!=scope||scope->scope_serial_!=value.scope_serial)return false;
    *output=value;read_reason="water_shading_pair_accepted";return true;
}
bool water_shading_eye_current(const WaterShadingEyeContext& expected) noexcept {
    NativeReadPreserve preserve;auto* const scope=active_scope;
    if(!scope||!scope->active_||scope->blocked_||!scope->water_pair_valid_||
       scope->thread_!=GetCurrentThreadId()||!expected.scope_serial||scope->scope_serial_!=expected.scope_serial||
       !scope->current_.valid())return false;
    const auto& owner=expected.draw.owner;
    auto scoped=scope->owner_;
    scoped.target_width=owner.target_width;scoped.target_height=owner.target_height;
    if(scoped!=owner||scope->water_origins_!=expected.eye_origins||owner.eye>1||
       expected.draw.selected_view_origin!=expected.eye_origins[owner.eye])return false;
    VPosVector offset{};
    for(unsigned axis=0;axis<3;++axis)
        offset[axis]=.5f*(expected.eye_origins[owner.eye][axis]-expected.eye_origins[1-owner.eye][axis]);
    if(offset!=expected.reflection_offset)return false;
    const auto views=owner.view-owner.eye*0x38e0;
    auto live_values=[&]() noexcept {
        std::array<std::uint16_t,2> size{};WaterEyeOrigins origins{};
        return get(owner.base+0x41eb1e8+3*20+12,&size)&&
            size[0]==owner.target_width&&size[1]==owner.target_height&&
            get(views+0x100,&origins[0])&&get(views+0x38e0+0x100,&origins[1])&&origins==expected.eye_origins;
    };
    // No authority is reused across a COM call: every call checks this before
    // and after. Immutable producer fields are compared once; mutable native
    // values are bracketed without four nested snapshot reconstructions.
    return frame_current(owner)&&live_values()&&frame_current(owner)&&live_values()&&
        active_scope==scope&&scope->active_&&!scope->blocked_&&scope->scope_serial_==expected.scope_serial&&scope->current_.valid();
}
void trace_vpos_eye_refusal() noexcept {
    NativeReadPreserve preserve;if(!diagnostic_enabled)return;
    const auto n=refusal_reports.fetch_add(1,std::memory_order_relaxed);if(n>=12)return;
    const auto base=configured_base.load(std::memory_order_acquire);if(!base)return;
    std::array<std::uint32_t,2> context{};std::uint32_t view{},data{},mode{},selector{},depth{},width{},height{},selected{},count{};
    std::uint8_t target{},dirty{};VPosViewport source_viewport{},state_viewport{};std::uint32_t buffer{};
    get(base+0x747880,&context);get(base+0x3a5b5f8,&buffer);
    if(buffer){get(buffer+0x16cc38,&selected);get(buffer+0x16cc3c,&count);}
    if(context[0]){get(context[0]+0x1a08,&view);get(context[0]+0x1680,&data);get(context[0]+0x1a34,&mode);
        get(context[0]+0x1a7c,&selector);get(context[0]+0x1a0c,&depth);get(context[0]+0x1a88,&dirty);
        get(context[0]+0x1a80,&width);get(context[0]+0x1a84,&height);get(context[0]+0x1a4c,&source_viewport);}
    if(context[1]){get(context[1]+0x1128,&target);get(context[1]+0x1104,&state_viewport);}
    trace("t5_water_eye_refused","sample=%u reason=%s scope=%u source=0x%x state=0x%x view=0x%x data=0x%x buffer=0x%x selected=%u count=%u mode=%u selector=%u depth=%u dirty=%u target=%u size=%ux%u source_vp=%d,%d,%d,%d state_vp=%d,%d,%d,%d",
        n+1,read_reason,active_scope!=nullptr,context[0],context[1],view,data,buffer,selected,count,mode,selector,depth,unsigned(dirty),unsigned(target),width,height,
        source_viewport.x,source_viewport.y,source_viewport.width,source_viewport.height,state_viewport.x,state_viewport.y,state_viewport.width,state_viewport.height);
}
void VPosEyeScope::finish() noexcept {
    NativeReadPreserve preserve;if(!active_)return;
    if(thread_!=GetCurrentThreadId()){blocked_=true;return;}
    if(active_scope==this)active_scope=nullptr;else blocked_=true;
    active_=false;
}
void invoke_vpos_eye_native(VPosEyeScope* scope,void(*body)(void*),void* context) {
    __try{body(context);}__finally{if(scope)scope->finish();}
}
}
