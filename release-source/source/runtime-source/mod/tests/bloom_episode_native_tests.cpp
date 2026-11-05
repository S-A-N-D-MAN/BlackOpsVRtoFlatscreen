// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
namespace { unsigned checks{},failures{};void check(bool b,const char* why){++checks;if(!b){++failures;std::printf("FAIL %s\n",why);}} }
#include "bloom_episode_fixture.hpp"
#include "t5/bloom_episode_reuse_source.hpp"
#include <bit>
#include <thread>
#include <unordered_map>
namespace {thread_local std::uint64_t fixture_witness{};unsigned reuse_logs{},recorder_leaks{},native_recipe_checks{},source_factory_checks{},query_guard_checks{};bovr::t5::NativeReadRecipe* injected_recorder{};}
namespace bovr {
void trace(const char* event,const char*,...) noexcept {if(!std::strcmp(event,"t5_bloom_read_reuse_rejected"))++reuse_logs;}
std::uint64_t indexed_draw_reentry_serial() noexcept {return fixture_witness;}
}
using namespace bovr::t5;using namespace episode_fixture;
namespace {
// This target uses real protected native copies and a test-owned scalar witness.
// It never calls a D3D object, hook, retail process, driver or GPU.
void raw_recipe_tests(Memory& m){
    const unsigned start=checks;const auto saved=_mm_getcsr();
    auto recipe=std::make_unique<NativeReadRecipe>();auto nested=std::make_unique<NativeReadRecipe>();
    std::array<unsigned char,257> bytes{};for(unsigned i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>(i);
    recipe->append(reinterpret_cast<std::uintptr_t>(bytes.data()),bytes.data(),0);
    check(recipe->failed&&recipe->count==0&&recipe->detail.reason==NativeReadFailure::record_cap,"zero recording extent fails before storage");
    recipe->reset();recipe->append(1,nullptr,257);
    check(recipe->failed&&recipe->used==0,"oversized read refuses before dereferencing supplied copied bytes");
    recipe->reset();for(unsigned i=0;i<recipe->span_cap;++i)recipe->append(reinterpret_cast<std::uintptr_t>(bytes.data()),bytes.data(),1);
    check(!recipe->failed&&recipe->count==4096&&verify_native_reads(*recipe),"exact record cap includes duplicate ordered observations");
    recipe->append(1,nullptr,1);check(recipe->failed&&recipe->count==4096&&recipe->detail.reason==NativeReadFailure::record_cap,"record cap never truncates into acceptance");
    recipe->reset();for(unsigned i=0;i<256;++i)recipe->append(reinterpret_cast<std::uintptr_t>(bytes.data()),bytes.data(),256);
    check(!recipe->failed&&recipe->used==65536&&verify_native_reads(*recipe),"exact copied byte cap valid");
    recipe->append(1,nullptr,1);check(recipe->failed&&recipe->used==65536,"copied byte cap failure leaves bounded prefix");
    recipe->reset();check(!capture_native_reads(*recipe,[]{return true;})&&!native_read_recorder,"empty capture is not reusable authority");
    for(unsigned fail=0;fail<3;++fail){recipe->reset();
        const bool ok=capture_native_reads(*recipe,[&]{unsigned char value{};native_read_copy(reinterpret_cast<std::uintptr_t>(bytes.data()),&value);
            if(fail==1)throw 42;if(fail==2)RaiseException(0xe1341271,0,0,nullptr);return false;});
        check(!ok&&recipe->count==1&&recipe->failed&&recipe->detail.reason==NativeReadFailure::record_fault&&!native_read_recorder,"failed or exceptional capture retains prefix but uninstalls TLS recorder");
    }
    recipe->reset();nested->reset();
    check(!capture_native_reads(*recipe,[&]{unsigned char v{};native_read_copy(reinterpret_cast<std::uintptr_t>(bytes.data()),&v);
        check(!capture_native_reads(*nested,[]{return true;}),"nested recorder rejected without callback");return true;})&&recipe->detail.reason==NativeReadFailure::nested_capture&&nested->failed&&!native_read_recorder,"nested capture poisons both records and cleans TLS");
    recipe->reset();check(capture_native_reads(*recipe,[&]{unsigned char v{};native_read_copy(reinterpret_cast<std::uintptr_t>(bytes.data()),&v);
        std::thread worker([&]{check(!native_read_recorder,"recorder is private to actual thread");native_read_copy(reinterpret_cast<std::uintptr_t>(bytes.data()+1),&v);});worker.join();return true;})&&recipe->count==1,"foreign default copy cannot append owner recipe");
    const auto fault_page=m.base+0x200000;m.commit(fault_page,4096);m.write(fault_page,std::uint32_t{0x11223344});
    recipe->reset();const std::uint32_t word=0x11223344;recipe->append(fault_page,&word,4);DWORD old{};
    check(!!VirtualProtect(reinterpret_cast<void*>(fault_page),4096,PAGE_NOACCESS,&old),"guard owned replay fault page");
    check(!verify_native_reads(*recipe)&&recipe->detail.reason==NativeReadFailure::replay_fault&&recipe->detail.index==0,"protected replay fault is refused");
    DWORD ignored{};check(!!VirtualProtect(reinterpret_cast<void*>(fault_page),4096,old,&ignored),"restore owned fault page");
    // The pointer word is observed before its dependent address. Poison the
    // dependent page: the first mismatch must win without reaching that page.
    std::uint32_t pointer=static_cast<std::uint32_t>(fault_page);recipe->reset();recipe->append(reinterpret_cast<std::uintptr_t>(&pointer),&pointer,4);recipe->append(fault_page,&word,4);
    ++pointer;VirtualProtect(reinterpret_cast<void*>(fault_page),4096,PAGE_NOACCESS,&old);
    check(!verify_native_reads(*recipe)&&recipe->detail.reason==NativeReadFailure::read_mismatch&&recipe->detail.index==0&&recipe->detail.difference_observed,"pointer mismatch terminates before unreadable dependent span");
    VirtualProtect(reinterpret_cast<void*>(fault_page),4096,old,&ignored);
    // A repeated address is a repeated observation, not a deduplicated set.
    unsigned char duplicate=7;recipe->reset();recipe->append(reinterpret_cast<std::uintptr_t>(&duplicate),&duplicate,1);duplicate=9;recipe->append(reinterpret_cast<std::uintptr_t>(&duplicate),&duplicate,1);
    check(!verify_native_reads(*recipe)&&recipe->detail.index==0,"inconsistent duplicate prefix cannot pass by retaining the last value");
    recipe->reset();duplicate=7;recipe->append(reinterpret_cast<std::uintptr_t>(&duplicate),&duplicate,1);unsigned char expected=9;recipe->append(reinterpret_cast<std::uintptr_t>(&duplicate),&expected,1);
    check(!verify_native_reads(*recipe)&&recipe->detail.index==1,"later duplicate occurrence is independently checked");
    for(unsigned size:{1u,2u,3u,4u,5u,7u,8u,16u,20u,80u,256u}){recipe->reset();recipe->append(reinterpret_cast<std::uintptr_t>(bytes.data()),bytes.data(),size);
        for(unsigned byte=0;byte<size;++byte)for(unsigned bit=0;bit<8;++bit){bytes[byte]^=static_cast<unsigned char>(1u<<bit);recipe->failed=false;recipe->detail={};
            check(!verify_native_reads(*recipe)&&recipe->detail.index==0&&recipe->detail.byte==byte&&recipe->detail.diagnostic_value_valid,"all bits including scalar tails are compared");bytes[byte]^=static_cast<unsigned char>(1u<<bit);}
    }
    unsigned captured_cells=0,observations=0,occurrence_bytes=0,duplicates=0;
    for(unsigned slot=0;slot<2;++slot)for(unsigned mask=0;mask<4;++mask)for(unsigned eye:{1u,0u})for(unsigned role:bloom_role_order){if(!bloom_role_enabled(role,mask))continue;
        auto layout=fill(m,slot);episode_setup(m,layout,eye,role,0,mask);BloomEpisodeCursor cursor{eye,role,0,0};BloomDrawSnapshot direct{},captured{};BloomEpisodePlan dp{},cp{};
        check(read_bloom_episode_shader(m.base,layout,cursor,&direct,&dp)&&!native_read_recorder,"default native path stays recorder-free");recipe->reset();
        {NativeReadPreserve preserve;check(capture_native_reads(*recipe,[&]{return read_bloom_episode_shader(m.base,layout,cursor,&captured,&cp);}),"capture actual protected full native reader");}
        check(captured==direct&&cp==dp&&recipe->count&&recipe->used&&!recipe->failed&&!native_read_recorder,"actual capture publishes identical full draw/plan");
        if(recipe->failed||!recipe->count)continue;++captured_cells;observations+=recipe->count;occurrence_bytes+=recipe->used;
        std::unordered_map<std::uintptr_t,unsigned> first;
        for(unsigned i=0;i<recipe->count;++i)for(unsigned j=0;j<recipe->spans[i].size;++j){const auto address=recipe->spans[i].address+j;
            if(!first.emplace(address,i).second)++duplicates;}
        // Every byte of every occurrence is mutated, including duplicate reads
        // and interiors of the largest native copied structures. Expected first
        // refusal is computed from the ordered captured spans, not a hash.
        for(unsigned i=0;i<recipe->count;++i){const auto span=recipe->spans[i];
            for(unsigned j=0;j<span.size;++j){auto* at=reinterpret_cast<unsigned char*>(span.address+j);const auto before=*at;*at=before^1;recipe->failed=false;recipe->detail={};
                check(!verify_native_reads(*recipe)&&recipe->detail.reason==NativeReadFailure::read_mismatch&&recipe->detail.index==first.at(span.address+j)&&recipe->detail.difference_observed&&recipe->detail.diagnostic_value_valid,"every actual native occurrence byte rejects at its earliest dependent read");*at=before;}
            recipe->failed=false;recipe->detail={};check(verify_native_reads(*recipe),"restored source bytes replay successfully without recapture");
        }
        // A real reader failure after a plan prefix must not publish partial
        // draw/plan. A later default read remains independent of the failed TLS.
        if(role==0){const auto page=(m.base+0x30000+7*0x2000)&~std::uintptr_t(4095);VirtualProtect(reinterpret_cast<void*>(page),4096,PAGE_NOACCESS,&old);recipe->reset();captured.native_frame=999;cp.native_frame=999;
            check(!capture_native_reads(*recipe,[&]{return read_bloom_episode_shader(m.base,layout,cursor,&captured,&cp);})&&recipe->count>0&&recipe->failed&&captured==BloomDrawSnapshot{}&&cp==BloomEpisodePlan{}&&!native_read_recorder,"actual protected failure after prefix has no partial publication");
            VirtualProtect(reinterpret_cast<void*>(page),4096,old,&ignored);
            check(read_bloom_episode_shader(m.base,layout,cursor,&captured,&cp)&&captured==direct&&cp==dp&&!native_read_recorder,"default reader recovers after failed recording");}
    }
    check(captured_cells==176&&duplicates>0,"all four plans two eyes two backend buffers and every enabled role covered");
    std::printf("native_recipe_cells=%u recorded_occurrences=%u occurrence_bytes_mutated=%u repeated_address_bytes=%u all_bits_width_cases=11\n",captured_cells,observations,occurrence_bytes,duplicates);
    _mm_setcsr(saved);native_recipe_checks=checks-start;
}
struct ReuseFixture {
    Memory& memory;AuxiliaryViewLayout layout{};BloomEpisodeReuseSession session;BloomEpisodeReuseContext context{};BloomShaderSource source{};BloomEpisodeCursor cursor{};
    unsigned calls{},action{},at_call{},nested_rejects{};bool healthy=true;
    BloomDrawSnapshot* guard_draw{};BloomDrawCall* guard_call{};
    explicit ReuseFixture(Memory& m,unsigned slot=0,unsigned mask=0,unsigned eye=1,unsigned role=0):memory(m){layout=fill(m,slot);episode_setup(m,layout,eye,role,0,mask);cursor={eye,role,0,0};
        context.owner.context=this;context.owner.current=current;context.owner.creation_serial=73;context.owner.creation_thread=GetCurrentThreadId();context.owner.creation_extended=true;
        context.base=m.base;context.layout=&layout;context.session=&session;context.lifetime={11,12,13,14,15,16,17};source=make_bloom_episode_reuse_source(context);}
    static bool current(void* value) noexcept {auto& f=*static_cast<ReuseFixture*>(value);++f.calls;if(native_read_recorder)++recorder_leaks;
        if(f.calls==f.at_call){switch(f.action){case 1:return false;case 2:++f.layout.frame;break;case 3:++f.context.lifetime.history;break;
            case 4:{BloomDrawSnapshot d{};BloomEpisodePlan p{};d.native_frame=p.native_frame=999;const bool ok=f.source.read_episode(f.source.context,f.cursor,&d,&p,nullptr);if(!ok&&d==BloomDrawSnapshot{}&&p==BloomEpisodePlan{})++f.nested_rejects;break;}
            case 5:++fixture_witness;break;case 6:_mm_setcsr(_mm_getcsr()^0x2000);break;case 7:f.session.close();break;
            case 8:f.session.retire();break;case 9:(void)f.session.bind();break;case 10:(void)f.session.arm({});break;
            case 11:{std::thread revoke([&]{f.session.revoke();});revoke.join();break;}case 12:++f.context.base;break;
            case 13:++f.cursor.role;break;case 14:RaiseException(0xe1341271,0,0,nullptr);break;
            case 15:native_read_recorder=injected_recorder;break;case 16:f.context.owner.current=nullptr;break;
            case 17:if(f.guard_draw)++f.guard_draw->role;break;case 18:if(f.guard_call)++f.guard_call->start_index;break;default:break;}}
        SetLastError(0x87654321);return f.healthy;}
    bool begin(){check(session.prepare(),"owner recipe storage prepared");context.revision=session.bind();return context.revision&&session.arm({context.base+0x30b5d3,4,0,0,4,0,2});}
    bool read(BloomDrawSnapshot* d=nullptr,BloomEpisodePlan* p=nullptr,BloomReadStatus* why=nullptr){BloomDrawSnapshot a{};BloomEpisodePlan b{};return source.read_episode(source.context,cursor,d?d:&a,p?p:&b,why);}
};
__declspec(noinline) bool revoke_xmm_witness(BloomEpisodeReuseSession& session){
    alignas(16) unsigned char prior[512];_fxsave(prior);
    alignas(16) const unsigned expected[4]{0x12345678,0x87654321,0x13572468,0xabcdef01};alignas(16) unsigned actual[4]{};
    __asm movdqu xmm0,xmmword ptr expected
    session.revoke();
    __asm movdqu xmmword ptr actual,xmm0
    const bool same=std::memcmp(expected,actual,sizeof actual)==0;_fxrstor(prior);return same;
}
void factory_tests(Memory& m){
    const unsigned start=checks;const auto saved=_mm_getcsr();
    for(unsigned slot=0;slot<2;++slot)for(unsigned mask=0;mask<4;++mask)for(unsigned eye:{1u,0u})for(unsigned role:bloom_role_order){if(!bloom_role_enabled(role,mask))continue;
        ReuseFixture f(m,slot,mask,eye,role);check(f.source.context==&f.context&&!f.source.read&&f.source.read_episode&&f.source.native_draw_return==m.base+0x30b5d3&&f.source.creation_serial==73&&f.source.creation_extended,"real factory preserves owner metadata and disables old callback");
        check(f.begin(),"bound factory arms once");BloomDrawSnapshot expected{},d{};BloomEpisodePlan ep{},p{};check(read_bloom_episode_shader(m.base,f.layout,f.cursor,&expected,&ep),"independent direct full copy before factory");
        for(unsigned read=0;read<3;++read){SetLastError(0x12345678);const auto fp=_mm_getcsr();BloomReadStatus why{};check(f.read(&d,&p,&why)&&d==expected&&p==ep&&why==BloomReadStatus::accepted&&GetLastError()==0x12345678&&_mm_getcsr()==fp,"actual capture then two warm replays match full copied outputs and caller state");}
        check(f.calls==6&&f.session.open(),"original current read current order retained on every success");f.session.close();d.native_frame=p.native_frame=999;
        check(!f.read(&d,&p)&&d==BloomDrawSnapshot{}&&p==BloomEpisodePlan{},"closed scope cannot publish its last snapshot");
        check(f.session.arm({}),"normal close permits explicit next draw arm");check(f.read(&d,&p)&&d==expected&&p==ep,"newly armed draw recaptures after close");f.session.retire();
        check(!f.session.current(f.context.revision)&&!f.read(&d,&p)&&d==BloomDrawSnapshot{}&&p==BloomEpisodePlan{},"retired revision cannot resurrect a cached result");
    }
    for(unsigned field=0;field<20;++field){ReuseFixture f(m);check(f.begin()&&f.read(),"valid recipe before each independent key mutation");const auto old_layout=f.layout;const auto old_context=f.context;const auto old_cursor=f.cursor;const unsigned old_fp=_mm_getcsr();
        if(field==0)++f.context.base;if(field==1)++f.layout.frame;if(field==2)++f.layout.buffer;if(field==3)++f.layout.slots[0].scene.x;
        if(field==4)++f.cursor.eye;if(field==5)++f.cursor.role;if(field==6)++f.cursor.pass;if(field==7)f.cursor.finished=1;
        if(field==8)_mm_setcsr(old_fp^0x2000);if(field>=9&&field<16){auto words=std::bit_cast<std::array<std::uint64_t,7>>(f.context.lifetime);++words[field-9];f.context.lifetime=std::bit_cast<BloomReuseLifetime>(words);}
        if(field==16)++fixture_witness;if(field==17)f.context.layout=nullptr;if(field==18)++f.context.revision;if(field==19)f.session.revoke();
        BloomDrawSnapshot d{};BloomEpisodePlan p{};d.native_frame=p.native_frame=999;
        check(!f.read(&d,&p)&&d==BloomDrawSnapshot{}&&p==BloomEpisodePlan{},"base complete layout cursor FP lifetime witness revision or revocation change refuses without publication");
        f.layout=old_layout;f.context=old_context;f.cursor=old_cursor;_mm_setcsr(old_fp);
        const bool revived=f.read();if(revived)std::printf("unexpected_key_revival_field=%u\n",field);check(!revived,"restoring old key cannot revive a poisoned or revoked draw");
    }
    for(unsigned action=1;action<=14;++action)for(unsigned when:{1u,2u,3u,4u}){ReuseFixture f(m);check(f.begin(),"fresh before/current-boundary mutation");
        if(when>=3)check(f.read(),"warm recipe before late current-boundary mutation");f.action=action;f.at_call=when;
        BloomDrawSnapshot d{};BloomEpisodePlan p{};d.native_frame=p.native_frame=999;const unsigned fp=_mm_getcsr();SetLastError(0x34561234);
        const bool ok=f.read(&d,&p);
        check(!ok&&d==BloomDrawSnapshot{}&&p==BloomEpisodePlan{},"incoming key is captured before initial owner callback; any callback key change or revocation rejects output");
        check(f.calls==when,"key or owner rejection stops at original callback boundary without an additional current call");
        check(GetLastError()==0x34561234&&_mm_getcsr()==fp,"failed callback path restores outer LastError and MXCSR");
        if(action==4)check(f.nested_rejects==1,"nested read while capture or warm replay is busy rejects child and parent");
    }
    for(unsigned when:{1u,2u}){ReuseFixture f(m);check(f.begin(),"reader failure fixture armed");if(when==2)check(f.read(),"warm before actual native mutation");
        const auto address=m.base+0x417eed4;m.write(address,99u);const unsigned before=f.calls;BloomDrawSnapshot d{};BloomEpisodePlan p{};
        check(!f.read(&d,&p)&&f.calls==before+1&&d==BloomDrawSnapshot{}&&p==BloomEpisodePlan{},"actual full/fast reader failure short-circuits post-current callback");m.write(address,0u);check(!f.read(),"same scope stays invalid after native values restored");
        f.session.close();check(f.session.arm({})&&f.read(),"explicit new draw after failure captures anew without renewing process recipe storage");}
    {ReuseFixture f(m);check(!f.session.bind(),"bind before ready allocation fails closed");check(f.begin(),"prepare after failed unready bind permits fresh revision");check(!f.session.arm({})&&!f.read(),"second arm of open draw revokes rather than replacing its key");}
    {ReuseFixture f(m);check(f.begin()&&f.read(),"valid before outer nested capture");auto outer=std::make_unique<NativeReadRecipe>();
        check(!capture_native_reads(*outer,[&]{return f.read();})&&outer->detail.reason==NativeReadFailure::nested_capture&&!f.read()&&!native_read_recorder,"factory inside another capture poisons outer and closes inner authority");}
    {ReuseFixture f(m);check(f.begin(),"witness poison fixture begins");f.session.close();fixture_witness=UINT64_MAX;check(!f.session.arm({})&&!f.read(),"exhausted witness never grants recipe authority");fixture_witness=0;}
    {ReuseFixture f(m);check(f.begin()&&f.read(),"valid before foreign atomic revocation");const auto old=f.context.revision;std::thread other([&]{check(!f.session.current(old),"foreign thread cannot claim owner current");f.session.revoke();});other.join();
        check(!f.session.current(old)&&!f.read(),"foreign revocation invalidates owner without inspecting its storage");f.context.revision=f.session.bind();check(f.context.revision>old&&f.session.arm({})&&f.read(),"owner can explicitly bind a fresh lifetime after joined foreign revocation");}
    {ReuseFixture f(m);check(f.session.prepare(),"configuration prepares allocation before render-owner bind");std::thread owner([&]{f.context.revision=f.session.bind();check(f.context.revision&&f.session.arm({})&&f.read(),"eventual different thread binds prepared allocation and reads actual native data");f.session.close();f.session.retire();});owner.join();
        check(!f.session.current(f.context.revision)&&!f.session.bind(),"preparing thread cannot steal the established render owner");}
    {ReuseFixture f(m);check(f.begin()&&f.read(),"valid before cross-thread bind attempt");std::thread other([&]{check(!f.session.bind(),"foreign bind refuses and only revokes authority");});other.join();check(!f.read(),"foreign bind cannot leave original stale acceptance alive");}
    {ReuseFixture f(m);f.context.session=nullptr;BloomDrawSnapshot d{};BloomEpisodePlan p{};BloomReadStatus why=BloomReadStatus::accepted;d.native_frame=p.native_frame=999;
        check(!f.source.read_episode(f.source.context,f.cursor,&d,&p,&why)&&d==BloomDrawSnapshot{}&&p==BloomEpisodePlan{}&&why==BloomReadStatus::changed,"factory with absent session clears complete outputs and acceptance status");}
    {ReuseFixture f(m);check(f.begin(),"null-output fixture bound");BloomDrawSnapshot d{};BloomEpisodePlan p{};d.native_frame=p.native_frame=999;
        check(!f.source.read_episode(f.source.context,f.cursor,&d,nullptr,nullptr)&&d==BloomDrawSnapshot{}&&!f.read(),"missing plan destination clears supplied draw and latches active scope");
        check(f.begin(),"new explicit binding after missing plan failure");
        check(!f.source.read_episode(f.source.context,f.cursor,nullptr,&p,nullptr)&&p==BloomEpisodePlan{}&&!f.read(),"missing draw destination clears supplied plan and cannot revive on restored argument");}
    {ReuseFixture f(m);check(f.begin()&&f.read(),"valid before source current loss");f.healthy=false;const unsigned calls=f.calls;
        check(!f.source.current(f.source.context)&&f.calls==calls+1,"factory current delegates once then revokes lost owner");f.healthy=true;
        check(!f.source.current(f.source.context)&&f.calls==calls+1&&!f.read(),"restored owner callback cannot revive revoked revision or incur another callback");}
    {ReuseFixture f(m);check(f.begin()&&f.read(),"valid before current callback disappears");const auto callback=f.context.owner.current;const unsigned calls=f.calls;f.context.owner.current=nullptr;
        check(!f.source.current(f.source.context)&&f.calls==calls,"missing owner callback refuses without invocation");f.context.owner.current=callback;
        check(!f.source.current(f.source.context)&&f.calls==calls&&!f.read(),"restored missing callback cannot revive revoked source authority");}
    {ReuseFixture f(m);check(f.begin()&&f.read(),"valid before optimized revocation register witness");const auto fp=_mm_getcsr();SetLastError(0x11225588);
        check(revoke_xmm_witness(f.session)&&GetLastError()==0x11225588&&_mm_getcsr()==fp&&!f.session.current(f.context.revision),"actual optimized revoke preserves XMM0 LastError MXCSR while invalidating revision");}
    {ReuseFixture f(m);check(f.begin()&&f.read(),"valid before foreign read attempt");std::thread other([&]{BloomDrawSnapshot d{};BloomEpisodePlan p{};d.native_frame=p.native_frame=999;
        check(!f.read(&d,&p)&&d==BloomDrawSnapshot{}&&p==BloomEpisodePlan{},"foreign read clears caller outputs and atomically revokes without owner storage access");});other.join();check(!f.read(),"foreign read revokes owner session");}
    {ReuseFixture f(m);auto original=f.layout;std::vector<std::pair<std::size_t,std::size_t>> fields;const auto base=reinterpret_cast<std::uintptr_t>(&f.layout);
        const auto field=[&](auto& value){fields.emplace_back(reinterpret_cast<std::uintptr_t>(&value)-base,sizeof value);};
        field(f.layout.buffer);field(f.layout.view_array);field(f.layout.frame);field(f.layout.count);field(f.layout.selected);
        for(auto& v:f.layout.slots){field(v.address);field(v.scene);field(v.display);field(v.scissor);field(v.targets);field(v.extra);}
        for(auto& target:f.layout.targets){field(target.image);field(target.color);field(target.depth);field(target.width);field(target.height);}
        unsigned changed=0;
        for(const auto& [offset,size]:fields)for(std::size_t byte=0;byte<size;++byte){f.layout=original;check(f.begin()&&f.read(),"fresh valid recipe before each meaningful layout byte");
            auto* value=reinterpret_cast<unsigned char*>(&f.layout)+offset+byte;*value^=1;BloomDrawSnapshot d{};BloomEpisodePlan p{};
            check(!f.read(&d,&p)&&d==BloomDrawSnapshot{}&&p==BloomEpisodePlan{},"all header slot viewport target and dimension bytes participate in layout key");f.session.close();++changed;}
        std::printf("source_factory_layout_value_bytes=%u padding_excluded=1\n",changed);}
    for(bool warm:{false,true}){ReuseFixture f(m);check(f.begin()&&(!warm||f.read()),"protected factory prefix fixture armed or warm");
        const auto page=m.base+0x30000+7*0x2000;DWORD old{},ignored{};check(!!VirtualProtect(reinterpret_cast<void*>(page),4096,PAGE_NOACCESS,&old),"factory future plan page protected");
        BloomDrawSnapshot d{};BloomEpisodePlan p{};d.native_frame=p.native_frame=999;const auto calls=f.calls;
        check(!f.read(&d,&p)&&d==BloomDrawSnapshot{}&&p==BloomEpisodePlan{}&&f.calls==calls+1,"actual captured or replayed protected fault does not publish or call post-current");
        check(!!VirtualProtect(reinterpret_cast<void*>(page),4096,old,&ignored),"restore factory protected page");check(!f.read(),"page accessibility restoration does not revive invalid draw");}
    {ReuseFixture f(m);check(f.begin(),"unrelated closure fixture armed");f.cursor={0,0,0,0};const unsigned logs=reuse_logs;BloomReadStatus why{};
        check(!f.read(nullptr,nullptr,&why)&&why==BloomReadStatus::unrelated,"exact native already-closed right-eye candidate stays unrelated");
        const auto calls=f.calls;for(unsigned repeat=0;repeat<12;++repeat)f.session.report_refusal();
        check(reuse_logs==logs&&f.calls==calls,"unrelated refusal remains silent and introduces no owner callbacks");}
    {ReuseFixture f(m);const unsigned logs=reuse_logs;
        for(unsigned episode=0;episode<12;++episode){check(f.begin()&&f.read(),"fresh draw for bounded refusal reporting");m.write(m.base+0x417eed4,99u);check(!f.read(),"native changed refusal before reporting");const auto calls=f.calls;
            f.session.report_refusal();f.session.report_refusal();check(f.calls==calls,"metadata refusal logging never samples owner current");m.write(m.base+0x417eed4,0u);f.session.close();}
        check(reuse_logs-logs==8,"normal-owned refusal logs retain process-session eight budget across binds");}
    for(unsigned bits=0;bits<16;++bits){const unsigned mode=0x1f80|((bits&3)<<13)|((bits&4)<<4)|((bits&8)<<12)|0x21;_mm_setcsr(mode);SetLastError(0x77771111);
        ReuseFixture f(m);check(f.begin(),"all MXCSR control combinations allow actual reader capture");SetLastError(0x88882222);check(f.read()&&_mm_getcsr()==mode&&GetLastError()==0x88882222,"factory preserves exact MXCSR including flags for every native rounding DAZ FTZ mode");}
    check(recorder_leaks==0&&!native_read_recorder,"owner callbacks never run under installed recorder and every path clears TLS");
    _mm_setcsr(saved);source_factory_checks=checks-start;
}
void query_guard_tests(Memory& m){
    const auto first=checks;const auto saved=_mm_getcsr();
    const auto call_for=[](const ReuseFixture& f){return BloomDrawCall{f.context.base+0x30b5d3,4,0,0,4,0,2};};
    const auto guard=[](ReuseFixture& f,const BloomDrawSnapshot& n,const BloomDrawCall& call){return f.source.query_guard&&f.source.query_guard(f.source.context,f.cursor,n,call);};
    for(unsigned slot=0;slot<2;++slot)for(unsigned mask=0;mask<4;++mask)for(unsigned eye:{1u,0u})for(unsigned role:bloom_role_order){if(!bloom_role_enabled(role,mask))continue;
        ReuseFixture f(m,slot,mask,eye,role);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"actual protected capture precedes optional guard on all176 role cells");
        const auto calls=f.calls;const auto fp=_mm_getcsr();SetLastError(0x98765432);check(guard(f,n,call_for(f))&&f.calls==calls+2&&GetLastError()==0x98765432&&_mm_getcsr()==fp,"actual grouped guard uses two owner callbacks and preserves incoming state");
        const auto page=m.base+0x30000+7*0x2000;DWORD old{},ignored{};check(!!VirtualProtect(reinterpret_cast<void*>(page),4096,PAGE_NOACCESS,&old),"make captured future native plan inaccessible");
        check(guard(f,n,call_for(f)),"cheap guard deliberately does not revisit inaccessible native spans");
        check(!f.read(),"mandatory full endpoint still refuses inaccessible recorded native page");
        check(!!VirtualProtect(reinterpret_cast<void*>(page),4096,old,&ignored),"restore fixture native page");check(!guard(f,n,call_for(f)),"failed full endpoint cannot revive through cheap guard");}
    for(unsigned field=0;field<20;++field){ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"capture before each guard entry-key mutation");
        const auto context=f.context;const auto layout=f.layout;const auto cursor=f.cursor;const auto call=call_for(f);const unsigned fp=_mm_getcsr();const auto witness=fixture_witness;
        if(field==0)++f.context.base;if(field==1)++f.layout.frame;if(field==2)++f.layout.buffer;if(field==3)++f.layout.slots[1].display.x;
        if(field==4)++f.cursor.eye;if(field==5)++f.cursor.role;if(field==6)++f.cursor.pass;if(field==7)f.cursor.finished=1;if(field==8)_mm_setcsr(fp^0x2000);
        if(field>=9&&field<16){auto words=std::bit_cast<std::array<std::uint64_t,7>>(f.context.lifetime);++words[field-9];f.context.lifetime=std::bit_cast<BloomReuseLifetime>(words);}
        if(field==16)++fixture_witness;if(field==17)f.context.layout=nullptr;if(field==18)++f.context.revision;if(field==19)f.session.revoke();
        check(!guard(f,n,call),"every grouped full key cursor FP lifetime witness revision field remains authority");
        f.context=context;f.layout=layout;f.cursor=cursor;_mm_setcsr(fp);fixture_witness=witness;
        check(!guard(f,n,call)&&!f.read(),"restoring grouped key cannot revive invalid session");}
    for(unsigned word=0;word<7;++word){ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"capture before each original call word mutation");
        auto call=call_for(f);auto bytes=std::bit_cast<std::array<std::uint32_t,7>>(call);bytes[word]^=1;call=std::bit_cast<BloomDrawCall>(bytes);
        check(!guard(f,n,call)&&!guard(f,n,call_for(f)),"all seven original arguments reject changed and subsequently restored calls");}
    for(unsigned field=0;field<16;++field){ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"capture before accepted-draw identity mutation");const auto original=n;
        if(field==0)++n.buffer;if(field==1)++n.native_frame;if(field==2)++n.eye;if(field==3)++n.role;if(field==4)++n.target;if(field==5)++n.vertex_shader;if(field==6)++n.pixel_shader;
        if(field==7)++n.scene[0];if(field==8)++n.arguments[0].value;if(field==9)++n.samplers[0].texture;if(field==10)++n.names[0][0];if(field==11)++n.native_target.width;
        if(field==12)++n.scene_viewport.x;if(field==13)++n.custom_sampler_flags;if(field==14)++n.pass_index;if(field==15)++n.constants[7].destination;
        check(!guard(f,n,call_for(f))&&!guard(f,original,call_for(f)),"accepted draw includes current identity geometry code sampler names and unused copied rows");}
    for(unsigned action=1;action<=18;++action)for(unsigned when:{3u,4u}){ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"capture before current callback guard mutation");auto call=call_for(f);
        auto recorder=std::make_unique<NativeReadRecipe>();injected_recorder=recorder.get();f.guard_draw=&n;f.guard_call=&call;f.action=action;f.at_call=when;
        const auto fp=_mm_getcsr();SetLastError(0x22334455);const bool ok=guard(f,n,call);if(ok)std::printf("unexpected_guard_acceptance action=%u when=%u\n",action,when);
        check(!ok,"either inner current callback key owner nested TLS or borrowed argument mutation refuses guard");
        check(f.calls==when&&GetLastError()==0x22334455&&_mm_getcsr()==fp,"guard refusal stops at changed callback and preserves caller FP/error");
        if(action==15)check(recorder->failed,"owner callback left a recorder poisons that recorder as well as session");
        native_read_recorder=nullptr;injected_recorder=nullptr;if(action==16)f.context.owner.current=ReuseFixture::current;
        check(!guard(f,n,call),"callback guard failure remains latched even after a missing callback is restored");}
    {ReuseFixture f(m);BloomDrawSnapshot n{};check(f.begin()&&!guard(f,n,call_for(f))&&!f.read(),"armed but uncaptured session cannot manufacture grouped authority");}
    {ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"valid before closed grouped query");f.session.close();check(!guard(f,n,call_for(f)),"closed session refuses old owned draw");}
    {ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"valid before foreign grouped callback");std::thread foreign([&]{check(!guard(f,n,call_for(f)),"foreign query guard refuses without owning recipe");});foreign.join();check(!guard(f,n,call_for(f)),"foreign callback atomically revokes owner session");}
    {ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"valid before grouped native mutation");const auto at=f.layout.slots[0].address+0x21cc;std::uint32_t before{};std::memcpy(&before,reinterpret_cast<void*>(at),4);
        m.write(at,before^1u);check(guard(f,n,call_for(f)),"cheap owned-key guard deliberately accepts native-only mutation until full endpoint");m.write(at,before);
        check(f.read(),"native mutation restored between full boundaries is an explicit acceptance difference");}
    for(unsigned mode=0;mode<16;++mode){const auto fp=0x1fa1u|((mode&3)<<13)|((mode&4)?0x40:0)|((mode&8)?0x8000:0);_mm_setcsr(fp);
        ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p)&&guard(f,n,call_for(f))&&_mm_getcsr()==fp,"all recorded MXCSR modes preserved by actual owned-key guard");}
    _mm_setcsr(saved);query_guard_checks=checks-first;
}
void owned_query_guard_tests(Memory& m){
    const auto initial=checks,saved=_mm_getcsr();
    const auto call_for=[](const ReuseFixture& f){return BloomDrawCall{f.context.base+0x30b5d3,4,0,0,4,0,2};};
    const auto guard=[](ReuseFixture& f,const BloomDrawSnapshot& n,const BloomDrawCall& call){return f.source.query_guard_owned&&f.source.query_guard_owned(f.source.context,f.cursor,n,call);};
    for(unsigned mode=0;mode<16;++mode){const auto fp=0x1fa1u|((mode&3)<<13)|((mode&4)?0x40:0)|((mode&8)?0x8000:0);_mm_setcsr(fp);
        ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"owned-only guard follows full protected capture");const auto calls=f.calls;
        SetLastError(0x78342119);check(guard(f,n,call_for(f))&&f.calls==calls&&_mm_getcsr()==fp&&GetLastError()==0x78342119,"owned-only guard adds no callback and preserves every MXCSR mode and LastError");}
    _mm_setcsr(saved);
    for(unsigned field=0;field<20;++field){ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"capture before owned-only key mutation");
        const auto context=f.context;const auto layout=f.layout;const auto cursor=f.cursor;const auto call=call_for(f);const auto fp=_mm_getcsr();const auto witness=fixture_witness;
        if(field==0)++f.context.base;if(field==1)++f.layout.frame;if(field==2)++f.layout.buffer;if(field==3)++f.layout.slots[1].display.x;
        if(field==4)++f.cursor.eye;if(field==5)++f.cursor.role;if(field==6)++f.cursor.pass;if(field==7)f.cursor.finished=1;if(field==8)_mm_setcsr(fp^0x2000);
        if(field>=9&&field<16){auto words=std::bit_cast<std::array<std::uint64_t,7>>(f.context.lifetime);++words[field-9];f.context.lifetime=std::bit_cast<BloomReuseLifetime>(words);}
        if(field==16)++fixture_witness;if(field==17)f.context.layout=nullptr;if(field==18)++f.context.revision;if(field==19)f.session.revoke();
        const auto calls=f.calls;check(!guard(f,n,call)&&calls==f.calls,"owned-only guard rejects every immutable key, lifetime, thread witness and FP mismatch without owner calls");
        f.context=context;f.layout=layout;f.cursor=cursor;_mm_setcsr(fp);fixture_witness=witness;check(!guard(f,n,call)&&!f.read(),"restored owned-only identity never revives rejected draw");}
    for(unsigned word=0;word<7;++word){ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"capture before owned original-call mutation");
        auto call=call_for(f);auto words=std::bit_cast<std::array<std::uint32_t,7>>(call);words[word]^=1;call=std::bit_cast<BloomDrawCall>(words);
        check(!guard(f,n,call)&&!guard(f,n,call_for(f)),"owned-only guard retains all seven immutable native call words");}
    {ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"capture before owned borrowed snapshot mutation");const auto old=n;n.constants[7].destination^=1;
        check(!guard(f,n,call_for(f))&&!guard(f,old,call_for(f)),"owned-only guard compares complete accepted snapshot including unused rows");}
    {ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"capture before owned foreign guard");
        std::thread foreign([&]{check(!guard(f,n,call_for(f)),"foreign owned guard refuses");});foreign.join();check(!guard(f,n,call_for(f)),"foreign owned guard revokes the original owner");}
    {ReuseFixture f(m);BloomDrawSnapshot n{};BloomEpisodePlan p{};check(f.begin()&&f.read(&n,&p),"capture before owned endpoint native mutation");
        const auto at=f.layout.slots[0].address+0x21cc;std::uint32_t old{};std::memcpy(&old,reinterpret_cast<void*>(at),4);m.write(at,old^1u);
        check(guard(f,n,call_for(f))&&!f.read(),"owned-only guard does no native reads; unchanged full endpoint catches persistent native mutation");m.write(at,old);}
    std::printf("bloom_owned_query_guard_checks=%u no_owner_callbacks=1 full_endpoint_retained=1\n",checks-initial);
}
}
int main(){
    check(sizeof(BloomEpisodePlan)==3528&&offsetof(BloomEpisodePlan,roles)==96&&sizeof(BloomDrawSnapshot)==1696,"versioned episode and unchanged draw layouts");
    const auto saved=_mm_getcsr();
    for(unsigned rounding=0;rounding<4;++rounding)for(unsigned daz=0;daz<2;++daz)for(unsigned ftz=0;ftz<2;++ftz){
        const unsigned mode=0x1f80|(rounding<<13)|(daz<<6)|(ftz<<15)|0x21;_mm_setcsr(mode);
        for(const auto& words:{std::array<std::uint32_t,6>{0x3f800000,0xbf800000,0,0x80000000,0,0},
            std::array<std::uint32_t,6>{1,0,0,0x3f800000,0,0},std::array<std::uint32_t,6>{0x7f7fffff,0x7f7fffff,0,0,0,0}}){unsigned mask{};bloom_tint_enabled(words,mask);check(_mm_getcsr()==mode,"ordered native ADDSS predicate restores all MXCSR status/control bits");}
    }
    _mm_setcsr(0x1f80);Memory m;check(!!m.base,"complete sparse native fixture");if(!m.base){return 1;}
    for(unsigned views:{2u,3u})for(unsigned slot=0;slot<2;++slot)for(unsigned mask=0;mask<4;++mask)for(unsigned count=1;count<=4;++count){auto l=fill(m,slot,views);const auto first=postfx_first_eye(views);BloomEpisodeSet sequence{first};
        const unsigned expected=2*(9+2*!!(mask&1)+2*!!(mask&2)+count-1);unsigned records=0;
        for(unsigned eye:{first,1u-first})for(unsigned role:bloom_role_order){if(!bloom_role_enabled(role,mask))continue;
            for(unsigned p=0;p<(role?1u:count);++p){episode_setup(m,l,eye,role,p,mask,count);BloomDrawSnapshot n{};BloomEpisodePlan plan{};BloomReadStatus why{};
                SetLastError(0x12348765);const bool ok=read_bloom_episode_shader(m.base,l,sequence.cursor(),&n,&plan,&why);
                check(ok&&why==BloomReadStatus::accepted&&GetLastError()==0x12348765,"actual complete provider admits exact planned role and preserves LastError");
                if(ok){check(plan.expected_draws==expected&&plan.enabled[eye]==mask&&n.role==role&&n.pass_index==p&&bloom_plan_matches(plan,sequence.cursor(),n),"copied optional plan and actual current pass agree");
                    if(!records){BloomEpisodeSet wrong_order{1u-first};check(!wrong_order.accept(plan,n),"ordinary and auxiliary first-eye order cannot be interchanged");}
                    check(sequence.accept(plan,n),"actual copied provider values advance exact reducer");++records;
                    if(eye==first&&role==8){BloomDrawSnapshot tail{};BloomEpisodePlan tail_plan{};BloomReadStatus tail_why{};
                        check(!read_bloom_episode_shader(m.base,l,sequence.cursor(),&tail,&tail_plan,&tail_why)&&tail_why==BloomReadStatus::unrelated,
                            "closed first-eye tail is ignored before the correct second eye in both native orders");}
                    auto changed=plan;changed.view_count=views==2?3u:2u;
                    auto wrong_count=sequence;check(!wrong_count.accept(changed,n),"view-count mutation cannot rebind an active episode");
                }
            }
        }
        check(records==expected&&sequence.complete(),"all9base and optional exact multipass episode pair complete within32 records");
    }
    {auto regular=fill(m,0,2);episode_setup(m,regular,0,0,0,0);AuxiliaryPostfxSnapshot copied{};
        check(read_stereo_postfx(m.base,regular,0,&copied)&&!read_auxiliary_postfx(m.base,regular,0,&copied),
            "ordinary bloom admission does not broaden the historical auxiliary history reader");
        for(unsigned mutation=0;mutation<5;++mutation){auto bad=regular;
            if(mutation==0)bad.slots[1].extra=1;if(mutation==1)++bad.slots[1].address;
            if(mutation==2)++bad.slots[0].display.x;if(mutation==3)bad.slots[2]=bad.slots[0];if(mutation==4)bad.selected=0;
            check(!read_stereo_postfx(m.base,bad,0,&copied)&&copied==AuxiliaryPostfxSnapshot{},
                "ordinary postfx rejects changed slot identity, extra flags, geometry, unused third slot or entry index");}
    }
    auto l=fill(m,0);
    for(unsigned role:bloom_role_order){if(role>=9)continue;episode_setup(m,l,1,role,0,0);BloomDrawSnapshot n{};BloomEpisodePlan p{};BloomEpisodeCursor c{1,role,0,0};
        check(read_bloom_episode_shader(m.base,l,c,&n,&p),"current copied role read for negative matrix");
        for(unsigned bad=0;bad<10;++bad){episode_setup(m,l,1,role,0,0);const unsigned key=role==11?9:role;const auto b=m.base+0x30000+key*0x2000;
            const auto target=m.base+0x41eb1e8+20*bloom_role_target(role,0);
            if(bad==0)m.write(target+4,0u);if(bad==1)m.write(target+12,std::uint16_t{4097});
            if(bad==2)m.write(l.slots[0].address+0x3784,0x7fc00000u);if(bad==3)m.write(b+0x506,std::uint16_t{5});
            if(bad==4)m.write(m.base+bloom_material_rvas[4],static_cast<std::uint32_t>(m.base+0x30000));
            if(bad==5)m.write(b+0xa00+8+4,37u);if(bad==6)m.write(b+0xa00+8,std::uint16_t{2});
            if(bad==7)m.write(m.base+0x417eed4,1u);if(bad==8)m.write(m.base+0x417eecc,0u);
            if(bad==9){const unsigned code=role==8?10:34;const auto input=bloom_code_target(role,0,code);m.write(m.base+0x41eb1e8+20*input,0u);}
            check(!read_bloom_episode_shader(m.base,l,c,&n,&p)&&n==BloomDrawSnapshot{}&&p==BloomEpisodePlan{},"bad color/dimensions/float/pass/alias/unhandled sampler or native input table rejects without copied authority");
        }
    }
    for(unsigned role:bloom_role_order){episode_setup(m,l,1,role,0,3);const auto target=m.base+0x41eb1e8+20*bloom_role_target(role,3);m.write(target,0u);BloomDrawSnapshot n{};BloomEpisodePlan p{};
        check(read_bloom_episode_shader(m.base,l,{1,role,0,0},&n,&p)==(role==3||role==8),"surface-only exception limited to exact mixed and final scene outputs");}
    episode_setup(m,l,1,8,0,0);BloomDrawSnapshot n{};BloomEpisodePlan p{};BloomReadStatus why{};
    check(!read_bloom_episode_shader(m.base,l,{0,0,0,0},&n,&p,&why)&&why==BloomReadStatus::unrelated,"only closed-right final-color reuse lies outside episode");
    check(!read_bloom_episode_shader(m.base,l,{1,0,0,0},&n,&p,&why)&&why!=BloomReadStatus::unrelated,"same candidate in open wrong stage remains a poisoning failure");
    episode_setup(m,l,1,0,0,0);const auto page=(m.base+0x30000+0x2000*7)&~std::uintptr_t(4095);DWORD old{};VirtualProtect(reinterpret_cast<void*>(page),4096,PAGE_NOACCESS,&old);
    check(!read_bloom_episode_shader(m.base,l,{1,0,0,0},&n,&p)&&n==BloomDrawSnapshot{}&&p==BloomEpisodePlan{},"future enabled material plan protected fault rejects before getter authority");DWORD ignored{};VirtualProtect(reinterpret_cast<void*>(page),4096,old,&ignored);
    episode_setup(m,l,1,0,0,3,4);const auto tech=m.base+0x30000+0x2000+0x500;
    std::array<std::uint32_t,5> extra{};std::memcpy(extra.data(),reinterpret_cast<void*>(tech+8),20);m.write(tech+28,extra);m.write(tech+6,std::uint16_t{2});
    BloomEpisodeRejection rejected{};
    check(!read_bloom_episode_shader(m.base,l,{1,0,0,0},&n,&p,&why,&rejected)&&rejected.reason==BloomEpisodeReject::planned_record_cap&&rejected.planned_draws==34&&n==BloomDrawSnapshot{}&&p==BloomEpisodePlan{},"actual 34-pass plan explicitly rejects planned_record_cap without truncation");
    for(unsigned kind=0;kind<3;++kind){episode_setup(m,l,1,0,0,0);const BloomArgument a=kind==0?BloomArgument{8,2,34}:kind==1?BloomArgument{2,2,34}:BloomArgument{4,2,37};m.write(m.base+0x30000+0xa08,a);
        rejected={};check(!read_bloom_episode_shader(m.base,l,{1,0,0,0},&n,&p,&why,&rejected)&&rejected.reason!=BloomEpisodeReject::none&&rejected.argument_index==1&&std::memcmp(&rejected.argument,&a,sizeof a)==0&&rejected.eye==1&&rejected.role==0&&rejected.native_frame==l.frame,"unhandled source/code/shape retains exact copied argument rejection without accepted draw authority");}
    episode_setup(m,l,1,0,0,0);m.write(m.base+0x30000+0x508+15,std::uint8_t{8});rejected={};
    check(!read_bloom_episode_shader(m.base,l,{1,0,0,0},&n,&p,&why,&rejected)&&rejected.reason==BloomEpisodeReject::custom_samplers&&rejected.argument.value==8&&rejected.argument_index==UINT32_MAX,"unmapped custom sampler flags cannot silently complete the normal argument input contract");
    _mm_setcsr(saved);raw_recipe_tests(m);factory_tests(m);query_guard_tests(m);owned_query_guard_tests(m);
    std::printf("bloom_query_guard_native_checks=%u actual_protected_factory=1 native_span_reads_in_guard=0\n",query_guard_checks);
    std::printf("bloom_episode_native_checks=%u native_recipe_checks=%u source_factory_checks=%u failures=%u native_gpu_calls=0\n",checks,native_recipe_checks,source_factory_checks,failures);return failures?1:0;
}
