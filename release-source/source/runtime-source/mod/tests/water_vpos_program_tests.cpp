// SPDX-License-Identifier: GPL-3.0-only
#include "t5/water_vpos_program.hpp"
#include "t5/water_reflection_program.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <filesystem>
#include <fstream>

using namespace bovr::t5;
namespace {
unsigned failures{},checks{};
void check(bool ok,const char* message){++checks;if(!ok){++failures;std::printf("FAIL %s\n",message);}}
bool close(double a,double b){return std::fabs(a-b)<2.e-5;}
std::array<double,3> ray(const VPosWorldConstants& c,double x,double y){
    std::array<double,3> out{};for(unsigned i=0;i<3;++i)out[i]=c.x[i]*x+c.y[i]*y+c.one[i];return out;
}
VPosMatrix creek(){return {
    .486903608f,-.538770437f,0,0,
    0,0,.719015896f,0,
    .741553903f,.6701653f,0,-3.99804688f,
    .74191618f,.670492709f,0,0};}
VPosWorldConstants captured(){return {
    {-.000360665959f,.000399085489f,0,-1.05060906e-6f},
    {0,0,.00096582633f,-0.f},
    {.181388736f,-1.69215155f,-1.39078987f,.00268955925f}};}
}
int main(int argc,char** argv){
    const auto matrix=creek();const auto original=captured();VPosWorldConstants output{};
    const auto matrix_copy=matrix;
    for(unsigned width:{2560u,3840u,5120u})for(unsigned eye:{0u,1u}){
        const unsigned height=width*9/16;
        auto in=original;
        // Rescale the captured full-target constants to each supported target.
        for(unsigned i=0;i<3;++i){in.x[i]*=5120.f/static_cast<float>(width);in.y[i]*=2880.f/static_cast<float>(height);}
        const VPosViewport viewport{static_cast<int>(eye*width/2),0,static_cast<int>(width/2),static_cast<int>(height)};
        check(water_vpos_gpu_constants(matrix,in,width,height,viewport,&output),"actual camera/full-target vectors accepted at supported resolutions");
        const double x=viewport.x+viewport.width*.5,y=viewport.height*.5;
        const auto corrected=ray(output,x,y),old=ray(in,x,y);
        check(close(corrected[0],-matrix[12])&&close(corrected[1],-matrix[13])&&close(corrected[2],-matrix[14]),
              "both corrected eye centers match negative camera forward");
        check(std::fabs(old[0]+matrix[12])>.4,"captured old constants reproduce large eye-center ray error");
        check(std::bit_cast<unsigned>(output.x[3])==std::bit_cast<unsigned>(in.x[3])&&
              std::bit_cast<unsigned>(output.y[3])==std::bit_cast<unsigned>(in.y[3])&&
              std::bit_cast<unsigned>(output.one[3])==std::bit_cast<unsigned>(in.one[3]),"unused W values preserved bitwise including negative zero");
        const auto twice=output;
        check(!water_vpos_gpu_constants(matrix,twice,width,height,viewport,&output),"already corrected vectors cannot be corrected a second time");
    }
    check(matrix==matrix_copy,"geometry/projection input remains unchanged");
    check(water_vpos_gpu_constants(matrix,original,5120,2880,{0,0,5120,2880},&output)&&output==original,
          "full-target viewport is an exact no-op");
    check(water_vpos_gpu_constants(matrix,original,5120,2880,{201,127,777,444},&output),"offset viewport accepted");
    const auto center=ray(output,201+777*.5,127+444*.5);
    check(close(center[0],-matrix[12])&&close(center[1],-matrix[13])&&close(center[2],-matrix[14]),"general offset viewport reconstructs camera-forward center");
    auto wrong=matrix;wrong[0]+=.2f*matrix[12];wrong[1]+=.2f*matrix[13];
    check(!water_vpos_gpu_constants(wrong,original,5120,2880,{0,0,2560,2880},&output),"off-axis projection rejected");
    wrong=matrix;wrong[4]=.5f;
    check(!water_vpos_gpu_constants(wrong,original,5120,2880,{0,0,2560,2880},&output),"skewed camera rejected");
    wrong=matrix;wrong[12]=0;wrong[13]=0;wrong[14]=0;wrong[15]=1;
    check(!water_vpos_gpu_constants(wrong,original,5120,2880,{0,0,2560,2880},&output),"nonperspective camera rejected");
    const std::array<std::array<float,3>,5> offsets{{
        {.5f,-2.f,1.f},{20.f,0.f,0.f},{-20.f,0.f,0.f},
        {-10.f,15.f,40.f},{10.f,-15.f,-40.f}}};
    bool positive_depth_translation{},nonzero_forward_translation{};
    for(const auto& offset:offsets){
        auto translated=matrix;
        const std::array<double,3> vertex{3.,-5.,7.};
        for(unsigned row=0;row<4;++row){
            for(unsigned i=0;i<3;++i)translated[row*4+3]+=matrix[row*4+i]*offset[i];
            double before=matrix[row*4+3],after=translated[row*4+3];
            for(unsigned i=0;i<3;++i){
                before+=matrix[row*4+i]*vertex[i];
                after+=translated[row*4+i]*(vertex[i]-offset[i]);
            }
            check(close(before,after),"native eyeOffset compensation preserves all four clip coordinates");
        }
        positive_depth_translation|=translated[11]>0.f;
        nonzero_forward_translation|=std::fabs(translated[15])>1.f;
        for(unsigned eye:{0u,1u}){
            const VPosViewport viewport{static_cast<int>(eye*2560u),0,2560,2880};
            VPosWorldConstants baseline{};
            check(water_vpos_gpu_constants(matrix,original,5120,2880,viewport,&baseline),"untranslated reference accepted");
            check(water_vpos_gpu_constants(translated,original,5120,2880,viewport,&output)&&output==baseline&&
                  std::bit_cast<unsigned>(output.x[3])==std::bit_cast<unsigned>(original.x[3])&&
                  std::bit_cast<unsigned>(output.y[3])==std::bit_cast<unsigned>(original.y[3])&&
                  std::bit_cast<unsigned>(output.one[3])==std::bit_cast<unsigned>(original.one[3]),
                  "physical camera-relative translation leaves directional correction and W bitwise unchanged");
        }
    }
    check(positive_depth_translation&&nonzero_forward_translation,
          "physical offsets exercise positive c2.w and large c3.w previously rejected");
    for(unsigned index:{3u,7u,11u,15u}){
        for(float value:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),1.0e8f}){
            wrong=matrix;wrong[index]=value;
            check(!water_vpos_gpu_constants(wrong,original,5120,2880,{0,0,2560,2880},&output),
                  "homogeneous values still require finite bounded input");
        }
    }
    wrong=matrix;wrong[0]*=1.2f;wrong[1]*=1.2f;
    check(!water_vpos_gpu_constants(wrong,original,5120,2880,{0,0,2560,2880},&output),"projection scale disagreeing with full-target rays rejected");
    auto stale=original;stale.one[0]+=.02f;
    check(!water_vpos_gpu_constants(matrix,stale,5120,2880,{0,0,2560,2880},&output),"stale camera constants rejected");
    stale=original;stale.x[0]=std::numeric_limits<float>::quiet_NaN();
    check(!water_vpos_gpu_constants(matrix,stale,5120,2880,{0,0,2560,2880},&output),"nonfinite constants rejected");
    check(!water_vpos_gpu_constants(matrix,original,5120,2880,{-1,0,2560,2880},&output),"negative viewport rejected");
    check(!water_vpos_gpu_constants(matrix,original,5120,2880,{3000,0,2560,2880},&output),"out-of-target viewport rejected");
    check(!water_vpos_gpu_constants(matrix,original,0,2880,{0,0,2560,2880},&output),"zero target rejected");
    check(!water_vpos_gpu_constants(matrix,original,5120,2880,{0,0,2560,2880},nullptr),"null output rejected");
    const auto& programs=water_vpos_programs();
    check(programs.size()==3,"exact three actual waterflow programs only");
    check(programs[0].registers==std::array<unsigned,3>{9,10,11}&&programs[0].pixel_bytes==5524&&programs[0].vertex_bytes==1708,
          "no-feather shadow exact register and size mapping");
    for(unsigned i:{1u,2u})check(programs[i].registers==std::array<unsigned,3>{10,11,20},"NV feather mapping preserves depth renderTargetSize c9 and exposure c21");
    for(const auto& program:programs){
        check(std::strlen(program.pixel_sha256)==64&&std::strlen(program.vertex_sha256)==64,"exact complete program fingerprints");
        check(std::strncmp(program.pixel_sha256,"8a87",4)!=0,"underwater shader with no VPOS is excluded");
    }
    for(unsigned width:{2560u,3840u,5120u})for(unsigned eye:{0u,1u}){
        const unsigned height=width*9/16;WaterReflectionUv uv{};
        check(water_reflection_uv({int(eye*width/2),0,int(width/2),int(height)},width,height,width,height,&uv),
              "actual packed reflection texture admits selected eye UV");
        for(float u:{-.25f,0.f,.25f,.5f,.75f,1.f,1.25f})for(float v:{-.25f,0.f,.5f,1.f,1.25f}){
            const float mapped_u=std::clamp(u*uv.affine[0]+uv.affine[2],uv.bounds[0],uv.bounds[2]);
            const float mapped_v=std::clamp(v*uv.affine[1]+uv.affine[3],uv.bounds[1],uv.bounds[3]);
            const double texel_u=double(mapped_u)*width-.5,texel_v=double(mapped_v)*height-.5;
            check(texel_u>=double(eye*width/2)&&texel_u<=double((eye+1)*width/2-1)&&
                  texel_v>=0&&texel_v<=double(height-1),
                  "clamped reflection bilinear footprint stays inside the selected eye");
        }
        check(close(.5*uv.affine[0]+uv.affine[2],eye*.5+.25),"eye-local reflection center maps to its own packed half");
    }
    const std::array<std::uint32_t,4> reflected{0x03000042,0x802f0000,0x80e40000,0xa0e40801};
    const std::array<std::uint32_t,4> depth_sample{0x03000042,0x800f0000,0x80e40000,0xa0e40800};
    const std::vector<std::uint32_t> synthetic{0xffff0300,reflected[0],reflected[1],reflected[2],reflected[3],
        depth_sample[0],depth_sample[1],depth_sample[2],depth_sample[3],0xffff};
    std::vector<std::uint32_t> derivative;
    check(water_reflection_insert(synthetic,1,reflected,&derivative)&&derivative.size()==synthetic.size()+13&&
          std::equal(synthetic.begin()+1,synthetic.end(),derivative.begin()+14),
          "UV insertion changes only coordinates at the selected reflection lookup; depth instruction preserved");
    check(!water_vpos_make_derivative(0,synthetic,&derivative),"unknown shader cannot receive the production derivative");
    if(argc>1){
        for(unsigned id=0;id<programs.size();++id){
            const auto& p=programs[id];
            const auto path=std::filesystem::path(argv[1])/(std::string("ps-")+p.pixel_sha256+".bin");
            std::ifstream file(path,std::ios::binary|std::ios::ate);
            check(bool(file),"captured actual shader fixture exists");if(!file)continue;
            const auto bytes=static_cast<std::streamoff>(file.tellg());
            check(bytes==std::streamoff(p.pixel_bytes),"captured actual shader has exact size");
            if(bytes!=std::streamoff(p.pixel_bytes))continue;
            std::vector<std::uint32_t> captured_program(p.pixel_bytes/4);
            file.seekg(0);file.read(reinterpret_cast<char*>(captured_program.data()),p.pixel_bytes);
            check(bool(file)&&water_vpos_make_derivative(id,captured_program,&derivative),"exact actual water shader accepts the single-site UV derivative");
            if(derivative.size()!=captured_program.size()+13){check(false,"actual derivative adds exactly thirteen DWORDs");continue;}
            check(std::equal(captured_program.begin(),captured_program.begin()+p.reflection_sample_word,derivative.begin())&&
                  std::equal(captured_program.begin()+p.reflection_sample_word,captured_program.end(),derivative.begin()+p.reflection_sample_word+13),
                  "every original bytecode instruction including depth, cube and VPOS math is unchanged");
            if(id){const unsigned depth_word=id==1?491u:1408u;
                const unsigned relocated=depth_word+(depth_word>=p.reflection_sample_word?13u:0u);
                check((captured_program[depth_word]&0xffff)==0x42&&
                      std::equal(captured_program.begin()+depth_word,captured_program.begin()+depth_word+4,derivative.begin()+relocated),
                      "NV floatZ direct VPOS texture lookup remains byte-for-byte unchanged");}
            captured_program.back()^=1u;
            check(!water_vpos_make_derivative(id,captured_program,&derivative),"single-byte mismatch rejects actual shader derivative");
        }
    }
    std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
