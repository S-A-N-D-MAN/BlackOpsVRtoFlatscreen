// SPDX-License-Identifier: GPL-3.0-only
#include "render/eye_staging.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
namespace {
unsigned checks{},failures{};void check(bool result,const char* why){++checks;if(!result){++failures;std::printf("FAIL %s\n",why);}}
const std::array<RECT,2> rects{{{0,0,2,2},{2,0,4,2}}};
bool same_rect(const RECT& a,const RECT& b){return !std::memcmp(&a,&b,sizeof a);}
struct Surface {void** table{};unsigned refs{};D3DSURFACE_DESC desc{D3DFMT_X8R8G8B8,D3DRTYPE_SURFACE,0,D3DPOOL_DEFAULT,D3DMULTISAMPLE_NONE,0,4,2};std::array<unsigned,8> pixels{};};
ULONG __stdcall release(Surface* s){check(s->refs!=0,"COM release always has an owned reference");return --s->refs;}
HRESULT __stdcall describe(Surface* s,D3DSURFACE_DESC* d){*d=s->desc;return S_OK;}
struct Device {void** table{};Surface bb{},packed{};unsigned calls{},creates{};HRESULT copy_result=S_OK;std::vector<unsigned> copied;D3DVIEWPORT9 viewport{7,8,9,10,0,1};RECT scissor{11,12,13,14};unsigned current_target=22;};
HRESULT __stdcall backbuffer(Device* d,UINT swap,UINT index,D3DBACKBUFFER_TYPE kind,IDirect3DSurface9** out){check(swap==0&&index==0&&kind==D3DBACKBUFFER_TYPE_MONO,"fixed authoritative backbuffer only");++d->calls;++d->bb.refs;*out=reinterpret_cast<IDirect3DSurface9*>(&d->bb);return S_OK;}
HRESULT __stdcall create(Device* d,UINT width,UINT height,D3DFORMAT format,D3DMULTISAMPLE_TYPE multisample,DWORD quality,BOOL lockable,IDirect3DSurface9** out,HANDLE* shared){check(width==4&&height==2&&format==D3DFMT_X8R8G8B8&&multisample==D3DMULTISAMPLE_NONE&&!quality&&!lockable&&!shared,"matching owned packed target only");++d->calls;++d->creates;++d->packed.refs;*out=reinterpret_cast<IDirect3DSurface9*>(&d->packed);return S_OK;}
HRESULT __stdcall stretch(Device* d,IDirect3DSurface9* src,const RECT* sr,IDirect3DSurface9* dst,const RECT* dr,D3DTEXTUREFILTERTYPE filter){++d->calls;check(sr&&dr&&same_rect(*sr,*dr)&&filter==D3DTEXF_NONE,"copies retain exact eye rectangles without scaling");if(FAILED(d->copy_result))return d->copy_result;
    auto* from=reinterpret_cast<Surface*>(src);auto* to=reinterpret_cast<Surface*>(dst);check((from==&d->bb&&to==&d->packed)||(from==&d->packed&&to==&d->bb),"only private staging copy, no native auxiliary target mutation");d->copied.push_back(static_cast<unsigned>(sr->left/2));
    for(LONG y=sr->top;y<sr->bottom;++y)for(LONG x=sr->left;x<sr->right;++x)to->pixels[static_cast<unsigned>(y*4+x)]=from->pixels[static_cast<unsigned>(y*4+x)];return S_OK;}
struct Fixture {std::array<void*,119> dt{};std::array<void*,17> st{};Device d{};Fixture(){st[2]=reinterpret_cast<void*>(release);st[12]=reinterpret_cast<void*>(describe);dt[18]=reinterpret_cast<void*>(backbuffer);dt[28]=reinterpret_cast<void*>(create);dt[34]=reinterpret_cast<void*>(stretch);d.table=dt.data();d.bb.table=st.data();d.packed.table=st.data();}IDirect3DDevice9* device(){return reinterpret_cast<IDirect3DDevice9*>(&d);}};
}
int main(){using bovr::render::EyeStaging;
    for(const auto order:{std::array<unsigned,2>{0,1},std::array<unsigned,2>{1,0}}){Fixture f;EyeStaging staging;
        check(SUCCEEDED(staging.begin(f.device(),1,rects,order)),"each explicit order begins");const auto viewport=f.d.viewport;const auto scissor=f.d.scissor;const auto target=f.d.current_target;
        const auto calls=f.d.calls;check(FAILED(staging.save_eye(f.device(),1,order[1]))&&f.d.calls==calls,"wrong first eye rejected before COM");
        f.d.bb.pixels.fill(0xff112233);check(SUCCEEDED(staging.save_eye(f.device(),1,order[0])),"first produced eye copied");
        const auto after_first=f.d.calls;check(FAILED(staging.save_eye(f.device(),1,order[0]))&&f.d.calls==after_first,"duplicate eye rejected without native copy");
        f.d.bb.pixels.fill(0xffaabbcc);check(SUCCEEDED(staging.save_eye(f.device(),1,order[1])),"second produced eye copied after native full clear");
        check(staging.completed_eye_mask()==3&&f.d.copied==std::vector<unsigned>{order[0],order[1]},"both distinctive eyes have correct copy order");
        f.d.bb.pixels.fill(0);check(SUCCEEDED(staging.restore_pair(f.device(),1)),"full packed pair restored");
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<4;++x)check(f.d.bb.pixels[y*4+x]==((x/2)==order[0]?0xff112233u:0xffaabbccu),"distinct per-eye bytes preserved despite later native full clear");
        check(!std::memcmp(&viewport,&f.d.viewport,sizeof viewport)&&same_rect(scissor,f.d.scissor)&&f.d.current_target==target,"staging never mutates native target viewport or scissor");
        const auto consumed=f.d.calls;check(FAILED(staging.restore_pair(f.device(),1))&&f.d.calls==consumed,"pair is one-use");
        check(SUCCEEDED(staging.begin(f.device(),2,rects,order)),"next producer reuses compatible resource");unsigned foreign_calls=f.d.calls;std::thread foreign([&]{check(FAILED(staging.save_eye(f.device(),2,order[0])),"foreign thread rejected");});foreign.join();check(f.d.calls==foreign_calls,"foreign owner never calls native COM");
        f.d.copy_result=E_FAIL;check(FAILED(staging.save_eye(f.device(),2,order[0]))&&!staging.completed_eye_mask()&&FAILED(staging.restore_pair(f.device(),2)),"copy failure cannot retain prior pair");staging.release();check(!f.d.bb.refs&&!f.d.packed.refs&&f.d.creates==1,"all owned refs released; compatible staging reused");}
    Fixture f;EyeStaging staging;for(const auto invalid:{std::array<unsigned,2>{0,0},std::array<unsigned,2>{1,1},std::array<unsigned,2>{2,0},std::array<unsigned,2>{0,2}})check(FAILED(staging.begin(f.device(),1,rects,invalid))&&!f.d.calls,"invalid order fails before any native API");
    check(SUCCEEDED(staging.begin(f.device(),1,rects)),"default API retains ordinary order");check(FAILED(staging.save_eye(f.device(),1,1))&&SUCCEEDED(staging.save_eye(f.device(),1,0))&&SUCCEEDED(staging.save_eye(f.device(),1,1)),"default order remains left then right");staging.release();
    std::printf("eye_staging_order_checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
