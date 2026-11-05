// SPDX-License-Identifier: GPL-3.0-only
// Included inside the adapter's private namespace. C3 never assigns pixels,
// claims a frame, changes input, or feeds the C1 submission classifier.
void begin_cinematic_production(Adapter& a,const FrameBeginPoint& point) noexcept {
    if(!a.cinematic_production_enabled)return;
    const auto error=GetLastError();
    try {
        if(!a.initialized.load()||a.stop_requested.load()||a.replacement_pending.load()||a.device_lost.load()) {
            std::lock_guard lock(a.metadata);a.cinematic_production.clear();SetLastError(error);return;
        }
        CinematicProductionMarker marker{};NativeFinalUi first{};
        const bool copied=read_ui_context(a,&first)&&read_native_command_frame(a.base,true,&marker.native)&&
            read_native_cinematic_snapshot(&marker.movie)&&read_ui_context(a,&marker.context)&&
            cinematic_production_same_context(first,marker.context);
        std::lock_guard lock(a.metadata);vr::FrameTicket pending{};
        if(!copied||!a.initialized.load()||a.stop_requested.load()||a.replacement_pending.load()||a.device_lost.load()||
           point.image_base!=a.base||point.native_thread!=GetCurrentThreadId()||!a.coordinator.read_pending(&pending)) {
            a.cinematic_production.clear();SetLastError(error);return;
        }
        marker.thread=point.native_thread;marker.serial=++a.cinematic_production_serial;
        marker.generation=pending.key.generation;marker.observed_ms=GetTickCount64();marker.valid=true;
        const auto status=a.cinematic_production.begin(marker,point.ordinary_screen_frame);
        const auto n=++a.cinematic_production_begin_counts[static_cast<unsigned>(status)];
        if(n<=3||n%600==0)trace("t5_cinematic_production_begin","call=%llu marker=%llu status=%u thread=%lu generation=%llu buffer=0x%lx native_frame=%lu used=%d views=%lu movie=%s count=%llu menu_event_authority=0",
            point.call_serial,marker.serial,static_cast<unsigned>(status),marker.thread,marker.generation,static_cast<unsigned long>(marker.native.buffer),
            marker.native.frame,marker.native.used,marker.native.view_count,marker.movie.current_name.data(),n);
    }catch(...){std::lock_guard lock(a.metadata);a.cinematic_production.clear();}
    SetLastError(error);
}
void publish_cinematic_production(Adapter& a,const HudPassPoint& point,std::shared_ptr<const HudCommandPublication> publication) noexcept {
    if(!a.cinematic_production_enabled)return;
    const auto error=GetLastError();
    try {
        CinematicProductionMarker marker{};NativeFinalUi first{};
        const bool copied=publication&&read_ui_context(a,&first)&&read_native_command_frame(a.base,true,&marker.native)&&
            publication->native==marker.native&&read_native_cinematic_snapshot(&marker.movie)&&read_ui_context(a,&marker.context)&&
            cinematic_production_same_context(first,marker.context);
        std::lock_guard lock(a.metadata);vr::FrameTicket pending{};
        if(!copied||!a.initialized.load()||a.stop_requested.load()||a.replacement_pending.load()||a.device_lost.load()||
           point.image_base!=a.base||point.native_thread!=GetCurrentThreadId()||!a.coordinator.read_pending(&pending)) {
            a.cinematic_production.clear();SetLastError(error);return;
        }
        marker.thread=point.native_thread;marker.serial=++a.cinematic_production_serial;
        marker.generation=pending.key.generation;marker.observed_ms=GetTickCount64();marker.valid=true;
        marker.context.backend=marker.native.buffer;marker.context.native_frame=marker.native.frame;marker.context.commands=marker.native.shared_commands;
        const auto status=a.cinematic_production.complete(marker,point.enclosing_return,std::move(publication));
        const auto n=++a.cinematic_production_end_counts[static_cast<unsigned>(status)];
        if(n<=3||n%600==0)trace("t5_cinematic_production_end","call=%llu marker=%llu status=%u thread=%lu generation=%llu enclosing=0x%lx buffer=0x%lx native_frame=%lu used=%d views=%lu movie=%s count=%llu menu_event_authority=0",
            point.call_serial,marker.serial,static_cast<unsigned>(status),marker.thread,marker.generation,static_cast<unsigned long>(point.enclosing_return),
            static_cast<unsigned long>(marker.native.buffer),marker.native.frame,marker.native.used,marker.native.view_count,marker.movie.current_name.data(),n);
    }catch(...){std::lock_guard lock(a.metadata);a.cinematic_production.clear();}
    SetLastError(error);
}
void begin_cinematic_census(Adapter& a,const NativeFinalUi& native,std::uint64_t serial) noexcept {
    if(!a.cinematic_production_enabled)return;
    const auto error=GetLastError();a.cinematic_census_open={};
    try {
        const auto now=GetTickCount64();
        if(a.cinematic_census_reports>=64||(a.cinematic_census_sample_ms&&now>=a.cinematic_census_sample_ms&&now-a.cinematic_census_sample_ms<500)) {
            SetLastError(error);return;
        }
        a.cinematic_census_sample_ms=now;
        NativeCinematicSnapshot movie{};if(!read_native_cinematic_snapshot(&movie)){SetLastError(error);return;}
        CinematicProductionStamp stamp{};vr::FrameTicket pending{};
        {std::lock_guard lock(a.metadata);
         if(!a.coordinator.read_pending(&pending)||!a.cinematic_production.match(native,movie,pending.key.generation,now,&stamp)) {SetLastError(error);return;}}
        if(!stamp.commands||!match_native_shared_backend(a.base,*stamp.commands)||!a.cinematic_owner_backend_serial||
           a.cinematic_owner_backend_serial!=a.cinematic_backend_serial.load()||serial!=a.final_ui_serial.load()){SetLastError(error);return;}
        a.cinematic_census_open={stamp,native,movie,serial,a.cinematic_owner_backend_serial};
    }catch(...){a.cinematic_census_open={};}
    SetLastError(error);
}
void complete_cinematic_census(Adapter& a,const NativeFinalUi& native) noexcept {
    if(!a.cinematic_production_enabled)return;
    const auto error=GetLastError();
    auto open=std::move(a.cinematic_census_open);a.cinematic_census_open={};
    try {
        if(!open.production.commands||!open.serial||open.serial!=a.final_ui_serial.load()||open.backend_serial!=a.cinematic_backend_serial.load()) {SetLastError(error);return;}
        NativeCinematicSnapshot movie{};vr::FrameTicket pending{};CinematicProductionStamp matched{};
        if(!read_native_cinematic_snapshot(&movie)||!cinematic_production_same_context(open.native,native)||
           !cinematic_production_same_movie(open.movie,movie)||!match_native_shared_backend(a.base,*open.production.commands)){SetLastError(error);return;}
        {std::lock_guard lock(a.metadata);
         if(!a.coordinator.read_pending(&pending)||!a.cinematic_production.match(native,movie,pending.key.generation,GetTickCount64(),&matched)||
            matched.epoch!=open.production.epoch){SetLastError(error);return;}}
        const auto bytes=published_shared_list(*open.production.commands);
        const auto census=render::census_hud_commands(bytes);
        if(open.serial!=a.final_ui_serial.load()||open.backend_serial!=a.cinematic_backend_serial.load()){SetLastError(error);return;}
        const auto n=++a.cinematic_census_reports;
        trace("t5_cinematic_owned_census","report=%llu epoch=%llu native_epoch=%llu generation=%llu serial=%llu backend_epoch=%llu producer_thread=%lu backend_thread=%lu native_frame=%lu views=%lu state=%d catchers=0x%x menu_type=%d movie=%s bytes=%u status=%s unsupported=0x%x malformed=0x%x native_tail_pending=1 diagnostic_only=1 routing_changed=0",
            n,open.production.epoch,open.production.native_epoch,open.production.end.generation,open.serial,open.backend_serial,open.production.end.thread,
            GetCurrentThreadId(),native.native_frame,open.production.end.native.view_count,native.connection_state,native.key_catchers,native.page.menu_type,
            movie.current_name.data(),static_cast<unsigned>(bytes.size()),render::command_list_status_name(census.status),census.unsupported_layout_mask,census.malformed_layout_mask);
        for(unsigned id=0;id<29;++id)if(census.active[id]||census.tagged[id])
            trace("t5_cinematic_owned_command","report=%llu id=%u active=%u tagged=%u shortest=%u longest=%u",n,id,census.active[id],census.tagged[id],census.shortest[id],census.longest[id]);
        // At most 1 KiB of the already owned span, first eight reports only.
        // Whole-list length/truncation are explicit; raw bytes never imply a
        // recognized ABI, decoder frame identity, or drawable command.
        unsigned raw_slot=4;
        for(unsigned slot=0;slot<4;++slot)if(a.cinematic_census_raw_names[slot]==movie.current_name){raw_slot=slot;break;}
        if(raw_slot==4)for(unsigned slot=0;slot<4;++slot)if(!a.cinematic_census_raw_names[slot][0]){
            a.cinematic_census_raw_names[slot]=movie.current_name;raw_slot=slot;break;}
        if(raw_slot<4&&a.cinematic_census_raw_counts[raw_slot]<2){
            ++a.cinematic_census_raw_counts[raw_slot];
            const auto length=(std::min)(bytes.size(),std::size_t{1024});constexpr char hex[]="0123456789abcdef";
            for(std::size_t offset=0;offset<length;offset+=128){std::array<char,257> out{};const auto chunk=(std::min)(std::size_t{128},length-offset);
                for(std::size_t i=0;i<chunk;++i){out[2*i]=hex[bytes[offset+i]>>4];out[2*i+1]=hex[bytes[offset+i]&15];}
                trace("t5_cinematic_owned_bytes","report=%llu offset=%u chunk=%u total=%u truncated=%u hex=%s",n,static_cast<unsigned>(offset),static_cast<unsigned>(chunk),static_cast<unsigned>(bytes.size()),bytes.size()>length,out.data());}
        }
    }catch(...){} // Only copied diagnostic/reporting work; no native game call.
    SetLastError(error);
}
