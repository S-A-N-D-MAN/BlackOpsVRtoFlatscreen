// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
#include "t5/grenade_projectile_update.hpp"
#include "t5/identity.hpp"
#include "t5/manual_reload_native_guards.hpp"
#include "loader/trace.hpp"
#include <atomic>
#include <cmath>
#include <cstring>

namespace bovr::t5::grenade_projectile {
namespace {
using U32=std::uint32_t;
constexpr std::uintptr_t entities_va=0x1a796f8,entities_slot=0x1c03144;
constexpr std::size_t entity_size=0x34c,trajectory_size=0x20;







std::atomic<bool> configured{};
bool key_valid(const Key& key) noexcept {
    return key.gameplay_epoch&&key.xr_generation&&key.transaction&&key.weapon>0&&key.weapon<256;
}
bool finite(const std::array<float,3>& v) noexcept {return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]);}
bool read_native(void*,std::uintptr_t address,void* out,std::size_t size) noexcept {
    if(!out||!size||address<0x10000||size>0x1000||address>UINT32_MAX-size)return false;
    __try {std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool write_native(void*,std::uintptr_t address,const void* expected,const void* replacement,std::size_t size) noexcept {
    if(!configured.load(std::memory_order_acquire)||!expected||!replacement||size!=trajectory_size||
       address<entities_va+0x10||address-entities_va>=max_entities*entity_size||
       (address-entities_va)%entity_size!=0x10)return false;
    __try {
        if(*reinterpret_cast<const DWORD*>(0x251ca98)!=GetCurrentThreadId()||*reinterpret_cast<const int*>(0x2899d4c))return false;
        MEMORY_BASIC_INFORMATION page{};
        if(VirtualQuery(reinterpret_cast<void*>(address),&page,sizeof(page))!=sizeof(page)||page.State!=MEM_COMMIT||
           (page.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto access=page.Protect&0xff;
        if(access!=PAGE_READWRITE&&access!=PAGE_WRITECOPY)return false;
        const auto page_at=reinterpret_cast<std::uintptr_t>(page.BaseAddress);
        if(address<page_at||address-page_at>page.RegionSize||page.RegionSize-(address-page_at)<size)return false;
        if(std::memcmp(reinterpret_cast<void*>(address),expected,size))return false;
        std::memcpy(reinterpret_cast<void*>(address),replacement,size);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool synchronize_native(void*) noexcept {
    if(!configured.load(std::memory_order_acquire))return false;
    __try {
        if(*reinterpret_cast<const DWORD*>(0x251ca98)!=GetCurrentThreadId())return false;
        reinterpret_cast<void(__cdecl*)()>(0x640c50)();
        return *reinterpret_cast<const int*>(0x2899d4c)==0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T>bool get(const Access& access,std::uintptr_t at,T* out) noexcept {
    return access.read&&access.read(access.context,at,out,sizeof(T));
}
template<class T,std::size_t N>T value(const std::array<unsigned char,N>& bytes,std::size_t offset) noexcept {
    T out{};std::memcpy(&out,bytes.data()+offset,sizeof(T));return out;
}
struct Context {
    U32 cg{},server{},entity_array{},variant{},definition{};
    int time{},count{},worker{},parent_number{},server_pm{},server_health{},key_catcher{},connection{};
    unsigned char player_number{};
    unsigned short grenade_classname{};
    int type{},offhand_class{},offhand_slot{};
    friend bool operator==(const Context&,const Context&)=default;
};
bool context(const Access& access,unsigned weapon,Context* out) noexcept {
    Context c{};U32 parent_client{};
    if(!get(access,0x2ff5354,&c.cg)||!get(access,0x1c03140,&c.server)||!get(access,entities_slot,&c.entity_array)||
       !get(access,0x1c0314c,&c.count)||!get(access,0x1c04890,&c.time)||!get(access,0x2899d4c,&c.worker)||
       !get(access,entities_va,&c.parent_number)||!get(access,entities_va+0x13c,&parent_client)||
       !get(access,0x23a55f8,&c.grenade_classname)||!get(access,0x2910160,&c.key_catcher)||!get(access,0x2910164,&c.connection)||
       c.cg<0x10000||c.server<0x10000||c.server>UINT32_MAX-0x1c8||parent_client!=c.server||
       c.entity_array!=entities_va||c.count<1||c.count>int(max_entities)||c.worker||c.parent_number||
       c.time<0||!c.grenade_classname||c.key_catcher||c.connection<9)return false;
    if(!get(access,c.server+0x130,&c.player_number)||!get(access,c.server+4,&c.server_pm)||!get(access,c.server+0x1c4,&c.server_health)||
       c.player_number||c.server_pm||c.server_health<=0||!get(access,0xbe19a8+weapon*4,&c.variant)||c.variant<0x10000||
       c.variant>UINT32_MAX-12||!get(access,c.variant+8,&c.definition)||c.definition<0x10000||c.definition>UINT32_MAX-0x70||
       !get(access,c.definition+0x1c,&c.type)||!get(access,c.definition+0x68,&c.offhand_class)||!get(access,c.definition+0x6c,&c.offhand_slot)||
       c.type!=1||c.offhand_class<1||c.offhand_class>3||c.offhand_slot<1||c.offhand_slot>2)return false;
    *out=c;return true;
}
bool stamp(const Access& access,unsigned slot,Stamp* out) noexcept {
    const auto at=entities_va+slot*entity_size;
    return get(access,at+0x330,&out->generation)&&get(access,at+0x340,&out->birth)&&get(access,at+0xdd,&out->inuse)&&out->inuse<=1;
}
bool same_stamp(const Stamp& a,const Stamp& b) noexcept {return a.generation==b.generation&&a.birth==b.birth&&a.inuse==b.inuse;}
struct Entity {
    std::array<unsigned char,0x184> front{};
    std::array<unsigned char,0x14> lifetime{};
    Stamp stamp{};
    friend bool operator==(const Entity& a,const Entity& b) noexcept {
        return a.front==b.front&&a.lifetime==b.lifetime&&same_stamp(a.stamp,b.stamp);
    }
};
bool entity(const Access& access,unsigned slot,Entity* out) noexcept {
    const auto at=entities_va+slot*entity_size;
    if(!get(access,at,&out->front)||!get(access,at+0x330,&out->lifetime))return false;
    out->stamp={value<U32>(out->lifetime,0),value<U32>(out->lifetime,0x10),value<unsigned char>(out->front,0xdd)};
    return out->stamp.inuse<=1;
}
bool candidate(const Entity& e,unsigned slot,const Baseline& base,const Context& c) noexcept {
    const auto& old=base.slots[slot];
    const bool fresh=slot>=base.count||e.stamp.generation!=old.generation||!old.inuse;
    const auto birth=static_cast<std::int64_t>(e.stamp.birth),age=static_cast<std::int64_t>(c.time)-birth;
    return fresh&&e.stamp.inuse==1&&birth>=base.time&&age>=0&&age<=100&&
        value<U32>(e.front,0)==slot&&value<unsigned short>(e.front,0xbe)==4&&
        value<unsigned char>(e.front,0xd4)==base.key.weapon&&value<unsigned char>(e.front,0x15a)==0x0e&&
        value<unsigned short>(e.front,0x15c)==c.grenade_classname&&
        value<unsigned short>(e.front,0x134)==1&&value<unsigned short>(e.front,0x17c)==1&&
        value<unsigned char>(e.front,0xc)==6&&value<int>(e.front,0x10)==birth&&
        value<int>(e.front,0x14)==0&&value<int>(e.front,0x180)>c.time&&
        finite(value<std::array<float,3>>(e.front,0x18))&&finite(value<std::array<float,3>>(e.front,0x24))&&
        finite(value<std::array<float,3>>(e.front,0x11c));
}
bool invoke(GrenadeThrowCallback callback,const GrenadeThrowArguments& args,GrenadeThrowReplacement* out) noexcept {
    __try {return callback&&callback(args,out);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool loaded_guards() noexcept {
    __try {return !bovr::native_guard::compare(reinterpret_cast<const void*>(0x56ac90), 130, "092c56281a3372bf62ed1b966e40979058d57d4525ff911f8d4100f82519c91c")&&
        !bovr::native_guard::compare(reinterpret_cast<const void*>(0x438c98), 139, "e5e2801ea077decf1aeb72fe74f9f1209fcd8386e0cb2f74552fea2bb0dacfa3")&&
        !bovr::native_guard::compare(reinterpret_cast<const void*>(0x613620), 545, "2d56a98ddae851b95e2743892f0f085f803cf73958dab8ecbb23cbad63f32853")&&
        !bovr::native_guard::compare(reinterpret_cast<const void*>(0x5f0900), 257, "33a59fc9d42d511652a804c01a9dcf55701b04d45f583fa3a165800536f7e6f0")&&
        !bovr::native_guard::compare(reinterpret_cast<const void*>(0x4bf260), 194, "8924d37249164ec47540cb3a075a98e4975a54d20f302bbd5c4d4c0899e2070e")&&
        !bovr::native_guard::compare(reinterpret_cast<const void*>(0x6318d0), 466, "9908d89790e7a906405ea1c9f850b4c39092791093d60083eab46515ddabdad3")&&
        !bovr::native_guard::compare(reinterpret_cast<const void*>(0x7e6b50), 156, "3c587f2ce4efc6e22ebd14f5ab20e487373bdecd70d34a4231d4ca676a0e224a")&&
        bovr::native_guard::matches_absolute(manual_native::server_wait_guard)&&
        bovr::native_guard::matches_absolute(manual_native::server_command_sync_guard)&&
        bovr::native_guard::matches_absolute(manual_native::main_thread_init_guard);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
bool configure(HMODULE image) noexcept {
    if(configured.load(std::memory_order_acquire))return image==GetModuleHandleW(nullptr);
    if(image!=GetModuleHandleW(nullptr)||reinterpret_cast<std::uintptr_t>(image)!=0x400000)return false;
    try {std::string reason;
        if(!validate_path(executable_path()).accepted||!validate_loaded_headers(image,reason)||!loaded_guards())return false;
        configured.store(true,std::memory_order_release);return true;
    }catch(...){return false;}
}
const Access& native_access() noexcept {static const Access access{nullptr,synchronize_native,read_native,write_native};return access;}
bool capture(Baseline* base,const Key& key,const Access& access) noexcept {
    if(!base)return false;*base={};
    if(!key_valid(key)||!access.synchronize||!access.read||!access.compare_write||!access.synchronize(access.context))return false;
    Context c{},again{};if(!context(access,key.weapon,&c))return false;
    for(unsigned i=0;i<unsigned(c.count);++i)if(!stamp(access,i,&base->slots[i]))return false;
    for(unsigned i=0;i<unsigned(c.count);++i){Stamp current{};if(!stamp(access,i,&current)||!same_stamp(base->slots[i],current))return false;}
    if(!context(access,key.weapon,&again)||c!=again)return false;
    base->key=key;base->cg=c.cg;base->server=c.server;base->variant=c.variant;base->definition=c.definition;
    base->time=c.time;base->count=unsigned(c.count);base->owner=GetCurrentThreadId();base->ready=true;return true;
}
Result poll(Baseline* base,const Key& key,GrenadeThrowCallback callback,const Access& access) noexcept {
    Result result{};
    if(!base||!base->ready||base->key!=key||base->owner!=GetCurrentThreadId()||!key_valid(key)||!callback)return result;
    if(base->retired){result.status=Status::retired;return result;}
    if(!access.synchronize||!access.read||!access.compare_write||!access.synchronize(access.context))return result;
    Context c{};
    // Pause, a menu catcher, or a failed copy provides no authority to write,
    // but also no evidence that this baseline's player/weapon was rebound.
    // A balanced key-up can produce the retained cancellation drop on resume.
    if(!context(access,key.weapon,&c))return result;
    if(c.cg!=base->cg||c.server!=base->server||c.variant!=base->variant||c.definition!=base->definition||
       c.time<base->time||static_cast<std::int64_t>(c.time)-base->time>15000){base->retired=true;result.status=Status::stale;return result;}
    Entity selected{};unsigned found{};
    for(unsigned i=1;i<unsigned(c.count);++i){Entity row{};if(!entity(access,i,&row)){result.status=Status::invalid;return result;}
        if(candidate(row,i,*base,c)){if(found){base->retired=true;result.status=Status::ambiguous;return result;}selected=row;found=i;}}
    if(!found){result.status=Status::waiting;return result;}
    result.entity=found;result.generation=selected.stamp.generation;result.birth=int(selected.stamp.birth);result.time=c.time;
    Context recheck{};Entity current{};
    if(!context(access,key.weapon,&recheck)||c!=recheck||!entity(access,found,&current)||selected!=current){result.status=Status::stale;return result;}
    GrenadeThrowArguments args{};args.parent=entities_va;args.weapon=key.weapon;args.model=value<unsigned char>(selected.front,0xd6);
    args.rotate=1;args.fuse_time=value<int>(selected.front,0x180)-c.time;
    args.origin=value<std::array<float,3>>(selected.front,0x11c);args.velocity=value<std::array<float,3>>(selected.front,0x24);
    GrenadeThrowReplacement replacement{args.origin,args.velocity};
    if(!invoke(callback,args,&replacement)){result.status=Status::callback_rejected;return result;}
    // The host accepted/consumed this release. Never retry it, even when a
    // changed native record or rejected write prevents application.
    base->retired=true;
    if(!finite(replacement.origin)||replacement.origin!=args.origin||!finite(replacement.velocity)||
       !context(access,key.weapon,&recheck)||c!=recheck||!entity(access,found,&current)||selected!=current){result.status=Status::stale;return result;}
    std::array<unsigned char,trajectory_size> before{},after{};
    std::memcpy(before.data(),selected.front.data()+0x10,before.size());after=before;
    std::memcpy(after.data(),&c.time,sizeof(c.time));
    std::memcpy(after.data()+8,args.origin.data(),sizeof(args.origin));
    std::memcpy(after.data()+20,replacement.velocity.data(),sizeof(replacement.velocity));
    const auto address=entities_va+found*entity_size;
    const bool written=access.compare_write(access.context,address+0x10,before.data(),after.data(),after.size());
    result.status=written?Status::applied:Status::write_rejected;
    trace("t5_grenade_projectile_update","applied=%u entity=%u generation=%u weapon=%u birth=%d time=%d age=%d "
          "origin=%.3f,%.3f,%.3f velocity=%.3f,%.3f,%.3f native_current_origin_retained=1 fuse_untouched=1 writes=trajectory32 instruction_patches=0",
          unsigned(written),found,result.generation,key.weapon,result.birth,c.time,c.time-result.birth,
          double(args.origin[0]),double(args.origin[1]),double(args.origin[2]),double(replacement.velocity[0]),double(replacement.velocity[1]),double(replacement.velocity[2]));
    return result;
}
}
