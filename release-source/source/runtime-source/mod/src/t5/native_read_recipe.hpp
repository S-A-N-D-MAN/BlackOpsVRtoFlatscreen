// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <intrin.h>
namespace bovr::t5 {
enum class NativeReadFailure : unsigned {none,entry_key,record_cap,record_fault,read_mismatch,replay_fault,owner_changed,nested_capture,allocation,closed};
struct NativeReadDetail {NativeReadFailure reason{};unsigned index{},size{},byte{};std::uintptr_t address{};unsigned expected{},actual{};bool difference_observed{},diagnostic_value_valid{};};
struct NativeReadRecipe {
    struct Span {std::uintptr_t address{};unsigned size{},offset{};};
    static constexpr unsigned span_cap=4096,byte_cap=65536,span_bytes=256;
    std::array<Span,span_cap> spans{};std::array<unsigned char,byte_cap> bytes{};
    unsigned count{},used{};bool failed{};NativeReadDetail detail{};
    void reset() noexcept {count=used=0;failed=false;detail={};}
    void fail(NativeReadFailure why) noexcept {if(!failed)detail.reason=why;failed=true;}
    void append(std::uintptr_t p,const void* value,std::size_t size) noexcept {
        if(failed)return;
        if(!size||size>span_bytes||count>=span_cap||size>byte_cap-used){fail(NativeReadFailure::record_cap);return;}
        spans[count++]={p,static_cast<unsigned>(size),used};std::memcpy(bytes.data()+used,value,size);used+=static_cast<unsigned>(size);
    }
};
// Trivial TLS initialization; no allocator or callback in native-copy recording.
inline thread_local NativeReadRecipe* native_read_recorder{};
template<class T>inline void native_read_copy(std::uintptr_t p,T* out) noexcept {
    if(auto* recorder=native_read_recorder){
        _ReadWriteBarrier();std::memcpy(out,reinterpret_cast<const void*>(p),sizeof(T));_ReadWriteBarrier();
        recorder->append(p,out,sizeof(T));
    }else std::memcpy(out,reinterpret_cast<const void*>(p),sizeof(T));
}
namespace native_recipe_detail {
template<class F>bool capture_finally(NativeReadRecipe& recipe,const F& call) {
    auto* prior=native_read_recorder;
    if(prior){prior->fail(NativeReadFailure::nested_capture);recipe.fail(NativeReadFailure::nested_capture);return false;}
    native_read_recorder=&recipe;bool result=false;
    __try {result=call();}__finally {native_read_recorder=prior;}
    return result;
}
template<class F>bool capture_seh(NativeReadRecipe& recipe,const F& call) {
    __try {return capture_finally(recipe,call);}
    __except(GetExceptionCode()==0xe06d7363?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER){recipe.fail(NativeReadFailure::record_fault);return false;}
}
template<class T>__forceinline bool same(std::uintptr_t address,const unsigned char* expected) noexcept {
    T actual{},old{};std::memcpy(&actual,reinterpret_cast<const void*>(address),sizeof(T));std::memcpy(&old,expected,sizeof(T));return actual==old;
}
__forceinline bool same_span(const NativeReadRecipe::Span& s,const unsigned char* expected) noexcept {
    if(s.size==1)return same<std::uint8_t>(s.address,expected);
    if(s.size==2)return same<std::uint16_t>(s.address,expected);
    if(s.size==4)return same<std::uint32_t>(s.address,expected);
    unsigned offset=0;for(;offset+4<=s.size;offset+=4)if(!same<std::uint32_t>(s.address+offset,expected+offset))return false;
    for(;offset<s.size;++offset)if(!same<std::uint8_t>(s.address+offset,expected+offset))return false;return true;
}
}
template<class F>bool capture_native_reads(NativeReadRecipe& recipe,const F& call) noexcept {
    bool ok=false;try {ok=native_recipe_detail::capture_seh(recipe,call);}catch(...){recipe.fail(NativeReadFailure::record_fault);}
    if(!ok&&!recipe.failed)recipe.fail(NativeReadFailure::record_fault);
    return ok&&!recipe.failed&&recipe.count!=0;
}
// The span barriers prevent the optimizer from reusing duplicate observations
// across iterations. They do not promise atomicity or memcpy's load granularity.
inline bool verify_native_reads(NativeReadRecipe& recipe) noexcept {
    if(recipe.failed||!recipe.count||recipe.count>recipe.span_cap||recipe.used>recipe.byte_cap)return false;
    unsigned current=0;bool ok=true;
    __try {
        for(;current<recipe.count;++current){const auto& s=recipe.spans[current];
            _ReadWriteBarrier();const bool same=native_recipe_detail::same_span(s,recipe.bytes.data()+s.offset);_ReadWriteBarrier();
            if(!same){recipe.fail(NativeReadFailure::read_mismatch);recipe.detail.index=current;recipe.detail.address=s.address;recipe.detail.size=s.size;recipe.detail.difference_observed=true;
                // A bounded diagnostic reread follows refusal only, never adds
                // authority or resumes the failed recipe. It is protected here.
                for(unsigned i=0;i<s.size;++i){unsigned char value{};std::memcpy(&value,reinterpret_cast<const void*>(s.address+i),1);
                    if(value!=recipe.bytes[s.offset+i]){recipe.detail.byte=i;recipe.detail.expected=recipe.bytes[s.offset+i];recipe.detail.actual=value;recipe.detail.diagnostic_value_valid=true;break;}}
                ok=false;break;
            }
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){
        recipe.fail(NativeReadFailure::replay_fault);recipe.detail.index=current;
        if(current<recipe.count){recipe.detail.address=recipe.spans[current].address;recipe.detail.size=recipe.spans[current].size;}ok=false;
    }
    return ok;
}
struct NativeReadPreserve {
    alignas(16) unsigned char fp[512];DWORD error;
    NativeReadPreserve() noexcept {_fxsave(fp);error=GetLastError();}
    ~NativeReadPreserve(){SetLastError(error);_fxrstor(fp);}
};
}
