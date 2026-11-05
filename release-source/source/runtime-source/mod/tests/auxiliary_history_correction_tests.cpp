// SPDX-License-Identifier: GPL-3.0-only
#define BOVR_HISTORY_CAPTURE_TEST_MAIN inherited_history_capture_main
#include "auxiliary_history_tests.cpp"
#include "t5/auxiliary_history_correction.hpp"
#include "t5/history_scalar_ownership.hpp"
namespace correction_fixture {
using namespace bovr::t5;
using history_fixture::Device;using history_fixture::Surface;
unsigned release_calls{},texture_queries{};
struct Texture {void** table{};Surface* surface{};unsigned refs=1;};
HRESULT __stdcall surface_query(Surface* s,REFIID id,void** out){check(id==IID_IUnknown,"surface canonical identity query");history_fixture::sadd(s);*out=s;return S_OK;}
ULONG __stdcall texture_add(Texture* t){return ++t->refs;}
ULONG __stdcall texture_release(Texture* t){++release_calls;check(t->refs>1,"native texture ownership retained");return --t->refs;}
HRESULT __stdcall texture_query(Texture* t,REFIID id,void** out){if(t->surface->owner->fail("texturequery"))return E_FAIL;check(id==IID_IDirect3DTexture9,"texture queried as actual typed 2D interface");texture_add(t);*out=t;return S_OK;}
HRESULT __stdcall texture_surface(Texture* t,UINT level,IDirect3DSurface9** out){if(t->surface->owner->fail("surfacelevel"))return E_FAIL;check(level==0,"native actual level0 only");history_fixture::sadd(t->surface);*out=reinterpret_cast<IDirect3DSurface9*>(t->surface);return S_OK;}
struct Fixture;
Fixture* active{};
AuxiliaryHistoryCorrection* callback_correction{};
unsigned retire_on_copy{},retire_on_desc{},retire_on_release{};
bool change_texture_on_desc{};
ULONG __stdcall counted_surface_release(Surface* s){++release_calls;if(retire_on_release){retire_on_release=0;callback_correction->retire();}return history_fixture::srelease(s);}
ULONG __stdcall counted_device_release(Device* d){++release_calls;return history_fixture::drelease(d);}
HRESULT __stdcall counted_texture_query(Texture* t,REFIID id,void** out){++texture_queries;return texture_query(t,id,out);}
HRESULT __stdcall reentrant_desc(Surface*,D3DSURFACE_DESC*);
struct Fixture : history_fixture::Fixture {
    std::array<void*,19> texture_table{};Texture texture{};std::array<std::uint32_t,1> image{};std::array<unsigned char,32> dvar{};
    std::uint64_t serial=1,epoch=1;unsigned time=1016,calls{},eye{};bool expect_cold=true,native_cpp{},native_seh{},faulted{},relax_input{};
    std::atomic<bool> stop{};unsigned partial_failure{},partial_failure2{};bool protect_index{};DWORD index_protection{};
    std::vector<unsigned char> expected_previous;std::vector<unsigned> observed_times,observed_origins;
    unsigned color_value(unsigned e) const {return 0xff000000|(unsigned(serial)*0x100u)+(e+1)*0x11u;}
    unsigned width{},height{};
    Fixture(unsigned w=1280,unsigned h=720):width(w),height(h){
        d.st[0]=reinterpret_cast<void*>(surface_query);d.st[2]=reinterpret_cast<void*>(counted_surface_release);d.st[12]=reinterpret_cast<void*>(reentrant_desc);
        table[2]=reinterpret_cast<void*>(counted_device_release);texture_table[0]=reinterpret_cast<void*>(counted_texture_query);
        texture_table[1]=reinterpret_cast<void*>(texture_add);texture_table[2]=reinterpret_cast<void*>(texture_release);
        texture_table[18]=reinterpret_cast<void*>(texture_surface);texture={texture_table.data(),&d.one,1};
        image[0]=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&texture));
        m.write(m.base+0x41eb1e8+20,AuxiliaryTarget{static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image.data())),static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&d.one)),0x33330001,static_cast<std::uint16_t>(width),static_cast<std::uint16_t>(height)});
        m.write(m.base+0x356f9e8,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(dvar.data())));
        m.write(m.base+0x356a33c,std::array<std::uint32_t,5>{0,0,0x3f800000,0x3f800000,1});
        for(const auto& s:layout.slots)if(!s.extra){m.write(s.address+0x10b0,std::array<std::uint8_t,48>{});m.write(s.address+0x10e0,std::array<std::uint32_t,48>{});}
        d.one.desc.Width=width;d.one.desc.Height=height;d.one.pixels.resize(std::size_t(width)*height*4);
        for(unsigned i=0;i<2;++i){auto& slot=layout.slots[i];slot.scene=slot.display=slot.scissor={int(i*width/2),0,int(width/2),int(height)};
            m.write(slot.address+0x140,slot.scene);m.write(slot.address+0x150,slot.display);m.write(slot.address+0x160,slot.scissor);}
        m.write(m.base+0x3ccbfbc,bovr::render::HudEyeViewport{0,0,int(width),int(height)});
        m.write(m.base+0x3ccbff0,width);m.write(m.base+0x3ccbff4,height);
        table[34]=reinterpret_cast<void*>(stretch);advance(false);retire_on_copy=retire_on_desc=retire_on_release=0;change_texture_on_desc=false;
    }
    ~Fixture(){check(texture.refs==1,"typed texture borrow released outside native unwind");}
    void advance(bool next=true){if(next){++serial;time+=16;}++layout.frame;m.write(layout.buffer+0x15d7a0,layout.frame);
        m.write(layout.buffer+0x16cc38,2u);for(unsigned i=0;i<2;++i)m.write(layout.slots[i].address+0x170,std::array<std::uint32_t,5>{time,0x3f800000,0,0,0});
        observed_times.clear();observed_origins.clear();faulted=false;stop=false;
    }
    AuxiliaryPostfxKey key(){return {0,100+serial,2,200+serial,GetCurrentThreadId()};}
    static HRESULT __stdcall stretch(Device* device,IDirect3DSurface9* src,const RECT* sr,IDirect3DSurface9* dst,const RECT* dr,D3DTEXTUREFILTERTYPE filter){
        auto* from=reinterpret_cast<Surface*>(src);auto* to=reinterpret_cast<Surface*>(dst);
        const bool ordinary_failure=device->fail("stretch");check(filter==D3DTEXF_NONE,"history copy never filters");
        if(retire_on_copy&&device->calls["stretch"]==retire_on_copy){retire_on_copy=0;callback_correction->retire();}
        check((sr==nullptr)==(dr==nullptr),"history copy has paired rectangles");
        if(active&&(device->calls["stretch"]==active->partial_failure||device->calls["stretch"]==active->partial_failure2)){
            std::fill(to->pixels.begin(),to->pixels.begin()+to->pixels.size()/2,std::uint8_t{0xee});return E_FAIL;}
        if(ordinary_failure)return E_FAIL;
        if(!sr)to->pixels=from->pixels;
        else{check(!std::memcmp(sr,dr,sizeof(RECT)),"history region copied without scaling");
            for(LONG y=sr->top;y<sr->bottom;++y)for(LONG x=sr->left;x<sr->right;++x)
                std::memcpy(to->pixels.data()+(std::size_t(y)*to->desc.Width+x)*4,from->pixels.data()+(std::size_t(y)*from->desc.Width+x)*4,4);}
        if(device->expire_copy)device->current=false;return S_OK;
    }
    static void __cdecl native(){auto& f=*active;++f.calls;unsigned index{};std::memcpy(&index,reinterpret_cast<void*>(f.layout.buffer+0x16cc38),4);f.eye=index==2?1:0;
        if(f.protect_index)check(!!VirtualProtect(reinterpret_cast<void*>(f.layout.buffer+0x16cc38),4,PAGE_NOACCESS,&f.index_protection),"fixture protects native index before original fault");
        if(f.native_cpp)throw std::runtime_error("native C10 fixture");
        if(f.native_seh)RaiseException(0xe011c010,0,0,nullptr);
        std::uint8_t enabled{};std::memcpy(&enabled,reinterpret_cast<void*>(f.layout.slots[f.eye].address+0x21fc),1);
        std::uint32_t t{},origin{};std::memcpy(&t,reinterpret_cast<void*>(f.m.base+0x356a32c),4);std::memcpy(&origin,reinterpret_cast<void*>(f.m.base+0x356a33c),4);
        f.observed_times.push_back(t);f.observed_origins.push_back(origin);
        if(f.relax_input){}
        else if(f.expect_cold)check(enabled==0,"cold seed disables only this regular eye blur");
        else{check(enabled==1,"warm preserves native blur enabled");check(f.d.one.pixels==f.expected_previous,"both native calls see identical previous packed image");
            check(origin==(f.eye?0x3f000000u:0u),"offset-only origin matches eye with full UV span");}
        f.m.write(f.m.base+0x356a32c,f.time);f.m.write(f.m.base+0x356a33c,std::array<std::uint32_t,5>{0,0,0x3f800000,0x3f800000,1});
        f.m.write(f.m.base+0x3ccbfa8,f.time);f.m.write(f.m.base+0x417ff28,std::uint8_t{3});
        f.m.write(f.m.base+0x3ccbfbc,bovr::render::HudEyeViewport{int(f.eye*f.width/2),0,int(f.width/2),int(f.height)});
        std::fill(f.d.one.pixels.begin(),f.d.one.pixels.end(),std::uint8_t{0});
        for(unsigned y=0;y<f.height;++y)for(unsigned x=f.eye*f.width/2;x<(f.eye+1)*f.width/2;++x){const auto c=f.color_value(f.eye);std::memcpy(f.d.one.pixels.data()+(std::size_t(y)*f.width+x)*4,&c,4);}
    }
    void begin(AuxiliaryHistoryCorrection& c){active=this;c.begin(true,m.base,layout,key(),serial,epoch,d.api(),current());}
    void invoke(AuxiliaryHistoryCorrection& c,unsigned e){m.write(layout.buffer+0x16cc38,e?2u:0u);c.before_native(e);
        invoke_history_original(native,&c,nullptr,&faulted,&stop,reinterpret_cast<std::uint32_t*>(layout.buffer+0x16cc38),2);c.after_native(e);}
    bool run(AuxiliaryHistoryCorrection& c,bool ordinary=true){begin(c);invoke(c,1);invoke(c,0);m.write(layout.buffer+0x16cc38,2u);return c.finish(ordinary);}
    void verify_pair(){for(unsigned y:{0u,height/2,height-1})for(unsigned x:{0u,width/2-1,width/2,width-1}){unsigned pixel{};std::memcpy(&pixel,d.one.pixels.data()+(std::size_t(y)*width+x)*4,4);check(pixel==color_value(x/(width/2)),"committed history assembles same-frame left and right only");}}
};
HRESULT __stdcall reentrant_desc(Surface* s,D3DSURFACE_DESC* out){const auto hr=history_fixture::desc(s,out);
    if(retire_on_desc){retire_on_desc=0;callback_correction->retire();}
    if(change_texture_on_desc){change_texture_on_desc=false;active->image[0]=0x10000;}
    return hr;
}
bool catch_native_seh(AuxiliaryHistoryCorrection* c,Fixture* f,AuxiliaryHistoryCapture* cap=nullptr){
    __try {invoke_history_original(Fixture::native,c,cap,&f->faulted,&f->stop,reinterpret_cast<std::uint32_t*>(f->layout.buffer+0x16cc38),2);}
    __except(GetExceptionCode()==0xe011c010?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;
}
void draw_plan_fault(AuxiliaryHistoryCorrection* c,Fixture* f){
    bool started=false;
    execute_stereo_draw_plan(auxiliary_draw_plan(),[&](std::uint32_t value){
        auto* index=reinterpret_cast<std::uint32_t*>(f->layout.buffer+0x16cc38);
        if(f->faulted)restore_history_index(index,value);else std::memcpy(index,&value,4);
    },[&]{invoke_history_original(Fixture::native,c,nullptr,&f->faulted,&f->stop,
        reinterpret_cast<std::uint32_t*>(f->layout.buffer+0x16cc38),2);},[](unsigned){return true;},&started);
}
bool catch_plan_seh(AuxiliaryHistoryCorrection* c,Fixture* f){
    __try {draw_plan_fault(c,f);}
    __except(GetExceptionCode()==0xe011c010?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}return false;
}
void scalar_ownership_tests(){
    auto* memory=static_cast<std::uint32_t*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(memory!=nullptr,"real protected scalar fixture allocated");if(!memory)return;
    const auto p=reinterpret_cast<std::uintptr_t>(memory);DWORD old{};
    for(unsigned i=0;i<6;++i){HistoryScalarOwnership<std::uint32_t> word;memory[i]=100+i;
        check(word.install(p+4*i,100+i,200+i),"each time/UV word installs separately");
        check(!!VirtualProtect(memory,4096,PAGE_READONLY,&old),"scalar fixture read only");
        check(!word.restore(true)&&word.pending&&memory[i]==200+i,"failed restore retains exact field ownership");
        check(!!VirtualProtect(memory,4096,PAGE_NOACCESS,&old),"scalar fixture unreadable");
        check(!word.restore(true)&&word.pending,"unreadable field retains ownership");
        check(!!VirtualProtect(memory,4096,PAGE_READWRITE,&old),"scalar fixture restore access");
        check(word.restore(true)&&!word.pending&&memory[i]==100+i,"later same-scope retry restores original");
        check(word.install(p+4*i,100+i,200+i),"field reinstall");
        check(!word.restore(false)&&word.pending,"changed scope never writes coincident installed value");
        memory[i]=900+i;check(word.restore(false)&&!word.pending&&memory[i]==900+i,"confirmed foreign write retires without overwrite");
    }
    HistoryScalarOwnership<std::uint32_t> first,second;
    memory[1023]=7;memory[1024]=8;
    check(!!VirtualProtect(memory+1024,4096,PAGE_READONLY,&old),"later scalar store failure page");
    check(first.install(p+4092,7,17)&&!second.install(p+4096,8,18)&&second.pending,"failed later aligned store retains attempted ownership after earlier success");
    check(first.restore(true)&&second.restore(true)&&memory[1023]==7&&memory[1024]==8,"partially completed transaction restores each field independently");
    check(!first.install(p+1,0,1),"unaligned word store is rejected");
    check(!!VirtualProtect(memory+1024,4096,PAGE_READWRITE,&old),"later scalar page writable again");
    HistoryScalarOwnership<std::uint8_t> byte;auto* b=reinterpret_cast<std::uint8_t*>(memory);*b=1;
    check(byte.install(p,1,0),"blur byte installed");VirtualProtect(memory,4096,PAGE_READONLY,&old);
    check(!byte.restore(true)&&byte.pending,"blur failed restore retained");VirtualProtect(memory,4096,PAGE_READWRITE,&old);
    check(byte.restore(true)&&*b==1&&!byte.pending,"blur retry restored");
    VirtualFree(memory,0,MEM_RELEASE);
}
}
int main(){using namespace correction_fixture;using namespace bovr::t5;std::setvbuf(stdout,nullptr,_IONBF,0);
    scalar_ownership_tests();
    {Fixture f;AuxiliaryHistoryCorrection c;SetLastError(0x12345);check(f.run(c),"cold seed complete");check(GetLastError()==0x12345,"transaction retains LastError");f.verify_pair();
        f.expected_previous=f.d.one.pixels;f.expect_cold=false;const auto old=f.time;f.advance();check(f.run(c),"warm transaction complete");f.verify_pair();
        check(f.observed_times==std::vector<unsigned>{old,old},"both eyes use the same original timestamp");check(f.calls==4,"one original per eye on two transactions");c.retire();}
    for(unsigned i=0;i<48;++i){Fixture f;AuxiliaryHistoryCorrection c;f.m.write(f.layout.slots[0].address+0x10b0+i,std::uint8_t{1});f.m.write(f.layout.slots[0].address+0x10e0+4*i,0x1234u);
        const auto before=f.d.calls;f.begin(c);check(!c.selected()&&f.d.calls==before,"each custom pass conjunction rejects before native COM");c.retire();}
    {Fixture f;AuxiliaryHistoryCorrection c;f.dvar[0x18]=1;f.begin(c);check(!c.selected()&&f.d.calls["create"]==0,"fallback custom dvar rejects");c.retire();}
    {Fixture f;AuxiliaryHistoryCorrection c;f.texture.surface=&f.d.six;f.begin(c);check(!c.selected()&&f.d.calls["create"]==0,"actual texture level must equal native color surface");c.retire();}
    {Fixture f;AuxiliaryHistoryCorrection c;const auto before=f.d.calls;c.begin(false,f.m.base,f.layout,f.key(),f.serial,f.epoch,f.d.api(),f.current());c.before_native(1);c.after_native(1);check(c.finish(true),"disabled finish preserves native staging");
        check(f.d.calls==before,"disabled correction has zero COM");c.retire();}
    for(unsigned why=0;why<5;++why){Fixture f;AuxiliaryHistoryCorrection c;check(f.run(c),"continuity seed");f.expected_previous=f.d.one.pixels;f.advance();
        if(why==0)++f.serial;if(why==1)++f.epoch;if(why==2)f.m.write(f.m.base+0x356a32c,1u);if(why==3){f.time-=100;for(unsigned i=0;i<2;++i)f.m.write(f.layout.slots[i].address+0x170,f.time);}if(why==4)c.retire();
        f.expect_cold=true;check(f.run(c),"gap reset changed clock or retirement produces cold seed");c.retire();}
    for(unsigned copy=1;copy<=4;++copy){Fixture f;AuxiliaryHistoryCorrection c;f.partial_failure=copy;f.relax_input=true;f.begin(c);f.invoke(c,1);f.invoke(c,0);f.m.write(f.layout.buffer+0x16cc38,2u);
        check(!c.finish(true),"partial capture backup or commit failure rejects pair");check(f.calls==2,"copy failure still calls native once per eye");c.retire();}
    {Fixture f;AuxiliaryHistoryCorrection c;f.partial_failure=4;f.partial_failure2=5;check(!f.run(c),"failed commit plus failed rollback rejected");f.advance();const auto before=f.d.calls;f.begin(c);
        check(!c.selected()&&f.d.calls==before,"rollback failure latches until owner retirement");c.retire();f.partial_failure=f.partial_failure2=0;check(f.run(c),"owner retirement allows fresh cold seed");c.retire();}
    {Fixture f;AuxiliaryHistoryCorrection c;check(!f.run(c,false),"commit requires ordinary completed pair");check(f.d.calls["stretch"]==2,"ordinary failure never backs up or commits target");c.retire();}
    for(bool seh:{false,true}){Fixture f;AuxiliaryHistoryCorrection c;f.begin(c);c.before_native(1);f.m.write(f.layout.buffer+0x16cc38,0u);const auto before=f.d.calls;
        const auto releases=release_calls;f.native_seh=seh;f.native_cpp=!seh;bool caught=false;
        if(seh)caught=catch_native_seh(&c,&f);else try{invoke_history_original(Fixture::native,&c,nullptr,&f.faulted,&f.stop,reinterpret_cast<std::uint32_t*>(f.layout.buffer+0x16cc38),2);}catch(const std::runtime_error&){caught=true;}
        unsigned index{};std::memcpy(&index,reinterpret_cast<void*>(f.layout.buffer+0x16cc38),4);
        check(caught&&f.faulted&&f.stop&&index==2&&f.calls==1,"native C++ and SEH forwarded once with scalar/index scope retirement");check(before==f.d.calls&&releases==release_calls,"native unwind performs no COM calls or Releases");c.retire();}
    for(bool seh:{false,true}){Fixture f;AuxiliaryHistoryCorrection c;f.begin(c);c.before_native(1);f.native_seh=seh;f.native_cpp=!seh;f.protect_index=true;
        const auto releases=release_calls;const auto before=f.d.calls;bool caught=false;
        if(seh)caught=catch_plan_seh(&c,&f);else try{draw_plan_fault(&c,&f);}catch(const std::runtime_error&){caught=true;}
        check(caught&&f.calls==1&&f.faulted&&f.stop,"actual outer draw plan preserves original exception despite inaccessible index restore");
        check(releases==release_calls&&before==f.d.calls,"outer native unwind performs no COM release or GPU operation");
        DWORD ignored{};VirtualProtect(reinterpret_cast<void*>(f.layout.buffer+0x16cc38),4,f.index_protection,&ignored);c.retire();}
    for(unsigned fail=1;fail<=6;++fail){Fixture f;AuxiliaryHistoryCorrection c;check(f.run(c),"warm failure matrix seed");f.advance();f.expect_cold=false;f.expected_previous=f.d.one.pixels;
        f.partial_failure=f.d.calls["stretch"]+fail;f.relax_input=true;f.begin(c);f.invoke(c,1);f.invoke(c,0);f.m.write(f.layout.buffer+0x16cc38,2u);const auto native_last=f.d.one.pixels;
        check(!c.finish(true),"each warm install/capture/backup/commit partial failure rejects exact pair");check(f.calls==4,"warm failures preserve both original calls");
        if(fail==6)check(f.d.one.pixels==native_last,"partial commit failure rolls back actual final native pixels");c.retire();}
    for(unsigned create=1;create<=3;++create){Fixture f;AuxiliaryHistoryCorrection c;f.d.failed_op="create";f.d.failed_number=create;f.begin(c);
        check(!c.selected()&&f.d.calls["stretch"]==0,"each private allocation failure preserves native pixels");c.retire();}
    for(const char* operation:{"getdevice","query","desc","texturequery","surfacelevel"}){Fixture f;AuxiliaryHistoryCorrection c;f.d.failed_op=operation;f.begin(c);
        check(!c.selected()&&f.d.calls["stretch"]==0&&f.d.calls["create"]==0,"each failed native resource acquisition rejects before allocation or pixel mutation");c.retire();}
    for(bool retired:{false,true}){Fixture f;AuxiliaryHistoryCorrection c;callback_correction=&c;retire_on_desc=retired?1:0;change_texture_on_desc=!retired;const auto queries=texture_queries;
        f.begin(c);check(!c.selected()&&texture_queries==queries,"GetDesc reset or tuple replacement never dereferences previous raw texture");c.retire();}
    for(unsigned copy=1;copy<=4;++copy){Fixture f;AuxiliaryHistoryCorrection c;callback_correction=&c;retire_on_copy=copy;f.relax_input=true;
        check(!f.run(c),"reset reentry at each cold copy rejects pair without deleting active State");c.retire();}
    {Fixture f;AuxiliaryHistoryCorrection c;f.begin(c);callback_correction=&c;retire_on_release=1;c.retire();check(!c.selected(),"nested retire during Release retains outer operation lease");}
    {Fixture f;AuxiliaryHistoryCorrection c;f.begin(c);c.before_native(1);DWORD old{};const auto address=f.layout.slots[1].address+0x21fc;
        VirtualProtect(reinterpret_cast<void*>(address),1,PAGE_READONLY,&old);c.native_fault();c.retire();
        check(*reinterpret_cast<std::uint8_t*>(address)==0,"failed blur cleanup is retained through resource retirement");
        const auto before=f.d.calls;f.begin(c);check(!c.selected()&&f.d.calls==before,"pending scalar prevents new resource transaction");
        DWORD ignored{};VirtualProtect(reinterpret_cast<void*>(address),1,old,&ignored);c.retire();check(*reinterpret_cast<std::uint8_t*>(address)==1,"same-scope retirement retries retained blur ownership");}
    {Fixture f;AuxiliaryHistoryCorrection c;f.begin(c);c.before_native(1);DWORD old{};const auto address=f.layout.slots[1].address+0x21fc;
        VirtualProtect(reinterpret_cast<void*>(address),1,PAGE_READONLY,&old);c.native_fault();c.retire();DWORD ignored{};VirtualProtect(reinterpret_cast<void*>(address),1,old,&ignored);
        ++f.epoch;f.begin(c);c.retire(f.epoch);
        check(!c.selected()&&*reinterpret_cast<std::uint8_t*>(address)==0,"new reset epoch forbids a late old-journal write despite identical native frame/tuple");
        f.m.write(address,std::uint8_t{1});c.retire(f.epoch);check(f.run(c),"confirmed native overwrite retires expired journal before a fresh seed");c.retire();}
    {Fixture f;AuxiliaryHistoryCorrection c;check(f.run(c),"warm scalar-fault seed");f.expected_previous=f.d.one.pixels;f.expect_cold=false;f.advance();f.begin(c);f.invoke(c,1);
        f.m.write(f.layout.buffer+0x16cc38,0u);c.before_native(0);DWORD old{};auto* address=reinterpret_cast<void*>(f.m.base+0x356a32c);
        VirtualProtect(address,4,PAGE_READONLY,&old);const auto releases=release_calls;c.native_fault();check(release_calls==releases,"failed warm timer cleanup on native fault never releases COM");c.retire();
        check(*static_cast<std::uint32_t*>(address)==f.time-16,"pending previous-time word is retained while page is not writable");
        DWORD ignored{};VirtualProtect(address,4,old,&ignored);c.retire();check(*static_cast<std::uint32_t*>(address)==f.time,"late same-scope retry restores the pre-left native time");}
    for(bool seh:{false,true}){Fixture f;AuxiliaryHistoryCorrection c;AuxiliaryHistoryCapture cap;AuxiliaryHistoryBudget budget;f.begin(c);
        cap.begin(true,budget,f.m.base,f.layout,f.key(),f.d.api(),f.current());c.before_native(1);f.native_seh=seh;f.native_cpp=!seh;
        const auto releases=release_calls;const auto before=f.d.calls;bool caught=false;
        if(seh)caught=catch_native_seh(&c,&f,&cap);
        else try{invoke_history_original(Fixture::native,&c,&cap,&f.faulted,&f.stop,reinterpret_cast<std::uint32_t*>(f.layout.buffer+0x16cc38),2);}catch(const std::runtime_error&){caught=true;}
        check(caught&&before==f.d.calls&&releases==release_calls,"owner-held optional C9 capture retains all resources throughout native unwind");
        cap.retire();c.retire();check(release_calls>releases,"normal owner retirement releases deferred correction and diagnostic resources");}
    {Fixture f;AuxiliaryHistoryCorrection c;f.begin(c);const auto before=f.d.calls;const auto releases=release_calls;
        std::thread foreign([&]{c.before_native(1);c.after_native(1);c.native_fault();c.retire();check(!c.finish(true),"foreign completion cannot claim owner transaction");});foreign.join();
        check(before==f.d.calls&&release_calls==releases&&c.selected(),"foreign methods leave owner state and COM untouched");c.retire();}
    for(unsigned scale:{1u,2u,3u,4u}){Fixture f(1280*scale,720*scale);AuxiliaryHistoryCorrection c;
        check(f.run(c),"full-sized native history target cold-seeds both actual eye regions");f.verify_pair();
        f.expected_previous=f.d.one.pixels;f.expect_cold=false;f.advance();
        check(f.run(c),"full-sized history warm transaction restores previous image and commits current two-eye result");f.verify_pair();c.retire();}
    for(auto size:std::array<std::array<unsigned,2>,4>{{{1280,721},{1920,1080},{2560,720},{3840,1080}}}){
        Fixture f(size[0],size[1]);AuxiliaryHistoryCorrection c;f.begin(c);
        check(!c.selected()&&f.d.calls["create"]==0&&f.d.calls["stretch"]==0,
            "unsupported scene shape is refused before history allocation or pixel mutation");c.retire();}
    {Fixture f(3840,2160);AuxiliaryHistoryCorrection c;++f.layout.slots[1].scene.x;f.begin(c);
        check(!c.selected()&&f.d.calls["create"]==0&&f.d.calls["stretch"]==0,"shifted high-resolution eye rectangle refuses history correction");c.retire();}
    check(!validate_history_correction_guards(nullptr),"fixture cannot enable retail native correction guards");
    std::printf("history_correction_checks=%u failures=%u real_gpu_calls=0 game_launches=0\n",checks,failures);return failures?1:0;
}
