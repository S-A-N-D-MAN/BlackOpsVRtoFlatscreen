// SPDX-License-Identifier: GPL-3.0-only
#include "t5/menu_frame_epoch.hpp"
#include <cstdio>
#include <functional>
#include <cstring>
namespace {
using namespace bovr::t5;
unsigned checks{},failures{};
constexpr std::uintptr_t base=0x400000;
void check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}}
MenuFrameMarker marker(unsigned slot=0,std::uint32_t frame=10) {
    MenuFrameMarker v{};v.native.buffer=base+(slot?0x38c6400:0x3740100);
    v.native.array=base+0x3737090+slot*24;v.native.bytes=0x10000000+slot*0x20000;
    v.native.frame=frame;v.native.capacity=0x18000;v.native.configured_views=1;v.native.renderer_active=1;
    v.thread=17;v.serial=1;v.generation=1;v.revision=3;v.events=5;v.published_ms=1000;v.valid=true;
    v.context.valid=true;v.context.connection_state=3;v.context.key_catchers=0x10;v.context.menu_count=1;
    v.context.page={0x1234,1,2};return v;
}
std::shared_ptr<HudCommandPublication> complete_marker(MenuFrameMarker& v) {
    v.serial+=1;v.published_ms+=10;v.native.shared_commands=v.native.bytes+v.native.used;
    v.native.used+=12;v.native.last=static_cast<std::uint32_t>(v.native.bytes+v.native.used-4);
    v.context.backend=v.native.buffer;v.context.native_frame=v.native.frame;v.context.commands=v.native.shared_commands;
    auto p=std::make_shared<HudCommandPublication>();p->native=v.native;p->bytes.resize(v.native.used);
    const auto offset=v.native.shared_commands-v.native.bytes;
    p->bytes[offset]=8;p->bytes[offset+2]=27;p->bytes[offset+4]=1;
    p->bytes[offset+8]=4;return p;
}
bool matches(MenuFrameEpoch& policy,const MenuFrameMarker& end,FinalizedMenuStamp* out=nullptr) {
    FinalizedMenuStamp local{};return policy.match(end.context,end.generation,end.revision,end.published_ms+1,out?out:&local);
}
void basics() {
    for(unsigned slot=0;slot<2;++slot) {
        MenuFrameEpoch policy(base);auto begin=marker(slot);begin.native.used=begin.native.critical=16;
        check(policy.begin(begin,true)==MenuEpochStatus::accepted,"nonzero source start accepted");
        auto end=begin;auto p=complete_marker(end);
        check(policy.complete(end,base+0x3a18af,p)==MenuEpochStatus::accepted,"exact finalized range accepted");
        FinalizedMenuStamp stamp{};check(matches(policy,end,&stamp)&&stamp.begin.native.used==16&&stamp.epoch==1,"owned begin and epoch retained");
        p.reset();check(matches(policy,end),"publication owning lifetime retained");
        check(policy.complete(end,base+0x3a18af,stamp.commands)==MenuEpochStatus::identity_changed,"repeated end consumes validity");
        check(!matches(policy,end),"repeat end cannot retain submitted identity");
    }
    MenuFrameEpoch policy(base);auto end=marker();auto p=complete_marker(end);
    check(policy.complete(end,base+0x3a18af,p)==MenuEpochStatus::missing_begin,"no backend-time begin fallback");
    auto begin=marker();begin.serial=3;
    check(policy.begin(begin,false)==MenuEpochStatus::wrong_route,"alternate begin route rejected");
    end=begin;p=complete_marker(end);
    check(policy.complete(end,base+0x3a18af,p)==MenuEpochStatus::missing_begin,"alternate route cannot create interval");
}
void begin_rejections() {
    const std::function<void(MenuFrameMarker&)> mutations[]{
        [](auto&v){v.valid=false;},[](auto&v){v.thread=0;},[](auto&v){v.serial=0;},[](auto&v){v.generation=0;},
        [](auto&v){v.revision=0;},[](auto&v){v.events=0;},[](auto&v){v.published_ms=0;},[](auto&v){v.context.valid=false;},
        [](auto&v){v.native.renderer_active=0;},[](auto&v){v.native.buffer+=4;},[](auto&v){v.native.array+=24;},
        [](auto&v){v.native.bytes=0;},[](auto&v){v.native.bytes|=1;},[](auto&v){v.native.capacity=4;},
        [](auto&v){v.native.configured_views=5;},[](auto&v){v.native.used=1;},[](auto&v){v.native.critical=1;},
        [](auto&v){v.native.critical=4;},[](auto&v){v.native.used=v.native.capacity;},[](auto&v){v.native.bytes=0xffff0000;}
    };
    for(const auto& mutate:mutations) {
        MenuFrameEpoch policy(base);auto bad=marker();mutate(bad);
        check(policy.begin(bad,true)!=MenuEpochStatus::accepted,"invalid begin rejected");
        auto end=marker();auto p=complete_marker(end);
        check(policy.complete(end,base+0x3a18af,p)!=MenuEpochStatus::accepted,"bad begin creates no interval");
    }
}
void completion_rejections() {
    const std::function<void(MenuFrameMarker&)> mutations[]{
        [](auto&v){++v.thread;},[](auto&v){++v.generation;},[](auto&v){++v.revision;},[](auto&v){++v.events;},
        [](auto&v){v.serial=1;},[](auto&v){++v.native.frame;},[](auto&v){v.native.bytes+=4;},
        [](auto&v){v.native.capacity*=2;v.native.configured_views=2;},[](auto&v){++v.context.connection_state;},
        [](auto&v){v.context.key_catchers=0;},[](auto&v){++v.context.menu_count;},[](auto&v){++v.context.page.focused_menu;},
        [](auto&v){++v.context.page.stack_count;},[](auto&v){++v.context.page.menu_type;},
        [](auto&v){v.published_ms=999;},[](auto&v){v.published_ms=1251;},[](auto&v){v.native.renderer_active=0;}
    };
    for(const auto& mutate:mutations) {
        MenuFrameEpoch policy(base);auto begin=marker();check(policy.begin(begin,true)==MenuEpochStatus::accepted,"start rejection case");
        auto end=begin;auto p=complete_marker(end);mutate(end);p->native=end.native;
        check(policy.complete(end,base+0x3a18af,p)!=MenuEpochStatus::accepted,"end identity/context/time mismatch rejected");
        check(!matches(policy,end),"rejected completion has no backend identity");
    }
    for(auto route:{base+0x9508e,std::uintptr_t{0},base+0x3a18b0}) {
        MenuFrameEpoch policy(base);auto begin=marker();policy.begin(begin,true);auto end=begin;auto p=complete_marker(end);
        check(policy.complete(end,route,p)==MenuEpochStatus::wrong_route,"nonordinary end caller rejected");
        end.serial+=1;check(policy.complete(end,base+0x3a18af,p)!=MenuEpochStatus::accepted,"wrong end consumes begin");
    }
}
void extent_and_backend() {
    for(unsigned test=0;test<8;++test) {
        MenuFrameEpoch policy(base);auto begin=marker();begin.native.used=16;policy.begin(begin,true);
        auto end=begin;auto p=complete_marker(end);
        switch(test) {
        case 0:p.reset();break;
        case 1:p->native.frame++;break;
        case 2:end.native.shared_commands=end.native.bytes+12;p->native=end.native;break;
        case 3:p->bytes.resize(20);break;
        case 4:p->bytes[16]=0;break;
        case 5:p->bytes[18]=29;break;
        case 6:p->bytes[24]=8;break;
        case 7:p->bytes[26]=27;break;
        }
        check(policy.complete(end,base+0x3a18af,p)==MenuEpochStatus::invalid_extent,"owned finalized interval rejects malformed/stale bytes");
    }
    MenuFrameEpoch policy(base);auto begin=marker();policy.begin(begin,true);auto end=begin;auto p=complete_marker(end);
    check(policy.complete(end,base+0x3a18af,p)==MenuEpochStatus::accepted,"backend fixture finalizes");
    FinalizedMenuStamp out{};
    for(unsigned test=0;test<11;++test) {
        auto backend=end.context;auto generation=end.generation,revision=end.revision,now=end.published_ms;
        switch(test) {
        case 0:backend.valid=false;break;case 1:backend.backend+=4;break;case 2:backend.native_frame++;break;
        case 3:backend.commands+=4;break;case 4:backend.page.focused_menu++;break;case 5:backend.connection_state++;break;
        case 6:backend.key_catchers=0;break;case 7:generation++;break;case 8:revision++;break;
        case 9:now=999;break;case 10:now=1251;break;
        }
        check(!policy.match(backend,generation,revision,now,&out)&&!out.epoch,"current backend guard rejects change");
    }
    check(policy.match(end.context,end.generation,end.revision,1250,&out),"inclusive 250ms lifetime");
    check(!policy.match(end.context,end.generation,end.revision,1011,nullptr),"null output rejected");
    policy.clear();check(!matches(policy,end),"reset invalidates completed identity");
}
void chronology() {
    MenuFrameEpoch policy(base);auto begin=marker();policy.begin(begin,true);auto end=begin;auto p=complete_marker(end);
    policy.complete(end,base+0x3a18af,p);FinalizedMenuStamp old{};matches(policy,end,&old);
    auto next=marker(1,11);next.serial=3;policy.begin(next,true);auto nextend=next;auto nextpub=complete_marker(nextend);
    check(policy.complete(nextend,base+0x3a18af,nextpub)==MenuEpochStatus::accepted&&matches(policy,end),"both finalized SMP slots retained");
    auto reset=marker(0,0);reset.serial=5;check(policy.begin(reset,true)==MenuEpochStatus::accepted,"native frame reset starts new bounded epoch");
    check(!matches(policy,nextend),"reset retires other SMP slot");auto resetend=reset;auto resetpub=complete_marker(resetend);
    check(policy.complete(resetend,base+0x3a18af,resetpub)==MenuEpochStatus::accepted,"new reset extent accepted");
    FinalizedMenuStamp fresh{};check(matches(policy,resetend,&fresh)&&fresh.native_epoch>old.native_epoch&&fresh.epoch>old.epoch,"identities monotonic after native reset");
    auto duplicate=reset;duplicate.serial=7;
    check(policy.begin(duplicate,true)==MenuEpochStatus::duplicate_begin&&!matches(policy,resetend),"repeated native frame cannot renew freshness");
    auto a=marker(0,1);a.serial=8;policy.begin(a,true);auto b=marker(1,2);b.serial=9;policy.begin(b,true);
    auto aend=a;auto ap=complete_marker(aend);aend.serial=10;
    check(policy.complete(aend,base+0x3a18af,ap)==MenuEpochStatus::missing_begin,"intervening begin invalidates older interval");
    auto bend=b;auto bp=complete_marker(bend);bend.serial=11;
    check(policy.complete(bend,base+0x3a18af,bp)==MenuEpochStatus::missing_begin,"unexpected end also consumes newest interval");
    auto gen=marker(1,2);gen.generation=2;gen.serial=12;
    check(policy.begin(gen,true)==MenuEpochStatus::accepted,"new XR generation admits existing frame value");
    auto genend=gen;auto gp=complete_marker(genend);
    check(policy.complete(genend,base+0x3a18af,gp)==MenuEpochStatus::accepted&&matches(policy,genend),"new XR generation finalizes");
}
}
int main(){basics();begin_rejections();completion_rejections();extent_and_backend();chronology();
    std::printf("menu_frame_epoch_checks=%u failures=%u native_calls=0 graphics_calls=0\n",checks,failures);return failures?1:0;}
