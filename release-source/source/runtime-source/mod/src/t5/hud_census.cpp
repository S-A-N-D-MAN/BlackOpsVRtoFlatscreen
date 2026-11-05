// SPDX-License-Identifier: GPL-3.0-only
#include "t5/hud_census.hpp"
#include "render/hud_commands.hpp"
#include "loader/trace.hpp"
#include <cstring>
#include <cstdio>
#include <climits>
#include <vector>
namespace bovr::t5 {
namespace {
struct Array {
    std::uint32_t bytes{};
    std::int32_t used{},critical{};
    std::uint32_t last{};
    std::int32_t capacity{},warning{};
    friend bool operator==(const Array&,const Array&)=default;
};
static_assert(sizeof(Array)==24);
struct Snapshot {
    std::uintptr_t array{},expected_array{},observed_backend{},observed_commands{};
    std::uintptr_t view_array{},view{};
    std::uint32_t view_count{};
    std::int32_t scene_viewport[4]{},display_viewport[4]{};
    Array values{};int configured_views{};std::uint32_t frame{};
    // Diagnostic stage only: 1 backend range, 2 owner identity, 3 bounded
    // metadata read/SEH, 4 allocation/range shape. Zero means accepted.
    unsigned rejection_stage{};
};
constexpr unsigned shared_list=UINT_MAX;
bool snapshot(std::uintptr_t base,const NativeFinalUi& ui,Snapshot* out,unsigned view_index=shared_list) noexcept {
    if(!out) return false;
    Snapshot s{};*out=s;
    __try {
        const auto first=base+0x3740100;
        s.rejection_stage=1;
        if(ui.backend!=first&&ui.backend!=first+0x186300) {*out=s;return false;}
        const auto slot=ui.backend==first?0u:1u;
        s.expected_array=base+0x3737090+slot*24;s.rejection_stage=2;
        std::memcpy(&s.observed_backend,reinterpret_cast<void*>(base+0x3a5b5f8),4);
        std::memcpy(&s.observed_commands,reinterpret_cast<void*>(ui.backend+0x16cd24),4);
        std::memcpy(&s.frame,reinterpret_cast<void*>(ui.backend+0x15d7a0),4);
        std::memcpy(&s.array,reinterpret_cast<void*>(ui.backend+0x16cc34),4);
        if(s.observed_backend!=ui.backend||s.observed_commands!=ui.commands||s.frame!=ui.native_frame||s.array!=s.expected_array) {*out=s;return false;}
        if(view_index!=shared_list) {
            std::memcpy(&s.view_count,reinterpret_cast<void*>(ui.backend+0x16cc3c),4);
            std::memcpy(&s.view_array,reinterpret_cast<void*>(ui.backend+0x16cc40),4);
            if(s.view_count>4||view_index>=s.view_count||s.view_array!=base+0x420f730+slot*0xe380) {*out=s;return false;}
            s.view=s.view_array+view_index*0x38e0;
            std::memcpy(&s.observed_commands,reinterpret_cast<void*>(s.view+0x2220),4);
            std::memcpy(s.scene_viewport,reinterpret_cast<void*>(s.view+0x140),16);
            std::memcpy(s.display_viewport,reinterpret_cast<void*>(s.view+0x150),16);
        }
        s.rejection_stage=3;
        std::memcpy(&s.values,reinterpret_cast<void*>(s.array),24);
        std::memcpy(&s.configured_views,reinterpret_cast<void*>(base+0x356a480),4);
    } __except(EXCEPTION_EXECUTE_HANDLER) {*out=s;return false;}
    const auto& a=s.values;
    // Explicit diagnostic copy ceiling, independent of the native allocation.
    // Native capacity must still equal its actual configured allocation formula.
    s.rejection_stage=4;
    if(s.configured_views<1||s.configured_views>16||a.capacity!=s.configured_views*0x18000||
       a.used<0||a.critical<0||a.critical>a.used||a.used>a.capacity||a.warning<0||a.warning>a.capacity||!a.bytes||
       (a.bytes&3)||(s.observed_commands&3)||s.observed_commands<a.bytes||s.observed_commands-a.bytes>static_cast<std::uint32_t>(a.capacity-4)||
       static_cast<std::uint64_t>(a.bytes)+static_cast<std::uint32_t>(a.capacity)>UINT32_MAX) {*out=s;return false;}
    s.rejection_stage=0;
    *out=s;return true;
}
bool copy_commands(std::uintptr_t address,void* output,std::size_t bytes) noexcept {
    __try {std::memcpy(output,reinterpret_cast<void*>(address),bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
unsigned view_count_at(std::uintptr_t base,const NativeFinalUi& ui) noexcept {
    __try {
        if(ui.backend!=base+0x3740100&&ui.backend!=base+0x38c6400) return 0;
        const auto count=*reinterpret_cast<std::uint32_t*>(ui.backend+0x16cc3c);
        return count<=4?count:0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}
void observe_one(std::uintptr_t base,const NativeFinalUi& ui,unsigned long serial,unsigned view_index) noexcept {
    const char* kind=view_index==shared_list?"shared_final":"client_view";
    try {
        Snapshot before{},after{};
        if(!snapshot(base,ui,&before,view_index)) {
            trace("t5_hud_census_rejected","call=%lu kind=%s view_index=%u view=0x%lx reason=owned_command_array_metadata stage=%u backend=0x%lx observed_backend=0x%lx native_frame=%lu observed_frame=%lu commands=0x%lx observed_commands=0x%lx array=0x%lx expected_array=0x%lx bytes=0x%x used=%d critical=%d last=0x%x capacity=%d warning=%d views=%d command_copy_attempted=0 native_writes=0",
                  serial,kind,view_index,static_cast<unsigned long>(before.view),before.rejection_stage,static_cast<unsigned long>(ui.backend),static_cast<unsigned long>(before.observed_backend),
                  ui.native_frame,before.frame,static_cast<unsigned long>(ui.commands),static_cast<unsigned long>(before.observed_commands),
                  static_cast<unsigned long>(before.array),static_cast<unsigned long>(before.expected_array),before.values.bytes,
                  before.values.used,before.values.critical,before.values.last,before.values.capacity,before.values.warning,before.configured_views);
            return;
        }
        // Exact SP R_EndFrame appends ID0 then clears used/critical/last at
        // 6D7BD3..6D7BD9 before backend consumption. Passive census therefore
        // parses only an owned allocation-bounded copy through native ID0;
        // this is not a finalized frontend extent or replay authorization.
        const auto length=before.values.capacity-static_cast<std::uint32_t>(before.observed_commands-before.values.bytes);
        std::vector<std::uint8_t> copy(length);
        if(!copy_commands(before.observed_commands,copy.data(),copy.size())||!snapshot(base,ui,&after,view_index)||
            before.array!=after.array||before.values!=after.values||before.frame!=after.frame||before.configured_views!=after.configured_views||
            before.observed_commands!=after.observed_commands||before.view_array!=after.view_array||before.view_count!=after.view_count||
            std::memcmp(before.scene_viewport,after.scene_viewport,16)||std::memcmp(before.display_viewport,after.display_viewport,16)) {
            trace("t5_hud_census_rejected","call=%lu reason=copy_fault_or_metadata_changed",serial);return;
        }
        const auto census=render::census_hud_commands(copy);
        char counts[1024]{};std::size_t position=0;
        for(unsigned id=0;id<29;++id) if(census.active[id]||census.tagged[id]) {
            const int n=std::snprintf(counts+position,sizeof(counts)-position,"%s%u:%u/%u[%u..%u]",position?",":"",id,
                census.active[id],census.tagged[id],census.shortest[id],census.longest[id]);
            if(n<0||static_cast<std::size_t>(n)>=sizeof(counts)-position) break;position+=n;
        }
        trace("t5_hud_census","call=%lu kind=%s view_index=%u view=0x%lx scene_viewport=%d,%d,%d,%d display_viewport=%d,%d,%d,%d backend=0x%lx native_frame=%lu array=0x%lx commands=0x%lx used=%d capacity=%d views=%d state=%d catcher=0x%x status=%s records=%zu consumed=%zu layout_recognized=%u unsupported=0x%x malformed=0x%x counts_active_tagged_lengths=%s extent=owned_allocation_terminated publication_length_verified=0 replay_calls=0 native_writes=0",
            serial,kind,view_index,static_cast<unsigned long>(before.view),before.scene_viewport[0],before.scene_viewport[1],before.scene_viewport[2],before.scene_viewport[3],
            before.display_viewport[0],before.display_viewport[1],before.display_viewport[2],before.display_viewport[3],
            static_cast<unsigned long>(ui.backend),ui.native_frame,static_cast<unsigned long>(before.array),static_cast<unsigned long>(before.observed_commands),
            before.values.used,before.values.capacity,before.configured_views,ui.connection_state,ui.key_catchers,
            render::command_list_status_name(census.status),census.records,census.consumed_bytes,census.all_active_layouts_recognized,
            census.unsupported_layout_mask,census.malformed_layout_mask,counts);
    } catch(...) {trace("t5_hud_census_rejected","call=%lu reason=copy_allocation_or_parser_exception",serial);}
}
}
void observe_hud_commands(std::uintptr_t base,const NativeFinalUi& ui,unsigned long serial) noexcept {
    wchar_t opt[8]{};GetEnvironmentVariableW(L"BOVR_HUD_CENSUS",opt,8);
    if(wcscmp(opt,L"1")||(serial>12&&serial%120)) return;
    observe_one(base,ui,serial,shared_list);
    const auto count=view_count_at(base,ui);
    for(unsigned view=0;view<count;++view) observe_one(base,ui,serial,view);
}
}
