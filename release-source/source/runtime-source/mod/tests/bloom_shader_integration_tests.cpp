// SPDX-License-Identifier: GPL-3.0-only
// Real optional slot82 wrapper + real typed collector + real stereo draw plan.
// No COM/GPU implementation other than this process's private test vtables.
#define main inherited_bloom_collector_main
#include "bloom_shader_probe_tests.cpp"
#undef main
#include "../src/loader/d3d9_observer.cpp"
namespace integration {
using namespace bovr;
struct NativeException {unsigned value;};
struct Run {Fixture fixture;void** table{};unsigned mode{},native{},copies{},before{},after{},abandoned{},reset_calls{};bool active{};std::uint32_t index{2};std::map<std::string,unsigned> native_entry_calls;};
Run* running{};
HRESULT __stdcall no_swap(Device*,UINT,IDirect3DSwapChain9** out){*out=nullptr;return E_NOINTERFACE;}
HRESULT __stdcall native_reset(Device*,D3DPRESENT_PARAMETERS*){++running->reset_calls;return S_OK;}
HRESULT __stdcall native_present(Device*,const RECT*,const RECT*,HWND,const RGNDATA*){return S_OK;}
HRESULT __stdcall native_create_swap(Device*,D3DPRESENT_PARAMETERS*,IDirect3DSwapChain9** p){*p=nullptr;return E_FAIL;}
HRESULT __stdcall native_draw(Device* d,D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertices,UINT start,UINT primitives){
    auto& r=*running;++r.native;check(d==&r.fixture.d&&type==D3DPT_TRIANGLELIST&&base==-8&&min==7&&vertices==4&&start==13&&primitives==2,"actual wrapped DIP preserves every original argument");
    check(!set_indexed_draw_callbacks({}),"original call owns callback context lease");
    r.native_entry_calls=d->calls;SetLastError(0x51515151);if(r.mode==1||r.mode==6)RaiseException(0xe032c110,0,0,nullptr);if(r.mode==2)throw NativeException{0x1289};return S_OK;
}
std::uint64_t before(void* p,IDirect3DDevice9* device,const IndexedDrawCall& c) noexcept {
    auto& r=*static_cast<Run*>(p);++r.before;
    check(r.active&&!set_indexed_draw_callbacks({}),"backend and wrapper leases overlap at getter entry");
    std::uint64_t token{};
    __try {token=r.fixture.probe.before(device,{c.caller,static_cast<unsigned>(c.type),c.base_vertex,c.min_vertex,c.num_vertices,c.start_index,c.primitive_count});if(token&&(r.mode==5||r.mode==6))RaiseException(0xe0c11000,0,0,nullptr);}
    __except(EXCEPTION_EXECUTE_HANDLER){r.fixture.probe.abandon();token=0;}
    return token;
}
void after(void* p,std::uint64_t t,HRESULT hr) noexcept {auto& r=*static_cast<Run*>(p);++r.after;check(!set_indexed_draw_callbacks({}),"getter completion retains callback lease");r.fixture.probe.after(t,hr);}
void abandon(void* p,std::uint64_t t) noexcept {auto& r=*static_cast<Run*>(p);++r.abandoned;const auto n=r.fixture.d.calls;r.fixture.probe.abandon(t);check(n==r.fixture.d.calls,"actual wrapper native fault abandonment contains no COM");}
bool before_reset(void* p,IDirect3DDevice9*) {auto& r=*static_cast<Run*>(p);r.fixture.d.current=false;return !r.active;}
void prepare(Run& r){running=&r;r.table=static_cast<void**>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));check(r.table!=nullptr,"private observed vtable allocation");
    std::memcpy(r.table,r.fixture.dt.data(),sizeof r.fixture.dt);r.fixture.d.table=r.table;r.table[13]=reinterpret_cast<void*>(native_create_swap);r.table[14]=reinterpret_cast<void*>(no_swap);r.table[16]=reinterpret_cast<void*>(native_reset);r.table[17]=reinterpret_cast<void*>(native_present);r.table[82]=reinterpret_cast<void*>(native_draw);
    set_observation_active(true);check(set_indexed_draw_callbacks({&r,before,after,abandon})&&set_graphics_callbacks({&r,nullptr,before_reset,nullptr,nullptr}),"complete diagnostic callbacks registered");
    check(observe_device(r.fixture.d.api(),false),"actual private device observer installed");r.fixture.begin();}
bool body(Run* r){bool started=false;auto& f=r->fixture;
    const bool complete=execute_stereo_draw_plan(auxiliary_draw_plan(),[&](std::uint32_t i){r->index=i;},[&]{
        const unsigned eye=r->index==2?1u:0u;
        for(unsigned role=0;role<4;++role){f.select(eye,role,0,1);SetLastError(0x45454545);const auto hr=f.d.api()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,-8,7,4,13,2);check(hr==S_OK&&GetLastError()==0x51515151,"actual collector leaves native HRESULT and LastError intact");}
    },[&](unsigned){++r->copies;return true;},&started);
    f.probe.finish(complete);return complete&&started;
}
bool backend(Run* r){r->active=true;bool result=false;
    __try {result=body(r);}
    __finally {if(AbnormalTermination())r->fixture.probe.abandon();else r->fixture.probe.retire();r->active=false;}
    return result;
}
bool seh_backend(Run* r){__try {backend(r);}__except(GetExceptionCode()==0xe032c110?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;}
void cleanup(Run& r){r.fixture.probe.retire();check(set_indexed_draw_callbacks({})&&set_graphics_callbacks({}),"normal owner completion releases both callback tuples");
    check(r.fixture.d.refs==1&&r.fixture.d.vs.refs==1&&r.fixture.d.ps.refs==1&&r.fixture.d.surface.refs==1&&r.fixture.d.texture.refs==1,"actual observed device wrapper returns every leased reference");
    // Registry is process-lifetime; retain the test vtable until process exit.
}
}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);std::vector<std::unique_ptr<integration::Run>> runs;
    for(unsigned mode=0;mode<8;++mode){runs.push_back(std::make_unique<integration::Run>());auto& r=*runs.back();r.mode=mode;if(mode==7)r.fixture.surface_scene=true;integration::prepare(r);
        if(mode==4)r.fixture.d.source_seh_after_vertex=true;
        if(mode==3){r.fixture.d.hook_name="function_size";r.fixture.d.hook=[&]{D3DPRESENT_PARAMETERS p{};check(r.fixture.d.api()->Reset(&p)==D3DERR_DEVICELOST,"actual Reset wrapper defers while backend owns getters");};}
        if(mode==0||mode==7){check(integration::backend(&r)&&r.native==8&&r.copies==2&&r.before==8&&r.after==8&&!r.abandoned,"two-eye four-role native sequence and completed collector pair");check(r.fixture.manifest().find("\"complete\":true")!=std::string::npos,"same real wrapper/getter sequence writes complete manifest");
            if(mode==7)check(!r.fixture.d.snapshot.native_target.image&&r.fixture.d.snapshot.target==3&&r.fixture.d.snapshot.width==1280&&r.fixture.d.snapshot.height==720,"actual optional DIP wrapper completes eight synthetic draws including surface-only target3 in both eyes");}
        else if(mode==1||mode==2||mode==6){bool caught=false;const auto releases=r.fixture.d.calls["resource_release"];
            if(mode==1||mode==6)caught=integration::seh_backend(&r);else try{integration::backend(&r);}catch(const integration::NativeException& e){caught=e.value==0x1289;}
            check(caught&&r.native==1&&!r.copies&&r.abandoned==(mode==6?0u:1u)&&!r.active,"actual native SEH/C++ escapes with one call and no copy/fallback");
            check(r.fixture.d.vs.refs>1&&r.fixture.d.calls["resource_release"]>=releases&&r.fixture.d.calls==r.native_entry_calls,"native fault retains references and performs exactly zero COM throughout unwind");
            check(r.fixture.manifest().empty(),"native fault writes no completed or incomplete file during unwind");}
        else if(mode==3){check(integration::backend(&r)&&r.native==8&&r.copies==2&&!r.reset_calls,"getter reentrant Reset rejection remains diagnostic, native scene still executes once per draw");check(r.fixture.manifest().find("\"complete\":false")!=std::string::npos,"interrupted getter cannot become an accepted shader pair");}
        else {check(integration::backend(&r)&&r.native==8&&r.copies==2&&!r.after&&!r.abandoned,"provider/entry callback SEH returns token0 while native renders unchanged");
            check(r.fixture.d.vs.refs==1&&r.fixture.d.ps.refs==1,"normal owner completion retires all partial/pre-acquired callback references");
            check(mode==4?r.fixture.manifest().empty():r.fixture.manifest().find("\"complete\":false")!=std::string::npos,"token0 fault never becomes complete evidence");}
        integration::cleanup(r);
    }
    integration::running=nullptr;
    std::printf("bloom_shader_integration_checks=%u failures=%u real_gpu_calls=0\n",checks,failures);return failures?1:0;
}
