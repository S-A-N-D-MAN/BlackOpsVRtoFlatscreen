// SPDX-License-Identifier: GPL-3.0-only
#include "t5/native_clear_pair.hpp"
#include "t5/borrowed_weapon_claim.hpp"
#include <cstdio>
#include <functional>
using namespace bovr::t5::animation_detail;
namespace {unsigned checks{},failures{};void check(bool ok,const char* msg){++checks;if(!ok){++failures;std::printf("FAIL %s\n",msg);}}
ClearValue sample(std::uint64_t serial=10,std::uint64_t time=1000){ClearValue v{};v.valid=true;v.serial=serial;v.published_ms=time;
    v.key.command_epoch=3;v.key.command_sequence=400;v.key.thread=19;v.key.weapon=4;v.key.profile_row=2;v.key.native_time=100;
    v.key.frame_time=16;v.key.command_number=90;v.key.command_clock=150;v.key.skeleton_clock=35;v.key.gun_bone=60;v.key.flash_bone=66;
    v.key.bone_count=70;v.key.pool_index=21;v.key.opaque_identity={100,200,300,400,500,600};return v;}}
int main(){
    { // Ordinary unreserved normal work retires both copied metadata channels.
        NativeClearPair pair;bovr::t5::BorrowedCameraClaim camera;
        auto a=sample(),b=sample(11,1010);b.invocation_token=700;
        pair.begin(10,true);pair.camera(a);camera.publish(a);
        check(camera.available(10),"fixture owns a copied camera observation before ordinary wrapper refusal");
        pair.begin(11,false);
        unsigned profile_work{},normal_work{},return_work{};
        std::uint64_t attempt_serial=10;
        if(!borrowed_normal_preparation_required(true,true,false,false)) {
            pair.invalidate();camera.invalidate();attempt_serial=0;
        } else {
            ++profile_work;ClearValue previous{};normal_work+=pair.normal(b,&previous);
        }
        ClearValue completed{};
        if(pair.has_normal(11)){++profile_work;return_work+=pair.take_normal(b.key,11,700,1020,&completed);}
        check(!profile_work&&!normal_work&&!return_work,"unreserved ordinary normal and return perform no profile/completion work");
        check(!attempt_serial&&!camera.available(10)&&!pair.has_normal(11),"refusal leaves no stale copied camera, attempt or normal metadata");
        check(!pair.take_normal(b.key,11,700,1020,&completed)&&!completed.valid,"current wrapper cannot consume a refused normal record");
        // A new reservation must still earn a fresh camera and adjacent normal.
        a=sample(14,1030);b=sample(15,1040);b.invocation_token=701;
        pair.begin(14,true);pair.camera(a);camera.publish(a);attempt_serial=14;
        pair.begin(15,false);ClearValue before{};
        check(borrowed_normal_preparation_required(true,true,false,true)&&pair.normal(b,&before)&&before.serial==14,
              "fresh active claim recovers through its independent current camera/normal pair");
        check(pair.take_normal(b.key,15,701,1050,&completed)&&completed.serial==15,
              "recovered normal retains current invocation and completion checks");
        check(!pair.take_normal(b.key,15,701,1050,&completed),"recovered normal remains single-use");
    }
    for(unsigned route=0;route<4;++route) {
        // Flag off, writer off, primary grenade begun after camera, and an
        // active tracking-loss claim all retain the original observer protocol.
        const bool enabled=route!=0,writer=route!=1,grenade=route==2,active=route==3;
        NativeClearPair pair;auto a=sample(),b=sample(11,1010);b.invocation_token=700;
        pair.begin(10,true);pair.camera(a);pair.begin(11,false);ClearValue before{},after{};
        check(borrowed_normal_preparation_required(enabled,writer,grenade,active),
              "disabled/passive/grenade/active-claim exception preserves normal preparation");
        check(pair.normal(b,&before)&&before.serial==10&&pair.take_normal(b.key,11,700,1020,&after),
              "exception route preserves adjacent normal and current wrapper completion");
    }
    { // An active claim is eligibility only; stale payload checks still reject.
        NativeClearPair pair;auto a=sample(),b=sample(11,1010);b.invocation_token=700;
        pair.begin(10,true);pair.camera(a);pair.begin(11,false);ClearValue before{},after{};
        check(borrowed_normal_preparation_required(true,true,false,true),"active claim reaches original payload checks");
        b.key.profile_revision++;
        check(!pair.normal(b,&before)&&!before.valid,"gate cannot authorize changed copied profile revision");
        check(!pair.take_normal(a.key,11,700,1020,&after)&&!after.valid,"stale original key cannot complete a changed normal");
    }
    NativeClearPair p;auto a=sample(),b=sample(11,1010);b.invocation_token=700;ClearValue before{},after{};
    p.begin(10,true);p.camera(a);p.begin(11,false);check(p.normal(b,&before)&&before.serial==10,"one immediately adjacent camera/normal pair accepted");
    check(p.has_normal(11)&&p.take_normal(b.key,11,700,1020,&after)&&after.serial==11,"normal record consumed on exact current native clear");
    check(!p.take_normal(b.key,11,700,1020,&after)&&!after.valid,"normal is single-use even with repeated same context");
    const std::function<void(ClearComparisonKey&)> mutations[]{[](auto&x){++x.command_epoch;},[](auto&x){++x.command_sequence;},[](auto&x){++x.thread;},
      [](auto&x){++x.weapon;},[](auto&x){++x.profile_row;},[](auto&x){++x.native_time;},[](auto&x){++x.frame_time;},[](auto&x){++x.command_number;},
      [](auto&x){++x.command_clock;},[](auto&x){++x.skeleton_clock;},[](auto&x){++x.weapon_state;},[](auto&x){++x.other_weapon_state;},
      [](auto&x){++x.gun_bone;},[](auto&x){++x.flash_bone;},[](auto&x){++x.bone_count;},[](auto&x){++x.pool_index;},
      [](auto&x){++x.opaque_identity[0];},[](auto&x){++x.opaque_identity[1];},[](auto&x){++x.opaque_identity[2];},
      [](auto&x){++x.opaque_identity[3];},[](auto&x){++x.opaque_identity[4];},[](auto&x){++x.opaque_identity[5];}};
    for(const auto& mutate:mutations){NativeClearPair q;auto changed=b;mutate(changed.key);q.begin(10,true);q.camera(a);q.begin(11,false);
        check(!q.normal(changed,&before)&&!before.valid,"every native/profile/clock/context difference prevents a camera comparison");
        check(q.has_normal(11)&&!q.take_normal(b.key,11,700,1020,&after),"normal-to-after join requires the exact same copied identity too");
        check(!q.take_normal(changed.key,11,700,1020,&after),"failed consume cannot later replay a retained record");}
    for(unsigned mode=0;mode<7;++mode){NativeClearPair q;q.begin(10,true);q.camera(a);auto normal=b;
        if(mode==0)q.begin(11,false); // an intervening unsupported native clear
        if(mode==1)q.begin(11,true); // newer camera, no sample
        if(mode==2)q.invalidate();
        if(mode==3)normal.published_ms=999;
        if(mode==4)normal.published_ms=1101;
        if(mode==5)normal.valid=false;
        if(mode==6)normal.serial=10;
        q.begin(mode<2?12:11,false);if(mode<2)normal.serial=12;
        check(!q.normal(normal,&before),"intervening clear, invalidation, expiry, regression or rejected sample cannot pair");}
    for(unsigned mode=0;mode<5;++mode){NativeClearPair q;q.begin(10,true);q.camera(a);q.begin(11,false);q.normal(b,&before);
        if(mode==0)q.begin(12,false);if(mode==1)q.invalidate();
        check(!q.take_normal(b.key,mode==2?12:11,700,mode==3?1009:mode==4?1111:1020,&after),"after-native rejects intervening epoch, stale or regressed original time");}
    for(std::uint64_t token:{0ULL,699ULL,701ULL}){NativeClearPair q;q.begin(10,true);q.camera(a);q.begin(11,false);q.normal(b,&before);
        check(!q.take_normal(b.key,11,token,1020,&after)&&!after.valid,"missing or foreign wrapper token cannot consume same native context");
        check(!q.take_normal(b.key,11,700,1020,&after),"failed wrapper-scope consume invalidates retained normal result");}
    {NativeClearPair q;auto invalid=b;invalid.invocation_token=0;q.begin(10,true);q.camera(a);q.begin(11,false);
        check(!q.normal(invalid,&before)&&!q.has_normal(11),"normal clear without an actual invocation token cannot publish");}
    {NativeClearPair q;auto invalid=a;invalid.invocation_token=700;q.begin(10,true);q.camera(invalid);q.begin(11,false);
        check(!q.normal(b,&before),"camera sample cannot claim an active weapon invocation");}
    std::printf("native_clear_pair_checks=%u failures=%u native_calls=0\n",checks,failures);return failures?1:0;
}
