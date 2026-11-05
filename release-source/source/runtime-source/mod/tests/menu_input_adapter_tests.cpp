// SPDX-License-Identifier: GPL-3.0-only
#include "t5/menu_input_adapter.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>
namespace detail=bovr::t5::menu_input_detail;
namespace xr=wawvr::xr;
namespace {
unsigned failures{},checks{};
void check(bool passed,const char* name) {
    ++checks;if(!passed)++failures;printf("%s %s\n",passed?"PASS":"FAIL",name);
}
struct Call {int local{},key{},down{};unsigned time{};};
std::array<Call,4096> calls{};unsigned call_count{};
bool physical_down{},fail_read{},fail_down{},fail_up{},native_held{};
struct MouseCall {int local{},x{},y{};};
std::array<MouseCall,64> mouse_calls{};unsigned mouse_call_count{};bool fail_mouse{};
void __cdecl fake_native_key(int local,int key,int down,unsigned time) {
    calls[call_count++]={local,key,down,time};
    if(down) native_held=true;
    if((down && fail_down) || (!down && fail_up)) RaiseException(0xE000B017,0,0,nullptr);
    if(!down) native_held=false;
}
static_assert(std::is_same_v<decltype(&fake_native_key),detail::NativeKeyEvent>);
void __cdecl fake_native_mouse(int local,int x,int y) {
    mouse_calls[mouse_call_count++]={local,x,y};
    if(fail_mouse) RaiseException(0xE000B018,0,0,nullptr);
}
static_assert(std::is_same_v<decltype(&fake_native_mouse),detail::NativeMouseEvent>);
bool fake_key_down(std::uintptr_t context,int key,bool* out) noexcept {
    if(context!=0x1234 || !out || key<0 || key>255 || fail_read) return false;
    *out=physical_down;return true;
}
xr::ActionSnapshot neutral(std::uint64_t sequence) {
    xr::ActionSnapshot s{};s.sequence=sequence;s.focused=true;s.menu.active=true;
    for(auto& hand:s.hands) {
        hand.grip.active=hand.grip.position_valid=hand.grip.orientation_valid=true;
        hand.grip.position_tracked=hand.grip.orientation_tracked=true;
        hand.primary.active=hand.secondary.active=hand.stick.active=true;
    }
    return s;
}
}
int main() {
    std::array<unsigned char,0x21d8> ui{};
    std::array<unsigned char,0x58> first_menu{},second_menu{};
    const auto fake_base=reinterpret_cast<std::uintptr_t>(ui.data())-0x216aa50;
    auto put=[&](std::size_t offset,const auto& value){memcpy(ui.data()+offset,&value,sizeof(value));};
    const auto first=reinterpret_cast<std::uintptr_t>(first_menu.data());
    const auto second=reinterpret_cast<std::uintptr_t>(second_menu.data());
    unsigned focused=6,unfocused=2;int count=2,type=3,index=0;
    put(0,index);put(0x1580,count);put(0x21d4,type);put(0x1500,first);put(0x1508,second);
    memcpy(first_menu.data()+0x54,&focused,4);memcpy(second_menu.data()+0x54,&unfocused,4);
    bovr::t5::NativeMenuPage page{};
    check(bovr::t5::read_native_menu_page(fake_base,&page) && page.focused_menu==first &&
          page.stack_count==2 && page.menu_type==3,"page reader scans native stack backward for visible focused flags");
    memcpy(second_menu.data()+0x54,&focused,4);
    check(bovr::t5::read_native_menu_page(fake_base,&page) && page.focused_menu==second,
          "topmost focused native page wins");
    const auto previous=page;count=1;put(0x1580,count);
    check(bovr::t5::read_native_menu_page(fake_base,&page) && !(page==previous),
          "physical same-catcher page pop changes page identity");
    count=17;put(0x1580,count);
    check(!bovr::t5::read_native_menu_page(fake_base,&page),"page reader rejects native stack overflow");
    count=2;index=1;put(0x1580,count);put(0,index);
    check(!bovr::t5::read_native_menu_page(fake_base,&page),"page reader rejects unverified split-screen context");
    index=0;put(0,index);const std::uintptr_t null_menu=0;put(0x1508,null_menu);
    check(!bovr::t5::read_native_menu_page(fake_base,&page),"page reader rejects null native menu entry");
    auto* inaccessible=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    check(inaccessible && !bovr::t5::read_native_menu_page(
              reinterpret_cast<std::uintptr_t>(inaccessible)-0x216aa50,&page),
          "unreadable native page fails closed without game write");
    if(inaccessible) VirtualFree(inaccessible,0,MEM_RELEASE);

    check(!bovr::t5::configure_menu_input_adapter(GetModuleHandleW(nullptr),false),
          "configuration refuses missing event hook regardless of opt-in flags");

    detail::KeyBindings binding{fake_native_key,0x1234,fake_key_down};
    for(const int key:{13,27,154,155,200}) {
        const auto start=call_count;
        const auto result=detail::dispatch_tap(binding,key,0xABCDEF12);
        check(result.status==detail::TapStatus::sent && result.pending_release_key==0 &&
              call_count==start+2 && calls[start].local==0 && calls[start].key==key &&
              calls[start].down==1 && calls[start+1].down==0 &&
              calls[start].time==0xABCDEF12 && calls[start+1].time==0xABCDEF12 && !native_held,
              "cdecl four-argument native key tap preserves local/key/time and balances release");
    }
    volatile unsigned stack_sentinel=0x12345678;
    for(int i=0;i<1000;++i) static_cast<void>(detail::dispatch_tap(binding,detail::key_up,unsigned(i)));
    check(stack_sentinel==0x12345678 && !native_held && call_count==2010,"1000 x86 native tap ABI calls preserve stack and ownership");
    auto start=call_count;
    check(detail::dispatch_tap(binding,28672,1).status==detail::TapStatus::invalid_key && call_count==start,
          "gameplay synthetic command ID cannot index native key array");
    physical_down=true;
    check(detail::dispatch_tap(binding,13,1).status==detail::TapStatus::physical_conflict && call_count==start,
          "physical held key receives no synthetic press or release");physical_down=false;
    fail_read=true;
    check(detail::dispatch_tap(binding,13,1).status==detail::TapStatus::read_failed && call_count==start,
          "failed native ownership read refuses dispatch");fail_read=false;
    fail_down=true;
    auto result=detail::dispatch_tap(binding,13,9);
    check(result.status==detail::TapStatus::native_failed && !result.pending_release_key &&
          call_count==start+2 && !native_held,"partial native down SEH still attempts balanced up");fail_down=false;
    fail_up=true;
    result=detail::dispatch_tap(binding,27,10);
    check(result.status==detail::TapStatus::native_failed && result.pending_release_key==27 && native_held,
          "failed native up reports exact pending release");fail_up=false;
    check(detail::retry_release(binding.dispatch,result.pending_release_key,11) && !native_held,
          "owner-thread retry releases pending key without another down");
    check(detail::dispatch_cursor(fake_native_mouse,960,540) && mouse_call_count==1 &&
          mouse_calls[0].local==0 && mouse_calls[0].x==960 && mouse_calls[0].y==540,
          "cdecl UI mouse dispatch preserves absolute native pixel coordinates");
    check(!detail::dispatch_cursor(fake_native_mouse,-1,0) && !detail::dispatch_cursor(fake_native_mouse,0,16384) &&
          mouse_call_count==1,"native cursor dispatcher rejects out-of-range coordinates");
    fail_mouse=true;
    check(!detail::dispatch_cursor(fake_native_mouse,1,2) && mouse_call_count==2,
          "UI mouse SEH fails closed on the owner thread");
    fail_mouse=false;

    detail::Context menu{true,true,false,true,0,0x10};
    detail::Context game{true,false,false,false,10,0};
    detail::Policy p;
    auto s=neutral(1);
    check(p.update(s,true,1,menu,1)==detail::Intent::none,"neutral visible menu arms without action");
    s.sequence++;s.hands[1].primary.current=true;
    check(p.update(s,true,1,menu,2)==detail::Intent::accept,"dominant primary accepts native focused item");
    check(p.update(s,true,1,menu,3)==detail::Intent::none,"repeated event drain does not duplicate action");
    p.invalidate();
    check(p.update(s,true,1,menu,4)==detail::Intent::none,"same publication cannot rearm after UI action");
    s.sequence++;
    check(p.update(s,true,1,menu,5)==detail::Intent::none,"held primary cannot activate newly submitted page");
    s=neutral(++s.sequence);p.update(s,true,1,menu,6);
    s.sequence++;s.hands[1].secondary.current=true;
    check(p.update(s,true,1,menu,7)==detail::Intent::back,"dominant secondary goes back");
    s=neutral(++s.sequence);p.update(s,true,1,menu,8);
    s.sequence++;s.hands[0].stick.current.y=1;
    check(p.update(s,true,1,menu,9)==detail::Intent::up,"support stick up chooses verified key154");
    s.sequence++;check(p.update(s,true,1,menu,10)==detail::Intent::none,"held navigation does not repeat every XR frame");
    s=neutral(++s.sequence);p.update(s,true,1,menu,11);
    s.sequence++;s.hands[0].stick.current.y=-1;
    check(p.update(s,true,1,menu,12)==detail::Intent::down,"center then down chooses verified key155");
    s=neutral(++s.sequence);p.update(s,true,1,game,13);
    s.sequence++;s.menu.current=true;
    check(p.update(s,true,1,game,14)==detail::Intent::pause,"active gameplay XR menu button opens native pause without panel");
    s.sequence++;check(p.update(s,true,1,menu,15)==detail::Intent::none,"held pause button cannot immediately close menu");
    s=neutral(++s.sequence);p.update(s,true,1,menu,16);
    s.sequence++;s.menu.current=true;s.hands[1].primary.current=true;
    check(p.update(s,true,1,menu,17)==detail::Intent::back,"back wins simultaneous menu accept");
    s=neutral(++s.sequence);p.update(s,true,1,menu,18);
    s.sequence++;s.focused=false;s.hands[1].primary.current=true;
    check(p.update(s,true,1,menu,19)==detail::Intent::none,"focus loss disarms native menu input");
    s.sequence++;s.focused=true;
    check(p.update(s,true,1,menu,20)==detail::Intent::none,"focus recovery while held does not click");
    s=neutral(++s.sequence);p.update(s,true,1,menu,21);
    s.sequence++;s.hands[0].grip.position_tracked=false;s.hands[1].primary.current=true;
    check(p.update(s,true,1,menu,22)==detail::Intent::none,"tracking loss disarms native input");
    s.sequence++;s.hands[0].grip.position_tracked=true;
    check(p.update(s,true,1,menu,23)==detail::Intent::none,"tracking recovery held primary remains disarmed");
    s=neutral(++s.sequence);p.update(s,true,1,menu,24);
    s.sequence++;s.hands[1].primary.current=true;
    check(p.update(s,true,2,menu,25)==detail::Intent::none,"XR generation transition requires neutral before click");
    s=neutral(++s.sequence);p.update(s,true,2,menu,26);
    auto stale=menu;stale.panel_current=false;
    s.sequence++;s.hands[1].primary.current=true;
    check(p.update(s,true,2,stale,27)==detail::Intent::none,"unsubmitted or stale menu pixels cannot accept");
    s.sequence++;
    check(p.update(s,true,2,menu,28)==detail::Intent::none,"page recovery does not turn held action into click");
    for(unsigned reason=0;reason<4;++reason) {
        detail::Policy guarded;auto c=menu;s=neutral(100+reason*2);guarded.update(s,true,1,c,100);
        if(reason==0)c.native_capture=true;
        if(reason==1)c.key_catchers=0x11;
        if(reason==2)c.menu_exists=false;
        if(reason==3)c.valid=false;
        s.sequence++;s.hands[1].primary.current=true;
        check(guarded.update(s,true,1,c,101)==detail::Intent::none,"native capture/console/no menu/invalid context blocks navigation");
    }
    detail::Policy expired;s=neutral(300);expired.update(s,true,1,menu,1);
    check(expired.update(s,true,1,menu,252)==detail::Intent::none,"duplicate sample ages out after250ms");
    s.sequence++;s.hands[1].primary.current=true;
    check(expired.update(s,true,1,menu,253)==detail::Intent::none,"expired neutral cannot arm a later held press");
    detail::Policy stale_neutral;s=neutral(350);stale_neutral.update(s,true,1,menu,1);stale_neutral.invalidate();
    stale_neutral.update(s,true,1,menu,2);s.sequence++;s.hands[1].primary.current=true;
    check(stale_neutral.update(s,true,1,menu,3)==detail::Intent::none,"old neutral duplicate cannot rearm after page invalidation");
    detail::Policy left(true);s=neutral(400);left.update(s,true,1,menu,1);
    s.sequence++;s.hands[0].primary.current=true;
    check(left.update(s,true,1,menu,2)==detail::Intent::accept,"left-handed primary follows dominant hand");

    detail::PointerSurface surface{};
    surface.panel.pose.position.z=-2.0f;surface.panel.size_meters={2.0f,1.0f};
    surface.submission_serial=10;surface.source_width=1920;surface.source_height=1080;
    detail::PointerPolicy pointer;
    s=neutral(500);auto& right=s.hands[1];
    right.aim.active=right.aim.position_valid=right.aim.orientation_valid=true;
    right.aim.position_tracked=right.aim.orientation_tracked=true;
    right.trigger_click.active=true;right.trigger_click.current=true;
    auto pointed=pointer.update(s,true,7,menu,surface);
    check(pointed.hit&&pointed.move&&!pointed.click&&pointed.absolute_x==960&&pointed.absolute_y==540,
          "held trigger on pointer activation moves cursor but cannot click");
    check(std::fabs(pointed.u-0.5f)<0.001f&&std::fabs(pointed.v-0.5f)<0.001f,
          "pointer hit publishes normalized compositor coordinates");
    s.sequence++;right.trigger_click.current=false;
    pointed=pointer.update(s,true,7,menu,surface);
    check(pointed.hit&&pointed.move&&!pointed.click,"tracked release arms the menu pointer");
    s.sequence++;right.trigger_click.current=true;
    pointed=pointer.update(s,true,7,menu,surface);
    check(pointed.hit&&pointed.move&&pointed.click,"fresh trigger edge clicks the pointed native item once");
    pointed=pointer.update(s,true,7,menu,surface);
    check(pointed.hit&&!pointed.move&&!pointed.click,"repeated event drain cannot move or click a duplicate XR sample");
    ++surface.submission_serial;
    pointed=pointer.update(s,true,7,menu,surface);
    check(pointed.hit&&pointed.move&&!pointed.click,"freshly submitted page accepts position without replaying held click");
    s.sequence++;right.trigger_click.current=false;right.aim.pose.position.x=3.0f;
    pointed=pointer.update(s,true,7,menu,surface);
    check(!pointed.hit&&!pointed.click,"controller ray miss never clamps to a menu edge");
    s.sequence++;right.trigger_click.current=true;
    pointed=pointer.update(s,true,7,menu,surface);
    check(!pointed.hit&&!pointed.click,"off-panel trigger press consumes its edge without selecting");
    s.sequence++;right.aim.pose.position.x=0.0f;
    pointed=pointer.update(s,true,7,menu,surface);
    check(pointed.hit&&!pointed.click,"entering the panel while trigger remains held cannot click");
    s.sequence++;s.focused=false;
    check(!pointer.update(s,true,7,menu,surface).hit,"focus loss invalidates pointer ownership");
    s.sequence++;s.focused=true;right.trigger_click.current=true;
    pointed=pointer.update(s,true,7,menu,surface);
    check(pointed.hit&&!pointed.click,"focus recovery while held requires a release before clicking");
    s.sequence++;right.trigger_click.current=false;pointer.update(s,true,7,menu,surface);
    s.sequence++;right.trigger_click.current=true;
    check(pointer.update(s,true,7,menu,surface).click,"release after focus recovery rearms pointer click");
    auto stale_pointer=menu;stale_pointer.panel_current=false;
    s.sequence++;right.trigger_click.current=false;pointer.update(s,true,7,stale_pointer,surface);
    s.sequence++;right.trigger_click.current=true;
    check(!pointer.update(s,true,7,stale_pointer,surface).click,
          "unsubmitted menu pixels cannot receive a pointer click");
    detail::PointerPolicy analog_pointer;
    s=neutral(600);right=s.hands[1];
    right.aim.active=right.aim.position_valid=right.aim.orientation_valid=true;
    right.aim.position_tracked=right.aim.orientation_tracked=true;
    right.trigger.active=true;right.trigger.current=0.0f;
    analog_pointer.update(s,true,8,menu,surface);
    s.sequence++;right.trigger.current=0.56f;
    check(analog_pointer.update(s,true,8,menu,surface).click,"analog trigger crosses 0.55 press threshold once");
    s.sequence++;right.trigger.current=0.50f;
    check(!analog_pointer.update(s,true,8,menu,surface).click,"analog trigger hysteresis retains held state above 0.45");
    s.sequence++;right.trigger.current=0.44f;analog_pointer.update(s,true,8,menu,surface);
    s.sequence++;right.trigger.current=0.56f;
    check(analog_pointer.update(s,true,8,menu,surface).click,"analog release below 0.45 rearms a later press");
    detail::PointerPolicy left_pointer(true);s=neutral(700);auto& left_hand=s.hands[0];
    left_hand.aim.active=left_hand.aim.position_valid=left_hand.aim.orientation_valid=true;
    left_hand.aim.position_tracked=left_hand.aim.orientation_tracked=true;
    left_hand.trigger_click.active=true;left_hand.trigger_click.current=false;
    left_pointer.update(s,true,9,menu,surface);s.sequence++;left_hand.trigger_click.current=true;
    check(left_pointer.update(s,true,9,menu,surface).click,"left-handed menu pointer follows configured dominant controller");
    detail::PointerPolicy position_only;s=neutral(800);right=s.hands[1];
    right.aim.active=right.aim.position_valid=right.aim.orientation_valid=true;
    right.aim.position_tracked=right.aim.orientation_tracked=true;
    check(position_only.update(s,true,10,menu,surface).move &&
          !position_only.update(s,true,10,menu,surface).move,
          "inactive trigger action still deduplicates pointer movement by XR sample");
    printf("%u checks %u failures; native_game_dispatches=0 game_launches=0 graphics=0\n",checks,failures);
    return failures?1:0;
}
