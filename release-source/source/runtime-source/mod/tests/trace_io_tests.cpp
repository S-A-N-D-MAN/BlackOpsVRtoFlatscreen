// SPDX-License-Identifier: GPL-3.0-only
// Real trace implementation with only namespace and OS I/O observations changed.
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <thread>
#include <vector>
namespace {
std::atomic<unsigned> writes{},flushes{},active_writes{},overlap{};
std::atomic<bool> fail_write{};
unsigned checks{};
void check(bool value,const char* message){
    ++checks;if(!value){std::printf("FAIL trace I/O: %s\n",message);std::exit(1);}
}
}
BOOL WINAPI trace_observed_write(HANDLE file,LPCVOID buffer,DWORD bytes,LPDWORD written,LPOVERLAPPED pending){
    ++writes;if(active_writes.fetch_add(1))++overlap;
    const BOOL result=fail_write?FALSE:WriteFile(file,buffer,bytes,written,pending);
    --active_writes;SetLastError(0x54524345);return result;
}
BOOL WINAPI trace_forbidden_flush(HANDLE){++flushes;SetLastError(0xBADF105);return TRUE;}
#define bovr trace_io_fixture
#define WriteFile trace_observed_write
#define FlushFileBuffers trace_forbidden_flush
#include "../src/loader/trace.cpp"
#undef FlushFileBuffers
#undef WriteFile
#undef bovr
namespace {
std::wstring temp_file(){
    wchar_t folder[MAX_PATH]{},path[MAX_PATH]{};
    check(GetTempPathW(MAX_PATH,folder)>0&&GetTempFileNameW(folder,L"bvt",0,path)!=0,"create private trace fixture file");
    return path;
}
std::string read(const std::wstring& path){
    std::ifstream file(path.c_str(),std::ios::binary);
    check(file.good(),"log is readable by independent reader while writer handle stays open");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void close_fixture(){
    if(trace_io_fixture::log_file!=INVALID_HANDLE_VALUE){
        check(CloseHandle(trace_io_fixture::log_file)!=FALSE,"close fixture-owned log handle");
        trace_io_fixture::log_file=INVALID_HANDLE_VALUE;
    }
}
}
int trace_io_child_mode(int argc,char** argv){
    if(argc!=2||std::strcmp(argv[1],"--trace-io-child"))return -1;
    SetLastError(0xC011D001);trace_io_fixture::initialize_trace(GetModuleHandleW(nullptr));
    if(GetLastError()!=0xC011D001||trace_io_fixture::log_file==INVALID_HANDLE_VALUE)return 2;
    SetLastError(0xC011D002);trace_io_fixture::trace("normal_exit","marker=complete");
    // Deliberately no explicit close/flush: normal process termination owns it.
    return GetLastError()==0xC011D002&&writes==1&&flushes==0?0:3;
}
void run_trace_io_tests(){
    constexpr DWORD sentinel=0x1234A55A;
    SetLastError(sentinel);trace_io_fixture::trace("uninitialized","ignored=1");
    check(GetLastError()==sentinel&&writes==0&&flushes==0,"no-handle path preserves LastError and performs no I/O");
    wchar_t old_env[32768]{};const DWORD old_count=GetEnvironmentVariableW(L"BOVR_TRACE_FILE",old_env,32768);
    check(old_count<32768,"existing trace environment fits retained buffer");
    const auto path=temp_file();check(SetEnvironmentVariableW(L"BOVR_TRACE_FILE",path.c_str())!=FALSE,"set private trace path");
    SetLastError(sentinel);trace_io_fixture::initialize_trace(GetModuleHandleW(nullptr));
    check(GetLastError()==sentinel&&trace_io_fixture::log_file!=INVALID_HANDLE_VALUE,"real initialization preserves LastError");
    SetLastError(sentinel);trace_io_fixture::trace("immediate","value=%u",42u);
    check(GetLastError()==sentinel&&writes==1&&flushes==0,"one trace writes once without any device flush and preserves LastError");
    auto content=read(path);check(content.find("event=immediate value=42\r\n")!=std::string::npos,"complete CRLF line is immediately readable before close");
    fail_write=true;SetLastError(sentinel);trace_io_fixture::trace("failed_write","ignored=1");
    check(GetLastError()==sentinel&&writes==2&&flushes==0,"failed WriteFile still preserves caller LastError without flush/retry");
    check(read(path)==content,"failed write does not manufacture a success line");fail_write=false;
    std::atomic<unsigned> error_changes{};std::vector<std::thread> threads;
    constexpr unsigned thread_count=8,per_thread=64;
    for(unsigned t=0;t<thread_count;++t)threads.emplace_back([t,&error_changes]{
        for(unsigned n=0;n<per_thread;++n){SetLastError(sentinel+t);trace_io_fixture::trace("parallel","worker=%u item=%u",t,n);
            if(GetLastError()!=sentinel+t)++error_changes;}
    });
    for(auto& thread:threads)thread.join();
    check(writes==2+thread_count*per_thread&&flushes==0&&!overlap&&!error_changes,
        "eight concurrent producers each write once under unchanged serialization and retain thread error state");
    content=read(path);std::set<std::pair<unsigned,unsigned>> seen;std::size_t offset{};unsigned lines{};
    while(offset<content.size()){
        const auto end=content.find("\r\n",offset);check(end!=std::string::npos,"every record ends in intact CRLF");
        const auto line=content.substr(offset,end-offset);check(line.starts_with("tick=")&&line.find(" tid=")!=std::string::npos,"each record retains one timestamp/thread prefix");
        const auto event=line.find("event=parallel ");
        if(event!=std::string::npos){unsigned t{},n{};int consumed{};
            check(sscanf_s(line.c_str()+event,"event=parallel worker=%u item=%u%n",&t,&n,&consumed)==2&&
                event+static_cast<std::size_t>(consumed)==line.size()&&t<thread_count&&n<per_thread&&seen.emplace(t,n).second,
                "concurrent record contents are complete, in range and unique without merged bytes");}
        ++lines;offset=end+2;
    }
    check(lines==1+thread_count*per_thread&&seen.size()==thread_count*per_thread,"all concurrently appended lines visible exactly once");
    close_fixture();check(read(path)==content,"closing handle preserves exact cached-write contents");

    const auto child_path=temp_file();check(SetEnvironmentVariableW(L"BOVR_TRACE_FILE",child_path.c_str())!=FALSE,"set independent child log path");
    wchar_t executable[32768]{};const auto count=GetModuleFileNameW(nullptr,executable,32768);
    check(count&&count<32768,"read exact test executable path");
    std::wstring command=L"\""+std::wstring(executable)+L"\" --trace-io-child";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,
        "start only exact isolated test child with no visible window");
    const auto wait=WaitForSingleObject(process.hProcess,5000);DWORD exit=999;
    check(wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&exit)&&exit==0,"normal child exit completes without explicit flush/close");
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    const auto child=read(child_path);
    check(child.find("event=normal_exit marker=complete\r\n")!=std::string::npos,"cached final record survives normal process exit");
    check(SetEnvironmentVariableW(L"BOVR_TRACE_FILE",old_count?old_env:nullptr)!=FALSE,"restore original trace environment");
    check(DeleteFileW(path.c_str())&&DeleteFileW(child_path.c_str()),"remove only two exact fixture-owned temporary files");
    std::printf("trace_io_checks=%u failures=0 cached_write=1 device_flush_calls=%u concurrent_lines=%u\n",checks,flushes.load(),thread_count*per_thread);
}
