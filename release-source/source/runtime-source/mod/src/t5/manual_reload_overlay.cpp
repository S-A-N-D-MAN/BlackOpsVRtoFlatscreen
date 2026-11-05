// SPDX-License-Identifier: GPL-3.0-only
#include "t5/manual_reload_overlay.hpp"
#include "t5/manual_reload_adapter.hpp"
#include "input/manual_reload.hpp"
#include "xr_math.h"
#include "loader/trace.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>

namespace bovr::t5 {
namespace {
namespace xr=wawvr::xr;namespace m=input::manual;
using Microsoft::WRL::ComPtr;
struct Vertex {float position[4],color[4];};
struct Point {xr::Vec3f position;std::array<float,4> color;};
struct Renderer {
    ID3D11Device* owner{};
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11RasterizerState> raster;ComPtr<ID3D11DepthStencilState> depth;
    std::array<Point,288> points{};unsigned count{};std::uint64_t frame{};bool active{};
    bool initialize(ID3D11Device* d){
        if(owner==d&&vs&&ps&&vertices&&layout&&raster&&depth)return true;
        owner=nullptr;vs.Reset();ps.Reset();vertices.Reset();layout.Reset();raster.Reset();depth.Reset();
        constexpr char shader[]=R"(struct V {float4 p:SV_POSITION;float4 c:COLOR;}; V VS(float4 p:POSITION,float4 c:COLOR){V o;o.p=p;o.c=c;return o;} float4 PS(V v):SV_TARGET{return v.c;})";
        ComPtr<ID3DBlob> vb,pb,error;
        if(FAILED(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VS","vs_4_0",0,0,&vb,&error))||
           FAILED(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PS","ps_4_0",0,0,&pb,&error))||
           FAILED(d->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&vs))||
           FAILED(d->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&ps)))return false;
        const D3D11_INPUT_ELEMENT_DESC elements[]{{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
        if(FAILED(d->CreateInputLayout(elements,2,vb->GetBufferPointer(),vb->GetBufferSize(),&layout)))return false;
        D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(Vertex)*288;b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_VERTEX_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        D3D11_DEPTH_STENCIL_DESC z{};z.DepthEnable=FALSE;z.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;z.DepthFunc=D3D11_COMPARISON_ALWAYS;
        if(FAILED(d->CreateBuffer(&b,nullptr,&vertices))||FAILED(d->CreateRasterizerState(&r,&raster))||FAILED(d->CreateDepthStencilState(&z,&depth)))return false;
        owner=d;return true;
    }
    void box(xr::Vec3f origin,xr::Quaternionf q,xr::Vec3f half,std::array<float,4> color){
        constexpr unsigned faces[36]{0,1,3,0,3,2,4,6,7,4,7,5,0,4,5,0,5,1,2,3,7,2,7,6,0,2,6,0,6,4,1,5,7,1,7,3};
        if(count+36>points.size())return;
        for(unsigned face=0;face<36;++face){const unsigned i=faces[face];const auto p=xr::Rotate(q,{(i&1)?half.x:-half.x,(i&2)?half.y:-half.y,(i&4)?half.z:-half.z});
            const float shade=.65f+.07f*float(face/6);auto c=color;for(unsigned k=0;k<3;++k)c[k]*=shade;
            points[count++]={{origin.x+p.x,origin.y+p.y,origin.z+p.z},c};}
    }
    void prepare(const xr::FrameState& f){
        frame=f.frame_id;active=false;count=0;m::Output out{};
        if(!read_manual_reload_visual(&out)||out.stage==m::Stage::ready||!f.actions.focused||!f.views_valid)return;
        const auto& main=f.actions.hands[out.hand];const auto& hand=f.actions.hands[out.hand^1];
        if(!main.grip.position_tracked||!main.aim.orientation_tracked||!hand.grip.position_tracked||!hand.grip.orientation_tracked)return;
        const bool rounds=out.kind==m::ReloadKind::internal_rounds||out.kind==m::ReloadKind::break_action;
        const auto device_size=rounds?xr::Vec3f{.010f,.029f,.010f}:
            (out.kind==m::ReloadKind::single_round?xr::Vec3f{.018f,.075f,.018f}:xr::Vec3f{.019f,.063f,.034f});
        // Campaign presentation keeps held ammunition; pickup, insertion and
        // charging targets remain physical interactions without diagnostic guides.
        const std::array<float,4> mag{.18f,.22f,.15f,1};
        if(out.held_device&&!manual_reload_authored_device_visible(f.frame_id,out.transaction)){box(hand.grip.pose.position,hand.grip.pose.orientation,device_size,mag);
            auto top=xr::Rotate(hand.grip.pose.orientation,{0,device_size.y,0});top={top.x+hand.grip.pose.position.x,top.y+hand.grip.pose.position.y,top.z+hand.grip.pose.position.z};
            box(top,hand.grip.pose.orientation,{device_size.x*.85f,.004f,device_size.z*.85f},{.65f,.48f,.12f,1});}
        active=count!=0;
    }
    bool draw(ID3D11Device* d,ID3D11DeviceContext* context,const xr::FrameState& f,unsigned eye,const xr::EyeRenderTarget& target){
        if(eye==0)prepare(f);if(frame!=f.frame_id)return false;if(!active)return true;
        if(!initialize(d))return false;
        const auto& view=f.eyes[eye];const auto inverse=xr::Conjugate(view.pose.orientation);
        const float l=std::tan(view.fov.angle_left),r=std::tan(view.fov.angle_right),b=std::tan(view.fov.angle_down),t=std::tan(view.fov.angle_up);
        if(!(r>l&&t>b))return false;
        std::array<Vertex,288> data{};
        for(unsigned i=0;i<count;++i){const auto p=points[i].position;
            const auto v=xr::Rotate(inverse,{p.x-view.pose.position.x,p.y-view.pose.position.y,p.z-view.pose.position.z});
            const float z=-v.z;
            if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(z))return false;
            // Homogeneous positions let hardware clip crossing/behind-eye
            // geometry without pinning it to a screen edge.
            data[i]={{(2*v.x-(r+l)*z)/(r-l),(2*v.y-(t+b)*z)/(t-b),z-.015f,z},{}};
            std::memcpy(data[i].color,points[i].color.data(),16);
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(context->Map(vertices.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
        std::memcpy(mapped.pData,data.data(),sizeof(Vertex)*count);context->Unmap(vertices.Get(),0);
        const D3D11_VIEWPORT viewport{0,0,float(target.width),float(target.height),0,1};context->RSSetViewports(1,&viewport);context->RSSetState(raster.Get());
        context->OMSetDepthStencilState(depth.Get(),0);context->OMSetBlendState(nullptr,nullptr,0xffffffff);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* buffer=vertices.Get();const UINT stride=sizeof(Vertex),offset=0;context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
        context->Draw(count,0);return true;
    }
};
}
bool render_manual_reload_overlay(void*,ID3D11Device* d,ID3D11DeviceContext* c,const xr::FrameState& f,unsigned eye,const xr::EyeRenderTarget& target){
    // Graphics owner calls both eyes serially. Snapshot one interaction and
    // one set of tracked poses for the complete submitted stereo pair.
    static Renderer renderer;
    return eye<2&&renderer.draw(d,c,f,eye,target);
}
}
