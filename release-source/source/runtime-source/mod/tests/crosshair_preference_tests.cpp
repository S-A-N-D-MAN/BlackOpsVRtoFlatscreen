// Real VR input callback and native-setting reader over owned CPU storage.
// Dvar layout is pinned by capture_display_state_v2.py and the R40 live copy:
// native hash +8, flags +C, type +10, current +18, latched +28.
#define main legacy_crosshair_input_tests_main
#include "native_command_aim_adapter_tests.cpp"
#undef main

namespace {
struct CrosshairFixture;
CrosshairFixture* setting_fixture{};
bool setting_applies=true,setting_reenters=false,setting_throws=false;
void __cdecl setting_sink(int local_client,int controller,const char* text);
struct CrosshairFixture:Fixture {
    unsigned char* value{};
    CrosshairFixture():Fixture(){
        value=image+0x9000;
        store(aim.base+0x2b67bc4,reinterpret_cast<std::uintptr_t>(value));
        store(reinterpret_cast<std::uintptr_t>(value),reinterpret_cast<std::uintptr_t>(value+0x80));
        memcpy(value+0x80,"cg_drawCrosshair",16);
        store(reinterpret_cast<std::uintptr_t>(value+8),std::uint32_t{0xa13579bd});
        store(reinterpret_cast<std::uintptr_t>(value+0xc),std::uint32_t{0x1001});
        value[0x18]=value[0x28]=1;
        controls.crosshair_enabled=controls.cod4_controls=true;
        controls.execute=setting_sink;
        setting_fixture=this;setting_applies=true;setting_reenters=setting_throws=false;
    }
    ~CrosshairFixture(){setting_fixture=nullptr;on_rig_read=nullptr;}
    void callback(){t5::before_native(&controls,context());}
    void service(){controls.gameplay=true;controls.override_active=false;controls.owner=GetCurrentThreadId();
        t5::service_crosshair_preference(controls,context());}
};
void __cdecl setting_sink(int local_client,int controller,const char* text){
    check(local_client==0&&controller==0&&!strcmp(text,"cg_drawCrosshair 0"),
        "only exact native named setting command with ordinary local arguments");
    dispatches.emplace_back(text);
    if(setting_reenters){setting_reenters=false;setting_fixture->service();}
    if(setting_throws)RaiseException(0xe0421974,0,0,nullptr);
    if(setting_applies)setting_fixture->value[0x18]=setting_fixture->value[0x28]=0;
}
void active_flow(){
    aim_now=1000;CrosshairFixture f;
    check(t5::pre_snapshot.gameplay_active,"native fixture is actually ordinary live gameplay");
    t5::CrosshairDvar copied{};
    check(t5::copy_crosshair_dvar(f.aim.base,&copied)&&copied.flags==0x1001&&copied.current==1,
        "captured native +C flags accepted independently of different +8 hash");
    std::array<unsigned char,0x90> original{};memcpy(original.data(),f.value,original.size());
    f.callback();
    check(dispatches.size()==1&&f.value[0x18]==0&&f.value[0x28]==0&&!f.controls.crosshair_busy,
        "actual callback dispatches true preference once and native sink sets false");
    original[0x18]=original[0x28]=0;
    check(!memcmp(original.data(),f.value,original.size()),"all other native dvar bytes unchanged");
    f.value[0x18]=f.value[0x28]=1;f.callback();
    check(dispatches.size()==1&&f.value[0x18]==1,"same native occurrence cannot redispatch after reset");
    f.next();setting_reenters=true;f.callback();
    check(dispatches.size()==2&&!f.controls.crosshair_busy&&f.value[0x18]==0,
        "fresh occurrence reapplies script reset and nested setting service cannot duplicate");
    f.next();f.callback();
    check(dispatches.size()==2,"already false preference performs no native command");
    f.next();f.value[0x18]=1;setting_applies=false;f.callback();
    check(dispatches.size()==3&&f.value[0x18]==1&&!f.controls.crosshair_busy,
        "native refusal does not cause direct memory write or immediate retry");
    f.next();setting_applies=true;f.callback();
    check(dispatches.size()==3&&f.value[0x18]==1,"refused setter remains latched across later commands without retry loop");
    f.next();f.value[0x18]=f.value[0x28]=0;f.callback();
    check(dispatches.size()==3,"observed native zero clears refusal latch without dispatch");
    f.next();f.value[0x18]=f.value[0x28]=1;f.callback();
    check(dispatches.size()==4&&f.value[0x18]==0,"later native reset after observed zero reapplies preference");
}
void invalid_storage(){
    for(unsigned kind=0;kind<6;++kind){
        aim_now=1000;CrosshairFixture f;
        if(kind==0)f.value[0x80]='x';
        if(kind==1)store(reinterpret_cast<std::uintptr_t>(f.value+0x10),std::uint32_t{5});
        if(kind==2){store(reinterpret_cast<std::uintptr_t>(f.value+8),std::uint32_t{0x1001});
            store(reinterpret_cast<std::uintptr_t>(f.value+0xc),std::uint32_t{});}
        if(kind==3)f.value[0x18]=2;
        if(kind==4)store(f.aim.base+0x2b67bc4,std::uintptr_t{});
        if(kind==5)store(f.aim.base+0x2b67bc4,std::uintptr_t{1});
        t5::CrosshairDvar copied{};
        check(!t5::copy_crosshair_dvar(f.aim.base,&copied),"wrong name/type/flags/bool/null/unreadable native storage rejected");
        f.callback();check(dispatches.empty(),"invalid copied storage never invokes native setter");
    }
}
void inactive_contexts(){
    for(unsigned kind=0;kind<9;++kind){
        aim_now=1000;CrosshairFixture f;auto c=f.context();
        if(kind==0)f.controls.crosshair_enabled=false;
        if(kind==1)rig_live=false;
        if(kind==2){c.key_catcher=0x10;t5::pre_snapshot.context=c;}
        if(kind==3){c.native_override=true;t5::pre_snapshot.context=c;}
        if(kind==4){c.connection_state=8;t5::pre_snapshot.context=c;}
        if(kind==5){c.native_thread+=1;t5::pre_snapshot.context=c;}
        if(kind==6)t5::pre_snapshot.native_pm_flags|=0x800;
        if(kind==7)t5::pre_snapshot.gameplay_active=false;
        if(kind==8)aim_now=1101;
        t5::before_native(&f.controls,c);
        check(dispatches.empty()&&f.value[0x18]==1,"disabled/inactive/menu/override/disconnected/foreign/frozen/expired callback refuses setting");
    }
}
DWORD invoke_setting_fault(CrosshairFixture* f){
    __try{f->service();}__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
    return 0;
}
void exceptional_dispatch(){
    aim_now=1000;CrosshairFixture f;setting_throws=true;
    check(invoke_setting_fault(&f)==0xe0421974&&!f.controls.crosshair_busy&&dispatches.size()==1,
        "native SEH preserves propagation while finally clears busy state");
    setting_throws=false;f.service();
    check(dispatches.size()==1,"exceptional occurrence remains consumed exactly once");
    f.next();f.service();check(dispatches.size()==1,"exceptional setter does not become a per-command retry loop");
    f.next();f.value[0x18]=0;f.service();
    f.next();f.value[0x18]=1;f.service();
    check(dispatches.size()==2&&f.value[0x18]==0,"observed native zero and later reset recover after exceptional delivery");
}
}
int main(){
    active_flow();invalid_storage();inactive_contexts();exceptional_dispatch();
    printf("crosshair preference: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
