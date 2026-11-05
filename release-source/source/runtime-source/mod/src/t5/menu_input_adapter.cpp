// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/menu_input_adapter.hpp"
#include "t5/identity.hpp"
#include "t5/vr_scene_adapter.hpp"
#include "loader/trace.hpp"
#include "menu_panel_logic.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <utility>

namespace bovr::t5 {
namespace {
std::mutex menu_pointer_visual_lock;
MenuPointerVisual menu_pointer_visual{};

void publish_menu_pointer_visual(const menu_input_detail::PointerUpdate& update,
                                 std::uint64_t generation,
                                 std::uint64_t sample_sequence,
                                 std::uint64_t submission_serial) noexcept {
    try {
        std::lock_guard lock(menu_pointer_visual_lock);
        menu_pointer_visual={};
        if(update.hit && std::isfinite(update.u) && std::isfinite(update.v) &&
           update.u>=0.0f && update.u<=1.0f && update.v>=0.0f && update.v<=1.0f)
            menu_pointer_visual={true,update.u,update.v,generation,sample_sequence,submission_serial};
    } catch(...) {}
}
}

bool read_menu_pointer_visual(MenuPointerVisual* output) noexcept {
    if(!output)return false;
    *output={};
    try {
        std::lock_guard lock(menu_pointer_visual_lock);
        *output=menu_pointer_visual;
        return output->visible;
    } catch(...) {*output={};return false;}
}

bool read_native_menu_page(std::uintptr_t base,NativeMenuPage* output) noexcept {
    if(!base || !output) return false;
    *output={};
    __try {
        const auto context=base+0x216aa50;
        int index{},count{},type{};
        memcpy(&index,reinterpret_cast<const void*>(context),4);
        memcpy(&count,reinterpret_cast<const void*>(context+0x1580),4);
        memcpy(&type,reinterpret_cast<const void*>(context+0x21d4),4);
        if(index!=0 || count<0 || count>16 || type<0 || type>22) return false;
        std::uintptr_t selected{},slot{};unsigned flags{};
        for(int i=count-1;i>=0;--i) {
            const auto address=context+0x1500+std::uintptr_t(i)*8;
            std::uintptr_t menu{};memcpy(&menu,reinterpret_cast<const void*>(address),4);
            if(!menu) return false;
            unsigned candidate_flags{};memcpy(&candidate_flags,reinterpret_cast<const void*>(menu+0x54),4);
            if((candidate_flags&6)==6) {selected=menu;slot=address;flags=candidate_flags;break;}
        }
        int count_after{},type_after{},index_after{};
        memcpy(&index_after,reinterpret_cast<const void*>(context),4);
        memcpy(&count_after,reinterpret_cast<const void*>(context+0x1580),4);
        memcpy(&type_after,reinterpret_cast<const void*>(context+0x21d4),4);
        if(index!=index_after || count!=count_after || type!=type_after) return false;
        if(selected) {
            std::uintptr_t pointer_after{};unsigned flags_after{};
            memcpy(&pointer_after,reinterpret_cast<const void*>(slot),4);
            memcpy(&flags_after,reinterpret_cast<const void*>(selected+0x54),4);
            if(pointer_after!=selected || flags_after!=flags) return false;
        }
        *output={selected,count,type};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {*output={};return false;}
}
namespace menu_input_detail {
namespace {
bool allowed_key(int key) noexcept {
    return key==key_enter || key==key_escape || key==key_up || key==key_down || key==key_mouse1;
}
bool invoke(NativeKeyEvent dispatch,int key,int down,unsigned time) noexcept {
    if(!dispatch || !allowed_key(key)) return false;
    __try {dispatch(0,key,down,time);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}
void Policy::invalidate() noexcept {static_cast<void>(router_.release_all());}
Intent Policy::update(const wawvr::xr::ActionSnapshot& sample,bool live,std::uint64_t generation,
                      const Context& context,std::uint64_t now) noexcept {
    if(!generation_seen_ || generation!=generation_) {
        invalidate();generation_=generation;generation_seen_=true;sequence_seen_=false;
    }
    const bool menu=context.valid && context.menu_exists && !context.native_capture &&
                    context.key_catchers==0x10 && context.panel_current;
    const bool gameplay=context.valid && context.connection_state==10 && context.key_catchers==0;
    if(!live || !generation || (!menu && !gameplay)) {invalidate();return Intent::none;}
    // Native event draining can run repeatedly between XR publications. A
    // previous neutral sample must not rearm after an action/page invalidation.
    const bool duplicate=sequence_seen_ && sequence_==sample.sequence;
    if(duplicate) {
        bool tracked=sample.focused;
        for(const auto& hand:sample.hands) tracked=tracked && hand.grip.active &&
            hand.grip.position_valid && hand.grip.orientation_valid &&
            hand.grip.position_tracked && hand.grip.orientation_tracked;
        if(!tracked) invalidate();
        static_cast<void>(router_.expire(now));
        return Intent::none;
    }
    sequence_=sample.sequence;sequence_seen_=true;
    const auto controls=router_.update(sample,menu?input::Context::menu:input::Context::gameplay,now);
    // At most one native action per sample. Escape wins over confirm, and native
    // gameplay +/- edges from this independent router are deliberately ignored.
    if(controls.menu_toggle) return menu?Intent::back:Intent::pause;
    if(!menu) return Intent::none;
    if(controls.menu_back) return Intent::back;
    if(controls.menu_accept) return Intent::accept;
    if(controls.menu_up) return Intent::up;
    if(controls.menu_down) return Intent::down;
    return Intent::none;
}
void PointerPolicy::invalidate() noexcept {
    generation_=sequence_=submission_serial_=0;
    generation_seen_=sequence_seen_=armed_=held_=false;
    trigger_source_=TriggerSource::none;
}
PointerUpdate PointerPolicy::update(const wawvr::xr::ActionSnapshot& sample,bool live,std::uint64_t generation,
                                    const Context& context,const PointerSurface& surface) noexcept {
    PointerUpdate update{};
    const unsigned hand_index=left_handed_?0U:1U;
    const auto& hand=sample.hands[hand_index];
    const bool lifecycle=live && generation && sample.focused && context.valid && context.menu_exists &&
                         !context.native_capture && context.key_catchers==0x10 &&
                         hand.grip.active && hand.grip.position_valid && hand.grip.orientation_valid &&
                         hand.grip.position_tracked && hand.grip.orientation_tracked &&
                         hand.aim.active && hand.aim.position_valid && hand.aim.orientation_valid &&
                         hand.aim.position_tracked && hand.aim.orientation_tracked;
    if(!lifecycle) {invalidate();return update;}
    if(!generation_seen_ || generation!=generation_) {
        invalidate();generation_=generation;generation_seen_=true;
    }
    if(sequence_seen_ && sample.sequence<sequence_) {invalidate();return update;}

    TriggerSource source=TriggerSource::none;
    bool trigger_held{};
    if(hand.trigger_click.active) {
        source=TriggerSource::boolean_click;trigger_held=hand.trigger_click.current;
    } else if(hand.trigger.active && std::isfinite(hand.trigger.current)) {
        source=TriggerSource::analog;
        trigger_held=held_ ? hand.trigger.current>0.45f : hand.trigger.current>=0.55f;
    }
    if(source==TriggerSource::none) {armed_=held_=false;trigger_source_=source;}
    else if(source!=trigger_source_) {armed_=held_=false;trigger_source_=source;}

    const bool new_sample=!sequence_seen_ || sample.sequence!=sequence_;
    bool rising{};
    if(new_sample) {
        if(source!=TriggerSource::none) {
            if(!armed_) {
                if(!trigger_held) armed_=true;
            } else rising=trigger_held&&!held_;
            held_=trigger_held;
        }
        sequence_=sample.sequence;sequence_seen_=true;
    }

    const bool surface_changed=surface.submission_serial && surface.submission_serial!=submission_serial_;
    if(surface_changed) submission_serial_=surface.submission_serial;
    if(!context.panel_current || !surface.submission_serial || !surface.source_width || !surface.source_height ||
       surface.source_width>16384 || surface.source_height>16384) return update;
    const auto hit=wawvr::mod::point_at_world_menu_panel(hand.aim,surface.panel);
    if(!hit.valid || !std::isfinite(hit.u) || !std::isfinite(hit.v)) return update;
    update.hit=true;
    update.u=hit.u;update.v=hit.v;
    update.absolute_x=static_cast<int>(std::lround(hit.u*static_cast<float>(surface.source_width-1U)));
    update.absolute_y=static_cast<int>(std::lround(hit.v*static_cast<float>(surface.source_height-1U)));
    update.move=new_sample||surface_changed;
    update.click=rising;
    return update;
}
int key_for(Intent intent) noexcept {
    switch(intent) {
    case Intent::accept:return key_enter;
    case Intent::back:case Intent::pause:return key_escape;
    case Intent::up:return key_up;
    case Intent::down:return key_down;
    default:return 0;
    }
}
TapResult dispatch_tap(const KeyBindings& bindings,int key,unsigned time) noexcept {
    if(!allowed_key(key)) return {TapStatus::invalid_key,0};
    bool held{};
    if(!bindings.dispatch || !bindings.read_key_down ||
       !bindings.read_key_down(bindings.read_context,key,&held)) return {TapStatus::read_failed,0};
    if(held) return {TapStatus::physical_conflict,0};
    const bool pressed=invoke(bindings.dispatch,key,1,time);
    // Even a failed native down may have updated the native key array first.
    // Never skip its release merely because a menu synchronously transitioned.
    const bool released=invoke(bindings.dispatch,key,0,time);
    return {pressed && released?TapStatus::sent:TapStatus::native_failed,released?0:key};
}
bool retry_release(NativeKeyEvent dispatch,int key,unsigned time) noexcept {
    return invoke(dispatch,key,0,time);
}
bool dispatch_cursor(NativeMouseEvent dispatch,int x,int y) noexcept {
    if(!dispatch || x<0 || y<0 || x>16383 || y>16383) return false;
    __try {dispatch(0,x,y);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}

namespace {
constexpr std::uintptr_t key_event_rva=0x187500;
constexpr native_guard::Span key_event_guard{key_event_rva,19,"d781d8bb69d5154f3e9086fb46df24302ac43b90966dd87bd3abb6daeb5e9992"};
constexpr std::uintptr_t mouse_event_rva=0x55380;
constexpr native_guard::Span mouse_event_guard{mouse_event_rva,219,"5cb8e1dcf96bbff8534b8521f46436e73094a0ceb0d69a0bfb57003f1a5ba0f0"};
struct NativeContext final {
    menu_input_detail::Context policy{};
    NativeMenuPage page{};
    unsigned time{};
};
struct NativeCursor final {
    float x{},y{},previous_x{},previous_y{},scale_x{},scale_y{};
    int visible{};
    bool valid{};
};
NativeCursor read_cursor(std::uintptr_t base) noexcept {
    NativeCursor cursor{};
    __try {
        memcpy(&cursor.x,reinterpret_cast<const void*>(base+0x216aa60),4);
        memcpy(&cursor.y,reinterpret_cast<const void*>(base+0x216aa64),4);
        memcpy(&cursor.previous_x,reinterpret_cast<const void*>(base+0x216aa68),4);
        memcpy(&cursor.previous_y,reinterpret_cast<const void*>(base+0x216aa6c),4);
        memcpy(&cursor.visible,reinterpret_cast<const void*>(base+0x216aa70),4);
        memcpy(&cursor.scale_x,reinterpret_cast<const void*>(base+0x878e20),4);
        memcpy(&cursor.scale_y,reinterpret_cast<const void*>(base+0x878e24),4);
        cursor.valid=std::isfinite(cursor.x)&&std::isfinite(cursor.y)&&
                     std::isfinite(cursor.scale_x)&&std::isfinite(cursor.scale_y);
    } __except(EXCEPTION_EXECUTE_HANDLER) {cursor={};}
    return cursor;
}
bool read_context(std::uintptr_t base,NativeContext* output) noexcept {
    if(!output) return false;
    *output={};
    __try {
        memcpy(&output->policy.key_catchers,reinterpret_cast<const void*>(base+0x2510160),4);
        memcpy(&output->policy.connection_state,reinterpret_cast<const void*>(base+0x2510164),4);
        memcpy(&output->time,reinterpret_cast<const void*>(base+0x2081764),4);
        int menu_count{},binding{},editing{};
        memcpy(&menu_count,reinterpret_cast<const void*>(base+0x216bf4c),4);
        memcpy(&binding,reinterpret_cast<const void*>(base+0x21e66f4),4);
        memcpy(&editing,reinterpret_cast<const void*>(base+0x21e66f8),4);
        const bool page_valid=read_native_menu_page(base,&output->page);
        output->policy.menu_exists=menu_count>0 && page_valid && output->page.focused_menu!=0;
        output->policy.native_capture=binding!=0 || editing!=0;
        output->policy.valid=page_valid && output->policy.connection_state>=0 &&
                             output->policy.connection_state<=10 && menu_count>=0;
        return output->policy.valid;
    } __except(EXCEPTION_EXECUTE_HANDLER) {*output={};return false;}
}
bool read_key_down(std::uintptr_t base,int key,bool* held) noexcept {
    if(!held || key<0 || key>255) return false;
    __try {
        int down{};memcpy(&down,reinterpret_cast<const void*>(base+0x8724c4+std::uintptr_t(key)*16),4);
        *held=down!=0;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {*held=false;return false;}
}
bool native_guards_match(std::uintptr_t base) noexcept {
    // These are loaded .text checks, including the native switch tables that
    // establish key154/155 and active-state Escape rather than MP enum guesses.
    __try {
        if(!native_guard::matches(base,key_event_guard) || !native_guard::matches(base,mouse_event_guard)) return false;



        if(bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x3260), 11, "89c14e7fda93a5b618a41a4e1b79bf7ba6cb9d6401d3e7b9b7a226b9c8c543f7") ||
           bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x1d97c0), 12, "c9d13024b0f9cbc3472691dc9f85b14cb07a242ea8e32cc3f5a368c6861e8fd5") ||
           bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x1879d2), 8, "d0eee81ef17e2287e1d7dd36ea6ed9fea467976891f0359de117886761a8cc7f") ||
           bovr::native_guard::compare(reinterpret_cast<const void*>(base+0x1595a0),13,"4cbacc9e2b97cae2c5ac89b9f65d8ae70d19a4fc1c902a79f95fb935cfaeed62")) return false;
        constexpr std::array<std::pair<int,std::uintptr_t>,4> routes{{
            {13,0x1d9e2e},{27,0x1d9b17},{154,0x1d9b01},{155,0x1d9b5e}}};
        for(const auto& route:routes) {
            const auto index=*reinterpret_cast<const unsigned char*>(base+0x1d9eb0+route.first-1);
            if(index>7) return false;
            std::uintptr_t target{};memcpy(&target,reinterpret_cast<const void*>(base+0x1d9e90+index*4),4);
            if(target!=base+route.second) return false;
        }
        const auto pause_index=*reinterpret_cast<const unsigned char*>(base+0x187e04+9);
        if(pause_index!=2) return false;
        std::uintptr_t pause_target{};memcpy(&pause_target,reinterpret_cast<const void*>(base+0x187df4+pause_index*4),4);
        return pause_target==base+0x1879a5;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
struct Adapter final {
    Adapter(std::uintptr_t address,bool left):base(address),policy(left),
        pointer(left),keys{reinterpret_cast<menu_input_detail::NativeKeyEvent>(address+key_event_rva),address,read_key_down},
        mouse(reinterpret_cast<menu_input_detail::NativeMouseEvent>(address+mouse_event_rva)){}
    std::uintptr_t base{};
    menu_input_detail::Policy policy;
    menu_input_detail::PointerPolicy pointer;
    menu_input_detail::KeyBindings keys;
    menu_input_detail::NativeMouseEvent mouse{};
    DWORD owner{};
    std::uint64_t calls{},actions{},pointer_moves{};
    int pointer_x{-1},pointer_y{-1};
    int pending_release{};
    bool busy{},faulted{};
};
Adapter* adapter{};
bool enabled(const wchar_t* name) noexcept {
    wchar_t value[8]{};GetEnvironmentVariableW(name,value,8);return wcscmp(value,L"1")==0;
}
struct BusyGuard {bool& flag; explicit BusyGuard(bool& value):flag(value){flag=true;} ~BusyGuard(){flag=false;}};
}

bool configure_menu_input_adapter(HMODULE executable,bool event_hook) noexcept {
    if(!enabled(L"BOVR_MENU_INPUT_PROBE") || !enabled(L"BOVR_XR_STEREO_PROBE") ||
       !enabled(L"BOVR_MENU_QUAD_PROBE") || !event_hook) return false;
    if(adapter) return true;
    if(executable!=GetModuleHandleW(nullptr) || reinterpret_cast<std::uintptr_t>(executable)!=0x400000) return false;
    try {
        if(!validate_path(executable_path()).accepted) return false;
        const auto base=reinterpret_cast<std::uintptr_t>(executable);
        if(!native_guards_match(base)) return false;
        publish_menu_pointer_visual({},0,0,0);
        adapter=new Adapter(base,enabled(L"BOVR_LEFT_HANDED"));
        trace("t5_menu_input_configured","native=CL_KeyEvent+UI_MouseEvent key_set=13,27,154,155,200 mode=balanced_taps pointer=dominant_aim+trigger game_thread=after_event_drain default=off");
        return true;
    } catch(...) {return false;}
}

void service_menu_input_after_events() noexcept {
    if(!adapter || adapter->busy) return;
    auto& state=*adapter;
    const auto thread=GetCurrentThreadId();
    if(!state.owner) state.owner=thread;
    if(state.owner!=thread) {trace("t5_menu_input_rejected","reason=event_thread_changed owner=%lu actual=%lu",state.owner,thread);return;}
    BusyGuard busy(state.busy);
    ++state.calls;
    NativeContext native{};
    if(!read_context(state.base,&native)) {state.policy.invalidate();publish_menu_pointer_visual({},0,0,0);return;}
    if(state.pending_release) {
        const bool released=menu_input_detail::retry_release(state.keys.dispatch,state.pending_release,native.time);
        trace("t5_menu_input_release_retry","key=%d released=%u",state.pending_release,released);
        if(!released) return;
        state.pending_release=0;
    }
    if(state.faulted) {publish_menu_pointer_visual({},0,0,0);return;}
    wawvr::xr::ActionSnapshot sample{};std::uint64_t generation{};
    const bool live=read_vr_input_snapshot(&sample,&generation);
    MenuPanelSnapshot panel{};
    const bool panel_read=read_menu_panel_snapshot(&panel);
    native.policy.panel_current=panel_read && panel.interactive &&
        panel.generation==generation && panel.connection_state==native.policy.connection_state &&
        panel.key_catchers==native.policy.key_catchers && panel.page==native.page;
    if(state.calls<=8 || state.calls%600==0)
        trace("t5_menu_input_sample","call=%llu live=%u focused=%u generation=%llu sample=%llu panel_current=%u submission=%llu connection=%d catcher=0x%x menu=%u native_capture=%u thread=%lu",
              state.calls,live,sample.focused,generation,sample.sequence,native.policy.panel_current,panel.submission_serial,
              native.policy.connection_state,native.policy.key_catchers,native.policy.menu_exists,native.policy.native_capture,thread);
    const auto intent=state.policy.update(sample,live,generation,native.policy,GetTickCount64());
    const menu_input_detail::PointerSurface surface{panel.panel,panel.submission_serial,panel.source_width,panel.source_height};
    const auto pointer=state.pointer.update(sample,live,generation,native.policy,surface);
    publish_menu_pointer_visual(pointer,generation,sample.sequence,panel.submission_serial);
    const bool cursor_sent=pointer.move && menu_input_detail::dispatch_cursor(state.mouse,pointer.absolute_x,pointer.absolute_y);
    if(pointer.move) {
        const auto n=++state.pointer_moves;
        const bool changed=pointer.absolute_x!=state.pointer_x || pointer.absolute_y!=state.pointer_y;
        state.pointer_x=pointer.absolute_x;state.pointer_y=pointer.absolute_y;
        if(n<=12 || n%240==0 || changed) {
            const auto cursor=read_cursor(state.base);
            trace("t5_menu_pointer_move","x=%d y=%d sent=%u native_valid=%u native=%.3f,%.3f previous=%.3f,%.3f visible=%d scale=%.6f,%.6f generation=%llu sample=%llu submission=%llu count=%llu",
                pointer.absolute_x,pointer.absolute_y,cursor_sent,cursor.valid,cursor.x,cursor.y,cursor.previous_x,cursor.previous_y,
                cursor.visible,cursor.scale_x,cursor.scale_y,generation,sample.sequence,panel.submission_serial,n);
        }
    }
    // Native Back/Pause wins page ownership. A pointed trigger click then wins
    // other conventional navigation so only one synchronous mutation occurs.
    const bool back=intent==menu_input_detail::Intent::back || intent==menu_input_detail::Intent::pause;
    const int key=back?menu_input_detail::key_for(intent):
                  (pointer.click&&cursor_sent?menu_input_detail::key_mouse1:menu_input_detail::key_for(intent));
    if(!key) return;
    const auto result=menu_input_detail::dispatch_tap(state.keys,key,native.time);
    state.pending_release=result.pending_release_key;
    state.policy.invalidate();
    publish_menu_pointer_visual({},0,0,0);
    if(result.status==menu_input_detail::TapStatus::sent || result.status==menu_input_detail::TapStatus::native_failed)
        invalidate_menu_panel_interaction();
    if(result.status==menu_input_detail::TapStatus::native_failed) state.faulted=true;
    trace("t5_menu_input_action","action=%u pointer=%u x=%d y=%d key=%d status=%u generation=%llu sample=%llu submission=%llu connection=%d catcher=0x%x thread=%lu count=%llu pending_release=%d",
          unsigned(intent),key==menu_input_detail::key_mouse1,pointer.absolute_x,pointer.absolute_y,key,unsigned(result.status),generation,sample.sequence,panel.submission_serial,
          native.policy.connection_state,native.policy.key_catchers,thread,++state.actions,state.pending_release);
}
}
