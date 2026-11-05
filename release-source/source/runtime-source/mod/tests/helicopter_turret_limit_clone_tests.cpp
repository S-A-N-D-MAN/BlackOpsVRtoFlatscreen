// SPDX-License-Identifier: GPL-3.0-only
#include "t5/helicopter_turret_limit_clone.hpp"
#include <cstdio>
#include <limits>

namespace {
namespace limits=bovr::t5::helicopter_turret_limits;
using Info=limits::Info;
unsigned checks{},failures{};
void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
template<class T>void put(Info& data,std::size_t offset,T value){std::memcpy(data.data()+offset,&value,sizeof value);}
template<class T>T get(const Info& data,std::size_t offset){T value{};std::memcpy(&value,data.data()+offset,sizeof value);return value;}
Info original(){
    Info value{};for(std::size_t i=0;i<value.size();++i)value[i]=static_cast<std::uint8_t>((i*37+11)%256);
    put<std::int16_t>(value,0x40,6);
    put<float>(value,0x1ec,35.f);put<float>(value,0x1f0,35.f);
    put<float>(value,0x1f4,10.f);put<float>(value,0x1f8,10.f);put<float>(value,0x1fc,60.f);
    return value;
}
bool unchanged_failure(const Info& a,const Info& b){
    Info out{};out.fill(0xa5);const auto previous=out;
    return !limits::clone(a,b,&out)&&out==previous;
}
}
int main(){
    {
        Info boat=original();put<std::int16_t>(boat,0x40,4);
        put<float>(boat,0x1ec,90.f);put<float>(boat,0x1f0,90.f);
        put<float>(boat,0x1f4,30.f);put<float>(boat,0x1f8,5.f);
        const Info rechecked=boat;Info output{};
        check(limits::clone(boat,rechecked,&output),"recorded PBR turret limits allow headset aiming");
        bool preserved=true;
        for(std::size_t i=0;i<boat.size();++i)if(i<0x1ec||i>=0x1fc)preserved&=boat[i]==output[i];
        check(preserved&&get<float>(output,0x1ec)==179.f&&get<float>(output,0x1f8)==89.f,
              "boat type rate weapon and physics data preserved outside four aim clamps");
    }
    Info first=original(),second=first,out{};const auto before_first=first,before_second=second;
    check(limits::clone(first,second,&out),"two equal independent native helicopter snapshots produce a private clone");
    check(get<float>(out,0x1ec)==179.f&&get<float>(out,0x1f0)==179.f&&
          get<float>(out,0x1f4)==89.f&&get<float>(out,0x1f8)==89.f,
          "only requested yaw179 and pitch89 mechanical spans replace native35/10");
    bool untouched=true;unsigned changed{};
    for(std::size_t i=0;i<first.size();++i) {
        // Independent byte allowlist: four native floats, exactly16 bytes.
        if(i<0x1ec||i>=0x1fc)untouched=untouched&&out[i]==first[i];
        if(out[i]!=first[i])++changed;
    }
    check(untouched&&changed>0&&changed<=16,"all bytes outside the exact16-byte clamp span are preserved");
    check(get<float>(out,0x1fc)==60.f&&get<std::int16_t>(out,0x40)==6,
          "native rotation rate and vehicle type remain exact");
    check(first==before_first&&second==before_second,"source snapshots remain unmodified on success");
    // Separate invocations derive from their current copies, never a retained
    // global-info substitution. This helper owns no shared pointer or lifetime.
    second=first;put<float>(first,0x1fc,80.f);put<float>(second,0x1fc,80.f);
    first.back()^=0x40;second.back()^=0x40;
    check(limits::clone(first,second,&out)&&get<float>(out,0x1fc)==80.f&&out.back()==first.back(),
          "new native rate and unrelated tail data are preserved in a later independent clone");
    for(std::size_t offset:{std::size_t(0),std::size_t(0x40),std::size_t(0x100),std::size_t(0x1ec),
        std::size_t(0x1f8),std::size_t(0x1fc),std::size_t(0x900),std::size_t(0x1dc7)}) {
        first=original();second=first;second[offset]^=1;const auto a=first,b=second;
        check(unchanged_failure(first,second)&&first==a&&second==b,
              "drift anywhere in the complete header/rate/limits/tail rejects with output and inputs untouched");
    }
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    for(unsigned offset:{0x1ecu,0x1f0u,0x1f4u,0x1f8u}) {
        for(float invalid:{0.f,-0.f,-1.f,nan,inf,-inf,std::nextafter(180.f,inf)}) {
            first=original();put(first,offset,invalid);second=first;
            check(unchanged_failure(first,second),"each original limit must be finite and in the positive <=180 degree domain");
        }
        for(float valid:{.001f,180.f}) {
            first=original();put(first,offset,valid);second=first;
            check(limits::clone(first,second,&out),"valid positive minimum and exact180 boundary accepted");
        }
    }
    for(std::int16_t type:{std::int16_t(-1),std::int16_t(0),std::int16_t(5),std::int16_t(7)}) {
        first=original();put(first,0x40,type);second=first;
        check(unchanged_failure(first,second),"unsupported driver type rejected");
    }
    first=original();second=first;const auto input=first;out.fill(0xa5);const auto untouched_output=out;
    check(!limits::clone(first,second,nullptr)&&first==input&&second==input,"null output refuses without input changes");
    check(!limits::clone(first,first,&out)&&out==untouched_output&&first==input,
          "same input storage cannot masquerade as an independent recheck");
    check(!limits::clone(first,second,&first)&&first==input&&second==input,
          "output aliasing first snapshot is rejected before mutation");
    check(!limits::clone(first,second,&second)&&first==input&&second==input,
          "output aliasing rechecked snapshot is rejected before mutation");
    // Check range arithmetic without constructing overlapping C++ Info objects.
    std::array<std::uint8_t,sizeof(Info)*2+1> storage{};
    const auto* a=storage.data();
    check(limits::detail::disjoint(a,a+sizeof(Info))&&limits::detail::disjoint(a+sizeof(Info),a),
          "adjacent full-size info copies remain disjoint in either address order");
    check(!limits::detail::disjoint(a,a+sizeof(Info)-1)&&!limits::detail::disjoint(a+sizeof(Info)-1,a)&&
          !limits::detail::disjoint(a,a+1)&&!limits::detail::disjoint(a,a),
          "partial and exact overlapping spans are rejected before any byte access");
    std::printf("helicopter turret limit clone: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
