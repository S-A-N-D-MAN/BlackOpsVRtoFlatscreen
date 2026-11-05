// SPDX-License-Identifier: GPL-3.0-only
#include "t5/cinematic_movie_log_budget.hpp"
#include <cstdio>
#include <cstring>
namespace {unsigned checks{},failures{};void check(bool result,const char* why){++checks;if(!result){++failures;std::printf("FAIL %s\n",why);}}
std::array<char,256> name(const char* p){std::array<char,256> n{};std::memcpy(n.data(),p,std::strlen(p));return n;}}
int main(){using namespace bovr::t5;CinematicMovieLogBudget budget;
    auto invalid=name("");check(!budget.admit(invalid,MovieLogStage::begin),"empty decoder name does not reserve a key");
    invalid.fill('x');check(!budget.admit(invalid,MovieLogStage::begin),"unterminated copied name rejected");
    check(!budget.admit(name("treyarch"),static_cast<MovieLogStage>(4)),"invalid diagnostic stage rejected");
    for(unsigned n=1;n<=120;++n)check(budget.admit(name("treyarch"),MovieLogStage::begin)==(n<=6||n==120),"first movie has six initial and bounded periodic records");
    for(auto stage:{MovieLogStage::begin,MovieLogStage::complete,MovieLogStage::staged,MovieLogStage::present})
        for(unsigned n=1;n<=7;++n)check(budget.admit(name("cuba_load"),stage)==(n<=6),"new short Cuba movie has independent initial records for all stages");
    auto padded=name("cuba_load");padded[100]='X';check(!budget.admit(padded,MovieLogStage::begin),"bytes after terminator cannot replenish key budget");
    for(unsigned i=2;i<16;++i){char key[32]{};std::snprintf(key,sizeof key,"other_%u",i);check(budget.admit(name(key),MovieLogStage::begin),"sixteen immutable keys include TV textures without hiding subsequent Cuba");}
    for(unsigned i=0;i<100;++i)check(!budget.admit(name("seventeenth"),MovieLogStage::begin),"new decoder keys never evict and replenish older counters");
    for(unsigned n=1;n<=7;++n)check(budget.admit(name("cuba_load"),MovieLogStage::begin,1)==(n<=6),"an early rejection does not exhaust a later different gate outcome");
    check(!budget.admit(name("cuba_load"),MovieLogStage::begin,16),"invalid reason cannot index outside fixed diagnostic budget");
    for(unsigned i=0;i<80000;++i)budget.admit(name("cuba_load"),MovieLogStage::complete);
    check(budget.emitted()==512,"absolute global cap reached exactly");
    for(auto stage:{MovieLogStage::begin,MovieLogStage::complete,MovieLogStage::staged,MovieLogStage::present})
        check(!budget.admit(name("treyarch"),stage)&&!budget.admit(name("cuba_load"),stage),"global cap also suppresses a different stage or returning movie");
    std::printf("movie_log_budget_checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
