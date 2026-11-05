// SPDX-License-Identifier: GPL-3.0-only
// Explicit hidden D3D9Ex test. Exercises our exact fit/staging code and a small
// synthetic HUD renderer, not proprietary BO material/font implementations.
#include "render/eye_staging.hpp"
#include "render/hud_eye_commands.hpp"
#include "render/hud_pixel_diagnostics.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
namespace {
unsigned checks{},failures{},frames{};std::uint64_t compared{};
bovr::render::HudPixelDiagnostics diagnostic;
void check(bool ok,const char* why,HRESULT hr=S_OK){++checks;if(!ok){++failures;std::printf("FAIL %s hr=%08lx\n",why,static_cast<unsigned long>(hr));}}
template<class T>T get(const std::uint8_t* p){T v{};std::memcpy(&v,p,sizeof(v));return v;}
template<class T>void put(std::vector<std::uint8_t>& b,std::size_t o,T v){std::memcpy(b.data()+o,&v,sizeof(v));}
std::vector<std::uint8_t> record(unsigned id,unsigned size){std::vector<std::uint8_t>b(size);put(b,0,static_cast<std::uint16_t>(size));b[2]=static_cast<std::uint8_t>(id);return b;}
void append(std::vector<std::uint8_t>& a,const std::vector<std::uint8_t>& b){a.insert(a.end(),b.begin(),b.end());}
void image(std::vector<std::uint8_t>& list,unsigned material,float x,float y,float w,float h,DWORD color){
    auto b=record(10,48);put(b,4,material);put(b,8,x);put(b,12,y);put(b,16,1.0f);put(b,20,w);put(b,24,h);put(b,44,color);append(list,b);
}
std::vector<std::uint8_t> source_hud(){
    std::vector<std::uint8_t>b;
    image(b,1,80,80,160,80,0xff000000); // black, not alpha extracted
    image(b,2,400,80,160,80,0x80ff0000); // alpha
    image(b,3,720,80,160,80,0x80008000); // additive
    image(b,4,160,280,160,160,0xffffffff); // circular test material
    image(b,1,-80,480,160,80,0xffffff00); // clipped to each native eye viewport
    auto q=record(15,48);put(q,4,1u);const float xy[]{600,300,720,360,660,480,540,420};std::memcpy(q.data()+8,xy,sizeof(xy));put(q,40,1.0f);put(q,44,0xff00ffffu);append(b,q);
    auto text=record(17,96);put(text,4,1040.0f);put(text,8,80.0f);put(text,12,1.0f);put(text,20,0x1234u);put(text,24,1.0f);put(text,28,1.0f);text[92]='H';append(b,text);
    append(b,record(0,4));return b;
}
struct Vertex{float x,y,z;DWORD color;};
bool draw(IDirect3DDevice9* d,std::span<const Vertex> vertices,D3DPRIMITIVETYPE type,UINT primitives,unsigned material){
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    d->SetRenderState(D3DRS_DESTBLEND,material==3?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA);
    return SUCCEEDED(d->DrawPrimitiveUP(type,primitives,vertices.data(),sizeof(Vertex)));
}
bool rectangle(IDirect3DDevice9*d,float x,float y,float w,float h,DWORD color,unsigned material){
    const std::array<Vertex,4>v{{{x,y,0.5f,color},{x+w,y,0.5f,color},{x+w,y+h,0.5f,color},{x,y+h,0.5f,color}}};
    return draw(d,v,D3DPT_TRIANGLEFAN,2,material);
}
bool execute_synthetic(IDirect3DDevice9* d,const std::vector<std::uint8_t>& bytes){
    for(std::size_t offset=0;offset<bytes.size();){
        const auto* p=bytes.data()+offset;const auto length=get<std::uint16_t>(p);const unsigned id=p[2];if(!id)return true;
        if(id==6){
            D3DVIEWPORT9 vp{get<UINT>(p+4),get<UINT>(p+8),get<UINT>(p+12),get<UINT>(p+16),0,1};
            if(FAILED(d->SetViewport(&vp)))return false;
            D3DMATRIX projection{};projection.m[0][0]=2.0f/vp.Width;projection.m[1][1]=-2.0f/vp.Height;
            projection.m[2][2]=projection.m[3][3]=1;projection.m[3][0]=-1.0f-1.0f/vp.Width;projection.m[3][1]=1.0f+1.0f/vp.Height;
            if(FAILED(d->SetTransform(D3DTS_PROJECTION,&projection)))return false;
        } else if(id==10){
            const auto material=get<unsigned>(p+4);const auto x=get<float>(p+8),y=get<float>(p+12),w=get<float>(p+20),h=get<float>(p+24);const auto color=get<DWORD>(p+44);
            if(material==4){
                std::array<Vertex,34> v{};v[0]={x+w/2,y+h/2,0.5f,color};
                for(unsigned i=0;i<33;++i){const auto angle=static_cast<float>(i)*6.283185307f/32;v[i+1]={x+w/2+std::cos(angle)*w/2,y+h/2+std::sin(angle)*h/2,0.5f,color};}
                if(!draw(d,v,D3DPT_TRIANGLEFAN,32,1))return false;
            }else if(!rectangle(d,x,y,w,h,color,material))return false;
        } else if(id==15){
            std::array<Vertex,4>v{};for(unsigned i=0;i<4;++i)v[i]={get<float>(p+8+i*8),get<float>(p+12+i*8),0.5f,get<DWORD>(p+44)};
            if(!draw(d,v,D3DPT_TRIANGLEFAN,2,get<unsigned>(p+4)))return false;
        } else if(id==17){
            const float x=get<float>(p+4),y=get<float>(p+8),sx=get<float>(p+24),sy=get<float>(p+28);
            // One explicit synthetic black H: same native text position/scale
            // fields, with simple geometry instead of a BO font asset.
            if(!rectangle(d,x,y,16*sx,112*sy,0xff000000,1)||!rectangle(d,x+64*sx,y,16*sx,112*sy,0xff000000,1)||
               !rectangle(d,x+16*sx,y+48*sy,48*sx,16*sy,0xff000000,1))return false;
        } else if(id!=27)return false;
        offset+=length;
    }return false;
}
std::vector<DWORD> readback(IDirect3DDevice9*d,IDirect3DSurface9*source,UINT w,UINT h){
    IDirect3DSurface9*copy{};if(FAILED(d->CreateOffscreenPlainSurface(w,h,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,nullptr)))return {};
    HRESULT hr=d->GetRenderTargetData(source,copy);D3DLOCKED_RECT locked{};if(SUCCEEDED(hr))hr=copy->LockRect(&locked,nullptr,D3DLOCK_READONLY);
    std::vector<DWORD> pixels;if(SUCCEEDED(hr)){pixels.resize(static_cast<std::size_t>(w)*h);for(UINT y=0;y<h;++y)
        std::memcpy(pixels.data()+static_cast<std::size_t>(y)*w,static_cast<const std::uint8_t*>(locked.pBits)+y*locked.Pitch,w*4);copy->UnlockRect();}
    // X8 has no defined alpha channel; compare every meaningful RGB bit.
    for(auto& pixel:pixels)pixel&=0xffffff;
    copy->Release();return pixels;
}
bool channel_close(DWORD a,DWORD b){for(unsigned shift:{0u,8u,16u})if(std::abs(static_cast<int>((a>>shift)&255)-static_cast<int>((b>>shift)&255))>2)return false;return true;}
DWORD alpha_red(DWORD background){return RGB(0,0,0)|(((128*255+127*((background>>16)&255))/255)<<16)|(((127*((background>>8)&255))/255)<<8)|((127*(background&255))/255);}
void run(IDirect3DDevice9Ex*d,bovr::render::EyeStaging& staging,UINT width,UINT height,std::uint64_t first){
    IDirect3DSurface9*bb{};check(SUCCEEDED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb))&&bb,"actual backbuffer");if(!bb)return;
    const std::array<RECT,2>rects{{{0,0,static_cast<LONG>(width/2),static_cast<LONG>(height)},{static_cast<LONG>(width/2),0,static_cast<LONG>(width),static_cast<LONG>(height)}}};
    const auto source=source_hud();std::array<bovr::render::HudEyeCommands,2>commands;
    for(unsigned eye=0;eye<2;++eye){commands[eye]=bovr::render::fit_hud_commands_to_eye(source,1280,720,{static_cast<int>(eye*width/2),0,static_cast<int>(width/2),static_cast<int>(height)});check(commands[eye].status==bovr::render::HudEyeStatus::accepted,"exact HUD fit code accepts synthetic native layout");}
    D3DMATRIX identity{};for(unsigned i=0;i<4;++i)identity.m[i][i]=1;
    d->SetTransform(D3DTS_WORLD,&identity);d->SetTransform(D3DTS_VIEW,&identity);
    d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);
    d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    d->SetTexture(0,nullptr);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    for(std::uint64_t frame=first;frame<first+16;++frame){
        const std::array<DWORD,2>world{0x00282846u+static_cast<DWORD>(frame),0x005a3c0au+static_cast<DWORD>(frame)};
        check(SUCCEEDED(staging.begin(d,frame,rects)),"begin world pair");
        for(unsigned eye=0;eye<2;++eye){check(SUCCEEDED(d->ColorFill(bb,nullptr,0)),"synthetic full clear between native world eyes");check(SUCCEEDED(d->ColorFill(bb,&rects[eye],world[eye])),"produce distinct world eye");check(SUCCEEDED(staging.save_eye(d,frame,eye)),"save completed world eye");}
        diagnostic.begin(d,{1,frame,frame,1,static_cast<std::uint32_t>(frame)},source,commands[0].bytes,commands[1].bytes);
        check(SUCCEEDED(staging.restore_pair(d,frame)),"restore immutable world pair before HUD");
        diagnostic.capture(d,frame,"world_restored");
        check(SUCCEEDED(d->BeginScene()),"begin synthetic native HUD pass");
        check(execute_synthetic(d,commands[0].bytes)&&execute_synthetic(d,commands[1].bytes),"draw two native-shaped HUD lists with actual per-eye projection/viewport");
        // Screen the exact optional readback while the draw scene is still open,
        // matching the existing retail outer-HUD callback placement.
        diagnostic.capture(d,frame,"hud_completed");
        check(SUCCEEDED(d->EndScene()),"complete HUD pass");
        const auto expected=readback(d,bb,width,height);check(expected.size()==static_cast<std::size_t>(width)*height,"composed framebuffer readable");
        if(expected.empty())break;
        const float scale=static_cast<float>(width/2)/1280,offset_y=(height-720*scale)/2;
        for(unsigned eye=0;eye<2;++eye){
            auto pixel=[&](float x,float y){const auto px=static_cast<UINT>(eye*width/2+x*scale);const auto py=static_cast<UINT>(offset_y+y*scale);return expected[static_cast<std::size_t>(py)*width+px]&0xffffff;};
            check(pixel(160,120)==0,"black HUD image survives without alpha extraction");
            check(channel_close(pixel(480,120),alpha_red(world[eye])),"translucent HUD blends against its own distinct world eye");
            check(((pixel(800,120)>>8)&255)>((world[eye]>>8)&255),"additive HUD contributes light in each eye");
            check(pixel(1048,112)==0,"synthetic black text stroke appears in both eyes");
            check(pixel(240,360)==0xffffff&&pixel(151,360)!=0xffffff&&pixel(240,271)!=0xffffff,"uniform circular HUD has bounded equal-axis coverage");
            check(pixel(24,520)==0xffff00,"offscreen source primitive is clipped into each eye");
            check(pixel(1240,520)==(world[eye]&0xffffff),"right-eye clipped primitive cannot corrupt the left eye's far edge");
        }
        check(SUCCEEDED(staging.begin(d,frame,rects))&&SUCCEEDED(staging.save_eye(d,frame,0))&&SUCCEEDED(staging.save_eye(d,frame,1)),"recapture completed HUD plus world pair using same exact producer");
        check(SUCCEEDED(d->ColorFill(bb,nullptr,0x0000ff00)),"late native shared UI overwrites entire backbuffer");
        check(SUCCEEDED(staging.restore_pair(d,frame)),"pre-Present restores composed pair rather than old world-only pixels");
        diagnostic.capture(d,frame,"pre_present",true);
        const auto actual=readback(d,bb,width,height);check(actual==expected,"every composed eye pixel survives late full-target overwrite");compared+=actual.size();
        check(FAILED(staging.restore_pair(d,frame)),"composed pair cannot be reused after consumption");
        check(SUCCEEDED(d->PresentEx(nullptr,nullptr,nullptr,nullptr,0)),"native Present remains healthy");++frames;
    }
    staging.invalidate();check(FAILED(staging.restore_pair(d,first)),"invalidated composition cannot submit old pixels");bb->Release();
}
void run_asymmetric_hud(IDirect3DDevice9Ex* d,UINT width,UINT height){
    using namespace bovr::render;
    constexpr float radians=3.14159265358979323846f/180;
    const float wide=std::tan(54*radians),narrow=std::tan(40*radians),up=std::tan(43.98f*radians),down=std::tan(54.27f*radians);
    const float sx=2*wide/(wide+narrow),sy=2*down/(up+down);
    const std::array<HudEyeDestination,2> destinations{{{0,1-sy,sx,sy},{1-sx,1-sy,sx,sy}}};
    HudVisibleRegion visible{};check(common_visible_hud_region(destinations,&visible),"Quest3 common visible region computes");
    IDirect3DSurface9* bb{};IDirect3DTexture9* copy{};IDirect3DSurface9* copy_surface{};
    auto hr=d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb);
    if(SUCCEEDED(hr))hr=d->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_X8R8G8B8,D3DPOOL_DEFAULT,&copy,nullptr);
    if(SUCCEEDED(hr))hr=copy->GetSurfaceLevel(0,&copy_surface);
    check(SUCCEEDED(hr),"asymmetric material-edge probe source resources",hr);
    if(FAILED(hr)){if(copy_surface)copy_surface->Release();if(copy)copy->Release();if(bb)bb->Release();return;}
    std::vector<std::uint8_t> source;
    image(source,1,0,100,24,48,0xff00ff00);image(source,1,1256,100,24,48,0xffff0000);
    // Native-shaped score box at the actual captured coordinate, with a unique
    // opaque test material so destination clipping can be counted exactly.
    image(source,1,1125.5f,552,150,40.5f,0xff00ffff);append(source,record(0,4));
    std::array<std::array<unsigned,3>,2> before{},after{};
    for(unsigned corrected=0;corrected<2;++corrected){
        d->SetTexture(0,nullptr);d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);
        d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
        d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
        check(SUCCEEDED(d->ColorFill(bb,nullptr,0x00112233)),"distinct non-marker background");
        check(SUCCEEDED(d->BeginScene()),"begin material-edge HUD draw");
        for(unsigned eye=0;eye<2;++eye){
            const auto commands=fit_hud_commands_to_eye(source,1280,720,{static_cast<int>(eye*width/2),0,static_cast<int>(width/2),static_cast<int>(height)},corrected?visible:HudVisibleRegion{});
            check(commands.status==HudEyeStatus::accepted&&execute_synthetic(d,commands.bytes),"draw exact native-shaped edge/score materials using selected safe fit");
        }
        check(SUCCEEDED(d->EndScene())&&SUCCEEDED(d->StretchRect(bb,nullptr,copy_surface,nullptr,D3DTEXF_NONE)),"retain actual packed HUD pixels before asymmetric output pass");
        check(SUCCEEDED(d->ColorFill(bb,nullptr,0)),"clear synthetic XR output");
        d->SetTexture(0,copy);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
        d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
        d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        D3DVIEWPORT9 full{0,0,width,height,0,1};d->SetViewport(&full);
        struct SampleVertex {float x,y,z,w,u,v;};
        check(SUCCEEDED(d->BeginScene()),"begin actual GPU asymmetric destination mapping");
        for(unsigned eye=0;eye<2;++eye){
            const auto& destination=destinations[eye];
            const float x=(eye+destination.x)*(width/2)-0.5f,y=destination.y*height-0.5f;
            const float right=x+destination.width*(width/2),bottom=y+destination.height*height;
            const float u=eye*0.5f;
            const std::array<SampleVertex,4> v{{{x,y,0.5f,1,u,0},{right,y,0.5f,1,u+0.5f,0},{right,bottom,0.5f,1,u+0.5f,1},{x,bottom,0.5f,1,u,1}}};
            // Each eye has its own output clipping region, just as two distinct
            // OpenXR targets do. Negative/oversized quad edges are intentional.
            const RECT clip{static_cast<LONG>(eye*width/2),0,static_cast<LONG>((eye+1)*width/2),static_cast<LONG>(height)};
            d->SetScissorRect(&clip);d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
            check(SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLEFAN,2,v.data(),sizeof(SampleVertex))),"rasterize oversized asymmetric viewport equivalent");
        }
        check(SUCCEEDED(d->EndScene()),"end asymmetric output draw");d->SetTexture(0,nullptr);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        const auto output=readback(d,bb,width,height);check(output.size()==static_cast<std::size_t>(width)*height,"asymmetric output readback");
        if(output.size()!=static_cast<std::size_t>(width)*height)break;
        auto& counts=corrected?after:before;
        for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){const auto color=output[static_cast<std::size_t>(y)*width+x];const unsigned eye=x/(width/2);
            if(color==0x00ff00)++counts[eye][0];if(color==0xff0000)++counts[eye][1];if(color==0x00ffff)++counts[eye][2];}
    }
    check(before[0][1]==0&&before[0][2]==0&&before[1][0]==0&&before[1][2]>0,"old fit reproduces asymmetric missing left score and opposite outer edge on GPU");
    for(unsigned eye=0;eye<2;++eye)for(unsigned marker=0;marker<3;++marker)check(after[eye][marker]>0,"corrected common fit preserves each edge and captured-coordinate score material in BOTH eyes");
    std::printf("asymmetric_hud_material_pixels old=%u,%u,%u;%u,%u,%u corrected=%u,%u,%u;%u,%u,%u\n",
        before[0][0],before[0][1],before[0][2],before[1][0],before[1][1],before[1][2],after[0][0],after[0][1],after[0][2],after[1][0],after[1][1],after[1][2]);
    copy_surface->Release();copy->Release();bb->Release();
}
}
int main(int argc,char**argv){
    if(argc!=2||std::strcmp(argv[1],"--hidden-device")){std::puts("Pass --hidden-device for the explicit offline graphics test.");return 2;}
    HWND window=CreateWindowExW(0,L"STATIC",L"BlackOpsVR HUD staging proof",WS_OVERLAPPEDWINDOW,0,0,160,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!window)return 1;
    wchar_t path[MAX_PATH]{};GetSystemDirectoryW(path,MAX_PATH);wcscat_s(path,L"\\d3d9.dll");HMODULE module=LoadLibraryW(path);
    using Create=HRESULT(WINAPI*)(UINT,IDirect3D9Ex**);const auto create=reinterpret_cast<Create>(GetProcAddress(module,"Direct3DCreate9Ex"));
    IDirect3D9Ex* factory{};HRESULT hr=create?create(D3D_SDK_VERSION,&factory):E_FAIL;check(SUCCEEDED(hr)&&factory,"system D3D9Ex factory",hr);
    if(!factory){DestroyWindow(window);if(module)FreeLibrary(module);return 1;}
    D3DPRESENT_PARAMETERS p{};p.Windowed=TRUE;p.hDeviceWindow=window;p.BackBufferWidth=320;p.BackBufferHeight=180;p.BackBufferFormat=D3DFMT_X8R8G8B8;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9Ex* device{};hr=factory->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,nullptr,&device);check(SUCCEEDED(hr)&&device,"system graphics device",hr);
    if(device){bovr::render::EyeStaging staging;run(device,staging,320,180,1);staging.release();p.BackBufferWidth=256;p.BackBufferHeight=144;
        hr=device->ResetEx(&p,nullptr);check(SUCCEEDED(hr),"ResetEx after composition resource retirement",hr);if(SUCCEEDED(hr))run(device,staging,256,144,101);
        staging.release();if(SUCCEEDED(hr))run_asymmetric_hud(device,256,144);check(device->Release()==0,"all composition/backbuffer/device references retired");}
    factory->Release();FreeLibrary(module);DestroyWindow(window);
    std::printf("%s checks=%u failures=%u frames=%u exact_pixel_comparisons=%llu retail_calls=0 xr_calls=0\n",failures?"FAIL":"PASS",checks,failures,frames,compared);return failures?1:0;
}
