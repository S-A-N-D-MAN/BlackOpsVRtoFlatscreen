// SPDX-License-Identifier: GPL-3.0-only
#include "t5/hand_mesh_draw.hpp"
#include "t5/hand_mesh.hpp"
#include "loader/trace.hpp"
#include <memory>
#include <mutex>
namespace bovr::t5 {
namespace {
struct Surface {std::uintptr_t native_ib{};unsigned vertices{},triangles{},removed{};std::vector<std::uint16_t> indices;};
struct Mesh {hand_mesh::PublicationKey key{};std::vector<Surface> surfaces;};
std::mutex mutex;std::shared_ptr<const Mesh> publication;std::uint64_t publication_ms{};
// Owner-thread graphics resources. Native IB ownership is obtained exclusively
// through GetIndices, never by dereferencing a borrowed asset COM pointer.
struct DrawSurface {IDirect3DIndexBuffer9* native{};IDirect3DIndexBuffer9* filtered{};};
std::shared_ptr<const Mesh> rendered;std::vector<DrawSurface> buffers;
IDirect3DDevice9* active_device{};IDirect3DIndexBuffer9* restore_ib{};bool abandoned{};
std::uint64_t draws{};
bool copy(std::uintptr_t address,void* out,std::size_t size) noexcept {
    if(address<0x10000||address>UINT32_MAX-size)return false;
    __try{memcpy(out,reinterpret_cast<void*>(address),size);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T>bool read(std::uintptr_t address,T* out){return copy(address,out,sizeof(T));}
bool build(Mesh& m){
    // Pinned T5 XModel + XSurface layouts, copied while the exact native arms
    // model is admitted. No global model/index/vertex memory is ever changed.
    unsigned char bones{},roots{},count{};std::uint32_t surfaces{};
    const auto model=m.key.model;
    if(!read(model+4,&bones)||bones!=m.key.layout.arms||!read(model+5,&roots)||roots!=m.key.layout.roots||
       !read(model+6,&count)||!count||count>16||!read(model+0x20,&surfaces))return false;
    for(unsigned i=0;i<count;++i){std::array<unsigned char,0x44> raw{};if(!copy(surfaces+i*0x44,raw.data(),raw.size()))return false;
        const auto u16=[&](unsigned at){std::uint16_t v{};memcpy(&v,raw.data()+at,2);return v;};
        const auto u32=[&](unsigned at){std::uint32_t v{};memcpy(&v,raw.data()+at,4);return v;};
        Surface s{};s.vertices=u16(4);s.triangles=u16(6);s.native_ib=u32(0x2c);
        if(!s.vertices||s.vertices>8192||!s.triangles||s.triangles>16384||!s.native_ib)return false;
        std::array<unsigned,4> groups{};unsigned words=0;for(unsigned g=0;g<4;++g){groups[g]=u16(0x10+2*g);if(groups[g]>s.vertices)return false;words+=groups[g]*(1+2*g);}
        std::vector<std::uint16_t> indices(s.triangles*3);
        if(!copy(u32(0xc),indices.data(),indices.size()*2))return false;
        if((u16(2)&0x80)!=0){
            std::vector<std::uint16_t> blend(words);
            if(!copy(u32(0x18),blend.data(),blend.size()*2)||
               !hand_mesh::filter(m.key.layout,m.key.mode,s.vertices,groups,blend,indices,&s.indices,&s.removed))return false;
        }else{
            // Winter arms combine one skinned arms surface with rigid wrist
            // and watch surfaces. Do not reject the complete model because an
            // accessory has no blend array; validate its copied native groups.
            const unsigned lists=raw[1];
            if(!lists||lists>borrowed_profile::max_profile_bones||words||u32(0x18))return false;
            const auto list_pointer=u32(0x28);
            if(list_pointer<0x10000||list_pointer>UINT32_MAX-lists*12)return false;
            std::vector<hand_mesh::RigidGroup> rigid(lists);
            for(unsigned j=0;j<lists;++j){std::array<std::uint16_t,6> group{};
                if(!copy(list_pointer+j*12,group.data(),sizeof(group)))return false;
                rigid[j]={group[0],group[1],group[2],group[3]};
            }
            if(!hand_mesh::filter_rigid(m.key.layout,m.key.mode,s.vertices,rigid,indices,&s.indices,&s.removed))return false;
        }
        m.surfaces.push_back(std::move(s));
    }
    unsigned char after_bones{},after_roots{},after_count{};std::uint32_t after_surfaces{};
    return !m.surfaces.empty()&&read(model+4,&after_bones)&&after_bones==bones&&read(model+5,&after_roots)&&after_roots==roots&&
        read(model+6,&after_count)&&after_count==count&&read(model+0x20,&after_surfaces)&&after_surfaces==surfaces;
}
void release_buffers(){for(auto& b:buffers){if(b.native)b.native->Release();if(b.filtered)b.filtered->Release();}buffers.clear();rendered.reset();}
bool publish(const hand_mesh::PublicationKey& key) noexcept {
    try{std::lock_guard lock(mutex);
        if(!key.model||!hand_mesh::valid(key.layout)){publication.reset();publication_ms=0;return false;}
        if(publication&&publication->key==key){publication_ms=GetTickCount64();return true;}
        // Revoke the previous mode before building the next one. A rejected
        // left-only copy can never leave a cached primary-arm mask published.
        publication.reset();publication_ms=0;
        auto next=std::make_shared<Mesh>();next->key=key;
        if(!build(*next)){static unsigned failures{};if(++failures<=4)trace("t5_hand_mesh","status=copy_rejected row=%u mode=%u model=%p",key.row,unsigned(key.mode),reinterpret_cast<void*>(key.model));return false;}
        unsigned removed=0;for(const auto& s:next->surfaces)removed+=s.removed;
        trace("t5_hand_mesh","status=published row=%u mode=%u surfaces=%u removed_triangles=%u native_asset_writes=0",key.row,unsigned(key.mode),unsigned(next->surfaces.size()),removed);
        publication=std::move(next);publication_ms=GetTickCount64();return true;
    }catch(...){return false;/* Optional visual feature must not escape the native callback. */}
}
}
bool publish_hand_mesh(std::uintptr_t model,unsigned row,std::uint64_t epoch,std::uint64_t generation) noexcept {
    hand_mesh::PublicationKey key{model,row,epoch,generation};
    const auto* profile=borrowed_profile::get(row);const auto* shape=borrowed_profile::shape(row);
    if(!profile||!shape||!hand_mesh::copy_arms_layout(*profile,shape->arms,&key.layout))key.model=0;
    return publish(key);
}
bool publish_left_hand_mesh(std::uintptr_t model,const borrowed_profile::Profile& profile,unsigned arms,
    std::uint64_t epoch,std::uint64_t generation) noexcept {
    hand_mesh::PublicationKey key{model,borrowed_profile::invalid_runtime_row,epoch,generation,hand_mesh::FilterMode::left_hand_only};
    if(!epoch||!generation||!hand_mesh::copy_arms_layout(profile,arms,&key.layout))key.model=0;
    return publish(key);
}
bool before_hand_mesh_draw(IDirect3DDevice9* device,const IndexedDrawCall& call) noexcept {
    if(active_device||abandoned||!device||call.type!=D3DPT_TRIANGLELIST||call.start_index||call.min_vertex||call.base_vertex||!call.primitive_count)return false;
    try{std::shared_ptr<const Mesh> mesh;{std::lock_guard lock(mutex);const auto now=GetTickCount64();if(!publication||now<publication_ms||now-publication_ms>100)return false;mesh=publication;}
        if(mesh->key.mode==hand_mesh::FilterMode::weapon_split&&call.primitive_count<3)return false;
        bool candidate=false;for(const auto& s:mesh->surfaces)candidate|=s.removed&&s.vertices==call.num_vertices&&s.triangles==call.primitive_count;if(!candidate)return false;
        IDirect3DIndexBuffer9* ib{};if(FAILED(device->GetIndices(&ib))||!ib)return false;
        unsigned index=0;for(;index<mesh->surfaces.size();++index){const auto& s=mesh->surfaces[index];if(s.removed&&s.native_ib==reinterpret_cast<std::uintptr_t>(ib)&&s.vertices==call.num_vertices&&s.triangles==call.primitive_count)break;}
        if(index==mesh->surfaces.size()){ib->Release();return false;}
        if(rendered!=mesh){release_buffers();rendered=mesh;buffers.resize(mesh->surfaces.size());}
        auto& b=buffers[index];const auto& s=mesh->surfaces[index];
        if(!b.filtered){
            IDirect3DIndexBuffer9* filtered{};const auto bytes=UINT(s.indices.size()*2);
            if(FAILED(device->CreateIndexBuffer(bytes,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&filtered,nullptr))||!filtered){ib->Release();return false;}
            void* data{};if(FAILED(filtered->Lock(0,bytes,&data,0))){filtered->Release();ib->Release();return false;}
            memcpy(data,s.indices.data(),bytes);const auto unlocked=filtered->Unlock();if(FAILED(unlocked)){filtered->Release();ib->Release();return false;}
            b.filtered=filtered;b.native=ib;ib->AddRef();
        }
        if(FAILED(device->SetIndices(b.filtered))){ib->Release();return false;}
        restore_ib=ib;active_device=device;if(++draws<=6||draws%600==0)trace("t5_hand_mesh_draw","count=%llu row=%u mode=%u surface=%u removed=%u original_vertices=%u",draws,mesh->key.row,unsigned(mesh->key.mode),index,s.removed,s.vertices);
        return true;
    }catch(...){return false;}
}
void after_hand_mesh_draw() noexcept {
    if(!active_device||!restore_ib)return;
    const auto hr=active_device->SetIndices(restore_ib);
    if(FAILED(hr)){abandoned=true;trace("t5_hand_mesh_restore","status=failed hr=%08lx",hr);return;}
    restore_ib->Release();restore_ib=nullptr;active_device=nullptr;
}
void abandon_hand_mesh_draw() noexcept {abandoned=true;}
bool retire_hand_mesh_draw() noexcept {
    if(active_device)return false;release_buffers();abandoned=false;return true;
}
}
