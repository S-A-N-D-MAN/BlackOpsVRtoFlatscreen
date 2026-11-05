// SPDX-License-Identifier: GPL-3.0-only
// The production native dispatcher/bridges run against authored native stubs.
// Facts use the existing reader fixture; selected angle/pointer writes target
// owned pages at retail logical addresses. No game, XR runtime or GPU is used.
#include "t5/command_probe.hpp"
#define main unused_mounted_reader_main
#define Fixture ReaderFixture
#define check reader_check
#define checks reader_checks
#define failures reader_failures
#define read_pre_command_snapshot unused_reader_pre
#include "mounted_look_context_tests.cpp"
#undef read_pre_command_snapshot
#undef failures
#undef checks
#undef check
#undef Fixture
#undef main
static std::uint64_t fixture_now=1000;
static ULONGLONG fixture_clock(){return fixture_now;}
#define GetTickCount64 fixture_clock
#include "../src/t5/mounted_gun_runtime.cpp"
#undef GetTickCount64
#include <stdexcept>
#include <condition_variable>
namespace h=bovr::t5::mounted_gun_runtime_detail;
namespace xr=wawvr::xr;
namespace {
unsigned checks{},failures{},native_calls{},active_kind{};
void check(bool okay,const char* label){++checks;if(!okay){++failures;std::printf("FAIL %s\n",label);}}
bovr::t5::PreCommandSnapshot current_pre{};
bovr::input::CommandRigSample latest_rig{};
bool current_generation=true,rig_available=true;
std::uintptr_t call_target{},server_ps{},pose_pointer{},input_esi{},input_eax{},first_arg{},second_arg{};
std::uintptr_t* expected_return_slot{};
unsigned wrong_return{};
std::function<void()> native_hook;
std::uint32_t input_regs[7]{},output_regs[7]{},entry_args[2]{},entry_flags{},exit_flags{},native_flags{},entry_error{};
std::array<float,3> seen_angles{},native_angles{3.f,7.f,0.f};
__declspec(align(16)) unsigned char caller_fx[512]{},native_fx[512]{},entry_fx[512]{},output_fx[512]{};
std::uintptr_t turret_pointer{};
void __cdecl observe_native(){
    ++native_calls;
    if(active_kind==2){const auto view=*reinterpret_cast<const std::uint32_t*>(pose_pointer+0x60);
        std::memcpy(seen_angles.data(),reinterpret_cast<const void*>(view),12);
    }else{
        std::memcpy(seen_angles.data(),reinterpret_cast<const void*>(server_ps+0x180),12);
        if(active_kind==0)std::memcpy(reinterpret_cast<void*>(turret_pointer+0x54),native_angles.data(),12);
    }
    if(native_hook)native_hook();
}
__declspec(naked) void native_stub(){__asm {
    mov dword ptr [input_regs],eax
    mov dword ptr [input_regs+4],ebx
    mov dword ptr [input_regs+8],ecx
    mov dword ptr [input_regs+12],edx
    mov dword ptr [input_regs+16],esi
    mov dword ptr [input_regs+20],edi
    mov dword ptr [input_regs+24],ebp
    pushfd
    pop dword ptr [entry_flags]
    fxsave [entry_fx]
    mov eax,[esp+4]
    mov dword ptr [entry_args],eax
    mov eax,[esp+8]
    mov dword ptr [entry_args+4],eax
    call GetLastError
    mov dword ptr [entry_error],eax
    call observe_native
    fld1
    pcmpeqd xmm0,xmm0
    fxsave [native_fx]
    mov eax,13579bdfh
    mov ecx,2468ace0h
    mov edx,11223344h
    stc
    pushfd
    pop dword ptr [native_flags]
    ret
}}
__declspec(naked) void call_bridge(){__asm {
    push ebp
    push ebx
    push esi
    push edi
    // This fixture is the authored stand-in for the three pinned retail CALLs.
    mov eax,offset fixture_return
    add eax,dword ptr [wrong_return]
    mov edx,dword ptr [expected_return_slot]
    mov dword ptr [edx],eax
    fxsave [caller_fx]
    push dword ptr [second_arg]
    push dword ptr [first_arg]
    mov eax,dword ptr [input_eax]
    mov esi,dword ptr [input_esi]
    mov ebx,01020304h
    mov ecx,88774422h
    mov edx,55667788h
    mov edi,40302010h
    mov ebp,789abcdeh
    cmp eax,eax
    stc
    call dword ptr [call_target]
fixture_return:
    lea esp,[esp+8]
    mov dword ptr [output_regs],eax
    mov dword ptr [output_regs+4],ebx
    mov dword ptr [output_regs+8],ecx
    mov dword ptr [output_regs+12],edx
    mov dword ptr [output_regs+16],esi
    mov dword ptr [output_regs+20],edi
    mov dword ptr [output_regs+24],ebp
    pushfd
    pop dword ptr [exit_flags]
    fxsave [output_fx]
    fxrstor [caller_fx]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
}}
void __cdecl fault_stub(){++native_calls;RaiseException(0xe0421994,0,0,nullptr);}
bool invoke_fault(){__try{call_bridge();}__except(GetExceptionCode()==0xe0421994?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;}
struct RuntimeFixture {
    ReaderFixture memory;
    std::vector<void*> pages;
    std::uint32_t centities{},client{},info{},pose{};
    std::array<float,3> original{11.f,22.f,33.f};
    RuntimeFixture(){
        for(const auto& region:std::array<std::pair<std::uintptr_t,std::size_t>,4>{{
            {0x1a70000,0x100000},{0xb70000,0x10000},{0x2ff0000,0x10000},{ReaderFixture::cg,0xb0000}}}){
            void* p=VirtualAlloc(reinterpret_cast<void*>(region.first),region.second,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
            check(p==reinterpret_cast<void*>(region.first),"owned retail-address fixture page");if(!p)throw std::runtime_error("fixture allocation");pages.push_back(p);
        }
        void* p=VirtualAlloc(nullptr,0x50000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);check(p!=nullptr,"owned client and centity fixture");
        if(!p)throw std::runtime_error("fixture client allocation");pages.push_back(p);
        client=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));info=client+0x1000;centities=client+0x2000;
        pose=centities+42*static_cast<std::uint32_t>(h::client_entity_stride);server_ps=client;pose_pointer=pose;
        turret_pointer=h::entities+42*h::entity_stride;
        h::put(h::entities,std::uint32_t{0});h::put(h::entities+0x13c,client);h::put(turret_pointer,std::uint32_t{42});
        h::put(turret_pointer+0x14c,info);h::put(client+0x1c4,100);h::put(client+0xe0,std::uint32_t{0x100});
        h::put(client+0x440,std::uint32_t{1});h::put(client+0x444,std::int16_t{42});h::put(client+0x180,original);
        h::put(info+0x34,h::aim::Angles{-45,-60});h::put(info+0x3c,h::aim::Angles{45,60});h::put(0xb77a58,15.f);
        h::put(h::client_entities_slot,centities);h::put(pose+2,std::uint8_t{11});h::put(pose+0x6c,std::uint8_t{1});
        h::put(pose+0x60,std::uint32_t{ReaderFixture::ps+0x180});h::put(ReaderFixture::ps+0x180,original);
        current_pre=memory.pre;latest_rig={};latest_rig.xr_generation=latest_rig.anchor_generation=3;
        latest_rig.published_ms=990;latest_rig.frame.frame_id=7;latest_rig.frame.actions.sequence=4;
        latest_rig.frame.actions.focused=true;latest_rig.frame.should_render=latest_rig.frame.views_valid=true;
        latest_rig.frame.predicted_display_time=1000000;latest_rig.frame.predicted_display_period=11111111;
        auto& a=latest_rig.frame.actions.hands[1].aim;a.active=a.orientation_tracked=a.orientation_valid=true;
        const float angle=.2f;a.pose.orientation={0,std::sin(angle/2),0,std::cos(angle/2)};
        fixture_now=1000;current_generation=rig_available=true;h::owner_host=memory.host();
        memory.put<DWORD>(static_cast<U32>(h::main_thread_slot),GetCurrentThreadId());
        memory.put<DWORD>(static_cast<U32>(h::server_thread_slot),0);wrong_return=0;native_hook={};
        h::owner_source.reset(new ml::Source(ml::make_source(true,memory.host())));
        h::enabled.store(true);bovr::t5::revoke_mounted_gun_scene();publish();
        check(h::scene.valid,"production scene publisher accepts exact local mounted scene");
        h::aim_original=h::fire_original=h::visual_original=reinterpret_cast<std::uintptr_t>(&native_stub);
    }
    ~RuntimeFixture(){h::enabled.store(false);bovr::t5::revoke_mounted_gun_scene();h::owner_source.reset();
        native_hook={};h::consumer_returns={0x81c7a2,0x819cbf,0x5c7ed1};
        h::owner_host=ml::production_host();for(void* p:pages)VirtualFree(p,0,MEM_RELEASE);}
    void publish(){bovr::t5::publish_mounted_gun_scene(latest_rig,{},xr::Basis3f{},current_pre,ReaderFixture::cg+0x8c100);}
    void prepare(unsigned kind){active_kind=kind;native_calls=0;seen_angles={};
        expected_return_slot=&h::consumer_returns[kind];
        first_arg=kind==2?0x12345678:h::entities;second_arg=0;
        input_eax=kind==0?h::entities:turret_pointer;input_esi=kind==2?pose:turret_pointer;
        call_target=reinterpret_cast<std::uintptr_t>(kind==0?&h::aim_bridge:kind==1?&h::fire_bridge:&h::visual_bridge);
    }
    void invoke(unsigned kind){prepare(kind);SetLastError(0x12345678);call_bridge();}
    void restored(){std::array<float,3> a{},b{};std::uint32_t view{};
        h::get(client+0x180,&a);h::get(ReaderFixture::ps+0x180,&b);h::get(pose+0x60,&view);
        check(a==original&&b==original&&view==ReaderFixture::ps+0x180,"server and predicted camera angles plus visible pose pointer restored");
    }
};
void positive(){RuntimeFixture f;
    for(unsigned kind=0;kind<3;++kind){f.invoke(kind);
        check(native_calls==1,"each mounted consumer calls original exactly once");
        check(input_regs[0]==input_eax&&input_regs[1]==0x01020304&&input_regs[2]==0x88774422&&
            input_regs[3]==0x55667788&&input_regs[4]==input_esi&&input_regs[5]==0x40302010&&input_regs[6]==0x789abcde,
            "all native input registers preserved");
        check((entry_flags&0x8d5)==0x45&&!std::memcmp(caller_fx,entry_fx,512),"incoming flags and complete native FP state preserved");
        check(entry_error==0x12345678&&GetLastError()==0x12345678,"selection and restoration preserve LastError");
        check(output_regs[0]==0x13579bdf&&output_regs[2]==0x2468ace0&&output_regs[3]==0x11223344&&
            output_regs[1]==input_regs[1]&&output_regs[4]==input_regs[4]&&output_regs[5]==input_regs[5]&&output_regs[6]==input_regs[6],
            "native output registers and nonvolatile registers preserved");
        check(exit_flags==native_flags&&!std::memcmp(native_fx,output_fx,512),"native output flags FP SSE and MXCSR preserved");
        if(kind)check(entry_args[0]==first_arg&&(kind!=1||entry_args[1]==0),"native one and two argument ABIs preserved");
        check(seen_angles!=f.original,"selected native consumer sees controller orientation");
        if(kind==1)check(seen_angles==native_angles,"shot uses actual native-clamped gun angles from aim call");
        f.restored();
    }
    check(h::native_aim.valid,"successful native aim records bounded clamped witness");
}
void thread_roles(){
    {RuntimeFixture f;
        std::thread worker([&]{
            f.memory.put<DWORD>(static_cast<U32>(h::server_thread_slot),GetCurrentThreadId());
            f.invoke(0);check(native_calls==1&&seen_angles!=f.original,"verified native server worker accepts exact aim caller");f.restored();
            f.invoke(1);check(native_calls==1&&seen_angles==native_angles,"verified native server worker accepts exact shot caller");f.restored();
            f.invoke(2);check(native_calls==1&&seen_angles==f.original,"visual admission remains main-thread only");f.restored();
            f.publish();check(!h::scene.valid,"server worker cannot publish a render-owned scene");
        });worker.join();
        f.publish();check(h::scene.valid,"main thread can publish after worker publication rejection");
    }
    for(unsigned kind=0;kind<3;++kind){RuntimeFixture f;f.invoke(0);
        std::thread worker([&]{f.invoke(kind);
            check(native_calls==1&&seen_angles==f.original,"unregistered worker cannot borrow valid main-thread pre-command owner");f.restored();});worker.join();
    }
    {RuntimeFixture f;const auto thread=current_pre.context.native_thread;++current_pre.context.native_thread;
        f.invoke(0);check(seen_angles==f.original,"pre-command producer must equal verified native main thread");
        current_pre.context.native_thread=thread;
    }
    for(unsigned kind=0;kind<3;++kind){RuntimeFixture f;f.invoke(0);f.prepare(kind);wrong_return=1;call_bridge();
        check(native_calls==1&&seen_angles==f.original,"unexpected return site cannot select any native consumer");f.restored();
    }
}
void concurrent_publication(){
    {RuntimeFixture f;std::mutex mutex;std::condition_variable ready;bool in_native=false,published=false;
        native_hook=[&]{std::unique_lock lock(mutex);in_native=true;ready.notify_one();ready.wait(lock,[&]{return published;});};
        std::thread worker([&]{
            f.memory.put<DWORD>(static_cast<U32>(h::server_thread_slot),GetCurrentThreadId());f.invoke(0);
        });
        {std::unique_lock lock(mutex);ready.wait(lock,[&]{return in_native;});}
        const auto original_serial=h::scene.serial;++latest_rig.frame.frame_id;++latest_rig.frame.actions.sequence;
        latest_rig.published_ms=1000;f.publish();
        {std::lock_guard lock(mutex);published=true;}ready.notify_one();worker.join();native_hook={};
        check(h::scene.serial>original_serial&&h::native_aim.valid&&h::native_aim.scene_serial==original_serial,
            "main-thread publication during server native aim retains original clamped witness");f.restored();
        f.invoke(1);check(native_calls==1&&seen_angles==native_angles,"new same-mount publication before fire uses original native-clamped result");f.restored();
    }
    {RuntimeFixture f;f.invoke(0);fixture_now=1091;current_pre.observed_ms=1091;latest_rig.published_ms=1091;
        ++latest_rig.frame.frame_id;f.publish();f.invoke(1);
        check(h::scene.valid&&seen_angles==f.original,"fresh publication cannot refresh an expired original aim rig");f.restored();
    }
    {RuntimeFixture f;f.invoke(0);++latest_rig.anchor_generation;f.publish();f.invoke(1);
        check(seen_angles==f.original,"new anchor identity cannot borrow old aim witness");f.restored();
    }
}
void refusals(){
    for(unsigned fault=0;fault<14;++fault){RuntimeFixture f;f.invoke(0);
        switch(fault){
        case 0:current_generation=false;break;
        case 1:latest_rig.frame.actions.hands[1].aim.orientation_tracked=false;break;
        case 2:latest_rig.frame.actions.focused=false;break;
        case 3:fixture_now=1101;break;
        case 4:current_pre.context.key_catcher=16;break;
        case 5:++current_pre.gameplay_epoch;break;
        case 6:f.memory.put<std::int16_t>(ReaderFixture::ps+0x444,43);break;
        case 7:h::put(f.client+0x444,std::int16_t{43});break;
        case 8:h::put(f.pose+2,std::uint8_t{14});break;
        case 9:h::put(f.client+0x1c4,0);break;
        case 10:h::put(f.info+0x34,h::aim::Angles{30,70});break;
        case 11:h::put(f.client+0x464,std::array<float,3>{1,0,0});break;
        case 12:rig_available=false;break;
        case 13:bovr::t5::revoke_mounted_gun_scene();break;
        }
        f.invoke(1);check(native_calls==1&&seen_angles==f.original,"invalid owner tracking epoch camera or age leaves native shot unchanged");f.restored();
    }
    {RuntimeFixture f;f.invoke(1);check(seen_angles==f.original,"fire without native-clamped aim witness stays native");}
    {RuntimeFixture f;f.invoke(0);h::put(turret_pointer+0x54,std::array<float,3>{4,7,0});f.invoke(1);
        check(seen_angles==f.original,"changed native barrel angles invalidate previous shot witness");}
    {RuntimeFixture f;f.prepare(2);input_esi+=h::client_entity_stride;call_bridge();f.restored();
        check(native_calls==1&&seen_angles==f.original,"another visible turret cannot borrow this mount aim");}
    {RuntimeFixture f;bovr::t5::publish_mounted_gun_scene(latest_rig,{},xr::Basis3f{},current_pre,ReaderFixture::cg+0x8c104);
        check(!h::scene.valid,"incorrect refdef identity revokes scene publication");}
    {RuntimeFixture f;f.memory.put<int>(ReaderFixture::cg+0x8a348,10016);f.memory.put<int>(0x2993850,21);
        f.memory.put<int>(0x2481764,9016);f.publish();f.invoke(0);
        check(h::scene.valid&&seen_angles!=f.original,"later render/server seam accepts fresh exact mount after native counters advance");}
}
void unwind(){
    for(unsigned kind=0;kind<3;++kind){RuntimeFixture f;f.invoke(0);f.prepare(kind);
        h::aim_original=h::fire_original=h::visual_original=reinterpret_cast<std::uintptr_t>(&fault_stub);
        check(invoke_fault()&&native_calls==1,"native SEH propagates with exactly one native invocation");f.restored();
        check(h::depth==0,"native unwind releases scope depth");
    }
}
void scoped_tow(){
    RuntimeFixture f;f.memory.scoped_tow();current_pre=f.memory.pre;f.publish();
    check(h::scene.valid&&h::scene.owner.facts.scoped_tow.weapon==3,
        "scoped TOW scene publishes from copied turret identity while viewmodel remains hidden");
    f.invoke(0);check(native_calls==1&&seen_angles!=f.original&&h::native_aim.valid,
        "scoped TOW reaches native clamped mounted aim");f.restored();
    f.invoke(1);check(native_calls==1&&seen_angles==native_angles,
        "scoped TOW fire consumes same native barrel witness");f.restored();
    check(f.memory.get<int>(ReaderFixture::cg+0xa9d44)==1,
        "scoped TOW never alters the native hideviewmodel flag");
    f.memory.put<float>(ReaderFixture::ps+0x1a0,10.25f);f.memory.put<float>(ReaderFixture::ps+0x1a4,120.1f);
    f.publish();f.invoke(1);check(native_calls==1&&seen_angles==native_angles,
        "moving truck clamp base retains the same current TOW mount and shot witness");f.restored();
    f.memory.put<float>(ReaderFixture::ps+0x1a8,40.f);f.publish();f.invoke(1);
    check(native_calls==1&&seen_angles==f.original,"changed TOW clamp limits cannot reuse old shot witness");f.restored();
    f.invoke(0);
    f.memory.put<U8>(ReaderFixture::tow_client+0x238,4);f.invoke(1);
    check(native_calls==1&&seen_angles==f.original,
        "changed scoped turret client weapon revokes a previously published aim");f.restored();
    f.memory.put<U8>(ReaderFixture::tow_client+0x238,3);f.publish();f.invoke(0);
    f.memory.put<U32>(ReaderFixture::tow_variant+0x8c,0);f.invoke(1);
    check(native_calls==1&&seen_angles==f.original,
        "missing scoped TOW overlay cannot reuse previous aim permission");f.restored();
}
}
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
Validation validate_path(const std::wstring&){return {true,"fixture","fixture"};}
bool validate_loaded_headers(HMODULE,std::string&){return true;}
std::wstring executable_path(){return L"private fixture";}
bool read_pre_command_snapshot(PreCommandSnapshot* out) noexcept {*out=current_pre;return true;}
bool read_vr_command_input_snapshot(xr::ActionSnapshot* a,std::uint64_t* g,input::CommandRigSample* r) noexcept {
    *a=latest_rig.frame.actions;*g=latest_rig.xr_generation;*r=latest_rig;return rig_available;
}
bool command_input_generation_current(const input::CommandRigSample& r) noexcept{return current_generation&&r.xr_generation==latest_rig.xr_generation;}
}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);positive();refusals();thread_roles();concurrent_publication();unwind();scoped_tow();
    std::printf("mounted gun runtime: %u checks, %u failures\n",checks,failures);return failures?1:0;}
