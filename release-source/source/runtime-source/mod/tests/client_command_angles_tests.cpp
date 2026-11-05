// SPDX-License-Identifier: GPL-3.0-only
#include "t5/client_command_angles.hpp"
#include <cstdio>
#include <initializer_list>
#include <limits>
namespace {
namespace ca=bovr::t5::client_command_angles;
unsigned checks{},failures{};
void check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL %s\n",why);}}
ca::Words words(float pitch,float yaw){return {std::bit_cast<std::uint32_t>(pitch),std::bit_cast<std::uint32_t>(yaw)};}
ca::Facts facts(){ca::Facts f{};f.exact=f.ordinary=true;f.kind=4;f.fire_type=1;f.weapon=15;f.now=1000;f.published_ms=990;f.packed=0x40002000;
    for(unsigned i=0;i<f.command.size();++i)f.command[i]=static_cast<unsigned char>(i*37+11);
    f.command[0x18]=15;std::memcpy(f.command.data()+0x20,&f.packed,4);return f;}
struct Memory{
    std::array<std::uint32_t,6> data{0x11223344,0x55667788,0,0,0x99887766,0x55443322};
    unsigned calls{},fail{},conflict{};bool same_selected_conflict{},change_owned_on_failure{};
    static bool cas(void* p,std::uintptr_t a,std::uint32_t expected,std::uint32_t selected,std::uint32_t* old)noexcept{
        auto& m=*static_cast<Memory*>(p);++m.calls;if(a<0x1000||a>=0x1018||(a&3))return false;
        const auto i=(a-0x1000)/4;
        if(m.calls==m.fail){if(m.change_owned_on_failure)m.data[2]=std::bit_cast<std::uint32_t>(77.f);return false;}
        if(m.calls==m.conflict)m.data[i]=m.same_selected_conflict?selected:std::bit_cast<std::uint32_t>(64.f);
        *old=m.data[i];if(*old==expected)m.data[i]=selected;return true;
    }
    ca::Host host(){return {this,nullptr,cas};}
    void set(ca::Words w){data[2]=w[0];data[3]=w[1];}
    ca::Words get()const{return {data[2],data[3]};}
};
}
int main(){
    const auto f=facts();ca::Words out{0xdeadbeef,0xbaadf00d};const auto sentinel=out;
    check(ca::select(f,&out)&&out==words(45,90),"exact authored packed command selects native signed pitch/yaw");
    auto signed_f=f;signed_f.packed=0x8000c000;std::memcpy(signed_f.command.data()+0x20,&signed_f.packed,4);
    check(ca::select(signed_f,&out)&&out==words(-90,-180),"negative packed signed angles use exact native units");
    check(!ca::select(f,nullptr),"null selection output refused");
    for(int type=-1;type<=5;++type)for(int kind=-1;kind<=9;++kind)for(int fire=-1;fire<=7;++fire)for(bool dual:{false,true}){
        auto x=f;x.type=type;x.kind=kind;x.fire_type=fire;x.dual=dual;out=sentinel;
        const bool expected=type==0&&kind>=0&&kind<=4&&fire>=0&&fire<=6&&fire!=5&&!dual;
        check(ca::select(x,&out)==expected,"only ordinary nondual supported bullet classes and nonstacked fire types selected");
        if(!expected)check(out==sentinel,"rejected selection does not change output");
    }
    for(unsigned bad=0;bad<9;++bad){auto x=f;out=sentinel;
        if(bad==0)x.exact=false;if(bad==1)x.ordinary=false;if(bad==2)x.weapon=0;if(bad==3)x.weapon=256;
        if(bad==4)++x.command[0x18];if(bad==5)x.published_ms=0;if(bad==6)x.published_ms=1001;
        if(bad==7)x.published_ms=899;if(bad==8)x.command[0x20]^=1;
        const auto before=x.command;check(!ca::select(x,&out)&&out==sentinel&&x.command==before,"identity freshness and exact command failures preserve inputs and output");
    }
    {auto x=f;x.published_ms=900;check(ca::select(x,&out),"exact100ms boundary remains current");}
    const auto before=words(10,20),selected=words(45,90);
    {Memory m;m.set(before);const auto original=m.data;unsigned owned{};
        check(ca::transfer(m.host(),0x1008,before,selected,&owned)&&owned==3&&m.get()==selected,"successful compare exchanges own exactly both changed words");
        check(m.data[0]==original[0]&&m.data[1]==original[1]&&m.data[4]==original[4]&&m.data[5]==original[5],"transfer never touches neighboring fields");
        ca::restore(m.host(),0x1008,before,selected,owned);check(m.data==original,"owned unchanged fields restore exactly");}
    {Memory m;m.set(selected);unsigned owned=99;check(ca::transfer(m.host(),0x1008,selected,selected,&owned)&&owned==0,"unchanged native words do not acquire mutation ownership");}
    for(unsigned fail:{1u,2u}){Memory m;m.set(before);m.fail=fail;unsigned owned=99;
        check(!ca::transfer(m.host(),0x1008,before,selected,&owned)&&owned==0&&m.get()==before,"failed first or second exchange cancels only successful prior writes");}
    for(bool same:{false,true}){Memory m;m.set(before);m.conflict=2;m.same_selected_conflict=same;unsigned owned{};
        check(!ca::transfer(m.host(),0x1008,before,selected,&owned)&&!owned&&m.get()==words(10,same?90.f:64.f),"failed exchange never rolls back another writer including identical selected bits");}
    {Memory m;m.set(before);m.fail=2;m.change_owned_on_failure=true;unsigned owned{};
        check(!ca::transfer(m.host(),0x1008,before,selected,&owned)&&m.get()==words(77,20),"rollback preserves a native change to previously owned field");}
    for(unsigned mask=0;mask<4;++mask){Memory m;m.set(selected);ca::restore(m.host(),0x1008,before,selected,mask);
        check(m.get()==words(mask&1?10.f:45.f,mask&2?20.f:90.f),"restore obeys each ownership bit independently");}
    for(unsigned bad=0;bad<6;++bad){Memory m;m.set(before);auto a=std::uintptr_t{0x1008};auto old=before,target=selected;auto host=m.host();unsigned owned=99;
        if(bad==0)a=0;if(bad==1)++a;if(bad==2)a=UINTPTR_MAX-3;if(bad==3)old[0]=0x7fc00000;
        if(bad==4)target[1]=0x7f800000;if(bad==5)host.compare_exchange=nullptr;
        check(!ca::transfer(host,a,old,target,&owned)&&!owned&&!m.calls&&m.get()==before,"invalid address alignment finiteness or missing host fails before writes");}
    {Memory m;m.set(before);check(!ca::transfer(m.host(),0x1008,before,selected,nullptr)&&!m.calls,"null ownership output refuses before writes");}
    std::printf("checks=%u failures=%u native_calls=0\n",checks,failures);return failures?1:0;
}
