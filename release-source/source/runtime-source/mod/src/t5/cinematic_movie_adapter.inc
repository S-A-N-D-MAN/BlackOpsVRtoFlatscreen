// SPDX-License-Identifier: GPL-3.0-only
// Included inside the existing adapter private namespace. No native movie
// routine, list replay, new hook, menu event or input revision is used.
bool read_cinematic_canvas(IDirect3DDevice9* device,CinematicCanvas* out) {
    if(out)*out={};if(!device||!out)return false;
    IDirect3DSurface9* bb{};IDirect3DSurface9* rt{};D3DSURFACE_DESC d{};
    D3DVIEWPORT9 v{};RECT s{};DWORD enabled{};
    const bool ok=SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb))&&bb&&SUCCEEDED(bb->GetDesc(&d))&&
        SUCCEEDED(device->GetRenderTarget(0,&rt))&&rt&&SUCCEEDED(device->GetViewport(&v))&&
        SUCCEEDED(device->GetRenderState(D3DRS_SCISSORTESTENABLE,&enabled))&&SUCCEEDED(device->GetScissorRect(&s));
    if(ok)*out={d.Width,d.Height,v.X==0&&v.Y==0&&v.Width==d.Width&&v.Height==d.Height,
        !enabled||(s.left<=0&&s.top<=0&&s.right>=static_cast<LONG>(d.Width)&&s.bottom>=static_cast<LONG>(d.Height)),bb==rt};
    if(rt)rt->Release();if(bb)bb->Release();return ok;
}
void begin_cinematic_movie(Adapter& a,const NativeFinalUi& native,std::uint64_t serial) {
    if(!a.cinematic_movie_enabled)return;a.cinematic_movie_gate.invalidate();
    NativeCinematicSnapshot movie{};const bool decoder_copied=read_native_cinematic_snapshot(&movie);
    CinematicProductionStamp stamp{};vr::FrameTicket ticket{};CinematicCanvas canvas{};
    // These value-only reason samples do not admit a frame. In particular, a
    // native input capture is still rejected before any D3D metadata queries.
    auto report=[&](unsigned reason,const char* label,bool accepted=false){
        auto key=movie.current_name;
        if(!key[0]){constexpr char unavailable[]="<decoder-unavailable>";std::memcpy(key.data(),unavailable,sizeof unavailable);}
        if(!a.cinematic_movie_log.admit(key,MovieLogStage::begin,reason))return;
        trace("t5_cinematic_movie_begin","serial=%llu backend_epoch=%llu accepted=%u reason=%s reason_id=%u epoch=%llu native_frame=%lu views=%lu producer_thread=%lu backend_thread=%lu pending_generation=%llu canvas=%ux%u viewport_full=%u scissor_full=%u bb_target=%u catcher=%ld state=%ld decoder_copied=%u movie=%s menu_event_authority=0",
            serial,a.cinematic_owner_backend_serial,accepted,label,reason,stamp.epoch,native.native_frame,stamp.commands?stamp.end.native.view_count:UINT32_MAX,
            stamp.end.thread,GetCurrentThreadId(),ticket.key.generation,canvas.width,canvas.height,canvas.full_viewport,canvas.full_scissor,canvas.backbuffer_target,
            native.key_catchers,native.connection_state,decoder_copied,key.data());
    };
    if(native.key_catchers!=0){report(1,"native_input_capture");return;}
    if(!a.retained_device){report(2,"no_device");return;}
    if(!decoder_copied){report(3,"decoder_unavailable");return;}
    bool pending=false,matched=false;
    {std::lock_guard lock(a.metadata);pending=a.coordinator.read_pending(&ticket);
     if(pending)matched=a.cinematic_production.match(native,movie,ticket.key.generation,GetTickCount64(),&stamp);}
    if(!pending){report(4,"no_pending");return;}
    if(!matched){report(5,"production_unmatched");return;}
    if(!stamp.commands){report(6,"no_owned_commands");return;}
    if(stamp.end.native.view_count!=0){report(7,"world_views");return;}
    if(!match_native_shared_backend(a.base,*stamp.commands)){report(8,"shared_backend_mismatch");return;}
    if(!a.cinematic_owner_backend_serial||a.cinematic_owner_backend_serial!=a.cinematic_backend_serial.load()){report(9,"backend_epoch");return;}
    if(serial!=a.final_ui_serial.load()){report(10,"final_epoch");return;}
    if(!read_cinematic_canvas(a.retained_device,&canvas)){report(11,"canvas_read");return;}
    if(!read_native_cinematic_snapshot(&movie)){report(12,"decoder_recheck");return;}
    if(serial!=a.final_ui_serial.load()||a.cinematic_owner_backend_serial!=a.cinematic_backend_serial.load()){report(13,"serial_recheck");return;}
    const bool accepted=a.cinematic_movie_gate.begin(serial,a.cinematic_owner_backend_serial,stamp,native,movie,canvas,true,GetTickCount64());
    ++a.cinematic_movie_begin_counts[accepted?1:0];report(accepted?0u:14u,accepted?"accepted":"gate_rejected",accepted);
}
bool complete_cinematic_movie(Adapter& a,const NativeFinalUi& native) {
    if(!a.cinematic_movie_enabled)return false;
    const auto commands=a.cinematic_movie_gate.open_commands();NativeCinematicSnapshot movie{};
    const bool copied=commands&&match_native_shared_backend(a.base,*commands)&&read_native_cinematic_snapshot(&movie);
    const auto proof=a.cinematic_movie_gate.complete(a.final_ui_serial.load(),a.cinematic_backend_serial.load(),native,movie,copied,GetTickCount64());
    const auto n=++a.cinematic_movie_complete_counts[static_cast<unsigned>(proof.status)];
    if(a.cinematic_movie_log.admit(movie.current_name,MovieLogStage::complete,static_cast<unsigned>(proof.status)))trace("t5_cinematic_movie_complete","serial=%llu backend_epoch=%llu status=%s count=%llu native_frame=%lu movie_primitives=%lu surviving_full_canvas=%lu rectangle=%.6f,%.6f,%.6f,%.6f movie=%s native_tail_pending=1 capture_here=0",
        a.final_ui_serial.load(),a.cinematic_backend_serial.load(),cinematic_command_status_name(proof.status),n,native.native_frame,
        proof.movie_primitives,proof.full_canvas_primitives,proof.last_x,proof.last_y,proof.last_width,proof.last_height,movie.current_name.data());
    return proof.status==CinematicCommandStatus::full_canvas_movie_primitive;
}
void stage_cinematic_movie_at_present(Adapter& a,IDirect3DDevice9* device) {
    vr::FrameTicket pending{};NativeFinalUi current{};NativeCinematicSnapshot movie{};
    if(!a.initialized.load()||a.stop_requested.load()||a.device_lost.load()||a.replacement_pending.load()||
       device!=a.retained_device||GetCurrentThreadId()!=a.owner.load()||!a.coordinator.read_pending(&pending)||
       !read_ui_context(a,&current)||!read_native_cinematic_snapshot(&movie)){a.cinematic_movie_gate.invalidate();return;}
    CinematicMovieFrameStamp frame{};
    if(!a.cinematic_movie_gate.take_at_present(a.final_ui_serial.load(),a.cinematic_backend_serial.load(),pending.key.generation,current,movie,GetTickCount64(),&frame))return;
    // Full native rendering has returned. This pre-Present seam supplies the
    // capture consumed immediately below by the existing quad transport.
    IDirect3DSurface9* bb{};D3DSURFACE_DESC d{};
    const bool same=SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb))&&bb&&SUCCEEDED(bb->GetDesc(&d))&&
        d.Width==frame.canvas.width&&d.Height==frame.canvas.height;
    if(bb)bb->Release();if(!same)return;
    vr::FrameTicket ticket{};if(!a.coordinator.claim_render_frame(&ticket)||ticket.key!=pending.key)return;
    const auto now=GetTickCount64();
    if(!a.cinematic_anchor_tick||now<a.cinematic_anchor_tick||now-a.cinematic_anchor_tick>250||
       a.cinematic_anchor_movie.current_name!=movie.current_name||a.cinematic_anchor_movie.bink_handle!=movie.bink_handle||
       movie.active_image_frame<a.cinematic_anchor_movie.active_image_frame||a.cinematic_anchor_native_epoch!=frame.production.native_epoch)a.cinematic_panel.clear();
    xr::QuadLayer panel{};if(!a.cinematic_panel.update(ticket.frame.head_center,ticket.key.generation,ui::OpaqueContext::cinematic,d.Width,d.Height,&panel))return;
    a.cinematic_anchor_tick=now;a.cinematic_anchor_movie=movie;a.cinematic_anchor_native_epoch=frame.production.native_epoch;
    Produced pixels{};pixels.valid=true;pixels.menu=true;pixels.cinematic=true;pixels.cinematic_independent=true;pixels.key=ticket.key;
    {std::lock_guard lock(a.metadata);pixels.stamp={frame.native.backend,++a.producer_serial};}
    pixels.native_frame=frame.native.native_frame;pixels.native_ui=frame.native;pixels.panel=panel;
    pixels.ui_production_epoch=frame.production.epoch;pixels.ui_production_ms=frame.production.begin.observed_ms;
    pixels.source_width=d.Width;pixels.source_height=d.Height;pixels.cinematic_movie=movie;pixels.cinematic_commands=frame.production.commands;
    pixels.cinematic_final_serial=frame.final_serial;pixels.cinematic_backend_serial=frame.backend_serial;
    if(!a.coordinator.stage_menu_frame(ticket.key,pixels.stamp,panel)||!a.coordinator.mark_menu_complete(ticket.key,pixels.stamp))return;
    pixels.completed=true;{std::lock_guard lock(a.metadata);a.current_pixels=pixels;a.submitted_menu={};}
    const auto n=++a.cinematic_presented;
    if(a.cinematic_movie_log.admit(movie.current_name,MovieLogStage::staged))trace("t5_cinematic_movie_staged","serial=%llu backend_epoch=%llu count=%llu xr_frame=%llu generation=%llu producer=%llu epoch=%llu original_production_ms=%llu native_frame=%lu canvas=%ux%u movie=%s capture=authoritative_pre_present native_tail_returned=1 world_restore=0 interactive=0 menu_event_authority=0",
        frame.final_serial,frame.backend_serial,n,ticket.key.frame_id,ticket.key.generation,pixels.stamp.producer_serial,frame.production.epoch,
        frame.production.begin.observed_ms,frame.native.native_frame,d.Width,d.Height,movie.current_name.data());
}
