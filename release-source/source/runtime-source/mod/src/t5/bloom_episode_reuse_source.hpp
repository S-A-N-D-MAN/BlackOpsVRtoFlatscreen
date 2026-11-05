// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "t5/bloom_episode_source.hpp"
#include "t5/native_read_recipe.hpp"
#include "loader/d3d9_observer.hpp"
#include <atomic>
#include <memory>
#include <new>
namespace bovr::t5 {
struct BloomReuseLifetime {
    std::uint64_t producer{},generation{},frame{},reset{},history{},backend{},device_creation{};
    friend bool operator==(const BloomReuseLifetime&,const BloomReuseLifetime&)=default;
};
class BloomEpisodeReuseSession;
struct BloomEpisodeReuseContext {
    BloomShaderSource owner{};std::uintptr_t base{};const AuxiliaryViewLayout* layout{};
    BloomEpisodeReuseSession* session{};BloomReuseLifetime lifetime{};std::uint64_t revision{};
};
// All ordinary fields and allocated bytes have exactly one thread owner. A
// foreign/reset/reentrant caller may ONLY change revocation_, never those fields.
// Storage is not freed by invalidate/close, nor by native unwind. Adapter lifetime
// is explicit process lifetime; its normal owner retirement may call retire().
class BloomEpisodeReuseSession {
    enum class Phase {closed,armed,valid,invalid};
    std::atomic<DWORD> owner_{};std::atomic<std::uint64_t> revocation_{1};
    std::atomic_flag preparing_=ATOMIC_FLAG_INIT;std::atomic<bool> ready_{};
    std::unique_ptr<NativeReadRecipe> recipe_;
    Phase phase_{};bool busy_{},silent_{},reported_{};unsigned rejection_logs_{};
    // Original call identity is immutable from explicit arm through matched
    // adapter/class tokens. key_equal compares the read's remaining inputs;
    // there is no newly supplied call parameter on the source-read callback.
    std::uint64_t bound_{},witness_{};BloomDrawCall call_{};
    struct Key {std::uintptr_t base{};AuxiliaryViewLayout layout{};BloomEpisodeCursor cursor{};
        unsigned fp{};BloomReuseLifetime lifetime{};} key_;
    BloomDrawSnapshot draw_{};BloomEpisodePlan plan_{};
    bool own() const noexcept {return owner_.load(std::memory_order_acquire)==GetCurrentThreadId();}
    bool authority(std::uint64_t revision) const noexcept {
        const auto now=revocation_.load(std::memory_order_acquire);
        return own()&&revision&&revision!=UINT64_MAX&&now==revision;
    }
    bool key_equal(const BloomEpisodeReuseContext& c,const BloomEpisodeCursor& cursor) const noexcept {
        return c.base==key_.base&&c.layout&&*c.layout==key_.layout&&c.lifetime==key_.lifetime&&
            cursor.eye==key_.cursor.eye&&cursor.role==key_.cursor.role&&cursor.pass==key_.cursor.pass&&
            cursor.finished==key_.cursor.finished&&(_mm_getcsr()&~0x3fu)==key_.fp;
    }
    bool valid_after(const BloomEpisodeReuseContext& c) const noexcept {
        return authority(c.revision)&&bound_==c.revision&&
            witness_!=indexed_draw_reentry_exhausted&&indexed_draw_reentry_serial()==witness_;
    }
    void failure(NativeReadFailure why) noexcept {phase_=Phase::invalid;if(recipe_)recipe_->fail(why);}
    void diagnostic() noexcept {
        if(!recipe_||silent_||reported_||rejection_logs_>=8)return;++rejection_logs_;reported_=true;const auto& d=recipe_->detail;
        constexpr const char* names[]{"none","entry_key","record_cap","record_fault","read_mismatch","replay_fault","owner_changed","nested_capture","allocation","closed"};
        trace("t5_bloom_read_reuse_rejected","stage=%s read_index=%u address=%08x size=%u byte=%u difference_observed=%u diagnostic_value_valid=%u expected=%u actual=%u authority_unconfirmed=1 diagnostic_only=1 accepted=0",
            names[unsigned(d.reason)],d.index,unsigned(d.address),d.size,d.byte,unsigned(d.difference_observed),unsigned(d.diagnostic_value_valid),d.expected,d.actual);
    }
    bool read_body(BloomEpisodeReuseContext& c,const BloomEpisodeCursor& cursor,BloomDrawSnapshot* draw,BloomEpisodePlan* plan,BloomReadStatus* why) noexcept {
        if(phase_==Phase::armed&&c.layout)key_={c.base,*c.layout,cursor,_mm_getcsr()&~0x3fu,c.lifetime};
        // Keep the original successful current/read/current order. No recorder
        // is installed during either owner callback. Their reentry revokes us.
        if(!c.owner.current||!c.layout||!c.owner.current(c.owner.context)){failure(NativeReadFailure::owner_changed);return false;}
        if(!valid_after(c)){failure(NativeReadFailure::owner_changed);return false;}
        if(!key_equal(c,cursor)){failure(NativeReadFailure::entry_key);return false;}
        bool ok=false;
        if(phase_==Phase::armed){
            recipe_->reset();
            ok=capture_native_reads(*recipe_,[&]() noexcept {return read_bloom_episode_shader(c.base,*c.layout,cursor,&draw_,&plan_,why);});
        }else if(phase_==Phase::valid){
            if(!key_equal(c,cursor))failure(NativeReadFailure::entry_key);
            else ok=verify_native_reads(*recipe_);
        }else return false;
        // Preserve the parent short circuit on a failed reader. A detailed
        // refusal can be reported only at the existing normal callback boundary.
        if(!ok){phase_=Phase::invalid;silent_=why&&*why==BloomReadStatus::unrelated;return false;}
        const bool current=c.owner.current(c.owner.context);
        if(!current||!valid_after(c)){failure(NativeReadFailure::owner_changed);return false;}
        if(!key_equal(c,cursor)){failure(NativeReadFailure::entry_key);return false;}
        if(phase_==Phase::armed)phase_=Phase::valid;
        if(phase_!=Phase::valid||!valid_after(c))return false;
        *draw=draw_;*plan=plan_;if(why)*why=BloomReadStatus::accepted;return true;
    }
    bool read_seh(BloomEpisodeReuseContext& c,const BloomEpisodeCursor& cursor,BloomDrawSnapshot* d,BloomEpisodePlan* p,BloomReadStatus* why) {
        __try {return read_body(c,cursor,d,p,why);}
        __except(GetExceptionCode()==0xe06d7363?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER){failure(NativeReadFailure::record_fault);return false;}
    }
    bool guard_identity(const BloomEpisodeReuseContext& c,const BloomEpisodeCursor& cursor,
        const BloomDrawSnapshot& accepted,const BloomDrawCall& call) const noexcept {
        return phase_==Phase::valid&&c.owner.current&&valid_after(c)&&key_equal(c,cursor)&&accepted==draw_&&
            call.caller==call_.caller&&call.type==call_.type&&call.base_vertex==call_.base_vertex&&
            call.min_vertex==call_.min_vertex&&call.num_vertices==call_.num_vertices&&
            call.start_index==call_.start_index&&call.primitive_count==call_.primitive_count;
    }
    bool guard_check(const BloomEpisodeReuseContext& c,const BloomEpisodeCursor& cursor,
        const BloomDrawSnapshot& accepted,const BloomDrawCall& call) noexcept {
        if(native_read_recorder){native_read_recorder->fail(NativeReadFailure::nested_capture);failure(NativeReadFailure::nested_capture);revoke();return false;}
        if(!guard_identity(c,cursor,accepted,call)){failure(NativeReadFailure::entry_key);return false;}return true;
    }
    bool guard_body(BloomEpisodeReuseContext& c,const BloomEpisodeCursor& cursor,
        const BloomDrawSnapshot& accepted,const BloomDrawCall& call) noexcept {
        if(!guard_check(c,cursor,accepted,call))return false;
        // Preserve both inner current callbacks and their short circuits. The
        // grouped path observes owned key/draw/call identity, no native spans.
        if(!c.owner.current||!c.owner.current(c.owner.context)){failure(NativeReadFailure::owner_changed);return false;}
        if(!guard_check(c,cursor,accepted,call))return false;
        if(!c.owner.current||!c.owner.current(c.owner.context)){failure(NativeReadFailure::owner_changed);return false;}
        if(!guard_check(c,cursor,accepted,call))return false;
        return true;
    }
    bool guard_seh(BloomEpisodeReuseContext& c,const BloomEpisodeCursor& cursor,
        const BloomDrawSnapshot& accepted,const BloomDrawCall& call) {
        __try {return guard_body(c,cursor,accepted,call);}
        __except(GetExceptionCode()==0xe06d7363?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER){failure(NativeReadFailure::record_fault);return false;}
    }
    bool guard_owned_seh(BloomEpisodeReuseContext& c,const BloomEpisodeCursor& cursor,
        const BloomDrawSnapshot& accepted,const BloomDrawCall& call) {
        // No callback or native read can change owner-only copied fields in
        // this interval. Recheck the atomic revision and reentry witness after
        // the full key comparison; the caller supplies both owner callbacks.
        __try {return guard_check(c,cursor,accepted,call)&&valid_after(c);}
        __except(GetExceptionCode()==0xe06d7363?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER){failure(NativeReadFailure::record_fault);return false;}
    }
public:
    bool prepare() noexcept {NativeReadPreserve preserve;
        // Startup configuration may precede the eventual render owner. Publish
        // allocation exactly once without claiming that owner's thread here.
        if(ready_.load(std::memory_order_acquire))return true;
        if(preparing_.test_and_set(std::memory_order_acquire)){revoke();return false;}
        if(!ready_.load(std::memory_order_acquire)){
            recipe_.reset(new(std::nothrow) NativeReadRecipe);ready_.store(!!recipe_,std::memory_order_release);}
        preparing_.clear(std::memory_order_release);return ready_.load(std::memory_order_acquire);}
    void revoke() noexcept {
        NativeReadPreserve preserve;
        auto n=revocation_.load(std::memory_order_relaxed);
        while(n!=UINT64_MAX&&!revocation_.compare_exchange_weak(n,n+1,std::memory_order_acq_rel,std::memory_order_relaxed)){}
    }
    std::uint64_t bind() noexcept {NativeReadPreserve preserve;DWORD empty=0;
        if(!ready_.load(std::memory_order_acquire)){revoke();return 0;}
        owner_.compare_exchange_strong(empty,GetCurrentThreadId(),std::memory_order_acq_rel);
        if(!own()){revoke();return 0;}if(busy_){revoke();return 0;}
        revoke();bound_=revocation_.load(std::memory_order_acquire);phase_=Phase::closed;
        if(!recipe_||bound_==UINT64_MAX)return 0;recipe_->reset();return bound_;
    }
    bool current(std::uint64_t revision) const noexcept {return authority(revision);}
    void report_refusal() noexcept {NativeReadPreserve preserve;
        if(own()&&!busy_&&phase_==Phase::invalid)diagnostic();}
    bool arm(const BloomDrawCall& call) noexcept {NativeReadPreserve preserve;
        if(!own()){revoke();return false;}if(busy_||phase_!=Phase::closed){revoke();return false;}
        if(!recipe_||!authority(bound_))return false;
        phase_=Phase::armed;silent_=false;reported_=false;call_=call;witness_=indexed_draw_reentry_serial();recipe_->reset();
        if(witness_==indexed_draw_reentry_exhausted){failure(NativeReadFailure::owner_changed);return false;}return true;
    }
    void close() noexcept {NativeReadPreserve preserve;
        if(!own()){revoke();return;}if(busy_){revoke();return;}
        phase_=Phase::closed;if(recipe_)recipe_->reset();
    }
    void retire() noexcept {NativeReadPreserve preserve;revoke();if(!own()||busy_)return;phase_=Phase::closed;}
    ~BloomEpisodeReuseSession(){NativeReadPreserve preserve;recipe_.reset();}
    bool read(BloomEpisodeReuseContext& c,const BloomEpisodeCursor& cursor,BloomDrawSnapshot* d,BloomEpisodePlan* p,BloomReadStatus* why) noexcept {
        NativeReadPreserve preserve;BloomReadStatus local_status{};auto* status=why?why:&local_status;
        if(d)*d={};if(p)*p={};*status=BloomReadStatus::changed;
        if(!own()){revoke();return false;}if(busy_){revoke();return false;}
        if(native_read_recorder){native_read_recorder->fail(NativeReadFailure::nested_capture);failure(NativeReadFailure::nested_capture);revoke();return false;}
        if(!d||!p||!recipe_||!authority(c.revision)||c.revision!=bound_||!c.layout){
            if(phase_==Phase::armed||phase_==Phase::valid)failure(NativeReadFailure::entry_key);return false;}
        busy_=true;
        bool ok=false;try {ok=read_seh(c,cursor,d,p,status);}catch(...){failure(NativeReadFailure::record_fault);}
        busy_=false;if(!ok){*d={};*p={};}return ok;
    }
    bool guard(BloomEpisodeReuseContext& c,const BloomEpisodeCursor& cursor,
        const BloomDrawSnapshot& accepted,const BloomDrawCall& call) noexcept {
        NativeReadPreserve preserve;
        if(!own()){revoke();return false;}if(busy_){revoke();return false;}
        if(native_read_recorder){native_read_recorder->fail(NativeReadFailure::nested_capture);failure(NativeReadFailure::nested_capture);revoke();return false;}
        if(!recipe_||!authority(c.revision)||c.revision!=bound_||!c.layout||phase_!=Phase::valid){failure(NativeReadFailure::entry_key);return false;}
        busy_=true;bool ok=false;
        try {ok=guard_seh(c,cursor,accepted,call);}catch(...){failure(NativeReadFailure::record_fault);}
        busy_=false;return ok;
    }
    bool guard_owned(BloomEpisodeReuseContext& c,const BloomEpisodeCursor& cursor,
        const BloomDrawSnapshot& accepted,const BloomDrawCall& call) noexcept {
        NativeReadPreserve preserve;
        if(!own()){revoke();return false;}if(busy_){revoke();return false;}
        if(native_read_recorder){native_read_recorder->fail(NativeReadFailure::nested_capture);failure(NativeReadFailure::nested_capture);revoke();return false;}
        if(!recipe_||!authority(c.revision)||c.revision!=bound_||!c.layout||phase_!=Phase::valid){failure(NativeReadFailure::entry_key);return false;}
        busy_=true;bool ok=false;
        try {ok=guard_owned_seh(c,cursor,accepted,call);}catch(...){failure(NativeReadFailure::record_fault);}
        busy_=false;return ok;
    }
    // Owner-only inspection for bounded CPU fixtures; no native authority API.
    bool open() const noexcept {return own()&&(phase_==Phase::armed||phase_==Phase::valid);}
};
inline BloomShaderSource make_bloom_episode_reuse_source(BloomEpisodeReuseContext& c) noexcept {
    auto out=c.owner;out.context=&c;out.read=nullptr;out.native_draw_return=c.base+0x30b5d3;
    out.initial_eye=c.layout&&postfx_view_count(c.layout->count)?postfx_first_eye(c.layout->count):2u;
    out.current=[](void* p) noexcept {auto& v=*static_cast<BloomEpisodeReuseContext*>(p);NativeReadPreserve preserve;
        if(!v.session)return false;
        if(!v.owner.current){v.session->revoke();return false;}
        if(!v.session->current(v.revision)){v.session->revoke();return false;}
        const bool current=v.owner.current(v.owner.context);
        if(!current){v.session->revoke();return false;}return v.session->current(v.revision);};
    out.read_episode=[](void* p,const BloomEpisodeCursor& cursor,BloomDrawSnapshot* d,BloomEpisodePlan* plan,BloomReadStatus* why) noexcept {
        auto& v=*static_cast<BloomEpisodeReuseContext*>(p);
        if(!v.session){if(d)*d={};if(plan)*plan={};if(why)*why=BloomReadStatus::changed;return false;}
        return v.session->read(v,cursor,d,plan,why);};
    out.query_guard=[](void* p,const BloomEpisodeCursor& cursor,const BloomDrawSnapshot& accepted,const BloomDrawCall& call) noexcept {
        auto& v=*static_cast<BloomEpisodeReuseContext*>(p);return v.session&&v.session->guard(v,cursor,accepted,call);};
    out.query_guard_owned=[](void* p,const BloomEpisodeCursor& cursor,const BloomDrawSnapshot& accepted,const BloomDrawCall& call) noexcept {
        auto& v=*static_cast<BloomEpisodeReuseContext*>(p);return v.session&&v.session->guard_owned(v,cursor,accepted,call);};return out;
}
}
