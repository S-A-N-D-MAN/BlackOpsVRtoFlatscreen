// SPDX-License-Identifier: GPL-3.0-only
// Actual production before/after callbacks plus the actual protected pilot graph
// reader over sparse fixture memory. No retail process, XR runtime or GPU calls.
#define main legacy_helicopter_aim_main
#include "native_command_aim_adapter_tests.cpp"
#undef main
#define main legacy_helicopter_reader_main
#define Memory HelicopterMemory
#define check helicopter_reader_check
#define checks helicopter_reader_checks
#define failures helicopter_reader_failures
#include "helicopter_context_tests.cpp"
#undef failures
#undef checks
#undef check
#undef Memory
#undef main
#include <stdexcept>

namespace {
struct HelicopterFixture;
HelicopterFixture* helicopter_fixture{};
unsigned helicopter_delivery_effect{};
void __cdecl helicopter_sink(int, int, const char*);
bool helicopter_copy(std::uintptr_t, const t5::PreCommandSnapshot&, const t5::CommandContext&,
                     std::uint64_t, hc::Facts*) noexcept;
bool helicopter_look(std::uintptr_t, float, float) noexcept;

struct HelicopterFixture {
    HelicopterMemory memory;
    Fixture native;
    t5::Adapter controls;
    struct Look { float pitch{}, yaw{}; };
    std::vector<Look> looks;
    unsigned copies{};
    bool look_ok{true};
    HelicopterFixture() : native(true, true), controls(native.aim.base, false, true, true, true, {}) {
        helicopter_fixture = this;
        controls.execute = helicopter_sink;
        controls.helicopter_enabled = true;
        controls.copy_helicopter = helicopter_copy;
        controls.write_helicopter_look = helicopter_look;
        controls.cod4_controls = true;
        controls.router = input::ActionRouter(false, true);
        native.aim.exact_profile = true;
        helicopter_delivery_effect = 0;
        on_rig_read = nullptr;
        generation_checks = 0;
        actions.menu.active = true;
        for (auto& h : actions.hands) {
            h.grip.active = h.grip.position_valid = h.grip.position_tracked = true;
            h.grip.orientation_valid = h.grip.orientation_tracked = true;
            h.trigger.active = h.trigger_click.active = h.squeeze.active = h.stick.active = true;
            h.primary.active = h.secondary.active = h.stick_click.active = true;
        }
        memory.thread = GetCurrentThreadId();
        memory.pre.context.native_thread = memory.thread;
        sync();
        hc::Facts facts{};
        check(memory.read(&facts) && hc::eligible(facts), "helicopter fixture joins actual protected native pilot graph");
    }
    ~HelicopterFixture() { helicopter_fixture = nullptr; on_rig_read = nullptr; }
    void sync() {
        memory.now = aim_now;
        memory.pre.observed_ms = aim_now;
        memory.put<int>(0x2993850, memory.pre.context.command_number);
        memory.put<int>(0x2481764, memory.pre.context.native_frame_time);
        memory.put<int>(0x2910160, memory.pre.context.key_catcher);
        memory.put<int>(0x2910164, memory.pre.context.connection_state);
        memory.put<int>(0x2911e2c, memory.pre.context.native_override ? 1 : 0);
        memory.put<int>(HelicopterMemory::cg + 0x8a348, memory.pre.native_game_time);
        t5::pre_snapshot = memory.pre;
        rig.frame.actions = actions;
        rig.published_ms = aim_now;
        memory.reads.clear(); memory.pre_reads = 0;
    }
    void prepare(bool fresh = true) {
        ++memory.pre.sequence;
        ++memory.pre.context.command_number;
        memory.pre.context.native_frame_time += 16;
        memory.pre.native_game_time += 16;
        aim_now += 16;
        if (fresh) ++actions.sequence;
        sync();
    }
    void invoke() { t5::before_native(&controls, memory.pre.context); }
    void step(bool fresh = true) { prepare(fresh); invoke(); }
    std::size_t count(const char* prefix) const {
        return static_cast<std::size_t>(std::count_if(dispatches.begin(), dispatches.end(),
            [&](const auto& text) { return text.starts_with(prefix); }));
    }
    void unchanged_post(const char* reason) {
        auto command = native.bytes(), before = command;
        t5::after_native(&controls, memory.pre.context, command.data());
        check(command == before, reason);
        check(!native.aim.admission_serial.load(), "helicopter never publishes handheld tracked-aim admission");
    }
};

bool helicopter_copy(std::uintptr_t base, const t5::PreCommandSnapshot& pre, const t5::CommandContext& context,
                     std::uint64_t now, hc::Facts* out) noexcept {
    auto& f = *helicopter_fixture;
    if (base != f.native.aim.base) return false;
    ++f.copies;
    f.memory.reads.clear(); f.memory.pre_reads = 0;
    // Injectable base only translates fixture storage. The actual graph reader,
    // exact retail offsets, eligibility and two-pass/pre/thread checks still run.
    return hc::copy_current(f.memory.host(), HelicopterMemory::base, pre, context, now, out);
}
bool helicopter_look(std::uintptr_t base, float first, float second) noexcept {
    auto& f = *helicopter_fixture;
    if (base != f.native.aim.base) return false;
    f.looks.push_back({first, second});
    return f.look_ok && t5::set_helicopter_look(base, first, second);
}
void __cdecl helicopter_sink(int, int, const char* command) {
    dispatches.emplace_back(command);
    const unsigned effect = helicopter_delivery_effect;
    helicopter_delivery_effect = 0;
    auto& f = *helicopter_fixture;
    if (effect == 1) f.memory.put<U32>(HelicopterMemory::scr + 0x1f0, 3); // Native script took AI control.
    if (effect == 2) {
        t5::before_native(&f.controls, f.memory.pre.context);
        f.unchanged_post("nested helicopter post leaves all52 native command bytes unchanged");
    }
    if (effect == 3) {
        std::thread foreign([&] {
            auto context = f.memory.pre.context; context.native_thread = GetCurrentThreadId();
            t5::before_native(&f.controls, context);
            auto command_bytes = f.native.bytes(), saved = command_bytes;
            t5::after_native(&f.controls, context, command_bytes.data());
            check(command_bytes == saved, "foreign helicopter post leaves all52 native command bytes unchanged");
        });
        foreign.join();
    }
    if (effect == 4) throw std::runtime_error("native-like helicopter side effect then throw");
}
bool close_angle(float a, float b) { return std::fabs(a - b) < .0001f; }
void commanded_axes(HelicopterFixture& f, std::int8_t forward, std::int8_t right,
                    std::int8_t pitch, std::int8_t yaw, const t5::CommandContext* context = nullptr) {
    auto bytes = f.native.bytes(), expected = bytes;
    if (forward || right) {
        std::memcpy(expected.data() + 0x1b, &forward, 1); std::memcpy(expected.data() + 0x1c, &right, 1);
    }
    if (pitch || yaw) {
        std::memcpy(expected.data() + 0x1e, &pitch, 1); std::memcpy(expected.data() + 0x1f, &yaw, 1);
    }
    t5::after_native(&f.controls, context ? *context : f.memory.pre.context, bytes.data());
    check(bytes == expected, "actual helicopter post writes only1B/1C movement and1E/1F look bytes");
    check(!f.controls.helicopter_occurrence && !f.native.aim.admission_serial.load(),
          "post consumes exact occurrence and never admits handheld command aim");
    f.unchanged_post("second post cannot replay previously admitted helicopter axes");
}
void native_edges() {
    HelicopterFixture f; f.step();
    check(f.controls.helicopter_active && dispatches.empty(), "fresh neutral admits pilot without any synthetic native press");
    f.unchanged_post("neutral helicopter input preserves desktop command fields");
    actions.hands[1].trigger.current = actions.hands[0].trigger.current = 1;
    actions.hands[1].primary.current = actions.hands[0].primary.current = true;
    actions.hands[1].squeeze.current = 1; f.step();
    const std::vector<std::string> expected{"+attack 28700 9032", "+throw 28701 9032",
        "+vehiclemoveup 28702 9032", "+reload 28704 9032", "+activate 28705 9032", "+vehicleattack 28706 9032"};
    check(dispatches == expected, "actual callback maps five simultaneous nonopposed inputs to exact dedicated keys");
    check(f.controls.cycle_requests == 0 && f.count("+speed_throw ") == 0 && f.count("+gostand ") == 0,
          "helicopter controls do not invoke firearm alternate ADS or jump");
    f.unchanged_post("button-only helicopter callback leaves every command byte native-owned");
    f.step(false); f.unchanged_post("duplicate held button-only post remains native-owned");
    f.step(); f.unchanged_post("fresh held button-only post remains native-owned");
    check(dispatches == expected, "duplicate and fresh held samples do not repeat native down commands");
    actions.hands[1].trigger.current = actions.hands[0].trigger.current = 0;
    actions.hands[1].primary.current = actions.hands[0].primary.current = false;
    actions.hands[1].squeeze.current = 0; f.step();
    check(f.count("-attack 28700 ") == 1 && f.count("-throw 28701 ") == 1 &&
          f.count("-vehiclemoveup 28702 ") == 1 && f.count("-reload 28704 ") == 1 &&
          f.count("-activate 28705 ") == 1 && f.count("-vehicleattack 28706 ") == 1 && dispatches.size() == 12,
          "each helicopter key receives exactly one distinct balanced release");
    actions.hands[1].secondary.current = true; f.step();
    check(f.count("+vehiclemovedown 28703 ") == 1, "B uses the sixth dedicated native descend key");
    actions.hands[1].primary.current = true; f.step();
    check(f.count("-vehiclemovedown 28703 ") == 1 && f.count("+vehiclemoveup 28702 ") == 1,
          "opposed A and B release altitude keys instead of issuing simultaneous native directions");
    actions.hands[1].secondary.current = false; f.step();
    check(f.count("+vehiclemoveup 28702 ") == 2, "fresh B release restores the still-held A intent");
}
void axes_and_look() {
    HelicopterFixture f; f.step();
    check(f.looks.empty(), "first neutral occurrence has no look integration interval");
    actions.hands[0].stick.current = {-.6f, 1}; actions.hands[1].stick.current = {1, -1}; f.step();
    check(f.looks.size() == 1 && close_angle(f.looks.back().pitch, -.8f + 70.f * .016f) &&
          close_angle(f.looks.back().yaw, 75.9f - 90.f * .016f),
          "actual pre integrates signed native pitch and yaw in degrees from the16ms interval");
    check(close_angle(*reinterpret_cast<float*>(f.native.image + t5::yaw_rva - 4), f.looks.back().pitch) &&
          close_angle(*reinterpret_cast<float*>(f.native.image + t5::yaw_rva), f.looks.back().yaw),
          "actual protected look writer updates both owned native angle producer slots");
    auto native_context = f.memory.pre.context;
    native_context.viewangles[0] += 5; native_context.viewangles[1] -= 3;
    commanded_axes(f, 127, -64, 127, -127, &native_context);
    check(f.copies >= 4, "accepted delivery repeatedly joins the actual protected graph");
    f.step(false); commanded_axes(f, 127, -64, 127, -127);
    actions.hands[0].stick.current = {}; f.step(); commanded_axes(f, 0, 0, 127, -127);
    actions.hands[1].stick.current = {}; f.step(); f.unchanged_post("two neutral groups preserve all original desktop bytes");
    actions.hands[0].stick.current = {1, 0}; f.step(); commanded_axes(f, 0, 127, 0, 0);
    actions.hands[0].stick.current = {}; actions.hands[1].stick.current = {1, 1};
    f.memory.pre.context.viewangles[0] = 130.f;
    aim_now += 100; f.step();
    check(close_angle(f.looks.back().pitch, 126.5f) && close_angle(f.looks.back().yaw, 71.4f),
          "look interval caps at50ms without incorrectly clamping raw native delta-angle pitch to89degrees");
    commanded_axes(f, 0, 0, -127, -127);
}
void before_refusals() {
    for (unsigned fault = 0; fault < 16; ++fault) {
        HelicopterFixture f; f.step(); actions.hands[1].trigger.current = 1;
        actions.hands[0].stick.current = {1, 1}; actions.hands[1].stick.current = {1, 1}; f.step();
        commanded_axes(f, 127, 127, -127, -127);
        f.prepare();
        switch (fault) {
        case 0: f.memory.put<U32>(HelicopterMemory::scr + 0x1f0, 3); break;
        case 1: f.memory.put<int>(HelicopterMemory::scr + 0x620, 1); break;
        case 2: f.memory.put<U32>(HelicopterMemory::vehicle + 0x148, 0); break;
        case 3: f.memory.put<int>(HelicopterMemory::server + 0x1c4, 0); break;
        case 4: actions.menu.current = true; break;
        case 5: actions.focused = false; break;
        case 6: actions.hands[0].grip.position_tracked = false; break;
        case 7: ++rig.xr_generation; break;
        case 8: generation_current = false; break;
        case 9: rig_live = false; break;
        case 10: f.memory.pre.context.key_catcher = 16; break;
        case 11: f.memory.pre.context.native_override = true; break;
        case 12: f.memory.pre.sequence = f.controls.helicopter_pre.sequence; break;
        case 13: f.memory.pre.context.command_number = f.controls.helicopter_pre.context.command_number; break;
        case 14: f.memory.pre.context.native_frame_time = f.controls.helicopter_pre.context.native_frame_time - 1; break;
        case 15: f.memory.pre.native_game_time = f.controls.helicopter_pre.native_game_time - 1; break;
        }
        f.sync(); const auto look_count = f.looks.size(); f.invoke();
        check(f.count("+attack 28700 ") == 1 && f.count("-attack 28700 ") == 1 && !f.controls.helicopter_keys[0] &&
              f.count("+vehicleattack 28706 ")==1&&f.count("-vehicleattack 28706 ")==1&&!f.controls.helicopter_keys[6],
              "context menu graph generation tracking or progression loss releases the exact owned cannon key");
        check(f.looks.size() == look_count, "rejected helicopter occurrence cannot deliver stale native look");
        f.unchanged_post("rejected helicopter before cannot modify later native command fields");
        f.step();
        check(f.count("+attack 28700 ") == 1, "held cannon does not reacquire after a refused before occurrence");
    }
    {
        HelicopterFixture f; f.step(); f.prepare(); actions.hands[1].trigger.current = 1;
        f.sync(); f.memory.fail_read = 1; f.invoke();
        check(dispatches.empty(), "failed protected native graph copy cannot emit a helicopter command");
        f.unchanged_post("failed graph copy leaves native result untouched");
    }
    {
        HelicopterFixture f; f.step(); actions.hands[1].trigger.current = 1; f.step();
        f.prepare(); ++f.memory.pre.gameplay_epoch; f.sync(); f.invoke();
        check(f.count("-attack 28700 ") == 1 && f.count("+attack 28700 ") == 1,
              "new gameplay epoch releases old held key and requires neutral for the new epoch");
        f.unchanged_post("new epoch held controls do not revive old analog occurrence");
    }
}
void post_refusals() {
    for (unsigned fault = 0; fault < 6; ++fault) {
        HelicopterFixture f; f.step(); actions.hands[1].trigger.current = 1;
        actions.hands[0].stick.current = {1, 1}; actions.hands[1].stick.current = {1, 1}; f.step();
        switch (fault) {
        case 0: f.memory.put<U32>(HelicopterMemory::scr + 0x1f0, 0x81); break;
        case 1: generation_current = false; break;
        case 2: ++f.memory.pre.sequence; t5::pre_snapshot = f.memory.pre; break;
        case 3: aim_now += 101; break;
        case 4: f.memory.fail_read = 1; break;
        case 5: ++f.memory.pre.context.command_number; break;
        }
        f.unchanged_post("post rechecks graph generation pre time and command before writing any helicopter bytes");
        check(f.controls.helicopter_release_pending, "refused post schedules balanced owner cleanup");
        f.memory.fail_read = 0; f.step();
        check(f.count("-attack 28700 ") == 1 && f.count("+attack 28700 ") == 1,
              "next owner callback releases held key after post refusal without replaying down");
    }
}
void interruption_and_writer_failure() {
    for (unsigned effect = 1; effect <= 4; ++effect) {
        HelicopterFixture f; f.step(); f.prepare();
        actions.hands[1].trigger.current = actions.hands[0].trigger.current = 1;
        actions.hands[1].stick.current = {1, 1}; f.sync(); helicopter_delivery_effect = effect;
        try { f.invoke(); } catch (const std::runtime_error&) {}
        check(f.count("+attack 28700 ") == 1 && f.count("+throw 28701 ") == 0 && f.looks.empty(),
              "native script takeover nested foreign or exceptional dispatch cancels every later press and look");
        f.unchanged_post("interrupted native delivery cannot leave a usable analog post claim");
        f.step();
        check(f.count("-attack 28700 ") == 1 && f.count("+attack 28700 ") == 1,
              "interrupted cannon down receives one owner cleanup release and never repeats down");
    }
    {
        HelicopterFixture f; f.step(); actions.hands[1].trigger.current = 1; f.step();
        actions.hands[1].trigger.current = 0; f.prepare(); helicopter_delivery_effect = 4;
        try { f.invoke(); } catch (const std::runtime_error&) {}
        check(f.count("-attack 28700 ") == 1 && f.controls.helicopter_keys[0],
              "uncertain throwing release retains owned key for an idempotent retry");
        f.unchanged_post("uncertain native release forbids an analog post");
        f.step();
        check(f.count("-attack 28700 ") == 2 && !f.controls.helicopter_keys[0] && f.count("+attack 28700 ") == 1,
              "owner retries only the explicit keyed release after uncertainty");
    }
    {
        HelicopterFixture f; f.step(); actions.hands[1].trigger.current = 1; f.step();
        actions.hands[1].stick.current = {1, 1}; f.look_ok = false; f.step();
        check(f.count("-attack 28700 ") == 1 && !f.controls.helicopter_keys[0],
              "failed native angle write releases retained helicopter buttons");
        f.unchanged_post("failed native look write cancels all pending axis writes");
    }
}
void redemption_path_and_flight() {
    HelicopterFixture f;
    f.memory.huey(); f.sync(); f.step();
    check(f.controls.helicopter_active && f.controls.helicopter_facts.scripted_huey,
          "Redemption disabled-personal-weapon driver enters actual helicopter callbacks");
    actions.hands[0].stick.current={0,1}; actions.hands[1].stick.current={1,0};
    actions.hands[0].trigger.current=actions.hands[1].trigger.current=1;
    actions.hands[1].primary.current=true; f.step();
    check(f.count("+attack 28700 ")==1 && f.count("+vehicleattacksecond 28709 ")==1 && f.count("+throw 28701 ")==1 &&
          f.count("+vehicleattack ")==0,
          "scripted approach routes both cannon consumers and the mission rocket button");
    check(f.count("+vehiclemoveup 28702 ")==0 && f.looks.empty(),
          "authored path suppresses flight and raw steering while script weapons stay available");
    f.unchanged_post("authored approach never replaces native flight command axes");
    for(unsigned n=0;n<8;++n) {
        f.step(n%2==0); f.unchanged_post("duplicate and fresh approach input leave native flight owned by mission");
    }
    check(f.count("+attack 28700 ")==1 && f.count("-attack 28700 ")==0,
          "approach holds keep one cannon lease across repeated XR publications");

    f.memory.put<U32>(HelicopterMemory::scr+0x1f0,0x201); f.step();
    check(f.controls.helicopter_active && hc::flight_control_allowed(f.controls.helicopter_facts) &&
          f.count("-attack 28700 ")==1 && f.count("-throw 28701 ")==1 &&
          f.count("-vehicleattacksecond 28709 ")==1 && f.count("+vehiclemoveup 28702 ")==0,
          "ReturnPlayerControl balances old leases and requires neutral before newly acquired flight");
    f.unchanged_post("held approach sticks cannot begin flight on authority transition");
    actions.hands[0].stick.current=actions.hands[1].stick.current={};
    actions.hands[0].trigger.current=actions.hands[1].trigger.current=0;
    actions.hands[1].primary.current=false; f.step();
    actions.hands[0].stick.current={0,1}; actions.hands[1].stick.current={1,0};
    actions.hands[1].trigger.current=1; actions.hands[1].primary.current=true; f.step();
    check(f.count("+attack 28700 ")==2 && f.count("+vehicleattacksecond 28709 ")==2 &&
          f.count("+vehiclemoveup 28702 ")==1 && f.looks.size()==1,
          "fresh neutral restores the earlier helicopter flight fire and altitude controls");
    commanded_axes(f,127,0,0,-127);

    f.memory.put<U32>(HelicopterMemory::scr+0x1f0,0x281); f.step();
    check(f.count("-vehiclemoveup 28702 ")==1 && f.count("-attack 28700 ")==2 && f.looks.size()==1,
          "return to authored flight releases pilot ascent and firing without an extra steering write");
    f.unchanged_post("return to authored path stops VR flight bytes");
    actions.hands[0].stick.current=actions.hands[1].stick.current={};
    actions.hands[1].primary.current=false; actions.hands[1].trigger.current=0; f.step();
    actions.hands[1].trigger.current=1; f.step();
    f.memory.pre.context.key_catcher=16; f.step();
    check(f.count("+attack 28700 ")==3 && f.count("-attack 28700 ")==3 &&
          f.count("+vehicleattacksecond 28709 ")==3 && f.count("-vehicleattacksecond 28709 ")==3 &&
          f.count("+vehicleattack ")==0 && f.count("-vehicleattack ")==0,
          "pausing Redemption balances both physical cannon trigger leases");
    f.unchanged_post("paused Redemption cannot deliver stale flight axes");
}
void redemption_authority_changes_during_command() {
    for(bool path:{false,true}) {
        HelicopterFixture f; f.memory.huey(path); f.sync(); f.step();
        actions.hands[1].trigger.current=1; actions.hands[0].stick.current={0,1}; f.step();
        f.memory.put<U32>(HelicopterMemory::scr+0x1f0,path?0x201:0x281);
        f.unchanged_post("flight authority changing between callbacks rejects the old command output");
        check(f.controls.helicopter_release_pending,
              "eligible but differently owned flight graph schedules balanced cleanup");
        f.step();
        check(f.count("+attack 28700 ")==1 && f.count("-attack 28700 ")==1 &&
              f.count("+vehicleattacksecond 28709 ")==1 && f.count("-vehicleattacksecond 28709 ")==1,
              "authority change during command releases both guns without held reacquisition");
    }
}
void redemption_tracking_rearm() {
    for(bool path:{false,true})for(unsigned hand:{0u,1u}) {
        HelicopterFixture f;f.memory.huey(path);f.sync();f.step();
        actions.hands[1].trigger.current=actions.hands[0].trigger.current=1;f.step();
        check(f.count("+attack 28700 ")==1&&f.count("+vehicleattacksecond 28709 ")==1&&f.count("+throw 28701 ")==1,
              "each Huey flight phase acquires the two real cannon consumers and rockets");
        actions.hands[hand].grip.orientation_tracked=false;f.step();
        check(f.count("-attack 28700 ")==1&&f.count("-vehicleattacksecond 28709 ")==1&&f.count("-throw 28701 ")==1,
              "either controller tracking loss releases every held Huey weapon input");
        f.unchanged_post("lost tracking cannot deliver Huey flight axes");
        actions.hands[hand].grip.orientation_tracked=true;f.step(false);f.step();
        check(f.count("+attack 28700 ")==1&&f.count("+vehicleattacksecond 28709 ")==1&&f.count("+throw 28701 ")==1,
              "tracking recovery with duplicate or fresh held triggers cannot rearm Huey weapons");
        actions.hands[1].trigger.current=actions.hands[0].trigger.current=0;f.step();
        actions.hands[1].trigger.current=actions.hands[0].trigger.current=1;f.step();
        check(f.count("+attack 28700 ")==2&&f.count("+vehicleattacksecond 28709 ")==2&&f.count("+throw 28701 ")==2,
              "fresh neutral then press rearms the Huey weapon consumers together");
        f.memory.pre.context.key_catcher=16;f.step();
        check(f.count("-attack 28700 ")==2&&f.count("-vehicleattacksecond 28709 ")==2&&f.count("-throw 28701 ")==2&&
              f.count("+vehicleattack ")==0&&f.count("-vehicleattack ")==0,
              "rearmed Huey weapons release on pause without using the Hind firing slot");
    }
}
} // namespace

int main() {
    native_edges(); axes_and_look(); before_refusals(); post_refusals(); interruption_and_writer_failure();
    redemption_path_and_flight();
    redemption_authority_changes_during_command();
    redemption_tracking_rearm();
    std::printf("helicopter actual input adapter: %u checks, %u failures; retail_calls=0\n", checks, failures);
    return failures ? 1 : 0;
}
