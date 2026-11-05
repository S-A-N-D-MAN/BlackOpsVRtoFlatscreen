// SPDX-License-Identifier: GPL-3.0-only
#include "t5/ordinary_event_fx.hpp"
#include <cstdio>
#include <climits>
#include <initializer_list>
#include <limits>

namespace {
namespace fx=bovr::t5::ordinary_event_fx;
unsigned checks{},failures{};
void check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL %s\n",why);}}
fx::Witness witness(){
    fx::Witness w{};
    for(unsigned i=0;i<w.command.size();++i)w.command[i]=static_cast<unsigned char>(i*37+11);
    w.cg=0x10000000;w.client=0x12000000;w.publication=123;w.epoch=7;w.revision=9;w.observed=1000;
    w.use=3;w.network_use=4;w.birth=500;w.sequence=16;w.event=29;w.parm=87;w.weapon=4;
    return w;
}
bool find(const fx::Ledger& ledger,const fx::Witness& key,std::uint64_t now,fx::Witness* out){
    return ledger.find(key.cg,key.sequence,key.event,key.parm,key.weapon,now,out);
}
void ledger_contract(){
    const auto w=witness();fx::Ledger ledger{};fx::Witness out{};
    check(!find(ledger,w,1000,&out),"empty ledger has no inferred current-command witness");
    ledger.add(w);
    check(find(ledger,w,1000,&out)&&out==w,"exact event returns complete immutable command and owner witness");
    check(!find(ledger,w,1000,nullptr),"null output refused");
    for(auto now:{999ull,1101ull,(std::numeric_limits<unsigned long long>::max)()}){
        out=w;out.publication=999;const auto before=out;
        check(!find(ledger,w,now,&out)&&out==before,"expired or reversed clock refuses without changing output");
    }
    check(find(ledger,w,1100,&out)&&out==w,"exact one-hundred-millisecond boundary is valid");
    for(unsigned field=0;field<5;++field){
        auto wrong=w;
        if(field==0)++wrong.cg;if(field==1)++wrong.sequence;if(field==2)++wrong.event;
        if(field==3)++wrong.parm;if(field==4)++wrong.weapon;
        out=w;out.publication=999;const auto before=out;
        check(!find(ledger,wrong,1001,&out)&&out==before,"foreign event key never selects latest command fallback");
    }
    auto invalid=w;invalid.sequence=256;
    check(!find(ledger,invalid,1001,&out),"out-of-range sequence refused");
    auto replay=w;replay.observed=1099;ledger.add(replay);
    check(ledger.next==1&&find(ledger,w,1099,&out)&&out.observed==1000,
          "identical prediction replay coalesces without refreshing original timestamp");
    check(!find(ledger,w,1101,&out),"replay cannot extend stale command lifetime");
    for(unsigned byte=0;byte<w.command.size();++byte){
        ledger={};ledger.add(w);auto conflicting=w;conflicting.command[byte]^=1;ledger.add(conflicting);
        out=w;out.publication=999;const auto before=out;
        check(!find(ledger,w,1001,&out)&&out==before,"every one of fifty-two conflicting command bytes makes event ambiguous");
    }
    for(unsigned field=0;field<7;++field){
        ledger={};ledger.add(w);auto conflicting=w;
        if(field==0)++conflicting.publication;if(field==1)++conflicting.epoch;
        if(field==2)++conflicting.revision;if(field==3)++conflicting.client;
        if(field==4)++conflicting.use;if(field==5)++conflicting.network_use;if(field==6)++conflicting.birth;
        ledger.add(conflicting);
        check(!find(ledger,w,1001,&out),"same event with different publication or incarnation is ambiguous");
    }
    ledger={};auto no_publication=w;no_publication.publication=0;ledger.add(no_publication);
    check(!find(ledger,w,1001,&out),"missing publication is never evidence");
    ledger={};auto no_time=w;no_time.observed=0;ledger.add(no_time);
    check(!find(ledger,w,1,&out),"missing observation time is never evidence");
    ledger={};ledger.add(w);
    for(unsigned i=1;i<=64;++i){auto newer=w;newer.sequence=w.sequence+i;newer.publication+=i;ledger.add(newer);}
    check(!find(ledger,w,1001,&out),"bounded ledger evicts old witness instead of retaining an unbounded history");
}
fx::Cursor cursor(){
    fx::Cursor c{};c.fire_return=0x64b86b;c.entity_return=0x78206f;
    c.check_return=0x56e892;c.transition_return=0x5c3901;
    c.ps=c.expected_ps=0x1008a364;c.ops=c.expected_ops=0x1008bdc4;
    c.event=c.ring_event=29;c.parm=c.ring_parm=87;c.sequence=17;c.previous=13;c.cursor=13;return c;
}
void cursor_contract(){
    const auto exact=cursor();check(fx::predicted_cursor(exact),"exact ordinary native predicted event stack admitted");
    for(int i=13;i<17;++i){auto c=exact;c.cursor=i;check(fx::predicted_cursor(c),"each of four actually appended event slots admitted");}
    for(unsigned field=0;field<6;++field){
        auto c=exact;
        if(field==0)++c.fire_return;if(field==1)++c.entity_return;if(field==2)++c.check_return;
        if(field==3)++c.transition_return;if(field==4)++c.ps;if(field==5)++c.ops;
        check(!fx::predicted_cursor(c),"foreign native caller or player-state pointer rejected");
    }
    for(unsigned event=0;event<256;++event){auto c=exact;c.event=c.ring_event=event;
        check(fx::predicted_cursor(c)==(event==29||event==30),"only ordinary right fire and last-shot events admitted");}
    {auto c=exact;++c.ring_event;check(!fx::predicted_cursor(c),"ring event must match current event argument");}
    {auto c=exact;++c.ring_parm;check(!fx::predicted_cursor(c),"ring parameter must match current event argument");}
    for(int value:{-1,12,17,18}){auto c=exact;c.cursor=value;check(!fx::predicted_cursor(c),"event cursor outside latest four and final sequence rejected");}
    {auto c=exact;c.previous=14;check(!fx::predicted_cursor(c),"cursor before old player-state event sequence rejected");}
    {auto c=exact;c.previous=18;check(!fx::predicted_cursor(c),"reversed native event batch rejected");}
    {auto c=exact;c.sequence=256;check(!fx::predicted_cursor(c),"non-byte current event sequence rejected");}
    {auto c=exact;c.sequence=0;c.previous=c.cursor=-1;
        check(fx::predicted_cursor(c),"native rebased negative cursor represents wrap event 255");
        c.cursor=255;c.previous=255;check(!fx::predicted_cursor(c),"unrebased cursor cannot impersonate wrap batch");}
}
void math_and_ownership(){
    check(fx::decode(0)==fx::Angles{0,0},"zero packed command angles decode exactly");
    check(fx::decode(0x40002000)==fx::Angles{45,90},"packed command pitch and yaw order is exact");
    check(fx::decode(0x8000c000)==fx::Angles{-90,-180},"signed packed angle wrap is exact");
    check(fx::decode(0x7fff8000)==fx::Angles{-180,32767.f*(360.f/65536.f)},"signed endpoints preserve native multiplier");
    const fx::Angles selected{45,90};
    for(unsigned written=0;written<4;++written){
        check(fx::restore_mask(true,written,selected,selected)==written,"only successfully owned equal fields may restore");
        check(fx::restore_mask(false,written,selected,selected)==0,"retired incarnation never restores either field");
        check(fx::restore_mask(true,written,selected,{12,90})==(written&2),"native changed pitch remains native");
        check(fx::restore_mask(true,written,selected,{45,12})==(written&1),"native changed yaw remains native");
        check(fx::restore_mask(true,written,selected,{12,13})==0,"both native changed fields remain native");
    }
    check(fx::restore_mask(true,3,{0,0},{-0.f,0})==2,"ownership compares signed-zero bit pattern rather than numerical equality");
    check(fx::restore_mask(true,0x80,selected,selected)==0,"unknown ownership bits cannot grant restoration");
    for(unsigned before=0;before<256;++before)for(unsigned count=0;count<256;++count){
        const auto after=(before+count)&255u;
        check(fx::appended(before,after)==(count<=4?count:0),"only bounded native appended event count is admitted across byte wrap");
    }
    check(fx::appended(255,0)==1&&fx::appended(254,2)==4,"wrap produces exact one-slot and four-slot counts");
    check(!fx::appended(256,0)&&!fx::appended(0,256)&&!fx::appended(UINT_MAX,0),"out-of-range sequence values are refused");
}
}
int main(){
    ledger_contract();cursor_contract();math_and_ownership();
    std::printf("%u checks, %u failures; native_calls=0 game_calls=0\n",checks,failures);
    return failures?1:0;
}
