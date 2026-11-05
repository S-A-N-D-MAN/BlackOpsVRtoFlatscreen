// SPDX-License-Identifier: GPL-3.0-only
#include "t5/shadow_pair_diagnostic.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <immintrin.h>
#include <string>
#include <thread>
#include <vector>
namespace bovr {void trace(const char*,const char*,...) noexcept {}}
using namespace bovr::t5;
namespace {
unsigned checks{},failures{};
void check(bool okay,const char* why){++checks;if(!okay){++failures;std::printf("FAIL %s\n",why);}}
template<class T>void put(std::uintptr_t p,const T& v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof v);}
struct Fixture {
    std::uintptr_t base{},buffer{},views{};std::uint32_t frame=10;
    std::wstring directory,old_flag,old_trace;std::vector<std::wstring> files;
    static std::wstring env(const wchar_t* key){wchar_t value[32768]{};GetEnvironmentVariableW(key,value,32768);return value;}
    Fixture(){
        old_flag=env(L"BOVR_SHADOW_PAIR_DIAGNOSTIC");old_trace=env(L"BOVR_TRACE_FILE");
        wchar_t temp[32768]{};GetTempPathW(32768,temp);directory=temp;
        directory+=L"bovr-shadow-pair-fixture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"\\";
        check(CreateDirectoryW(directory.c_str(),nullptr)!=0,"fixture creates unique output directory");
        SetEnvironmentVariableW(L"BOVR_SHADOW_PAIR_DIAGNOSTIC",L"1");SetEnvironmentVariableW(L"BOVR_TRACE_FILE",(directory+L"fixture.log").c_str());
        base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4240000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        check(base!=0,"fixture owns bounded native-shaped allocation");if(!base)return;
        buffer=base+0x3740100;views=base+0x420f730;
        // The isolated test target validates authored zero-filled guard ranges.
        put(base+0x373708c,static_cast<std::uint32_t>(buffer));put(buffer+0x16cc40,static_cast<std::uint32_t>(views));
        for(unsigned i=0;i<2;++i){put(views+i*0x38e0+0x3510,static_cast<std::uint32_t>(buffer));put(views+i*0x38e0+0x184,1u);}
    }
    ~Fixture(){for(const auto& p:files)DeleteFileW(p.c_str());RemoveDirectoryW(directory.c_str());
        if(base)VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);
        SetEnvironmentVariableW(L"BOVR_SHADOW_PAIR_DIAGNOSTIC",old_flag.empty()?nullptr:old_flag.c_str());
        SetEnvironmentVariableW(L"BOVR_TRACE_FILE",old_trace.empty()?nullptr:old_trace.c_str());}
    EyeShadowState eye(unsigned index){
        put(buffer+0x15d7a0,frame);put(buffer+0x16cc38,index);put(buffer+0x16cc3c,index+1);put(buffer+0x16cbe0,index+1);
        EyeShadowState s{};s.valid=true;s.eye=index;s.buffer=buffer;s.frame=frame;s.view=views+index*0x38e0;s.sun_index=3;s.state.mask[0]=8;
        s.state.sun.fill(static_cast<std::uint8_t>(0x31+index));put(reinterpret_cast<std::uintptr_t>(s.state.sun.data())+0xa0,2u);
        std::array<std::uint8_t,0x140> camera{};camera.fill(static_cast<std::uint8_t>(0x71+index));put(s.view,camera);
        std::array<std::uint8_t,32> constants{};std::memcpy(constants.data(),s.state.sun.data()+0x80,32);put(s.view+0x2980,constants);
        std::array<std::uint8_t,0x68> lod{};lod.fill(static_cast<std::uint8_t>(0xa1+index));put(base+0x356ecf8,lod);
        put(s.view+0x24b0,0x12340000u+index);put(s.view+0x24b4,12u+index);
        s.state.spot_count=4;s.state.spots.fill(static_cast<std::uint8_t>(0x51+index));
        for(unsigned slot=0;slot<4;++slot){s.state.spots[slot*shadow_spot_bytes+0x180]=static_cast<std::uint8_t>(5+slot);
            put(s.view+0x2550+slot*0x50,0x56780000u+index*0x100+slot);put(s.view+0x2554+slot*0x50,30u+index*10+slot);}
        put(buffer+0x184390,s.state.spot_count);put(buffer+0x1843a0,s.state.spots);return s;
    }
    std::string output(std::uint64_t producer,unsigned sample){
        const auto path=directory+L"shadow-pair-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(producer)+L"-"+std::to_wstring(sample)+L".json";
        files.push_back(path);std::ifstream input(std::filesystem::path(path),std::ios::binary);return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    }
};
void run(){Fixture f;if(!f.base)return;
    SetEnvironmentVariableW(L"BOVR_SHADOW_PAIR_DIAGNOSTIC",nullptr);ShadowPairDiagnostic disabled;disabled.configure(1);
    check(!disabled.begin(1,1,1),"absent flag performs no native mapping read or allocation");
    SetEnvironmentVariableW(L"BOVR_SHADOW_PAIR_DIAGNOSTIC",L"1");
    ShadowPairDiagnostic wrong;wrong.configure(1);check(!wrong.begin(1,1,1),"unreadable mapping is diagnostic-only refusal");
    ShadowPairDiagnostic diagnostic;diagnostic.configure(f.base);
    auto sample=diagnostic.begin(f.buffer,f.frame,20);check(sample!=nullptr,"enabled exact mappings create pair sample");if(!sample)return;
    auto left=f.eye(0);const auto error=0x12345678ul;SetLastError(error);const auto csr=_mm_getcsr();
    diagnostic.capture(sample.get(),left);check(GetLastError()==error&&_mm_getcsr()==csr,"capture preserves LastError and MXCSR");
    check(sample->eyes[0].valid&&!sample->eyes[1].valid,"one captured eye cannot make a pair");
    diagnostic.complete(sample.get(),true);check(f.output(20,1).empty(),"one-sided pair does not emit");
    auto right=f.eye(1);diagnostic.capture(sample.get(),right);
    check(sample->eyes[0].camera[0]==0x71&&sample->eyes[1].camera[0]==0x72,"each camera is retained at its own native completion");
    check(sample->eyes[0].corrected_lod[0][0]==0xa1&&sample->eyes[1].corrected_lod[0][0]==0xa2,"LOD globals copied separately at each eye completion");
    check(sample->eyes[0].sun[0]==0x31&&sample->eyes[1].sun[0]==0x32,"immutable sun blocks are not replaced by latest shared state");
    check(sample->eyes[0].spots[0]==0x51&&sample->eyes[1].spots[0]==0x52&&sample->eyes[0].spot_count==4&&sample->eyes[1].spot_count==4,"all four spotlight records stay immutable at each eye completion");
    check(sample->eyes[0].spot_lists[3][4]==33&&sample->eyes[1].spot_lists[3][4]==43,"fourth exact per-eye spot list is retained at its own completion");
    diagnostic.complete(sample.get(),false);check(f.output(20,1).empty(),"failed producer ownership does not consume or emit pair");
    put(f.buffer+0x16cc3c,3u);diagnostic.complete(sample.get(),true);check(f.output(20,1).empty(),"auxiliary count3 is not emitted as ordinary pair");put(f.buffer+0x16cc3c,2u);
    std::thread foreign([&]{diagnostic.complete(sample.get(),true);});foreign.join();check(f.output(20,1).empty(),"foreign-thread completion denied");
    SetLastError(error);diagnostic.complete(sample.get(),true);check(GetLastError()==error&&_mm_getcsr()==csr,"serialization and trace preserve LastError and MXCSR");
    auto json=f.output(20,1);check(json.size()>16000&&json.ends_with("}\n"),"whole extended pair JSON exceeds prior buffer and is complete and untruncated");
    check(json.find("3131313131313131")!=std::string::npos&&json.find("3232323232323232")!=std::string::npos,"both immutable sun blocks emitted");
    check(json.find("\"drawsurf_count\":12")!=std::string::npos&&json.find("\"drawsurf_count\":13")!=std::string::npos,"per-eye draw list pointers and counts summarized");
    check(json.find("5151515151515151")!=std::string::npos&&json.find("5252525252525252")!=std::string::npos&&json.find("\"spot_count\":4")!=std::string::npos,"both full immutable spotlight blocks and active counts emitted");
    check(json.find("\"source_view_offset\":\"0x2550\"")!=std::string::npos&&json.find("\"source_view_offset\":\"0x2640\"")!=std::string::npos&&json.find("\"drawsurf_count\":33")!=std::string::npos&&json.find("\"drawsurf_count\":43")!=std::string::npos,"exact first/fourth spotlight offsets and both eye counts are summarized");
    check(!diagnostic.begin(f.buffer,f.frame,21),"same native buffer/frame is never emitted twice");
    for(unsigned variant=0;variant<11;++variant){++f.frame;auto bad=diagnostic.begin(f.buffer,f.frame,100+variant);check(bad!=nullptr,"rejected pair does not consume budget");if(!bad)continue;
        auto s=f.eye(0);if(variant==0)s.valid=false;if(variant==1)s.state.mask[0]=0;if(variant==2)s.frame++;
        if(variant==3)put(s.view+0x38c4,std::uint8_t{1});if(variant==4)put(s.view+0x188,1u);
        if(variant==5)put(s.view+0x2980,0u);if(variant==6)put(f.buffer+0x16cbe0,2u);if(variant==7)put(s.view+0x3510,0u);
        if(variant==8)s.state.spot_count=5;if(variant==9)put(f.buffer+0x184390,3u);if(variant==10)put(f.buffer+0x1843a0,std::uint8_t{0});
        diagnostic.capture(bad.get(),s);check(!bad->eyes[0].valid,"bad sun/context/constant/ownership eye is not diagnosed as accepted");
        put(s.view+0x38c4,std::uint8_t{});put(s.view+0x188,0u);put(s.view+0x3510,static_cast<std::uint32_t>(f.buffer));
    }
    for(unsigned number=2;number<=3;++number){++f.frame;auto s=diagnostic.begin(f.buffer,f.frame,200+number);check(s!=nullptr,"remaining distinct native frame admitted");if(!s)continue;
        for(unsigned eye=0;eye<2;++eye){auto shadow=f.eye(eye);
            if(number==2){shadow.state.mask[0]=0;put(reinterpret_cast<std::uintptr_t>(shadow.state.sun.data())+0xa0,0u);put(shadow.view+0x2980,0u);}
            diagnostic.capture(s.get(),shadow);}
        check(s->eyes[0].valid&&s->eyes[1].valid,"spot-only pair remains observable without unused sun constants agreement");diagnostic.complete(s.get(),true);
        check(!f.output(200+number,number).empty(),"remaining pair written once");}
    ++f.frame;check(!diagnostic.begin(f.buffer,f.frame,999),"three-pair cap disables future capture allocation");
}
}
int main(){run();std::printf("shadow pair diagnostic: %u checks, %u failures\n",checks,failures);return failures?1:0;}
