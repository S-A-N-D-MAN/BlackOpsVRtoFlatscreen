// SPDX-License-Identifier: GPL-3.0-only
#include "input/command_rig_ledger.hpp"
#include <cstdio>
#include <cstring>
#include <thread>
using namespace bovr::input;
unsigned checks{},failures{};
void check(bool value,const char* name) {++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}}
ProducedRigCommand produced(std::uint64_t serial=1,std::uint64_t now=1000) {
    ProducedRigCommand p{};
    for(unsigned i=0;i<p.bytes.size();++i) p.bytes[i]=static_cast<unsigned char>(i*7+serial);
    const auto native_time=static_cast<std::int32_t>(1000+serial);
    std::memcpy(p.bytes.data(),&native_time,sizeof(native_time));
    p.publication_serial=serial;p.gameplay_epoch=3;p.native_thread=11;p.command_number=static_cast<int>(serial);
    p.observed_ms=now;p.gameplay_active=true;p.rig.xr_generation=2;p.rig.anchor_generation=5;p.rig.published_ms=now-2;
    p.rig.frame.frame_id=42;p.rig.frame.actions.sequence=42;p.rig.frame.actions.focused=true;
    p.rig.frame.should_render=p.rig.frame.views_valid=true;p.rig.frame.predicted_display_time=100000;
    p.rig.frame.actions.hands[1].grip.pose.position={1.25f,2.5f,-3.75f};
    p.rig.tracking_anchor.position={4,5,6};
    return p;
}
ConsumedCommandIdentity consumed(const ProducedRigCommand& p,std::uint64_t scope=1,std::uint64_t now=1001) {
    return {p.bytes,scope,p.gameplay_epoch,p.rig.xr_generation,now,22,true};
}
void native_time(ProducedRigCommand& value,std::int32_t time) {std::memcpy(value.bytes.data(),&time,sizeof(time));}
int main() {
    {
        CommandRigLedger ledger;auto p=produced();CommandRigMatch match{};
        check(ledger.consume(consumed(p),1001,&match)==CommandRigStatus::unavailable&&!match.valid,"empty ledger does not invent association");
        check(ledger.publish(p,1000)==CommandRigStatus::accepted,"valid produced command copied");
        p.bytes.fill(0);p.rig.frame.actions.hands[1].grip.pose.position.x=99;
        const auto original=produced();auto c=consumed(original,2);
        check(ledger.consume(c,1001,&match)==CommandRigStatus::accepted,"actual consumed full fingerprint joins");
        check(match.produced.bytes==original.bytes&&match.produced.rig.frame.actions.hands[1].grip.pose.position.x==1.25f,"caller mutations cannot change owned command or rig");
        check(match.produced.rig.published_ms==998&&match.produced.rig.tracking_anchor.position.x==4,"join preserves original source time and anchor");
        check(match.server_thread==22&&match.produced.native_thread==11,"native server and producer threads may differ");
        check(ledger.current(match,3,2,1002),"only active matching scope permits current association");
        ledger.end_scope(1);check(ledger.current(match,3,2,1002),"unrelated scope exit does not retire current scope");
        ledger.end_scope(2);check(!ledger.current(match,3,2,1002),"actual scope exit retires association");
        check(ledger.consume(consumed(original,3),1002,&match)==CommandRigStatus::replayed&&!match.valid,"same produced command cannot join a second native consumption");
    }
    for(unsigned byte=0;byte<52;++byte) {
        CommandRigLedger ledger;auto p=produced();ledger.publish(p,1000);auto c=consumed(p);c.bytes[byte]^=1;CommandRigMatch match{};
        check(ledger.consume(c,1001,&match)!=CommandRigStatus::accepted,"every opaque native command byte participates in identity");
    }
    {
        CommandRigLedger ledger;auto a=produced(),b=produced(2,1001);b.bytes=a.bytes;ledger.publish(a,1000);ledger.publish(b,1001);CommandRigMatch match{};
        check(ledger.consume(consumed(a,1,1002),1002,&match)==CommandRigStatus::ambiguous,"repeated server time and complete fingerprint are ambiguous");
        b.bytes[51]^=1;ledger.clear();a.publication_serial=3;b.publication_serial=4;a.gameplay_epoch=b.gameplay_epoch=4;ledger.publish(a,1002);ledger.publish(b,1002);
        check(ledger.consume(consumed(a,2,1003),1003,&match)==CommandRigStatus::accepted,"same timestamp with distinct complete bytes can join uniquely");
    }
    for(unsigned variant=0;variant<10;++variant) {
        CommandRigLedger ledger;auto p=produced();ledger.publish(p,1000);auto c=consumed(p);CommandRigMatch match{};
        switch(variant) {
        case 0:c.local_active=false;break;case 1:c.scope_serial=0;break;case 2:c.gameplay_epoch=4;break;
        case 3:c.xr_generation=4;break;case 4:c.native_thread=0;break;case 5:c.observed_ms=999;break;
        case 6:c.observed_ms=1002;break;case 7:c.observed_ms=1;break;case 8:ledger.clear();break;
        case 9:p=produced(2,1001);p.gameplay_active=false;ledger.publish(p,1001);break;
        }
        check(ledger.consume(c,1001,&match)!=CommandRigStatus::accepted&&!match.valid,"invalid consumed context clears output");
    }
    {
        CommandRigLedger ledger;auto p=produced();ledger.publish(p,1000);CommandRigMatch match{};ledger.consume(consumed(p),1001,&match);
        check(!ledger.current(match,3,2,1099),"publication age is measured from original XR publication");
        check(!ledger.current(match,4,2,1001)&&!ledger.current(match,3,4,1001),"current permission must still match gameplay and XR generation");
        auto next=produced(2,1002);next.gameplay_epoch=4;ledger.publish(next,1002);
        check(!ledger.current(match,3,2,1002),"new epoch invalidates copied prior result");
        ledger.clear();check(ledger.publish(p,1003)==CommandRigStatus::replayed,"clear does not reset produced serial highwater");
        check(ledger.consume(consumed(p),1003,&match)==CommandRigStatus::replayed,"clear does not reset native scope serial highwater");
    }
    for(unsigned variant=0;variant<5;++variant) {
        CommandRigLedger ledger;auto p=produced();ledger.publish(p,1000);auto next=produced(2,1001);
        switch(variant) {case 0:next.native_thread=55;break;case 1:next.command_number=0;break;case 2:next.observed_ms=999;break;case 3:next.rig.published_ms=1002;break;case 4:next.rig.frame.actions.focused=false;break;}
        check(ledger.publish(next,1001)!=CommandRigStatus::accepted,"producer discontinuity invalidates ring");
        CommandRigMatch match{};check(ledger.consume(consumed(p),1002,&match)!=CommandRigStatus::accepted,"rejected producer cannot leave old rig permission");
    }
    {
        CommandRigLedger ledger;auto p=produced();ledger.publish(p,1000);CommandRigMatch outer{},inner{};
        ledger.consume(consumed(p),1001,&outer);auto absent=produced(2,1001);
        check(ledger.consume(consumed(absent,2,1002),1002,&inner)==CommandRigStatus::unavailable,"nested absent command rejects");
        check(!ledger.current(outer,3,2,1002),"nested native consumption cannot revive outer scope");
    }
    {
        CommandRigLedger ledger;auto p=produced();CommandRigMatch match{};CommandRigStatus published{},joined{};
        std::thread frontend([&]{published=ledger.publish(p,1000);});frontend.join();
        std::thread server([&]{joined=ledger.consume(consumed(p),1001,&match);});server.join();
        check(published==CommandRigStatus::accepted&&joined==CommandRigStatus::accepted,"synchronized cross-thread copied handoff");
        ledger.clear();check(!ledger.current(match,3,2,1002),"reset invalidates handed-off copy");
    }
    {
        CommandRigLedger ledger;
        for(unsigned i=1;i<=CommandRigLedger::capacity;++i)
            check(ledger.publish(produced(i),1000)==CommandRigStatus::accepted,"bounded ring accepts full capacity");
        check(ledger.publish(produced(129),1000)==CommandRigStatus::capacity_exceeded,"overflow cannot erase a fresh ambiguous fingerprint");
        ledger.clear();check(ledger.publish(produced(130,1050),1050)==CommandRigStatus::capacity_exceeded,"ordinary clear cannot bypass lost-history quarantine");
        check(ledger.publish(produced(131,1101),1101)==CommandRigStatus::accepted,"association rearms after all lost history is stale");
    }
    {
        CommandRigLedger ledger;
        for(unsigned i=1;i<=CommandRigLedger::capacity;++i) ledger.publish(produced(i),1000);
        check(ledger.publish(produced(129,1101),1101)==CommandRigStatus::accepted,"stale history can wrap without unbounded storage");
        CommandRigMatch match{};
        check(ledger.consume(consumed(produced(129,1101),1,1102),1102,&match)==CommandRigStatus::accepted,"new exact entry remains usable after safe wrap");
    }
    {
        CommandRigLedger ledger;const auto old=produced(1,1000);
        for(unsigned i=1;i<=CommandRigLedger::capacity;++i) ledger.publish(produced(i),1000);
        auto repeated=produced(129,1101);repeated.bytes=old.bytes;repeated.rig.frame.actions.hands[1].grip.pose.position.x=99;
        check(ledger.publish(repeated,1101)==CommandRigStatus::regressed,"old fingerprint cannot acquire newer rig after stale wrap");
        CommandRigMatch match{};
        check(ledger.consume(consumed(old,1,1102),1102,&match)!=CommandRigStatus::accepted&&!match.valid,"delayed old consumption cannot borrow rig129");
    }
    {
        CommandRigLedger ledger;auto old=produced();ProducedRigCommand retained{};
        for(unsigned i=1;i<=CommandRigLedger::capacity;++i) {
            auto p=produced(i,i==CommandRigLedger::capacity?1050:1000);native_time(p,1001);
            ledger.publish(p,p.observed_ms);retained=p;
        }
        CommandRigMatch active{};
        check(ledger.consume(consumed(retained,1,1100),1100,&active)==CommandRigStatus::accepted&&ledger.current(active,3,2,1100),"fresh retained command at repeated native time can be uniquely associated before forgetting");
        auto repeated=produced(129,1101);repeated.bytes=old.bytes;
        check(ledger.publish(repeated,1101)==CommandRigStatus::forgotten,"equal native timestamp at stale eviction is rejected without wall-time refresh");
        check(!ledger.current(active,3,2,1101),"forgotten cutoff retires still-fresh retained active match at same native time");
        CommandRigMatch match{};
        check(ledger.consume(consumed(retained,2,1101),1101,&match)==CommandRigStatus::forgotten,"retained fingerprint below cutoff is also ineligible");
        ledger.clear();repeated.publication_serial=130;repeated.command_number=130;
        check(ledger.publish(repeated,1102)==CommandRigStatus::forgotten,"ordinary clear preserves native-time cutoff");
        auto next=produced(131,1102);native_time(next,1002);
        check(ledger.publish(next,1102)==CommandRigStatus::accepted,"strictly newer native time can resume association");
        check(ledger.consume(consumed(old,3,1103),1103,&match)==CommandRigStatus::forgotten,"late evicted command remains rejected after newer native command is published");
        repeated.publication_serial=132;repeated.command_number=132;repeated.gameplay_epoch=4;
        check(ledger.publish(repeated,1103)==CommandRigStatus::accepted,"explicit new gameplay identity can reset native chronology");
        check(ledger.consume(consumed(repeated,4,1103),1103,&match)==CommandRigStatus::accepted,"new identity owns its own command-time history");
        repeated.publication_serial=133;repeated.command_number=133;repeated.rig.xr_generation=3;native_time(repeated,0);
        check(ledger.publish(repeated,1103)==CommandRigStatus::accepted,"explicit new XR identity resets cutoff including a valid native time zero");
    }
    {
        CommandRigLedger ledger;auto early=produced(),late=produced(2,1001);
        ledger.publish(early,1000);ledger.publish(late,1001);CommandRigMatch active{},rejected{};
        check(ledger.consume(consumed(late,1,1002),1002,&active)==CommandRigStatus::accepted,"newest consumed native time establishes chronology");
        check(ledger.consume(consumed(early,2,1003),1003,&rejected)==CommandRigStatus::regressed,"server consumption time regression invalidates association");
        check(!ledger.current(active,3,2,1003),"consumed regression cannot leave prior copied permission alive");
        ledger.clear();late.publication_serial=3;late.command_number=3;late.observed_ms=1003;late.rig.published_ms=1002;
        check(ledger.publish(late,1003)==CommandRigStatus::forgotten,"clear after consumed regression cannot revive same native time");
    }
    {
        CommandRigLedger ledger;auto p=produced();native_time(p,0x7fffffff);
        check(ledger.publish(p,1000)==CommandRigStatus::accepted,"largest valid native signed time is representable");
        auto wrapped=produced(2,1001);native_time(wrapped,static_cast<std::int32_t>(0x80000000u));
        check(ledger.publish(wrapped,1001)==CommandRigStatus::invalid,"native signed timestamp wrap fails closed");
        wrapped=produced(3,1002);native_time(wrapped,1);
        check(ledger.publish(wrapped,1002)==CommandRigStatus::regressed,"low timestamp after wrap cannot silently restart same identity");
        wrapped.publication_serial=4;wrapped.command_number=4;wrapped.gameplay_epoch=4;
        check(ledger.publish(wrapped,1002)==CommandRigStatus::accepted,"new gameplay identity explicitly permits restarted native time");
        auto c=consumed(wrapped,1,1003);c.bytes[3]|=0x80;CommandRigMatch match{};
        check(ledger.consume(c,1003,&match)==CommandRigStatus::invalid&&!match.valid,"invalid native consumed timestamp fails closed");
    }
    std::printf("command_rig_ledger_checks=%u failures=%u native_calls=0 xr_calls=0\n",checks,failures);
    return failures?1:0;
}
