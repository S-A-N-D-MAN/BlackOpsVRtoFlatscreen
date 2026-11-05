// Generated exact callback bodies; source identity is pinned externally.
std::uint64_t before_bloom_draw(void* context,IDirect3DDevice9* device,const IndexedDrawCall& call) noexcept {
    auto& a=*static_cast<Adapter*>(context);
    if(a.bloom_correction_enabled&&GetCurrentThreadId()!=a.owner.load()){a.bloom_read_reuse.revoke();return 0;}
    // A pending journal came from our own contained normal-return operation.
    // Reconcile before ANY owner draw, including unrelated native callers;
    // never infer safety from the original-native unwind abandonment flag.
    if(a.bloom_correction_enabled&&GetCurrentThreadId()==a.owner.load()&&device==a.retained_device&&
       !a.device_lost.load()&&a.bloom_correction.pending()&&!a.bloom_correction.busy())
        a.bloom_correction.reconcile_normal(device,true);
    if(a.bloom_correction_enabled&&a.bloom_read_reuse.open()){a.bloom_read_reuse.revoke();return 0;}
    if(!a.bloom_scene_active.load())return 0;
    if((!a.bloom_shader_enabled&&!a.bloom_correction_enabled)||!a.history_backend_active.load()||GetCurrentThreadId()!=a.owner.load()||
       device!=a.retained_device||call.caller!=a.base+0x30b5d3||call.type!=D3DPT_TRIANGLELIST||call.num_vertices!=4||call.primitive_count!=2)return 0;
    if(a.bloom_correction_enabled&&a.device_lost.load())return 0;
    if(a.bloom_correction_enabled) {
        const BloomDrawCall draw{call.caller,static_cast<std::uint32_t>(call.type),call.base_vertex,call.min_vertex,call.num_vertices,call.start_index,call.primitive_count};
        a.bloom_passive_token=0;a.bloom_active_token=0;
        __try {if(a.bloom_shader_enabled)a.bloom_passive_token=a.bloom_shaders.before(device,draw);}
        __except(EXCEPTION_EXECUTE_HANDLER){a.bloom_shaders.abandon();}
        __try {if(a.bloom_read_reuse.arm(draw))a.bloom_active_token=a.bloom_correction.before(device,draw);}
        __except(EXCEPTION_EXECUTE_HANDLER){a.bloom_correction.abandon();}
        if(!a.bloom_active_token){a.bloom_read_reuse.report_refusal();a.bloom_read_reuse.close();}
        if(!a.bloom_passive_token&&!a.bloom_active_token)return 0;
        if(++a.bloom_callback_serial==0){a.bloom_read_reuse.close();a.bloom_correction.abandon();a.bloom_shaders.abandon();return 0;}
        return a.bloom_callback_serial;
    }
    std::uint64_t token{};
    __try {token=a.bloom_shaders.before(device,{call.caller,static_cast<std::uint32_t>(call.type),call.base_vertex,call.min_vertex,call.num_vertices,call.start_index,call.primitive_count});}
    __except(EXCEPTION_EXECUTE_HANDLER){a.bloom_shaders.abandon();token=0;}
    return token;
}
void after_bloom_draw(void* context,std::uint64_t token,HRESULT result) noexcept {
    auto& a=*static_cast<Adapter*>(context);
    if(a.bloom_correction_enabled) {
        if(GetCurrentThreadId()!=a.owner.load()){a.bloom_read_reuse.revoke();return;}
        if(token!=a.bloom_callback_serial){a.bloom_read_reuse.close();a.bloom_correction.abandon();a.bloom_shaders.abandon();return;}
        __try {if(a.bloom_passive_token)a.bloom_shaders.after(a.bloom_passive_token,result);}
        __except(EXCEPTION_EXECUTE_HANDLER){a.bloom_shaders.abandon();}
        __try {if(a.bloom_active_token)a.bloom_correction.after(a.bloom_active_token,result);}
        __except(EXCEPTION_EXECUTE_HANDLER){a.bloom_correction.abandon();}
        a.bloom_read_reuse.report_refusal();a.bloom_read_reuse.close();a.bloom_passive_token=0;a.bloom_active_token=0;return;
    }
    __try {a.bloom_shaders.after(token,result);}
    __except(EXCEPTION_EXECUTE_HANDLER){a.bloom_shaders.abandon();}
}
void abandon_bloom_draw(void* context,std::uint64_t token) noexcept {
    auto& a=*static_cast<Adapter*>(context);
    if(a.bloom_correction_enabled){a.bloom_read_reuse.revoke();if(GetCurrentThreadId()!=a.owner.load())return;
        a.bloom_read_reuse.close();a.bloom_correction.abandon(a.bloom_active_token);if(a.bloom_passive_token)a.bloom_shaders.abandon(a.bloom_passive_token);a.bloom_active_token=0;a.bloom_passive_token=0;}
    else a.bloom_shaders.abandon(token);
}
bool complete_bloom_draw_scope(Adapter& a,const Produced& production,bool scope_started,bool complete) noexcept {
    if(!a.bloom_correction_enabled)return complete;
    a.bloom_read_reuse.close();
    if(production.auxiliary||(a.bloom_ordinary_correction&&production.regular_slots_copied))complete=scope_started&&a.bloom_correction.finish(complete);
    if(a.bloom_correction.pending())complete=false;
    if(!complete)a.bloom_rejected_key=production.key;
    return complete;
}
bool weapon_integration_renderer_ready() noexcept {
    const auto* a=adapter.load();return a&&a->bloom_correction_enabled&&a->history_correction_enabled&&a->auxiliary_enabled&&a->menu_enabled;
}
