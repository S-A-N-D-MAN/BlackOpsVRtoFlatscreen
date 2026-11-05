// SPDX-License-Identifier: GPL-3.0-only
// Actual adapter commit/cancel functions, owned snapshots and pure interaction.
// Synchronization, role reads and native transfer use compile-time fakes.
// The two retail globals are redirected to owned fixture storage.
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
// Windows reserves the randomized initial stack before CRT initializers, so
// even early VirtualAlloc cannot reliably own the retail addresses. Specialize
// only this test translation unit's uint32 reader to relocate those globals;
// player, variant and definition reads still use their real owned pointers.
static std::uint32_t reload_reader_variants[256]{},reload_reader_multiplier{};
namespace bovr::t5 { namespace {
template<class T>T read(std::uintptr_t);
template<>std::uint32_t read<std::uint32_t>(std::uintptr_t p) {
    constexpr std::uintptr_t variants=0xbe19a8;
    if(p>=variants&&p<variants+sizeof(reload_reader_variants))
        p=reinterpret_cast<std::uintptr_t>(reload_reader_variants)+(p-variants);
    else if(p==0xbcaf84)p=reinterpret_cast<std::uintptr_t>(&reload_reader_multiplier);
    std::uint32_t value{};std::memcpy(&value,reinterpret_cast<const void*>(p),sizeof(value));return value;
}
}}

static std::uint64_t test_now=1000;
static ULONGLONG reload_test_clock(){return test_now;}
#define GetTickCount64 reload_test_clock
#define BOVR_TEST_MANUAL_RELOAD_ADAPTER 1
#include "../src/t5/manual_reload_adapter.cpp"
#undef BOVR_TEST_MANUAL_RELOAD_ADAPTER
#undef GetTickCount64
namespace {
using namespace bovr;namespace xr=wawvr::xr;
unsigned checks{},transfers{},syncs{};bool sync_ok=true,roles_ok=true,pre_ok=true;
unsigned transfer_mode{};
t5::PreCommandSnapshot pre{};t5::Ammo native_ammo{};
void check(bool okay,const char* why){++checks;if(!okay){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
bool synchronize(){++syncs;return sync_ok;}
bool roles(const t5::Publication& p,std::uintptr_t ps,unsigned* role,t5::Ammo* out){
    if(!roles_ok)return false;
    if(ps==p.cg+0x8a364)*role=0;else if(ps==p.server)*role=1;else return false;
    *out=native_ammo;out->ps=ps;return t5::same_definition(*out,p.command_ammo);
}
bool transfer(std::uintptr_t ps,t5::Ammo* out){
    ++transfers;if(ps!=t5::publication.server)return false;
    if(transfer_mode==1)return false;
    native_ammo.clip+=static_cast<int>(t5::publication.interaction.commit_rounds);
    native_ammo.reserve-=static_cast<int>(t5::publication.interaction.commit_rounds);
    if(transfer_mode==2)++native_ammo.clip;
    *out=native_ammo;
    return transfer_mode!=3; // Native changed ammo, but observing its result failed.
}
struct Fixture {
    xr::FrameState frame{};input::manual::Context context{};input::manual::Output output{};
    Fixture(){
        t5::configured=true;t5::controller.reset();t5::publication={};test_now=1000;pre={};
        transfers=syncs=transfer_mode=0;sync_ok=roles_ok=pre_ok=true;
        t5::reload_test_host={synchronize,roles,transfer};
        pre.context.valid=true;pre.context.native_thread=GetCurrentThreadId();pre.context.command_number=30;
        pre.context.connection_state=9;pre.context.native_frame_time=4000;pre.sequence=50;pre.gameplay_epoch=3;
        pre.native_cg_identity=0x6000000;pre.native_game_time=9000;pre.observed_ms=test_now;
        pre.gameplay_active=pre.native_gameplay_fields_valid=true;
        context.valid=context.cod4_controls=true;context.weapon=31;context.clip=4;context.reserve=20;context.capacity=8;
        context.epoch=3;context.generation=7;context.now=test_now;context.mechanism={true};
        frame.views_valid=true;frame.actions.focused=true;frame.head_center.position={0,1.7F,0};
        for(auto& hand:frame.actions.hands){hand.grip.active=hand.grip.position_valid=hand.grip.position_tracked=true;
            hand.grip.orientation_valid=hand.grip.orientation_tracked=true;hand.grip.pose.position={.2F,1.3F,-.35F};
            hand.aim=hand.grip;hand.primary.active=hand.squeeze.active=hand.trigger.active=true;}
        tick();frame.actions.hands[1].primary.current=true;tick();frame.actions.hands[1].primary.current=false;tick();
        auto& support=frame.actions.hands[0];support.grip.pose.position=output.supply;support.squeeze.current=1;tick();
        support.grip.pose.position=output.receiver;tick();support.squeeze.current=0;tick();
        check(output.request_commit&&output.stage==input::manual::Stage::committing,"real physical gesture produces adapter commit request");
        native_ammo={};native_ammo.ps=0x7000000;native_ammo.weapon=context.weapon;native_ammo.primary_weapon=context.weapon;
        native_ammo.variant=0x7200000;native_ammo.definition=0x7300000;native_ammo.clip=context.clip;
        native_ammo.reserve=context.reserve;native_ammo.capacity=context.capacity;native_ammo.flags=t5::disable_reload;
        native_ammo.mechanism=context.mechanism;
        auto& p=t5::publication;p.interaction=output;p.cg=pre.native_cg_identity;p.server=native_ammo.ps;
        p.epoch=context.epoch;p.generation=context.generation;p.transaction=output.transaction;
        p.input_thread=pre.context.native_thread;p.input_command=pre.context.command_number;p.input_pre_sequence=pre.sequence;
        p.updated=p.request_started=test_now;p.gate_active=true;p.command_ammo=native_ammo;
    }
    void tick(){++frame.actions.sequence;context.now=++test_now;output=t5::controller.update(frame,context);pre.observed_ms=test_now;}
    void commit(){const auto copied=t5::publication;t5::commit_on_server_owner(copied,pre.context.native_frame_time);}
};
// The actual protected ammo reader over owned fixture data, without any game
// process, native reload helper, or gameplay writes.
void stack_count_ammo_reader() {
    // Static fixture buffers live in this test image at 0x60000000, keeping
    // their real addresses disjoint from the two redirected retail globals.
    static std::array<unsigned char,0x2000> player{};
    static std::array<unsigned char,0x100> variant{};
    static std::array<unsigned char,0x800> definition{};
    static std::array<unsigned char,0x40> multiplier{};
    const auto ps=reinterpret_cast<std::uintptr_t>(player.data()),var=reinterpret_cast<std::uintptr_t>(variant.data());
    const auto def=reinterpret_cast<std::uintptr_t>(definition.data()),mult=reinterpret_cast<std::uintptr_t>(multiplier.data());
    const auto put=[]<class T>(std::uintptr_t at,T value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));};
    const char name[]="m202_flash_magic_bullet_sp";
    put(ps+0x1c4,100);put(ps+0x144,std::uint8_t{7});reload_reader_variants[7]=var;put(var+8,def);
    put(var,reinterpret_cast<std::uintptr_t>(name));put(var+0x20,4);put(var+0x44,std::uint32_t{1});put(var+0x4c,std::uint32_t{2});
    put(def+0x1c,2);put(def+0x20,6);put(def+0x30,5);reload_reader_multiplier=mult;put(mult+0x18,1.f);
    put(ps+0x344,std::uint32_t{1});put(ps+0x348,12);put(ps+0x3bc,std::uint32_t{2});put(ps+0x3c0,3);
    t5::Ammo first{},one{};
    check(t5::ammo(ps,&first)&&first.weapon==7&&first.clip==3&&first.reserve==12,"actual launcher ammo reader accepts count zero fixture");
    for(std::uint32_t count:{0u,1u,2u,4u,7u,8u,0x80000000u,0xffffffffu}) {put(ps+0x1b0,count);t5::Ammo out{};
        const bool accepted=t5::ammo(ps,&out);
        check(accepted==(count<=7),"native reload reader permits valid native counts and rejects out-of-range values");
        if(accepted)check(out.stack_count==count&&out.clip==3&&out.reserve==12,"ammo observation retains counter without changing ammunition");
    }
    put(ps+0x1b0,std::uint32_t{1});check(t5::ammo(ps,&one)&&!t5::same_definition(first,one),"reload handoff cannot inherit a different observed native stack count");
    // Switching to an ordinary weapon leaves native stackFireCount at one.
    put(ps+0x144,std::uint8_t{9});reload_reader_variants[9]=var;put(def+0x1c,0);put(def+0x20,1);put(def+0x30,0);put(var+0x20,30);
    t5::Ammo ordinary{};check(t5::ammo(ps,&ordinary)&&ordinary.weapon==9&&ordinary.stack_count==1,
        "ordinary weapon reload recovers after a launcher leaves its native count at one");
    std::memset(reload_reader_variants,0,sizeof(reload_reader_variants));reload_reader_multiplier=0;
}
void charging_support_ownership() {
    namespace m=input::manual;
    Fixture f;t5::controller.reset();f.context.weapon=30;f.context.clip=0;f.context.reserve=40;
    f.frame.actions.hands[1].primary.current=false;
    auto& support=f.frame.actions.hands[0];support.squeeze.current=support.trigger.current=0;
    f.tick();f.frame.actions.hands[1].primary.current=true;f.tick();
    f.frame.actions.hands[1].primary.current=false;f.tick();
    support.grip.pose.position=f.output.supply;support.squeeze.current=1;f.tick();
    support.grip.pose.position=f.output.receiver;f.tick();support.squeeze.current=0;f.tick();
    check(f.output.stage==m::Stage::charge&&f.output.supported&&f.output.block_attack&&
        !f.output.held_device&&!f.output.request_commit,"actual empty-rifle insertion waits for a physical charge without holding a device");
    native_ammo.weapon=native_ammo.primary_weapon=f.context.weapon;native_ammo.clip=f.context.clip;native_ammo.reserve=f.context.reserve;
    t5::publication.command_ammo=native_ammo;
    const auto publish=[&]{t5::publication.interaction=f.output;t5::publication.updated=test_now;};
    publish();check(!t5::manual_reload_uses_offhand(),"waiting charge releases support ownership while attack remains blocked");
    std::array<unsigned char,48> command{};command[0x18]=static_cast<unsigned char>(f.context.weapon);
    const std::uint32_t fire_reload=0x88000000u;std::memcpy(command.data()+4,&fire_reload,4);
    std::memcpy(command.data(),&pre.context.native_frame_time,4);
    const auto clip=native_ammo.clip,reserve=native_ammo.reserve;
    t5::apply_manual_reload_command(pre.context,command.data());std::uint32_t buttons{};std::memcpy(&buttons,command.data()+4,4);
    check(!(buttons&fire_reload)&&t5::publication.interaction.block_attack&&!transfers&&
        native_ammo.clip==clip&&native_ammo.reserve==reserve,
        "support availability never permits firing, stock reload or native transfer before physical charging");
    support.grip.pose.position=f.output.handle;support.trigger.current=1;f.tick();publish();
    check(f.output.stage==m::Stage::charging&&t5::manual_reload_uses_offhand(),"acquired charging handle takes back the support hand");
    f.context.cancel_requested=true;f.tick();f.context.cancel_requested=false;publish();
    check(f.output.stage==m::Stage::charge&&f.output.cycle_pending&&f.output.block_attack&&
        !f.output.request_commit&&!t5::manual_reload_uses_offhand(),
        "canceling a charging grab restores support without acknowledging the pending empty action");
    support.grip.position_tracked=false;f.tick();publish();
    check(f.output.stage==m::Stage::charge&&f.output.cycle_pending&&f.output.block_attack&&
        !t5::manual_reload_uses_offhand(),"tracking loss preserves pending action while releasing absent hand geometry");
    support.grip.position_tracked=true;support.trigger.current=0;f.tick();f.tick();publish();
    check(f.output.cycle_pending&&f.output.block_attack&&!f.output.request_commit&&!t5::manual_reload_uses_offhand(),
        "tracking recovery cannot convert a support grip into an acknowledged action or ammo transfer");
    auto value=f.output;
    for(const auto stage:{m::Stage::available,m::Stage::held,m::Stage::insertion,m::Stage::charging,m::Stage::committing}) {
        value.stage=stage;check(m::uses_offhand(value),"active feed, acquired handle and commit stages retain support ownership");
    }
    value.stage=m::Stage::charge;
    for(unsigned held=0;held<3;++held){auto captured=value;
        if(held==0)captured.held_device=true;if(held==1)captured.request_commit=true;if(held==2)captured.pump_gripped=true;
        check(m::uses_offhand(captured),"a waiting-stage flag cannot release a held device, requested commit or captured pump");
    }
    value.supported=false;check(!m::uses_offhand(value),"unsupported publication cannot reserve the support hand");
    value.supported=true;value.stage=m::Stage::held;value.block_attack=false;
    check(!m::uses_offhand(value),"completed nonblocking publication cannot retain a past hand owner");
    t5::publication.gate_active=false;publish();t5::publication.interaction.stage=m::Stage::held;
    check(!t5::manual_reload_uses_offhand(),"retired native gate cannot retain support ownership");
}
}
namespace bovr::t5 {
bool read_pre_command_snapshot(PreCommandSnapshot* out) noexcept{if(!out||!pre_ok)return false;*out=pre;return true;}
bool command_input_generation_current(const input::CommandRigSample&) noexcept{return true;}
}
int main(){
    stack_count_ammo_reader();
    charging_support_ownership();
    {Fixture f;f.commit();check(transfers==1&&t5::publication.committed&&!t5::publication.commit_failed,
        "actual adapter acknowledges one exactly conserved native transfer");
        f.commit();check(transfers==1,"completed claimed transaction cannot be replayed");
        check(!t5::cancel_failed_request(t5::publication,test_now+10000),"completed acknowledgment cannot become timeout failure");}
    for(unsigned mode:{1U,2U,3U}){Fixture f;const auto before_claim=t5::publication;transfer_mode=mode;f.commit();
        check(transfers==1&&t5::publication.commit_claimed&&t5::publication.commit_failed&&!t5::publication.committed,
            "returned failure, changed amount, or unreadable result becomes terminal claimed failure");
        f.commit();check(transfers==1,"uncertain native call is never retried");
        check(t5::cancel_failed_request(t5::publication,test_now),"claimed failure requests gesture cancellation immediately");
        const auto clip=native_ammo.clip,reserve=native_ammo.reserve;
        check(t5::cancel_manual_reload_for_selection(pre.context)&&!t5::publication.interaction.request_commit,
            "actual owner can cancel failed transaction before deliberate selection");
        f.commit();t5::commit_on_server_owner(before_claim,pre.context.native_frame_time);
        check(transfers==1&&native_ammo.clip==clip&&native_ammo.reserve==reserve,
            "cancellation performs no transfer, rollback or fabricated ammo correction");
    }
    {Fixture f;const auto pending=t5::publication;
        check(t5::cancel_manual_reload_for_selection(pre.context),"unclaimed physical request can be canceled before native dispatch");
        t5::commit_on_server_owner(pending,pre.context.native_frame_time);
        check(!transfers&&!t5::publication.interaction.request_commit,"a retained same-command request cannot replay after cancellation clears its claim");}
    {Fixture f;t5::publication.command_ammo.paired=native_ammo.paired=true;
        t5::publication.commit_claimed=t5::publication.pair_failed=true;
        native_ammo.clip=8;native_ammo.reserve=16; // Model only a prior partial pair outcome, never call either hand.
        check(t5::cancel_manual_reload_for_selection(pre.context)&&!t5::publication.interaction.request_commit&&
            !t5::publication.pair_failed&&native_ammo.clip==8&&native_ammo.reserve==16&&!transfers,
            "canceling a partial-pair failure retires its request without replaying or undoing the applied hand");}
    {Fixture f;t5::publication.interaction.request_commit=t5::publication.interaction.block_attack=false;roles_ok=false;
        check(t5::cancel_manual_reload_for_selection(pre.context),"idle publication cannot impose extra native cancellation requirements on ordinary Y selection");}
    {Fixture f;sync_ok=false;for(unsigned i=0;i<10;++i)f.commit();
        check(!transfers&&!t5::publication.commit_claimed,"unavailable native owner stays unclaimed");
        check(!t5::cancel_failed_request(t5::publication,test_now+t5::commit_wait_ms),"ordinary bounded native wait is retained");
        check(t5::cancel_failed_request(t5::publication,test_now+t5::commit_wait_ms+1),"continued preclaim refusal has finite cancellation bound");
        f.context.cancel_requested=true;f.tick();check(!f.output.request_commit&&!f.output.block_attack,
            "timeout cancels current physical request without reporting native completion");}
    {Fixture f;native_ammo.state=11;f.commit();check(!transfers&&!t5::publication.commit_claimed,
        "unsupported native transfer state cannot claim a request");
        check(t5::cancel_failed_request(t5::publication,test_now+t5::commit_wait_ms+1),"persistent native-state refusal can escape through bounded cancellation");}
    for(unsigned fault=0;fault<7;++fault){Fixture f;const auto before=t5::publication;
        auto c=pre.context;
        if(fault==0)++c.command_number;if(fault==1)test_now+=101;if(fault==2)++pre.sequence;
        if(fault==3)++pre.gameplay_epoch;if(fault==4)roles_ok=false;if(fault==5)sync_ok=false;if(fault==6)c.key_catcher=16;
        check(!t5::cancel_manual_reload_for_selection(c)&&t5::publication.interaction.request_commit&&
            t5::publication.transaction==before.transaction&&!transfers,"stale or unowned selection cannot cancel someone else's gesture");
    }
    {Fixture f;bool accepted=true;std::thread other([&]{accepted=t5::cancel_manual_reload_for_selection(pre.context);});other.join();
        check(!accepted&&t5::publication.interaction.request_commit&&!transfers,"foreign thread cannot cancel or transfer current reload");}
    std::printf("%u actual reload adapter lifecycle checks passed; native calls=0 game calls=0\n",checks);
}
