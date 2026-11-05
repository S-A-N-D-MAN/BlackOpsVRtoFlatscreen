// SPDX-License-Identifier: GPL-3.0-only
// Exercise the production before/after callbacks using owned native storage.
#define main legacy_vehicle_aim_main
#include "native_command_aim_adapter_tests.cpp"
#undef main

namespace {
int vehicle_mode{},apc_seat{1};
bool vehicle_copy(std::uintptr_t,const t5::PreCommandSnapshot& pre,const t5::CommandContext&,
                  std::uint64_t,t5::motorcycle::Facts* out) noexcept {
    if(vehicle_mode==3){*out={};return false;}
    *out={};auto& f=*out;f.pre=pre;f.cg=pre.native_cg_identity;f.native_time=pre.native_game_time;
    f.health=100;f.pm_type=1;f.entity_flags=0x4000;f.weapon_flags=0x02000080;
    f.vehicle_index=vehicle_mode==2?253:(vehicle_mode?756:700);f.vehicle_entity=0x1a796f8+f.vehicle_index*0x34c;
    f.script_vehicle=0x1ca4430;f.info=0x1cd97a8;f.vehicle_pos=vehicle_mode?1:0;f.vehicle_type=vehicle_mode?0:1;
    const char* name=vehicle_mode==2?"apc_btr60_grenade":(vehicle_mode?"truck_gaz63_player_single50":"motorcycle_player");
    if(vehicle_mode==2){f.camera[6]=4;f.entity_flags=0xa4002;f.weapon_flags=0x02000000;f.vehicle_pos=apc_seat;}
    std::memcpy(f.vehicle_name.data(),name,std::strlen(name)+1);return true;
}
struct RideFixture {
    Fixture native{true,true};
    RideFixture(int mode) {
        vehicle_mode=mode;apc_seat=1;native.aim.exact_profile=true;
        native.controls.cod4_controls=true;native.controls.motorcycle_enabled=true;
        native.controls.copy_motorcycle=vehicle_copy;
        for(auto& h:actions.hands) {
            h.grip.active=h.grip.orientation_valid=h.grip.position_valid=true;
            h.grip.orientation_tracked=h.grip.position_tracked=true;
            h.trigger.active=h.trigger_click.active=h.squeeze.active=h.stick.active=true;
            h.primary.active=h.secondary.active=true;
        }
        t5::pre_snapshot.gameplay_active=false;
        t5::pre_snapshot.native_pm_type=1;t5::pre_snapshot.native_weapon_flags=mode==2?0x02000000:0x02000080;
        t5::pre_snapshot.native_entity_flags=mode==2?0xa4002:0x4000;
    }
    void step() {
        native.next();aim_now+=16;++actions.sequence;
        t5::pre_snapshot.observed_ms=aim_now;
        rig.frame.actions=actions;rig.published_ms=aim_now;
        t5::before_native(&native.controls,native.context());
    }
    unsigned count(const char* prefix) const {
        return unsigned(std::count_if(dispatches.begin(),dispatches.end(),
            [&](const auto& s){return s.starts_with(prefix);}));
    }
};
}
int main() {
    for(int mode:{0,1,2}) {
        const bool gunner=mode!=0,apc=mode==2;
        RideFixture f(mode);f.step();
        check(f.native.controls.motorcycle_active,"actual callback admits copied bike, truck or BTR gunner");
        actions.hands[1].trigger.current=1;actions.hands[0].stick.current={.6f,.8f};f.step();
        check(f.count("+attack 28727 ")==1&&f.count("+vehicleattack 28722 ")==(apc?0u:1u),
              "actual callback supplies seated attack without BTR driver-fire authority");
        check(gunner?f.count("+gas ")==0:f.count("+gas ")==1,"seated gunner never acquires driver gas");
        auto command=f.native.bytes();auto before=command;
        t5::after_native(&f.native.controls,f.native.context(),command.data());
        if(gunner)check(command==before,"seated gunner leaves every native movement byte unchanged");
        else check(command[0x1b]!=before[0x1b]&&command[0x1c]!=before[0x1c],"motorcycle still writes throttle and steering");
        actions.hands[1].aim.pose.orientation={0,0.17364818f,0,0.98480775f};f.step();
        const auto yaw=*reinterpret_cast<float*>(f.native.image+t5::yaw_rva);
        check(gunner?(std::fabs(yaw)>19.f&&std::fabs(yaw)<21.f):yaw==0.f,
              "seated gunner uses controller look; motorcycle leaves riding camera alone");
        f.step();
        check(*reinterpret_cast<float*>(f.native.image+t5::yaw_rva)==yaw,"held controller orientation cannot repeatedly rotate native view");
        actions.hands[1].trigger.current=0;f.step();
        check(f.count("-attack 28727 ")==1&&f.count("-vehicleattack 28722 ")==(apc?0u:1u),"fire commands have balanced releases");
        actions.hands[0].trigger.current=1;f.step();
        check(f.count("+frag 28728 ")==(apc?1u:0u)&&f.count("+vehicleattacksecond 28723 ")==(apc?0u:1u),
              "BTR secondary uses native frag; bike and truck secondary routing stays unchanged");
        actions.hands[1].trigger.current=1;f.step();
        t5::pre_snapshot.context.key_catcher=0x10;f.step();
        check(f.count("-attack 28727 ")==2&&f.count("-vehicleattack 28722 ")==(apc?0u:2u),"pause releases primary firing paths");
        check(f.count("-frag 28728 ")==(apc?1u:0u)&&f.count("-vehicleattacksecond 28723 ")==(apc?0u:1u),
              "pause releases secondary command with its original name");
        if(apc) {
            t5::pre_snapshot.context.key_catcher=0;f.step();
            check(f.count("+attack 28727 ")==2&&f.count("+frag 28728 ")==1,"held triggers cannot fire on BTR pause resume");
            actions.hands[0].trigger.current=actions.hands[1].trigger.current=0;f.step();
            actions.hands[0].trigger.current=actions.hands[1].trigger.current=1;f.step();
            check(f.count("+attack 28727 ")==3&&f.count("+frag 28728 ")==2,"fresh neutral rearms both BTR weapons");
            actions.focused=false;f.step();
            check(f.count("-attack 28727 ")==3&&f.count("-frag 28728 ")==2,"focus loss releases both BTR weapons");
            actions.focused=true;actions.hands[0].trigger.current=actions.hands[1].trigger.current=0;f.step();
            actions.hands[0].trigger.current=actions.hands[1].trigger.current=1;f.step();
            apc_seat=2;f.step();
            check(!f.native.controls.motorcycle_active&&f.count("-attack 28727 ")==4&&f.count("-frag 28728 ")==3,
                  "leaving BTR player seat revokes and balances its complete input lease");
            apc_seat=1;actions.hands[0].trigger.current=actions.hands[1].trigger.current=0;f.step();
            actions.hands[0].trigger.current=actions.hands[1].trigger.current=1;f.step();
            const auto yaw_before_tracking_loss=*reinterpret_cast<float*>(f.native.image+t5::yaw_rva);
            actions.hands[1].grip.orientation_tracked=false;
            actions.hands[1].aim.pose.orientation={0,-0.17364818f,0,0.98480775f};f.step();
            check(f.count("-attack 28727 ")==5&&f.count("-frag 28728 ")==4,
                  "controller tracking loss immediately releases both BTR weapons");
            check(*reinterpret_cast<float*>(f.native.image+t5::yaw_rva)==yaw_before_tracking_loss,
                  "lost controller tracking cannot rotate BTR gun");
            actions.hands[1].grip.orientation_tracked=true;f.step();
            check(f.count("+attack 28727 ")==5&&f.count("+frag 28728 ")==4,
                  "tracking return with triggers held does not reacquire weapons");
            actions.hands[0].trigger.current=actions.hands[1].trigger.current=0;f.step();
            actions.hands[0].trigger.current=actions.hands[1].trigger.current=1;f.step();
            vehicle_mode=3;t5::pre_snapshot.native_pm_type=0;t5::pre_snapshot.native_weapon_flags=0;
            t5::pre_snapshot.native_entity_flags=0;t5::pre_snapshot.gameplay_active=true;f.step();
            check(!f.native.controls.motorcycle_active&&f.count("-attack 28727 ")==6&&f.count("-frag 28728 ")==5&&
                  std::none_of(f.native.controls.motorcycle_keys.begin(),f.native.controls.motorcycle_keys.end(),[](bool value){return value;}),
                  "return to ordinary walking releases every APC lease without retaining gunner control");
            check(f.count("+gas ")==0&&f.count("+reverse ")==0&&f.count("+handbrake ")==0,
                  "BTR path retains all driving authority throughout input transitions");
        }
    }
    std::printf("motorcycle_input_callback_checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
