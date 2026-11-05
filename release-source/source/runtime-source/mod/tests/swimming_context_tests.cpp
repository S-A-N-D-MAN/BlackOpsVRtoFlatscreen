// SPDX-License-Identifier: GPL-3.0-only
#include "t5/swimming_context.hpp"
#include "t5/primary_weapon_tracking.hpp"
#include <map>
#include <set>
#include <vector>
#include <cstdio>
#include <cstdlib>
namespace swim=bovr::t5::swimming;
using U32=std::uint32_t;
unsigned checks{};
void check(bool ok,const char* label){++checks;if(!ok){std::printf("FAIL %s\n",label);std::exit(1);}}
struct Memory {
    static constexpr U32 cg=0x20000000,ps=cg+0x8a364,dvar=0x21000000,name=0x22000000;
    std::map<std::uintptr_t,unsigned char> data;
    bovr::t5::PreCommandSnapshot pre{};
    DWORD thread=42;unsigned reads{},fail_at{},mutate_at{},pre_reads{};
    bool change_pre{},change_thread{};
    std::uintptr_t mutation=ps+0x55c;U32 mutation_value=4;
    template<class T>void put(std::uintptr_t at,const T& value){const auto* p=reinterpret_cast<const unsigned char*>(&value);for(unsigned i=0;i<sizeof(T);++i)data[at+i]=p[i];}
    Memory(){
        pre.context.valid=true;pre.context.native_thread=42;pre.context.command_number=17;
        pre.context.native_frame_time=9000;pre.context.connection_state=10;
        pre.sequence=25;pre.observed_ms=1000;pre.gameplay_epoch=9;pre.native_cg_identity=cg;
        pre.native_game_time=2673762;pre.native_health=100;pre.native_entity_flags=2;pre.native_entity_flags2=0x8000;
        pre.native_gameplay_fields_valid=pre.gameplay_active=true;
        put<U32>(0x2ff5354,cg);put<int>(ps+4,0);put<U32>(ps+0xc,0);put<U32>(ps+0x10,0);
        put<U32>(ps+0xe0,2);put<U32>(ps+0xe4,0x8000);put<std::uint8_t>(ps+0x130,0);
        put<std::uint8_t>(ps+0x144,42);put<int>(ps+0x1c4,100);put<U32>(ps+0x440,0);
        put<std::int16_t>(ps+0x444,1023);put<int>(ps+0x448,0);put<int>(ps+0x44c,0);put<int>(ps+0x55c,5);
        put(ps+0x84,std::array<float,3>{14.f,283.f,1.148f});
        put<int>(cg+0x8a348,pre.native_game_time);put<int>(cg+0x8c158,pre.native_game_time);
        put(cg+0x8c134,std::array<float,9>{1,0,0,0,1,0,0,0,1});
        for(U32 offset:{0x8a360u,0xcu,0xa9d44u,0x1cu,0x14u,0xa4780u,0xa4680u})put<int>(cg+offset,0);
        constexpr U32 slots[]{0xbcd150,0xbcd1b8,0xbcaf04,0xbcaebc};
        constexpr const char* names[]{"player_topDownCamMode","player_viewLockEnt","player_view_pitch_up","player_view_pitch_down"};
        for(unsigned i=0;i<4;++i){const U32 at=dvar+i*0x100,text=name+i*0x100;
            put<U32>(slots[i],at);put<U32>(at,text);put<U32>(at+0x10,i<2?5u:1u);
            if(i<2)put<int>(at+0x18,0);else put<float>(at+0x18,i==2?85.f:80.f);
            for(unsigned j=0;j<std::strlen(names[i])+1;++j)put<char>(text+j,names[i][j]);
        }
        put<int>(0x2993850,17);put<int>(0x2481764,9000);put<int>(0x2910160,0);
        put<int>(0x2910164,10);put<int>(0x2911e2c,0);
    }
    static bool copy(void* opaque,std::uintptr_t at,void* out,std::size_t size) noexcept {
        auto& m=*static_cast<Memory*>(opaque);if(++m.reads==m.fail_at)return false;
        auto* p=static_cast<unsigned char*>(out);
        for(std::size_t i=0;i<size;++i){auto it=m.data.find(at+i);if(it==m.data.end())return false;p[i]=it->second;}
        if(m.reads==m.mutate_at)m.put<U32>(m.mutation,m.mutation_value);
        return true;
    }
    static bool snapshot(void* opaque,bovr::t5::PreCommandSnapshot* out) noexcept {
        auto& m=*static_cast<Memory*>(opaque);*out=m.pre;
        if(++m.pre_reads>1&&m.change_pre)++out->sequence;
        if(m.pre_reads>1&&m.change_thread)++m.thread;
        return true;
    }
    static DWORD owner(void* opaque) noexcept{return static_cast<Memory*>(opaque)->thread;}
    bool read(swim::Facts* out,std::uint64_t now=1000,std::uintptr_t base=0x400000){reads=pre_reads=0;
        return swim::copy_current({this,copy,snapshot,owner},base,pre,pre.context,now,out);}
    bool walk(swim::Facts* out,std::uint64_t now=1000){reads=pre_reads=0;
        return swim::copy_walking_current({this,copy,snapshot,owner},0x400000,pre,pre.context,now,out);}
};
Memory* native_memory{};
namespace bovr::t5 {bool read_pre_command_snapshot(PreCommandSnapshot* out) noexcept {
    return native_memory&&Memory::snapshot(native_memory,out);
}}
struct NativePages {
    std::vector<void*> pages;
    explicit NativePages(const Memory& m){
        std::set<std::uintptr_t> bases;
        for(const auto& byte:m.data)bases.insert(byte.first&~std::uintptr_t{0xffff});
        for(auto base:bases){void* p=VirtualAlloc(reinterpret_cast<void*>(base),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            check(p==reinterpret_cast<void*>(base),"isolated fixture retail-address pages allocated");pages.push_back(p);}
        for(const auto& byte:m.data)*reinterpret_cast<unsigned char*>(byte.first)=byte.second;
    }
    ~NativePages(){for(void* page:pages)VirtualFree(page,0,MEM_RELEASE);}
};
int main(){
    for(int level:{0,1,2})for(int hidden:{0,1}){
        Memory x;x.put<int>(Memory::ps+0x55c,level);x.put<int>(Memory::cg+0xa4680,hidden);
        x.pre.gameplay_active=false;x.pre.native_weapon_flags=0x80;x.put<U32>(Memory::ps+0x10,0x80);
        const auto saved=x.data;swim::Facts f{};
        check(x.walk(&f)&&f.waterlevel==level&&f.camera[6]==hidden,
            "ordinary disarmed and hidden-viewmodel walking has independent locomotion permission");
        check(x.data==saved&&!x.pre.gameplay_active,"walking never changes script flags or grants weapon tracking");
        const auto reads=x.reads;
        for(unsigned failed=1;failed<=reads;++failed){Memory y=x;y.fail_at=failed;
            check(!y.walk(&f)&&f.cg==0,"every failed walking getter refuses turn without partial facts");}
    }
    for(unsigned fault=0;fault<16;++fault){
        Memory x;x.put<int>(Memory::ps+0x55c,0);x.put<int>(Memory::cg+0xa4680,1);
        x.pre.gameplay_active=false;x.pre.native_weapon_flags=0x80;x.put<U32>(Memory::ps+0x10,0x80);
        switch(fault){
        case 0:x.pre.native_pm_type=1;x.put<int>(Memory::ps+4,1);break;
        case 1:case 2:case 3:case 4:case 5:{constexpr U32 bits[]{4,8,0x400,0x800,0x20000};
            x.pre.native_pm_flags=bits[fault-1];x.put<U32>(Memory::ps+0xc,bits[fault-1]);break;}
        case 6:x.put<int>(Memory::cg+0xa4680,2);break;
        case 7:x.put<int>(Memory::ps+0x55c,3);break;
        case 8:x.pre.native_health=0;x.put<int>(Memory::ps+0x1c4,0);break;
        case 9:x.pre.context.key_catcher=16;break;
        case 10:x.pre.context.native_override=true;break;
        case 11:x.put<U32>(Memory::ps+0x440,1);break;
        case 12:x.pre.observed_ms=899;break;
        case 13:x.change_pre=true;break;
        case 14:x.change_thread=true;break;
        case 15:x.put<int>(Memory::cg+0xa4780,1);break;
        }
        swim::Facts f{};check(!x.walk(&f)&&f.cg==0,
            "disarmed walking retains linked frozen ladder camera health menu and freshness restrictions");
    }
    Memory m;swim::Facts baseline{};check(m.read(&baseline),"live normal swimming graph accepted");
    const auto total=m.reads;
    check(baseline.waterlevel==5&&baseline.delta_angles[2]==1.148f&&baseline.pitch_up_limit==85&&baseline.pitch_down_limit==80,
        "native water delta and asymmetric pitch bounds copied without mutation");
    for(unsigned i=1;i<=total;++i){Memory x;x.fail_at=i;swim::Facts f{};
        check(!x.read(&f)&&f.cg==0,"every failed graph getter rejects without partial facts");}
    // Mutate each distinct identity between the two completed graph copies.
    for(auto at:{Memory::ps+0x55cu,Memory::ps+0x84u,Memory::ps+0x10u,Memory::cg+0x8c134u,Memory::cg+0x8c158u,
                 Memory::dvar+0x218u,0x2ff5354u,0x2993850u}){
        Memory x;x.mutate_at=total/2;x.mutation=at;x.mutation_value=4;swim::Facts f{};
        check(!x.read(&f),"changed water pose time bounds or ownership rejects between copies");}
    for(int level:{3,4,5}){Memory x;x.put<int>(Memory::ps+0x55c,level);swim::Facts f{};
        check(x.read(&f)&&f.waterlevel==level,"surface and submerged native swim levels accepted");}
    // Regression: the live sampan approach is PM_NORMAL, waterlevel 5, health
    // 51, weapon_flags 0x88. The ordinary weapon classifier correctly refuses
    // this state; swimming must independently validate locomotion permission.
    for(U32 flags:{0u,8u,0x80u,0x88u,0x100u,0x180u,0x188u,0x200u}){
        Memory x;x.pre.gameplay_active=false;x.pre.native_weapon_flags=flags;
        x.pre.native_health=51;x.put<U32>(Memory::ps+0x10,flags);x.put<int>(Memory::ps+0x1c4,51);
        const auto before=x.data;swim::Facts f{};
        check(x.read(&f)&&f.predicted.weapon_flags==flags&&f.predicted.health==51,
            "script disabled primary offhand or cycling does not freeze native swimming");
        check(x.data==before&&!x.pre.gameplay_active,
            "locomotion reader preserves script weapon flags and weapon eligibility");
    }
    {
        namespace weapon=bovr::t5::primary_weapon_tracking;
        check(weapon::player_mode({0,51,0,8,2,0x8000})==weapon::Mode::ordinary&&
              weapon::player_mode({0,51,0,0x88,2,0x8000})==weapon::Mode::rejected,
            "disabled primary weapon remains ineligible despite swimming admission");
    }
    // Dropping primary-weapon permission must not drop real movement, camera,
    // ownership or menu restrictions, including the native melee-charge mode.
    for(unsigned fault=0;fault<20;++fault){
        Memory x;x.pre.gameplay_active=false;x.pre.native_weapon_flags=0x88;
        x.put<U32>(Memory::ps+0x10,0x88);swim::Facts f{};
        switch(fault){
        case 0:x.pre.native_pm_type=1;x.put<int>(Memory::ps+4,1);break;
        case 1:case 2:case 3:case 4:case 5:{
            constexpr U32 bits[]{4,8,0x400,0x800,0x20000};const auto bit=bits[fault-1];
            x.pre.native_pm_flags=bit;x.put<U32>(Memory::ps+0xc,bit);break;}
        case 6:case 7:case 8:{
            constexpr U32 bits[]{0x100,0x200,0x4000};const auto bit=bits[fault-6];
            x.pre.native_entity_flags=bit;x.put<U32>(Memory::ps+0xe0,bit);break;}
        case 9:case 10:case 11:{
            constexpr U32 bits[]{0x40000,0x10000000,0x40000000};const auto bit=bits[fault-9];
            x.pre.native_entity_flags2=bit;x.put<U32>(Memory::ps+0xe4,bit);break;}
        case 12:x.put<U32>(Memory::ps+0x440,1);break;
        case 13:x.put<int>(Memory::ps+0x55c,2);break;
        case 14:x.pre.context.key_catcher=16;x.put<int>(0x2910160,16);break;
        case 15:x.pre.context.native_override=true;x.put<int>(0x2911e2c,1);break;
        case 16:x.pre.native_health=0;x.put<int>(Memory::ps+0x1c4,0);break;
        case 17:x.pre.observed_ms=899;break;
        case 18:x.thread=43;break;
        case 19:x.pre.native_gameplay_fields_valid=false;break;
        }
        check(!x.read(&f)&&f.cg==0,"disabled weapon never overrides native movement camera or freshness exclusions");
    }
    for(int level:{-1,0,1,2,6,INT_MAX}){Memory x;x.put<int>(Memory::ps+0x55c,level);swim::Facts f{};
        check(!x.read(&f),"land shallow or impossible water levels refused");}
    for(int pm:{1,2,3,4,5,6,7,9,10}){Memory x;x.pre.native_pm_type=pm;x.put<int>(Memory::ps+4,pm);swim::Facts f{};
        check(!x.read(&f),"linked spectator dead and vehicle movement modes refused");}
    for(U32 flags:{4u,8u,0x400u,0x800u,0x20000u}){Memory x;x.pre.native_pm_flags=flags;x.put<U32>(Memory::ps+0xc,flags);swim::Facts f{};
        check(!x.read(&f),"mantle ladder frozen and melee charge preserve native movement");}
    for(U32 flags:{0x100u,0x200u,0x4000u}){Memory x;x.pre.native_entity_flags=flags;x.put<U32>(Memory::ps+0xe0,flags);swim::Facts f{};
        check(!x.read(&f),"mounted and constrained camera player refused");}
    for(U32 flags:{0x40000u,0x10000000u,0x40000000u}){Memory x;x.pre.native_entity_flags2=flags;x.put<U32>(Memory::ps+0xe4,flags);swim::Facts f{};
        check(!x.read(&f),"missile vehicle and cinematic movement refused");}
    for(unsigned fault=0;fault<19;++fault){Memory x;swim::Facts f{};
        switch(fault){
        case 0:x.pre.context.key_catcher=16;break;
        case 1:x.pre.context.native_override=true;break;
        case 2:x.pre.context.connection_state=8;break;
        case 3:x.thread=43;break;
        case 4:x.pre.native_gameplay_fields_valid=false;break;
        case 5:x.change_pre=true;break;
        case 6:x.change_thread=true;break;
        case 7:x.put<U32>(0x2ff5354,0xfffffff0);break;
        case 8:x.put<std::uint8_t>(Memory::ps+0x130,1);break;
        case 9:x.put<U32>(Memory::ps+0x440,1);break;
        case 10:x.put<int>(Memory::cg+0x8c158,x.pre.native_game_time-1);break;
        case 11:x.put<float>(Memory::ps+0x84,std::numeric_limits<float>::quiet_NaN());break;
        case 12:x.put<float>(Memory::ps+0x8c,std::numeric_limits<float>::infinity());break;
        case 13:x.put<float>(Memory::ps+0x88,360001.f);break;
        case 14:x.put<float>(Memory::cg+0x8c134,2.f);break;
        case 15:x.put<float>(Memory::cg+0x8c134,std::numeric_limits<float>::quiet_NaN());break;
        case 16:x.put<int>(0x2993850,18);break;
        case 17:x.put<int>(0x2481764,9001);break;
        case 18:x.put<U32>(Memory::ps+0x10,0x80);break;
        }
        check(!x.read(&f),"invalid owner state camera timing or numeric field rejected");
    }
    for(unsigned i=0;i<4;++i)for(unsigned fault=0;fault<4;++fault){Memory x;swim::Facts f{};
        const auto at=Memory::dvar+i*0x100;
        switch(fault){case 0:x.put<U32>(at+0x10,0);break;case 1:x.put<char>(Memory::name+i*0x100,'X');break;
        case 2:if(i<2)x.put<int>(at+0x18,1);else x.put<float>(at+0x18,0);break;
        case 3:if(i<2)x.put<int>(at+0x18,2);else x.put<float>(at+0x18,91);break;}
        check(!x.read(&f),"wrong dvar identity type forced camera or invalid bound refused");}
    for(unsigned side:{2u,3u})for(float limit:{.02f,.05f,.1f,.199f}){Memory x;swim::Facts f{};
        x.put<float>(Memory::dvar+side*0x100+0x18,limit);
        check(!x.read(&f),"tiny native pitch limit cannot invert the numeric safety margin");}
    {Memory x;swim::Facts f{};x.put<float>(Memory::dvar+0x218,.2f);x.put<float>(Memory::dvar+0x318,.2f);
        check(x.read(&f),"smallest supported pitch limits leave room for both safety margins");}
    for(U32 offset:{0x8a360u,0xcu,0xa9d44u,0x1cu,0x14u,0xa4780u,0xa4680u}){Memory x;swim::Facts f{};
        x.put<int>(Memory::cg+offset,offset==0x14?3:1);check(!x.read(&f),"non ordinary native camera refused");}
    {Memory x;swim::Facts f{};x.put<int>(Memory::cg+0x8a360,2);check(x.read(&f),"native first person shadow mode accepted");}
    {Memory x;swim::Facts f{};check(!x.read(&f,1101)&&!x.read(&f,999)&&!x.read(&f,1000,0x500000),"stale future and unpinned base refused");}
    // Exercise the production SEH copy and wrapper against this process's own
    // allocated fixture pages, never a running game or global runtime files.
    {
        Memory x;x.pre.context.native_thread=x.thread=GetCurrentThreadId();
        x.pre.gameplay_active=false;x.pre.native_weapon_flags=0x88;x.put<U32>(Memory::ps+0x10,0x88);
        NativePages pages{x};native_memory=&x;
        swim::Facts f{};SetLastError(1234);
        check(swim::copy_current(0x400000,x.pre,x.pre.context,1000,&f)&&GetLastError()==1234,
            "actual protected production reader accepts disabled weapon swimming and preserves last error");
        check(*reinterpret_cast<U32*>(Memory::ps+0x10)==0x88&&!x.pre.gameplay_active,
            "actual reader never clears disabled weapons or grants primary weapon permission");
        DWORD old{};const auto water_page=Memory::ps+0x55c;
        check(VirtualProtect(reinterpret_cast<void*>(water_page),4,PAGE_NOACCESS,&old)!=0,"water fixture page protected");
        check(!swim::copy_current(0x400000,x.pre,x.pre.context,1000,&f)&&f.cg==0,
            "actual inaccessible native water getter fails closed without exception escape");
        DWORD ignored{};check(VirtualProtect(reinterpret_cast<void*>(water_page),4,old,&ignored)!=0,"fixture page restored");
        *reinterpret_cast<float*>(Memory::ps+0x84)=std::numeric_limits<float>::infinity();
        check(!swim::copy_current(0x400000,x.pre,x.pre.context,1000,&f),"actual nonfinite delta cannot authorize movement");
        native_memory=nullptr;
    }
    std::printf("%u swimming native context checks passed\n",checks);
}
