// SPDX-License-Identifier: GPL-3.0-only
// Run the unchanged A2 fixture, then the actual T2 kernels and writer witnesses.
#define main baseline_geometry_main
#include "command_aim_placement_tests.cpp"
#undef main
#include <bit>
#include <limits>
namespace {
void pair_inputs(Inputs& i) {
    auto& actions=i.scene.prediction.actions;
    for(auto& h:actions.hands){h.squeeze.active=true;h.squeeze.current=.8F;
        h.grip.active=h.grip.position_valid=h.grip.position_tracked=true;}
    const auto& right=actions.hands[1];
    const auto d=xr::Rotate(right.aim.pose.orientation,{.09F,.04F,-.35F});
    actions.hands[0].grip.pose.position=a::add(right.grip.pose.position,d);
    i.command.frame.actions=actions;
}
input::CommandAimStatus paired_aim(const Inputs& i,input::CommandAimProposal* out) {
    return input::propose_native_command_aim(i.command,i.command.frame.actions,i.body.stock_camera.axis,xr::Hand::Right,1050,out,true);
}
input::BorrowedPlacementStatus paired_visible(const Inputs& i,input::BorrowedPlacementValues* out) {
    return input::propose_borrowed_placement(i.scene,i.body,i.geometry,1050,out,true);
}
bool exact_geometry(const input::BorrowedPlacementValues& a0,const input::BorrowedPlacementValues& b0) {
    return !memcmp(&a0.gun,&b0.gun,sizeof(a0.gun))&&!memcmp(&a0.dobj,&b0.dobj,sizeof(a0.dobj))&&
        !memcmp(&a0.barrel,&b0.barrel,sizeof(a0.barrel))&&a0.identity==b0.identity&&a0.action_sequence==b0.action_sequence;
}
}
int main() {
    if(baseline_geometry_main())return 1;
    const auto before=checks;
    for(unsigned row=0;row<3;++row)for(unsigned sample=0;sample<160;++sample){
        Inputs i(row,sample);pair_inputs(i);const auto scene_original=i.scene,command_scene=i.scene;
        const auto command_original=i.command;
        input::CommandAimProposal cmd{},off{};input::BorrowedPlacementValues visual{},one{};
        check(paired_aim(i,&cmd)==input::CommandAimStatus::accepted&&paired_visible(i,&visual)==input::BorrowedPlacementStatus::proposed,
            "both actual kernels accept their independently owned current pair");
        check(cmd.support.paired&&visual.support.paired&&a::distance(cmd.forward,visual.gun.axis.forward)<.00001F,
            "same original support pair composes identical command and visible forward through noncommuting anchor/body");
        check(a::distance(decoded_forward(cmd.packed),visual.gun.axis.forward)<.0002F,"native signed-angle packing bounds hold for pair forward");
        check(aim(i,&off)==input::CommandAimStatus::accepted&&visible(i,&one)==input::BorrowedPlacementStatus::proposed&&
            a::distance(cmd.forward,off.forward)>.05F,"paired ray is actually different from right aim ray");
        check(!memcmp(&visual.gun.origin,&one.gun.origin,sizeof(one.gun.origin)),"pair orientation never moves the native visible right grip origin");
        input::ScaledWeaponPlacement output{},original{{},{1,2,3},1};
        check(t5::borrowed_writer::make_value(visual,original,1050,&output),"actual borrowed writer accepts pair placement");
        a::Transform pose{};const auto q=output.quaternion;
        check(a::convert_native({{q.x,q.y,q.z,q.w},output.origin,2},&pose),"native quaternion formula reconstructs selected pair DObj");
        a::LocalGeometry normal{};normal.pose_world=pose;normal.gun_world=a::compose(pose,i.geometry.gun_in_dobj);
        normal.flash_world=a::compose(normal.gun_world,i.geometry.flash_in_gun);
        t5::borrowed_writer::Witness witness{};witness.selected=true;
        check(t5::borrowed_writer::observe_normal(witness,visual,normal,i.geometry)&&t5::borrowed_writer::observe_after(witness,visual,normal),
            "unchanged normal and after witnesses accept all three authored profile rows with paired root");
        check(a::distance(normal.gun_world.axis.forward,cmd.forward)<.00001F,"normal result retains paired command forward");
        auto wrong=normal;wrong.gun_world.origin.x+=1;
        t5::borrowed_writer::Witness refused{};refused.selected=true;
        check(!t5::borrowed_writer::observe_normal(refused,visual,wrong,i.geometry)&&!t5::borrowed_writer::observe_after(refused,visual,normal),
            "paired placement cannot bypass original native normal/after mismatch refusal");
        check(!memcmp(&i.scene,&scene_original,sizeof(i.scene))&&!memcmp(&i.command,&command_original,sizeof(i.command)),"both original action samples remain unmodified");
        i.command.frame.actions.hands[0].grip.pose.position.x+=.02F;
        input::CommandAimProposal different{};input::BorrowedPlacementValues unchanged{};
        check(paired_aim(i,&different)==input::CommandAimStatus::accepted&&paired_visible(i,&unchanged)==input::BorrowedPlacementStatus::proposed&&
            a::distance(different.forward,cmd.forward)>.001F&&exact_geometry(visual,unchanged),"newer independent command support pose never replaces the scene's original pose");
        i.scene=command_scene;i.command=command_original;
        i.scene.prediction.head_center={rotation(1,.4F,-.3F),{5,6,7}};i.command.frame.head_center=i.scene.prediction.head_center;
        check(paired_aim(i,&different)==input::CommandAimStatus::accepted&&paired_visible(i,&unchanged)==input::BorrowedPlacementStatus::proposed&&
            different.packed==cmd.packed&&exact_geometry(visual,unchanged),"head movement alone cannot rotate or translate the owned controller pair");
    }
    // Every support-only refusal must preserve the precise old A2 right result,
    // including squeeze and tracking recovery while still held (no hidden latch).
    for(unsigned mode=0;mode<19;++mode){
        Inputs i(0,0);pair_inputs(i);auto& x=i.scene.prediction.actions;const auto nan=std::numeric_limits<float>::quiet_NaN();
        switch(mode){
        case 0:x.hands[0].squeeze.current=.64999F;break;
        case 1:x.hands[1].squeeze.current=.64999F;break;
        case 2:x.hands[0].squeeze.active=false;break;
        case 3:x.hands[1].squeeze.active=false;break;
        case 4:x.hands[0].squeeze.current=nan;break;
        case 5:x.hands[1].squeeze.current=1.001F;break;
        case 6:x.hands[0].grip.active=false;break;
        case 7:x.hands[0].grip.position_valid=false;break;
        case 8:x.hands[0].grip.position_tracked=false;break;
        case 9:x.hands[0].grip.pose.position.x=nan;break;
        case 10:x.hands[0].grip.pose.position=x.hands[1].grip.pose.position;break;
        case 11:x.hands[0].grip.pose.position=a::add(x.hands[1].grip.pose.position,xr::Rotate(x.hands[1].aim.pose.orientation,{0,0,-.10F}));break;
        case 12:x.hands[0].grip.pose.position=a::add(x.hands[1].grip.pose.position,xr::Rotate(x.hands[1].aim.pose.orientation,{0,0,-.82F}));break;
        case 13:x.hands[0].grip.pose.position=a::add(x.hands[1].grip.pose.position,xr::Rotate(x.hands[1].aim.pose.orientation,{0,0,.3F}));break;
        case 14:x.hands[0].grip.pose.position=a::add(x.hands[1].grip.pose.position,xr::Rotate(x.hands[1].aim.pose.orientation,{.3F,0,-.02F}));break;
        case 15:x.hands[0].squeeze.current=-.01F;break;
        case 16:x.hands[1].squeeze.current=std::numeric_limits<float>::infinity();break;
        case 17:x.hands[0].grip.pose.position={1e30F,1e30F,1e30F};break;
        case 18:x.hands[0].squeeze.current=0;break;
        }
        i.command.frame.actions=x;input::CommandAimProposal cmd{},old{};input::BorrowedPlacementValues v{},vold{};
        if(mode==1||mode==3||mode==5||mode==16){
            check(paired_aim(i,&cmd)==input::CommandAimStatus::accepted&&cmd.support.paired&&
                paired_visible(i,&v)==input::BorrowedPlacementStatus::proposed&&v.support.paired&&
                a::distance(cmd.forward,v.gun.axis.forward)<.00001F,"COD4 support hold ignores unused dominant squeeze");
            continue;
        }
        check(paired_aim(i,&cmd)==input::CommandAimStatus::accepted&&aim(i,&old)==input::CommandAimStatus::accepted&&
            !cmd.support.paired&&cmd.packed==old.packed&&!memcmp(&cmd.forward,&old.forward,sizeof(cmd.forward)),"support refusal returns bit-exact A2 command orientation");
        check(paired_visible(i,&v)==input::BorrowedPlacementStatus::proposed&&visible(i,&vold)==input::BorrowedPlacementStatus::proposed&&
            !v.support.paired&&exact_geometry(v,vold),"support refusal returns bit-exact A2 visual and authored barrel");
        pair_inputs(i);check(paired_aim(i,&cmd)==input::CommandAimStatus::accepted&&cmd.support.paired,"fresh held pair requalifies immediately without retained rearm state");
    }
    {
        xr::ActionSnapshot x{};x.focused=true;
        for(auto& h:x.hands){h.squeeze.active=true;h.squeeze.current=.65F;h.grip.active=h.grip.position_valid=h.grip.position_tracked=true;}
        x.hands[1].aim.active=x.hands[1].aim.orientation_valid=x.hands[1].aim.orientation_tracked=true;
        x.hands[0].grip.pose.position={0,0,-.1016F};
        check(input::two_hand_aim_basis(x,{},xr::Hand::Right,true).paired,"exact inclusive minimum and threshold admitted");
        x.hands[0].grip.pose.position.z=-.8128F;
        check(input::two_hand_aim_basis(x,{},xr::Hand::Right,true).paired,"exact inclusive maximum admitted");
        check(!input::two_hand_aim_basis(x,{},xr::Hand::Right,false).paired&&!input::two_hand_aim_basis(x,{},xr::Hand::Left,true).paired,"disabled and wrong handedness cannot form pair");
        x.hands[0].grip.pose.position={0,.3F,0};x.hands[1].aim.pose.orientation=rotation(0,1.57079632679F,0);
        check(input::two_hand_aim_basis(x,{},xr::Hand::Right,true).reason==input::SupportAimReason::pole,"pair pole refuses shared geometry before yaw ambiguity");
        x.hands[0].grip.pose.position={0,0,-.3F};x.hands[1].aim.pose.orientation=rotation(0,0,1.2F);
        const auto rolled=input::two_hand_aim_basis(x,{},xr::Hand::Right,true);
        check(rolled.paired&&a::rigid(rolled.local)&&std::fabs(rolled.local.up.y)>.5F,"right wrist roll survives projection onto pair-normal plane");
        const auto original=rolled;x.hands[0].aim.pose.orientation={NAN,NAN,NAN,NAN};
        x.hands[0].grip.pose.orientation={NAN,NAN,NAN,NAN};x.hands[1].grip.pose.orientation={NAN,NAN,NAN,NAN};
        const auto unused=input::two_hand_aim_basis(x,{},xr::Hand::Right,true);
        check(unused.paired&&!memcmp(&unused.local,&original.local,sizeof(unused.local)),"unused grip and left aim rotations do not affect geometric roll");
        auto y=x;check(input::same_support_actions(x,y),"same invalid unused float payload compares by bits");
        y.hands[0].grip.pose.orientation.x=std::bit_cast<float>(std::bit_cast<unsigned>(y.hands[0].grip.pose.orientation.x)^1);
        check(!input::same_support_actions(x,y),"different invalid payload cannot pretend to be original action copy");
        x.hands[1].aim.pose.orientation={};x.hands[0].grip.pose.position={0,.3F,-.026F};
        const auto steep=input::two_hand_aim_basis(x,{},xr::Hand::Right,true);
        check(steep.paired&&a::rigid(steep.local)&&steep.local.forward.z>.99F,"near-parallel right-up pair remains rigid above forward-progress boundary");
    }
    {
        Inputs i(0,0);i.scene.tracking_anchor={};pair_inputs(i);
        auto& actions=i.scene.prediction.actions;
        actions.hands[1].aim.pose.orientation={};actions.hands[1].grip.pose.position={};
        actions.hands[0].grip.pose.position={.15F,.08F,-.4F};
        const auto basis=input::two_hand_aim_basis(actions,{},xr::Hand::Right,true);
        const float length=std::sqrt(.4F*.4F+.15F*.15F+.08F*.08F);
        check(basis.paired&&a::distance(basis.local.forward,{.4F/length,-.15F/length,.08F/length})<.000001F,
            "independent known axis oracle pins OpenXR forward/right/up signs and metric baseline");
        for(unsigned n=0;n<120;++n){
            const auto q=rotation(float(n)*.021F,float(n)*.013F,float(n)*-.017F);
            const xr::Posef anchor{q,{.5F,1.2F,-.7F}};auto transformed=actions;
            transformed.hands[1].aim.pose.orientation=q;
            for(unsigned h=0;h<2;++h)transformed.hands[h].grip.pose.position=a::add(anchor.position,xr::Rotate(q,actions.hands[h].grip.pose.position));
            const auto b=input::two_hand_aim_basis(transformed,anchor,xr::Hand::Right,true);
            check(b.paired&&a::axis_error(b.local,basis.local)<.00001F,"simultaneous full anchor/controller rigid transform preserves local pair including roll");
        }
        i.command.frame.actions=actions;i.command.tracking_anchor={};
        actions.hands[1].grip.position_tracked=false;i.command.frame.actions=actions;
        input::CommandAimProposal cmd{},old{};input::BorrowedPlacementValues visual{};
        check(paired_aim(i,&cmd)==input::CommandAimStatus::accepted&&aim(i,&old)==input::CommandAimStatus::accepted&&cmd.packed==old.packed&&!cmd.support.paired&&
            paired_visible(i,&visual)==input::BorrowedPlacementStatus::tracking,"lost dominant grip position preserves A2 aim fallback and A2 visual refusal independently");
    }
    std::printf("two_hand_geometry_checks=%u cumulative=%u failures=%u native_calls=0 gpu_calls=0 retained_pair_state=0\n",checks-before,checks,failures);
    return failures?1:0;
}
