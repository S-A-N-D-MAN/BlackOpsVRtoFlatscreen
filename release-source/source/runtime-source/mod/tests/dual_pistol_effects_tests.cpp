// SPDX-License-Identifier: GPL-3.0-only
#include "t5/dual_pistol_effects.hpp"
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <cstring>
#include <array>
namespace {
unsigned checks{},failures{};
void check(bool value,const char* name){++checks;if(!value){++failures;printf("FAIL %s\n",name);}}
}
int main(){
    using namespace bovr::t5::dual_pistol_effects;
    constexpr std::uintptr_t entry=0x1000,frame=entry+20;
    CallLayout c{entry,0x799c3a,frame+0x80,frame+0x4c,frame+0x58,frame+0x64};
    check(regular_local_layout(c,entry,entry+0x190),"exact normal local branch fits bounded active stack");
    check(!regular_local_layout(c,entry,entry+0x18f),"truncated native frame rejected");
    check(!regular_local_layout(c,entry+1,entry+0x190),"frame below stack rejected");
    check(!regular_local_layout(c,entry+0x190,entry),"reversed stack range rejected");
    const auto original_layout=c;
    c.angles=0x100000+0x8a4e4;
    check(!regular_local_layout(c,entry,entry+0x190),"shared zombietron AngleVectors call remains native");
    c=original_layout;c.return_address=0x799d62;
    check(!regular_local_layout(c,entry,entry+0x190),"third person AngleVectors call remains native");
    c=original_layout;c.right=c.up;
    check(!regular_local_layout(c,entry,entry+0x190),"aliased output arrays rejected");
    c=original_layout;c.entry=UINTPTR_MAX-4;
    check(!regular_local_layout(c,0,UINTPTR_MAX),"frame arithmetic cannot wrap");
    for(unsigned event=0;event<256;++event){
        check(event_weapon(event,2,2,3)==(event==29||event==30),"right effects accept primary right-fire events only");
        check(event_weapon(event,3,2,3)==(event==31||event==32),"left effects accept partner left-fire events only");
    }
    for(unsigned event:{29u,30u,31u,32u}){
        check(!event_weapon(event,2,2,2),"same weapon cannot impersonate a validated partner");
        check(!event_weapon(event,2,0,3),"unarmed rejected");
        check(!event_weapon(event,2,2,256),"out of range partner rejected");
    }
    for(unsigned event=0;event<256;++event){
        check(ordinary_event(event,5,5)==(event==29||event==30),"ordinary visual event requires primary right-fire or last-shot event");
        check(!ordinary_event(event,6,5),"ordinary effect cannot bind other weapon");
    }
    for(int type:{-1,0,1,2,3,4,5})for(int kind=-1;kind<=10;++kind)
        for(int fire=-1;fire<=7;++fire)for(unsigned dual:{0u,1u,2u,255u}){
            const bool expected=type==0&&kind>=0&&kind<=4&&fire>=0&&fire<=6&&fire!=5&&dual==0;
            check(ordinary_weapon(type,kind,fire,dual)==expected,
                "ordinary event correction admits only nondual bullet guns with supported nonstacked fire type");
        }
    {
        std::array<unsigned char,68> original{},scratch{};
        for(unsigned i=0;i<original.size();++i)original[i]=static_cast<unsigned char>(i*37+11);
        bovr::t5::NativeBulletGeometry stale{{1,0,0},{0,-1,0},{0,0,1},{5,6,7}},selected{};
        memcpy(original.data(),&stale,sizeof(stale));const auto saved=original;
        constexpr std::uint32_t packed=0x40000000u;
        check(command_geometry(packed,stale,&selected),"same-frame scratch fixture decodes exact ninety-degree command yaw");
        const auto selected_saved=selected;
        for(const auto mode:{OrdinaryMode::disabled,OrdinaryMode::observe,OrdinaryMode::correct})for(bool dual:{false,true}){
            scratch.fill(0xa5);const auto before=scratch;const bool expected=dual||mode==OrdinaryMode::correct;
            check(apply_command_scratch(mode,dual,selected,original.data(),scratch.data())==expected,
                "scratch correction requires explicit ordinary correct mode or existing dual policy");
            check(original==saved&&!memcmp(&selected,&selected_saved,sizeof(selected)),
                "scratch correction never mutates original native parameter block or selected geometry");
            if(expected){
                check(!memcmp(scratch.data(),&selected,36)&&memcmp(scratch.data(),original.data(),36),
                    "same-frame command yaw replaces stale native forward right up basis exactly");
                check(!memcmp(scratch.data()+36,original.data()+36,12),
                    "scratch correction preserves every native eye-origin byte36 through47");
                check(!memcmp(scratch.data()+48,original.data()+48,20),
                    "scratch correction preserves every native parameter tail byte48 through67");
                float forward[3]{};memcpy(forward,scratch.data(),sizeof(forward));
                check(std::fabs(forward[0])<1e-6f&&std::fabs(forward[1]-1)<1e-6f&&std::fabs(forward[2])<1e-6f,
                    "corrected scratch points at packed current command yaw instead of prior zero-degree native aim");
            }else check(scratch==before,"observe or disabled ordinary correction leaves all output bytes unchanged");
        }
        scratch.fill(0x5a);const auto before=scratch;
        check(!apply_command_scratch(OrdinaryMode::correct,false,selected,nullptr,scratch.data())&&scratch==before,
            "null native source refuses without partial scratch writes");
        check(!apply_command_scratch(OrdinaryMode::correct,false,selected,original.data(),nullptr)&&original==saved,
            "null scratch refuses without touching native source");
        check(!apply_command_scratch(OrdinaryMode::observe,true,selected,nullptr,scratch.data())&&scratch==before,
            "existing dual policy cannot bypass null native source guard");
    }
    for(const auto mode:{OrdinaryMode::disabled,OrdinaryMode::observe,OrdinaryMode::correct}){
        SetEnvironmentVariableW(L"BOVR_HITSCAN_EFFECTS_MODE",mode==OrdinaryMode::observe?L"observe":mode==OrdinaryMode::correct?L"correct":nullptr);
        check(ordinary_mode()==mode,"explicit ordinary mode parsed exactly");
        bovr::t5::NativeBulletGeometry original{{1,0,0},{0,-1,0},{0,0,1},{5,6,7}},selected{};
        check(command_geometry(0x40000000,original,&selected),"quantized ninety-degree yaw has valid native geometry");
        check(std::fabs(selected.forward[0])<1e-6f&&std::fabs(selected.forward[1]-1)<1e-6f&&
            !memcmp(selected.muzzle,original.muzzle,sizeof(original.muzzle)),"command geometry agrees with encoded yaw and preserves eye origin");
        auto actual=original;const bool wrote=apply_basis(mode,false,selected,actual.forward,actual.right,actual.up);
        check(wrote==(mode==OrdinaryMode::correct),"observe and disabled modes never write the native output basis");
        check(!memcmp(&actual,mode==OrdinaryMode::correct?&selected:&original,sizeof(actual)),"candidate only copies basis; observation is byte-identical");
        actual=original;check(apply_basis(mode,true,selected,actual.forward,actual.right,actual.up)&&
            !memcmp(&actual,&selected,sizeof(actual)),"ordinary diagnostic option cannot disable the existing dual correction");
    }
    SetEnvironmentVariableW(L"BOVR_HITSCAN_EFFECTS_MODE",L"invalid");
    check(ordinary_mode()==OrdinaryMode::disabled,"unknown option fails closed");
    SetEnvironmentVariableW(L"BOVR_HITSCAN_EFFECTS_MODE",nullptr);
    for(unsigned pitch=0;pitch<65536;pitch+=1023)for(unsigned yaw=0;yaw<65536;yaw+=997){
        bovr::t5::NativeBulletGeometry original{{1,0,0},{0,-1,0},{0,0,1},{1,2,3}},out{};
        check(command_geometry(pitch|(yaw<<16),original,&out),"finite packed command decodes including signed wrap");
        float ff{},rr{},uu{},fr{},fu{},ru{};
        for(unsigned i=0;i<3;++i){ff+=out.forward[i]*out.forward[i];rr+=out.right[i]*out.right[i];uu+=out.up[i]*out.up[i];
            fr+=out.forward[i]*out.right[i];fu+=out.forward[i]*out.up[i];ru+=out.right[i]*out.up[i];}
        check(std::fabs(ff-1)<1e-5f&&std::fabs(rr-1)<1e-5f&&std::fabs(uu-1)<1e-5f&&
            std::fabs(fr)<1e-5f&&std::fabs(fu)<1e-5f&&std::fabs(ru)<1e-5f,"decoded native basis remains orthonormal");
        for(unsigned i=0;i<3;++i){const auto j=(i+1)%3,k=(i+2)%3;
            check(std::fabs(out.forward[j]*out.right[k]-out.forward[k]*out.right[j]+out.up[i])<1e-5f,
                "native forward-right basis has native negative-up handedness");}
    }
    bovr::t5::NativeBulletGeometry bad{},unchanged{};bad.muzzle[0]=std::numeric_limits<float>::quiet_NaN();
    check(!command_geometry(0,bad,&unchanged)&&!command_geometry(0,unchanged,nullptr),"invalid origin or output rejected without fabrication");
    printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
