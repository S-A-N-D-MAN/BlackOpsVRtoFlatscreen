// SPDX-License-Identifier: GPL-3.0-only
#include "../src/t5/grenade_projectile_update.cpp"
#include <map>
#include <vector>
#include <cstdio>
#include <limits>
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
namespace bovr::t5 {
Validation validate_path(const std::wstring&) {return {};}
bool validate_loaded_headers(HMODULE,std::string&) {return false;}
std::wstring executable_path() {return {};}
}
namespace {
namespace gp=bovr::t5::grenade_projectile;
constexpr std::uintptr_t arena=0x1a796f8,server=0x200000,variant=0x4000000,definition=0x4000200;
constexpr std::size_t stride=0x34c;
unsigned checks{},failures{},callbacks{};
bool accept=true,move_origin{},nan_velocity{},mutate_on_callback{};
bovr::t5::GrenadeThrowArguments observed{};
void check(bool ok,const char* text){++checks;if(!ok)++failures;std::printf("%s %s\n",ok?"PASS":"FAIL",text);}
struct Fixture {
    std::map<std::uintptr_t,std::vector<unsigned char>> memory;
    bool barrier=true,write_ok=true,read_ok=true;
    unsigned barriers{},writes{};
    std::uintptr_t write_address{};std::size_t write_size{};
    gp::Access access{this,sync,read,write};
    gp::Key key{11,12,13,7};gp::Baseline baseline;
    static bool sync(void* ctx) noexcept {auto& f=*static_cast<Fixture*>(ctx);++f.barriers;return f.barrier;}
    unsigned char* find(std::uintptr_t at,std::size_t size) noexcept {
        auto it=memory.upper_bound(at);if(it==memory.begin())return nullptr;--it;
        if(at-it->first>it->second.size()||size>it->second.size()-(at-it->first))return nullptr;
        return it->second.data()+at-it->first;
    }
    static bool read(void* ctx,std::uintptr_t at,void* out,std::size_t size) noexcept {
        auto& f=*static_cast<Fixture*>(ctx);auto* p=f.find(at,size);if(!f.read_ok||!p)return false;
        std::memcpy(out,p,size);return true;
    }
    static bool write(void* ctx,std::uintptr_t at,const void* before,const void* after,std::size_t size) noexcept {
        auto& f=*static_cast<Fixture*>(ctx);auto* p=f.find(at,size);if(!f.write_ok||!p||std::memcmp(p,before,size))return false;
        ++f.writes;f.write_address=at;f.write_size=size;std::memcpy(p,after,size);return true;
    }
    template<class T>void set(std::uintptr_t at,T v){auto* p=find(at,sizeof(v));if(!p){memory[at].resize(sizeof(v));p=find(at,sizeof(v));}std::memcpy(p,&v,sizeof(v));}
    template<class T>T get(std::uintptr_t at){T v{};std::memcpy(&v,find(at,sizeof(v)),sizeof(v));return v;}
    Fixture() {
        memory[arena].resize(4*stride);memory[server].resize(0x500);memory[variant].resize(0x100);memory[definition].resize(0x700);
        set<std::uint32_t>(0x2ff5354,0x300000);set<std::uint32_t>(0x1c03140,std::uint32_t(server));
        set<std::uint32_t>(0x1c03144,std::uint32_t(arena));set<int>(0x1c0314c,2);set<int>(0x1c04890,1000);set<int>(0x2899d4c,0);
        set<std::uint32_t>(arena+0x13c,std::uint32_t(server));set<unsigned short>(0x23a55f8,19);
        set<int>(0x2910160,0);set<int>(0x2910164,9);set<int>(server+0x1c4,100);
        set<std::uint32_t>(0xbe19a8+7*4,std::uint32_t(variant));set<std::uint32_t>(variant+8,std::uint32_t(definition));
        set<int>(definition+0x1c,1);set<int>(definition+0x68,1);set<int>(definition+0x6c,1);
        callbacks=0;accept=true;move_origin=nan_velocity=mutate_on_callback=false;
    }
    bool capture(){return gp::capture(&baseline,key,access);}
    void born(unsigned slot=1,int birth=1020,int now=1050) {
        const auto at=arena+slot*stride;set<int>(0x1c04890,now);
        set<std::uint32_t>(at,slot);set<unsigned char>(at+0xdd,1);set<unsigned short>(at+0xbe,4);
        set<unsigned char>(at+0xd4,7);set<unsigned char>(at+0xd6,2);set<unsigned char>(at+0x15a,0x0e);
        set<unsigned short>(at+0x15c,19);set<unsigned short>(at+0x134,1);set<unsigned short>(at+0x17c,1);
        set<unsigned char>(at+0xc,6);set<int>(at+0x10,birth);set<int>(at+0x340,birth);set<int>(at+0x180,4000);
        set(at+0x18,std::array<float,3>{10,20,30});set(at+0x24,std::array<float,3>{700,0,200});
        set(at+0x11c,std::array<float,3>{25,20,31});
    }
};
Fixture* active{};
bool callback(const bovr::t5::GrenadeThrowArguments& args,bovr::t5::GrenadeThrowReplacement* out) noexcept {
    ++callbacks;observed=args;out->origin=args.origin;out->velocity={0,900,240};
    if(move_origin)out->origin[0]+=10;
    if(nan_velocity)out->velocity[0]=std::numeric_limits<float>::infinity();
    if(mutate_on_callback)active->set<std::uint32_t>(arena+stride+0x330,9);
    return accept;
}
gp::Result poll(Fixture& f){active=&f;return gp::poll(&f.baseline,f.key,callback,f.access);}
}
int main() {
    {
        Fixture f;check(f.capture(),"baseline captured only after barrier and complete copied context");
        check(poll(f).status==gp::Status::waiting&&callbacks==0&&f.writes==0,"no native spawn means no callback or write");
        f.born();auto original=f.memory[arena];const auto result=poll(f);
        check(result.status==gp::Status::applied&&result.entity==1&&callbacks==1&&f.writes==1,"unique newborn local grenade applies once");
        check(observed.parent==arena&&observed.weapon==7&&observed.model==2&&observed.fuse_time==2950,
              "callback has local parent weapon model and current remaining timer");
        check(f.get<int>(arena+stride+0x10)==1050&&f.get<std::array<float,3>>(arena+stride+0x18)==std::array<float,3>{25,20,31}&&
              f.get<std::array<float,3>>(arena+stride+0x24)==std::array<float,3>{0,900,240},"rebase uses native current position and time with physical velocity");
        const auto offset=stride+0x10;bool only_trajectory=true;
        for(std::size_t i=0;i<original.size();++i)if((i<offset||i>=offset+32)&&original[i]!=f.memory[arena][i])only_trajectory=false;
        check(only_trajectory&&f.write_address==arena+stride+0x10&&f.write_size==32,
              "currentOrigin rotation ownership timer and every other native byte remain unchanged");
        check(poll(f).status==gp::Status::retired&&callbacks==1&&f.writes==1,"repeat poll cannot apply or call host twice");
        check(f.barriers==3,"capture and both active polls synchronize even without reload");
    }
    {
        Fixture f;f.born(1,990,1000);check(f.capture(),"preexisting grenade baseline");
        check(poll(f).status==gp::Status::waiting&&f.writes==0,"preexisting same-weapon grenade cannot be claimed");
        f.set<std::uint32_t>(arena+stride+0x330,1);f.born();
        check(poll(f).status==gp::Status::applied,"reused native slot with new generation can match");
    }
    {
        Fixture f;check(f.capture(),"growth baseline");f.set<int>(0x1c0314c,3);f.born(2);
        check(poll(f).status==gp::Status::applied,"newly appended entity slot can match");
    }
    for(unsigned bad=0;bad<11;++bad) {
        Fixture f;f.capture();f.born();const auto at=arena+stride;
        switch(bad){
            case 0:f.set<unsigned short>(at+0x134,2);break;
            case 1:f.set<unsigned short>(at+0x17c,2);break;
            case 2:f.set<unsigned char>(at+0xd4,8);break;
            case 3:f.set<unsigned short>(at+0xbe,3);break;
            case 4:f.set<unsigned char>(at+0x15a,0x0f);break;
            case 5:f.set<unsigned short>(at+0x15c,20);break;
            case 6:f.set<unsigned char>(at+0xc,0);break;
            case 7:f.set<int>(at+0x10,1040);break;
            case 8:f.set<int>(0x1c04890,1121);break;
            case 9:f.set<int>(at+0x180,1040);break;
            case 10:f.set<float>(at+0x11c,std::numeric_limits<float>::quiet_NaN());break;
        }
        check(poll(f).status==gp::Status::waiting&&callbacks==0&&f.writes==0,
              "foreign changed bounced stale expired or nonfinite projectile rejected");
    }
    {
        Fixture f;f.capture();f.set<int>(0x1c0314c,3);f.born(1);f.born(2);
        check(poll(f).status==gp::Status::ambiguous&&f.baseline.retired&&callbacks==0&&f.writes==0,"ambiguous new local grenades fail closed");
    }
    {
        Fixture f;f.capture();f.born();accept=false;
        check(poll(f).status==gp::Status::callback_rejected&&!f.baseline.retired&&f.writes==0,"host can wait until physical release");
        accept=true;check(poll(f).status==gp::Status::applied&&callbacks==2,"later accepted release applies once");
    }
    for(unsigned failure=0;failure<4;++failure){
        Fixture f;f.capture();f.born();move_origin=failure==0;nan_velocity=failure==1;mutate_on_callback=failure==2;f.write_ok=failure!=3;
        const auto r=poll(f);check((r.status==gp::Status::stale||r.status==gp::Status::write_rejected)&&f.writes==0&&f.baseline.retired,
              "accepted invalid geometry mutation or failed write is not retried");
    }
    {
        Fixture f;f.barrier=false;check(!f.capture()&&!f.baseline.ready,"failed server barrier cannot create baseline");
        f.barrier=true;f.capture();f.born();f.barrier=false;check(poll(f).status==gp::Status::invalid&&callbacks==0,"failed poll barrier forbids callback and write");
    }
    {
        Fixture f;f.capture();f.born();++f.key.gameplay_epoch;check(poll(f).status==gp::Status::invalid&&callbacks==0,"changed gameplay epoch cannot claim old baseline");
        --f.key.gameplay_epoch;f.set<int>(0x1c04890,999);check(poll(f).status==gp::Status::stale&&f.baseline.retired,"native time reset retires baseline");
    }
    {
        Fixture f;f.capture();f.set<int>(0x2910160,1);
        check(poll(f).status==gp::Status::invalid&&!f.baseline.retired&&callbacks==0&&f.writes==0,
              "pause key catcher blocks writes without retiring pending cancellation baseline");
        f.set<int>(0x2910160,0);f.read_ok=false;
        check(poll(f).status==gp::Status::invalid&&!f.baseline.retired&&callbacks==0,
              "transient unavailable copy preserves baseline without callback");
        f.read_ok=true;f.born(1,1080,1100);
        check(poll(f).status==gp::Status::applied&&callbacks==1&&f.writes==1,
              "resume can apply retained drop to matching newly born native grenade");
    }
    {
        Fixture f;f.set<int>(0x1c0314c,1023);check(!f.capture(),"entity bound rejects oversized arena");
        f.set<int>(0x1c0314c,2);f.set<int>(definition+0x1c,6);check(!f.capture(),"mine equipment does not enter grenade writer");
    }
    std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
