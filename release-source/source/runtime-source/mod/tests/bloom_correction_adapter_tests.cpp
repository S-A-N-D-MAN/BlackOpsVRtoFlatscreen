// SPDX-License-Identifier: GPL-3.0-only
#define main inherited_c12_class_main
#include "bloom_correction_replay_tests.cpp"
#undef main
#include "t5/bloom_episode_reuse_source.hpp"
namespace actual_adapter_seams {
struct Key {std::uint64_t generation{},frame_id{};friend bool operator==(const Key&,const Key&)=default;};
struct Produced {bool auxiliary{};Key key{};bool regular_slots_copied{};};
struct Passive {unsigned before_calls{},after_calls{},abandon_calls{};std::uint64_t before(IDirect3DDevice9*,const BloomDrawCall&){++before_calls;return 999;}
    void after(std::uint64_t t,HRESULT){check(t==999,"passive token is kept separate");++after_calls;}void abandon(std::uint64_t=0){++abandon_calls;}};
struct Adapter {
    bool auxiliary_history_enabled{},history_correction_enabled{},auxiliary_enabled{},menu_enabled{};
    BloomCorrectionReplay& bloom_correction;Passive bloom_shaders;
    bool bloom_correction_enabled{true},bloom_shader_enabled{};std::atomic<bool> history_backend_active{true},device_lost{};
    bool bloom_ordinary_correction{true};std::atomic<bool> bloom_scene_active{true};
    std::atomic<DWORD> owner{GetCurrentThreadId()};IDirect3DDevice9* retained_device{};std::uintptr_t base{0x400000};
    std::uint64_t bloom_passive_token{},bloom_active_token{},bloom_callback_serial{};
    Key bloom_rejected_key{};
    BloomEpisodeReuseSession bloom_read_reuse;
    explicit Adapter(BloomCorrectionReplay& correction):bloom_correction(correction){check(bloom_read_reuse.prepare()&&bloom_read_reuse.bind(),"actual adapter fixture prepares bounded active-only session");}
};
std::atomic<Adapter*> adapter{};
#include "bloom_correction_adapter_seams.inc"
IndexedDrawCall call(std::uintptr_t caller=0x70b5d3){return {caller,D3DPT_TRIANGLELIST,0,0,4,16,2};}
}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);_mm_setcsr(0x1f80);using namespace actual_adapter_seams;
    {Fixture f;Adapter a{f.d.correction};a.retained_device=f.d.api();a.bloom_shader_enabled=true;
        a.bloom_ordinary_correction=false;a.bloom_scene_active=false;
        const auto calls=f.d.calls;const Produced ordinary{false,{2,91},true};
        check(!before_bloom_draw(&a,f.d.api(),call())&&f.d.calls==calls&&!a.bloom_read_reuse.open()&&!a.bloom_shaders.before_calls,
            "default ordinary RGB route performs no replay, source/quad capture or passive probe work");
        check(complete_bloom_draw_scope(a,ordinary,false,true)&&f.d.calls==calls,"default regular copied slots complete without a replay scope");
        check(!complete_bloom_draw_scope(a,ordinary,false,false)&&a.bloom_rejected_key==ordinary.key,"RGB pair failure rejects the exact ordinary key");
        a.bloom_ordinary_correction=true;
        check(!complete_bloom_draw_scope(a,ordinary,false,true),"ordinary replay opt-in still requires successful current replay scope");}
    unsigned readiness_checks=0;
    {Fixture f;Adapter a{f.d.correction};
        auto ready_check=[&](bool expected,const char* label){++readiness_checks;check(weapon_integration_renderer_ready()==expected,label);};
        adapter.store(nullptr);ready_check(false,"actual renderer readiness rejects absent adapter");
        adapter.store(&a);a.history_correction_enabled=a.auxiliary_enabled=a.menu_enabled=true;
        ready_check(true,"active history with passive diagnostic off is the actual J renderer route");
        a.auxiliary_history_enabled=true;ready_check(true,"optional passive history does not change ready active route");
        a.history_correction_enabled=false;ready_check(false,"passive history alone cannot replace active history");
        a.auxiliary_history_enabled=false;ready_check(false,"missing active history rejects regardless of passive probe");
        a.history_correction_enabled=true;
        for(bool* prerequisite:{&a.bloom_correction_enabled,&a.auxiliary_enabled,&a.menu_enabled}){
            *prerequisite=false;ready_check(false,"actual renderer readiness retains each existing prerequisite");*prerequisite=true;
        }
        ready_check(true,"all active prerequisites restored admit readiness again");adapter.store(nullptr);
    }
    {Fixture f;f.begin();Adapter a{f.d.correction};a.retained_device=f.d.api();a.bloom_shader_enabled=true;
        const auto token=before_bloom_draw(&a,f.d.api(),call());check(token&&a.bloom_active_token&&a.bloom_passive_token==999,"actual adapter admits separate active/passive tokens");
        after_bloom_draw(&a,token,D3D_OK);check(f.d.own_draws==1&&f.restored()&&a.bloom_shaders.before_calls==1&&a.bloom_shaders.after_calls==1,"actual composite after invokes passive before restored active replay");}
    {Fixture f;Adapter a{f.d.correction};a.retained_device=f.d.api();a.bloom_shader_enabled=true;a.bloom_correction_enabled=false;
        const auto token=before_bloom_draw(&a,f.d.api(),call());after_bloom_draw(&a,token,S_OK);check(token==999&&f.d.calls.empty()&&a.bloom_shaders.after_calls==1,"actual disabled-active branch keeps frozen passive routing and zero correction COM");}
    for(unsigned mode=0;mode<3;++mode){Fixture f;f.begin();auto token=f.before();f.d.hook_name="up";f.d.hook=[&]{f.d.failure="get_stream";};f.after(token);
        check(f.d.correction.pending(),"retained pending stream before unrelated callback");f.d.failure.clear();Adapter a{f.d.correction};a.retained_device=f.d.api();a.history_backend_active=false;
        const auto calls=f.d.calls;
        if(mode==0){before_bloom_draw(&a,f.d.api(),call(0x4567));check(!f.d.correction.pending()&&f.restored(),"actual unrelated caller reconciles pending state before filter");}
        if(mode==1){a.device_lost=true;before_bloom_draw(&a,f.d.api(),call(0x4567));check(f.d.correction.pending()&&calls==f.d.calls,"failed ResetEx device is not queried by draw preflight");}
        if(mode==2){std::thread foreign([&]{before_bloom_draw(&a,f.d.api(),call(0x4567));});foreign.join();check(f.d.correction.pending()&&calls==f.d.calls,"foreign draw cannot reconcile owner journal");}
    }
    for(bool regular:{false,true})for(unsigned mode=0;mode<3;++mode){Fixture f(0);const bool began=f.begin();check(began,"actual begin returns current installed scope");Adapter a{f.d.correction};a.retained_device=f.d.api();Produced auxiliary{!regular,{2,9},regular};
        if(mode==0)f.pair();if(mode==1){f.d.states[D3DRS_ALPHABLENDENABLE]=TRUE;check(!f.before(),"selected current auxiliary state rejected");}if(mode==2)f.d.correction.abandon();
        check(complete_bloom_draw_scope(a,auxiliary,began,true)==(mode==0),"actual auxiliary completion retains current success/reject/abandon result");f.d.correction.end_scope();
        const auto calls=f.d.calls;const auto prior_key=a.bloom_rejected_key;Produced ordinary{false,{2,10}};
        check(complete_bloom_draw_scope(a,ordinary,false,true)&&calls==f.d.calls&&a.bloom_rejected_key==prior_key,"actual ordinary completion after auxiliary success/reject/abandon ignores stale state without COM or new discard key");
        ordinary.key.frame_id=11;check(!complete_bloom_draw_scope(a,ordinary,false,false)&&a.bloom_rejected_key==ordinary.key,"ordinary native staging failure still rejects its own exact key");
        f.d.states[D3DRS_ALPHABLENDENABLE]=FALSE;const bool next_began=f.begin();f.pair();check(next_began&&complete_bloom_draw_scope(a,auxiliary,next_began,true),"fresh auxiliary begin after ordinary bypass clears historical success/reject/abandon state and completes");}
    {Fixture f;const bool began=f.begin();Adapter a{f.d.correction};a.retained_device=f.d.api();auto t=f.before();f.d.hook_name="up";f.d.hook=[&]{f.d.failure="get_stream";};f.after(t);
        check(!complete_bloom_draw_scope(a,{true,{2,9}},began,true)&&f.d.correction.pending(),"current auxiliary failure retains pending journal");f.d.correction.end_scope();
        const auto calls=f.d.calls;Produced ordinary{false,{2,10}};check(!complete_bloom_draw_scope(a,ordinary,false,true)&&f.d.correction.pending()&&calls==f.d.calls&&a.bloom_rejected_key==ordinary.key,"pending journal globally rejects ordinary scene without cleanup in completion");
        f.d.failure.clear();check(f.d.correction.reconcile_normal(f.d.api(),true)&&complete_bloom_draw_scope(a,ordinary,false,true),"ordinary scope passes only after actual normal pending restoration");
        f.d.hook={};const bool next_began=f.begin();f.pair();check(next_began&&complete_bloom_draw_scope(a,{true,{2,9}},next_began,true),"fresh auxiliary sequence succeeds after pending restoration and ordinary transition");}
    for(unsigned mode=0;mode<4;++mode){Fixture f;bool began=false;
        if(mode==0)began=f.begin(true,UINT64_MAX); // Also defends direct API misuse; adapter skips this call.
        if(mode==1)began=f.begin(false);
        if(mode==2){f.d.add_throw_before=true;began=f.begin();}
        if(mode==3){f.d.correction.configure(true);auto invalid=f.source();invalid.creation_serial=0;began=f.d.correction.begin({0,12,2,9,GetCurrentThreadId()},1,f.d.api(),invalid,{&f.d,[](void* p,const BloomDrawSnapshot&,const BloomDrawCall&,BloomQuadSnapshot* q)noexcept{*q=static_cast<Device*>(p)->q;return true;}});}
        Adapter a{f.d.correction};Produced auxiliary{true,{2,9}};check(!began&&!complete_bloom_draw_scope(a,auxiliary,began,true)&&a.bloom_rejected_key==auxiliary.key,"selected skipped/reset-exhausted/disabled/invalid/acquisition-failed begin cannot become accepted inactive auxiliary finish");}
    {Fixture f;f.begin();const auto t=f.before();f.d.hook_name="up";f.d.hook=[&]{f.d.failure="get_stream";};f.after(t);
        Adapter a{f.d.correction};a.retained_device=f.d.api();check(a.bloom_read_reuse.arm(f.draw())&&f.d.correction.pending(),"stale open recipe can coexist with a retained normal restoration journal");
        f.d.failure.clear();const auto gets=f.d.calls["get_stream"];
        check(!before_bloom_draw(&a,f.d.api(),call(0x4567))&&f.d.calls["get_stream"]>gets&&!f.d.correction.pending()&&f.restored(),"actual mandatory journal reconciliation precedes new open-session refusal on unrelated owner draw");a.bloom_read_reuse.close();}
    for(unsigned mode=0;mode<4;++mode){Fixture f;f.begin();Adapter a{f.d.correction};a.retained_device=f.d.api();
        const auto revision=a.bloom_read_reuse.bind();const auto token=before_bloom_draw(&a,f.d.api(),call());
        check(token&&a.bloom_read_reuse.open(),"actual callback arms current recipe before active class");const auto calls=f.d.calls;const auto active_token=a.bloom_active_token;
        if(mode==0){std::thread foreign([&]{after_bloom_draw(&a,token,S_OK);});foreign.join();
            check(a.bloom_active_token==active_token&&a.bloom_read_reuse.open()&&!a.bloom_read_reuse.current(revision)&&calls==f.d.calls,"foreign after only atomically revokes and leaves owner tokens/storage/journal untouched");abandon_bloom_draw(&a,token);}
        if(mode==1){after_bloom_draw(&a,token+1,S_OK);check(!a.bloom_read_reuse.open()&&!f.d.own_draws,"mismatched callback token closes recipe and abandons active authority");}
        if(mode==2){abandon_bloom_draw(&a,token);check(calls==f.d.calls&&!a.bloom_read_reuse.open()&&!a.bloom_read_reuse.current(revision),"native abandon closes owner metadata with no COM and revokes backend reuse");}
        if(mode==3){check(!before_bloom_draw(&a,f.d.api(),call())&&a.bloom_active_token==active_token&&!a.bloom_read_reuse.current(revision),"nested open callback cannot overwrite original active token or rearm recipe");abandon_bloom_draw(&a,token);}}
    {Fixture f;f.begin();f.d.current=false;Adapter a{f.d.correction};a.retained_device=f.d.api();
        check(!before_bloom_draw(&a,f.d.api(),call())&&!a.bloom_read_reuse.open(),"normal token0 closes session even when no native-read authority was published");}
    std::printf("q1_renderer_readiness_checks=%u\n",readiness_checks);
    std::printf("bloom_correction_actual_adapter_seam_checks=%u failures=%u gpu_calls=0\n",checks,failures);return failures?1:0;
}
