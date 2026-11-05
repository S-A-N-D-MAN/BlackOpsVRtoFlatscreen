// SPDX-License-Identifier: GPL-3.0-only
#include "t5/guided_missile_context.hpp"
#include "input/action_router.hpp"
#include <map>
#include <cstdio>
#include <cstdlib>
namespace gm=bovr::t5::guided_missile;
using U32=std::uint32_t;
unsigned checks{};
void check(bool ok,const char* label){++checks;if(!ok){std::printf("FAIL %s\n",label);std::exit(1);}}
struct Memory {
    static constexpr U32 cg=0x20000000,ps=cg+0x8a364,server=0x1c08b40,player=0x1a796f8;
    static constexpr U32 missile=player+412*0x34c,variant=0x21000000,definition=0x22000000;
    std::map<std::uintptr_t,unsigned char> data;
    bovr::t5::PreCommandSnapshot pre{};
    DWORD thread=42;unsigned reads{},fail_at{},mutate_at{},pre_reads{};bool change_pre{};
    template<class T>void put(std::uintptr_t at,T value){const auto* p=reinterpret_cast<const unsigned char*>(&value);for(unsigned i=0;i<sizeof(T);++i)data[at+i]=p[i];}
    void player_fields(U32 at){
        put<int>(at+4,0);put<U32>(at+0xc,0x40);put<U32>(at+0x10,0x2008200);put<U32>(at+0xe0,2);put<U32>(at+0xe4,0x10040000);
        put<std::uint8_t>(at+0x130,0);put<std::uint8_t>(at+0x144,27);put<int>(at+0x1c4,100);put<U32>(at+0x440,0);
        put<std::int16_t>(at+0x444,412);put<int>(at+0x448,0);put<int>(at+0x44c,0);
    }
    Memory(){
        pre.context.valid=true;pre.context.native_thread=42;pre.context.command_number=17;pre.context.native_frame_time=9000;pre.context.connection_state=10;
        pre.sequence=25;pre.observed_ms=1000;pre.gameplay_epoch=9;pre.native_cg_identity=cg;pre.native_game_time=2673762;pre.native_health=100;
        pre.native_pm_flags=0x40;pre.native_weapon_flags=0x2008200;pre.native_entity_flags=2;pre.native_entity_flags2=0x10040000;pre.native_gameplay_fields_valid=true;
        put<U32>(0x2ff5354,cg);put<U32>(0x1c03140,server);put<U32>(player+0x13c,server);put<int>(player,0);
        player_fields(ps);player_fields(server);put<int>(missile,412);put<std::uint16_t>(missile+0xbe,4);put<std::uint8_t>(missile+0xdd,1);
        put<std::uint16_t>(missile+0x134,1);put<std::uint8_t>(missile+0xd4,27);put<int>(missile+0x340,2673100);
        put<U32>(0xbe19a8+4*27,variant);put<U32>(variant+8,definition);put<int>(definition+0x668,6);
        put<int>(cg+0x8a348,pre.native_game_time);put<int>(0x2993850,17);put<int>(0x2481764,9000);
        put<int>(0x2910160,0);put<int>(0x2910164,10);put<int>(0x2911e2c,0);
    }
    static constexpr U32 tow_name=0x23000000;
    void emplaced_tow(int pm=1){
        for(auto at:{ps,server}){put<int>(at+4,pm);put<std::uint8_t>(at+0x144,48);}
        pre.native_pm_type=pm;
        put<std::uint8_t>(missile+0xd4,3);put<U32>(0xbe19a8+4*3,variant);
        put<U32>(variant,tow_name);put<U32>(variant+0x8c,0x24000000);
        put<int>(definition+0x1c,2);put<int>(definition+0x20,7);
        constexpr char name[]="m220_tow_emplaced_khesanh_sp";static_assert(sizeof name==29);
        for(unsigned i=0;i<sizeof name;++i)put<char>(tow_name+i,name[i]);
    }
    static bool copy(void* opaque,std::uintptr_t at,void* out,std::size_t size) noexcept {
        auto& m=*static_cast<Memory*>(opaque);if(++m.reads==m.fail_at)return false;
        auto* p=static_cast<unsigned char*>(out);
        for(std::size_t i=0;i<size;++i){auto it=m.data.find(at+i);if(it==m.data.end())return false;p[i]=it->second;}
        if(m.reads==m.mutate_at)m.put<std::uint16_t>(missile+0x134,2);
        return true;
    }
    static bool snapshot(void* opaque,bovr::t5::PreCommandSnapshot* out) noexcept {
        auto& m=*static_cast<Memory*>(opaque);*out=m.pre;if(++m.pre_reads>1&&m.change_pre)++out->sequence;return true;
    }
    static DWORD owner(void* opaque) noexcept{return static_cast<Memory*>(opaque)->thread;}
    bool read(gm::Facts* out,std::uint64_t now=1000){reads=pre_reads=0;return gm::copy_current({this,copy,snapshot,owner},0x400000,pre,pre.context,now,out);}
};
int main(){
    gm::Facts baseline{};Memory m;check(m.read(&baseline),"recorded local TV flight graph accepted");
    const auto total=m.reads;
    check(baseline.number==412&&baseline.owner==1&&baseline.weapon==27,"captured missile identity retained");
    for(unsigned i=1;i<=total;++i){Memory x;x.fail_at=i;gm::Facts f{};check(!x.read(&f),"every unreadable field rejects");}
    for(unsigned i=1;i<=total/2;++i){Memory x;x.mutate_at=i;gm::Facts f{};check(!x.read(&f),"owner change between graph reads rejects");}
    for(auto at:{Memory::ps,Memory::server}){
        Memory x;x.put<U32>(at+0xe4,0);gm::Facts f{};check(!x.read(&f),"ordinary player is not a guided missile");
        x=Memory{};x.put<std::int16_t>(at+0x444,1023);check(!x.read(&f),"no missile sentinel rejected");
        x=Memory{};x.put<int>(at+0x1c4,0);check(!x.read(&f),"dead player rejected");
    }
    {Memory x;gm::Facts f{};x.put<std::uint16_t>(Memory::missile+0xbe,6);check(!x.read(&f),"non missile entity rejected");}
    {Memory x;gm::Facts f{};x.put<std::uint8_t>(Memory::missile+0xdd,0);check(!x.read(&f),"freed missile rejected");}
    {Memory x;gm::Facts f{};x.put<int>(Memory::definition+0x668,3);check(!x.read(&f),"other guidance kind rejected");}
    {Memory x;gm::Facts f{};x.put<U32>(Memory::player+0x13c,Memory::server+4);check(!x.read(&f),"foreign owner client rejected");}
    {Memory x;gm::Facts f{};x.pre.context.key_catcher=16;check(!x.read(&f),"paused command rejected");}
    {Memory x;gm::Facts f{};x.thread=43;check(!x.read(&f),"foreign command thread rejected");}
    {Memory x;gm::Facts f{};x.change_pre=true;check(!x.read(&f),"changed publication rejected");}
    {Memory x;gm::Facts f{};check(!x.read(&f,1101),"stale command rejected");}
    {auto f=baseline;++f.launch_time;check(!gm::same_flight(baseline,f),"reused entity starts a new flight");}
    for(int pm:{0,1}){
        Memory x;x.emplaced_tow(pm);gm::Facts f{};check(x.read(&f),"exact owned TV TOW can retain a different held knife and linked truck movement");
        check(f.emplaced_tow&&f.weapon==3&&f.predicted.weapon==48,"TOW exception keeps missile and held weapon identities distinct");
        const auto reads=x.reads;
        for(unsigned i=1;i<=reads;++i){Memory bad;bad.emplaced_tow(pm);bad.fail_at=i;gm::Facts rejected{};
            check(!bad.read(&rejected),"every linked TOW ownership or descriptor getter must remain readable");}
        for(auto at:{Memory::ps,Memory::server}){Memory bad;bad.emplaced_tow(pm);bad.put<int>(at+4,2);gm::Facts rejected{};
            check(!bad.read(&rejected),"TOW exception never grants flight to other movement modes");}
    }
    for(unsigned fault=0;fault<10;++fault){Memory x;x.emplaced_tow();gm::Facts f{};
        switch(fault){
        case 0:x.put<char>(Memory::tow_name,'x');break;
        case 1:x.put<char>(Memory::tow_name+28,'x');break;
        case 2:x.put<U32>(Memory::variant+0x8c,0);break;
        case 3:x.put<int>(Memory::definition+0x1c,0);break;
        case 4:x.put<int>(Memory::definition+0x20,6);break;
        case 5:x.put<int>(Memory::definition+0x668,5);break;
        case 6:x.put<std::uint16_t>(Memory::missile+0x134,2);break;
        case 7:x.put<U32>(Memory::ps+0x440,1);break;
        case 8:x.put<U32>(Memory::ps+0xe0,0x300);break;
        case 9:x.put<std::uint8_t>(Memory::server+0x144,47);break;
        }
        check(!x.read(&f),"non TOW changed descriptor foreign owner active turret or disagreed held weapon rejects linked exception");
    }
    {Memory x;x.put<std::uint8_t>(Memory::ps+0x144,48);x.put<std::uint8_t>(Memory::server+0x144,48);gm::Facts f{};
        check(!x.read(&f),"ordinary TV missile still cannot mismatch held weapon without exact TOW descriptor");}
    using namespace bovr::input;wawvr::xr::ActionSnapshot s{};s.sequence=1;s.focused=true;
    for(auto& h:s.hands){h.grip.active=h.grip.position_valid=h.grip.orientation_valid=h.grip.position_tracked=h.grip.orientation_tracked=true;h.trigger.active=h.stick.active=true;}
    ActionRouter router(false,true);std::uint64_t tick=2000;
    const auto update=[&](Context c){++s.sequence;return router.update(s,c,++tick,true,true);};
    const auto fire=[](const Output& o,bool pressed){for(unsigned i=0;i<o.edge_count;++i)if(o.edges[i].action==Action::fire&&o.edges[i].pressed==pressed)return true;return false;};
    update(Context::gameplay);s.hands[1].trigger.current=1;check(fire(update(Context::gameplay),true),"launch trigger pressed");
    auto o=update(Context::guided_missile);check(fire(o,false)&&!fire(o,true),"flight entry releases launch trigger without detonating");
    check(!fire(update(Context::guided_missile),true),"held launch trigger stays disarmed");
    s.hands[1].trigger.current=0;update(Context::guided_missile);s.hands[1].trigger.current=1;
    check(fire(update(Context::guided_missile),true),"deliberate second trigger retains native detonation");
    s.hands[0].stick.current={1,1};s.hands[1].stick.current={1,1};o=update(Context::guided_missile);
    check(!o.forward&&!o.right&&!o.turn&&!o.weapon_next&&!o.weapon_alternate&&!o.lower_stance,"flight cannot walk turn jump or cycle");
    s.focused=false;o=update(Context::guided_missile);check(fire(o,false),"focus loss releases detonation key");
    std::printf("%u guided context and transition checks passed\n",checks);
}
