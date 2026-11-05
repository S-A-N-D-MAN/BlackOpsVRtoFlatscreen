// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_resolution_policy.hpp"
#include <cstdio>
#include <cstdlib>
namespace r=bovr::native_resolution;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
int main(){
    r::Context c{r::clarity,r::clarity,{16384,16384},r::api_return,0x6b7c20,true,true,true,true,true,true};
    const r::Rect physical{0,0,2560,1440};
    auto d=r::plan(c,physical,40,true);
    check(d.allowed&&d.changed&&d.monitor==r::Rect{0,0,3840,2160},"native canvas expands before game allocations");
    for(auto caller:{0x6b7c20u,0x6b7ca5u,0x6b756fu}){c.helper_caller=caller;
        check(r::plan(c,physical,40,true).allowed,"custom parsing and both native cache updates agree");}
    check(r::requested("3840x2160")==r::clarity,"default exact requested preset admitted");
    check(r::requested("2560x1440")==r::recovery&&r::requested("5120x2880")==r::high,"lower and higher exact presets admitted");
    for(auto preset:{r::recovery,r::clarity,r::high}){
        auto selected=c;selected.requested=selected.native=preset;selected.caps=preset;
        const auto result=r::plan(selected,{0,0,1280,720},40,true);
        check(result.allowed&&result.monitor.right==static_cast<int>(preset.width)&&result.monitor.bottom==static_cast<int>(preset.height),
              "each preset reaches exact source size before engine allocation");
        --selected.caps.width;check(!r::plan(selected,physical,40,true).allowed,"each preset respects hardware allocation caps");
        selected.caps=preset;selected.native={1280,720};check(!r::plan(selected,physical,40,true).allowed,"each preset requires matching native profile");
    }
    for(auto text:{"","3840x2160 "," 3840x2160","3840x2160junk","3840X2160","4992x2688","3841x2160","0x0","-3840x2160","6400x3600"})
        check(r::requested(text)==r::Size{},"unknown oversized and ordinary settings retain stock behavior");
    for(bool r::Context::*flag:{&r::Context::guarded,&r::Context::native_values_stable,&r::Context::windowed,
            &r::Context::factory_matches,&r::Context::monitor_matches,&r::Context::stack_buffer_matches}){
        auto bad=c;bad.*flag=false;d=r::plan(bad,physical,40,true);
        check(!d.allowed&&!d.changed&&d.monitor==physical,"each native authority guard independently preserves physical rectangle");}
    for(auto caller:{0u,0x6b6880u,0x6b756eu,0x6b7c21u}){auto bad=c;bad.helper_caller=caller;
        check(!r::plan(bad,physical,40,true).allowed,"unrelated helper callers cannot change monitor dimensions");}
    auto bad=c;bad.api_caller++;check(!r::plan(bad,physical,40,true).allowed,"same helper through an unverified API call is stock");
    bad=c;bad.native=r::recovery;check(!r::plan(bad,physical,40,true).allowed,"native mode must match requested preset");
    bad=c;bad.requested={4992,2688};bad.native=bad.requested;check(!r::plan(bad,physical,40,true).allowed,"native runtime-sized request above validated ceiling rejected");
    bad=c;bad.caps.width=3839;check(!r::plan(bad,physical,40,true).allowed,"hardware width cap respected");
    bad=c;bad.caps.height=2159;check(!r::plan(bad,physical,40,true).allowed,"hardware height cap respected");
    bad=c;bad.caps={3840,2160};check(r::plan(bad,physical,40,true).allowed,"exact hardware cap accepted");
    for(unsigned size:{0u,39u,41u,72u,104u})check(!r::plan(c,physical,size,true).allowed,"only pinned stack MONITORINFO size is writable");
    check(!r::plan(c,physical,40,false).allowed,"failed original monitor API never becomes synthetic success");
    for(auto rect:{r::Rect{-2560,-1440,0,0},r::Rect{100,200,2660,1640},r::Rect{0,0,7680,4320},r::Rect{0,0,3840,1080}}){
        d=r::plan(c,rect,40,true);check(d.allowed&&d.monitor.left==rect.left&&d.monitor.top==rect.top,"monitor origin stays physical");
        check(d.monitor.right>=rect.right&&d.monitor.bottom>=rect.bottom,"render allowance never shrinks actual monitor");
    }
    d=r::plan(c,{0,0,7680,4320},40,true);check(d.allowed&&!d.changed,"already large monitor remains byte-identical");
    for(auto rect:{r::Rect{1,0,1,1},r::Rect{0,1,1,1},r::Rect{5,5,0,0},
        r::Rect{2147483646,0,2147483647,1},r::Rect{0,2147483646,1,2147483647}})
        check(!r::plan(c,rect,40,true).allowed,"invalid dimensions or signed rectangle overflow preserve original");
    std::printf("PASS %u native resolution policy checks\n",checks);
}
