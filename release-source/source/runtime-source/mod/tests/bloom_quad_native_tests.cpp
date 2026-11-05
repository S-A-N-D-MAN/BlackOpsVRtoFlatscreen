// SPDX-License-Identifier: GPL-3.0-only
// Exercises the actual protected reader over owned sparse pages. The injected
// copy seam is used only to place failures/mutations between its real copies.
#include "t5/bloom_quad_native.hpp"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <immintrin.h>
#include <vector>
using namespace bovr::t5;
static unsigned checks{},failures{};
static void check(bool ok,const char* text){++checks;if(!ok){++failures;if(failures<20)std::printf("FAIL %s\n",text);}}
namespace {
struct Memory {
    std::uintptr_t base{};std::vector<std::uintptr_t> pages;
    Memory(){base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,(auxiliary_read_extent+4095)&~std::size_t(4095),MEM_RESERVE,PAGE_NOACCESS));check(base&&base<=UINT32_MAX-auxiliary_read_extent,"owned bounded image reservation");}
    ~Memory(){if(base)VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);}
    template<class T>void put(std::uintptr_t p,const T& v){
        for(auto page=p&~std::uintptr_t(4095);page<p+sizeof v;page+=4096)if(std::find(pages.begin(),pages.end(),page)==pages.end()){
            check(VirtualAlloc(reinterpret_cast<void*>(page),4096,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"owned sparse commit");pages.push_back(page);}
        std::memcpy(reinterpret_cast<void*>(p),&v,sizeof v);
    }
};
std::uint32_t word(float f){return std::bit_cast<std::uint32_t>(f);}
// Volatile binary32 oracle, separate from production's explicit SSE intrinsics.
float fdiv(float a,float b){volatile float r=a/b;return r;}
float fmul(float a,float b){volatile float r=a*b;return r;}
float fadd(float a,float b){volatile float r=a+b;return r;}
std::array<std::uint32_t,16> identity_matrix(){std::array<std::uint32_t,16> value{};
    for(unsigned i=0;i<4;++i)value[5*i]=word(1.0f);return value;}
void authored_blur_arguments(BloomDrawSnapshot& n,unsigned role) {
    // The three type-3 tuples are the compatibility schema consumed by the
    // reader. Remaining rows are synthetic ignored argument classes.
    n.arguments[0]={3,0,0x040000db};
    n.arguments[1]={3,23,0x01000079};
    n.arguments[2]={3,36,0x0100007a};
    const unsigned count=role==5?6u:8u;
    for(unsigned i=3;i<count;++i)n.arguments[i]={
        static_cast<std::uint16_t>(4+(i-3)%4),
        static_cast<std::uint16_t>(80+i),
        0xa5000000u|(role<<8)|i};
    n.argument_counts={1,0,static_cast<std::uint8_t>(count-1)};
}
struct Fixture {
    Memory memory;BloomDrawSnapshot n{};BloomDrawCall d{};BloomQuadSnapshot expected{};
    std::uintptr_t source{},state{};
    void setup(unsigned slot=0,unsigned eye=1,unsigned width=160,unsigned height=90,unsigned role=2,unsigned scale=1,unsigned views=3){
        const auto b=memory.base;source=b+0x3cca570;state=b+0x417ee00;
        n={};n.buffer=static_cast<unsigned>(b+(slot?0x38c6400:0x3740100));n.native_frame=42;n.view_count=views;n.selected=postfx_native_index(views,eye);n.eye=eye;n.role=role;
        const auto va=static_cast<unsigned>(b+0x420f730+slot*0xe380);n.view=n.source_view=va+eye*0x38e0;
        n.source_mode=2;n.selector=role==3||role==8?0:1;n.width=width;n.height=height;n.target=n.selector?(role==0?25u:role==1?26u:27u):3;
        n.material=static_cast<unsigned>(b+0x10000);n.technique=n.material+0x100;n.pass=n.technique+8;n.pass_count=1;
        n.scene={1000,0x3f800000,0,0,0};n.scene_viewport={static_cast<int>(eye*640*scale),0,static_cast<int>(640*scale),static_cast<int>(720*scale)};
        n.native_target={0x20100,0x20200,0,static_cast<std::uint16_t>(width),static_cast<std::uint16_t>(height)};
        n.argument_counts={0,1,0};n.arguments[0]={3,0,0x040000db};
        if(role==5||role==6){
            authored_blur_arguments(n,role);n.target=role==5?29:28;
        }
        d={b+0x30b5d3,4,0,0,4,48,2};
        memory.put(b+0x747880,static_cast<unsigned>(source));memory.put(b+0x747884,static_cast<unsigned>(state));
        memory.put(b+0x3a5b5f8,n.buffer);memory.put(n.buffer+0x15d7a0,n.native_frame);memory.put(n.buffer+0x16cc38,n.selected);
        memory.put(n.buffer+0x16cc3c,n.view_count);memory.put(n.buffer+0x16cc40,va);memory.put(source+0x1a08,n.view);
        memory.put(n.view+0x18c,0u);memory.put(n.view+0x3620,0u);memory.put(n.view+0x150,n.scene_viewport);memory.put(n.view+0x170,n.scene);
        memory.put(source+0x1a4c,n.scene_viewport);memory.put(source+0x1a34,n.source_mode);memory.put(source+0x1a7c,n.selector);
        memory.put(source+0x1a80,n.width);memory.put(source+0x1a84,n.height);memory.put(source+0x1a38,n.scene);
        memory.put(state+0x1128,static_cast<std::uint8_t>(n.target));memory.put(state+0xc8,std::uint8_t{4});
        memory.put(state+0xc4,n.material);memory.put(state+0xcc,n.technique);memory.put(state+0xd0,n.pass);memory.put(state+0xd4,0u);
        expected={};
        const float u0=fmul(fdiv(1.f,static_cast<float>(width)),static_cast<float>(eye*640*scale));
        const float u1=fadd(fmul(fdiv(1.f,static_cast<float>(width)),640.f*scale),u0),v1=fmul(fdiv(1.f,static_cast<float>(height)),720.f*scale);
        for(unsigned i=0;i<4;++i){const std::array<unsigned,8> v{(i==1||i==2)?word(640.f*scale):0u,i>=2?word(720.f*scale):0u,0,word(1),0xffffffff,
             (i==1||i==2)?word(u1):word(u0),i>=2?word(v1):0u,0x3ffe7f7f};std::copy(v.begin(),v.end(),expected.vertices.begin()+8*i);}
        expected.indices={3,0,2,2,0,1};memory.put(b+0x3a5e908,expected.vertices);memory.put(b+0x3a89248,expected.indices);
        memory.put(b+0x3c89254,4u);memory.put(b+0x3c89250,6u);memory.put(b+0x420c124,0u);
        memory.put(source+0x1a0c,0u);memory.put(source+0x19f0,std::array<unsigned,4>{0,0,0,word(1)});
        const auto identity=identity_matrix();memory.put(source,identity);memory.put(source+0x1690,identity);
        const float iw=fdiv(1.f,static_cast<float>(n.selector?width:640*scale)),ih=fdiv(1.f,static_cast<float>(n.selector?height:720*scale));
        std::array<unsigned,16> p{};p[0]=word(fmul(iw,2));p[5]=word(fmul(ih,-2));p[12]=word(fadd(-1,-iw));p[13]=word(fadd(ih,1));p[14]=p[15]=word(1);
        memory.put(source+0x16d0,p);memory.put(source+0x1710,p);memory.put(source+0x500,p);
        for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)expected.matrix[4*i+j]=p[4*j+i];memory.put(source+0x580,expected.matrix);
        for(unsigned off:{0x199au,0x19dau,0x19c2u,0x19e4u,0x19c6u})memory.put(source+off,std::uint16_t{7});
        memory.put(state+0xe8,std::array<unsigned,8>{7,0x040000db,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX});
        if(role==5||role==6){
            expected.uv_constants={{{word(.01f),word(.02f),word(.03f),word(.04f)},
                                    {word(.05f),word(.06f),word(.07f),word(.08f)}}};
            expected.uv_versions={13,29};expected.uv_bind_cache={{{13,0x01000079},{29,0x0100007a}}};
            memory.put(source+0xf90,expected.uv_constants[0]);memory.put(source+0xfa0,expected.uv_constants[1]);
            memory.put(source+0x1902,expected.uv_versions[0]);memory.put(source+0x1904,expected.uv_versions[1]);
            memory.put(state+0x1a0,expected.uv_bind_cache[0]);memory.put(state+0x208,expected.uv_bind_cache[1]);
        }
        memory.put(b+0x3d70760,static_cast<unsigned>(b+0x3d70750));memory.put(b+0x3d70728,static_cast<unsigned>(b+0x3d70718));
        memory.put(b+0x3d70750,std::array<unsigned,4>{384,4096,0x12340000,0});memory.put(b+0x3d70718,std::array<unsigned,4>{54,4096,0x12350000,0});
        memory.put(state+0x90,0x12330000u);memory.put(state+0x94,0x12350000u);memory.put(state+0x98,2u);
        memory.put(state+0x9c,std::array<unsigned,9>{32,0x12340000,256,0,0,0,0,0,0});memory.put(state+0xc0,0x12360000u);
    }
    bool read(BloomQuadSnapshot* q,BloomQuadStatus* why=nullptr){return read_bloom_quad(memory.base,n,d,q,why);}
};
bool zero(const BloomQuadSnapshot& q){return q==BloomQuadSnapshot{};}
struct CopyControl {
    unsigned calls{},fail_at{},mutate_at{};std::uintptr_t mutation{};unsigned replacement{};std::array<std::pair<std::uintptr_t,std::size_t>,256> reads{};
    static bool copy(void* raw,std::uintptr_t p,void* out,std::size_t bytes) noexcept {
        auto& c=*static_cast<CopyControl*>(raw);++c.calls;
        if(c.fail_at==c.calls)return false;
        std::memcpy(out,reinterpret_cast<const void*>(p),bytes);
        if(!c.fail_at&&!c.mutate_at&&c.calls<=c.reads.size())c.reads[c.calls-1]={p,bytes};
        if(c.mutate_at==c.calls)std::memcpy(reinterpret_cast<void*>(c.mutation),&c.replacement,sizeof(c.replacement));
        SetLastError(0x98765432);return true;
    }
};
alignas(16) unsigned char fp_before[512],fp_after[512];
Fixture* fp_fixture{};BloomQuadSnapshot fp_output{};bool fp_result{};
bool __cdecl fp_read(){return fp_fixture->read(&fp_output);}
__declspec(naked) void fp_call(){__asm {
    fxsave fp_before
    call fp_read
    mov fp_result,al
    fxsave fp_after
    ret
}}
void test_fp(Fixture& f){
    fp_fixture=&f;const auto saved=_mm_getcsr();_mm_setcsr(0x1fa1);SetLastError(0x76543210);
    fp_call();check(fp_result,"nonzero FP status does not alter admitted control");
    check(std::memcmp(fp_before,fp_after,160)==0&&std::memcmp(fp_before+160,fp_after+160,128)==0,"actual reader full x87/control/XMM image preservation");
    check(GetLastError()==0x76543210,"actual reader LastError preserved");
    for(unsigned mask:{0x0040u,0x2000u,0x4000u,0x6000u,0x8000u}){
        _mm_setcsr(0x1f80|mask);BloomQuadSnapshot q{};check(!f.read(&q)&&zero(q),"non-default rounding/DAZ/FTZ refused");check(_mm_getcsr()==(0x1f80|mask),"refusal preserves FP control");}
    _mm_setcsr(saved);
}
void test_blur(Fixture& f){
    for(unsigned role:{5u,6u})for(unsigned slot=0;slot<2;++slot)for(unsigned eye=0;eye<2;++eye){
        f.setup(slot,eye,160,90,role);BloomQuadSnapshot q{};BloomQuadStatus why{};
        check(f.read(&q,&why)&&why==BloomQuadStatus::accepted,"actual protected blur reader accepts both roles/stores/eyes with authored argument shapes");
        check(q.uv_constants==f.expected.uv_constants&&q.uv_versions==f.expected.uv_versions&&q.uv_bind_cache==f.expected.uv_bind_cache,"all raw blur rows/version/cache values copied exactly");
        check(q.matrix==f.expected.matrix&&q.matrix_bind_cache==std::array<unsigned,8>{7,0x040000db,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX},"c23/c36 cache does not alter exact c0..3 matrix cache");
        test_fp(f);
        const auto native=f.n;
        for(unsigned which=0;which<3;++which)for(unsigned bit=0;bit<32;++bit){f.n=native;f.n.arguments[which].value^=1u<<bit;
            check(!f.read(&q)&&zero(q),"each bit of the three type3 tuples remains exact");}
        for(unsigned which=0;which<3;++which){
            f.n=native;const unsigned count=f.n.argument_counts[0]+f.n.argument_counts[1]+f.n.argument_counts[2];
            for(unsigned i=which;i+1<count;++i)f.n.arguments[i]=f.n.arguments[i+1];f.n.arguments[count-1]={};--f.n.argument_counts[2];
            check(!f.read(&q)&&zero(q),"missing matrix or either blur tuple refused");
            f.n=native;f.n.arguments[count]=f.n.arguments[which];++f.n.argument_counts[2];
            check(!f.read(&q)&&zero(q),"duplicate matrix or either blur tuple refused");
            f.n=native;f.n.arguments[which].destination=which?0:23;
            check(!f.read(&q)&&zero(q),"overlapping matrix/blur register tuple refused");
        }
        f.n=native;f.n.arguments[6]={3,24,0x01000079};f.n.argument_counts={0,7,0};
        check(!f.read(&q)&&zero(q),"unknown additional type3 refused");
        for(unsigned other=0;other<13;++other)if(other!=5&&other!=6){f.n=native;f.n.role=other;
            check(!f.read(&q)&&zero(q),"cross-role blur arguments refused");}
        f.n=native;
        const std::array<std::uintptr_t,6> header{f.source+0x1902,f.source+0x1904,f.state+0x1a0,f.state+0x1a4,f.state+0x208,f.state+0x20c};
        for(auto address:header){unsigned saved{};std::memcpy(&saved,reinterpret_cast<void*>(address),4);f.memory.put(address,saved^1);
            check(!f.read(&q)&&zero(q),"stale version or mismatched packed/native destination cache refused");f.memory.put(address,saved);}
        CopyControl baseline;check(read_bloom_quad_copied(f.memory.base,f.n,f.d,&q,{&baseline,&CopyControl::copy}),"actual blur copied seam control");
        check(baseline.calls<baseline.reads.size(),"new fixed blur copy count stays bounded");
        for(unsigned nth=1;nth<=baseline.calls;++nth){CopyControl c;c.fail_at=nth;
            check(!read_bloom_quad_copied(f.memory.base,f.n,f.d,&q,{&c,&CopyControl::copy})&&zero(q),"every blur header/full-data copy failure zeros output");}
        for(unsigned row=0;row<2;++row){const auto start=f.source+(row?0xfa0:0xf90);unsigned first{};
            for(unsigned i=0;i<baseline.calls;++i)if(baseline.reads[i].first==start){first=i+1;break;}
            check(first!=0,"both raw float4 rows actually read");
            for(unsigned word_index=0;word_index<4;++word_index){const auto address=start+word_index*4;unsigned saved{};std::memcpy(&saved,reinterpret_cast<void*>(address),4);
                CopyControl c;c.mutate_at=first;c.mutation=address;c.replacement=saved^1;
                check(!read_bloom_quad_copied(f.memory.base,f.n,f.d,&q,{&c,&CopyControl::copy})&&zero(q),"same-version raw float4 intercopy mutation refused");f.memory.put(address,saved);}
            for(auto address:header){unsigned saved{};std::memcpy(&saved,reinterpret_cast<void*>(address),4);CopyControl c;c.mutate_at=first;c.mutation=address;c.replacement=saved^1;
                check(!read_bloom_quad_copied(f.memory.base,f.n,f.d,&q,{&c,&CopyControl::copy})&&zero(q),"post-data version/cache mutation caught by late header");f.memory.put(address,saved);}
        }
        // Raw identity only: the native binder has no finite-value predicate.
        // No arithmetic or float equality is applied to these copied rows.
        const std::array<unsigned,4> special{0x00000000,0x80000000,0x7fc01234,0xff800000};
        for(unsigned row=0;row<2;++row){f.memory.put(f.source+(row?0xfa0:0xf90),special);}
        check(f.read(&q)&&q.uv_constants[0]==special&&q.uv_constants[1]==special,"signed zero NaN payload infinity preserve exact bits without normalization");
    }
    // A nonblur read must neither inherit nor access the previously populated
    // blur values in the same owned address space.
    f.setup();BloomQuadSnapshot q{};CopyControl c;
    check(read_bloom_quad_copied(f.memory.base,f.n,f.d,&q,{&c,&CopyControl::copy}),"ordinary role remains admitted after blur fixture reuse");
    check(q.uv_constants==decltype(q.uv_constants){}&&q.uv_versions==decltype(q.uv_versions){}&&q.uv_bind_cache==decltype(q.uv_bind_cache){},"nonblur output rows and metadata remain zero");
    bool unused=true;for(unsigned i=0;i<c.calls;++i)for(auto p:{f.source+0xf90,f.source+0xfa0,f.source+0x1902,f.source+0x1904,f.state+0x1a0,f.state+0x208})if(c.reads[i].first==p)unused=false;
    check(unused,"nonblur path introduces no extra native reads");
}
}
int main(){
    const auto old=_mm_getcsr();_mm_setcsr(0x1f80);Fixture f;
    if(!f.memory.base)return 1;
    test_blur(f);
    for(unsigned slot=0;slot<2;++slot)for(unsigned eye=0;eye<2;++eye)for(const auto dims:std::array<std::array<unsigned,3>,4>{{{160,90,2},{320,180,1},{640,360,0},{1280,720,3}}}){
        f.setup(slot,eye,dims[0],dims[1],dims[2]);BloomQuadSnapshot q{};BloomQuadStatus why{};
        check(f.read(&q,&why)&&why==BloomQuadStatus::accepted,"both SMP stores/eyes and all native size classes admitted");
        check(q.vertices==f.expected.vertices&&q.indices==f.expected.indices&&q.matrix==f.expected.matrix,"all actual copied geometry/matrix bytes exact");
        check(q.stream_buffer==0x12340000&&q.stream_offset==256&&q.stream_stride==32&&q.index_buffer==0x12350000&&q.declaration==0x12360000,"native binding identities copied without COM calls");
    }
    for(unsigned views:{2u,3u})for(unsigned scale:{1u,2u,3u})for(unsigned slot:{0u,1u})for(unsigned eye:{0u,1u})
        for(const auto dims:std::array<std::array<unsigned,3>,6>{{{160,90,2},{320,180,1},{640,360,0},{1280,720,3},{160,90,5},{160,90,6}}}){
            f.setup(slot,eye,dims[0]*scale,dims[1]*scale,dims[2],scale,views);BloomQuadSnapshot resized{};
            check(f.read(&resized)&&resized.vertices==f.expected.vertices&&resized.matrix==f.expected.matrix,
                "scaled exact native vertex/matrix recipe accepted for both stores and eyes");
            const auto target=f.n.target;f.n.target=target==25?26:25;
            check(!f.read(&resized)&&zero(resized),"wrong target pyramid level cannot borrow another size");f.n.target=target;
            f.memory.put(f.n.buffer+0x16cc3c,views==2?3u:2u);
            check(!f.read(&resized)&&zero(resized),"current native view-count mutation cannot borrow an accepted draw snapshot");f.memory.put(f.n.buffer+0x16cc3c,views);
            if(eye){f.memory.put(f.n.buffer+0x16cc38,views==2?2u:1u);
                check(!f.read(&resized)&&zero(resized),"ordinary and auxiliary right-eye native indices are not interchangeable");f.memory.put(f.n.buffer+0x16cc38,f.n.selected);}
            ++f.n.scene_viewport.x;check(!f.read(&resized)&&zero(resized),"shifted scaled eye viewport fails closed");
        }
    f.setup(0,1,240,135,2,4);{BloomQuadSnapshot unsupported{};check(!f.read(&unsupported)&&zero(unsupported),"unknown scene resolution rejected");}
    f.setup();test_fp(f);
    // Every individual CPU vertex bit affects the selected native recipe.
    for(unsigned i=0;i<32;++i)for(unsigned bit=0;bit<32;++bit){auto v=f.expected.vertices;v[i]^=1u<<bit;f.memory.put(f.memory.base+0x3a5e908,v);BloomQuadSnapshot q{};check(!f.read(&q)&&zero(q),"each vertex bit mutation refused");}
    f.memory.put(f.memory.base+0x3a5e908,f.expected.vertices);
    for(unsigned i=0;i<6;++i)for(unsigned bit=0;bit<16;++bit){auto v=f.expected.indices;v[i]^=static_cast<std::uint16_t>(1u<<bit);f.memory.put(f.memory.base+0x3a89248,v);BloomQuadSnapshot q{};check(!f.read(&q)&&zero(q),"each index bit mutation refused");}
    f.setup();
    for(unsigned off:{0u,0x1690u,0x16d0u,0x1710u,0x500u,0x580u})for(unsigned i=0;i<16;++i){unsigned original{};std::memcpy(&original,reinterpret_cast<void*>(f.source+off+4*i),4);f.memory.put(f.source+off+4*i,original^1);BloomQuadSnapshot q{};check(!f.read(&q)&&zero(q),"every matrix word mutation refused");f.memory.put(f.source+off+4*i,original);}
    const std::vector<std::pair<std::uintptr_t,unsigned>> bad{
        {f.memory.base+0x747880,0},{f.memory.base+0x747884,0},{f.memory.base+0x3a5b5f8,0},{f.n.buffer+0x16cc40,0xfffffff0},
        {f.n.buffer+0x15d7a0,43},{f.n.buffer+0x16cc38,0},{f.n.buffer+0x16cc3c,2},{f.source+0x1a08,static_cast<unsigned>(f.n.view+0x38e0)},
        {f.n.view+0x18c,1},{f.n.view+0x3620,1},{f.source+0x1a38,1001},{f.source+0x1a4c,0},{f.source+0x1a34,1},{f.source+0x1a7c,0},{f.source+0x1a80,161},
        {f.state+0xc4,0},{f.state+0xcc,0},{f.state+0xd0,0},{f.state+0xd4,1},{f.memory.base+0x3c89254,5},{f.memory.base+0x3c89250,9},{f.memory.base+0x420c124,1},
        {f.source+0x1a0c,2},{f.source+0x19f0,1},{f.state+0xe8,8},{f.state+0xec,0x040000da},{f.state+0xf0,0},
        {f.memory.base+0x3d70760,0xfffffff0},{f.memory.base+0x3d70728,0},{f.memory.base+0x3d70750,383},{f.memory.base+0x3d70754,383},
        {f.memory.base+0x3d70758,1},{f.memory.base+0x3d7075c,1},{f.memory.base+0x3d70718,53},{f.memory.base+0x3d7071c,53},
        {f.state+0x90,0},{f.state+0x94,1},{f.state+0x9c,24},{f.state+0xa0,1},{f.state+0xa4,0xffffff80},{f.state+0xa8,1},{f.state+0xac,1},{f.state+0xb0,1},{f.state+0xb4,1},{f.state+0xb8,1},{f.state+0xbc,1},{f.state+0xc0,0}};
    for(const auto& [p,v]:bad){unsigned original{};std::memcpy(&original,reinterpret_cast<void*>(p),4);f.memory.put(p,v);BloomQuadSnapshot q{};check(!f.read(&q)&&zero(q),"current header/pool/binding predicate rejected");f.memory.put(p,original);}
    for(unsigned off:{0x199au,0x19dau,0x19c2u,0x19e4u,0x19c6u}){f.memory.put(f.source+off,std::uint16_t{8});BloomQuadSnapshot q{};check(!f.read(&q)&&zero(q),"stale native matrix version refused");f.memory.put(f.source+off,std::uint16_t{7});}
    const auto good_d=f.d;
    for(unsigned i=0;i<7;++i){auto* words=reinterpret_cast<unsigned*>(&f.d);words[i]^=1;BloomQuadSnapshot q{};check(!f.read(&q)&&zero(q),"all seven original call identities constrained");f.d=good_d;}
    for(unsigned i=0;i<64;++i){const auto saved=f.n.arguments;f.n.arguments[0].value^=1u<<(i%32);BloomQuadSnapshot q{};check(!f.read(&q)&&zero(q),"matrix source argument exact");f.n.arguments=saved;}
    CopyControl baseline;BloomQuadSnapshot q{};check(read_bloom_quad_copied(f.memory.base,f.n,f.d,&q,{&baseline,&CopyControl::copy}),"same kernel copied seam admits exact fixture");
    const auto count=baseline.calls;check(count>100&&count<256,"fixed bounded copy count");
    for(unsigned nth=1;nth<=count;++nth){CopyControl c;c.fail_at=nth;check(!read_bloom_quad_copied(f.memory.base,f.n,f.d,&q,{&c,&CopyControl::copy})&&zero(q),"every copy failure clears output");}
    // Mutating a header after the geometry copy must be caught by its late
    // header reread, before any success can leave the real reader.
    unsigned final_frame_copy{};for(unsigned i=0;i<count;++i)if(baseline.reads[i].first==f.n.buffer+0x15d7a0)final_frame_copy=i+1;
    for(unsigned nth=1;nth<final_frame_copy;++nth){CopyControl c;c.mutate_at=nth;c.mutation=f.n.buffer+0x15d7a0;c.replacement=43;
        check(!read_bloom_quad_copied(f.memory.base,f.n,f.d,&q,{&c,&CopyControl::copy})&&zero(q),"mid-read frame mutation rejected at subsequent protected header");f.memory.put(c.mutation,42u);}
    unsigned first_vertices{};for(unsigned i=0;i<count;++i)if(baseline.reads[i].first==f.memory.base+0x3a5e908){first_vertices=i+1;break;}
    for(const auto& [p,v]:std::array<std::pair<std::uintptr_t,unsigned>,4>{{{f.state+0x90,0x12330001},{f.memory.base+0x3c89254,5},{f.memory.base+0x420c124,1},{f.state+0xc0,0x12360001}}}){
        unsigned original{};std::memcpy(&original,reinterpret_cast<const void*>(p),4);CopyControl c;c.mutate_at=first_vertices;c.mutation=p;c.replacement=v;BloomQuadStatus why{};
        check(!read_bloom_quad_copied(f.memory.base,f.n,f.d,&q,{&c,&CopyControl::copy},&why)&&zero(q)&&why==BloomQuadStatus::changed,
              "late header specifically rejects post-copy device/count/pixelcost/declaration mutation");f.memory.put(p,original);
    }
    for(auto p:{f.memory.base+0x3a5e908,f.memory.base+0x3a89248,f.source+0x580}){
        DWORD protection{};const auto page=p&~std::uintptr_t(4095);check(VirtualProtect(reinterpret_cast<void*>(page),4096,PAGE_NOACCESS,&protection)!=0,"owned protected copy negative");
        check(!f.read(&q)&&zero(q),"actual protected reader contains inaccessible-page fault");DWORD ignored{};VirtualProtect(reinterpret_cast<void*>(page),4096,protection,&ignored);
    }
    check(!read_bloom_quad(0,f.n,f.d,&q)&&zero(q),"zero image refused");check(!read_bloom_quad(UINT32_MAX,f.n,f.d,&q)&&zero(q),"overflow image refused");
    check(!read_bloom_quad(f.memory.base,f.n,f.d,nullptr),"missing output refused");check(!validate_bloom_quad_guards(nullptr),"null native guard base refused");
    _mm_setcsr(old);std::printf("bloom_quad_native_tests: checks=%u failures=%u protected_reader=1 copied_mutation_seam=1 native_calls=0 gpu_calls=0\n",checks,failures);return failures?1:0;
}
