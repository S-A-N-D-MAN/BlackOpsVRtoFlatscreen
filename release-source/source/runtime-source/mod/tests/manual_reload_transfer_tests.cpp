// SPDX-License-Identifier: GPL-3.0-only
#include "t5/manual_reload_transfer.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
namespace r=bovr::t5::reload_transfer;
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
int main(){
    for(int cap:{1,2,5,30,100,999,4096})for(int clip:{0,cap/2,cap})for(int reserve:{0,1,3,5000})for(int add:{0,1,2}){
        r::AmmoValues before{clip,reserve,cap,add,0,1,false};
        int expected=std::min(cap-clip,reserve);if(add&&add<cap)expected=std::min(expected,add);
        auto p=r::plan(before,static_cast<unsigned>(expected));check(p.allowed==(expected>0),"native bounded magazine increment");
        if(p.allowed){auto after=before;after.clip+=expected;after.reserve-=expected;
            check(r::completed(p,before,after),"exact conserved native transfer accepted");
            ++after.clip;check(!r::completed(p,before,after),"extra transferred round rejected");
            after=before;after.clip+=expected;check(!r::completed(p,before,after),"unwithdrawn reserve rejected");}
        check(!r::plan(before,static_cast<unsigned>(expected+1)).allowed,"requested amount must match unmodified native helper");
    }
    {
        r::AmmoValues olympia{0,10,2,0,0};check(!r::plan(olympia,1).allowed&&r::plan(olympia,2).allowed,"Olympia uses native paired-shell batch");
        r::AmmoValues mosin{1,10,5,0,0};check(!r::plan(mosin,1).allowed&&r::plan(mosin,4).allowed,"Mosin native clip fills missing rounds");
        r::AmmoValues python{2,10,6,1,0};check(r::plan(python,1).allowed&&!r::plan(python,4).allowed,"segmented cylinder loads one round");
    }
    {
        r::AmmoValues a{99,100,100,0,0,100,false};check(!r::plan(a,1).allowed,"native no-partial magazine needs empty clip");
        a.clip=0;check(r::plan(a,100).allowed,"empty no-partial magazine admitted");
        a={5,10,6,2,0,2,false};check(!r::plan(a,1).allowed,"native no-partial batch cannot top off below increment");
        a.clip=4;check(r::plan(a,2).allowed,"native no-partial batch admits exact missing increment");
    }
    for(int clip:{0,1,80,100})for(int reserve:{1,50,100,200}){
        r::AmmoValues before{clip,reserve,100,0,0,1,true};const unsigned amount=static_cast<unsigned>(std::min(100,reserve));
        const auto p=r::plan(before,amount);check(p.allowed&&p.discarded==static_cast<unsigned>(clip),"fuel exchange explicitly accounts for discarded tank");
        auto after=before;after.clip=static_cast<int>(amount);after.reserve-=static_cast<int>(amount);
        check(r::completed(p,before,after),"native tank exchange amount and reserve validated");
        after.clip+=clip?clip:1;check(!r::completed(p,before,after),"old fuel cannot be added to replacement tank");
    }
    for(int state:{-1,9,10,11,12,13,14,15,18,100})
        check(!r::plan({0,30,30,0,state},30).allowed,"unsupported native states never reach transfer");
    check(!r::plan({0,30,4097,0,0},30).allowed,"capacity upper bound enforced");
    check(!r::plan({0,100001,30,0,0},30).allowed,"reserve upper bound enforced");
    check(!r::plan({0,30,30,-1,0},30).allowed,"negative native increment rejected");
    std::printf("%u native reload transfer checks passed\n",checks);
}
