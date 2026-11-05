// SPDX-License-Identifier: GPL-3.0-only
#include "input/helicopter_router.hpp"
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
using namespace bovr::input;
namespace xr = wawvr::xr;
unsigned checks{}, failures{};
void check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", label); }
}
bool near(float a, float b) { return std::fabs(a - b) < 0.00001f; }
bool held(const HelicopterOutput& o, HelicopterAction a) { return o.held[static_cast<unsigned>(a)]; }
bool zero(const HelicopterOutput& o) {
    for (bool b : o.held) if (b) return false;
    return o.axes.forward == 0 && o.axes.right == 0 && o.axes.yaw == 0 && o.axes.pitch == 0;
}
xr::ActionSnapshot neutral() {
    xr::ActionSnapshot s{};
    s.focused = true; s.sequence = 1;
    for (auto& h : s.hands) {
        h.grip.active = h.grip.orientation_valid = h.grip.position_valid = true;
        h.grip.orientation_tracked = h.grip.position_tracked = true;
        h.trigger.active = h.trigger_click.active = h.squeeze.active = h.stick.active = true;
        h.primary.active = h.secondary.active = true;
    }
    return s;
}
void press(xr::ActionSnapshot& s, HelicopterAction a, bool down) {
    switch (a) {
    case HelicopterAction::cannon: s.hands[1].trigger.current = down ? 1.f : 0.f; break;
    case HelicopterAction::rockets: s.hands[0].trigger.current = down ? 1.f : 0.f; break;
    case HelicopterAction::ascend: s.hands[1].primary.current = down; break;
    case HelicopterAction::descend: s.hands[1].secondary.current = down; break;
    case HelicopterAction::reload: s.hands[1].squeeze.current = down ? 1.f : 0.f; break;
    case HelicopterAction::use: s.hands[0].primary.current = down; break;
    case HelicopterAction::count: break;
    }
}
struct Fixture {
    HelicopterRouter router{};
    xr::ActionSnapshot s = neutral();
    std::uint64_t now = 1000;
    HelicopterOutput fresh(bool active = true) { ++s.sequence; now += 10; return router.update(s, active, now); }
    HelicopterOutput duplicate(bool active = true) { ++now; return router.update(s, active, now); }
};
}

int main() {
    {
        Fixture f;f.fresh();f.s.hands[0].stick.current={.6f,1.f};
        for(unsigned i=0;i<6;++i)press(f.s,static_cast<HelicopterAction>(i),true);
        auto o=f.fresh();const auto boat=driver_keys(o,4),hind=driver_keys(o,6);
        check(boat[0]&&boat[1]&&!boat[6]&&boat[7]&&!boat[8]&&boat[9],"boat trigger weapons and forward throttle delivered");
        check(!boat[2]&&!boat[3]&&boat[4]&&boat[5],"boat suppresses helicopter altitude keys while retaining reload and use");
        check(hind[0]&&hind[1]&&hind[6]&&!hind[7]&&!hind[8]&&!hind[9],"helicopter mapping retains original weapon controls without boat keys");
        f.s.hands[0].stick.current.y=-1.f;o=f.fresh();const auto reverse=driver_keys(o,4);
        check(!reverse[7]&&reverse[8],"reverse releases boat throttle and applies reverse");
        check(driver_keys(f.fresh(false),4)==std::array<bool,10>{},"lost driver context releases all boat keys");
        check(driver_keys(o,1)==std::array<bool,10>{},"unrecognized vehicle cannot acquire boat controls");
    }
    for (unsigned i = 0; i < static_cast<unsigned>(HelicopterAction::count); ++i) {
        const auto action = static_cast<HelicopterAction>(i);
        Fixture f;
        press(f.s, action, true);
        auto o = f.fresh();
        check(o.valid && zero(o), "entry held button cannot press without fresh neutral");
        press(f.s, action, false); f.fresh();
        press(f.s, action, true); o = f.fresh();
        check(held(o, action), "fresh neutral then press routes the expected helicopter action");
        unsigned active{}; for (bool b : o.held) active += b ? 1u : 0u;
        check(active == 1, "each mapping selects exactly one action");
        check(held(f.duplicate(), action), "unchanged duplicate maintains a held action");
        press(f.s, action, false);
        check(!held(f.fresh(), action), "fresh release balances the held action");
        press(f.s, action, true);
        check(held(f.fresh(), action), "fresh release rearms another deliberate press");
        f.router.reset();
        check(zero(f.duplicate()), "reset removes every pressed output on the same publication");
        press(f.s, action, false); f.duplicate();
        press(f.s, action, true);
        check(!held(f.fresh(), action), "changed-neutral duplicate after reset cannot rearm");
        press(f.s, action, false); f.fresh();
        press(f.s, action, true);
        check(held(f.fresh(), action), "new neutral after reset restores the control");
    }
    {
        Fixture f; f.fresh();
        press(f.s, HelicopterAction::cannon, true);
        check(zero(f.duplicate()), "changed duplicate cannot acquire a fresh cannon press");
        check(!held(f.fresh(), HelicopterAction::cannon), "untrusted duplicate press consumes neutral authorization");
        press(f.s, HelicopterAction::cannon, false); f.fresh();
        press(f.s, HelicopterAction::cannon, true); f.fresh();
        press(f.s, HelicopterAction::cannon, false);
        check(zero(f.duplicate()), "duplicate release immediately releases cannon");
        press(f.s, HelicopterAction::cannon, true);
        check(zero(f.fresh()), "duplicate release cannot substitute for fresh neutral");
    }
    {
        Fixture f;
        f.s.hands[0].stick.current = {.6f, -.6f};
        f.s.hands[1].stick.current = {-.6f, .6f};
        check(zero(f.fresh()), "both sticks require a neutral sample on entry");
        f.s.hands[0].stick.current = {.2f, -.2f};
        check(zero(f.fresh()), "deadzone boundary arms left group without motion");
        f.s.hands[0].stick.current = {.6f, -.6f};
        auto o = f.fresh();
        check(o.valid && o.axes.valid && near(o.axes.forward, -.5f) && near(o.axes.right, .5f) &&
              o.axes.yaw == 0 && o.axes.pitch == 0, "left translation can arm independently of right look");
        f.s.hands[1].stick.current = {}; f.fresh();
        f.s.hands[1].stick.current = {-.6f, .6f}; o = f.fresh();
        check(near(o.axes.yaw, -.5f) && near(o.axes.pitch, .5f), "right yaw and pitch preserve signed axis mapping");
        o = f.duplicate();
        check(near(o.axes.forward, -.5f) && near(o.axes.yaw, -.5f), "unchanged duplicate maintains both stick outputs");
        f.s.hands[0].stick.current = {}; o = f.duplicate();
        check(o.axes.forward == 0 && o.axes.right == 0 && near(o.axes.yaw, -.5f),
              "changed-neutral duplicate zeros only the changed stick group");
        f.s.hands[0].stick.current = {1, 1}; o = f.fresh();
        check(o.axes.forward == 0 && o.axes.right == 0, "duplicate stick neutral cannot rearm translation");
        f.s.hands[0].stick.current = {}; f.fresh();
        f.s.hands[0].stick.current = {-2, 2}; o = f.fresh();
        check(o.axes.forward == 1 && o.axes.right == -1, "fresh stick neutral restores clamped translation");
        f.s.hands[1].stick.current = {.9f, .9f}; o = f.duplicate();
        check(o.axes.yaw == 0 && o.axes.pitch == 0 && o.axes.forward == 1,
              "changed nonneutral duplicate does not acquire a new look direction");
    }
    {
        Fixture f; f.fresh();
        f.s.hands[1].squeeze.current = .64f;
        check(!held(f.fresh(), HelicopterAction::reload), "grip below press boundary stays released");
        f.s.hands[1].squeeze.current = .65f;
        check(held(f.fresh(), HelicopterAction::reload), "grip press threshold is inclusive");
        f.s.hands[1].squeeze.current = .26f;
        check(held(f.fresh(), HelicopterAction::reload), "grip hold survives hysteresis band");
        f.s.hands[1].squeeze.current = .25f;
        check(!held(f.fresh(), HelicopterAction::reload), "grip release threshold is inclusive");
        f.router.reset(); f.s.hands[1].squeeze.current = .4f; f.fresh();
        f.s.hands[1].squeeze.current = .7f;
        check(!held(f.fresh(), HelicopterAction::reload), "hysteresis midpoint cannot arm after reset");
    }
    {
        Fixture f; f.fresh();
        f.s.hands[1].primary.current = f.s.hands[1].secondary.current = true;
        check(zero(f.fresh()), "opposed ascend and descend cancel both published outputs");
        f.s.hands[1].primary.current = false;
        check(zero(f.duplicate()), "duplicate cancellation release cannot introduce a descend press");
        auto o = f.fresh();
        check(!held(o, HelicopterAction::ascend) && held(o, HelicopterAction::descend),
              "fresh release of ascend retains the internally held descend action");
        f.s.hands[1].primary.current = true;
        check(zero(f.fresh()), "opposition also cancels an already held descend action");
        f.s.hands[1].secondary.current = false; o = f.fresh();
        check(held(o, HelicopterAction::ascend) && !held(o, HelicopterAction::descend),
              "fresh release restores the remaining internally held ascend action");
    }
    for (unsigned loss = 0; loss < 7; ++loss) {
        Fixture f; f.fresh();
        press(f.s, HelicopterAction::cannon, true); f.s.hands[0].stick.current = {1, 1}; f.fresh();
        bool context = true;
        switch (loss) {
        case 0: context = false; break;
        case 1: f.s.focused = false; break;
        case 2: f.s.hands[0].grip.active = false; break;
        case 3: f.s.hands[1].grip.orientation_valid = false; break;
        case 4: f.s.hands[0].grip.position_valid = false; break;
        case 5: f.s.hands[1].grip.orientation_tracked = false; break;
        case 6: f.s.hands[0].grip.position_tracked = false; break;
        }
        auto o = f.duplicate(context);
        check(!o.valid && !o.axes.valid && zero(o), "context/focus/full tracking loss releases all outputs on a duplicate");
        const auto sequence = f.s.sequence;
        f.s = neutral(); f.s.sequence = sequence;
        check(zero(f.duplicate()), "returning context with changed-neutral duplicate does not rearm");
        press(f.s, HelicopterAction::cannon, true); f.s.hands[0].stick.current = {1, 1};
        check(zero(f.fresh()), "return-held buttons and sticks stay blocked until fresh neutral");
        press(f.s, HelicopterAction::cannon, false); f.s.hands[0].stick.current = {}; f.fresh();
        press(f.s, HelicopterAction::cannon, true); f.s.hands[0].stick.current = {1, 1}; o = f.fresh();
        check(held(o, HelicopterAction::cannon) && o.axes.forward == 1, "fresh neutral recovers controls after context loss");
    }
    {
        Fixture f; f.fresh(); press(f.s, HelicopterAction::cannon, true); f.fresh();
        auto o = f.router.update(f.s, true, f.now + 250);
        check(o.valid && held(o, HelicopterAction::cannon), "unchanged duplicate is accepted at exactly250ms");
        o = f.router.update(f.s, true, f.now + 251);
        check(!o.valid && zero(o), "repeated duplicate calls do not refresh the250ms publication age");
        f.now += 251;
        check(zero(f.fresh()), "fresh held input cannot revive a stale control");
        press(f.s, HelicopterAction::cannon, false); f.fresh();
        press(f.s, HelicopterAction::cannon, true);
        check(held(f.fresh(), HelicopterAction::cannon), "fresh neutral permits recovery after expiry");
    }
    for (unsigned clock = 0; clock < 3; ++clock) {
        Fixture f; f.fresh(); press(f.s, HelicopterAction::cannon, true); f.fresh();
        if (clock == 0) f.s.sequence = 0;
        if (clock == 1) f.s.sequence = 1;
        if (clock == 2) f.now -= 5;
        auto o = f.router.update(f.s, true, f.now);
        check(!o.valid && zero(o), "zero sequence or backwards sequence/clock releases and rejects");
        if (clock == 0) f.s.sequence = 10;
        check(zero(f.fresh()), "clock invalidation cannot resume a held button");
        press(f.s, HelicopterAction::cannon, false); f.fresh();
        press(f.s, HelicopterAction::cannon, true);
        check(held(f.fresh(), HelicopterAction::cannon), "fresh neutral recovers after rebased sequence or clock");
    }
    {
        Fixture f; f.fresh();
        press(f.s, HelicopterAction::cannon, true); press(f.s, HelicopterAction::rockets, true);
        f.s.hands[0].stick.current = {1, 1}; f.s.hands[1].stick.current = {1, 1}; f.fresh();
        f.s.hands[1].trigger.current = std::numeric_limits<float>::quiet_NaN();
        f.s.hands[1].trigger_click.current = true;
        auto o = f.duplicate();
        check(o.valid && !held(o, HelicopterAction::cannon) && held(o, HelicopterAction::rockets) && o.axes.forward == 1,
              "NaN analog trigger invalidates only cannon even when click says pressed");
        f.s.hands[0].stick.current.y = std::numeric_limits<float>::infinity(); o = f.duplicate();
        check(o.valid && o.axes.valid && o.axes.forward == 0 && o.axes.right == 0 && o.axes.yaw == 1,
              "nonfinite left stick preserves valid right look and other controls");
        f.s.hands[0].stick.current = {}; f.duplicate();
        f.s.hands[0].stick.current = {1, 1};
        check(f.fresh().axes.forward == 0, "NaN stick loss requires a fresh neutral to rearm");
        f.s.hands[1].stick.active = false; o = f.duplicate();
        check(o.axes.yaw == 0 && o.axes.pitch == 0 && held(o, HelicopterAction::rockets),
              "missing right stick invalidates only its group");
        f.s.hands[0].trigger.active = f.s.hands[0].trigger_click.active = false;
        check(!held(f.duplicate(), HelicopterAction::rockets), "both trigger actions inactive release rockets");
        f.s.hands[0].trigger_click.active = true; f.s.hands[0].trigger_click.current = false; f.duplicate();
        f.s.hands[0].trigger_click.current = true;
        check(!held(f.fresh(), HelicopterAction::rockets), "inactive trigger cannot rearm through duplicate neutral click");
        f.s.hands[0].trigger_click.current = false; f.fresh();
        f.s.hands[0].trigger_click.current = true;
        check(held(f.fresh(), HelicopterAction::rockets), "active click-only trigger supports fresh neutral and press");
    }
    {
        Fixture f; f.fresh();
        press(f.s, HelicopterAction::reload, true); press(f.s, HelicopterAction::use, true); f.fresh();
        f.s.hands[1].squeeze.current = std::numeric_limits<float>::infinity();
        auto o = f.duplicate();
        check(!held(o, HelicopterAction::reload) && held(o, HelicopterAction::use), "nonfinite grip releases only reload");
        f.s.hands[0].primary.active = false;
        check(!held(f.duplicate(), HelicopterAction::use), "inactive X releases use on the same publication");
        f.s.hands[0].primary.active = true; f.s.hands[0].primary.current = false; f.duplicate();
        f.s.hands[0].primary.current = true;
        check(!held(f.fresh(), HelicopterAction::use), "inactive button return requires fresh neutral");
    }
    {
        Fixture f; f.fresh(); press(f.s, HelicopterAction::cannon, true); f.fresh();
        f.now += 300;
        check(zero(f.fresh()), "long callback gap invalidates old holds even when a new sequence arrives");
    }
    std::printf("helicopter_router_checks=%u failures=%u native_commands=0\n", checks, failures);
    return failures ? 1 : 0;
}
