// SPDX-License-Identifier: GPL-3.0-only
#include "t5/ui_production_epoch.hpp"
#include <cstdio>
#include <cstdlib>
using namespace bovr::t5;
namespace {unsigned checks{};void check(bool pass,const char* why){++checks;if(!pass){std::printf("FAIL %s\n",why);std::exit(1);}}}
int main(){
    UiProductionEpoch ledger(0x3b40100);NativeUiProduction begin{};
    begin.call_serial=1;begin.thread_id=123;begin.frontend=0x3b40100;begin.command_array=0x3b37090;
    begin.bytes=0x100000;begin.native_frame=4;begin.used_total=16;begin.capacity=0x18000;begin.configured_max_views=1;
    begin.connection_state=3;begin.key_catchers=16;begin.menu_count=1;begin.page={0x200000,1,2};begin.valid=true;
    auto end=begin;end.used_total=120;end.commands=begin.bytes+16;end.completed_range=true;
    NativeFinalUi backend{begin.frontend,end.commands,begin.native_frame,3,16,1,true,begin.page};UiProductionStamp stamp{};
    check(!ledger.match(backend,1,&stamp),"backend cannot mint a missing frontend production stamp");
    ledger.begin(begin,1);check(ledger.complete(begin,end,1),"same producer completes stamped UI range");
    check(ledger.match(backend,1,&stamp)&&stamp.epoch==1,"backend receives exact producer revision and epoch");
    // Reproduce V8 failure: queue was built before an action; backend begins
    // afterward and sees the same native page pointer/catcher values.
    check(!ledger.match(backend,2,&stamp)&&!stamp.valid,"queued old list cannot inherit newer backend revision even on same native page");
    ++begin.call_serial;++begin.native_frame;end=begin;end.used_total=120;end.commands=begin.bytes+16;end.completed_range=true;
    ledger.begin(begin,2);check(ledger.complete(begin,end,2),"new post-action production establishes new epoch");
    check(!ledger.match(backend,2,&stamp),"prior native frame cannot use reused buffer stamp");
    backend.native_frame=begin.native_frame;
    check(ledger.match(backend,2,&stamp)&&stamp.epoch==2,"fresh frame on same physical page now qualifies");
    ledger.begin(begin,2);check(!ledger.complete(begin,end,3),"action between begin and end invalidates production");
    check(!ledger.match(backend,3,&stamp),"failed completion leaves no interaction fallback");
    ledger.begin(begin,3);auto changed=end;changed.page.focused_menu+=4;
    check(!ledger.complete(begin,changed,3),"native page change during production invalidates");
    for(unsigned field=0;field<6;++field){ledger.begin(begin,3);changed=end;
        switch(field){case 0:++changed.command_array;break;case 1:++changed.bytes;break;case 2:++changed.native_frame;break;
            case 3:++changed.thread_id;break;case 4:changed.commands+=4;break;case 5:changed.completed_range=false;break;}
        check(!ledger.complete(begin,changed,3),"changed producer range/owner identity cannot complete");}
    ledger.begin(begin,3);check(ledger.complete(begin,end,3),"valid slot recovers explicitly");
    ledger.clear();check(!ledger.match(backend,3,&stamp),"reset removes previously submitted range");
    ledger.begin(begin,3);check(ledger.complete(begin,end,3)&&ledger.match(backend,3,&stamp)&&stamp.epoch>2,"new producer epoch remains monotonic after reset");
    const auto first=stamp;auto other=begin;other.frontend+=0x186300;other.command_array+=24;other.call_serial=40;
    auto other_end=other;other_end.used_total=120;other_end.commands=other.bytes+16;other_end.completed_range=true;
    ledger.begin(other,3);check(ledger.complete(other,other_end,3)&&ledger.match(backend,3,&stamp)&&stamp.epoch==first.epoch,
        "independent SMP slot cannot replace outstanding first-buffer provenance");
    std::printf("ui_production_epoch_checks=%u failures=0 native_calls=0\n",checks);
}
