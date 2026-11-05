// SPDX-License-Identifier: GPL-3.0-only
// This executable owns fake pages at the pinned data addresses in its own
// process. It never installs the hook, opens a game process, or calls a native
// command: seeded tokens represent ownership after their key-up was sent.
#include "../src/t5/manual_grenade_adapter.cpp"
#include <cstdlib>
#include <limits>
#include <vector>

namespace bovr { void trace(const char*,const char*,...) noexcept {} }
namespace bovr::t5 {
bool manual_reload_uses_offhand() noexcept {return false;}
bool command_input_generation_current(const input::CommandRigSample&) noexcept {return true;}
unsigned renderer_configurations{};
bool configure_manual_grenade_renderer(HMODULE) noexcept {++renderer_configurations;return false;}
}
namespace bovr::t5::grenade_projectile {
namespace fixture {
unsigned captures{},polls{},configurations{};
Key captured_key{},polled_key{};
GrenadeThrowCallback polled_callback{};
void reset() noexcept {captures=polls=0;captured_key=polled_key={};polled_callback=nullptr;}
}
// Keep the production scanner, native server barrier and compare/write host
// out of this executable. Their injected-access tests run separately.
bool configure(HMODULE) noexcept {++fixture::configurations;return false;}
const Access& native_access() noexcept {static const Access unavailable{};return unavailable;}
bool capture(Baseline*,const Key& key,const Access&) noexcept {
    ++fixture::captures;fixture::captured_key=key;return false;
}
Result poll(Baseline*,const Key& key,GrenadeThrowCallback callback,const Access&) noexcept {
    ++fixture::polls;fixture::polled_key=key;fixture::polled_callback=callback;return {Status::waiting};
}
}
namespace t=bovr::t5;namespace i=bovr::input;namespace m=i::grenade;
namespace g=wawvr::mod;namespace xr=wawvr::xr;
namespace {
unsigned checks{},failures{};
void check(bool value,const char* why){++checks;if(!value){++failures;std::fprintf(stderr,"FAIL %s\n",why);}}
template<class T>void put(std::uintptr_t address,const T& value){std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));}
constexpr std::uintptr_t cg=0x50000000,server=0x51000000,variant=0x52000000,definition=0x53000000,projectile=definition+0x2000;
constexpr std::uintptr_t primary_variant=variant+0x4000,primary_definition=definition+0x4000,viewmodel=0x54000000,dobj=0x55000000;
constexpr std::uintptr_t ps=cg+0x8a364,parent=0x1a796f8;
constexpr unsigned grenade=21,primary=9;
struct Pages {
    std::array<std::pair<std::uintptr_t,std::size_t>,11> ranges{{
        {0xbe0000,0x10000},{0x1a70000,0x10000},{0x1c00000,0x10000},
        {0x2510000,0x10000},{0x2ff0000,0x10000},{0xc10000,0x10000},{cg,0x100000},
        {server,0x10000},{variant,0x10000},{definition,0x10000},{viewmodel,0x10000}}};
    Pages(){for(const auto& [address,size]:ranges){
        void* p=VirtualAlloc(reinterpret_cast<void*>(address),size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(p!=reinterpret_cast<void*>(address)){std::fprintf(stderr,"Cannot reserve isolated fake page %p\n",reinterpret_cast<void*>(address));std::exit(2);}
    }}
    ~Pages(){for(const auto& [address,size]:ranges){(void)size;VirtualFree(reinterpret_cast<void*>(address),0,MEM_RELEASE);}}
    void clear(){for(const auto& [address,size]:ranges)std::memset(reinterpret_cast<void*>(address),0,size);}
};
struct Fixture {
    i::CommandRigSample rig{};t::CommandContext context{};t::GrenadeThrowArguments args{};
    std::uint64_t now{};
    explicit Fixture(Pages& pages){
        pages.clear();t::state={};t::grenade_pose={};t::authored_transaction=t::authored_generation=t::authored_ms=0;
        t::projectile_baseline={};t::grenade_projectile::fixture::reset();t::configured=true;t::button_grenades=false;now=GetTickCount64();
        put<std::uint32_t>(0x2ff5354,cg);put<std::uint32_t>(parent+0x13c,server);put<std::uint32_t>(0x1c03140,server);
        put<DWORD>(0x251ca98,GetCurrentThreadId());put<int>(cg+0x8a348,1000);put<int>(cg+0x8c158,1000);
        put<int>(ps+0x1c4,100);put<unsigned char>(ps+0x134,grenade);put<int>(server+0x1c4,100);put<unsigned char>(server+0x134,grenade);
        put<std::uint32_t>(ps+0x144,0xA5A50000U|primary);put<std::uint32_t>(cg+0xa9c18,primary);
        put<std::uint32_t>(0xbe19a8+4*primary,primary_variant);
        put<std::uint32_t>(primary_variant+8,primary_definition);put<int>(primary_definition+0x1c,0);
        put<std::uint32_t>(0xc1c6d8,viewmodel);put<std::uint32_t>(viewmodel,dobj);
        put<xr::Vec3f>(cg+0x8c120,{100,200,300});put<xr::Basis3f>(cg+0x8c134,{{1,0,0},{0,1,0},{0,0,1}});
        put<unsigned>(ps+0x1dc,grenade);put<std::uint32_t>(0xbe19a8+4*grenade,variant);put<std::uint32_t>(variant+8,definition);
        put<int>(definition+0x1c,1);put<int>(definition+0x6c,1);put<int>(definition+0x68,1);
        put<std::uint32_t>(definition+0x5e8,projectile);
        put<int>(variant+0x44,5);put<int>(variant+0x4c,6);put<int>(ps+0x344,5);put<int>(ps+0x348,3);put<int>(ps+0x3bc,6);
        // Distinct values detect confusion between native world-up, relative-up
        // and forward-add fields rather than accidentally agreeing with zeros.
        put<int>(definition+0x5c8,700);put<int>(definition+0x5cc,200);put<int>(definition+0x5d0,999);put<int>(definition+0x5d4,100);
        context.command_number=1;context.connection_state=10;context.native_frame_time=1000;context.native_thread=GetCurrentThreadId();context.valid=true;
        rig.xr_generation=rig.anchor_generation=7;rig.published_ms=now;
        auto& f=rig.frame;f.frame_id=f.actions.sequence=1;f.actions.focused=f.views_valid=true;
        f.head_center.position={0,1.7f,0};rig.tracking_anchor=f.head_center;
        auto& h=f.actions.hands[0];h.grip.active=h.grip.position_valid=h.grip.position_tracked=true;
        h.grip.orientation_valid=h.grip.orientation_tracked=true;h.trigger.active=true;
        h.grip.pose.position={-.25f,1.15f,-.1f};
        args.parent=parent;args.weapon=grenade;args.model=35;args.rotate=1;args.fuse_time=900;
        args.origin={101,202,303};args.velocity={750,0,200};
    }
    void seed(bool pending=true,bool cancelled=false){
        auto& s=t::state;t::Native copy{};check(t::native_copy(context,&copy),"authoritative fake inventory copies successfully");
        s.native=copy;s.weapon=copy.weapons[0];s.generation=7;s.started=s.released=s.pose_ms=now;s.start_time=1000;
        m::Context c{now,cg,7,true,false,true,false};
        (void)s.controller.update(rig,c);rig.frame.actions.hands[0].trigger.current=.9f;++rig.frame.actions.sequence;
        s.visual=s.controller.update(rig,c);check(s.visual.stage==g::ManualGrenadeStage::holding,"fixture owns a real core hold");
        s.active=true;s.pending=pending;s.cancelled=cancelled;s.forward={1,0,0};s.origin={800,900,1000};s.velocity={0,200,80};
        s.native_key=0; // Already released key: adapter must never dispatch here.
    }
    void update(bool live=true){rig.published_ms=GetTickCount64();++rig.frame.actions.sequence;++rig.frame.frame_id;t::update_manual_grenade_input(context,rig,live);}
};
void callback_delivery(Pages& pages){
    {
        Fixture f(pages);f.seed();auto& s=t::state;
        check(s.weapon.tuning.native_projectile_speed==700&&
              s.weapon.tuning.native_projectile_speed_forward==100&&
              s.weapon.tuning.native_projectile_speed_up==200,"retail calibration copies speed, forward-add and world-up distinct offsets");
        const auto before=f.args;t::GrenadeThrowReplacement result{};
        check(t::replace_throw(f.args,&result)&&s.consumed,"matching released token is consumed by exactly one callback");
        check(result.origin==f.args.origin&&result.origin!=std::array<float,3>{s.origin.x,s.origin.y,s.origin.z},"spawn retains current native origin even when tracked hand is far away");
        check(std::fabs(result.velocity[0])<.001f&&result.velocity[1]>500&&result.velocity[2]>200,"physical leftward swing controls launch direction and upward velocity");
        check(!t::replace_throw(f.args,&result),"duplicate callback cannot consume or replay release");
        check(std::memcmp(&f.args,&before,sizeof(before))==0,"callback leaves native parent, model, fuse and original arguments unchanged");
    }
    for(bool cancelled:{false,true}){
        Fixture f(pages);f.seed(cancelled,cancelled);t::GrenadeThrowReplacement result{};
        if(cancelled){t::state.started=t::state.pose_ms=t::state.released=1;}
        check(t::replace_throw(f.args,&result),cancelled?"cancelled token survives wall-clock pause until owned native drop":"native cooking expiration while held consumes owned drop");
        check(result.origin==f.args.origin&&result.velocity==std::array<float,3>{},"cook expiration and cancelled tracking both drop with zero physical launch velocity");
    }
    {
        Fixture f(pages);f.seed();check(!t::replace_throw(f.args,nullptr)&&!t::state.consumed,"null replacement never consumes token");
        t::state.pose_ms=1;t::GrenadeThrowReplacement result{};
        check(!t::replace_throw(f.args,&result)&&!t::state.consumed,"stale normal release cannot replace a later stock spawn");
    }
}
void callback_identity(Pages& pages){
    for(unsigned fault=0;fault<12;++fault){
        Fixture f(pages);f.seed(true,true);
        switch(fault){
        case 0:f.args.parent+=0x200;break;
        case 1:++f.args.weapon;break;
        case 2:put<std::uint32_t>(parent+0x13c,server+0x1000);break;
        case 3:put<std::uint32_t>(0x1c03140,server+0x1000);break;
        case 4:put<unsigned char>(server+0x134,grenade+1);break;
        case 5:put<std::uint32_t>(0xbe19a8+4*grenade,variant+0x1000);break;
        case 6:put<std::uint32_t>(variant+8,definition+0x1000);break;
        case 7:put<int>(server+0x1c4,0);break;
        case 8:put<int>(cg+0x8a348,999);break;
        case 9:put<std::uint32_t>(0x2ff5354,cg+0x10000);break;
        case 10:put<int>(cg+0x8a348,16001);break;
        case 11:put<unsigned char>(server+0x130,1);break;
        }
        t::GrenadeThrowReplacement result{{17,18,19},{20,21,22}};const auto untouched=result;
        check(!t::replace_throw(f.args,&result)&&!t::state.consumed,"foreign, stale, dead or expired ownership cannot consume cancellation drop");
        check(result.origin==untouched.origin&&result.velocity==untouched.velocity,"rejected callback leaves proposed output untouched");
    }
}
void ownership_cleanup(Pages& pages){
    {
        Fixture f(pages);f.seed(false);t::GrenadeThrowReplacement result{};
        check(t::replace_throw(f.args,&result),"native cook consumes token before user releases trigger");
        f.update();check(!t::state.active&&!t::state.pending&&!t::state.consumed&&t::state.visual.stage==g::ManualGrenadeStage::ready,
            "consumed holding state resets instead of remaining stranded in core holding");
        f.update();check(t::state.visual.stage==g::ManualGrenadeStage::ready&&!t::state.active,"still-held trigger cannot grab again after native cook cleanup");
        m::Context c{GetTickCount64(),cg,7,true,false,true,false};
        f.rig.frame.actions.hands[0].trigger.current=0;++f.rig.frame.actions.sequence;(void)t::state.controller.update(f.rig,c);
        f.rig.frame.actions.hands[0].trigger.current=.9f;++f.rig.frame.actions.sequence;
        check(t::state.controller.update(f.rig,c).event==g::ManualGrenadeEvent::grab_frag,"fresh physical trigger press works after cooked grenade cleanup");
    }
    {
        Fixture f(pages);f.seed(false);f.update(false);
        check(t::state.active&&t::state.pending&&t::state.cancelled&&!t::state.consumed&&t::state.native_key==0,
            "lost native input retains owned cancellation through already-released key");
        t::GrenadeThrowReplacement result{};
        check(t::replace_throw(f.args,&result)&&result.velocity==std::array<float,3>{},"callback after cancellation key-up consumes safe native-origin drop");
        f.update();check(!t::state.active,"consumed cancelled token is retired on next fresh native input");
    }
    for(bool time_change:{false,true}){
        Fixture f(pages);f.seed(false);
        if(time_change){put<int>(cg+0x8a348,999);put<int>(cg+0x8c158,999);}
        else {put<std::uint32_t>(0x2ff5354,cg+0x10000);}
        f.update();t::GrenadeThrowReplacement result{};
        check(!t::replace_throw(f.args,&result),"identity or map-clock transition cannot deliver old grenade ownership");
    }
    {
        Fixture f(pages);f.seed(false);put<DWORD>(0x251ca98,GetCurrentThreadId()+1);f.update(false);
        check(t::state.active&&!t::state.pending&&!t::state.cancelled,"wrong native command thread cannot mutate token ownership");
    }
}
void projectile_adapter_boundary(Pages& pages){
    namespace p=t::grenade_projectile;
    {
        Fixture f(pages);f.update();f.rig.frame.actions.hands[0].trigger.current=.9f;f.update();
        check(p::fixture::captures==1&&p::fixture::captured_key.gameplay_epoch==cg&&
            p::fixture::captured_key.xr_generation==7&&p::fixture::captured_key.transaction&&
            p::fixture::captured_key.weapon==grenade,"new grab sends exact current transaction to projectile baseline capture");
        check(!t::state.active&&!t::state.native_key&&!t::state.visual.valid,
            "rejected projectile capture cancels grab before any native Down command can execute");
    }
    {
        Fixture f(pages);f.seed();auto& baseline=t::projectile_baseline;
        baseline.ready=true;baseline.key={cg,7,t::state.visual.transaction,grenade};f.update();
        check(p::fixture::polls==1&&p::fixture::polled_key==baseline.key&&p::fixture::polled_callback==t::replace_throw,
            "ready baseline polls copied callback through isolated host without native synchronization");
        baseline.retired=true;f.update();check(p::fixture::polls==1,"retired baseline cannot poll another projectile");
    }
}
void grenade_visual_ownership(Pages& pages){
    Fixture f(pages);f.seed(false);put<int>(ps+0x158,24);f.rig.frame.should_render=true;
    auto& pose=t::grenade_pose;pose.context=f.context;pose.transaction=t::state.visual.transaction;
    pose.generation=7;pose.updated_ms=f.now;pose.cg=cg;pose.server=server;pose.native_start_time=1000;
    pose.variant=variant;pose.definition=definition;pose.projectile_model=projectile;pose.weapon=grenade;
    pose.primary_variant=primary_variant;pose.primary_definition=primary_definition;pose.primary_weapon=primary;
    pose.previous_viewmodel_weapon=primary;
    pose.viewmodel=viewmodel;pose.dobj=dobj;
    pose.hand={{100,200,300},{{1,0,0},{0,1,0},{0,0,1}}};pose.holding=true;
    t::ManualGrenadePose copied{};
    check(t::read_manual_grenade_pose(&copied)&&t::manual_grenade_pose_current(copied),"owned selected grenade has fresh visual ticket");
    check(t::read_manual_grenade_render_pose(&copied)&&t::manual_grenade_render_pending(),"owned hold exposes exact projectile render ticket");
    put<std::uint32_t>(ps+0x10,2);put<std::uint32_t>(ps+0x144,0xA5A50000U);
    t::ManualGrenadePrimaryVisual visual{};
    check(t::read_manual_grenade_primary_visual(0,&visual)&&visual.primary_weapon==primary&&visual.dobj==dobj,
        "zero live weapon still identifies the primary DObj loaded before the native grab");
    check(visual.primary_word==0xA5A50000U&&visual.previous_viewmodel_weapon==primary,
        "visual ticket preserves the aligned player-state word and loaded viewmodel identity");
    t::ManualGrenadePrimaryVisual other_client{};
    check(!t::read_manual_grenade_primary_visual(1,&other_client),"another local client cannot own the primary visual scope");
    put<std::uint32_t>(ps+0x10,0);put<std::uint32_t>(ps+0x144,0xA5A50000U|primary);
    check(t::manual_grenade_primary_visual_current(visual,0),"the exact visual ticket remains current during its owned bit mask");
    put<std::uint32_t>(ps+0x144,0xA5A50000U|(primary+1));
    check(!t::manual_grenade_primary_visual_current(visual,0),"a different injected primary rejects the visual scope");
    put<std::uint32_t>(ps+0x144,0xA5A50000U|primary);put<std::uint32_t>(cg+0xa9c18,primary+1);
    check(!t::manual_grenade_primary_visual_current(visual,0),"a changed loaded viewmodel identity rejects the visual scope");
    put<std::uint32_t>(cg+0xa9c18,primary);
    put<std::uint32_t>(ps+0x10,2);put<unsigned char>(ps+0x134,grenade+1);
    check(!t::read_manual_grenade_primary_visual(0,&visual),"a foreign offhand rejects the primary visual scope");
    put<unsigned char>(ps+0x134,grenade);put<std::uint32_t>(ps+0x10,0);
    t::publish_manual_grenade_pose(pose.transaction,pose.generation);
    check(t::manual_grenade_authored_visible(pose.transaction,pose.generation),"accepted authored grenade replaces generic overlay");
    check(!t::manual_grenade_authored_visible(pose.transaction+1,pose.generation),"publication cannot hide another grenade transaction");
    put<unsigned char>(ps+0x134,grenade+1);check(!t::read_manual_grenade_pose(&copied),"native offhand switch revokes visual ticket");
    put<unsigned char>(ps+0x134,grenade);pose.updated_ms=1;check(!t::read_manual_grenade_pose(&copied),"stale controller pose cannot render grenade");pose.updated_ms=f.now;
    put<int>(cg+0x8a348,999);put<int>(cg+0x8c158,999);
    check(!t::read_manual_grenade_pose(&copied),"map clock rollback rejects visual even before input refresh");
    put<int>(cg+0x8a348,1000);put<int>(cg+0x8c158,1000);
    put<std::uint32_t>(parent+0x13c,server+0x1000);put<std::uint32_t>(0x1c03140,server+0x1000);
    check(!t::read_manual_grenade_pose(&copied),"same-cg server rebind rejects old visual ownership");
    put<std::uint32_t>(parent+0x13c,server);put<std::uint32_t>(0x1c03140,server);
    f.rig.frame.should_render=false;f.update();
    check(!t::read_manual_grenade_pose(&copied)&&pose.transaction,"non-rendering sample revokes freshness without destroying held ownership");
    f.rig.frame.should_render=true;f.update();
    check(t::read_manual_grenade_pose(&copied)&&copied.holding,"fresh render resumes the same owned grenade without another grab");
    t::state.pending=true;f.update();check(t::read_manual_grenade_pose(&copied)&&!copied.holding,"release tail remains owned but no longer held");
    t::state.consumed=true;f.update();check(!t::state.active&&t::read_manual_grenade_pose(&copied)&&!copied.holding,
        "native throw consumption cannot expose stock recovery animation before offhand ends");
    put<int>(ps+0x158,0);f.update();check(!t::read_manual_grenade_pose(&copied)&&!pose.transaction,"primary return clears grenade visual ownership");
}
void grenade_visual_palm_capture(Pages& pages){
    namespace a=t::animation_detail;
    Fixture f(pages);f.seed(false);put<int>(ps+0x158,24);f.rig.frame.should_render=true;
    auto& pose=t::grenade_pose;pose.transaction=t::state.visual.transaction;pose.generation=7;
    pose.cg=cg;pose.server=server;pose.native_start_time=1000;pose.variant=variant;
    pose.definition=definition;pose.projectile_model=projectile;pose.weapon=grenade;
    t::capture_primary_visual(t::state.native,&pose);
    auto& support=f.rig.frame.actions.hands[0];support.palm=support.grip;
    support.palm.pose.position={-.18F,1.34F,-.47F};
    support.palm.pose.orientation={std::sin(.37F),0,0,std::cos(.37F)};
    const auto local_grip=xr::OpenXrPoseToIwRelative(support.grip.pose,f.rig.tracking_anchor,xr::kIwUnitsPerMeter);
    const auto grip=a::compose({t::state.native.eye,t::state.native.body},{local_grip.position,local_grip.axis});
    t::ManualGrenadePose copied{};
    for(unsigned step=0;step<3;++step){
        if(step==1){support.palm.pose.position={.7F,1.8F,.3F};support.palm.pose.orientation={0,0,std::sin(-.53F),std::cos(-.53F)};}
        if(step==2)support.palm.orientation_valid=false;
        f.update();
        check(t::read_manual_grenade_render_pose(&copied)&&copied.holding,"adapter publishes a fresh held visual after optional palm changes");
        if(step<2){
            const auto local=xr::OpenXrPoseToIwRelative(support.palm.pose,f.rig.tracking_anchor,xr::kIwUnitsPerMeter);
            const auto surface=a::compose({t::state.native.eye,t::state.native.body},{local.position,local.axis});
            check(a::distance(copied.hand.origin,surface.origin)<.001F&&
                a::distance(copied.hand.axis.up,surface.axis.forward)<.001F&&
                a::distance(copied.hand.axis.forward,surface.axis.up)<.001F&&
                a::dot(copied.hand.axis.left,surface.axis.left)<-.999F,
                "actual adapter ticket maps palm fingers, thumb and palm normal to the canonical held glove");
        }else check(a::distance(copied.hand.origin,grip.origin)==0&&a::axis_error(copied.hand.axis,grip.axis)==0,
            "actual adapter restores the unchanged grip visual when palm tracking becomes invalid");
        check(a::distance(t::state.origin,grip.origin)<.001F&&a::distance(t::state.forward,grip.axis.forward)<.001F&&
            a::dot(t::state.velocity,t::state.velocity)==0,
            "moving only the palm visual never changes grip-based throw origin, direction or velocity");
    }
}
void button_native_delivery(Pages& pages){
    {
        Fixture f(pages);f.seed();t::state.button_mode=true;t::state.forward={0,.6f,.8f};
        const auto before=f.args;t::GrenadeThrowReplacement result{};
        check(t::replace_throw(f.args,&result)&&t::state.consumed,"button released token consumes existing native spawn once");
        check(result.origin==f.args.origin&&std::fabs(result.velocity[0])<.001f&&
            std::fabs(result.velocity[1]-480)<.001f&&std::fabs(result.velocity[2]-840)<.001f,
            "button callback uses head forward with current native speed/up and current native origin");
        check(std::memcmp(&f.args,&before,sizeof(before))==0&&!t::replace_throw(f.args,&result),
            "button mode preserves native fuse/model/ammo inputs and cannot replay");
    }
    for(bool cancelled:{false,true}){
        Fixture f(pages);f.seed(cancelled,cancelled);t::state.button_mode=true;t::GrenadeThrowReplacement result{};
        check(t::replace_throw(f.args,&result)&&result.velocity==std::array<float,3>{},
            "button cook expiration or cancelled tracking drops rather than throwing old headset ray");
    }
    {
        Fixture f(pages);t::Native n{};check(t::native_copy(f.context,&n),"button primary denial fixture current native context");
        put<unsigned char>(ps+0x144,grenade);
        check(t::button_primary_single(n),"current rechecked non-dual primary permits optional button ownership");
        put<unsigned char>(definition+0x56a,1);check(!t::button_primary_single(n),"dual primary preserves left-fire control");
        put<unsigned char>(definition+0x56a,2);check(!t::button_primary_single(n),"invalid dual metadata does not claim trigger");
        put<unsigned char>(definition+0x56a,0);put<unsigned char>(ps+0x144,0);
        check(!t::button_primary_single(n),"missing primary does not claim trigger");
        put<unsigned char>(ps+0x144,grenade);n.ordinary=false;
        check(!t::button_primary_single(n),"offhand/scripted context cannot start new button grenade");
    }
    for(unsigned blocker=0;blocker<5;++blocker){
        Fixture f(pages);t::button_grenades=true;put<unsigned char>(ps+0x144,grenade);
        auto& left=f.rig.frame.actions.hands[0];left.stick_click.active=left.squeeze.active=true;
        left.grip.pose.position={.4f,1.9f,-.6f};
        if(blocker==0)put<unsigned char>(definition+0x56a,1);
        if(blocker==1)left.squeeze.current=.8f;
        if(blocker==2)f.rig.frame.actions.hands[1].primary.current=true;
        if(blocker==3)left.squeeze.current=std::numeric_limits<float>::quiet_NaN();
        if(blocker==4)left.squeeze.active=false;
        f.update();left.trigger.current=.9f;f.update();
        check(t::grenade_projectile::fixture::captures==0&&!t::state.active&&!t::manual_grenade_consumes_trigger(),
            "button native dual/support/reload/unknown-grip blocker leaves trigger route without native grenade input");
        put<unsigned char>(definition+0x56a,0);left.squeeze.current=0;left.squeeze.active=true;
        f.rig.frame.actions.hands[1].primary.current=false;f.update();
        check(t::grenade_projectile::fixture::captures==0,"clearing actual adapter blocker cannot replay held button press");
        left.trigger.current=0;f.update();left.trigger.current=.9f;f.update();
        check(t::grenade_projectile::fixture::captures==1&&!t::state.active,
            "fresh button press uses same guarded projectile capture and refusal before native Down");
    }
}
}
int main(){
    // Claim the fixed native-address fixture before any test-owned heap
    // allocation can consume one of its low pages.
    Pages pages;
    t::configured=false;t::renderer_configurations=0;t::grenade_projectile::fixture::configurations=0;
    check(!t::configure_manual_grenades(nullptr,false)&&!t::configured&&
        t::renderer_configurations==0&&t::grenade_projectile::fixture::configurations==0,
        "missing weapon submission hook refuses before installing grenade hooks or renderer");
    std::vector<unsigned char> native_image(t::manual_grenade_native_guards::execute_rva+
        t::manual_grenade_native_guards::execute_entry_guard.size,0);
    check(!t::configure_manual_grenades(reinterpret_cast<HMODULE>(native_image.data()),true)&&!t::configured&&
        t::renderer_configurations==0&&t::grenade_projectile::fixture::configurations==0,
        "mismatched native Execute entry refuses before grenade hook or renderer configuration");
    callback_delivery(pages);callback_identity(pages);ownership_cleanup(pages);projectile_adapter_boundary(pages);grenade_visual_ownership(pages);grenade_visual_palm_capture(pages);button_native_delivery(pages);
    std::printf("manual_grenade_adapter: %u checks, %u failures\n",checks,failures);return failures?1:0;}
